/**
 * @file tst_frameprocessor.h
 * @brief Unit tests for FrameProcessor — static helpers and in-memory accumulation.
 */

#ifndef TST_FRAMEPROCESSOR_H
#define TST_FRAMEPROCESSOR_H

#include <QObject>

class TestFrameProcessor : public QObject
{
    Q_OBJECT

private slots:
    void constructorDefaults();
    void requestAbortSetsFlag();
    void derandomizeShortBufferIdentity();
    void derandomizeLongerBufferChanges();
    void processInvalidTimeChannel();
    void processInvalidPcmChannel();
    void processInvalidFile();
    void processAccumulatesReceiverData();
    void processLockOnlyModeHasNoChannels();
    void processFrameSyncErrorsMonotonic();
    void processSlopeAffectsValues();
    void processShortPeriodMoreSamples();
    void calibrationRoundTripOnRealFileProducesCleanSteps();
    void offPhaseSyncAfterLockLossIsNotExtracted();
    void nonLinearCalibrationAveragesRawBeforeInterpolating();
    void swapBytesOffFindsSyncThatTheSwapDestroys();
};

#endif // TST_FRAMEPROCESSOR_H
