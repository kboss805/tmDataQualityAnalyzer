#include "tst_qtprocessingevents.h"

#include <string>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include "qtprocessingevents.h"

namespace {

/// One delivered event, with the thread it was delivered on.
struct Seen
{
    QThread* thread;
    char     kind;     ///< 'L' log, 'E' error, 'F' finished
    QString  text;
    bool     success = false;
};

} // namespace

void TestQtProcessingEvents::deliversOnTheTargetsThreadInOrder()
{
    // A worker thread raises 300 events of mixed kinds, then finishes. Every handler must
    // run on THIS thread (the target's), and in exactly the order they were raised,
    // across kinds - the log lines of a run have to precede its completion, because the
    // receiver takes the result as soon as it sees completion.
    QObject target;
    std::vector<Seen> seen;
    QEventLoop loop;

    QtProcessingEvents events(
        &target,
        [&](const QString& m) { seen.push_back({QThread::currentThread(), 'L', m}); },
        [&](const QString& m) { seen.push_back({QThread::currentThread(), 'E', m}); },
        [&](bool ok) {
            seen.push_back({QThread::currentThread(), 'F', QString(), ok});
            loop.quit();
        });

    QThread* worker_thread = nullptr;
    std::thread worker([&]() {
        worker_thread = QThread::currentThread();
        for (int i = 0; i < 300; ++i)
        {
            const std::string text = "event " + std::to_string(i);
            if (i % 3 == 0)
            {
                events.errorOccurred(text);
            }
            else
            {
                events.logMessage(text);
            }
        }
        events.processingFinished(true);
    });

    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    worker.join();

    QVERIFY2(!seen.empty() && seen.back().kind == 'F', "completion never arrived");
    QVERIFY(worker_thread != QThread::currentThread()); // the test really crossed threads
    QCOMPARE(seen.size(), std::size_t{301});
    for (std::size_t i = 0; i + 1 < seen.size(); ++i)
    {
        QCOMPARE(seen[i].thread, QThread::currentThread());
        QCOMPARE(seen[i].kind, i % 3 == 0 ? 'E' : 'L');
        QCOMPARE(seen[i].text, QStringLiteral("event %1").arg(i));
    }
    QCOMPARE(seen.back().thread, QThread::currentThread());
    QVERIFY(seen.back().success);
}

void TestQtProcessingEvents::messagesAreDecodedFromUtf8()
{
    // The core's messages are UTF-8; the em dash and an accented letter must survive.
    QObject target;
    QStringList logs;
    QtProcessingEvents events(&target, [&](const QString& m) { logs << m; }, nullptr, nullptr);

    events.logMessage("caf\xC3\xA9 \xE2\x80\x94 ok");
    QCoreApplication::processEvents();

    QCOMPARE(logs, QStringList({QStringLiteral("caf") + QChar(0x00E9) + QStringLiteral(" ")
                                + QChar(0x2014) + QStringLiteral(" ok")}));
}

void TestQtProcessingEvents::missingHandlersAreSkipped()
{
    // Any of the three handlers may be left out; the call is then a no-op, not a crash.
    QObject target;
    QtProcessingEvents none(&target, nullptr, nullptr, nullptr);
    none.logMessage("ignored");
    none.errorOccurred("ignored");
    none.processingFinished(true);
    QCoreApplication::processEvents();

    int finished = 0;
    QtProcessingEvents only_finished(&target, nullptr, nullptr, [&](bool) { ++finished; });
    only_finished.logMessage("ignored");
    only_finished.errorOccurred("ignored");
    only_finished.processingFinished(false);
    QCoreApplication::processEvents();
    QCOMPARE(finished, 1);
}

void TestQtProcessingEvents::eventsForADestroyedTargetAreDropped()
{
    // The header says Qt discards events still queued for a destroyed target, which is
    // what lets the coordinator and extractor be destroyed with events in flight. That
    // is a claim about Qt, so it is checked rather than assumed.
    auto* target = new QObject;
    int delivered = 0;
    QtProcessingEvents events(
        target,
        [&](const QString&) { ++delivered; },
        [&](const QString&) { ++delivered; },
        [&](bool) { ++delivered; });

    events.logMessage("pending");
    events.errorOccurred("pending");
    events.processingFinished(true);
    delete target;
    QCoreApplication::processEvents();

    QCOMPARE(delivered, 0);
}
