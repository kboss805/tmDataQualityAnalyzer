/**
 * @file calibrationreport.h
 * @brief The operator-facing report of a calibration extraction (US5.3).
 *
 * What `CalibrationExtractor` found, in words: which receivers were calibrated and, for the rest, why. It
 * is pure (results in, text out), so the wording is unit-testable without running an extraction over a
 * real .ch10 file, and it lives apart from the extractor, which drives the reader and the worker.
 *
 * Pinned by the report cases in tests/core/test_calibrationextractor.cpp.
 */

#ifndef CALIBRATIONREPORT_H
#define CALIBRATIONREPORT_H

#include <string>
#include <vector>

#include "calibrationextractor.h"

/**
 * @brief Builds the operator-facing calibration report from @p results.
 *
 * Names which receivers were calibrated and, for the rest, why - grouped by
 * receiver (the unit operators actually think and speak in) rather than by
 * the individual L/R/C words. A bare "calibrated N of M channel(s)" count
 * cannot distinguish a receiver that was absent from the recording from one
 * whose steps were too compressed to resolve, which are entirely different
 * problems with entirely different remedies.
 *
 * @param[in] results       Per-channel outcomes, in word order.
 * @param[in] expectedSteps Step count from the step-config TOML, so a
 *                          plateau shortfall can be reported against it.
 * @return Multi-line UTF-8 report suitable for a message box.
 */
std::string calibrationReport(const std::vector<CalibrationChannelResult>& results, int expectedSteps);

#endif // CALIBRATIONREPORT_H
