/**
 * @file processingcoordinator.h
 * @brief Orchestrates worker-thread lifecycle for sequential multi-stream processing.
 *
 * Owns all transient processing state: the background QThread, the active
 * FrameProcessor, the queue of per-stream jobs, and progress tracking.
 * MainViewModel builds a list of fully-formed StreamJob objects (each carrying a
 * validated ProcessingParams plus an owned FrameSetup) and hands them off here.
 * The coordinator processes them one at a time, emitting streamProcessed() with
 * each stream's in-memory result and processingFinished() when the queue drains.
 */

#ifndef PROCESSINGCOORDINATOR_H
#define PROCESSINGCOORDINATOR_H

#include <QObject>
#include <QString>
#include <QThread>
#include <QVector>

#include "processedstreamdata.h"
#include "processingparams.h"

class FrameProcessor;
class FrameSetup;

/**
 * @brief One unit of work: a stream's parameters plus its frame parameter table.
 *
 * @c frameSetup is owned by the coordinator once the job is submitted and is
 * deleted after the job completes. It may be an empty FrameSetup (no parameters)
 * for FrameSyncLockStats streams.
 */
struct StreamJob
{
    ProcessingParams params;            ///< Fully-validated parameters for this stream.
    FrameSetup*      frameSetup = nullptr; ///< Owned word-map/calibration table (transferred to coordinator).
};

/**
 * @brief Owns the worker-thread lifecycle for sequential multi-stream processing.
 *
 * Constructed by MainViewModel as a child QObject so it is destroyed before the
 * ViewModel's own members.
 */
class ProcessingCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit ProcessingCoordinator(QObject* parent = nullptr);
    ~ProcessingCoordinator();

    ProcessingCoordinator(const ProcessingCoordinator&)            = delete;
    ProcessingCoordinator& operator=(const ProcessingCoordinator&) = delete;
    ProcessingCoordinator(ProcessingCoordinator&&)                 = delete;
    ProcessingCoordinator& operator=(ProcessingCoordinator&&)      = delete;

    /**
     * @brief Starts processing the supplied stream jobs in sequence.
     *
     * Takes ownership of each StreamJob::frameSetup. Returns false (and frees the
     * supplied frame setups) if @p jobs is empty or a run is already active.
     */
    bool startProcessing(QVector<StreamJob> jobs);

    /// Requests abort of the active processor and cancels remaining queued jobs.
    void cancelProcessing();

    /// Resets transient counters. Called by MainViewModel::clearState().
    void reset();

    // State queries — used by MainViewModel property getters
    bool  processing()      const;  ///< @return True while background processing is active.
    int   progressPercent() const;  ///< @return Overall progress across all jobs (0--100).

signals:
    /// Emitted when overall processing progress changes.
    void progressChanged(int percent);
    /// Emitted when active processing state changes.
    void processingStateChanged(bool active);
    /// Emitted once per stream with its accumulated in-memory result.
    void streamProcessed(const ProcessedStreamData& data);
    /// Emitted when the whole queue finishes (or aborts).
    void processingFinished(bool success);
    /// Forwarded log message from the worker thread.
    void logMessageReceived(const QString& message);
    /// Emitted on processing error.
    void errorOccurred(const QString& message);

private:
    /// Frees and clears any remaining queued jobs (including the active one's frame setup).
    void clearJobs();
    /// Launches a worker thread for the job at @p m_job_index.
    void launchWorkerThread();
    /// Tears down the active worker thread (quit + wait + delete).
    void teardownWorkerThread();

    // Slots connected to FrameProcessor signals
    void onProgressUpdated(int percent);
    void onProcessingFinished(bool success);
    void onLogMessage(const QString& message);

    // Thread lifecycle
    QThread*        m_worker_thread     = nullptr;
    FrameProcessor* m_current_processor = nullptr;

    // Job queue
    QVector<StreamJob> m_jobs;
    int                m_job_index    = 0;
    bool               m_any_success  = false;
    bool               m_cancelled    = false;

    // Processing state
    bool m_processing       = false;
    int  m_progress_percent = 0;
};

#endif // PROCESSINGCOORDINATOR_H
