/**
 * @file qtprocessingevents.h
 * @brief Delivers `ProcessingEvents` onto a Qt object's thread.
 *
 * The core calls `ProcessingEvents` on the thread that is processing. The Qt side (the
 * coordinator, the calibration extractor) wants those calls on the UI thread, in order,
 * exactly as queued signals delivered them. This posts each one to a target `QObject`
 * with a queued invocation, which is the same mechanism and so the same ordering:
 * events from one source arrive in the order they were raised.
 *
 * Includes Qt, so it belongs to the layers that are staying Qt (ViewModel, and the
 * model classes not yet converted) - never to a header the core is meant to be free of
 * Qt in.
 *
 * **Lifetime.** The target must outlive the events it has been sent, or at least the
 * thread that raises them. Both users join their worker threads before they are
 * destroyed, and Qt discards events still queued for a destroyed target.
 */

#ifndef QTPROCESSINGEVENTS_H
#define QTPROCESSINGEVENTS_H

#include <functional>
#include <string>
#include <utility>

#include <QMetaObject>
#include <QObject>
#include <QString>

#include "processingevents.h"

class QtProcessingEvents final : public ProcessingEvents
{
public:
    using TextHandler     = std::function<void(const QString&)>;
    using DoneHandler     = std::function<void(bool)>;
    using ProgressHandler = std::function<void(int)>;

    /// @p target is the object whose thread runs the handlers (normally the UI thread's
    /// owner). Each handler is optional.
    QtProcessingEvents(QObject* target, TextHandler on_log, TextHandler on_error,
                       DoneHandler on_finished, ProgressHandler on_progress = nullptr)
        : m_target(target)
        , m_on_log(std::move(on_log))
        , m_on_error(std::move(on_error))
        , m_on_finished(std::move(on_finished))
        , m_on_progress(std::move(on_progress))
    {
    }

    void logMessage(const std::string& message) override
    {
        postText(m_on_log, message);
    }

    void errorOccurred(const std::string& message) override
    {
        postText(m_on_error, message);
    }

    void processingFinished(bool success) override
    {
        if (!m_on_finished)
        {
            return;
        }
        // The handler is copied into the lambda: this object may be gone by the time the
        // queued call runs, and the handler must not be read from the wrong thread.
        QMetaObject::invokeMethod(
            m_target, [handler = m_on_finished, success]() { handler(success); },
            Qt::QueuedConnection);
    }

    void progressUpdated(int percent) override
    {
        if (!m_on_progress)
        {
            return;
        }
        QMetaObject::invokeMethod(
            m_target, [handler = m_on_progress, percent]() { handler(percent); },
            Qt::QueuedConnection);
    }

private:
    void postText(const TextHandler& handler, const std::string& message)
    {
        if (!handler)
        {
            return;
        }
        QMetaObject::invokeMethod(
            m_target,
            [handler, text = QString::fromStdString(message)]() { handler(text); },
            Qt::QueuedConnection);
    }

    QObject*    m_target;
    TextHandler m_on_log;
    TextHandler m_on_error;
    DoneHandler m_on_finished;
    ProgressHandler m_on_progress;
};

#endif // QTPROCESSINGEVENTS_H
