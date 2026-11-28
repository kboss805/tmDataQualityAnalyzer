/**
 * @file calibrationextractor.cpp
 * @brief Implementation of CalibrationExtractor (US5.3).
 */

#include "calibrationextractor.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>

#include "ch10packetreader.h"
#include "constants.h"
#include "containerutil.h"
#include "exceptionboundary.h"
#include "frameprocessor.h"
#include "framesetup.h"
#include "packetqueue.h"
#include "processedstreamdata.h"
#include "receiverparamsfile.h"
#include "stepdetector.h"
#include "textformat.h"

/// What the packet reader and the frame processor report to. Their log lines and errors pass
/// through to the extractor's own sink; the reader's progress does too; the worker's completion
/// is only noted, because the driver thread is the one that waits for it and acts on it.
class CalibrationExtractor::Relay final : public ProcessingEvents
{
public:
    explicit Relay(CalibrationExtractor& owner) : m_owner(owner) {}

    void logMessage(const std::string& message) override
    {
        m_owner.m_report.log(message);
    }

    void errorOccurred(const std::string& message) override
    {
        // Kept so the end can report the worker's own reason (e.g. the "no frame sync found" line, which names
        // the pattern and the bits per frame) instead of a generic one, and passed on as a log line like any other.
        {
            std::lock_guard<std::mutex> lock(m_owner.m_mutex);
            m_owner.m_error = message;
        }
        m_owner.m_report.log(message);
    }

    void processingFinished(bool success) override
    {
        m_owner.m_worker_ok = success;
    }

    void progressUpdated(int percent) override
    {
        m_owner.m_report.progress(percent);
    }

private:
    CalibrationExtractor& m_owner;
};

CalibrationExtractor::CalibrationExtractor()
    : m_relay(std::make_shared<Relay>(*this))
{
}

CalibrationExtractor::~CalibrationExtractor()
{
    cancel();
    joinDriver();
}

void CalibrationExtractor::joinDriver()
{
    if (m_driver.joinable())
    {
        m_driver.join();
    }
}

void CalibrationExtractor::wait()
{
    joinDriver();
}

std::string CalibrationExtractor::error() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_error;
}

void CalibrationExtractor::start(const Request& request, std::shared_ptr<ProcessingEvents> events)
{
    if (m_running.exchange(true))
    {
        return;
    }
    // Everything after the claim runs inside a boundary: a start that throws (a path the code page cannot spell, memory,
    // a thread that will not start) fails like any other early failure - the reason, then the end, with what was built
    // released and the claim with it - or this extractor would ignore every start after it.
    containExceptions([&]() { startRun(request, std::move(events)); },
                      [&](const char* what) {
                          const std::string reason = std::format("Calibration extraction could not start: {}.", what);
                          {
                              std::lock_guard<std::mutex> lock(m_mutex);
                              m_error = reason;
                          }
                          finish(false, reason, true);
                      });
}

