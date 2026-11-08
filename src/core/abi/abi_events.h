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
        auto* result = new tmdq_stream_result{std::make_shared<const ProcessedStreamData>(std::move(data))};
        deliver([&] { m_callbacks.stream_done(m_callbacks.user, result); });
    }

    void processingFinished(bool success) override
    {
        if (m_callbacks.finished)
        {
            deliver([&] { m_callbacks.finished(m_callbacks.user, success ? 1 : 0); });
        }
    }

private:
    template <typename Call>
    static void deliver(Call&& call)
    {
        try
        {
            call();
        }
        catch (...)
        {
        }
    }

    tmdq_callbacks m_callbacks;
};

} // namespace tmdq

#endif // TMDQ_ABI_EVENTS_H
