/**
 * @file abi_events.h
 * @brief What the runs and the calibration share: a finished stream's result, and the sink that forwards the
 *        core's reports to the host's function-pointer callbacks. Not part of the ABI.
 */

#ifndef TMDQ_ABI_EVENTS_H
#define TMDQ_ABI_EVENTS_H

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "pipelinerunner.h"
#include "processedstreamdata.h"
#include "tmdq_abi.h"

/// One finished stream. The host owns it from `stream_done` until tmdq_stream_result_free. The data is
/// shared: a series set built from the result keeps it alive after the host has freed the result.
struct tmdq_stream_result
{
    std::shared_ptr<const ProcessedStreamData> data;
};

namespace tmdq {

/// Forwards the runner's reports to the host's callbacks. Whatever a callback throws is swallowed:
/// it came from foreign code and must not unwind through the runner's threads.
class CallbackEvents final : public PipelineEvents
{
public:
    explicit CallbackEvents(const tmdq_callbacks& callbacks) : m_callbacks(callbacks) {}

    /// Whether the calling thread is inside one of this sink's callbacks. A handle's wait and free join the
    /// threads its callbacks arrive on, so from a callback they would join their own thread (which ends the
    /// process, out of a destructor) or a thread that is waiting for this callback to return.
    bool delivering() const { return t_delivering == this; }

    void logMessage(const std::string& message) override
    {
        if (m_callbacks.log)
        {
            deliver([&] { m_callbacks.log(m_callbacks.user, message.c_str()); });
        }
    }

    void errorOccurred(const std::string& message) override
    {
        if (m_callbacks.error)
        {
            deliver([&] { m_callbacks.error(m_callbacks.user, message.c_str()); });
        }
    }

    void progressUpdated(int percent) override
    {
        if (m_callbacks.progress)
        {
            deliver([&] { m_callbacks.progress(m_callbacks.user, static_cast<int32_t>(percent)); });
        }
    }

    void streamProcessed(ProcessedStreamData data) override
    {
        if (!m_callbacks.stream_done)
        {
            return;   // nobody to hand it to
        }
        // Built outside deliver(), on purpose: a result that cannot be allocated (std::bad_alloc on a huge run) must not
        // be swallowed as a callback's failure would be, which would end the run "successful" with a stream the host
        // never got. It propagates to the stream's thread boundary (PipelineRunner::runStream), which reports it and
        // fails the run. Owned here until the host takes it, so nothing leaks on the way.
        auto result = std::make_unique<tmdq_stream_result>(
            tmdq_stream_result{std::make_shared<const ProcessedStreamData>(std::move(data))});
        deliver([&] { m_callbacks.stream_done(m_callbacks.user, result.release()); });
    }

    void processingFinished(bool success) override
    {
        if (m_callbacks.finished)
        {
            deliver([&] { m_callbacks.finished(m_callbacks.user, success ? 1 : 0); });
        }
    }

private:
    /// The sink whose callback this thread is inside (the innermost, if a callback started another run).
    static inline thread_local const CallbackEvents* t_delivering = nullptr;

    template <typename Call>
    void deliver(Call&& call) const
    {
        struct Inside
        {
            explicit Inside(const CallbackEvents* sink) : outer(t_delivering) { t_delivering = sink; }
            ~Inside() { t_delivering = outer; }
            Inside(const Inside&) = delete;
            Inside& operator=(const Inside&) = delete;
            const CallbackEvents* outer;
        };
        const Inside inside(this);
        try
        {
            call();
        }
        // NOLINTNEXTLINE(bugprone-empty-catch): nothing may unwind a host's thread, and nothing is left to tell
        catch (...)
        {
        }
    }

    tmdq_callbacks m_callbacks;
};

} // namespace tmdq

#endif // TMDQ_ABI_EVENTS_H
