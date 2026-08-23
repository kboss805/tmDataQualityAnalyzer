/**
 * @file calibrationextractor.h
 * @brief Drives a single-stream raw extraction over a calibration Chapter 10
 *        file and builds per-channel non-linear calibration profiles (US5.3).
 *
 * Reuses the production pipeline (Ch10PacketReader + FrameProcessor) on its own
 * worker threads, but configures the word map with unit slope / zero offset so
 * the per-channel output is the raw count averaged over a fine (10 ms) window.
 * StepDetector then turns each channel's raw series into a CalibrationProfile.
 *
 * Lifecycle: construct, connect to progressChanged()/finished(), call start().
 * The owner typically pumps a nested event loop (e.g. behind a modal
 * QProgressDialog) until finished() fires, then reads results().
 */

#ifndef CALIBRATIONEXTRACTOR_H
#define CALIBRATIONEXTRACTOR_H

#include <QObject>
#include <QString>
#include <QVector>

#include "calibrationprofile.h"
#include "processingparams.h"

class Ch10PacketReader;
class FrameProcessor;
class FrameSetup;
class PacketQueue;
class QThread;

/// @brief Per-channel outcome of a calibration extraction run.
struct CalibrationChannelResult
{
    QString            name;             ///< Parameter/channel name (e.g. "L_RCVR1").
    int                word = -1;        ///< Zero-based word index within the minor frame.
    CalibrationProfile profile;          ///< Built profile (valid only on success).
    int                detectedPlateaus = 0; ///< Number of plateaus detected.
    bool               extraPlateaus = false; ///< More plateaus than expected steps.
    bool               hadData = false;  ///< Whether any samples were extracted for this channel.
};

/// @brief Extracts non-linear calibration profiles from a calibration Ch10 file.
class CalibrationExtractor : public QObject
{
    Q_OBJECT

public:
    /// @brief Inputs for one extraction run, gathered from the Receiver SNR dialog.
    struct Request
    {
        QString calFilename;             ///< Calibration .ch10 file.
        int     timeChannelId = -1;      ///< Time channel ID (from the parent dialog).
        int     pcmChannelId = -1;       ///< PCM channel ID of the stream being calibrated.
        FrameSyncParams sync;            ///< Frame sync pattern/mask, frame length, scrambling, data rate. MUST match the main run or the extracted raw counts won't line up with it.
        /// File-level byte order, mirrored from the Configure Streams dialog. Like
        /// `sync`, this MUST match the main run: it is a raw-affecting transform, so
        /// extracting with a different byte order than the run being calibrated
        /// produces a profile built from a different bitstream entirely.
        bool    swapBytes = true;
        QString receiverParamsToml;      ///< Word-map TOML; callers resolve the shipped default.toml here so the map matches the main run (empty only if that file is missing).
        int     numReceivers = 0;        ///< Sequential-grid fallback, used only when receiverParamsToml is empty.
        int     receiverChannels = 0;    ///< Sequential-grid fallback, used only when receiverParamsToml is empty.
        QVector<StepDefinition> steps;   ///< Expected steps parsed from the step-config TOML.
        double  clipStartSec = 0.0;      ///< Seconds of the cal recording to ignore at the START before step detection (skip signal-generator turn-on transients).
        double  clipEndSec = 0.0;        ///< Seconds of the cal recording to ignore at the END before step detection.
    };

    explicit CalibrationExtractor(QObject* parent = nullptr);
    ~CalibrationExtractor() override;

    CalibrationExtractor(const CalibrationExtractor&) = delete;
    CalibrationExtractor& operator=(const CalibrationExtractor&) = delete;

    /// Begins extraction asynchronously (sets up threads and returns).
    void start(const Request& request);
    /// Requests a cooperative abort of the in-flight extraction.
    void cancel();

    /// @return Per-channel results (valid after finished() fires).
    const QVector<CalibrationChannelResult>& results() const { return m_results; }
    /// @return Error message if the run failed.
    QString error() const { return m_error; }

signals:
    /// File-read completion percentage (0..100).
    void progressChanged(int percent);
    /// Human-readable status / warning messages.
    void logMessage(const QString& message);
    /// Emitted once when the run completes; @p summary is a user-facing recap.
    void finished(bool success, const QString& summary);

private slots:
    void onWorkerFinished(bool success);

private:
    bool buildFrameSetup(const Request& request, int wordsInMinorFrame, QString& error);
    void teardown();
    void finishWithError(const QString& error);

    Ch10PacketReader* m_reader        = nullptr;
    QThread*          m_reader_thread = nullptr;
    FrameProcessor*   m_worker        = nullptr;
    QThread*          m_worker_thread = nullptr;
    PacketQueue*      m_queue         = nullptr;
    FrameSetup*       m_frame_setup   = nullptr;

    ProcessingParams  m_params;
    QVector<StepDefinition> m_steps;
    double            m_sample_period_sec = 0.0;
    double            m_clip_start_sec = 0.0; ///< Cal seconds to ignore at the start before step detection.
    double            m_clip_end_sec = 0.0;   ///< Cal seconds to ignore at the end before step detection.
    QVector<CalibrationChannelResult> m_results;
    QString           m_error;
    bool              m_cancelled = false;
    bool              m_running   = false;
};

#endif // CALIBRATIONEXTRACTOR_H
