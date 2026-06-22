/**
 * @file stepdetector.cpp
 * @brief Implementation of StepDetector — edge-based plateau detection and
 *        step-config TOML parsing for non-linear calibration (US3.2).
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
    const double threshold = std::max(
        CalibrationConstants::kEdgeSigmaMultiple * sigma,
        CalibrationConstants::kMinEdgeRawCounts);

    // 3. Mark transition samples (|derivative| over threshold). A transition at
    //    deriv index i sits between rawValues[i] and rawValues[i+1]; mark both
    //    endpoints as unstable so plateaus exclude the moving region.
    QVector<bool> stable(rawValues.size(), true);
    for (int i = 0; i < deriv.size(); i++)
    {
        if (std::fabs(deriv[i]) > threshold)
        {
            stable[i] = false;
            stable[i + 1] = false;
        }
    }

    // 4. Collect maximal runs of stable samples, then keep only those that hold
    //    long enough to be confirmed as a genuine settled step rather than a
    //    transient/partial-jump blip. The confirmation window is a fixed
    //    duration (not a fraction of a configured dwell, since steps no longer
    //    carry one) with an absolute sample-count floor for coarse sample
    //    periods where the duration alone would be only one or two samples.
    const int confirm_samples = std::max(
        CalibrationConstants::kMinConfirmSamples,
        static_cast<int>(std::ceil(CalibrationConstants::kStepConfirmSeconds / samplePeriodSec)));

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

    result.detectedPlateaus = plateaus.size();

    // 5. Validity: need at least as many confirmed plateaus as expected steps.
    if (plateaus.size() < expected)
    {
        return result; // profile stays invalid -> linear fallback
    }
    result.extraPlateaus = plateaus.size() > expected;

    // 6. Settled average of every confirmed plateau. Average the confirmation
    //    window at the END of each plateau (the samples immediately before the
    //    next transition) rather than its start: any settling after a jump has
    //    had the rest of the plateau to die out by then, so this avoids
    //    transition contamination without needing a separate trim fraction.
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

    // 7. Select one clean calibration sweep: the first run of `expected`
    //    CONSECUTIVE plateaus whose settled averages move strictly in one
    //    direction (a real receiver response is monotonic — increasing or
    //    decreasing dB with raw count). A genuine step transition was already
    //    large enough to split the plateaus (> `threshold`), so we require each
    //    consecutive level change to exceed that same threshold and keep the
    //    same sign across the whole run.
    //
    //    This is robust to the messy reality of recorded cal files (US3.2): it
    //    skips leading pre-roll/no-signal plateaus, ignores trailing post-roll,
    //    and takes the FIRST of several back-to-back repeats. Crucially it also
    //    rejects flat/inactive channels — their plateau averages only jitter
    //    around the noise floor, never forming a monotonic run — instead of the
    //    old "first N plateaus" rule, which paired no-signal noise with the step
    //    dB values and produced degenerate (all-equal rawAvg) profiles.
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
    for (int begin = 0; begin + 1 < plateaus.size() && run_start < 0; )
    {
        const int dir = sign(plateauAvg[begin + 1] - plateauAvg[begin]);
        if (dir == 0)
        {
            begin++; // sub-threshold step: not a real transition, skip
            continue;
        }
        int end = begin + 1;
        while (end + 1 < plateaus.size() &&
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

    if (run_start < 0)
    {
        return result; // no clean monotonic sweep -> linear fallback
    }

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

    profile.valid = true;
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

