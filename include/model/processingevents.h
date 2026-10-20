/**
 * @file processingevents.h
 * @brief How the processing core reports what it is doing, without Qt.
 *
 * The processing classes used to be `QObject`s that emitted signals: a log line, an
 * error, "finished". Signals meant the core needed
 * `moc`, a thread with an event loop, and a `QString` for every message. This interface
 * replaces them with a plain callback the core calls, so the core links nothing from Qt
 * and can sit behind a C ABI (the callbacks map onto a table of function pointers).
 *
 * **Threading contract.** Every method is called on the thread that is running the
 * processing (a worker thread, in production), in the order the events happen. An
 * implementation that must deliver them somewhere else - the UI thread - has to do its
 * own hand-off; `qtprocessingevents.h` is the Qt one. Messages are UTF-8.
 *
 * Calls arrive in order per source. Which events a source raises depends on what it is:
 * `FrameProcessor` raises log / error / finished, and `processingFinished` is always its
 * last call, so a receiver may take its result as soon as it sees it; `Ch10PacketReader`
 * raises log / error / progress and no completion event (its owner joins the thread).
 */

#ifndef PROCESSINGEVENTS_H
#define PROCESSINGEVENTS_H

#include <memory>
#include <string>
#include <utility>

class ProcessingEvents
{
public:
    virtual ~ProcessingEvents() = default;

    /// A human-readable status line (a stage reached, a warning, a summary).
    virtual void logMessage(const std::string& message) = 0;

    /// A failure report. A source that has a completion event (`FrameProcessor`,
    /// `CalibrationExtractor`) follows it with `processingFinished(false)`; the packet
    /// reader has none.
    virtual void errorOccurred(const std::string& message) = 0;

    /// The run is over. @p success is true only on clean completion.
    virtual void processingFinished(bool success) = 0;

    /// How far through its input a source has got, 0..100. Optional: a sink that does not
    /// show progress need not override it.
    virtual void progressUpdated(int percent) { (void)percent; }
};

/**
 * @brief An optional `ProcessingEvents` sink that is safe to call whether or not one is set.
 *
 * Each source (the packet reader, the frame processor, the calibration extractor) reports
 * through one of these instead of repeating "if a sink is set, forward to it". With no sink
 * every call does nothing, which is what a source with nobody listening should do. Set the
 * sink before the source starts running; changing it while a run is in flight is not safe.
 */
class EventReporter
{
public:
    void setSink(std::shared_ptr<ProcessingEvents> sink) { m_sink = std::move(sink); }

    void log(const std::string& message) const
    {
        if (m_sink)
        {
            m_sink->logMessage(message);
        }
    }

    void error(const std::string& message) const
    {
        if (m_sink)
        {
            m_sink->errorOccurred(message);
        }
    }

    void finished(bool success) const
    {
        if (m_sink)
        {
            m_sink->processingFinished(success);
        }
    }

    void progress(int percent) const
    {
        if (m_sink)
        {
            m_sink->progressUpdated(percent);
        }
    }

private:
    std::shared_ptr<ProcessingEvents> m_sink;
};

#endif // PROCESSINGEVENTS_H
