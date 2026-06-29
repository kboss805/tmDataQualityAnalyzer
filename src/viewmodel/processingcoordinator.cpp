/**
 * @file processingcoordinator.cpp
 * @brief Implementation of ProcessingCoordinator — single reader + parallel workers.
 */

#include "processingcoordinator.h"

#include "constants.h"
#include "ch10packetreader.h"
#include "frameprocessor.h"
#include "framesetup.h"
#include "packetqueue.h"

ProcessingCoordinator::ProcessingCoordinator(QObject* parent)
    : QObject(parent)
{
}

ProcessingCoordinator::~ProcessingCoordinator()
{
    teardownAll();
    clearJobs();
}

////////////////////////////////////////////////////////////////////////////////
//                            STATE QUERIES                                   //
////////////////////////////////////////////////////////////////////////////////

bool  ProcessingCoordinator::processing()      const { return m_processing; }
int   ProcessingCoordinator::progressPercent() const { return m_progress_percent; }

////////////////////////////////////////////////////////////////////////////////
//                            PUBLIC API                                      //
////////////////////////////////////////////////////////////////////////////////

bool ProcessingCoordinator::startProcessing(QVector<StreamJob> jobs)
{
    if (m_processing)
    {
        return false; // jobs (and their owned FrameSetups) free automatically on return
    }

    if (jobs.isEmpty())
    {
        emit errorOccurred("No streams selected for processing.");
        return false;
    }

    clearJobs();
    m_jobs              = std::move(jobs);
    m_any_success       = false;
    m_cancelled         = false;
    m_progress_percent  = 0;

    // One queue per stream; wire each into its params before resolving attributes.
    m_queues.clear();
    m_queues.reserve(m_jobs.size());
    for (StreamJob& job : m_jobs)
    {
        auto* q = new PacketQueue();
        m_queues.push_back(q);
        job.params.packetQueue = q;
    }

    // Resolve TMATS-derived attributes once, synchronously, via the reader.
    m_reader = new Ch10PacketReader;
    QVector<ProcessingParams*> params_list;
    params_list.reserve(m_jobs.size());
    for (StreamJob& job : m_jobs)
    {
        params_list.push_back(&job.params);
    }

    const QString filename       = m_jobs[0].params.filename;
    const int     time_channel   = m_jobs[0].params.timeChannelId;

    QString error;
    if (!m_reader->prepare(filename, time_channel, params_list, error))
    {
        emit errorOccurred(error);
        delete m_reader;
        m_reader = nullptr;
        for (PacketQueue* q : m_queues) { delete q; }
        m_queues.clear();
        clearJobs();
        return false;
    }

    // Spin up one worker thread per stream. Workers block on their (empty)
    // queues until the reader begins producing.
    m_workers.clear();
    m_workers.reserve(m_jobs.size());
    m_workers_remaining = static_cast<int>(m_jobs.size());

    for (int i = 0; i < m_jobs.size(); ++i)
    {
        Worker w;
        w.thread    = new QThread;
        w.processor = new FrameProcessor;
        w.queue     = m_queues[i];
        w.processor->moveToThread(w.thread);

        FrameProcessor* processor = w.processor;
        connect(processor, &FrameProcessor::processingFinished, this,
                [this, processor, i](bool ok) { onWorkerFinished(processor, ok, i); });
        connect(processor, &FrameProcessor::logMessage,
                this, &ProcessingCoordinator::logMessageReceived);
        connect(processor, &FrameProcessor::errorOccurred,
                this, &ProcessingCoordinator::errorOccurred);
        connect(w.thread, &QThread::finished, processor, &QObject::deleteLater);

        ProcessingParams params = m_jobs[i].params; // includes resolvedAttrs + queue
        FrameSetup* setup = m_jobs[i].frameSetup.get(); // worker borrows; coordinator retains ownership
        connect(w.thread, &QThread::started, processor, [processor, params, setup]() {
            processor->process(params, setup);
        });

        m_workers.push_back(w);
    }

    // Reader thread.
    m_reader_thread = new QThread;
    m_reader->moveToThread(m_reader_thread);
    connect(m_reader, &Ch10PacketReader::progressUpdated,
            this, &ProcessingCoordinator::onReaderProgress);
    connect(m_reader, &Ch10PacketReader::logMessage,
            this, &ProcessingCoordinator::logMessageReceived);
    connect(m_reader, &Ch10PacketReader::errorOccurred,
            this, &ProcessingCoordinator::errorOccurred);
    connect(m_reader_thread, &QThread::started, m_reader, &Ch10PacketReader::run);

    m_processing = true;
    emit processingStateChanged(true);
    emit progressChanged(0);
    emit logMessageReceived(QString("--- Processing %1 stream(s) concurrently ---")
                            .arg(m_jobs.size()));

    // Start workers first so they are waiting before the reader produces.
    for (Worker& w : m_workers)
    {
        w.thread->start();
    }
    m_reader_thread->start();

    return true;
}

