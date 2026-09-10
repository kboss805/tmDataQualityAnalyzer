/**
 * @file stepdetector.cpp
 * @brief Implementation of StepDetector — edge-based plateau detection and
 *        step-config TOML parsing for non-linear calibration (US5.3).
 */

#include "stepdetector.h"

#include <algorithm>
#include <cmath>

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "constants.h"

namespace {

/// Strips an inline `#` comment (outside quotes) and surrounding whitespace.
QString stripComment(QString value)
{
    const int hash = value.indexOf('#');
    if (hash >= 0)
    {
        value = value.left(hash);
    }
    return value.trimmed();
}

/// Median of a copy of @p values (returns 0 for an empty input).
double median(QVector<double> values)
{
    if (values.isEmpty())
    {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int mid = values.size() / 2;
    if (values.size() % 2 == 0)
    {
        return 0.5 * (values[mid - 1] + values[mid]);
    }
    return values[mid];
}

} // namespace

// ---------------------------------------------------------------------------
// parseStepConfig
// ---------------------------------------------------------------------------

bool StepDetector::parseStepConfig(const QString& path,
                                   QVector<StepDefinition>& out,
                                   QString& error)
{
    out.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        error = "Could not open step-config file '" + QFileInfo(path).fileName() + "'.";
        return false;
    }

    QTextStream in(&file);
    bool in_step = false;
    StepDefinition current;

    auto flush = [&]() {
        if (in_step)
        {
            out.push_back(current);
        }
        current = StepDefinition{};
    };

    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
        {
            continue;
        }

        // Array-of-tables header: [[Step]] starts a new step entry.
        if (line.startsWith("[["))
        {
            flush();
            in_step = line.compare("[[Step]]", Qt::CaseInsensitive) == 0;
            continue;
        }
        // Any other section header ends the current step context.
        if (line.startsWith('['))
        {
            flush();
            in_step = false;
            continue;
        }
        if (!in_step)
        {
            continue;
        }

        const int eq = line.indexOf('=');
        if (eq < 0)
        {
            continue;
        }
        const QString key = line.left(eq).trimmed().toLower();
        const QString val = stripComment(line.mid(eq + 1));

