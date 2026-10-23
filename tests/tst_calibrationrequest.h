/**
 * @file tst_calibrationrequest.h
 * @brief Building a calibration request from what the dialog knows (plan step 2.2b).
 *
 * The Extract Calibration dialog decoded the stream's sync text and resolved the default word map inline.
 * That moved into the Qt-free core (`buildCalibrationRequest`). The old inline code is kept in the test
 * as a small function, verbatim apart from where its inputs come from, and run beside the new one over
 * generated inputs - so the move is shown not to have changed what the dialog asks the extractor to do.
 */

#ifndef TST_CALIBRATION_REQUEST_H
#define TST_CALIBRATION_REQUEST_H

#include <QObject>

class TestCalibrationRequest : public QObject
{
    Q_OBJECT

private slots:
    void theSyncTextIsDecodedLikeTheMainRun();
    void aSyncPatternOrMaskThatCannotBeReadIsRefusedWithTheDialogsMessage();
    void aFailedBuildLeavesTheRequestAlone();
    void everyOtherFieldIsCarriedAcross();
    void theDefaultWordMapIsUsedOnlyWhenNoneIsNamedAndTheAppRootHasOne();
    void theNewCodeAgreesWithTheOldInlineCodeOverGeneratedInputs();
};

#endif // TST_CALIBRATION_REQUEST_H
