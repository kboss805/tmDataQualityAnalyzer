#include "tst_processingcoordinator.h"

#include <QSignalSpy>
#include <QVector>
#include <QtTest>

#include "framesetup.h"
#include "processingcoordinator.h"
#include "processingparams.h"

void TestProcessingCoordinator::constructorDefaults()
{
    ProcessingCoordinator coord;
    QCOMPARE(coord.processing(), false);
    QCOMPARE(coord.progressPercent(), 0);
}

void TestProcessingCoordinator::resetClearsState()
{
    ProcessingCoordinator coord;
    coord.reset();
    QCOMPARE(coord.processing(), false);
    QCOMPARE(coord.progressPercent(), 0);
}

void TestProcessingCoordinator::cancelProcessingNoRunNoOp()
{
    ProcessingCoordinator coord;
    coord.cancelProcessing();  // must not crash with no active run
    QCOMPARE(coord.processing(), false);
}

void TestProcessingCoordinator::startProcessingEmptyReturnsFalse()
{
    ProcessingCoordinator coord;
    QSignalSpy error_spy(&coord, &ProcessingCoordinator::errorOccurred);

    bool started = coord.startProcessing({});  // no jobs

    QCOMPARE(started, false);
    QVERIFY(!error_spy.isEmpty());
    QCOMPARE(coord.processing(), false);
}

void TestProcessingCoordinator::startProcessingEmitsProcessingState()
{
    ProcessingCoordinator coord;
    QSignalSpy state_spy(&coord, &ProcessingCoordinator::processingStateChanged);

    // A lock-only job pointing at a nonexistent file: the worker launches, then
    // fails quickly on file open. The state signal fires synchronously before launch.
    StreamJob job;
    job.params.filename = "nonexistent_coordinator_test.ch10";
    job.params.time_channel_id = 1;
    job.params.pcm_channel_id = 1;
    job.params.mode = StreamMode::FrameSyncLockStats;
    job.frameSetup = new FrameSetup(nullptr);  // ownership transferred to coordinator

    QVector<StreamJob> jobs;
    jobs.push_back(job);

    bool started = coord.startProcessing(std::move(jobs));

    QCOMPARE(started, true);
    QVERIFY(!state_spy.isEmpty());
    QCOMPARE(state_spy.first().first().toBool(), true);

    // Let the worker fail and the queue drain.
    coord.cancelProcessing();
    QTest::qWait(300);
}
