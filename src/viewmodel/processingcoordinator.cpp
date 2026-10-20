/**
 * @file processingcoordinator.cpp
 * @brief Implementation of ProcessingCoordinator - PipelineRunner, reported as Qt signals.
 */

#include "processingcoordinator.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QMetaObject>

#include "constants.h"
#include "qtprocessingevents.h"

/// Delivers the runner's reports to the coordinator's thread, in order, as the signals the rest
/// of the application connects to. The four ordinary reports go through `QtProcessingEvents`;
/// a stream's result is posted the same way, so it stays in order with them: the runner raises
/// a stream's last log line before its result, and the end after every result.
class ProcessingCoordinator::Bridge final : public PipelineEvents
{
public:
    explicit Bridge(ProcessingCoordinator* owner)
        : m_owner(owner)
        , m_qt(owner,
               [owner](const QString& message) { emit owner->logMessageReceived(message); },
               [owner](const QString& message) { emit owner->errorOccurred(message); },
               [owner](bool success) { owner->onRunFinished(success); },
               [owner](int percent) { owner->onReaderProgress(percent); })
    {
    }

    void logMessage(const std::string& message) override { m_qt.logMessage(message); }
    void errorOccurred(const std::string& message) override { m_qt.errorOccurred(message); }
    void processingFinished(bool success) override { m_qt.processingFinished(success); }
    void progressUpdated(int percent) override { m_qt.progressUpdated(percent); }

    void streamProcessed(ProcessedStreamData data) override
    {
        QMetaObject::invokeMethod(
            m_owner,
            [owner = m_owner, result = std::move(data)]() { emit owner->streamProcessed(result); },
            Qt::QueuedConnection);
    }

private:
    ProcessingCoordinator* m_owner;
    QtProcessingEvents     m_qt;
};

ProcessingCoordinator::ProcessingCoordinator(QObject* parent)
    : QObject(parent)
{
}

ProcessingCoordinator::~ProcessingCoordinator()
{
    // The runner's threads post to this object, so they must end before it goes.
    m_runner.cancel();
    m_runner.wait();
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

    m_progress_percent = 0;
    const int stream_count = static_cast<int>(jobs.size());

    std::string error;
    if (!m_runner.start(std::vector<StreamJob>(jobs.begin(), jobs.end()),
                        std::make_shared<Bridge>(this), error))
    {
        emit errorOccurred(QString::fromStdString(error));
        return false;
    }

    // The runner's threads are already going, but everything they report reaches this object
    // as a queued event, so these come first.
    m_processing = true;
    emit processingStateChanged(true);
    emit progressChanged(0);
    emit logMessageReceived(QString("--- Processing %1 stream(s) concurrently ---")
                            .arg(stream_count));
    return true;
}

void ProcessingCoordinator::cancelProcessing()
{
    if (!m_processing)
    {
        return;
    }
    m_runner.cancel();
}

void ProcessingCoordinator::reset()
{
    if (m_processing)
    {
        return;
    }
    m_progress_percent = 0;
}

////////////////////////////////////////////////////////////////////////////////
//                            SLOTS                                           //
////////////////////////////////////////////////////////////////////////////////

void ProcessingCoordinator::onReaderProgress(int percent)
{
    m_progress_percent = percent;
    emit progressChanged(percent);
}

void ProcessingCoordinator::onRunFinished(bool success)
{
    m_progress_percent = UIConstants::kProgressBarMax;
    m_processing = false;

    emit progressChanged(m_progress_percent);
    emit processingStateChanged(false);
    emit processingFinished(success);
}
