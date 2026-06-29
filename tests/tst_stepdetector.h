#ifndef TST_STEPDETECTOR_H
#define TST_STEPDETECTOR_H

#include <QObject>

class TestStepDetector : public QObject
{
    Q_OBJECT

private slots:
    // Step-config TOML parsing
    void parseStepConfigValid();
    void parseStepConfigEmptyFails();

    // Plateau detection
    void detectCleanSteps();
    void detectTooFewPlateausFails();
    void detectExtraPlateausUsesLastOfMonotonicRun();
    void detectShortBlipDoesNotStealPairingSlot();
    void detectLongLeadingTransientDoesNotShiftPairing();
    void detectInvertedPolaritySweepNotReversed();
    void detectNonMonotonicPairingRejected();
    void detectNoisyStepsStillDetected();
    void detectSettlingAtPlateauStartExcluded();
    void roundTripSameDataIsExact();
    void detectMultiChannelSweepStaysTimeAligned();

    // Interpolation / out-of-range clamping
    void interpolateMidpoint();
    void interpolateBelowFirstClamps();
    void interpolateAboveLastClamps();
    void interpolateCoincidentRawNoCrash();
};

#endif // TST_STEPDETECTOR_H