void CalibrationExtractor::startRun(const Request& request, std::shared_ptr<ProcessingEvents> events)
{
    // A previous run's driver may still be returning from its last report.
    joinDriver();

    m_report.setSink(std::move(events));
    m_cancelled = false;
    m_worker_ok = false;
    m_thrown = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error.clear();
        m_thrown_reason.clear();
    }
    m_summary.clear();
    m_results.clear();
    m_steps = request.steps;
    m_clip_start_sec = std::max(0.0, request.clipStartSec);
    m_clip_end_sec   = std::max(0.0, request.clipEndSec);
    m_clip_ignored   = false;
    m_recording_sec  = 0.0;
    m_sample_period_sec = CalibrationConstants::kExtractSamplePeriodSec;

    // A failure before any work starts: record it, release what was built, and report.
    auto fail = [this](const std::string& reason) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_error = reason;
        }
        finish(false, reason, true);
    };

    if (request.steps.empty())
    {
        fail("No calibration steps were provided.");
        return;
    }

    // ---- Word map (unit slope, zero offset -> raw counts out) ----
    const int words_in_minor_frame = FrameSetup::wordsInMinorFrame(request.acquisition.bitsInMinorFrame);
    std::string setup_error;
    if (!buildFrameSetup(request, words_in_minor_frame, setup_error))
    {
        fail(setup_error);
        return;
    }

    // ---- ProcessingParams ----
    m_params = ProcessingParams{};
    m_params.filename          = request.calFilename;
    m_params.timeChannelId     = request.timeChannelId;
    m_params.pcmChannelId      = request.pcmChannelId;
    // Every raw-affecting field, set in the one place the main run also sets them: the extractor
    // must see the same bitstream the main run will.
    applyAcquisition(m_params, request.acquisition);
    m_params.mode              = StreamMode::ReceiverChannelInfo;
    m_params.streamLabel       = "Calibration";
    m_params.samplePeriodSec   = m_sample_period_sec;
    // Measure steps over elapsed stream time from zero, independent of the cal
    // file's IRIG time (which may be large/absent and would otherwise derail the
    // per-period sample windowing).
    m_params.useDataRateClock  = true;

    // ---- Queue + reader ----
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue = std::make_unique<PacketQueue>();
        m_params.packetQueue = m_queue.get();
        m_reader = std::make_shared<Ch10PacketReader>();
    }
    std::vector<ProcessingParams*> params_list{ &m_params };
    std::string error;
    if (!m_reader->prepare(request.calFilename, request.timeChannelId, params_list, error))
    {
        fail(error);
        return;
    }

    // Size the extraction window to average kFramesPerExtractWindow frames, then
    // clamp it to the range that detects reliably. The reader has just resolved
    // the per-bit period (from the user's data rate or TMATS) into resolvedAttrs.
    //
    // Both bounds are load-bearing and neither is arbitrary - see the constants,
    // which carry the measured calibrated-channel counts. Finer windows are worse,
    // not better: they resolve each dwell's settling ramp into sub-plateaus.
    const double bit_period_sec = m_params.resolvedAttrs.delta100ns * PCMConstants::k100NsToSeconds;
    if (bit_period_sec > 0.0 && m_params.bitsInMinorFrame > 0)
    {
        const double frame_duration_sec =
            static_cast<double>(m_params.bitsInMinorFrame) * bit_period_sec;
        const double period =
            std::clamp(CalibrationConstants::kFramesPerExtractWindow * frame_duration_sec,
                       CalibrationConstants::kMinAdaptiveExtractPeriodSec,
                       CalibrationConstants::kMaxExtractSamplePeriodSec);
        m_sample_period_sec      = period;
        m_params.samplePeriodSec = period;
        m_report.log(std::format("Calibration extraction sample period: {} ms "
                                 "(~{} frames/window at {}-bit frames).",
                                 textformat::formatFixed(period * 1000.0, 2),
                                 textformat::formatFixed(period / frame_duration_sec, 0),
                                 m_params.bitsInMinorFrame));
    }

    // ---- Worker ----
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_worker = std::make_shared<FrameProcessor>();
        m_worker->setEvents(m_relay);
        m_reader->setEvents(m_relay);
    }

    // The reader, the worker and the wait for both happen on one driver thread.
    m_driver = std::thread([this]() { drive(); });
}

void CalibrationExtractor::cancel()
{
    if (!m_running)
    {
        return;
    }
    m_cancelled = true;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_reader != nullptr)
    {
        m_reader->requestAbort();
    }
    if (m_worker != nullptr)
    {
        m_worker->requestAbort();
    }
    if (m_queue != nullptr)
    {
        m_queue->close();
    }
}

void CalibrationExtractor::drive()
{
    std::shared_ptr<Ch10PacketReader> reader;
    std::shared_ptr<FrameProcessor> worker;
    PacketQueue* queue = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        reader = m_reader;
        worker = m_worker;
        queue  = m_queue.get();
    }
    // Nothing thrown on either thread may leave it: an exception out of a thread's function ends the host process. A
    // throw stops the extraction instead, which then fails with the reason, as any other failure does.
    std::thread reader_thread;
    const bool completed = containExceptions(
        [&]() { driveRun(reader, worker, queue, reader_thread); },
        [&](const char* what) {
            noteThrown(std::format("Calibration extraction stopped on an internal error ({}).", what));
        });
    if (completed)
    {
        return;
    }
    // A throw on this thread (the worker, step detection, or a reader thread that would not start): let the reader go,
    // wait for it, and end the run once, with no results (step detection may have stopped part-way through them).
    reader->requestAbort();
    queue->close();
    if (reader_thread.joinable())
    {
        reader_thread.join();
    }
    m_results.clear();
    finish(false, adoptThrownReason(), true);
}

