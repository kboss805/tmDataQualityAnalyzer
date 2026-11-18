/**
 * @file stepconfigfile.h
 * @brief Reader of the step-config file: the calibration steps (their dB levels) a receiver's recording is held to.
 *
 * An `[[Step]]` array-of-tables TOML file lists the levels the signal generator stepped through. It is read here, beside
 * `framesyncfile.h` and `receiverparamsfile.h`, so that the Extract Calibration dialog (through the ABI) and the native
 * tests read it one way; the detection that pairs a recording's plateaus with these steps is `StepDetector`'s.
 */

#ifndef STEPCONFIGFILE_H
#define STEPCONFIGFILE_H

#include <filesystem>
#include <string>
#include <vector>

#include "calibrationprofile.h"

/**
 * @brief Parses an `[[Step]]` array-of-tables step-config TOML file.
 *
 * It reads the file through `TomlDocument` (its `tables("step")`), so a BOM or UTF-16 is handled as for every other
 * settings file. Each `[[Step]]` table is one step, and its `db` gives the level; a table without a usable `db` still
 * counts, at 0. The header and the key are matched without regard to ASCII case; the header must be exactly `[[Step]]`
 * (a trailing comment on it means it is not a step header); a value that is not a bare number is skipped and leaves the
 * step at 0. The native tests (test_stepconfigfile.cpp) pin each of these.
 *
 * @param[in]  path  Path to the step-config TOML file.
 * @param[out] out   Parsed steps, in file order.
 * @param[out] error Human-readable error message on failure (UTF-8).
 * @return true if at least one valid step was parsed.
 */
bool parseStepConfig(const std::filesystem::path& path, std::vector<StepDefinition>& out, std::string& error);

/**
 * @brief The step-config TOML the Extract Calibration dialog opens with.
 *
 * Almost every calibration uses the same step file, so the dialog preloads one rather than making the operator browse to
 * it each time. It prefers the file this stream last used (an earlier extraction, or a processing template's stored
 * reference) and otherwise falls back to the shipped default.
 *
 * @param[in] appRoot   Application root holding the settings/ tree.
 * @param[in] preferred The stream's previous step file; may be empty.
 * @return @p preferred if it names an existing file; else `<appRoot>/settings/rcvr_cals/default.toml` if that exists; else
 *         empty. UTF-8 in and out, and a plain string rather than a path so the displayed form keeps its forward slashes.
 */
std::string initialStepConfigPath(const std::string& appRoot, const std::string& preferred);

#endif // STEPCONFIGFILE_H
