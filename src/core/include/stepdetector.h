/**
 * @file stepdetector.h
 * @brief Detects calibration step plateaus in a raw-count series and builds a
 *        per-channel non-linear CalibrationProfile (US5.3).
 *
 * Pure logic with no UI dependencies, so it is unit-testable in isolation.
 * Detection uses derivative/edge detection: large sample-to-sample changes mark
 * step transitions, and the stable spans between them are candidate plateaus.
 * A candidate is only confirmed as a real step once it holds steady for at
 * least CalibrationConstants::kStepConfirmSeconds (so a partial/transient jump
 * can't be mistaken for a settled level); the calibration average is then
 * taken from the confirmation window immediately preceding the next
 * transition (the most-settled part of the plateau). Confirmed plateaus are
 * paired in time order with the expected steps from the step-config TOML.
 */

#ifndef STEPDETECTOR_H
#define STEPDETECTOR_H

#include <vector>

#include "calibrationprofile.h"

/// @brief Step-plateau detection and step-config TOML parsing.
class StepDetector
{
public:
    /// @brief Outcome of detecting steps on a single channel's raw series.
    struct Result
    {
        CalibrationProfile profile;           ///< Built profile (valid only if enough plateaus found).
        int  detectedPlateaus = 0;            ///< Number of plateaus detected.
        bool extraPlateaus    = false;        ///< True if more plateaus than expected steps were found.
        /// Which gate decided the outcome. `detectedPlateaus` alone cannot say: a
        /// channel with exactly the expected count still fails if those levels
        /// never form a monotonic sweep, and the two cases point at different
        /// causes (see CalibrationOutcome).
        CalibrationOutcome outcome = CalibrationOutcome::NoData;
        /// Raw-count edge threshold used to split plateaus. Reported because it is
        /// the quantity a too-small step is measured against, so a channel that
        /// merged its steps can be read directly against the threshold that merged
        /// them rather than inferred.
        double edgeThreshold = 0.0;
        /// True when the nominal threshold could not resolve the sweep and a lower
        /// one was needed. Worth surfacing rather than hiding: a channel that only
        /// calibrates after relaxation has steps close to its own noise, which is
        /// itself the signature of a receiver drifting out of tolerance.
        bool thresholdRelaxed = false;
        /// Settled averages of the plateaus detection actually found, in time
        /// order. The raw material behind every other field: a channel that
        /// "found 12 plateaus but no sweep" can only be understood by seeing
        /// whether those 12 levels are 8 real steps plus 4 noise splits, or a
        /// genuine reversal.
        std::vector<double> plateauLevels;
        /// True when the sweep ran off an end of the converter and the railed
        /// dwells were dropped: the profile covers the steps below them, and
        /// interpolateCalibration() clamps everything above to the top step that
        /// was still measurable. The operator needs to know - the channel IS
        /// calibrated, but only over part of its sweep, and the usual cause is a
        /// receiver gain set too high for the levels being injected.
        bool saturated = false;
        /// dB of every step no measured level could be paired with: the railed
        /// ones, plus any dwell noise shattered into runs too short to confirm.
        std::vector<double> unresolvedDb;
        /// Levels in the monotonic run the sweep was taken from. More than the step
        /// file's count means leading levels were set aside as lead-in; more than one
        /// extra (CalibrationConstants::kMaxLeadInLevels) suggests the step file is
        /// short rather than that the recording has a turn-on transient.
        int sweepLevels = 0;
    };

    /**
     * @brief Detects confirmed step plateaus in @p rawValues and builds a
     *        CalibrationProfile.
     *
     * A candidate plateau (a stable run between two detected edges) is only
     * confirmed as a real step once it holds for at least
     * CalibrationConstants::kStepConfirmSeconds (and kMinConfirmSamples,
     * whichever is larger); shorter runs are discarded as transition/partial-
     * jump noise rather than counted as a step. Plateaus whose averages are
     * within the edge threshold (or a small fraction of a step) of each other
     * are merged into one level. The sweep is the first maximal run of levels
     * that move strictly in one direction and is at least @p steps.size() long;
     * its LAST steps.size() levels are used, which drops a signal-generator
     * turn-on transient ahead of step 0 (it extends the run at its start) and
     * ignores a technician's back-to-back repeats and the return ramp after the
     * sweep. The Nth level of the sweep (time order) is paired with
     * @p steps[N]. The channel is valid only if such a run exists, or, when the
     * top of the sweep ran onto the converter's rail, as a partial profile over
     * the steps that stayed in range.
     *
     * @param[in] rawValues       Per-sample raw counts for one channel.
     * @param[in] samplePeriodSec Seconds between consecutive samples.
     * @param[in] steps           Expected steps from the step-config TOML.
     * @return Detection result; Result::profile.valid is false on failure.
     */
    static Result detect(const std::vector<double>& rawValues,
                         double samplePeriodSec,
                         const std::vector<StepDefinition>& steps);
};

#endif // STEPDETECTOR_H