void CalibrationExtractor::noteThrown(const std::string& reason)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_thrown)   // the first is the cause; a throw after it is a consequence
    {
        m_thrown_reason = reason;
        m_thrown = true;
    }
}

std::string CalibrationExtractor::adoptThrownReason()
{
    // The worker reports its own failure when the queue it was reading closes early ("no frame sync found"); the throw
    // that closed it is the reason the run failed.
    std::lock_guard<std::mutex> lock(m_mutex);
    m_error = m_thrown_reason;
    return m_error;
}

void CalibrationExtractor::driveRun(const std::shared_ptr<Ch10PacketReader>& reader,
                                    const std::shared_ptr<FrameProcessor>& worker, PacketQueue* queue,
                                    std::thread& reader_thread)
{
    const ProcessingParams params = m_params;
    FrameSetup* setup = m_frame_setup.get();

    // The reader gets a thread of its own; the worker runs here, on the driver thread, which
    // would otherwise only wait for it. A reader that throws lets the worker go (it waits on the queue) and fails the
    // run (m_thrown, set first): the worker would otherwise take the part read so far for the whole recording.
    reader_thread = std::thread([this, reader, queue]() {
        containExceptions([&]() { reader->run(); },
                          [&](const char* what) {
                              noteThrown(std::format("Reading the calibration recording stopped on an internal "
                                                     "error ({}).", what));
                              queue->close();
                          });
    });

    worker->process(params, setup);
    if (!m_worker_ok || m_cancelled)
    {
        // The worker is done with the queue; a reader still blocked on a full one must be let go.
        reader->requestAbort();
        queue->close();
    }
    reader_thread.join();

    if (m_worker_ok && !m_cancelled && !m_thrown)
    {
        const ProcessedStreamData data = worker->takeResult();
        const int calibrated = buildResults(data);

        if (m_clip_ignored)
        {
            m_report.log(std::format("Clip Start + Clip End ({} s) exceed the recording ({} s); "
                                     "both were ignored and the whole recording was used.",
                                     textformat::formatFixed(m_clip_start_sec + m_clip_end_sec, 1),
                                     textformat::formatFixed(m_recording_sec, 1)));
        }
        finish(true, std::format("Calibrated {} of {} channel(s).", calibrated, intSize(m_results)),
               false);
        return;
    }

    // Failure or cancellation path.
    if (m_cancelled)
    {
        finish(false, "Calibration extraction cancelled.", false);
        return;
    }
    const std::string reason = m_thrown ? adoptThrownReason() : error();
    finish(false, reason.empty() ? std::string("Calibration extraction failed.") : reason, true);
}

