#ifndef TST_CALIBRATIONEXTRACTOR_H
#define TST_CALIBRATIONEXTRACTOR_H

#include <QObject>

class TestCalibrationExtractor : public QObject
{
    Q_OBJECT

private slots:
    void missingFileFailsCleanly();
    void realFileCalibratesRcvr3Only();
    void byteOrderReachesTheExtraction();
    void realStepCalRecordingPairsEveryStepCorrectly();
    void summaryFlagsAShortStepFileAndAMismatchedWordMap();
    void extractionLogsOneLinePerChannelWithData();
    void aNonAsciiChannelNameSurvivesExtractionItsLogAndItsReport();
    // Lifecycle and early failures: what is reported, in what order, and when nothing starts.
    void anExtractionWithNoStepsFailsBeforeItStarts();
    void aWordMapThatCannotBeUsedIsReportedBeforeAnythingRuns();
    void aWordMapThatCannotBeUsedIsReportedBeforeAnythingRuns_data();
    void aDefaultWordMapThatDoesNotFitTheFrameIsReported();
    void aRecordingThatCannotBeOpenedIsReported();
    void aCancelledExtractionSaysSoAndLeavesNoResults();
    void startingWhileRunningIsIgnoredAndAnExtractorCanRunAgain();
    void aRunBeginsWithItsSamplePeriodAndSummarisesWhatItCalibrated();
    void realStepCalWithShortStepFileIsFlagged();
    void realStepCalWithWrongWordMapIsFlagged();
    void extractionReportsItsProgress();
    void clipsBeyondTheRecordingAreReported();
    void summarizeProducesExactlyThisReport();
    void summarizeProducesExactlyThisReport_data();
    void summaryNamesReceiversAndWhyTheyFellBack();
};

#endif // TST_CALIBRATIONEXTRACTOR_H
