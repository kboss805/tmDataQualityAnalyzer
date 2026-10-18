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
#include "qtbridge.h"
#include "qtprocessingevents.h"

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
    m_reader = std::make_shared<Ch10PacketReader>();
    std::vector<ProcessingParams*> params_list;
    params_list.reserve(m_jobs.size());
    for (StreamJob& job : m_jobs)
    {
        params_list.push_back(&job.params);
    }

    const std::filesystem::path filename = m_jobs[0].params.filename;
    const int     time_channel   = m_jobs[0].params.timeChannelId;

    std::string error;
    if (!m_reader->prepare(filename, time_channel, params_list, error))
    {
        emit errorOccurred(QString::fromStdString(error));
        m_reader.reset();
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
        w.processor = std::make_shared<FrameProcessor>();
        w.queue     = m_queues[i];

        // What the processor reports reaches this object's thread as queued events, in
        // order - the same delivery the signals it replaced gave. The finish handler looks
        // the processor up by index at delivery time rather than holding a pointer to it,
        // so an event arriving after teardown finds nothing instead of a dangling object.
        w.processor->setEvents(std::make_shared<QtProcessingEvents>(
            this,
            [this](const QString& message) { emit logMessageReceived(message); },
            [this](const QString& message) { emit errorOccurred(message); },
            [this, i](bool ok) { onWorkerFinished(ok, i); }));

        ProcessingParams params = m_jobs[i].params; // includes resolvedAttrs + queue
        // Captured by shared_ptr (not .get()) so the thread's body itself keeps FrameSetup
        // and the processor alive for as long as it can run — enforced by the type
        // system rather than by relying on teardownAll() always running (and joining
        // every worker thread) before m_jobs is ever cleared/reassigned.
        std::shared_ptr<FrameSetup> setup = m_jobs[i].frameSetup;
        std::shared_ptr<FrameProcessor> processor = w.processor;
        // QThread::create runs the body on the new thread and ends the thread when it
        // returns. The processor no longer needs an event loop of its own.
        w.thread = QThread::create([processor, params, setup]() {
            processor->process(params, setup.get());
        });

        m_workers.push_back(w);
    }

    // Reader thread.
    // Its reports reach this object's thread as queued events, in order (the delivery the
    // signals it replaced gave). No completion handler: the thread is joined at teardown.
    m_reader->setEvents(std::make_shared<QtProcessingEvents>(
        this,
        [this](const QString& message) { emit logMessageReceived(message); },
        [this](const QString& message) { emit errorOccurred(message); },
        nullptr,
        [this](int percent) { onReaderProgress(percent); }));
    std::shared_ptr<Ch10PacketReader> reader = m_reader;
    m_reader_thread = QThread::create([reader]() { reader->run(); });

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
    // Stop worker threads. Each processor is freed with m_workers, below, once its thread
    // has been joined.
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
    m_reader.reset(); // the reader thread is joined above, so this is the last reference

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

void ProcessingCoordinator::onWorkerFinished(bool success, int job_index)
{
    FrameProcessor* processor = (job_index >= 0 && job_index < m_workers.size())
        ? m_workers[job_index].processor.get()
        : nullptr;
    if (success && !m_cancelled && processor != nullptr)
    {
        ProcessedStreamData data = processor->takeResult();
        data.jobIndex = job_index;
        data.sourceId = m_jobs[job_index].params.sourceId;
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
