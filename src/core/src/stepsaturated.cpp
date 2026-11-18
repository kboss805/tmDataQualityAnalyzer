/**
 * @file stepsaturated.cpp
 * @brief Implementation of the saturated-top fallback of the step detector.
 */

#include "stepsaturated.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "constants.h"
#include "containerutil.h"
#include "stepplateaus.h"

using namespace stepplateaus;

namespace {

/// True when a level sits at the very top or bottom of the converter's range -
/// the receiver had no headroom left, so the dwell is not a measurement.
bool isRailedLevel(double value)
{
    const double full_scale = PCMConstants::kMaxRawSampleValue;
    return value >= CalibrationConstants::kSaturatedRawFraction * full_scale
           || value <= (1.0 - CalibrationConstants::kSaturatedRawFraction) * full_scale;
}

/// Where each step transition happened, and which step each one lands on.
struct DwellGrid
{
    std::vector<int> boundaries;  ///< Derivative indices where a transition happened.
    std::vector<int> slot;        ///< Step index each boundary lands on.
    double       dwell = 0.0; ///< Median samples per dwell.
};

/// Builds the dwell grid from the TRANSITIONS rather than the plateau spans.
///
/// A plateau's end is not reliably on the grid - noise shatters the tail of a
/// dwell into runs too short to confirm, which on the step_cal recording ends
/// R_RCVR1's 36 dB plateau a third of a dwell early. A plateau's BEGIN is always
/// inside its own dwell, though (the transition samples either side are marked
/// unstable), so counting whole dwells between transitions places every level.
///
/// Returns false when the recording cannot support a grid: a sweep whose dwells
/// were not each held for the same length of time keeps the linear fallback
/// rather than being handed a silently mis-numbered profile.
bool buildDwellGrid(const std::vector<double>& deriv, double majorEdge, DwellGrid& out)
{
    out = DwellGrid{};

    for (int i = 0; i < intSize(deriv); i++)
    {
        if (std::fabs(deriv[i]) < majorEdge)
        {
            continue;
        }
        // One transition can smear across the window that straddles it, so
        // marks a sample or two apart are the same boundary.
        if (out.boundaries.empty() || i - out.boundaries.back() > 2)
        {
            out.boundaries.push_back(i);
        }
    }
    if (intSize(out.boundaries) < 2)
    {
        return false;
    }
    std::vector<double> spacings;
    for (int i = 1; i < intSize(out.boundaries); i++)
    {
        spacings.push_back(out.boundaries[i] - out.boundaries[i - 1]);
    }
    out.dwell = median(spacings);
    if (out.dwell <= 0.0)
    {
        return false;
    }
    // Step index of each boundary. A gap of two dwells means one step never
    // produced a transition of its own - the railed pair at the top, or a
    // dwell noise ate - and the numbering must skip it rather than shift.
    out.slot.assign(intSize(out.boundaries), 0);
    for (int i = 1; i < intSize(out.boundaries); i++)
    {
        const double gap     = (out.boundaries[i] - out.boundaries[i - 1]) / out.dwell;
        const int    spanned = static_cast<int>(std::llround(gap));
        if (spanned < 1 || std::fabs(gap - spanned) > CalibrationConstants::kMaxDwellSlotError)
        {
            return false; // dwells were not held for a uniform time; do not guess
        }
        out.slot[i] = out.slot[i - 1] + spanned;
    }
    return true;
}

/// A dwell inside the sweep that never confirmed a plateau is not missing
/// from the RECORDING, only from plateau detection - the grid says exactly
/// where it is. Leaving it out is not neutral: interpolating straight across
/// the hole assumes the receiver is linear there, and an out-of-tolerance
/// receiver is not. On the step_cal recording, L_RCVR1's 24 dB dwell sits at
/// 28556 counts; bridged from 18 dB to 30 dB it read 23.09 dB. So measure it
/// directly: the median of its settled tail, between the two transitions
/// that bound it. Only a gap of exactly one dwell qualifies (a wider one
/// holds several steps that cannot be told apart), the value must fall
/// between its measured neighbours, and railed dwells above the top
/// measured step stay unresolved - there is nothing there to measure.
void measureUnconfirmedDwells(const std::vector<double>& rawValues,
                             const std::vector<StepDefinition>& steps,
                             const DwellGrid& grid,
                             int top_slot,
                             int confirm_samples,
                             Flags& paired,
                             std::vector<double>& raw_of_slot,
                             CalibrationProfile& profile)
{
    const std::vector<int>& boundaries    = grid.boundaries;
    const std::vector<int>& boundary_slot = grid.slot;
    const int expected = intSize(steps);
    for (int i = 0; i + 1 < intSize(boundaries); i++)
    {
        if (boundary_slot[i + 1] - boundary_slot[i] != 1)
        {
            continue;
        }
        const int slot = boundary_slot[i] + 1;
        // Slots are counted from the run's first level, so a transition before the
        // run has a negative one: that dwell is not part of the sweep.
        if (slot < 0 || slot >= top_slot || paired[slot])
        {
            continue;
        }
        int below = slot - 1;
        while (below >= 0 && !paired[below])
        {
            below--;
        }
        int above = slot + 1;
        while (above < expected && !paired[above])
        {
            above++;
        }
        if (below < 0 || above >= expected)
        {
            continue;
        }
        // The tail of the dwell, clear of both transitions: settling has died
        // out by then, and the sample straddling the next edge is excluded.
        const int lo = std::max(boundaries[i] + 2, boundaries[i + 1] - confirm_samples);
        const int hi = boundaries[i + 1];
        if (hi - lo < CalibrationConstants::kMinConfirmSamples)
        {
            continue;
        }
        std::vector<double> tail;
        tail.reserve(hi - lo);
        for (int j = lo; j < hi; j++)
        {
            tail.push_back(rawValues[j]);
        }
        const double level = median(tail);
        const double lower = std::min(raw_of_slot[below], raw_of_slot[above]);
        const double upper = std::max(raw_of_slot[below], raw_of_slot[above]);
        if (level <= lower || level >= upper)
        {
            continue; // not between its neighbours: not a clean dwell, leave the hole
        }
        CalibrationPoint point;
        point.rawAvg = level;
        point.trueDb = steps[slot].db;
        profile.points.push_back(point);
        paired[slot]      = true;
        raw_of_slot[slot] = level;
    }
}

} // namespace

