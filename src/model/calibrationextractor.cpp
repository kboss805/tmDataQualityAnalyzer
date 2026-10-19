/**
 * @file calibrationextractor.cpp
 * @brief Implementation of CalibrationExtractor (US5.3).
 */

#include "calibrationextractor.h"

#include <algorithm>
#include <format>
#include <map>
#include <string>

#include <QFileInfo>
#include <QThread>

#include "ch10packetreader.h"
#include "constants.h"
#include "containerutil.h"
#include "frameprocessor.h"
#include "framesetup.h"
#include "packetqueue.h"
#include "processedstreamdata.h"
#include "qtbridge.h"
#include "qtprocessingevents.h"
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

CalibrationExtractor::CalibrationExtractor(QObject* parent)
    : QObject(parent)
{
}

CalibrationExtractor::~CalibrationExtractor()
{
    teardown();
    delete m_frame_setup;
}

void CalibrationExtractor::start(const Request& request)
{
    if (m_running)
    {
        return;
    }
    m_running   = true;
    m_cancelled = false;
    m_error.clear();
    m_results.clear();
    m_steps = request.steps;
    m_clip_start_sec = qMax(0.0, request.clipStartSec);
    m_clip_end_sec   = qMax(0.0, request.clipEndSec);
    m_clip_ignored   = false;
    m_recording_sec  = 0.0;
    m_sample_period_sec = CalibrationConstants::kExtractSamplePeriodSec;

    if (request.steps.empty())
    {
        finishWithError("No calibration steps were provided.");
        return;
    }

    // ---- Frame sync numeric values ----
    bool sync_ok = false;
    const uint64_t frame_sync = request.sync.pattern.toULongLong(&sync_ok, UIConstants::kHexBase);
    if (!sync_ok || request.sync.pattern.isEmpty())
    {
        finishWithError("Invalid frame sync pattern '" + request.sync.pattern + "'.");
        return;
    }
    const int sync_pattern_length = static_cast<int>(request.sync.pattern.length()) * 4;

    uint64_t frame_sync_mask = 0;
    if (request.sync.mask.isEmpty())
    {
        frame_sync_mask = (sync_pattern_length > 0 && sync_pattern_length < 64)
            ? (1ULL << sync_pattern_length) - 1
            : 0xFFFFFFFFFFFFFFFFULL;
    }
    else
    {
        bool mask_ok = false;
        frame_sync_mask = request.sync.mask.toULongLong(&mask_ok, UIConstants::kHexBase);
        if (!mask_ok)
        {
            finishWithError("Invalid frame sync mask '" + request.sync.mask + "'.");
            return;
        }
    }

    const int words_in_minor_frame =
        FrameSetup::wordsInMinorFrame(request.sync.bitsInMinorFrame);

    // ---- Word map (unit slope, zero offset -> raw counts out) ----
    QString setup_error;
    if (!buildFrameSetup(request, words_in_minor_frame, setup_error))
    {
        finishWithError(setup_error);
        return;
    }

    // ---- ProcessingParams ----
    m_params = ProcessingParams{};
    m_params.filename          = toFsPath(request.calFilename);
    m_params.timeChannelId     = request.timeChannelId;
    m_params.pcmChannelId      = request.pcmChannelId;
    m_params.frameSync         = frame_sync;
    m_params.frameSyncMask     = frame_sync_mask;
    m_params.syncPatternLength = sync_pattern_length;
    m_params.wordsInMinorFrame = words_in_minor_frame;
    m_params.bitsInMinorFrame  = request.sync.bitsInMinorFrame;
    m_params.isRandomized      = request.sync.randomized;
    m_params.isInverted        = request.sync.inverted;
    // Raw-affecting, so it belongs with isRandomized/isInverted above: the
    // extractor must see the same bitstream the main run will.
    m_params.swapBytes         = request.swapBytes;
    m_params.mode              = StreamMode::ReceiverChannelInfo;
    m_params.dataRateBps       = (request.sync.dataRateMbps > 0.0) ? request.sync.dataRateMbps * 1e6 : 0.0;
    m_params.streamLabel       = "Calibration";
    m_params.samplePeriodSec   = m_sample_period_sec;
    m_params.startSeconds      = 0;
    m_params.stopSeconds       = UINT64_MAX; // whole file
    // Measure steps over elapsed stream time from zero, independent of the cal
    // file's IRIG time (which may be large/absent and would otherwise derail the
    // per-period sample windowing).
    m_params.useDataRateClock  = true;

    // ---- Queue + reader ----
    m_queue = new PacketQueue();
    m_params.packetQueue = m_queue;

    m_reader = std::make_shared<Ch10PacketReader>();
    std::vector<ProcessingParams*> params_list{ &m_params };
    std::string error;
    if (!m_reader->prepare(toFsPath(request.calFilename), request.timeChannelId, params_list, error))
    {
        finishWithError(QString::fromStdString(error));
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
        emit logMessage(QString("Calibration extraction sample period: %1 ms "
                                "(~%2 frames/window at %3-bit frames).")
                            .arg(period * 1000.0, 0, 'f', 2)
                            .arg(period / frame_duration_sec, 0, 'f', 0)
                            .arg(m_params.bitsInMinorFrame));
    }

    // ---- Worker ----
    m_worker = std::make_shared<FrameProcessor>();
    // What the worker reports reaches this object's thread as queued events, in order -
    // the delivery the signals it replaced gave.
    m_worker->setEvents(std::make_shared<QtProcessingEvents>(
        this,
        [this](const QString& message) { emit logMessage(message); },
        // Capture the worker's failure reason (e.g. "Frame sync pattern was not
        // found...") so finished() reports it instead of the generic fallback.
        [this](const QString& message) {
            m_error = message;
            emit logMessage(message);
        },
        [this](bool ok) { onWorkerFinished(ok); }));

    ProcessingParams params = m_params;
    FrameSetup* setup = m_frame_setup;
    std::shared_ptr<FrameProcessor> worker = m_worker;
    // QThread::create runs the body on the new thread and ends it when the body returns.
    m_worker_thread = QThread::create([worker, params, setup]() {
        worker->process(params, setup);
    });

    // ---- Reader thread ----
    m_reader->setEvents(std::make_shared<QtProcessingEvents>(
        this,
        [this](const QString& message) { emit logMessage(message); },
        [this](const QString& message) {
            m_error = message;
            emit logMessage(message);
        },
        nullptr,
        [this](int percent) { emit progressChanged(percent); }));
    std::shared_ptr<Ch10PacketReader> reader = m_reader;
    m_reader_thread = QThread::create([reader]() { reader->run(); });

    m_worker_thread->start();
    m_reader_thread->start();
}

