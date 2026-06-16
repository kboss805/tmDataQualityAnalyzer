/**
 * @file calibrationprofile.h
 * @brief Non-linear step-calibration data types (US3.2).
 *
 * A CalibrationProfile is an optional, session-only per-channel correction that
 * replaces the linear (raw * slope + offset) SNR model with piecewise-linear
 * interpolation between measured calibration points. Profiles are extracted from
 * a calibration Chapter 10 file plus a step-config TOML; they are never written
 * to disk.
 */

#ifndef CALIBRATIONPROFILE_H
#define CALIBRATIONPROFILE_H

#include <QVector>

/**
 * @brief One expected calibration step, read from the step-config TOML.
 *
 * The TOML uses an array of tables:
 * @code
 * [[Step]]
 * db = 0.0
 * dwell_sec = 5.0
 * @endcode
 */
struct StepDefinition
{
    double db       = 0.0; ///< True SNR value (dB) injected during this step.
    double dwellSec = 0.0; ///< How long the step is held, in seconds.
};

/**
 * @brief One measured calibration point: a detected raw-count average mapped to
 *        the true dB value of the corresponding step.
 */
struct CalibrationPoint
{
    double rawAvg = 0.0; ///< Edge-trimmed average raw count for this step's plateau.
    double trueDb = 0.0; ///< True dB value of the paired step (from the TOML).
};

/**
 * @brief Per-channel non-linear calibration profile.
 *
 * @c points are ordered to match the step order in the TOML (Nth detected
 * plateau paired with the Nth step). When @c valid is false the channel falls
 * back to the standard linear calibration math.
 */
struct CalibrationProfile
{
    QVector<CalibrationPoint> points; ///< Measured (rawAvg -> trueDb) points, sorted ascending by rawAvg.
    bool valid = false;               ///< True if this channel was successfully calibrated.
};

/**
 * @brief Converts a raw count to dB using piecewise-linear interpolation.
 *
 * Between calibration points the value is linearly interpolated. Below the
 * first / above the last point the value is extrapolated along the slope of the
 * nearest two points (so the response does not flatline at the extremes).
 *
 * @pre @p profile.points is sorted ascending by rawAvg and contains >= 2 points
 *      (guaranteed when profile.valid is true).
 */
inline double interpolateCalibration(double raw, const CalibrationProfile& profile)
{
    const QVector<CalibrationPoint>& pts = profile.points;
    const int n = pts.size();
    if (n == 0)
    {
        return raw;
    }
    if (n == 1)
    {
        return pts[0].trueDb;
    }

    auto segmentValue = [&](int a, int b, double x) -> double {
        const double dr = pts[b].rawAvg - pts[a].rawAvg;
        if (dr == 0.0)
        {
            return pts[a].trueDb; // coincident raw values: avoid divide-by-zero
        }
        const double slope = (pts[b].trueDb - pts[a].trueDb) / dr;
        return pts[a].trueDb + slope * (x - pts[a].rawAvg);
    };

    if (raw <= pts[0].rawAvg)
    {
        return segmentValue(0, 1, raw);                 // extrapolate below
    }
    if (raw >= pts[n - 1].rawAvg)
    {
        return segmentValue(n - 2, n - 1, raw);         // extrapolate above
    }
    for (int i = 1; i < n; i++)
    {
        if (raw <= pts[i].rawAvg)
        {
            return segmentValue(i - 1, i, raw);         // interpolate within
        }
    }
    return pts[n - 1].trueDb; // unreachable
}

#endif // CALIBRATIONPROFILE_H
