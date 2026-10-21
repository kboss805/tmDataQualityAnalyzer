/**
 * @file tst_pipelinerunner.h
 * @brief Tests for PipelineRunner, the Qt-free single-reader / parallel-worker pipeline.
 *
 * The runner was extracted from ProcessingCoordinator, whose own suite still pins what the
 * application sees. These cases pin the runner directly, with a plain recording sink and no event
 * loop: what it delivers, in what order, from where it can be stopped, and what it leaves behind.
 */

#ifndef TST_PIPELINERUNNER_H
#define TST_PIPELINERUNNER_H

#include <QObject>

class TestPipelineRunner : public QObject
{
    Q_OBJECT

private slots:
    void aSingleStreamDeliversItsResultThenTheEnd();
    void severalStreamsOnOneChannelEachDeliverTheirOwnResult();
    void aStreamThatFindsNoSyncDoesNotStopTheOthers();
    void aRunInWhichEveryStreamFailsEndsUnsuccessfully();
    void aRecordingThatCannotBeOpenedFailsBeforeAnythingRuns();
    void noJobsIsRefused();
    void aSecondStartWhileRunningIsRefused();
    void cancelEndsTheRunUnsuccessfullyWithNoResult();
    void cancelAfterOneStreamDeliveredDropsTheRestAndFailsTheRun();
    void cancelWakesAWorkerThatIsWaitingForPackets();
    void cancelStopsAWorkerThatHasNotStartedReading();
    void theRunnerIsNotRunningByTheTimeItReportsTheEnd();
    void aStreamThatEndsEarlyDoesNotLeaveTheReaderBlockedOnItsQueue();
    void theRunnerRunsAgainAfterARunEnds();
    void destroyingARunningRunnerStopsItAndReportsTheEnd();
};

#endif // TST_PIPELINERUNNER_H
