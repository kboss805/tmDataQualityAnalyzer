/**
 * @file calibrationreport.cpp
 * @brief Implementation of calibrationReport - the operator-facing report of a calibration extraction (US5.3).
 */

#include "calibrationreport.h"

#include <algorithm>
#include <format>
#include <map>
#include <string>

#include "calibrationprofile.h"
#include "constants.h"
#include "containerutil.h"
#include "receivernames.h"
#include "textformat.h"

namespace {

/// The strings of @p parts separated by @p separator.
std::string joinStrings(const std::vector<std::string>& parts, const std::string& separator)
{
    std::string joined;
    for (std::size_t i = 0; i < parts.size(); i++)
    {
        if (i > 0)
        {
            joined += separator;
        }
        joined += parts[i];
    }
    return joined;
}

} // namespace

std::string calibrationReport(const std::vector<CalibrationChannelResult>& results, int expectedSteps)
{
    if (results.empty())
    {
        return "No receiver channels were extracted.";
    }

    // Group channels by receiver. Receiver 0 collects anything whose name carries
    // no "_RCVR<N>" suffix (a hand-written receiver-params TOML may name its
    // parameters anything); those are reported by name instead of by number.
    struct Group
    {
        std::vector<std::string> calibrated;           ///< Channel parts that succeeded.
        std::vector<std::string> relaxed;              ///< …of those, ones needing a lower threshold.
        std::vector<std::string> saturated;            ///< …of those, ones whose top steps railed.
        int         recordingLevels = 0;               ///< Most levels a calibrated channel's sweep
                                                       ///< held, when well beyond the step file's.
        double      topDb = 0.0;                       ///< Highest dB those actually measured.
        std::vector<double> unresolved;                ///< dB no level could be paired with.
        std::map<CalibrationOutcome, std::vector<std::string>> failed;  ///< Channel parts per failure reason.
        std::vector<std::string> flatLevels;           ///< Stuck levels of flat channels.
        int minPlateaus = -1;                          ///< Fewest plateaus seen among failures.
    };
    std::map<int, Group> byReceiver;

    int calibratedChannels = 0;
    for (const CalibrationChannelResult& r : results)
    {
        const int receiver = receivernames::receiverIndexFromName(r.name);
        const std::string part = (receiver > 0) ? receivernames::channelPartOfName(r.name) : r.name;
        Group& g = byReceiver[receiver];

        if (r.outcome == CalibrationOutcome::Calibrated)
        {
            g.calibrated.push_back(part);
            if (r.sweepLevels - expectedSteps > CalibrationConstants::kMaxLeadInLevels)
            {
                g.recordingLevels = std::max(g.recordingLevels, r.sweepLevels);
            }
            if (r.thresholdRelaxed)
            {
                g.relaxed.push_back(part);
            }
            if (r.saturated)
            {
                g.saturated.push_back(part);
                g.topDb = std::max(g.topDb, topResolvedDb(r.profile));
                for (double db : r.unresolvedDb)
                {
                    if (std::find(g.unresolved.begin(), g.unresolved.end(), db) == g.unresolved.end())
                    {
                        g.unresolved.push_back(db);
                    }
                }
            }
            calibratedChannels++;
        }
        else
        {
            g.failed[r.outcome].push_back(part);
            if (r.hadData && (g.minPlateaus < 0 || r.detectedPlateaus < g.minPlateaus))
            {
                g.minPlateaus = r.detectedPlateaus;
            }
            if (r.outcome == CalibrationOutcome::FlatNoSignal && !r.plateauLevels.empty())
            {
                const std::string level = textformat::formatFixed(r.plateauLevels.front(), 0);
                if (std::find(g.flatLevels.begin(), g.flatLevels.end(), level) == g.flatLevels.end())
                {
                    g.flatLevels.push_back(level);
                }
            }
        }
    }

    // Receivers whose every channel calibrated collapse to a bare number list -
    // the common good case should read as one short line, not one line each.
    // Channels with no receiver number are one group of their own; when every one of
    // them calibrated they are listed by name beside that line, not under the fallback
    // heading (they did not fall back).
    std::vector<std::string> fullyCalibrated;
    std::vector<std::string> unnamedCalibrated;
    std::vector<std::string> detailLines;
    std::vector<std::string> relaxedLines;
    std::vector<std::string> saturatedLines;
    std::vector<std::string> stepFileLines;
    std::vector<std::string> wordMapLines;
    for (const auto& [receiver, g] : byReceiver)
    {
        const std::string label = (receiver > 0) ? std::format("Receiver {}", receiver)
                                                 : std::string("Unnamed receiver");

        if (g.recordingLevels > 0)
        {
            stepFileLines.push_back(std::format("  {} — {} levels in the recording, {} in the step file",
                                                label, g.recordingLevels, expectedSteps));
        }

        // One channel swept and another never moved: the signature a word map that
        // does not match the recording leaves, because it files one receiver's words
        // under another's name. A genuinely dead channel looks the same, so this is
        // a prompt to check, not a verdict.
        const auto flat = g.failed.find(CalibrationOutcome::FlatNoSignal);
        if (receiver > 0 && !g.calibrated.empty() && flat != g.failed.end())
        {
            wordMapLines.push_back(std::format("  {} — {} calibrated; {} never moved",
                                               label, joinStrings(g.calibrated, ", "),
                                               joinStrings(flat->second, ", ")));
        }

        if (!g.relaxed.empty())
        {
            relaxedLines.push_back(std::format("  {} — {}", label, joinStrings(g.relaxed, ", ")));
        }

        if (!g.saturated.empty())
        {
            std::vector<std::string> unresolvedText;
            for (double db : g.unresolved)
            {
                unresolvedText.push_back(textformat::formatFixed(db, 0));
            }
            saturatedLines.push_back(std::format(
                "  {} — {}: readings hold at {} dB{}",
                label, joinStrings(g.saturated, ", "), textformat::formatFixed(g.topDb, 0),
                unresolvedText.empty()
                    ? std::string()
                    : std::format(" (no measurement for {} dB)", joinStrings(unresolvedText, ", "))));
        }

        if (g.failed.empty())
        {
            if (receiver > 0)
            {
                fullyCalibrated.push_back(std::to_string(receiver));
            }
            else
            {
                unnamedCalibrated = g.calibrated;
            }
            continue;
        }

        // Mixed or wholly failed: say which channels fell back and why. A partly
        // calibrated receiver is the case most worth spelling out, because the
        // count alone would let it hide inside "N of M".
        std::vector<std::string> reasons;
        for (const auto& [outcome, channels] : g.failed)
        {
            std::string reason = std::format("{}: {}", joinStrings(channels, ", "),
                                             calibrationOutcomeText(outcome));
            if (outcome == CalibrationOutcome::TooFewPlateaus && g.minPlateaus >= 0)
            {
                reason += std::format(" ({} of {} steps resolved)", g.minPlateaus, expectedSteps);
            }
            else if (outcome == CalibrationOutcome::FlatNoSignal && !g.flatLevels.empty())
            {
                // The level itself is the diagnosis: 0 is a dead input, full
                // scale is a railed one, and anything between is a stuck DC
                // level. "Flat" alone would leave the operator guessing which.
                reason += std::format(" (held at {})", joinStrings(g.flatLevels, ", "));
            }
            reasons.push_back(reason);
        }

        const std::string calibratedPart =
            g.calibrated.empty()
                ? std::string()
                : std::format("{} calibrated; ", joinStrings(g.calibrated, ", "));
        detailLines.push_back(std::format("  {} — {}{}", label, calibratedPart,
                                          joinStrings(reasons, "; ")));
    }

    std::vector<std::string> out;
    out.push_back(std::format("Applied non-linear calibration to {} of {} channel(s).",
                              calibratedChannels, intSize(results)));

    if (!fullyCalibrated.empty() || !unnamedCalibrated.empty())
    {
        out.push_back("");
        if (!fullyCalibrated.empty())
        {
            out.push_back(std::format("Calibrated: receiver{} {}.",
                                      fullyCalibrated.size() == 1 ? "" : "s",
                                      joinStrings(fullyCalibrated, ", ")));
        }
        if (!unnamedCalibrated.empty())
        {
            out.push_back(std::format("Calibrated (unnamed channels): {}.",
                                      joinStrings(unnamedCalibrated, ", ")));
        }
    }

    // The two checks below come before the fallback list on purpose: they flag
    // results that LOOK successful but may be wrong, which the operator would
    // otherwise only discover from the plot.
    if (!stepFileLines.empty())
    {
        out.push_back("");
        out.push_back("Check the step file — these receivers stepped through more levels "
                      "than it lists, so the lowest were set aside as lead-in. If those "
                      "are real steps, every step is paired with the wrong level:");
        out.insert(out.end(), stepFileLines.begin(), stepFileLines.end());
    }

    if (!wordMapLines.empty())
    {
        out.push_back("");
        out.push_back("Check the word map — in these receivers one channel swept and "
                      "another never moved. A word map that does not match the recording "
                      "files one receiver's words under another's name; otherwise a "
                      "channel may be dead:");
        out.insert(out.end(), wordMapLines.begin(), wordMapLines.end());
    }

    if (!detailLines.empty())
    {
        out.push_back("");
        out.push_back("Fell back to linear calibration:");
        out.insert(out.end(), detailLines.begin(), detailLines.end());
    }

    // Calibrated, but the receiver ran out of range partway up the sweep, so the
    // profile stops there and clamps above it. Reported because the plot then
    // cannot rise past that step no matter how strong the signal gets - which
    // reads as a ceiling in the data rather than as a receiver setting to fix.
    if (!saturatedLines.empty())
    {
        out.push_back("");
        out.push_back("Calibrated only up to a saturated top — these receivers ran out "
                      "of range partway through the sweep, so their readings hold at "
                      "the highest step still measurable:");
        out.insert(out.end(), saturatedLines.begin(), saturatedLines.end());
    }

    // Calibrated, but only after loosening the edge threshold: the steps sat close
    // to this channel's own noise. Reported because it is the early warning the
    // count cannot give — these receivers calibrated, yet they are the ones
    // drifting toward the point where they will stop calibrating at all.
    if (!relaxedLines.empty())
    {
        out.push_back("");
        out.push_back("Calibrated only with a relaxed threshold — steps are close to "
                      "the noise floor, so these receivers are worth checking:");
        out.insert(out.end(), relaxedLines.begin(), relaxedLines.end());
    }

    return joinStrings(out, "\n");
}