        bool ok = false;
        const double num = val.toDouble(&ok);
        if (!ok)
        {
            continue;
        }
        if (key == "db")
        {
            current.db = num;
        }
        // Each step is pushed on the next [[Step]] header or at EOF by flush().
    }
    flush();

    if (out.isEmpty())
    {
        error = "No [[Step]] entries found in '" + QFileInfo(path).fileName() + "'.";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// detect
// ---------------------------------------------------------------------------

StepDetector::Result StepDetector::detect(const QVector<double>& rawValues,
                                          double samplePeriodSec,
                                          const QVector<StepDefinition>& steps)
{
    Result result;

    const int expected = steps.size();
    if (expected == 0 || rawValues.size() < 2 || samplePeriodSec <= 0.0)
    {
        return result; // invalid by default
    }

    // 1. Sample-to-sample derivative.
    QVector<double> deriv;
    deriv.reserve(rawValues.size() - 1);
    for (int i = 1; i < rawValues.size(); i++)
    {
        deriv.push_back(rawValues[i] - rawValues[i - 1]);
    }

    // 2. Adaptive edge threshold from the robust spread of the derivative.
    //    In-plateau samples dominate, so their noise sets the baseline; real
    //    transitions are large outliers above it.
    const double sigma = robustStdDev(deriv);
    const double nominal_threshold = std::max(
        CalibrationConstants::kEdgeSigmaMultiple * sigma,
        CalibrationConstants::kMinEdgeRawCounts);
    result.edgeThreshold = nominal_threshold;

    // 3. Mark transition samples (|derivative| over threshold). A transition at
    //    deriv index i sits between rawValues[i] and rawValues[i+1]; mark both
    //    endpoints as unstable so plateaus exclude the moving region.
    auto markStable = [&](double threshold) {
        QVector<bool> stable(rawValues.size(), true);
        for (int i = 0; i < deriv.size(); i++)
        {
            if (std::fabs(deriv[i]) > threshold)
            {
                stable[i] = false;
                stable[i + 1] = false;
            }
        }
        return stable;
    };

    // 4. Collect maximal runs of stable samples, then keep only those that hold
    //    long enough to be confirmed as a genuine settled step rather than a
    //    transient/partial-jump blip. The confirmation window is a fixed
    //    duration (not a fraction of a configured dwell, since steps no longer
    //    carry one) with an absolute sample-count floor for coarse sample
    //    periods where the duration alone would be only one or two samples.
    const int confirm_samples = std::max(
        CalibrationConstants::kMinConfirmSamples,
        static_cast<int>(std::ceil(CalibrationConstants::kStepConfirmSeconds / samplePeriodSec)));

    auto findPlateaus = [&](const QVector<bool>& stable) {
        QVector<Plateau> plateaus;
        int run_begin = -1;
        for (int i = 0; i <= rawValues.size(); i++)
        {
            const bool is_stable = (i < rawValues.size()) && stable[i];
            if (is_stable && run_begin < 0)
            {
                run_begin = i;
            }
            else if (!is_stable && run_begin >= 0)
            {
                if (i - run_begin >= confirm_samples)
                {
                    plateaus.push_back(Plateau{run_begin, i});
                }
                run_begin = -1;
            }
        }
        return plateaus;
    };

    // 6. Settled average of every confirmed plateau. Average the confirmation
    //    window at the END of each plateau (the samples immediately before the
    //    next transition) rather than its start: any settling after a jump has
    //    had the rest of the plateau to die out by then, so this avoids
    //    transition contamination without needing a separate trim fraction.
    auto settledAverages = [&](const QVector<Plateau>& plateaus) {
        QVector<double> plateauAvg(plateaus.size());
        for (int k = 0; k < plateaus.size(); k++)
        {
            const Plateau& p = plateaus[k];
            const int lo = std::max(p.begin, p.end - confirm_samples);
            double sum = 0.0;
            for (int i = lo; i < p.end; i++)
            {
                sum += rawValues[i];
            }
            plateauAvg[k] = sum / (p.end - lo);
        }
        return plateauAvg;
    };

    // 7. Select one clean calibration sweep: the first run of `expected`
    //    CONSECUTIVE plateaus whose settled averages move strictly in one
    //    direction (a real receiver response is monotonic — increasing or
    //    decreasing dB with raw count). A genuine step transition was already
    //    large enough to split the plateaus (> `threshold`), so we require each
    //    consecutive level change to exceed that same threshold and keep the
    //    same sign across the whole run.
    //
    //    This is robust to the messy reality of recorded cal files (US5.3): it
    //    skips leading pre-roll/no-signal plateaus, ignores trailing post-roll,
    //    and takes the FIRST of several back-to-back repeats. Crucially it also
    //    rejects flat/inactive channels — their plateau averages only jitter
    //    around the noise floor, never forming a monotonic run — instead of the
    //    old "first N plateaus" rule, which paired no-signal noise with the step
    //    dB values and produced degenerate (all-equal rawAvg) profiles.
    auto selectSweep = [&](const QVector<double>& plateauAvg, double threshold) -> int {
    auto sign = [&](double delta) -> int {
        if (delta > threshold) return 1;
        if (delta < -threshold) return -1;
        return 0; // change too small to be a real step transition
    };

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
    for (int begin = 0; begin + 1 < plateauAvg.size() && run_start < 0; )
    {
        const int dir = sign(plateauAvg[begin + 1] - plateauAvg[begin]);
        if (dir == 0)
        {
            begin++; // sub-threshold step: not a real transition, skip
            continue;
        }
        int end = begin + 1;
        while (end + 1 < plateauAvg.size() &&
               sign(plateauAvg[end + 1] - plateauAvg[end]) == dir)
        {
            end++;
        }
        // Maximal monotonic run is plateaus[begin..end].
        if (end - begin + 1 >= expected)
        {
            run_start = end - (expected - 1); // keep the last `expected` of the run
        }
        else
        {
            begin = end; // too short; the reversal starts the next run here
        }
    }
    return run_start;
    }; // selectSweep

    // 7b. Search the edge threshold, using the expected step count as the target
    //     rather than only as a pass/fail test.
    //
    //     A receiver far out of tolerance does not move a uniform number of raw
    //     counts per dB: one sweep can hold a 12-count step at the compressed end
    //     and a 1100-count step at the expansive end. A single threshold sized by
    //     the whole series' noise cannot resolve both — the small steps never
    //     clear it, so adjacent levels merge and the channel loses its profile
    //     even though the sweep is plainly there.
    //
    //     So try progressively lower thresholds and accept the FIRST (largest,
    //     most conservative) one that yields a clean sweep of `expected` levels.
    //     Relaxing is safe because the two real noise discriminators are unchanged
    //     and neither depends on the threshold: a candidate level must still hold
    //     steady for the confirmation window, and the run must still move strictly
    //     in one direction. A flat/no-signal channel's levels only jitter, so they
    //     never form such a run no matter how far the threshold drops — which is
    //     what stops this from resurrecting the degenerate "noise paired with step
    //     dB values" profiles the monotonic-run rule was introduced to kill.
    const double floor_threshold = std::max(
        CalibrationConstants::kMinEdgeSigmaMultiple * sigma,
        CalibrationConstants::kMinEdgeRawCounts);

    QVector<Plateau> plateaus;
    QVector<double> plateauAvg;
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

        const QVector<Plateau> candidates = findPlateaus(markStable(threshold));
        if (attempt == 0)
        {
            // Report the nominal pass's count, so the diagnostic describes the
            // channel as detection first saw it rather than as the last retry did.
            result.detectedPlateaus = candidates.size();
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
        QVector<double> levels;
        for (double avg : settledAverages(candidates))
        {
            if (!levels.isEmpty() && std::fabs(avg - levels.last()) <= threshold)
            {
                levels.last() = avg; // same level; keep the later, more-settled value
            }
            else
            {
                levels.push_back(avg);
            }
        }
        if (attempt == 0)
        {
            // Recorded before the count check below, so a channel that bails out
            // early still reports what it saw — that is exactly the case where
            // the levels are the only thing that explains the verdict (one level
            // at 0 is a dead input; one level at full scale is a railed one).
            result.plateauLevels = levels;
        }
        best_levels = std::max(best_levels, static_cast<int>(levels.size()));
        if (levels.size() < expected)
        {
            continue; // steps still merged; loosen and look again
        }
        reached_sweep_stage = true;

        const QVector<double>& averages = levels;
        const int start = selectSweep(averages, threshold);
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
        // Report the furthest gate reached. A channel that never resolved more
        // than one level did not "merge its steps" — it never moved at all, so
        // the sweep is absent from the recording rather than beyond the
        // detector's reach, and the two must not read the same.
        result.outcome = (best_levels <= 1)      ? CalibrationOutcome::FlatNoSignal
                         : reached_sweep_stage   ? CalibrationOutcome::NoMonotonicSweep
                                                 : CalibrationOutcome::TooFewPlateaus;
        return result; // linear fallback
    }
    result.extraPlateaus = plateaus.size() > expected;

    // 8. Pair the selected sweep with the steps in time order (Nth plateau of
    //    the sweep <-> Nth step), then sort by raw so interpolateCalibration()
    //    can assume ascending rawAvg.
    CalibrationProfile& profile = result.profile;
    profile.points.reserve(expected);
    for (int k = 0; k < expected; k++)
    {
        CalibrationPoint pt;
        pt.rawAvg = plateauAvg[run_start + k];
        pt.trueDb = steps[k].db;
        profile.points.push_back(pt);
    }
    std::sort(profile.points.begin(), profile.points.end(),
              [](const CalibrationPoint& a, const CalibrationPoint& b) {
                  return a.rawAvg < b.rawAvg;
              });

    profile.valid  = true;
    result.outcome = CalibrationOutcome::Calibrated;
    return result;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

double StepDetector::robustStdDev(const QVector<double>& values)
{
    if (values.isEmpty())
    {
        return 0.0;
    }
    const double med = median(values);
    QVector<double> abs_dev;
    abs_dev.reserve(values.size());
    for (double v : values)
    {
        abs_dev.push_back(std::fabs(v - med));
    }
    // 1.4826 scales the median absolute deviation to a Gaussian std-dev estimate.
    return 1.4826 * median(abs_dev);
}

