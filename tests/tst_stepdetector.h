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
    void parseStepConfigMatchesAreCaseInsensitive();
    void parseStepConfigHeaderMustBeExactlyStepToCount();
    void parseStepConfigIgnoresKeysOutsideAStepAndOtherSections();
    void parseStepConfigValueRules_data();
    void parseStepConfigValueRules();
    void parseStepConfigKeepsFileOrderAndLastDbWins();
    void parseStepConfigClearsItsOutputFirst();
    void parseStepConfigReadsBomCrlfAndUtf16_data();
    void parseStepConfigReadsBomCrlfAndUtf16();
    void parseStepConfigOpensNonAsciiPaths();
    void parseStepConfigErrorMessagesNameTheFile();
    void parseStepConfigAgreesWithTheOriginalHandWrittenParserOnGeneratedFiles();

    // Plateau detection
    void detectCleanSteps();
    void detectOnATooShortSeriesFailsCleanly();
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
    void detectOutcomeNamesTheGateThatRejected();
    void detectCompressedOutOfToleranceReceiverStillCalibrates();
    void detectNoiseSplitPlateausDoNotSeverTheSweep();
    void detectFlatChannelReportsNoSignalNotMergedSteps();
    void detectPinnedSamplesDoNotShrinkTheNoiseEstimate();
    void detectSplitDwellFarAboveNoiseIsStillOneStep();
    void detectRailedTopStepsCalibrateWhatStayedInRange();
    void detectUnconfirmedDwellIsMeasuredFromTheGrid();
    void detectReportsHowManyLevelsTheSweepHeld();
    void initialStepConfigPrefersTheStreamsFileThenTheDefault();
    void initialStepConfigPathIgnoresADirectory();

    // Interpolation / out-of-range clamping
    void interpolateMidpoint();
    void interpolateBelowFirstClamps();
    void interpolateAboveLastClamps();
    void interpolateCoincidentRawNoCrash();
};

#endif // TST_STEPDETECTOR_H
