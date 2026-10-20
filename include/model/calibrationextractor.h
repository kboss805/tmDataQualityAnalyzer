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
 * A plain class, with no Qt. Lifecycle: construct, call start() with a `ProcessingEvents` sink,
 * wait for `processingFinished`, then read results() and summary(). The sink is called from the
 * extraction's own threads - the thread inside start() for a failure before anything runs, the
 * reader or worker thread for their log lines and progress, and the driver thread for the
 * channel lines and the end - so it must be safe to call from any of them. `QtProcessingEvents`
 * is: it queues each call to a Qt object's thread. `processingFinished` is always the LAST call,
 * and nothing in the extraction is still running when it arrives except the driver returning.
 *
 * Step detection runs on the driver thread, not on the caller's, so a modal progress dialog stays
 * responsive while it works.
 */

#ifndef CALIBRATIONEXTRACTOR_H
#define CALIBRATIONEXTRACTOR_H

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "calibrationprofile.h"
#include "framesync.h"
#include "processedstreamdata.h"
#include "processingevents.h"
#include "processingparams.h"

class Ch10PacketReader;
class FrameProcessor;
class FrameSetup;
class PacketQueue;

/// @brief Per-channel outcome of a calibration extraction run.
struct CalibrationChannelResult
{
    std::string        name;             ///< Parameter/channel name (e.g. "L_RCVR1"), UTF-8.
    int                word = -1;        ///< Zero-based word index within the minor frame.
    CalibrationProfile profile;          ///< Built profile (valid only on success).
    int                detectedPlateaus = 0; ///< Number of plateaus detected.
    bool               extraPlateaus = false; ///< More plateaus than expected steps.
    bool               hadData = false;  ///< Whether any samples were extracted for this channel.
    CalibrationOutcome outcome = CalibrationOutcome::NoData; ///< Which gate decided the outcome.
    double             edgeThreshold = 0.0; ///< Raw-count edge threshold detection used.
    bool               thresholdRelaxed = false; ///< Calibrated only after loosening the
                                                  ///< edge threshold (steps near the noise floor).
    std::vector<double>    plateauLevels;   ///< Settled plateau averages detection found.
    bool               saturated = false; ///< Calibrated only up to a railed top; readings
                                          ///< clamp at the highest step still measurable.
    std::vector<double>    unresolvedDb;    ///< dB of steps no measured level could be paired with.
    int                sweepLevels = 0; ///< Levels in the sweep run; more than the step
                                        ///< file lists means some were set aside as lead-in.
};

/// @brief Extracts non-linear calibration profiles from a calibration Ch10 file.
class CalibrationExtractor
{
public:
    /// @brief Inputs for one extraction run, gathered from the Receiver SNR dialog.
    struct Request
    {
        std::filesystem::path calFilename;   ///< Calibration .ch10 file.
        int     timeChannelId = -1;          ///< Time channel ID (from the parent dialog).
        int     pcmChannelId = -1;           ///< PCM channel ID of the stream being calibrated.
        /// How the bits are recovered: sync pattern and mask, frame length, code format, data rate
        /// and the file-level byte order. MUST match the main run or the extracted raw counts
        /// won't line up with it - which is why both build their parameters from this one
        /// description (`applyAcquisition`), including the byte order, which is raw-affecting.
        Acquisition acquisition;
        /// Word-map TOML; callers resolve the shipped default.toml here so the map matches the main run (empty only if that file is missing).
        std::filesystem::path receiverParamsToml;
        int     numReceivers = 0;            ///< Sequential-grid fallback, used only when receiverParamsToml is empty.
        int     receiverChannels = 0;        ///< Sequential-grid fallback, used only when receiverParamsToml is empty.
        std::vector<StepDefinition> steps;   ///< Expected steps parsed from the step-config TOML.
        double  clipStartSec = 0.0;          ///< Seconds of the cal recording to ignore at the START before step detection (skip signal-generator turn-on transients).
        double  clipEndSec = 0.0;            ///< Seconds of the cal recording to ignore at the END before step detection.
    };