void ProcessingCoordinator::cancelProcessing()
{
    if (!m_processing)
    {
        return;
    }
    m_cancelled = true;

    if (m_reader != nullptr)
    {
        m_reader->requestAbort();
    }
    for (Worker& w : m_workers)
    {
        if (w.processor != nullptr)
        {
            w.processor->requestAbort();
        }
    }
    // Unblock anyone waiting on a queue.
    for (PacketQueue* q : m_queues)
    {
        q->close();
    }
}

void ProcessingCoordinator::reset()
{
    if (m_processing)
    {
        return;
    }
    m_progress_percent = 0;
    clearJobs();
}

////////////////////////////////////////////////////////////////////////////////
//                            PRIVATE HELPERS                                 //
////////////////////////////////////////////////////////////////////////////////

void ProcessingCoordinator::clearJobs()
{
    m_jobs.clear(); // each StreamJob's shared_ptr frees its owned FrameSetup
}

void ProcessingCoordinator::teardownAll()
{
    // Stop worker threads (processors auto-delete via deleteLater on finished).
    for (Worker& w : m_workers)
    {
        if (w.thread != nullptr)
        {
            w.thread->quit();
            w.thread->wait();
            delete w.thread;
        }
    }
    m_workers.clear();

    // Stop the reader thread.
    if (m_reader_thread != nullptr)
    {
        m_reader_thread->quit();
        m_reader_thread->wait();
        delete m_reader_thread;
        m_reader_thread = nullptr;
    }
    if (m_reader != nullptr)
    {
        delete m_reader;
        m_reader = nullptr;
    }

    // Queues are safe to delete now that all threads have stopped.
    for (PacketQueue* q : m_queues)
    {
        delete q;
    }
    m_queues.clear();
}

void ProcessingCoordinator::finalize()
{
    teardownAll();

    m_progress_percent = UIConstants::kProgressBarMax;
    m_processing = false;
    clearJobs();

    emit progressChanged(m_progress_percent);
    emit processingStateChanged(false);
    emit processingFinished(m_any_success && !m_cancelled);
}

////////////////////////////////////////////////////////////////////////////////
//                            SLOTS                                           //
////////////////////////////////////////////////////////////////////////////////

void ProcessingCoordinator::onReaderProgress(int percent)
{
    m_progress_percent = percent;
    emit progressChanged(percent);
}

void ProcessingCoordinator::onWorkerFinished(FrameProcessor* processor, bool success, int job_index)
{
    if (success && !m_cancelled && processor != nullptr)
    {
        ProcessedStreamData data = processor->takeResult();
        data.jobIndex = job_index;
        m_any_success = true;
        emit streamProcessed(data);
    }

    if (m_workers_remaining > 0)
    {
        m_workers_remaining--;
    }

    if (m_workers_remaining == 0)
    {
        finalize();
    }
}
