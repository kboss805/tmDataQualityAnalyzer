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
    void realStepCalWithShortStepFileIsFlagged();
    void realStepCalWithWrongWordMapIsFlagged();
    void clipsBeyondTheRecordingAreReported();
    void summaryNamesReceiversAndWhyTheyFellBack();
};

#endif // TST_CALIBRATIONEXTRACTOR_H
