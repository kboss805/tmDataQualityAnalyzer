/**
 * @file stepplateaus.h
 * @brief The plateau primitives the step detector (US5.3) is built from: the derivative and its noise, the stable
 *        samples, the confirmed plateaus, the levels they settle at, and the walk over a run of levels that moves one way.
 *
 * `StepDetector::detect` and its saturated-top fallback (`stepsaturated.h`) both work on these, so they are here and not
 * in either. Each is a pure function of its arguments; the native tests (test_stepplateaus.cpp, and test_stepdetector.cpp
 * through `detect`) pin them.
 */

#ifndef STEPPLATEAUS_H
#define STEPPLATEAUS_H

#include <vector>

#include "calibrationprofile.h"

namespace stepplateaus {

/// One flag per sample. Deliberately not `std::vector<bool>`: that specialisation is a
/// bit-packed proxy container (no real references, no data()), which is a trap here
/// rather than a saving.
using Flags = std::vector<char>;

/// One confirmed settled run of samples: [begin, end).
struct Plateau
{
    int begin;
    int end;
};

/// A coalesced level: the settled value plus the sample span it was measured
/// over. The span is what lets the saturated-top fallback tell a plateau
/// covering two dwells from two plateaus covering one each.
struct Level
{
    double value;
    int    begin;
    int    end;
};

/// Median of a copy of @p values (returns 0 for an empty input).
double median(std::vector<double> values);

/// Median magnitude of the change between adjacent values (0 for fewer than two).
double medianAdjacentGap(const std::vector<double>& values);

/// Sample-to-sample difference of @p raw (one shorter than its input).
std::vector<double> sampleDerivative(const std::vector<double>& raw);

/// Robust spread of the derivative, measured over the LIVE samples only (samples pinned at the series' floor or ceiling
/// are left out, so a parked or railed receiver does not drag the noise estimate under its live steps' jitter).
double liveNoiseSigma(const std::vector<double>& raw, const std::vector<double>& deriv);

/// Marks samples that are NOT part of a transition: a transition at derivative index i sits between raw[i] and
/// raw[i+1], so both endpoints are marked unstable and plateaus exclude the moving region.
Flags markStableSamples(const std::vector<double>& deriv, int sampleCount, double threshold);

/// Maximal runs of stable samples that hold for at least @p confirmSamples, so a transient or partial-jump blip is not
/// mistaken for a settled step.
std::vector<Plateau> confirmedPlateaus(const Flags& stable, int sampleCount, int confirmSamples);

/// Each plateau's settled value (the average of the confirmation window at its END) and span.
std::vector<Level> levelsOf(const std::vector<double>& raw, const std::vector<Plateau>& plateaus, int confirmSamples);

/// Merges adjacent levels within @p tolerance into one, keeping the later (more settled) value and extending the span
/// across both halves.
std::vector<Level> coalesceLevels(const std::vector<Level>& in, double tolerance);

/// The values of @p levels, in order.
std::vector<double> valuesOf(const std::vector<Level>& levels);

/// 1 for a change above @p threshold, -1 for one below minus it, 0 for one too small to be a real transition.
int transitionDirection(double delta, double threshold);

/// A maximal run of values that moves strictly in one direction: indices [begin, end], at least two values.
struct Run
{
    int begin;
    int end;
};

/// What a visitor says after a run: go on to the next one, or stop the walk.
enum class RunWalk
{
    Continue,
    Stop,
};

/**
 * @brief Calls @p visit with every maximal run of @p values whose consecutive changes each exceed @p threshold in the same
 *        direction, in order, until it returns `RunWalk::Stop`.
 *
 * A change within the threshold is not a transition and starts no run; a run ends where the direction reverses or a change
 * falls within the threshold, and the next run is looked for from that last value (a reversal is the start of the next
 * run). This is the walk both the full-sweep selection (the FIRST run long enough) and the saturated-top fallback (the
 * LONGEST run) make, and the one rule of what counts as one run.
 */
template <class Visit>
void forEachMonotonicRun(const std::vector<double>& values, double threshold, Visit&& visit)
{
    const int count = static_cast<int>(values.size());
    for (int begin = 0; begin + 1 < count; )
    {
        const int direction = transitionDirection(values[begin + 1] - values[begin], threshold);
        if (direction == 0)
        {
            begin++; // sub-threshold: not a real transition, skip
            continue;
        }
        int end = begin + 1;
        while (end + 1 < count && transitionDirection(values[end + 1] - values[end], threshold) == direction)
        {
            end++;
        }
        if (visit(Run{begin, end}) == RunWalk::Stop)
        {
            return;
        }
        begin = end;
    }
}

/// Sorts @p points by ascending raw average, which `interpolateCalibration()` assumes.
void sortByRawAvg(std::vector<CalibrationPoint>& points);

} // namespace stepplateaus

#endif // STEPPLATEAUS_H
