/**
 * @file pipelinerunner.cpp
 * @brief Implementation of PipelineRunner - the single reader and the parallel stream workers.
 */

#include "pipelinerunner.h"

#include <format>
#include <utility>

#include "ch10packetreader.h"
#include "exceptionboundary.h"
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
    m_failed = false;
    m_read_failed = false;

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

void PipelineRunner::failRun(const std::string& message)
{
    m_failed = true;
    m_events->errorOccurred(message);
}

void PipelineRunner::drive()
{
    std::shared_ptr<Ch10PacketReader> reader;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        reader = m_reader;
    }

    // The streams first, so each is waiting on its (still empty) queue before the reader produces. Nothing thrown here
    // may leave the thread (it would end the host process): a thread that cannot start, or a read that throws, fails
    // the run instead. The read stopped part-way, or never began, so no stream may report a result built from part of
    // the recording (m_read_failed, set before the queues close), and every stream waiting on a queue is let go.
    std::vector<std::thread> streams;
    containExceptions(
        [&]() {
            streams.reserve(m_jobs.size());
            for (std::size_t i = 0; i < m_jobs.size(); ++i)
            {
                streams.emplace_back([this, i]() { runStream(i); });
            }
            reader->run();          // this thread is the reader's
        },
        [&](const char* what) {
            m_read_failed = true;
            failRun(std::format("Reading the recording stopped on an internal error ({}); the run was abandoned.",
                                what));
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const std::unique_ptr<PacketQueue>& queue : m_queues)
            {
                queue->close();
            }
        });

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

    // Whatever the stream throws (a std::bad_alloc on a long run, a sink's own failure) ends this stream, not the host
    // process: it is reported, and the run ends unsuccessfully. The other streams carry on.
    containExceptions(
        [&]() {
            FrameProcessor& processor = *m_workers[index];

            // process() reports its own log lines and errors through the relay; its return value is the
            // outcome it would have reported as "finished".
            const bool ok = processor.process(job.params, job.frameSetup.get());

            if (ok && !m_cancelled && !m_read_failed)
            {
                ProcessedStreamData data = processor.takeResult();
                data.jobIndex = static_cast<int>(index);
                data.sourceId = job.params.sourceId;
                m_any_success = true;
                m_events->streamProcessed(std::move(data));
            }
        },
        [&](const char* what) {
            failRun(std::format("Stream {} ({}) stopped on an internal error ({}); its result was dropped.",
                                index + 1, job.params.streamLabel, what));
        });

    // However it ended, nothing more will be read from this stream's queue. The reader hands every
    // packet to every stream and blocks while a queue is full, so a stream that stopped early (its
    // processor aborted or threw, not the run cancelled) must close its queue or the reader waits on it
    // forever and every other stream starves. Closing a drained queue after a normal end changes nothing.
    job.params.packetQueue->close();
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
    const bool success = m_any_success && !m_cancelled && !m_failed;
    const std::shared_ptr<PipelineEvents> events = m_events;
    // Before the report, so a handler that starts the next run is not turned away.
    m_running = false;
    // On the reader thread, the last thing it does: a sink that throws must not take the host down with it.
    containExceptions([&]() { events->processingFinished(success); }, [](const char*) {});
}
