/**
 * @file stepdetector.h
 * @brief Detects calibration step plateaus in a raw-count series and builds a
 *        per-channel non-linear CalibrationProfile (US5.3).
 *
 * Pure logic with no Qt UI dependencies, so it is unit-testable in isolation.
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

#include <QString>
#include <QVector>

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
        QVector<double> plateauLevels;
        /// True when the sweep ran off an end of the converter and the railed
        /// dwells were dropped: the profile covers the steps below them, and
        /// interpolateCalibration() clamps everything above to the top step that
        /// was still measurable. The operator needs to know - the channel IS
        /// calibrated, but only over part of its sweep, and the usual cause is a
        /// receiver gain set too high for the levels being injected.
        bool saturated = false;
        /// dB of every step no measured level could be paired with: the railed
        /// ones, plus any dwell noise shattered into runs too short to confirm.
        QVector<double> unresolvedDb;
        /// Levels in the monotonic run the sweep was taken from. More than the step
        /// file's count means leading levels were set aside as lead-in; more than one
        /// extra (CalibrationConstants::kMaxLeadInLevels) suggests the step file is
        /// short rather than that the recording has a turn-on transient.
        int sweepLevels = 0;
    };

    /**
     * @brief Parses an `[[Step]]` array-of-tables step-config TOML file.
     *
     * The custom QSettings TOML format does not support arrays, so this is a
     * dedicated line parser. Each `[[Step]]` table must provide `db`.
     *
     * @param[in]  path  Path to the step-config TOML file.
     * @param[out] out   Parsed steps, in file order.
     * @param[out] error Human-readable error message on failure.
     * @return true if at least one valid step was parsed.
     */
    static bool parseStepConfig(const QString& path,
                                QVector<StepDefinition>& out,
                                QString& error);

    /**
     * @brief The step-config TOML the Extract Calibration dialog opens with.
     *
     * Almost every calibration uses the same step file, so the dialog preloads one
     * rather than making the operator browse to it each time. It prefers the file
     * this stream last used (an earlier extraction, or a processing template's
     * stored reference) and otherwise falls back to the shipped default.
     *
     * @param[in] appRoot   Application root holding the settings/ tree.
     * @param[in] preferred The stream's previous step file; may be empty.
     * @return @p preferred if it names an existing file; else
     *         `<appRoot>/settings/rcvr_cals/default.toml` if that exists; else empty.
     */
    static QString initialStepConfigPath(const QString& appRoot, const QString& preferred);

    /**
     * @brief Detects confirmed step plateaus in @p rawValues and builds a
     *        CalibrationProfile.
     *
     * A candidate plateau (a stable run between two detected edges) is only
     * confirmed as a real step once it holds for at least
     * CalibrationConstants::kStepConfirmSeconds (and kMinConfirmSamples,
     * whichever is larger); shorter runs are discarded as transition/partial-
     * jump noise rather than counted as a step. The Nth confirmed plateau
     * (time order) is paired with @p steps[N]. The channel is valid only if at
     * least @p steps.size() plateaus are confirmed; if more are found (e.g. a
     * technician ran the step sequence two or three times back to back), only
     * the first steps.size() are used and the rest are discarded.
     *
     * @param[in] rawValues       Per-sample raw counts for one channel.
     * @param[in] samplePeriodSec Seconds between consecutive samples.
     * @param[in] steps           Expected steps from the step-config TOML.
     * @return Detection result; Result::profile.valid is false on failure.
     */
    static Result detect(const QVector<double>& rawValues,
                         double samplePeriodSec,
                         const QVector<StepDefinition>& steps);

private:
    /// @brief A contiguous run of stable (non-transition) samples [begin, end).
    struct Plateau { int begin; int end; };

    /// Robust (MAD-based) standard-deviation estimate of @p values.
    static double robustStdDev(const QVector<double>& values);
};

#endif // STEPDETECTOR_H
