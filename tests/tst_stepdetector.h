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
    void detectExtraPlateausUsesFirstN();
    void detectShortBlipDoesNotStealPairingSlot();
    void detectNonMonotonicPairingRejected();
    void detectNoisyStepsStillDetected();
    void detectSettlingAtPlateauStartExcluded();
    void roundTripSameDataIsExact();

    // Interpolation / extrapolation
    void interpolateMidpoint();
    void interpolateBelowFirstExtrapolates();
    void interpolateAboveLastExtrapolates();
    void interpolateCoincidentRawNoCrash();
};

#endif // TST_STEPDETECTOR_H