    CalibrationExtractor();
    /// Cancels a run still in flight and waits for it to end.
    ~CalibrationExtractor();

    CalibrationExtractor(const CalibrationExtractor&) = delete;
    CalibrationExtractor& operator=(const CalibrationExtractor&) = delete;

    /**
     * @brief Begins an extraction. Ignored if one is already running.
     *
     * Everything that can fail before any work starts - no steps, a word map that cannot be
     * used, a recording that cannot be opened - is reported before this returns, on the calling
     * thread: `errorOccurred`, then `processingFinished(false)`. Otherwise the work runs on a
     * driver thread and this returns at once.
     *
     * @param[in] events Where reports go; shared so it outlives the driver thread.
     */
    void start(const Request& request, std::shared_ptr<ProcessingEvents> events);

    /// Requests a cooperative abort of the extraction in flight; it ends with
    /// `processingFinished(false)` and the summary "Calibration extraction cancelled.".
    void cancel();

    /// @return True from start() until just before `processingFinished` is reported.
    bool running() const { return m_running; }

    /// @return Per-channel results (valid once `processingFinished` has been reported).
    const std::vector<CalibrationChannelResult>& results() const { return m_results; }
    /// @return Error message if the run failed (empty on success and on a cancel).
    std::string error() const;
    /// @return What the run came to, for the status line: "Calibrated N of M channel(s).", the
    ///         failure's reason, or the cancel notice. Valid once `processingFinished` arrives.
    std::string summary() const { return m_summary; }
    /// @return True if Clip Start + Clip End would have left nothing of the
    ///         recording, so both were ignored and the whole recording was used.
    ///         Reported because the dialog otherwise gives no sign the values the
    ///         operator typed had no effect.
    bool clipIgnored() const { return m_clip_ignored; }
    /// @return Length of the extracted recording in seconds (valid after the run finishes).
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
     * @return Multi-line UTF-8 report suitable for a message box.
     */
    static std::string summarize(const std::vector<CalibrationChannelResult>& results,
                                 int expectedSteps);

private:
    class Relay;

    bool buildFrameSetup(const Request& request, int wordsInMinorFrame, std::string& error);
    /// The driver thread's body: run the reader and worker, wait for both, detect, report.
    void drive();
    /// Turns the worker's raw series into per-channel results; reports a line per channel.
    int buildResults(const ProcessedStreamData& data);
    /// Ends the run: records @p summary, releases the pipeline, reports, and is last.
    void finish(bool success, const std::string& summary, bool is_error);
    void joinDriver();
    void log(const std::string& message);

    std::shared_ptr<ProcessingEvents> m_events;
    std::shared_ptr<Relay>            m_relay;     ///< What the reader and worker report to.

    mutable std::mutex m_mutex;                    ///< Guards the pipeline pointers and m_error.
    std::shared_ptr<Ch10PacketReader> m_reader;    ///< Shared with the reader thread's body.
    std::shared_ptr<FrameProcessor>   m_worker;    ///< Shared with the worker thread's body.
    std::unique_ptr<PacketQueue>      m_queue;
    std::unique_ptr<FrameSetup>       m_frame_setup;

    ProcessingParams  m_params;
    std::vector<StepDefinition> m_steps;
    double            m_sample_period_sec = 0.0;
    double            m_clip_start_sec = 0.0; ///< Cal seconds to ignore at the start before step detection.
    double            m_clip_end_sec = 0.0;   ///< Cal seconds to ignore at the end before step detection.
    bool              m_clip_ignored = false; ///< The clips exceeded the recording and were ignored.
    double            m_recording_sec = 0.0;  ///< Extracted recording length, in seconds.
    std::vector<CalibrationChannelResult> m_results;
    std::string       m_error;
    std::string       m_summary;
    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_worker_ok{false};
    std::atomic<bool> m_running{false};
    std::thread       m_driver;
};

#endif // CALIBRATIONEXTRACTOR_H
