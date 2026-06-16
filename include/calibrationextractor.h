/**
 * @file calibrationextractor.h
 * @brief Drives a single-stream raw extraction over a calibration Chapter 10
 *        file and builds per-channel non-linear calibration profiles (US3.2).
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
        QString frameSyncHex;            ///< Frame sync pattern (hex).
        QString frameSyncMaskHex;        ///< Frame sync mask (hex).
        int     bitsInMinorFrame = 0;    ///< Bits per minor frame.
        bool    randomized = false;      ///< RNRZ-L on/off.
        double  dataRateMbps = 0.0;      ///< Data rate (0 = TMATS).
        QString receiverParamsToml;      ///< Word-map TOML (empty = default grid).
        int     numReceivers = 0;        ///< Used only when receiverParamsToml is empty.
        int     receiverChannels = 0;    ///< Used only when receiverParamsToml is empty.
        QVector<StepDefinition> steps;   ///< Expected steps parsed from the step-config TOML.
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
    QVector<CalibrationChannelResult> m_results;
    QString           m_error;
    bool              m_cancelled = false;
    bool              m_running   = false;
};

#endif // CALIBRATIONEXTRACTOR_H
