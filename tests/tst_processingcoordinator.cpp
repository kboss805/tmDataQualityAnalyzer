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
    QSignalSpy error_spy(&coord, &ProcessingCoordinator::errorOccurred);

    // With the single-reader architecture, Ch10PacketReader::prepare() runs
    // synchronously before any threads launch. A nonexistent file causes prepare()
    // to fail immediately, so startProcessing() returns false and emits errorOccurred
    // without ever flipping processingState to true.
    StreamJob job;
    job.params.filename = "nonexistent_coordinator_test.ch10";
    job.params.time_channel_id = 1;
    job.params.pcm_channel_id = 1;
    job.params.mode = StreamMode::FrameSyncLockStats;
    job.frameSetup = new FrameSetup(nullptr);

    QVector<StreamJob> jobs;
    jobs.push_back(std::move(job));

    bool started = coord.startProcessing(std::move(jobs));

    QCOMPARE(started, false);
    QVERIFY(!error_spy.isEmpty());
    QVERIFY(state_spy.isEmpty());  // no spurious "started" event
    QCOMPARE(coord.processing(), false);
}