void CalibrationExtractor::cancel()
{
    if (!m_running)
    {
        return;
    }
    m_cancelled = true;
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

void CalibrationExtractor::onWorkerFinished(bool success)
{
    if (success && !m_cancelled && m_worker != nullptr)
    {
        const ProcessedStreamData data = m_worker->takeResult();

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
                    lo = qBound(0, static_cast<int>(m_clip_start_sec / m_sample_period_sec), count);
                    hi = count - qBound(0, static_cast<int>(m_clip_end_sec / m_sample_period_sec), count);
                }
                const std::vector<double> clipped =
                    (lo < hi) ? std::vector<double>(full.begin() + lo, full.begin() + hi)
                              : full; // ignore an over-aggressive clip
                m_recording_sec = count * m_sample_period_sec;
                if (lo >= hi && (m_clip_start_sec > 0.0 || m_clip_end_sec > 0.0))
                {
                    m_clip_ignored = true;
                }

                StepDetector::Result det =
                    StepDetector::detect(clipped, m_sample_period_sec, m_steps);
                res.profile          = det.profile;
                res.detectedPlateaus = det.detectedPlateaus;
                res.extraPlateaus    = det.extraPlateaus;
                res.outcome          = det.outcome;
                res.edgeThreshold    = det.edgeThreshold;
                res.thresholdRelaxed = det.thresholdRelaxed;
                res.plateauLevels    = det.plateauLevels;
                res.saturated        = det.saturated;
                res.unresolvedDb     = det.unresolvedDb;
                res.sweepLevels      = det.sweepLevels;
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
                QString note;
                if (!res.profile.valid)
                {
                    note = QString(" — %1, edge threshold %2 raw counts")
                               .arg(calibrationOutcomeText(res.outcome))
                               .arg(res.edgeThreshold, 0, 'f', 1);
                }
                else if (res.saturated)
                {
                    // Calibrated, but only over part of its sweep. Say which part:
                    // the profile clamps above it, so a reading at that dB may mean
                    // "this or anything higher".
                    note = QString(" — top step(s) railed; calibrated %1 of %2 step(s), "
                                   "readings hold at %3 dB")
                               .arg(intSize(res.profile.points))
                               .arg(intSize(m_steps))
                               .arg(topResolvedDb(res.profile), 0, 'f', 0);
                }
                else if (res.thresholdRelaxed)
                {
                    note = QString(" — calibrated with a relaxed edge threshold "
                                   "(%1 raw counts)")
                               .arg(res.edgeThreshold, 0, 'f', 1);
                }
                emit logMessage(QString("  %1 (word %2): %3 plateau(s) detected, "
                                        "%4 step(s) expected%5%6")
                                    .arg(QString::fromStdString(res.name))
                                    .arg(res.word)
                                    .arg(res.detectedPlateaus)
                                    .arg(intSize(m_steps))
                                    .arg(res.detectedPlateaus != intSize(m_steps)
                                             ? " — MISMATCH" : "")
                                    .arg(note));
            }
            m_results.push_back(res);
        }

        if (m_clip_ignored)
        {
            emit logMessage(QString("Clip Start + Clip End (%1 s) exceed the recording (%2 s); "
                                    "both were ignored and the whole recording was used.")
                                .arg(m_clip_start_sec + m_clip_end_sec, 0, 'f', 1)
                                .arg(m_recording_sec, 0, 'f', 1));
        }

        teardown();
        m_running = false;

        const int total = intSize(m_results);
        const QString summary = QString("Calibrated %1 of %2 channel(s).").arg(calibrated).arg(total);
        emit finished(true, summary);
        return;
    }

    // Failure or cancellation path.
    teardown();
    m_running = false;
    if (m_cancelled)
    {
        emit finished(false, "Calibration extraction cancelled.");
    }
    else
    {
        emit finished(false, m_error.isEmpty() ? "Calibration extraction failed." : m_error);
    }
}

