/**
 * @file calibrationextractor.cpp
 * @brief Implementation of CalibrationExtractor (US3.2).
 */

#include "calibrationextractor.h"

#include <algorithm>

#include <QFileInfo>
#include <QThread>

#include "ch10packetreader.h"
#include "constants.h"
#include "frameprocessor.h"
#include "framesetup.h"
#include "packetqueue.h"
#include "processedstreamdata.h"
#include "stepdetector.h"

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
    const uint64_t frame_sync = request.frameSyncHex.toULongLong(&sync_ok, UIConstants::kHexBase);
    if (!sync_ok || request.frameSyncHex.isEmpty())
    {
        finishWithError("Invalid frame sync pattern '" + request.frameSyncHex + "'.");
        return;
    }
    const int sync_pattern_length = static_cast<int>(request.frameSyncHex.length()) * 4;

    uint64_t frame_sync_mask = 0;
    if (request.frameSyncMaskHex.isEmpty())
    {
        frame_sync_mask = (sync_pattern_length > 0 && sync_pattern_length < 64)
            ? (1ULL << sync_pattern_length) - 1
            : 0xFFFFFFFFFFFFFFFFULL;
    }
    else
    {
        bool mask_ok = false;
        frame_sync_mask = request.frameSyncMaskHex.toULongLong(&mask_ok, UIConstants::kHexBase);
        if (!mask_ok)
        {
            finishWithError("Invalid frame sync mask '" + request.frameSyncMaskHex + "'.");
            return;
        }
    }

    const int words_in_minor_frame =
        (request.bitsInMinorFrame + PCMConstants::kCommonWordLen - 1) / PCMConstants::kCommonWordLen;

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
    m_params.bitsInMinorFrame  = request.bitsInMinorFrame;
    m_params.isRandomized      = request.randomized;
    m_params.isInverted        = request.inverted;
    m_params.mode              = StreamMode::ReceiverChannelInfo;
    m_params.dataRateBps       = (request.dataRateMbps > 0.0) ? request.dataRateMbps * 1e6 : 0.0;
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

    // Size the extraction sample period to the resolved bit rate so each window
    // averages ~kFramesPerExtractWindow frames. The reader has just resolved the
    // per-bit period (from the user's data rate or TMATS) into resolvedAttrs.
    // The fixed 10 ms default starves windows on low-frame-rate recordings
    // (e.g. ~8000-bit frames at ~100 frames/s -> ~1 frame per 10 ms -> a sparse,
    // zero-laced series StepDetector reads as noise), so derive it instead.
    const double bit_period_sec = m_params.resolvedAttrs.delta100ns * 1e-7;
    if (bit_period_sec > 0.0 && m_params.bitsInMinorFrame > 0)
    {
        const double frame_duration_sec =
            static_cast<double>(m_params.bitsInMinorFrame) * bit_period_sec;
        double period = CalibrationConstants::kFramesPerExtractWindow * frame_duration_sec;
        // Floor at kMinAdaptiveExtractPeriodSec (not the finer raw default): on
        // fast frames the frames/window sizing collapses to a few ms, which
        // over-resolves plateaus and makes StepDetector mis-pair small steps.
        period = std::clamp(period, CalibrationConstants::kMinAdaptiveExtractPeriodSec,
                            CalibrationConstants::kMaxExtractSamplePeriodSec);
        m_sample_period_sec      = period;
        m_params.samplePeriodSec = period;
        emit logMessage(QString("Calibration extraction sample period: %1 ms "
                                "(~%2 frames/window at %3-bit frames).")
                            .arg(period * 1000.0, 0, 'f', 1)
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
                emit logMessage(QString("  %1 (word %2): %3 plateau(s) detected, "
                                        "%4 step(s) expected%5%6")
                                    .arg(res.name)
                                    .arg(res.word)
                                    .arg(res.detectedPlateaus)
                                    .arg(m_steps.size())
                                    .arg(res.detectedPlateaus != m_steps.size()
                                             ? " — MISMATCH" : "")
                                    .arg(res.profile.valid ? "" : " (no profile)"));
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
