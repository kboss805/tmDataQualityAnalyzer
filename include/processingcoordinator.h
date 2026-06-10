/**
 * @file processingcoordinator.h
 * @brief Orchestrates the single-reader / parallel-worker processing pipeline.
 *
 * Owns all transient processing state: one Ch10PacketReader thread that reads the
 * file once and fans PCM packets out to per-stream PacketQueues, and one
 * FrameProcessor worker thread per stream that drains its queue concurrently.
 * MainViewModel builds a list of fully-formed StreamJob objects (each carrying a
 * validated ProcessingParams plus an owned FrameSetup) and hands them off here.
 * The coordinator emits streamProcessed() with each stream's in-memory result and
 * processingFinished() when every worker has completed.
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
class Ch10PacketReader;
class PacketQueue;

/**
 * @brief One unit of work: a stream's parameters plus its frame parameter table.
 */
struct StreamJob
{
    ProcessingParams params;            ///< Fully-validated parameters for this stream.
    FrameSetup*      frameSetup = nullptr; ///< Owned word-map/calibration table (transferred to coordinator).
};

/**
 * @brief Owns the reader + worker thread lifecycle for multi-stream processing.
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
     * @brief Starts processing the supplied stream jobs concurrently.
     *
     * Takes ownership of each StreamJob::frameSetup. Returns false (and frees the
     * supplied frame setups) if @p jobs is empty or a run is already active.
     */
    bool startProcessing(QVector<StreamJob> jobs);

    /// Requests abort of all workers and the reader.
    void cancelProcessing();

    /// Resets transient counters. Called by MainViewModel::clearState().
    void reset();

    bool  processing()      const;  ///< @return True while background processing is active.
    int   progressPercent() const;  ///< @return Overall progress (0--100), driven by the reader.

signals:
    void progressChanged(int percent);
    void processingStateChanged(bool active);
    void streamProcessed(const ProcessedStreamData& data);
    void processingFinished(bool success);
    void logMessageReceived(const QString& message);
    void errorOccurred(const QString& message);

private:
    /// Per-stream worker bookkeeping.
    struct Worker
    {
        QThread*        thread    = nullptr;
        FrameProcessor* processor = nullptr;
        PacketQueue*    queue     = nullptr;
    };

    /// Frees and clears any remaining queued jobs (including frame setups).
    void clearJobs();
    /// Tears down all worker threads, the reader thread, and queues.
    void teardownAll();
    /// Finalizes the run once all workers have reported completion.
    void finalize();

    // Slots
    void onReaderProgress(int percent);
    void onWorkerFinished(FrameProcessor* processor, bool success);

    // Reader
    QThread*          m_reader_thread = nullptr;
    Ch10PacketReader* m_reader        = nullptr;

    // Workers (one per job)
    QVector<Worker>   m_workers;
    QVector<PacketQueue*> m_queues;

    // Job storage (frame setups owned here)
    QVector<StreamJob> m_jobs;

    int  m_workers_remaining = 0;
    bool m_any_success       = false;
    bool m_cancelled         = false;
    bool m_processing        = false;
    int  m_progress_percent  = 0;
};

#endif // PROCESSINGCOORDINATOR_H