bool CalibrationExtractor::buildFrameSetup(const Request& request,
                                           int wordsInMinorFrame,
                                           QString& error)
{
    delete m_frame_setup;
    m_frame_setup = new FrameSetup();

    if (request.receiverParamsToml.isEmpty())
    {
        std::string map_error;
        if (!m_frame_setup->buildDefaultReceiverMap(request.numReceivers, request.receiverChannels,
                                                    wordsInMinorFrame, map_error))
        {
            error = QString::fromStdString(map_error);
            return false;
        }
    }
    else if (!QFileInfo::exists(request.receiverParamsToml))
    {
        error = "Receiver Parameters file '" +
                QFileInfo(request.receiverParamsToml).fileName() + "' was not found.";
        return false;
    }
    else if (!m_frame_setup->tryLoadingFile(toFsPath(request.receiverParamsToml), wordsInMinorFrame))
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

void CalibrationExtractor::teardown()
{
    if (m_worker_thread != nullptr)
    {
        m_worker_thread->quit();
        m_worker_thread->wait();
        delete m_worker_thread;
        m_worker_thread = nullptr;
    }
    m_worker.reset(); // the thread is joined, so this is the last reference

    if (m_reader_thread != nullptr)
    {
        m_reader_thread->quit();
        m_reader_thread->wait();
        delete m_reader_thread;
        m_reader_thread = nullptr;
    }
    m_reader.reset(); // the reader thread is joined above, so this is the last reference

    delete m_queue;
    m_queue = nullptr;
    // m_params.packetQueue aliases the queue we just freed; clear it so an early
    // prepare() failure (which runs teardown before the queue is consumed) can't
    // leave a dangling pointer behind in m_params.
    m_params.packetQueue = nullptr;
}

void CalibrationExtractor::finishWithError(const QString& error)
{
    m_error = error;
    teardown();
    m_running = false;
    emit finished(false, error);
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
    std::vector<std::string> fullyCalibrated;
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
                detailLines.push_back(std::format("  {} ({}) — calibrated", label,
                                                  joinStrings(g.calibrated, ", ")));
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

    if (!fullyCalibrated.empty())
    {
        out.push_back("");
        out.push_back(std::format("Calibrated: receiver{} {}.",
                                  fullyCalibrated.size() == 1 ? "" : "s",
                                  joinStrings(fullyCalibrated, ", ")));
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

    // Calibrated, but only after loosening the edge threshold: the steps sat close
    // to this channel's own noise. Reported because it is the early warning the
    // count cannot give — these receivers calibrated, yet they are the ones
    // drifting toward the point where they will stop calibrating at all.
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

    if (!relaxedLines.empty())
    {
        out.push_back("");
        out.push_back("Calibrated only with a relaxed threshold — steps are close to "
                      "the noise floor, so these receivers are worth checking:");
        out.insert(out.end(), relaxedLines.begin(), relaxedLines.end());
    }

    return joinStrings(out, "\n");
}
