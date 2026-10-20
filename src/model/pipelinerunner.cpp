/**
 * @file pipelinerunner.cpp
 * @brief Implementation of PipelineRunner - the single reader and the parallel stream workers.
 */

#include "pipelinerunner.h"

#include <utility>

#include "ch10packetreader.h"
#include "frameprocessor.h"
#include "packetqueue.h"

/// What the reader and every stream's processor report to. Their log lines, errors and the
/// reader's progress pass straight through to the run's sink. A processor's own completion is
/// NOT forwarded: the run is over when every thread has ended, and the runner reports that once,
/// itself, not once per stream.
class PipelineRunner::Relay final : public ProcessingEvents
{
public:
    explicit Relay(std::shared_ptr<PipelineEvents> sink) : m_sink(std::move(sink)) {}

    void logMessage(const std::string& message) override { m_sink->logMessage(message); }
    void errorOccurred(const std::string& message) override { m_sink->errorOccurred(message); }
    void processingFinished(bool) override {}
    void progressUpdated(int percent) override { m_sink->progressUpdated(percent); }

private:
    std::shared_ptr<PipelineEvents> m_sink;
};

PipelineRunner::PipelineRunner() = default;

PipelineRunner::~PipelineRunner()
{
    cancel();
    joinDriver();
}

void PipelineRunner::joinDriver()
{
    if (m_driver.joinable())
    {
        m_driver.join();
    }
}

void PipelineRunner::wait()
{
    joinDriver();
}

bool PipelineRunner::start(std::vector<StreamJob> jobs, std::shared_ptr<PipelineEvents> events,
                           std::string& error)
{
    if (m_running.exchange(true))
    {
        error = "A processing run is already in progress.";
        return false;
    }
    // A previous run's reader thread may still be returning from its last report.
    joinDriver();

    if (jobs.empty())
    {
        error = "No streams were given to process.";
        m_running = false;
        return false;
    }

    m_events = std::move(events);
    m_cancelled = false;
    m_any_success = false;

    // Set up under the lock, so a cancel() that races the start sees whole objects or none.
    std::shared_ptr<Ch10PacketReader> reader;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs = std::move(jobs);

        // One queue per stream; wire each into its params before resolving attributes.
        m_queues.clear();
        m_queues.reserve(m_jobs.size());
        for (StreamJob& job : m_jobs)
        {
            m_queues.push_back(std::make_unique<PacketQueue>());
            job.params.packetQueue = m_queues.back().get();
        }
        m_reader = std::make_shared<Ch10PacketReader>();
        reader = m_reader;
    }

    // Resolve the TMATS-derived attributes once, synchronously, through the reader.
    std::vector<ProcessingParams*> params_list;
    params_list.reserve(m_jobs.size());
    for (StreamJob& job : m_jobs)
    {
        params_list.push_back(&job.params);
    }
    if (!reader->prepare(m_jobs[0].params.filename, m_jobs[0].params.timeChannelId, params_list, error))
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_reader.reset();
        m_queues.clear();
        m_jobs.clear();
        m_running = false;
        return false;
    }

    // What the reader and every processor report goes through one relay to the run's sink.
    const auto relay = std::make_shared<Relay>(m_events);
    reader->setEvents(relay);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_workers.clear();
        m_workers.reserve(m_jobs.size());
        for (std::size_t i = 0; i < m_jobs.size(); ++i)
        {
            auto processor = std::make_shared<FrameProcessor>();
            processor->setEvents(relay);
            m_workers.push_back(std::move(processor));
        }
    }

    m_driver = std::thread([this]() { drive(); });
    return true;
}

void PipelineRunner::cancel()
{
    if (!m_running)
    {
        return;
    }
    m_cancelled = true;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_reader != nullptr)
    {
        m_reader->requestAbort();
    }
    for (const std::shared_ptr<FrameProcessor>& worker : m_workers)
    {
        worker->requestAbort();
    }
    // Unblock anyone waiting on a queue.
    for (const std::unique_ptr<PacketQueue>& queue : m_queues)
    {
        queue->close();
    }
}

void PipelineRunner::drive()
{
    std::shared_ptr<Ch10PacketReader> reader;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        reader = m_reader;
    }

    // The streams first, so each is waiting on its (still empty) queue before the reader produces.
    std::vector<std::thread> streams;
    streams.reserve(m_jobs.size());
    for (std::size_t i = 0; i < m_jobs.size(); ++i)
    {
        streams.emplace_back([this, i]() { runStream(i); });
    }

    reader->run();          // this thread is the reader's

    for (std::thread& stream : streams)
    {
        stream.join();
    }
    reader.reset();         // before the report: with the join above, the last reference but the member's
    finish();
}

void PipelineRunner::runStream(std::size_t index)
{
    StreamJob& job = m_jobs[index];
    FrameProcessor& processor = *m_workers[index];

    // process() reports its own log lines and errors through the relay; its return value is the
    // outcome it would have reported as "finished".
    const bool ok = processor.process(job.params, job.frameSetup.get());
    if (ok && !m_cancelled)
    {
        ProcessedStreamData data = processor.takeResult();
        data.jobIndex = static_cast<int>(index);
        data.sourceId = job.params.sourceId;
        m_any_success = true;
        m_events->streamProcessed(std::move(data));
    }
}

void PipelineRunner::finish()
{
    {
        // Every thread that used the pipeline has been joined, so these are the last references.
        std::lock_guard<std::mutex> lock(m_mutex);
        m_workers.clear();
        m_reader.reset();
        m_queues.clear();   // after the streams that read them have ended
        m_jobs.clear();     // each job's shared_ptr frees its frame setup
    }
    const bool success = m_any_success && !m_cancelled;
    const std::shared_ptr<PipelineEvents> events = m_events;
    // Before the report, so a handler that starts the next run is not turned away.
    m_running = false;
    events->processingFinished(success);
}
