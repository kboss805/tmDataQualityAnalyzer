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
        else if (key == "dwell_sec")
        {
            current.dwellSec = num;
        }
        // Each step is pushed on the next [[Step]] header or at EOF by flush().
    }
    flush();

    // Validate.
    for (const StepDefinition& s : out)
    {
        if (s.dwellSec <= 0.0)
        {
            error = "Every [[Step]] must define a positive dwell_sec.";
            out.clear();
            return false;
        }
    }
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

    // 4. Collect maximal runs of stable samples that are long enough to be a
    //    real dwell (rejects short transition slivers).
    double min_dwell = steps[0].dwellSec;
    for (const StepDefinition& s : steps)
    {
        min_dwell = std::min(min_dwell, s.dwellSec);
    }
    const int min_plateau_samples = std::max(
        2, static_cast<int>(CalibrationConstants::kMinPlateauDwellFraction
                            * (min_dwell / samplePeriodSec)));

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
            if (i - run_begin >= min_plateau_samples)
            {
                plateaus.push_back(Plateau{run_begin, i});
            }
            run_begin = -1;
        }
    }

    result.detectedPlateaus = plateaus.size();

    // 5. Validity: need at least as many plateaus as expected steps.
    if (plateaus.size() < expected)
    {
        return result; // profile stays invalid -> linear fallback
    }
    result.extraPlateaus = plateaus.size() > expected;

    // 6. Pair the first `expected` plateaus (time order) with the steps.
    CalibrationProfile& profile = result.profile;
    profile.points.reserve(expected);
    for (int k = 0; k < expected; k++)
    {
        CalibrationPoint pt;
        pt.rawAvg = trimmedMean(rawValues, plateaus[k]);
        pt.trueDb = steps[k].db;
        profile.points.push_back(pt);
    }

    // Sort by raw so interpolateCalibration() can assume ascending rawAvg. We
    // make no monotonicity assumption about the receiver response, so the dB
    // values are simply carried along with their raw averages.
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

double StepDetector::trimmedMean(const QVector<double>& rawValues, const Plateau& p)
{
    const int len = p.end - p.begin;
    const int trim = static_cast<int>(CalibrationConstants::kEdgeTrimFraction * len);
    int lo = p.begin + trim;
    int hi = p.end - trim;
    if (hi - lo < 1) // trimming removed everything; fall back to the full span
    {
        lo = p.begin;
        hi = p.end;
    }
    double sum = 0.0;
    for (int i = lo; i < hi; i++)
    {
        sum += rawValues[i];
    }
    return sum / (hi - lo);
}
