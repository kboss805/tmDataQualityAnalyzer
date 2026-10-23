/**
 * @file calibrationextractor.cpp
 * @brief Implementation of CalibrationExtractor (US5.3).
 */

#include "calibrationextractor.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <system_error>
#include <utility>

#include "ch10packetreader.h"
#include "constants.h"
#include "containerutil.h"
#include "frameprocessor.h"
#include "framesetup.h"
#include "packetqueue.h"
#include "processedstreamdata.h"
#include "stepdetector.h"
#include "textformat.h"

namespace {

/// Highest dB a profile actually measured. Points are sorted by raw count and an
/// inverted-polarity receiver's count falls as dB climbs, so the top dB can sit
/// at either end - take the maximum rather than the last point.
double topResolvedDb(const CalibrationProfile& profile)
{
    double top = 0.0;
    for (const CalibrationPoint& point : profile.points)
    {
        top = std::max(top, point.trueDb);
    }
    return top;
}

/// The strings of @p parts separated by @p separator.
std::string joinStrings(const std::vector<std::string>& parts, const std::string& separator)
{
    std::string joined;
    for (std::size_t i = 0; i < parts.size(); i++)
    {
        if (i > 0)
        {
            joined += separator;
        }
        joined += parts[i];
    }
    return joined;
}

} // namespace

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
        // Kept so the end can report the worker's own reason (e.g. "Frame sync pattern was not
        // found...") instead of a generic one, and passed on as a log line like any other.
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
    // A previous run's driver may still be returning from its last report.
    joinDriver();

    m_report.setSink(std::move(events));
    m_cancelled = false;
    m_worker_ok = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error.clear();
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
    m_params.startSeconds      = 0;
    m_params.stopSeconds       = UINT64_MAX; // whole file
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
    const double bit_period_sec = m_params.resolvedAttrs.delta100ns * 1e-7;
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
    const ProcessingParams params = m_params;
    FrameSetup* setup = m_frame_setup.get();

    // The reader gets a thread of its own; the worker runs here, on the driver thread, which
    // would otherwise only wait for it.
    std::thread reader_thread([reader]() { reader->run(); });

    worker->process(params, setup);
    if (!m_worker_ok || m_cancelled)
    {
        // The worker is done with the queue; a reader still blocked on a full one must be let go.
        reader->requestAbort();
        queue->close();
    }
    reader_thread.join();

    if (m_worker_ok && !m_cancelled)
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
    const std::string reason = error();
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
    // Before the report, so a handler that starts the next run is not turned away.
    m_running = false;
    if (is_error)
    {
        m_report.error(summary);
    }
    m_report.finished(success);
}

bool CalibrationExtractor::buildFrameSetup(const Request& request,
                                           int wordsInMinorFrame,
                                           std::string& error)
{
    m_frame_setup = std::make_unique<FrameSetup>();

    if (request.receiverParamsToml.empty())
    {
        if (!m_frame_setup->buildDefaultReceiverMap(request.numReceivers, request.receiverChannels,
                                                    wordsInMinorFrame, error))
        {
            return false;
        }
    }
    else if (std::error_code ec; !std::filesystem::exists(request.receiverParamsToml, ec))
    {
        error = "Receiver Parameters file '" + request.receiverParamsToml.filename().string() +
                "' was not found.";
        return false;
    }
    else if (!m_frame_setup->tryLoadingFile(request.receiverParamsToml, wordsInMinorFrame))
    {
        error = "Failed to load Receiver Parameters word map. Check the file.";
        return false;
    }

    if (m_frame_setup->length() == 0)
    {
        error = "Receiver Parameters file contains no parameters.";
        return false;
    }

    // Unit slope / zero offset so FrameProcessor emits raw counts (averaged per
    // window) rather than calibrated dB.
    for (int i = 0; i < m_frame_setup->length(); i++)
    {
        ParameterInfo* p = m_frame_setup->getParameter(i);
        p->slope      = 1.0;
        p->scale      = 0.0;
        p->is_enabled = true;
        p->sample_sum = 0.0;
        p->profile    = CalibrationProfile{}; // ensure linear (raw) extraction
    }
    return true;
}

// ---------------------------------------------------------------------------
// summarize
// ---------------------------------------------------------------------------

