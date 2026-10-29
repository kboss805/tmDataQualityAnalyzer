/**
 * @file pipelinerunner.h
 * @brief Runs the single-reader / parallel-worker processing pipeline, with no Qt.
 *
 * One `Ch10PacketReader` reads the recording once and fans each PCM packet out to a
 * `PacketQueue` per stream; one `FrameProcessor` per stream drains its queue on a thread of its
 * own. This class owns that whole arrangement - the queues, the reader, the threads, the order they
 * start and stop in, and what a stream's result is stamped with - so the Qt application
 * (`ProcessingCoordinator`) and a C ABI over the core can both be a thin wrapper around it instead
 * of each carrying their own copy.
 *
 * **Threading.** `start()` resolves the recording's TMATS synchronously on the calling thread and
 * returns at once; everything after that happens on the runner's own threads: one per stream, and
 * one that reads the file, which is also the thread that reports the end. The sink's methods are
 * called from those threads, so a sink that needs them somewhere else (the UI thread) does its own
 * hand-off - `QtProcessingEvents` is the Qt one. What a sink can rely on:
 *
 *  - `logMessage` / `errorOccurred`: from the reader thread or any stream's thread; per source, in
 *    the order they were raised.
 *  - `progressUpdated`: from the reader thread only.
 *  - `streamProcessed`: from the thread of the stream it describes, after that stream's last log
 *    line, and only for a stream that completed successfully in a run that was not cancelled.
 *  - `processingFinished`: from the reader thread, once, LAST - after every `streamProcessed` and
 *    after every other thread has ended and every resource has been released. `success` is true if
 *    at least one stream completed and the run was not cancelled.
 *
 * A sink must not call `start()` on the runner from inside one of these calls: that would join
 * the thread it is running on. The Qt adapter never does, because it delivers by a queued call.
 */

#ifndef PIPELINERUNNER_H
#define PIPELINERUNNER_H

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "framesetup.h"
#include "processedstreamdata.h"
#include "processingevents.h"
#include "processingparams.h"

class Ch10PacketReader;
class FrameProcessor;
class PacketQueue;

/**
 * @brief One unit of work: a stream's parameters plus its frame parameter table.
 */
struct StreamJob
{
    ProcessingParams            params;     ///< Fully-validated parameters for this stream.
    std::shared_ptr<FrameSetup> frameSetup; ///< The word-map / calibration table; the runner keeps it alive
                                            ///< for as long as the stream can run. Shared so a job stays copyable.
};

/**
 * @brief Where a pipeline run reports to: the four `ProcessingEvents` calls, plus each stream's result.
 *
 * `processingFinished` here means the whole RUN is over (not one stream); see pipelinerunner.h
 * for which thread each call arrives on.
 */
class PipelineEvents : public ProcessingEvents
{
public:
    /// One stream's finished result, with `jobIndex` (its position in the job list) and `sourceId` set.
    virtual void streamProcessed(ProcessedStreamData data) = 0;
};

/**
 * @brief Runs a set of stream jobs over one recording, concurrently.
 */
class PipelineRunner
{
    friend struct PipelineRunnerTestAccess;   ///< Reaches a stream's processor to end it without a cancel.

public:
    PipelineRunner();
    /// Cancels a run still in flight and waits for it to end.
    ~PipelineRunner();

    PipelineRunner(const PipelineRunner&)            = delete;
    PipelineRunner& operator=(const PipelineRunner&) = delete;
    PipelineRunner(PipelineRunner&&)                 = delete;
    PipelineRunner& operator=(PipelineRunner&&)      = delete;

    /**
     * @brief Starts @p jobs over the recording the first of them names.
     *
     * Every job must name the same file and time channel (the first job's are used): there is one
     * reader. A failure before any thread starts - no jobs, a recording that cannot be opened,
     * a stream whose channel the recording does not have - returns false with the reason in
     * @p error and reports nothing to @p events; the jobs and their frame setups are released.
     * Returns false too, with a reason, if a run is already in flight.
     *
     * @param[in]  jobs   The streams to process. Job i's result carries `jobIndex == i`.
     * @param[in]  events Where reports go; shared so it outlives the threads that call it.
     * @param[out] error  Why the run could not start.
     * @return true if the run started; then exactly one `processingFinished` will follow.
     */
    bool start(std::vector<StreamJob> jobs, std::shared_ptr<PipelineEvents> events, std::string& error);

    /// Requests a cooperative stop of the run in flight; it ends with `processingFinished(false)`.
    /// Safe from any thread; does nothing when no run is in flight.
    void cancel();

    /// @return True from a successful start() until just before `processingFinished` is reported.
    bool running() const { return m_running; }

    /// Blocks until the run's threads have all ended (immediately if there is none).
    void wait();

private:
    class Relay;

    /// The reader thread's body: start the stream threads, read, wait for them, report the end.
    void drive();
    /// One stream's thread body: process, then deliver the result if it earned one.
    void runStream(std::size_t index);
    /// Ends the run: releases the pipeline, then reports. Called by drive() once every thread ended.
    void finish();
    void joinDriver();

    std::shared_ptr<PipelineEvents> m_events;

    mutable std::mutex m_mutex;                              ///< Guards the pipeline objects below against cancel().
    std::vector<StreamJob>                         m_jobs;
    std::vector<std::unique_ptr<PacketQueue>>      m_queues;   ///< One per job; params.packetQueue points here.
    std::vector<std::shared_ptr<FrameProcessor>>   m_workers;  ///< One per job.
    std::shared_ptr<Ch10PacketReader>              m_reader;

    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_any_success{false};
    std::atomic<bool> m_running{false};
    std::thread       m_driver;                              ///< The reader thread, which also ends the run.
};

#endif // PIPELINERUNNER_H
