/**
 * @file tst_framesync.h
 * @brief Tests for the shared frame-sync decode (framesync.h) and the hex parser under it.
 *
 * Qt is the oracle for the parser: the main run decoded hex with `QString::toULongLong(16)` for
 * years, and the calibration extraction must keep decoding it the same way now that both use
 * one Qt-free function.
 */

#ifndef TST_FRAMESYNC_H
#define TST_FRAMESYNC_H

#include <QObject>

class TestFrameSync : public QObject
{
    Q_OBJECT

private slots:
    void parseUnsignedHexAgreesWithQtOnAwkwardInputs();
    void parseUnsignedHexAgreesWithQtOnArbitraryText();
    void fullSyncMaskIsEveryBitOfThePatternAndNeverShiftsBySixtyFour();
    void resolveFrameSyncDecodesPatternMaskAndLength();
    void resolveFrameSyncReportsTheFirstProblemAndLeavesTheResultAlone();
    void applyAcquisitionSetsEveryRawAffectingField();
    void applyAcquisitionLeavesEverythingElseAlone();
};

#endif // TST_FRAMESYNC_H
