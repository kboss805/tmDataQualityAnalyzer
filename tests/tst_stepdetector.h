#ifndef TST_STEPDETECTOR_H
#define TST_STEPDETECTOR_H

#include <QObject>

class TestStepDetector : public QObject
{
    Q_OBJECT

private slots:
    // Step-config TOML parsing
    void parseStepConfigValid();
    void parseStepConfigMissingDwellFails();
    void parseStepConfigEmptyFails();

    // Plateau detection
    void detectCleanSteps();
    void detectTooFewPlateausFails();
    void detectExtraPlateausUsesFirstN();
    void detectNoisyStepsStillDetected();
    void detectEdgeTrimExcludesTransition();

    // Interpolation / extrapolation
    void interpolateMidpoint();
    void interpolateBelowFirstExtrapolates();
    void interpolateAboveLastExtrapolates();
    void interpolateCoincidentRawNoCrash();
};

#endif // TST_STEPDETECTOR_H
