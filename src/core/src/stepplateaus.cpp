/**
 * @file stepplateaus.cpp
 * @brief Implementation of the plateau primitives the step detector is built from.
 */

#include "stepplateaus.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "constants.h"
#include "containerutil.h"

namespace stepplateaus {

/// Median of a copy of @p values (returns 0 for an empty input).
double median(std::vector<double> values)
{
    if (values.empty())
    {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int mid = intSize(values) / 2;
    if (intSize(values) % 2 == 0)
    {
        return 0.5 * (values[mid - 1] + values[mid]);
    }
    return values[mid];
}

/// Median magnitude of the change between adjacent values (0 for fewer than two).
double medianAdjacentGap(const std::vector<double>& values)
{
    std::vector<double> gaps;
    for (int i = 1; i < intSize(values); i++)
    {
        gaps.push_back(std::fabs(values[i] - values[i - 1]));
    }
    return median(gaps);
}

/// Sample-to-sample difference of @p raw (one shorter than its input).
std::vector<double> sampleDerivative(const std::vector<double>& raw)
{
    std::vector<double> deriv;
    // reserve() takes an unsigned count: for an empty series the "one fewer" is -1, which
    // a std::vector reads as 18 quintillion elements, so it is clamped at zero.
    deriv.reserve(static_cast<std::size_t>(std::max(0, intSize(raw) - 1)));
    for (int i = 1; i < intSize(raw); i++)
    {
        deriv.push_back(raw[i] - raw[i - 1]);
    }
    return deriv;
}

namespace {

/// Robust (MAD-based) standard-deviation estimate of @p values. Sizes the
/// derivative threshold adaptively per channel.
double robustStdDev(const std::vector<double>& values)
{
    if (values.empty())
    {
        return 0.0;
    }
    const double med = median(values);
    std::vector<double> abs_dev;
    abs_dev.reserve(intSize(values));
    for (double v : values)
    {
        abs_dev.push_back(std::fabs(v - med));
    }
    // 1.4826 scales the median absolute deviation to a Gaussian std-dev estimate.
    return 1.4826 * median(abs_dev);
}

} // namespace

/// Robust spread of the derivative, measured over the LIVE samples only.
///
/// Samples pinned at the series' floor or ceiling are left out. A receiver
/// parked at the converter's zero, or driven onto its rail, repeats one value
/// exactly: a run of perfect-zero derivatives that says nothing about the noise
/// on the live steps. On a real cal recording (pre-sweep at 0, top steps railed
/// at 65472, post-sweep back at 0) that is a third of the series, and it drags
/// the estimate far under the live steps' own jitter. An undersized threshold
/// then either splits a noisy dwell into two plateaus a few tens of counts apart
/// - consuming a pairing slot and shifting every step below it by one - or
/// shatters a dwell into runs too short to confirm, losing the step altogether.
///
/// A wholly pinned series (a dead or railed channel) has no live samples; it
/// falls back to the full derivative so it still reads as noiseless and flat.
double liveNoiseSigma(const std::vector<double>& raw, const std::vector<double>& deriv)
{
    const auto [floor_it, ceiling_it] = std::minmax_element(raw.cbegin(), raw.cend());
    std::vector<double> live;
    live.reserve(intSize(deriv));
    for (int i = 0; i < intSize(deriv); i++)
    {
        const bool pinned = raw[i] == raw[i + 1]
                            && (raw[i] == *floor_it || raw[i] == *ceiling_it);
        if (!pinned)
        {
            live.push_back(deriv[i]);
        }
    }
    return robustStdDev(live.empty() ? deriv : live);
}

/// Marks samples that are NOT part of a transition. A transition at derivative
/// index i sits between raw[i] and raw[i+1], so both endpoints are marked
/// unstable and plateaus exclude the moving region.
Flags markStableSamples(const std::vector<double>& deriv, int sampleCount, double threshold)
{
    Flags stable(sampleCount, true);
    for (int i = 0; i < intSize(deriv); i++)
    {
        if (std::fabs(deriv[i]) > threshold)
        {
            stable[i]     = false;
            stable[i + 1] = false;
        }
    }
    return stable;
}

/// Maximal runs of stable samples that hold for at least @p confirmSamples, so a
/// transient or partial-jump blip is not mistaken for a settled step.
std::vector<Plateau> confirmedPlateaus(const Flags& stable, int sampleCount, int confirmSamples)
{
    std::vector<Plateau> plateaus;
    int run_begin = -1;
    for (int i = 0; i <= sampleCount; i++)
    {
        const bool is_stable = (i < sampleCount) && stable[i];
        if (is_stable && run_begin < 0)
        {
            run_begin = i;
        }
        else if (!is_stable && run_begin >= 0)
        {
            if (i - run_begin >= confirmSamples)
            {
                plateaus.push_back(Plateau{run_begin, i});
            }
            run_begin = -1;
        }
    }
    return plateaus;
}

/// Each plateau's settled value and span.
///
/// The value averages the confirmation window at the END of the plateau (the
/// samples immediately before the next transition) rather than its start: any
/// settling after a jump has had the rest of the plateau to die out by then, so
/// this avoids transition contamination without needing a separate trim
/// fraction.
std::vector<Level> levelsOf(const std::vector<double>& raw,
                        const std::vector<Plateau>& plateaus,
                        int confirmSamples)
{
    std::vector<Level> levels;
    levels.reserve(intSize(plateaus));
    for (const Plateau& p : plateaus)
    {
        const int lo = std::max(p.begin, p.end - confirmSamples);
        double sum = 0.0;
        for (int i = lo; i < p.end; i++)
        {
            sum += raw[i];
        }
        levels.push_back(Level{sum / (p.end - lo), p.begin, p.end});
    }
    return levels;
}

/// Merges adjacent levels within @p tolerance into one, keeping the later (more
/// settled) value and extending the span across both halves.
std::vector<Level> coalesceLevels(const std::vector<Level>& in, double tolerance)
{
    std::vector<Level> out;
    for (const Level& level : in)
    {
        if (!out.empty() && std::fabs(level.value - out.back().value) <= tolerance)
        {
            out.back().value = level.value;
            out.back().end   = level.end;
        }
        else
        {
            out.push_back(level);
        }
    }
    return out;
}

std::vector<double> valuesOf(const std::vector<Level>& levels)
{
    std::vector<double> values;
    values.reserve(intSize(levels));
    for (const Level& level : levels)
    {
        values.push_back(level.value);
    }
    return values;
}

/// A level change smaller than the edge threshold is not a real transition.
int transitionDirection(double delta, double threshold)
{
    if (delta > threshold) return 1;
    if (delta < -threshold) return -1;
    return 0;
}

void sortByRawAvg(std::vector<CalibrationPoint>& points)
{
    std::sort(points.begin(), points.end(),
              [](const CalibrationPoint& a, const CalibrationPoint& b) {
                  return a.rawAvg < b.rawAvg;
              });
}

} // namespace stepplateaus
