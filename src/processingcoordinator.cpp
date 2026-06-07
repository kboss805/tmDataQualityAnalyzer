/**
 * @file processingcoordinator.cpp
 * @brief Implementation of ProcessingCoordinator — sequential multi-stream worker lifecycle.
 */

#include "processingcoordinator.h"

#include "constants.h"
#include "frameprocessor.h"
#include "framesetup.h"

ProcessingCoordinator::ProcessingCoordinator(QObject* parent)
    : QObject(parent)
{
}

ProcessingCoordinator::~ProcessingCoordinator()
{
    if (m_worker_thread != nullptr && m_worker_thread->isRunning())
    {
        if (m_current_processor != nullptr)
        {
            m_current_processor->requestAbort();
        }
        m_worker_thread->quit();
        m_worker_thread->wait();
    }
    delete m_worker_thread;
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
        // Already running — free the rejected jobs' frame setups.
        for (StreamJob& job : jobs)
        {
            delete job.frameSetup;
        }
        return false;
    }

    if (jobs.isEmpty())
    {
        emit errorOccurred("No streams selected for processing.");
        return false;
    }

    clearJobs();
    m_jobs          = std::move(jobs);
    m_job_index     = 0;
    m_any_success   = false;
    m_cancelled     = false;
    m_processing    = true;
    m_progress_percent = 0;

    emit processingStateChanged(true);
    emit progressChanged(0);
    launchWorkerThread();
    return true;
}

void ProcessingCoordinator::cancelProcessing()
{
    m_cancelled = true;
    if (m_current_processor != nullptr)
    {
        m_current_processor->requestAbort();
    }
}

void ProcessingCoordinator::reset()
{
    m_progress_percent = 0;
    m_processing       = false;
    clearJobs();
}

////////////////////////////////////////////////////////////////////////////////
//                            PRIVATE HELPERS                                 //
////////////////////////////////////////////////////////////////////////////////

void ProcessingCoordinator::clearJobs()
{
    for (StreamJob& job : m_jobs)
    {
        delete job.frameSetup;
        job.frameSetup = nullptr;
    }
    m_jobs.clear();
    m_job_index = 0;
}

void ProcessingCoordinator::launchWorkerThread()
{
    teardownWorkerThread();

    StreamJob& job = m_jobs[m_job_index];

    m_worker_thread = new QThread;
    auto* processor = new FrameProcessor;
    m_current_processor = processor;
    processor->moveToThread(m_worker_thread);

    connect(processor, &FrameProcessor::progressUpdated,
            this, &ProcessingCoordinator::onProgressUpdated);
    connect(processor, &FrameProcessor::processingFinished,
            this, &ProcessingCoordinator::onProcessingFinished);
    connect(processor, &FrameProcessor::logMessage,
            this, &ProcessingCoordinator::onLogMessage);
    connect(processor, &FrameProcessor::errorOccurred,
            this, &ProcessingCoordinator::errorOccurred);

    connect(m_worker_thread, &QThread::finished,
            processor, &QObject::deleteLater);

    ProcessingParams params = job.params;
    FrameSetup* setup = job.frameSetup;
    connect(m_worker_thread, &QThread::started, processor, [processor, params, setup]() {
        processor->process(params, setup);
    });

    if (m_jobs.size() > 1)
    {
        emit logMessageReceived(QString("--- Stream %1 of %2: %3 ---")
            .arg(m_job_index + 1)
            .arg(m_jobs.size())
            .arg(job.params.stream_label));
    }

    m_worker_thread->start();
}

void ProcessingCoordinator::teardownWorkerThread()
{
    if (m_worker_thread != nullptr)
    {
        m_worker_thread->quit();
        m_worker_thread->wait();
        delete m_worker_thread;
        m_worker_thread     = nullptr;
        m_current_processor = nullptr;
    }
}

////////////////////////////////////////////////////////////////////////////////
//                            SLOTS                                           //
////////////////////////////////////////////////////////////////////////////////

void ProcessingCoordinator::onProgressUpdated(int percent)
{
    // Blend per-job progress into an overall percentage across the whole queue.
    const int total = m_jobs.isEmpty() ? 1 : static_cast<int>(m_jobs.size());
    m_progress_percent = ((m_job_index * UIConstants::kProgressBarMax) + percent) / total;
    emit progressChanged(m_progress_percent);
}

void ProcessingCoordinator::onProcessingFinished(bool success)
{
    // The worker's process() has returned; collect its in-memory result before teardown.
    if (success && m_current_processor != nullptr)
    {
        ProcessedStreamData data = m_current_processor->takeResult();
        m_any_success = true;
        emit streamProcessed(data);
    }

    teardownWorkerThread();

    m_job_index++;

    if (!m_cancelled && m_job_index < m_jobs.size())
    {
        launchWorkerThread();
        return;
    }

    // Queue drained (or cancelled) — finalize.
    m_progress_percent = UIConstants::kProgressBarMax;
    m_processing = false;
    clearJobs();

    emit progressChanged(m_progress_percent);
    emit processingStateChanged(false);
    emit processingFinished(m_any_success && !m_cancelled);
}

void ProcessingCoordinator::onLogMessage(const QString& message)
{
    emit logMessageReceived(message);
}
