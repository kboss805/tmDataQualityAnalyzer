/**
 * @file stepsaturated.h
 * @brief The saturated-top fallback of the step detector: a profile over the steps that stayed in range.
 *
 * A receiver whose gain is set too high runs off the end of its converter partway through the sweep, so the recording
 * cannot hold every step as a distinct level. `StepDetector::detect` tries this when the full-sweep selection finds none.
 */

#ifndef STEPSATURATED_H
#define STEPSATURATED_H

#include <vector>

#include "calibrationprofile.h"
#include "stepdetector.h"
#include "stepplateaus.h"

namespace stepsaturated {

/**
 * @brief Calibrates the steps that stayed in range of a sweep whose top railed, or says it cannot.
 *
 * Takes the longest monotonic run of the nominal pass's @p levels, drops the railed dwells at its top, numbers the
 * survivors by the regular grid of the sweep's transitions (so a dwell missing from the middle is not mistaken for a
 * shift), and measures the dwells the grid sees but plateau detection missed. A recording whose dwells are not held for a
 * regular time keeps the linear fallback rather than being handed a guess.
 *
 * @param[in]  rawValues         Per-sample raw counts for one channel.
 * @param[in]  deriv             Their sample-to-sample difference.
 * @param[in]  levels            The levels the nominal pass found.
 * @param[in]  steps             Expected steps.
 * @param[in]  nominal_threshold The nominal edge threshold.
 * @param[in]  confirm_samples   The confirmation window, in samples.
 * @param[out] result            Receives the profile and the diagnostics when this returns true.
 * @return true if a partial profile was built.
 */
bool calibrateSaturatedTop(const std::vector<double>& rawValues,
                           const std::vector<double>& deriv,
                           const std::vector<stepplateaus::Level>& levels,
                           const std::vector<StepDefinition>& steps,
                           double nominal_threshold,
                           int confirm_samples,
                           StepDetector::Result& result);

} // namespace stepsaturated

#endif // STEPSATURATED_H
