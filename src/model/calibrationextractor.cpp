/**
 * @file calibrationextractor.cpp
 * @brief Implementation of CalibrationExtractor (US5.3).
 */

#include "calibrationextractor.h"

#include <algorithm>

#include <QFileInfo>
#include <QMap>
#include <QStringList>
#include <QThread>

#include "ch10packetreader.h"
#include "constants.h"
#include "frameprocessor.h"
#include "framesetup.h"
#include "packetqueue.h"
#include "processedstreamdata.h"
#include "stepdetector.h"

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
    m_sample_period_sec = CalibrationConstants::kExtractSamplePeriodSec;

    if (request.steps.isEmpty())
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
        (request.sync.bitsInMinorFrame + PCMConstants::kCommonWordLen - 1) / PCMConstants::kCommonWordLen;

    // ---- Word map (unit slope, zero offset -> raw counts out) ----
    QString setup_error;
    if (!buildFrameSetup(request, words_in_minor_frame, setup_error))
    {
        finishWithError(setup_error);
        return;
    }

    // ---- ProcessingParams ----
    m_params = ProcessingParams{};
    m_params.filename          = request.calFilename;
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

    m_reader = new Ch10PacketReader;
    QVector<ProcessingParams*> params_list{ &m_params };
    QString error;
    if (!m_reader->prepare(request.calFilename, request.timeChannelId, params_list, error))
    {
        finishWithError(error);
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
    m_worker = new FrameProcessor;
    m_worker_thread = new QThread;
    m_worker->moveToThread(m_worker_thread);
    connect(m_worker, &FrameProcessor::processingFinished,
            this, &CalibrationExtractor::onWorkerFinished);
    connect(m_worker, &FrameProcessor::logMessage, this, &CalibrationExtractor::logMessage);
    // Capture the worker's failure reason (e.g. "Frame sync pattern was not
    // found...") so finished() reports it instead of the generic fallback.
    connect(m_worker, &FrameProcessor::errorOccurred, this, [this](const QString& msg) {
        m_error = msg;
        emit logMessage(msg);
    });
    connect(m_worker_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    ProcessingParams params = m_params;
    FrameSetup* setup = m_frame_setup;
    connect(m_worker_thread, &QThread::started, m_worker, [this, params, setup]() {
        m_worker->process(params, setup);
    });

    // ---- Reader thread ----
    m_reader_thread = new QThread;
    m_reader->moveToThread(m_reader_thread);
    connect(m_reader, &Ch10PacketReader::progressUpdated, this, &CalibrationExtractor::progressChanged);
    connect(m_reader, &Ch10PacketReader::logMessage, this, &CalibrationExtractor::logMessage);
    connect(m_reader, &Ch10PacketReader::errorOccurred, this, [this](const QString& msg) {
        m_error = msg;
        emit logMessage(msg);
    });
    connect(m_reader_thread, &QThread::started, m_reader, &Ch10PacketReader::run);

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
            res.hadData = !ch.values.isEmpty();

            if (res.hadData)
            {
                // Clip the user-specified leading/trailing seconds before step
                // detection, so signal-generator turn-on transients (and any
                // trailing junk) don't get mistaken for cal plateaus. The series
                // is sampled every m_sample_period_sec, so seconds -> sample count.
                const QVector<double>& full = ch.values;
                int lo = 0;
                int hi = full.size();
                if (m_sample_period_sec > 0.0)
                {
                    lo = qBound(0, static_cast<int>(m_clip_start_sec / m_sample_period_sec), full.size());
                    hi = full.size() - qBound(0, static_cast<int>(m_clip_end_sec / m_sample_period_sec), full.size());
                }
                const QVector<double> clipped =
                    (lo < hi) ? full.mid(lo, hi - lo) : full; // ignore an over-aggressive clip

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
                               .arg(res.profile.points.size())
                               .arg(m_steps.size())
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
                                    .arg(res.name)
                                    .arg(res.word)
                                    .arg(res.detectedPlateaus)
                                    .arg(m_steps.size())
                                    .arg(res.detectedPlateaus != m_steps.size()
                                             ? " — MISMATCH" : "")
                                    .arg(note));
            }
            m_results.push_back(res);
        }

        teardown();
        m_running = false;

        const int total = m_results.size();
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
    m_frame_setup = new FrameSetup(nullptr);

    if (request.receiverParamsToml.isEmpty())
    {
        if (!m_frame_setup->buildDefaultReceiverMap(request.numReceivers, request.receiverChannels,
                                                    wordsInMinorFrame, error))
        {
            return false;
        }
    }
    else if (!QFileInfo::exists(request.receiverParamsToml))
    {
        error = "Receiver Parameters file '" +
                QFileInfo(request.receiverParamsToml).fileName() + "' was not found.";
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

void CalibrationExtractor::teardown()
{
    if (m_worker_thread != nullptr)
    {
        m_worker_thread->quit();
        m_worker_thread->wait();
        delete m_worker_thread;
        m_worker_thread = nullptr;
    }
    m_worker = nullptr; // auto-deleted via deleteLater on thread finish

    if (m_reader_thread != nullptr)
    {
        m_reader_thread->quit();
        m_reader_thread->wait();
        delete m_reader_thread;
        m_reader_thread = nullptr;
    }
    delete m_reader;
    m_reader = nullptr;

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

QString CalibrationExtractor::summarize(const QVector<CalibrationChannelResult>& results,
                                        int expectedSteps)
{
    if (results.isEmpty())
    {
        return QStringLiteral("No receiver channels were extracted.");
    }

    // Group channels by receiver. Receiver 0 collects anything whose name carries
    // no "_RCVR<N>" suffix (a hand-written receiver-params TOML may name its
    // parameters anything); those are reported by name instead of by number.
    struct Group
    {
        QStringList calibrated;                        ///< Channel parts that succeeded.
        QStringList relaxed;                           ///< …of those, ones needing a lower threshold.
        QStringList saturated;                         ///< …of those, ones whose top steps railed.
        double      topDb = 0.0;                       ///< Highest dB those actually measured.
        QVector<double> unresolved;                    ///< dB no level could be paired with.
        QMap<CalibrationOutcome, QStringList> failed;  ///< Channel parts per failure reason.
        QStringList flatLevels;                        ///< Stuck levels of flat channels.
        int minPlateaus = -1;                          ///< Fewest plateaus seen among failures.
    };
    QMap<int, Group> byReceiver;

    int calibratedChannels = 0;
    for (const CalibrationChannelResult& r : results)
    {
        const int receiver = FrameSetup::receiverIndexFromName(r.name);
        const QString part = (receiver > 0) ? FrameSetup::channelPartOfName(r.name) : r.name;
        Group& g = byReceiver[receiver];

        if (r.outcome == CalibrationOutcome::Calibrated)
        {
            g.calibrated.push_back(part);
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
                    if (!g.unresolved.contains(db))
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
            if (r.outcome == CalibrationOutcome::FlatNoSignal && !r.plateauLevels.isEmpty())
            {
                const QString level = QString::number(r.plateauLevels.first(), 'f', 0);
                if (!g.flatLevels.contains(level))
                {
                    g.flatLevels.push_back(level);
                }
            }
        }
    }

    // Receivers whose every channel calibrated collapse to a bare number list -
    // the common good case should read as one short line, not one line each.
    QStringList fullyCalibrated;
    QStringList detailLines;
    QStringList relaxedLines;
    QStringList saturatedLines;
    for (auto it = byReceiver.constBegin(); it != byReceiver.constEnd(); ++it)
    {
        const int receiver = it.key();
        const Group& g = it.value();
        const QString label = (receiver > 0)
                                  ? QStringLiteral("Receiver %1").arg(receiver)
                                  : QStringLiteral("Unnamed receiver");

        if (!g.relaxed.isEmpty())
        {
            relaxedLines.push_back(QStringLiteral("  %1 — %2")
                                       .arg(label, g.relaxed.join(", ")));
        }

        if (!g.saturated.isEmpty())
        {
            QStringList unresolvedText;
            for (double db : g.unresolved)
            {
                unresolvedText << QString::number(db, 'f', 0);
            }
            saturatedLines.push_back(
                QStringLiteral("  %1 — %2: readings hold at %3 dB%4")
                    .arg(label, g.saturated.join(", "))
                    .arg(g.topDb, 0, 'f', 0)
                    .arg(unresolvedText.isEmpty()
                             ? QString()
                             : QStringLiteral(" (no measurement for %1 dB)")
                                   .arg(unresolvedText.join(", "))));
        }

        if (g.failed.isEmpty())
        {
            if (receiver > 0)
            {
                fullyCalibrated.push_back(QString::number(receiver));
            }
            else
            {
                detailLines.push_back(QStringLiteral("  %1 (%2) — calibrated")
                                          .arg(label, g.calibrated.join(", ")));
            }
            continue;
        }

        // Mixed or wholly failed: say which channels fell back and why. A partly
        // calibrated receiver is the case most worth spelling out, because the
        // count alone would let it hide inside "N of M".
        QStringList reasons;
        for (auto f = g.failed.constBegin(); f != g.failed.constEnd(); ++f)
        {
            QString reason = QString("%1: %2")
                                 .arg(f.value().join(", "),
                                      QString::fromLatin1(calibrationOutcomeText(f.key())));
            if (f.key() == CalibrationOutcome::TooFewPlateaus && g.minPlateaus >= 0)
            {
                reason += QStringLiteral(" (%1 of %2 steps resolved)")
                              .arg(g.minPlateaus).arg(expectedSteps);
            }
            else if (f.key() == CalibrationOutcome::FlatNoSignal && !g.flatLevels.isEmpty())
            {
                // The level itself is the diagnosis: 0 is a dead input, full
                // scale is a railed one, and anything between is a stuck DC
                // level. "Flat" alone would leave the operator guessing which.
                reason += QStringLiteral(" (held at %1)").arg(g.flatLevels.join(", "));
            }
            reasons.push_back(reason);
        }

        const QString calibratedPart =
            g.calibrated.isEmpty()
                ? QString()
                : QStringLiteral("%1 calibrated; ").arg(g.calibrated.join(", "));
        detailLines.push_back(QStringLiteral("  %1 — %2%3")
                                  .arg(label, calibratedPart, reasons.join("; ")));
    }

    QStringList out;
    out << QStringLiteral("Applied non-linear calibration to %1 of %2 channel(s).")
               .arg(calibratedChannels).arg(results.size());

    if (!fullyCalibrated.isEmpty())
    {
        out << QString();
        out << QStringLiteral("Calibrated: receiver%1 %2.")
                   .arg(fullyCalibrated.size() == 1 ? "" : "s", fullyCalibrated.join(", "));
    }

    if (!detailLines.isEmpty())
    {
        out << QString();
        out << QStringLiteral("Fell back to linear calibration:");
        out << detailLines;
    }

    // Calibrated, but only after loosening the edge threshold: the steps sat close
    // to this channel's own noise. Reported because it is the early warning the
    // count cannot give — these receivers calibrated, yet they are the ones
    // drifting toward the point where they will stop calibrating at all.
    // Calibrated, but the receiver ran out of range partway up the sweep, so the
    // profile stops there and clamps above it. Reported because the plot then
    // cannot rise past that step no matter how strong the signal gets - which
    // reads as a ceiling in the data rather than as a receiver setting to fix.
    if (!saturatedLines.isEmpty())
    {
        out << QString();
        out << QStringLiteral("Calibrated only up to a saturated top — these receivers ran out "
                              "of range partway through the sweep, so their readings hold at "
                              "the highest step still measurable:");
        out << saturatedLines;
    }

    if (!relaxedLines.isEmpty())
    {
        out << QString();
        out << QStringLiteral("Calibrated only with a relaxed threshold — steps are close to "
                              "the noise floor, so these receivers are worth checking:");
        out << relaxedLines;
    }

    return out.join('\n');
}
