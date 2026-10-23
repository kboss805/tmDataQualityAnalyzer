/**
 * @file tst_abi.h
 * @brief The C ABI of tmdq_core.dll (native/tmdq_core), exercised in-process.
 *
 * The ABI sources are compiled straight into the test executable (TMDQ_ABI_STATIC), so these tests call
 * `tmdq_*` exactly as a host would, from C++ rather than through P/Invoke. What they pin is the contract in
 * `tmdq_abi.h`: the layout of every struct, what a run reports and in what order, what a bad argument
 * returns, who owns what and when, and that a run through the ABI is the run the core itself makes.
 * (The managed side repeats the layout and numeric checks from C# in plan step 2.3.)
 */

#ifndef TST_ABI_H
#define TST_ABI_H

#include <QObject>

class TestAbi : public QObject
{
    Q_OBJECT

private slots:
    // --- the contract's shape
    void theStructLayoutsArePinned();
    void theVersionAndRevisionAreReported();
    void aStructFromANewerHostIsAcceptedAndOneFromAnOlderHostIsRefused();

    // --- the file
    void aFileListsItsChannelsLikeTheReader();
    void aFileThatCannotBeOpenedSaysWhy();
    void badArgumentsToTheFileFunctionsAreRefused();

    // --- runs
    void aRunOfSeveralStreamsMatchesWhatTheCoreMakes();
    void aRunReportsInTheDocumentedOrder();
    void aNonLinearProfileReachesTheJob();
    void settingsTheCoreRejectsAreRefusedWithTheCoresMessage();
    void aRecordingThatCannotBeUsedFailsToPrepare();
    void badArgumentsToTheRunFunctionsAreRefused();
    void cancellingEndsARunUnsuccessfully();
    void freeingARunInFlightCancelsAndWaits();
    void aResultOutlivesItsRunAndIsFreedFromAnyThread();
    void aCallbackThatThrowsDoesNotBreakTheRun();
    void twoRunsAtOnceAgreeWithOneAlone();
    void aSuccessfulCallClearsTheLastError();
    void theExceptionGuardTurnsAThrowIntoAStatus();
    void theByteOrderOfTheFileReachesTheJob();
    void aReceiverParametersFileAndTheAppRootReachTheJob();
    void theFirstProfileOfAWordWins();
    void waitAloneIsEnoughToSeeTheEnd();
    void optionalTextMayBeLeftNull();
    void theLinesBuildingJobsProducedReachLogFirst();
    void everySettingOfAStreamReachesTheJob();
    void readingAHostStructNeverStraysBeyondTheKnownOne();
};

#endif // TST_ABI_H