std::string CalibrationExtractor::summarize(const std::vector<CalibrationChannelResult>& results,
                                            int expectedSteps)
{
    if (results.empty())
    {
        return "No receiver channels were extracted.";
    }

    // Group channels by receiver. Receiver 0 collects anything whose name carries
    // no "_RCVR<N>" suffix (a hand-written receiver-params TOML may name its
    // parameters anything); those are reported by name instead of by number.
    struct Group
    {
        std::vector<std::string> calibrated;           ///< Channel parts that succeeded.
        std::vector<std::string> relaxed;              ///< …of those, ones needing a lower threshold.
        std::vector<std::string> saturated;            ///< …of those, ones whose top steps railed.
        int         recordingLevels = 0;               ///< Most levels a calibrated channel's sweep
                                                       ///< held, when well beyond the step file's.
        double      topDb = 0.0;                       ///< Highest dB those actually measured.
        std::vector<double> unresolved;                ///< dB no level could be paired with.
        std::map<CalibrationOutcome, std::vector<std::string>> failed;  ///< Channel parts per failure reason.
        std::vector<std::string> flatLevels;           ///< Stuck levels of flat channels.
        int minPlateaus = -1;                          ///< Fewest plateaus seen among failures.
    };
    std::map<int, Group> byReceiver;

    int calibratedChannels = 0;
    for (const CalibrationChannelResult& r : results)
    {
        const int receiver = FrameSetup::receiverIndexFromName(r.name);
        const std::string part = (receiver > 0) ? FrameSetup::channelPartOfName(r.name) : r.name;
        Group& g = byReceiver[receiver];

        if (r.outcome == CalibrationOutcome::Calibrated)
        {
            g.calibrated.push_back(part);
            if (r.sweepLevels - expectedSteps > CalibrationConstants::kMaxLeadInLevels)
            {
                g.recordingLevels = std::max(g.recordingLevels, r.sweepLevels);
            }
            if (r.thresholdRelaxed)
            {
                g.relaxed.push_back(part);
            }
            if (r.saturated)
            {
                g.saturated.push_back(part);
                g.topDb = std::max(g.topDb, topResolvedDb(r.profile));
                for (double db : r.unresolvedDb)
                {
                    if (std::find(g.unresolved.begin(), g.unresolved.end(), db) == g.unresolved.end())
                    {
                        g.unresolved.push_back(db);
                    }
                }
            }
            calibratedChannels++;
        }
        else
        {
            g.failed[r.outcome].push_back(part);
            if (r.hadData && (g.minPlateaus < 0 || r.detectedPlateaus < g.minPlateaus))
            {
                g.minPlateaus = r.detectedPlateaus;
            }
            if (r.outcome == CalibrationOutcome::FlatNoSignal && !r.plateauLevels.empty())
            {
                const std::string level = textformat::formatFixed(r.plateauLevels.front(), 0);
                if (std::find(g.flatLevels.begin(), g.flatLevels.end(), level) == g.flatLevels.end())
                {
                    g.flatLevels.push_back(level);
                }
            }
        }
    }

    // Receivers whose every channel calibrated collapse to a bare number list -
    // the common good case should read as one short line, not one line each.
    // Channels with no receiver number are one group of their own; when every one of
    // them calibrated they are listed by name beside that line, not under the fallback
    // heading (they did not fall back).
    std::vector<std::string> fullyCalibrated;
    std::vector<std::string> unnamedCalibrated;
    std::vector<std::string> detailLines;
    std::vector<std::string> relaxedLines;
    std::vector<std::string> saturatedLines;
    std::vector<std::string> stepFileLines;
    std::vector<std::string> wordMapLines;
    for (const auto& [receiver, g] : byReceiver)
    {
        const std::string label = (receiver > 0) ? std::format("Receiver {}", receiver)
                                                 : std::string("Unnamed receiver");

        if (g.recordingLevels > 0)
        {
            stepFileLines.push_back(std::format("  {} — {} levels in the recording, {} in the step file",
                                                label, g.recordingLevels, expectedSteps));
        }

        // One channel swept and another never moved: the signature a word map that
        // does not match the recording leaves, because it files one receiver's words
        // under another's name. A genuinely dead channel looks the same, so this is
        // a prompt to check, not a verdict.
        const auto flat = g.failed.find(CalibrationOutcome::FlatNoSignal);
        if (receiver > 0 && !g.calibrated.empty() && flat != g.failed.end())
        {
            wordMapLines.push_back(std::format("  {} — {} calibrated; {} never moved",
                                               label, joinStrings(g.calibrated, ", "),
                                               joinStrings(flat->second, ", ")));
        }

        if (!g.relaxed.empty())
        {
            relaxedLines.push_back(std::format("  {} — {}", label, joinStrings(g.relaxed, ", ")));
        }

        if (!g.saturated.empty())
        {
            std::vector<std::string> unresolvedText;
            for (double db : g.unresolved)
            {
                unresolvedText.push_back(textformat::formatFixed(db, 0));
            }
            saturatedLines.push_back(std::format(
                "  {} — {}: readings hold at {} dB{}",
                label, joinStrings(g.saturated, ", "), textformat::formatFixed(g.topDb, 0),
                unresolvedText.empty()
                    ? std::string()
                    : std::format(" (no measurement for {} dB)", joinStrings(unresolvedText, ", "))));
        }

        if (g.failed.empty())
        {
            if (receiver > 0)
            {
                fullyCalibrated.push_back(std::to_string(receiver));
            }
            else
            {
                unnamedCalibrated = g.calibrated;
            }
            continue;
        }

        // Mixed or wholly failed: say which channels fell back and why. A partly
        // calibrated receiver is the case most worth spelling out, because the
        // count alone would let it hide inside "N of M".
        std::vector<std::string> reasons;
        for (const auto& [outcome, channels] : g.failed)
        {
            std::string reason = std::format("{}: {}", joinStrings(channels, ", "),
                                             calibrationOutcomeText(outcome));
            if (outcome == CalibrationOutcome::TooFewPlateaus && g.minPlateaus >= 0)
            {
                reason += std::format(" ({} of {} steps resolved)", g.minPlateaus, expectedSteps);
            }
            else if (outcome == CalibrationOutcome::FlatNoSignal && !g.flatLevels.empty())
            {
                // The level itself is the diagnosis: 0 is a dead input, full
                // scale is a railed one, and anything between is a stuck DC
                // level. "Flat" alone would leave the operator guessing which.
                reason += std::format(" (held at {})", joinStrings(g.flatLevels, ", "));
            }
            reasons.push_back(reason);
        }

        const std::string calibratedPart =
            g.calibrated.empty()
                ? std::string()
                : std::format("{} calibrated; ", joinStrings(g.calibrated, ", "));
        detailLines.push_back(std::format("  {} — {}{}", label, calibratedPart,
                                          joinStrings(reasons, "; ")));
    }

    std::vector<std::string> out;
    out.push_back(std::format("Applied non-linear calibration to {} of {} channel(s).",
                              calibratedChannels, intSize(results)));

    if (!fullyCalibrated.empty() || !unnamedCalibrated.empty())
    {
        out.push_back("");
        if (!fullyCalibrated.empty())
        {
            out.push_back(std::format("Calibrated: receiver{} {}.",
                                      fullyCalibrated.size() == 1 ? "" : "s",
                                      joinStrings(fullyCalibrated, ", ")));
        }
        if (!unnamedCalibrated.empty())
        {
            out.push_back(std::format("Calibrated (unnamed channels): {}.",
                                      joinStrings(unnamedCalibrated, ", ")));
        }
    }

    // The two checks below come before the fallback list on purpose: they flag
    // results that LOOK successful but may be wrong, which the operator would
    // otherwise only discover from the plot.
    if (!stepFileLines.empty())
    {
        out.push_back("");
        out.push_back("Check the step file — these receivers stepped through more levels "
                      "than it lists, so the lowest were set aside as lead-in. If those "
                      "are real steps, every step is paired with the wrong level:");
        out.insert(out.end(), stepFileLines.begin(), stepFileLines.end());
    }

    if (!wordMapLines.empty())
    {
        out.push_back("");
        out.push_back("Check the word map — in these receivers one channel swept and "
                      "another never moved. A word map that does not match the recording "
                      "files one receiver's words under another's name; otherwise a "
                      "channel may be dead:");
        out.insert(out.end(), wordMapLines.begin(), wordMapLines.end());
    }

    if (!detailLines.empty())
    {
        out.push_back("");
        out.push_back("Fell back to linear calibration:");
        out.insert(out.end(), detailLines.begin(), detailLines.end());
    }

    // Calibrated, but the receiver ran out of range partway up the sweep, so the
    // profile stops there and clamps above it. Reported because the plot then
    // cannot rise past that step no matter how strong the signal gets - which
    // reads as a ceiling in the data rather than as a receiver setting to fix.
    if (!saturatedLines.empty())
    {
        out.push_back("");
        out.push_back("Calibrated only up to a saturated top — these receivers ran out "
                      "of range partway through the sweep, so their readings hold at "
                      "the highest step still measurable:");
        out.insert(out.end(), saturatedLines.begin(), saturatedLines.end());
    }

    // Calibrated, but only after loosening the edge threshold: the steps sat close
    // to this channel's own noise. Reported because it is the early warning the
    // count cannot give — these receivers calibrated, yet they are the ones
    // drifting toward the point where they will stop calibrating at all.
    if (!relaxedLines.empty())
    {
        out.push_back("");
        out.push_back("Calibrated only with a relaxed threshold — steps are close to "
                      "the noise floor, so these receivers are worth checking:");
        out.insert(out.end(), relaxedLines.begin(), relaxedLines.end());
    }

    return joinStrings(out, "\n");
}