int CalibrationExtractor::buildResults(const ProcessedStreamData& data)
{
    int calibrated = 0;
    for (const ProcessedChannelSeries& ch : data.channels)
    {
        CalibrationChannelResult res;
        res.name    = ch.name;
        res.word    = ch.word;
        res.hadData = !ch.values.empty();

        if (res.hadData)
        {
            // Clip the user-specified leading/trailing seconds before step
            // detection, so signal-generator turn-on transients (and any
            // trailing junk) don't get mistaken for cal plateaus. The series
            // is sampled every m_sample_period_sec, so seconds -> sample count.
            const std::vector<double>& full = ch.values;
            const int count = intSize(full);
            int lo = 0;
            int hi = count;
            if (m_sample_period_sec > 0.0)
            {
                lo = std::clamp(static_cast<int>(m_clip_start_sec / m_sample_period_sec), 0, count);
                hi = count - std::clamp(static_cast<int>(m_clip_end_sec / m_sample_period_sec), 0, count);
            }
            const std::vector<double> clipped =
                (lo < hi) ? std::vector<double>(full.begin() + lo, full.begin() + hi)
                          : full; // ignore an over-aggressive clip
            m_recording_sec = count * m_sample_period_sec;
            if (lo >= hi && (m_clip_start_sec > 0.0 || m_clip_end_sec > 0.0))
            {
                m_clip_ignored = true;
            }

            static_cast<StepDetector::Result&>(res) =
                StepDetector::detect(clipped, m_sample_period_sec, m_steps);
            if (res.profile.valid)
            {
                calibrated++;
            }

            // Surface the per-channel plateau count: a channel that detects
            // fewer plateaus than expected steps (near-saturation receivers
            // merge adjacent low steps, e.g. 0 dB and 3 dB) builds a profile
            // whose raw->dB pairing is shifted. Applied to the main run that
            // mis-mapping makes the channel climb at a different rate, which
            // shows up as the calibrated steps no longer lining up in time.
            std::string note;
            if (!res.profile.valid)
            {
                note = std::format(" — {}, edge threshold {} raw counts",
                                   calibrationOutcomeText(res.outcome),
                                   textformat::formatFixed(res.edgeThreshold, 1));
            }
            else if (res.saturated)
            {
                // Calibrated, but only over part of its sweep. Say which part:
                // the profile clamps above it, so a reading at that dB may mean
                // "this or anything higher".
                note = std::format(" — top step(s) railed; calibrated {} of {} step(s), "
                                   "readings hold at {} dB",
                                   intSize(res.profile.points), intSize(m_steps),
                                   textformat::formatFixed(topResolvedDb(res.profile), 0));
            }
            else if (res.thresholdRelaxed)
            {
                note = std::format(" — calibrated with a relaxed edge threshold ({} raw counts)",
                                   textformat::formatFixed(res.edgeThreshold, 1));
            }
            m_report.log(std::format("  {} (word {}): {} plateau(s) detected, {} step(s) expected{}{}",
                                     res.name, res.word, res.detectedPlateaus, intSize(m_steps),
                                     res.detectedPlateaus != intSize(m_steps) ? " — MISMATCH" : "", note));
        }
        m_results.push_back(res);
    }
    return calibrated;
}

void CalibrationExtractor::finish(bool success, const std::string& summary, bool is_error)
{
    m_summary = summary;
    {
        // Every thread that used the pipeline has been joined, so this is the last reference.
        std::lock_guard<std::mutex> lock(m_mutex);
        m_worker.reset();
        m_reader.reset();
        // m_params.packetQueue aliases the queue freed here; clear it so an early prepare()
        // failure can't leave a dangling pointer behind in m_params.
        m_params.packetQueue = nullptr;
        m_queue.reset();
    }
    // Before the report, so running() reads false once the end is reported. A handler that starts the next run from inside the
    // report is still refused ("could not start"): start() joins the driver thread, which is this one. The hosts start the
    // next run from their own thread (one runner per run, through the ABI).
    m_running = false;
    // Often on the driver thread, as the last thing it does: a sink that throws must not take the host down with it.
    containExceptions(
        [&]() {
            if (is_error)
            {
                m_report.error(summary);
            }
            m_report.finished(success);
        },
        [](const char*) {});
}

bool CalibrationExtractor::buildFrameSetup(const Request& request,
                                           int wordsInMinorFrame,
                                           std::string& error)
{
    m_frame_setup = std::make_unique<FrameSetup>();

    // Resolved exactly as the main run resolves it (resolveReceiverWordMap): the profiles are matched to its channels
    // by word.
    const std::filesystem::path none;
    const std::filesystem::path& named = request.receiverParamsIsDefault ? none : request.receiverParamsToml;
    const std::filesystem::path& fallback = request.receiverParamsIsDefault ? request.receiverParamsToml : none;
    WordMapResolution map;
    if (!resolveReceiverWordMap(*m_frame_setup, named, fallback, request.numReceivers, request.receiverChannels,
                                wordsInMinorFrame, map))
    {
        error = map.error;
        return false;
    }
    if (!map.warning.empty())
    {
        m_report.log("Calibration: " + map.warning);
    }

    // Unit slope / zero offset so FrameProcessor emits raw counts (averaged per
    // window) rather than calibrated dB.
    for (int i = 0; i < m_frame_setup->length(); i++)
    {
        ParameterInfo* p = m_frame_setup->getParameter(i);
        p->slope      = 1.0;
        p->scale      = 0.0;
        p->profile    = CalibrationProfile{}; // ensure linear (raw) extraction
    }
    return true;
}