namespace stepsaturated {

/// Saturated top: calibrates the steps that stayed in range.
/// 
/// A receiver whose gain is set too high for the injected levels runs off the
/// end of its converter partway through the sweep: every dwell from there on
/// reads the same full-scale count, so the recording physically cannot hold
/// `expected` distinct levels and no threshold will find them. Those railed
/// dwells are not measurements - the receiver had no headroom left - so they
/// are dropped rather than paired, and the profile covers the steps below.
/// interpolateCalibration() already clamps above its top point, so a raw value
/// that reaches the rail in the main run reads the top measured step's dB
/// rather than a number invented by extrapolating past the data.
/// 
/// Pairing the survivors needs to know WHICH steps they are, and position
/// alone cannot say once a dwell is missing from the middle (noise can shatter
/// one into runs too short to confirm, which is what L_RCVR1 does on
/// the step_cal recording). The generator holds every step for the same length of
/// time, so the TRANSITIONS between them fall on a regular grid (buildDwellGrid)
/// - each level's step index is the number of whole dwells between the run's
/// first level and the dwell its first sample sits in. The grid comes from the
/// transitions rather than the plateau spans because a plateau's end is not
/// reliably on it (noise can shatter a dwell's tail), while a plateau's begin is
/// always inside its own dwell. A recording whose dwells are not regular fails
/// the check and keeps the linear fallback rather than guessing.
bool calibrateSaturatedTop(const std::vector<double>& rawValues,
                           const std::vector<double>& deriv,
                           const std::vector<Level>& levels,
                           const std::vector<StepDefinition>& steps,
                           double nominal_threshold,
                           int confirm_samples,
                           StepDetector::Result& result)
{
    const int expected = intSize(steps);
    if (intSize(levels) < 2)
    {
        return false;
    }
    const std::vector<double> values = valuesOf(levels);

    // Longest monotonic run, by the same rule the full-sweep selection uses
    // but of any length - here the run is short BECAUSE the top railed.
    int run_begin = 0;
    int run_end   = 0;
    forEachMonotonicRun(values, nominal_threshold, [&](const Run& run) {
        if (run.end - run.begin > run_end - run_begin)
        {
            run_begin = run.begin;
            run_end   = run.end;
        }
        return RunWalk::Continue;
    });
    if (run_end <= run_begin)
    {
        return false;
    }

    // Drop the railed dwells at the TOP of the run only: a pinned level at its
    // start is the pre-sweep level, which is a real measurement of a real step.
    int last   = run_end;
    int railed = 0;
    while (last > run_begin && isRailedLevel(values[last]))
    {
        last--;
        railed++;
    }
    if (railed == 0)
    {
        return false; // a short run with nothing railed is a different failure
    }
    const int measured = last - run_begin + 1;
    if (measured < std::max(CalibrationConstants::kMinPartialProfileSteps, expected / 2))
    {
        return false; // too little of the sweep survived to be worth a profile
    }

    // The dwell grid comes from the TRANSITIONS, not from the plateau spans.
    // A plateau's end is not reliably on the grid - noise shatters the tail of
    // a dwell into runs too short to confirm, which on the step_cal recording ends
    // R_RCVR1's 36 dB plateau a third of a dwell early. A plateau's BEGIN is
    // always inside its own dwell, though (the transition samples either side
    // are marked unstable), so counting whole dwells between transitions
    // places every level.
    const double major_edge =
        CalibrationConstants::kMajorEdgeFractionOfStep
        * medianAdjacentGap(std::vector<double>(values.begin() + run_begin,
                                                values.begin() + run_end + 1));
    DwellGrid grid;
    if (!buildDwellGrid(deriv, major_edge, grid))
    {
        return false;
    }
    // Each level belongs to the dwell its first sample sits in. The run's
    // first level is taken to be step 0: unlike the full-sweep path there is
    // no spare level to drop as a turn-on transient, because the whole reason
    // this path runs is that levels are MISSING.
    //
    // The grid counts dwells from the first transition in the RECORDING, which
    // need not be the run's: a turn-on transient above the 0 dB level, before
    // the sweep reverses into it, adds a transition of its own. So every slot is
    // taken relative to the run's first level - otherwise that transient shifts
    // the whole numbering up one step (5000, 1000, 2000, 3000, rail read 1000 as
    // 6 dB and left 0 dB unresolved).
    auto slotOf = [&](int begin_sample) {
        int slot = 0;
        for (int i = 0; i < intSize(grid.boundaries); i++)
        {
            if (begin_sample > grid.boundaries[i])
            {
                slot = grid.slot[i] + 1;
            }
        }
        return slot;
    };
    const int run_first_slot = slotOf(levels[run_begin].begin);
    for (int& slot : grid.slot)
    {
        slot -= run_first_slot;
    }
    std::vector<int> step_slots;
    for (int k = run_begin; k <= last; k++)
    {
        const int slot = slotOf(levels[k].begin);
        if (slot < 0 || slot >= expected
            || (!step_slots.empty() && slot <= step_slots.back()))
        {
            return false;
        }
        step_slots.push_back(slot);
    }

    CalibrationProfile& profile = result.profile;
    profile.points.clear();
    profile.points.reserve(measured);
    Flags   paired(expected, false);
    std::vector<double> raw_of_slot(expected, 0.0);
    for (int i = 0; i < measured; i++)
    {
        CalibrationPoint point;
        point.rawAvg = values[run_begin + i];
        point.trueDb = steps[step_slots[i]].db;
        profile.points.push_back(point);
        paired[step_slots[i]]      = true;
        raw_of_slot[step_slots[i]] = point.rawAvg;
    }

    // Dwells the grid can see but plateau detection missed are measured here,
    // not left as holes for interpolation to bridge.
    measureUnconfirmedDwells(rawValues, steps, grid, step_slots.back(), confirm_samples,
                             paired, raw_of_slot, profile);
    sortByRawAvg(profile.points);
    for (int s = 0; s < expected; s++)
    {
        if (!paired[s])
        {
            result.unresolvedDb.push_back(steps[s].db);
        }
    }
    profile.valid      = true;
    result.saturated   = true;
    result.sweepLevels = run_end - run_begin + 1;
    result.outcome   = CalibrationOutcome::Calibrated;
    return true;
}

} // namespace stepsaturated
