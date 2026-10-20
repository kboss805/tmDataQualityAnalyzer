/**
 * @file processingcoordinator.h
 * @brief The Qt face of the single-reader / parallel-worker processing pipeline.
 *
 * The pipeline itself - the reader, one worker per stream, their queues and threads - is
 * `PipelineRunner`, which has no Qt. This class wraps it for the application: it delivers the
 * runner's reports to this object's thread as signals, and keeps the little state the UI asks
 * about (is a run active, how far along is it).
 * MainViewModel builds a list of fully-formed StreamJob objects (each carrying a
 * validated ProcessingParams plus an owned FrameSetup) and hands them off here.
 * The coordinator emits streamProcessed() with each stream's in-memory result and
 * processingFinished() when the run is over.
 */

#ifndef PROCESSINGCOORDINATOR_H
#define PROCESSINGCOORDINATOR_H

#include <QObject>
#include <QString>
#include <QVector>

#include "pipelinerunner.h"
#include "processedstreamdata.h"

/**
 * @brief Owns one pipeline run at a time and reports it as Qt signals.
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
    class Bridge;

    // Slots
    void onReaderProgress(int percent);
    void onRunFinished(bool success);

    PipelineRunner m_runner;
    bool m_processing        = false;
    int  m_progress_percent  = 0;
};

#endif // PROCESSINGCOORDINATOR_H
