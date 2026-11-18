/**
 * @file stepdetector.cpp
 * @brief Implementation of StepDetector — edge-based plateau detection for non-linear calibration (US5.3).
 */

#include "stepdetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "constants.h"
#include "containerutil.h"
#include "stepplateaus.h"
#include "stepsaturated.h"

using namespace stepplateaus;

namespace {

/// Selects one clean calibration sweep: the first maximal run of
/// CONSECUTIVE plateaus, at least `expected` long, whose settled averages move
/// strictly in one direction (a real receiver response is monotonic —
/// increasing or decreasing dB with raw count), and returns where that run's
/// LAST `expected` plateaus start. A genuine step transition was already
/// large enough to split the plateaus (> `threshold`), so we require each
/// consecutive level change to exceed that same threshold and keep the
/// same sign across the whole run.
/// 
/// This is robust to the messy reality of recorded cal files (US5.3): it
/// skips leading pre-roll/no-signal plateaus, ignores trailing post-roll,
/// and takes the FIRST of several back-to-back repeats. Crucially it also
/// rejects flat/inactive channels — their plateau averages only jitter
/// around the noise floor, never forming a monotonic run — instead of the
/// old "first N plateaus" rule, which paired no-signal noise with the step
/// dB values and produced degenerate (all-equal rawAvg) profiles.
int selectSweepStart(const std::vector<double>& plateauAvg, int expected,
                     double threshold, int& runLevels)
{
    runLevels = 0;
    // Find the FIRST maximal run of plateaus whose settled averages move strictly
    // in one direction, long enough to hold the whole sweep, and take its LAST
    // `expected` plateaus.
    //
    // A real cal recording is, in time order:
    //   [signal-generator turn-on transient] [the sweep: step 0 .. step N-1]
    //   [optional operator return ramp back down]
    // The sweep climbs monotonically to its peak step and then reverses (the ramp)
    // or the recording ends. The turn-on transient sits on the low-signal side
    // just ahead of the sweep and moves in the SAME direction into step 0, so it
    // extends the first monotonic run by one (or more) plateau at its START. Taking
    // the run's LAST `expected` plateaus drops that leading transient while keeping
    // the genuine sweep, and pairs them with the steps in time order below.
    //
    // This is direction-agnostic, which matters because receiver polarity sets the
    // raw-count direction: a normal receiver's raw count rises with signal, an
    // inverted / negative-slope receiver's falls. Using the run's own direction
    // (rather than anchoring on a max/min raw extreme) avoids locking onto the
    // return ramp, which on a full pyramid/valley recording forms an equally valid
    // monotonic run in the opposite half.
    int run_start = -1;
    forEachMonotonicRun(plateauAvg, threshold, [&](const Run& run) {
        // The maximal monotonic run is plateaus[run.begin..run.end].
        if (run.end - run.begin + 1 < expected)
        {
            return RunWalk::Continue; // too short; the reversal starts the next run here
        }
        run_start = run.end - (expected - 1); // keep the last `expected` of the run
        runLevels = run.end - run.begin + 1;
        return RunWalk::Stop;
    });
    return run_start;
}

} // namespace

// ---------------------------------------------------------------------------
// detect
// ---------------------------------------------------------------------------

