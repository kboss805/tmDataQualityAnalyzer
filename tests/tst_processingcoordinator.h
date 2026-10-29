#ifndef TST_PROCESSINGCOORDINATOR_H
#define TST_PROCESSINGCOORDINATOR_H

#include <QObject>

class TestProcessingCoordinator : public QObject
{
    Q_OBJECT

private slots:
    void constructorDefaults();
    void aRecordingUnderANonAsciiFolderProcessesEndToEnd();
    void workerReportsPrecedeTheResultAndFinishComesLast();
    void resetClearsState();
    void cancelProcessingNoRunNoOp();
    void cancelProcessingEndsTheRunWithoutAResult();
    void destroyingTheCoordinatorMidRunIsSafe();
    void startProcessingEmptyReturnsFalse();
    void startProcessingEmitsProcessingState();
    void benchmarkSingleVsMultiStreamThroughput();
};

#endif // TST_PROCESSINGCOORDINATOR_H
