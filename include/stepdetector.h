/**
 * @file stepdetector.h
 * @brief Detects calibration step plateaus in a raw-count series and builds a
 *        per-channel non-linear CalibrationProfile (US3.2).
 *
 * Pure logic with no Qt UI dependencies, so it is unit-testable in isolation.
 * Detection uses derivative/edge detection: large sample-to-sample changes mark
 * step transitions, the stable spans between them are plateaus, each plateau is
 * edge-trimmed and averaged, and plateaus are paired in time order with the
 * expected steps from the step-config TOML.
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
    };

    /**
     * @brief Parses an `[[Step]]` array-of-tables step-config TOML file.
     *
     * The custom QSettings TOML format does not support arrays, so this is a
     * dedicated line parser. Each `[[Step]]` table must provide `db` and
     * `dwell_sec`.
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
     * @brief Detects step plateaus in @p rawValues and builds a CalibrationProfile.
     *
     * The Nth detected plateau (time order) is paired with @p steps[N]. The
     * channel is valid only if at least @p steps.size() plateaus are detected;
     * if more are detected, the first steps.size() are used and
     * Result::extraPlateaus is set.
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

    /// Edge-trimmed mean of rawValues over [p.begin, p.end).
    static double trimmedMean(const QVector<double>& rawValues, const Plateau& p);
};

#endif // STEPDETECTOR_H
