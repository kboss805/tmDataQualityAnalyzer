/**
 * @file calibrationprofile.h
 * @brief Non-linear step-calibration data types (US5.3).
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
 * @endcode
 *
 * No dwell/duration is configured: StepDetector confirms a step by requiring
 * the level to hold steady for a fixed window (see
 * CalibrationConstants::kStepConfirmSeconds) rather than a per-step duration,
 * so technicians don't have to know or configure how long each step lasts.
 */
struct StepDefinition
{
    double db = 0.0; ///< True SNR value (dB) injected during this step.
};

/**
 * @brief Why a channel did or did not receive a non-linear profile.
 *
 * A channel that fails falls back to the linear slope/offset model, which is a
 * legitimate outcome rather than an error — but the operator needs to know
 * WHICH gate rejected it, because the remedies differ completely: a channel with
 * no data was never in the recording, whereas a channel whose steps were too
 * small to resolve is a receiver worth re-checking (or a clip/threshold worth
 * adjusting). Reporting only a success count, as the summary previously did,
 * makes those indistinguishable.
 */
enum class CalibrationOutcome
{
    Calibrated,       ///< A valid profile was built.
    NoData,           ///< No samples extracted for this channel (absent/disabled word).
    FlatNoSignal,     ///< Samples arrived but never moved: the channel holds one level for
                      ///< the whole recording (a dead input reads 0; a railed one reads full
                      ///< scale). No detector change can calibrate this — the sweep is not in
                      ///< the recording — so it must read differently from the merged-steps
                      ///< case below, which IS a detection limit.
    TooFewPlateaus,   ///< Fewer confirmed plateaus than expected steps — adjacent steps
                      ///< merged because their raw separation never cleared the edge
                      ///< threshold (typical of a compressed / near-saturation receiver).
    NoMonotonicSweep, ///< Enough plateaus, but no run of `expected` consecutive levels
                      ///< moving strictly one way — a flat/no-signal channel, or a
                      ///< response that reverses direction partway through the sweep.
};

/// Human-readable phrase for @p outcome, used in the calibration summary.
inline const char* calibrationOutcomeText(CalibrationOutcome outcome)
{
    switch (outcome)
    {
        case CalibrationOutcome::Calibrated:       return "calibrated";
        case CalibrationOutcome::NoData:           return "no data";
        case CalibrationOutcome::FlatNoSignal:     return "flat / no signal";
        case CalibrationOutcome::TooFewPlateaus:   return "too few plateaus";
        case CalibrationOutcome::NoMonotonicSweep: return "no monotonic sweep";
    }
    return "unknown";
}

/**
 * @brief One measured calibration point: a detected raw-count average mapped to
 *        the true dB value of the corresponding step.
 */
struct CalibrationPoint
{
    double rawAvg = 0.0; ///< Average raw count over the confirmed-stable window for this step.
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
 * first / above the last point the value is CLAMPED to that endpoint's dB
 * (it does not extrapolate). A receiver driven outside its calibrated range
 * (e.g. badly out of calibration) would otherwise extrapolate to wildly
 * divergent values that differ per channel; clamping pegs it at the nearest
 * calibrated limit so an out-of-range reading is obvious and bounded.
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
        return pts[0].trueDb;                            // clamp below (out of cal range)
    }
    if (raw >= pts[n - 1].rawAvg)
    {
        return pts[n - 1].trueDb;                        // clamp above (out of cal range)
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
