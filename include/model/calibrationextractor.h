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
    CalibrationOutcome outcome = CalibrationOutcome::NoData; ///< Which gate decided the outcome.
    double             edgeThreshold = 0.0; ///< Raw-count edge threshold detection used.
    bool               thresholdRelaxed = false; ///< Calibrated only after loosening the
                                                  ///< edge threshold (steps near the noise floor).
    QVector<double>    plateauLevels;   ///< Settled plateau averages detection found.
    bool               saturated = false; ///< Calibrated only up to a railed top; readings
                                          ///< clamp at the highest step still measurable.
    QVector<double>    unresolvedDb;    ///< dB of steps no measured level could be paired with.
    int                sweepLevels = 0; ///< Levels in the sweep run; more than the step
                                        ///< file lists means some were set aside as lead-in.
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
    /// @return True if Clip Start + Clip End would have left nothing of the
    ///         recording, so both were ignored and the whole recording was used.
    ///         Reported because the dialog otherwise gives no sign the values the
    ///         operator typed had no effect.
    bool clipIgnored() const { return m_clip_ignored; }
    /// @return Length of the extracted recording in seconds (valid after finished()).
    double recordingSeconds() const { return m_recording_sec; }

    /**
     * @brief Builds the operator-facing calibration report from @p results.
     *
     * Names which receivers were calibrated and, for the rest, why — grouped by
     * receiver (the unit operators actually think and speak in) rather than by
     * the individual L/R/C words. A bare "calibrated N of M channel(s)" count
     * cannot distinguish a receiver that was absent from the recording from one
     * whose steps were too compressed to resolve, which are entirely different
     * problems with entirely different remedies.
     *
     * Pure and static so the wording is unit-testable without running an
     * extraction over a real .ch10 file.
     *
     * @param[in] results       Per-channel outcomes, in word order.
     * @param[in] expectedSteps Step count from the step-config TOML, so a
     *                          plateau shortfall can be reported against it.
     * @return Multi-line report suitable for a message box.
     */
    static QString summarize(const QVector<CalibrationChannelResult>& results,
                             int expectedSteps);

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
    bool              m_clip_ignored = false; ///< The clips exceeded the recording and were ignored.
    double            m_recording_sec = 0.0;  ///< Extracted recording length, in seconds.
    QVector<CalibrationChannelResult> m_results;
    QString           m_error;
    bool              m_cancelled = false;
    bool              m_running   = false;
};

#endif // CALIBRATIONEXTRACTOR_H
