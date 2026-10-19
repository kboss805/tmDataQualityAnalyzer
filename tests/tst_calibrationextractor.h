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
    void aNonAsciiChannelNameSurvivesExtractionItsLogAndItsReport();
    void extractionLogsOneLinePerChannelWithData();
    void realStepCalWithShortStepFileIsFlagged();
    void realStepCalWithWrongWordMapIsFlagged();
    void extractionReportsItsProgress();
    void clipsBeyondTheRecordingAreReported();
    void summarizeProducesExactlyThisReport();
    void summarizeProducesExactlyThisReport_data();
    void summaryNamesReceiversAndWhyTheyFellBack();
};

#endif // TST_CALIBRATIONEXTRACTOR_H