StepDetector::Result StepDetector::detect(const std::vector<double>& rawValues,
                                          double samplePeriodSec,
                                          const std::vector<StepDefinition>& steps)
{
    Result result;

    const int expected = intSize(steps);
    if (expected == 0 || intSize(rawValues) < 2 || samplePeriodSec <= 0.0)
    {
        return result; // invalid by default
    }

    const std::vector<double> deriv = sampleDerivative(rawValues);

    // The edge threshold comes from the robust spread of the derivative: samples
    // inside a plateau dominate, so their noise sets the baseline and real
    // transitions are large outliers above it.
    const double sigma = liveNoiseSigma(rawValues, deriv);
    const double nominal_threshold = std::max(
        CalibrationConstants::kEdgeSigmaMultiple * sigma,
        CalibrationConstants::kMinEdgeRawCounts);
    result.edgeThreshold = nominal_threshold;

    // The confirmation window is a fixed duration (not a fraction of a configured
    // dwell, since steps no longer carry one) with an absolute sample-count floor
    // for coarse sample periods where the duration alone would be one or two
    // samples.
    const int confirm_samples = std::max(
        CalibrationConstants::kMinConfirmSamples,
        static_cast<int>(std::ceil(CalibrationConstants::kStepConfirmSeconds / samplePeriodSec)));

    auto plateausAt = [&](double threshold) {
        return confirmedPlateaus(markStableSamples(deriv, intSize(rawValues), threshold),
                                 intSize(rawValues), confirm_samples);
    };

    // Search the edge threshold, using the expected step count as the target
    // rather than only as a pass/fail test.
    //
    // A receiver far out of tolerance does not move a uniform number of raw
    // counts per dB: one sweep can hold a 12-count step at the compressed end
    // and a 1100-count step at the expansive end. A single threshold sized by
    // the whole series' noise cannot resolve both — the small steps never
    // clear it, so adjacent levels merge and the channel loses its profile
    // even though the sweep is plainly there.
    //
    // So try progressively lower thresholds and accept the FIRST (largest,
    // most conservative) one that yields a clean sweep of `expected` levels.
    // Relaxing is safe because the two real noise discriminators are unchanged
    // and neither depends on the threshold: a candidate level must still hold
    // steady for the confirmation window, and the run must still move strictly
    // in one direction. A flat/no-signal channel's levels only jitter, so they
    // never form such a run no matter how far the threshold drops — which is
    // what stops this from resurrecting the degenerate "noise paired with step
    // dB values" profiles the monotonic-run rule was introduced to kill.
    const double floor_threshold = std::max(
        CalibrationConstants::kMinEdgeSigmaMultiple * sigma,
        CalibrationConstants::kMinEdgeRawCounts);

    std::vector<Plateau> plateaus;
    std::vector<double> plateauAvg;
    int selected_run_levels = 0; ///< Length of the run selectSweepStart accepted.
    std::vector<Level>  nominal_levels; ///< Levels the nominal pass saw, for the saturated-top fallback.
    double accepted_threshold = nominal_threshold;
    int run_start = -1;
    bool reached_sweep_stage = false;
    int best_levels = 0; ///< Most distinct levels any attempt resolved.

    for (int attempt = 0; attempt < CalibrationConstants::kMaxEdgeRelaxAttempts; attempt++)
    {
        const double threshold =
            nominal_threshold * std::pow(CalibrationConstants::kEdgeRelaxFactor, attempt);
        if (attempt > 0 && threshold < floor_threshold)
        {
            break; // below the noise floor every sample reads as an edge
        }

        const std::vector<Plateau> candidates = plateausAt(threshold);
        if (attempt == 0)
        {
            // Report the nominal pass's count, so the diagnostic describes the
            // channel as detection first saw it rather than as the last retry did.
            result.detectedPlateaus = intSize(candidates);
        }

        // Coalesce adjacent plateaus that sit at the SAME physical level. Noise
        // routinely splits one dwell into two stable runs whose settled averages
        // differ by a count or two — on a real recording, levels like
        //   ... 34752, 49892, 49895, 57310, 57309, 65472 ...
        // are six plateaus but only four steps. Left alone, that ~1-count wobble
        // reads as "not a real transition" and severs the monotonic run in the
        // middle of an otherwise perfect sweep, so a plainly good calibration is
        // discarded. Merging on the same threshold that defines a real edge keeps
        // genuine steps apart while healing the splits.
        //
        // It also sharpens the dead-channel rejection rather than weakening it: a
        // flat channel's plateaus all collapse into ONE level, which can never
        // form a run of `expected`.
        std::vector<Level> levels =
            coalesceLevels(levelsOf(rawValues, candidates, confirm_samples), threshold);

        // ...and again against the STEP size, not only the noise. Noise grows with
        // signal on a real receiver (~3 counts on its low steps, ~60 on its high
        // ones), so a split high dwell can sit further apart than any threshold the
        // quiet steps support: the step_cal recording's L_RCVR3 split its 42 dB dwell
        // into 22797 / 22818 - 22 counts apart against a 17-count threshold, on
        // steps ~3800 counts tall. Two levels a small fraction of a typical step
        // apart are one step, whatever the noise estimate says.
        levels = coalesceLevels(levels,
                          std::max(threshold, CalibrationConstants::kSameLevelFractionOfStep
                                                  * medianAdjacentGap(valuesOf(levels))));
        if (attempt == 0)
        {
            // Recorded before the count check below, so a channel that bails out
            // early still reports what it saw — that is exactly the case where
            // the levels are the only thing that explains the verdict (one level
            // at 0 is a dead input; one level at full scale is a railed one).
            result.plateauLevels = valuesOf(levels);
            nominal_levels       = levels;
        }
        best_levels = std::max(best_levels, static_cast<int>(intSize(levels)));
        if (intSize(levels) < expected)
        {
            continue; // steps still merged; loosen and look again
        }
        reached_sweep_stage = true;

        const std::vector<double> averages = valuesOf(levels);
        const int start =
            selectSweepStart(averages, expected, threshold, selected_run_levels);
        if (start >= 0)
        {
            plateaus           = candidates;
            plateauAvg         = averages;
            accepted_threshold = threshold;
            run_start          = start;
            break;
        }
    }

    result.edgeThreshold    = accepted_threshold;
    result.thresholdRelaxed = (run_start >= 0) && (accepted_threshold < nominal_threshold);

    if (run_start < 0)
    {
        if (stepsaturated::calibrateSaturatedTop(rawValues, deriv, nominal_levels, steps,
                                              nominal_threshold, confirm_samples, result))
        {
            return result; // partial profile over the steps that stayed in range
        }
        // Report the furthest gate reached. A channel that never resolved more
        // than one level did not "merge its steps" — it never moved at all, so
        // the sweep is absent from the recording rather than beyond the
        // detector's reach, and the two must not read the same.
        result.outcome = (best_levels <= 1)      ? CalibrationOutcome::FlatNoSignal
                         : reached_sweep_stage   ? CalibrationOutcome::NoMonotonicSweep
                                                 : CalibrationOutcome::TooFewPlateaus;
        return result; // linear fallback
    }
    result.extraPlateaus = intSize(plateaus) > expected;
    result.sweepLevels   = selected_run_levels;

    // Pair the selected sweep with the steps in time order (Nth plateau of the
    // sweep <-> Nth step), then sort by raw so interpolateCalibration() can
    // assume ascending rawAvg.
    CalibrationProfile& profile = result.profile;
    profile.points.reserve(expected);
    for (int k = 0; k < expected; k++)
    {
        CalibrationPoint pt;
        pt.rawAvg = plateauAvg[run_start + k];
        pt.trueDb = steps[k].db;
        profile.points.push_back(pt);
    }
    sortByRawAvg(profile.points);

    profile.valid  = true;
    result.outcome = CalibrationOutcome::Calibrated;
    return result;
}

