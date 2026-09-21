/**
 * @file stepdetector.cpp
 * @brief Implementation of StepDetector — edge-based plateau detection and
 *        step-config TOML parsing for non-linear calibration (US5.3).
 */

#include "stepdetector.h"

#include <algorithm>
#include <cmath>

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "constants.h"

namespace {

/// Strips an inline `#` comment (outside quotes) and surrounding whitespace.
QString stripComment(QString value)
{
    const int hash = value.indexOf('#');
    if (hash >= 0)
    {
        value = value.left(hash);
    }
    return value.trimmed();
}

/// Median of a copy of @p values (returns 0 for an empty input).
double median(QVector<double> values)
{
    if (values.isEmpty())
    {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int mid = values.size() / 2;
    if (values.size() % 2 == 0)
    {
        return 0.5 * (values[mid - 1] + values[mid]);
    }
    return values[mid];
}

/// Median magnitude of the change between adjacent values (0 for fewer than two).
double medianAdjacentGap(const QVector<double>& values)
{
    QVector<double> gaps;
    for (int i = 1; i < values.size(); i++)
    {
        gaps.push_back(std::fabs(values[i] - values[i - 1]));
    }
    return median(gaps);
}

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
    QVector<int> boundaries;  ///< Derivative indices where a transition happened.
    QVector<int> slot;        ///< Step index each boundary lands on.
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
bool buildDwellGrid(const QVector<double>& deriv, double majorEdge, DwellGrid& out)
{
    out = DwellGrid{};

    for (int i = 0; i < deriv.size(); i++)
    {
        if (std::fabs(deriv[i]) < majorEdge)
        {
            continue;
        }
        // One transition can smear across the window that straddles it, so
        // marks a sample or two apart are the same boundary.
        if (out.boundaries.isEmpty() || i - out.boundaries.last() > 2)
        {
            out.boundaries.push_back(i);
        }
    }
    if (out.boundaries.size() < 2)
    {
        return false;
    }
    QVector<double> spacings;
    for (int i = 1; i < out.boundaries.size(); i++)
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
    out.slot.assign(out.boundaries.size(), 0);
    for (int i = 1; i < out.boundaries.size(); i++)
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
void measureUnconfirmedDwells(const QVector<double>& rawValues,
                             const QVector<StepDefinition>& steps,
                             const DwellGrid& grid,
                             int top_slot,
                             int confirm_samples,
                             QVector<bool>& paired,
                             QVector<double>& raw_of_slot,
                             CalibrationProfile& profile)
{
    const QVector<int>& boundaries    = grid.boundaries;
    const QVector<int>& boundary_slot = grid.slot;
    const int expected = steps.size();
for (int i = 0; i + 1 < boundaries.size(); i++)
{
    if (boundary_slot[i + 1] - boundary_slot[i] != 1)
    {
        continue;
    }
    const int slot = boundary_slot[i] + 1;
    if (slot >= top_slot || paired[slot])
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
    QVector<double> tail;
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

/// Sample-to-sample difference of @p raw (one shorter than its input).
QVector<double> sampleDerivative(const QVector<double>& raw)
{
    QVector<double> deriv;
    deriv.reserve(raw.size() - 1);
    for (int i = 1; i < raw.size(); i++)
    {
        deriv.push_back(raw[i] - raw[i - 1]);
    }
    return deriv;
}

/// Robust (MAD-based) standard-deviation estimate of @p values. Sizes the
/// derivative threshold adaptively per channel.
double robustStdDev(const QVector<double>& values)
{
    if (values.isEmpty())
    {
        return 0.0;
    }
    const double med = median(values);
    QVector<double> abs_dev;
    abs_dev.reserve(values.size());
    for (double v : values)
    {
        abs_dev.push_back(std::fabs(v - med));
    }
    // 1.4826 scales the median absolute deviation to a Gaussian std-dev estimate.
    return 1.4826 * median(abs_dev);
}

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
double liveNoiseSigma(const QVector<double>& raw, const QVector<double>& deriv)
{
    const auto [floor_it, ceiling_it] = std::minmax_element(raw.cbegin(), raw.cend());
    QVector<double> live;
    live.reserve(deriv.size());
    for (int i = 0; i < deriv.size(); i++)
    {
        const bool pinned = raw[i] == raw[i + 1]
                            && (raw[i] == *floor_it || raw[i] == *ceiling_it);
        if (!pinned)
        {
            live.push_back(deriv[i]);
        }
    }
    return robustStdDev(live.isEmpty() ? deriv : live);
}

/// Marks samples that are NOT part of a transition. A transition at derivative
/// index i sits between raw[i] and raw[i+1], so both endpoints are marked
/// unstable and plateaus exclude the moving region.
QVector<bool> markStableSamples(const QVector<double>& deriv, int sampleCount, double threshold)
{
    QVector<bool> stable(sampleCount, true);
    for (int i = 0; i < deriv.size(); i++)
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
QVector<Plateau> confirmedPlateaus(const QVector<bool>& stable, int sampleCount, int confirmSamples)
{
    QVector<Plateau> plateaus;
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
QVector<Level> levelsOf(const QVector<double>& raw,
                        const QVector<Plateau>& plateaus,
                        int confirmSamples)
{
    QVector<Level> levels;
    levels.reserve(plateaus.size());
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
QVector<Level> coalesceLevels(const QVector<Level>& in, double tolerance)
{
    QVector<Level> out;
    for (const Level& level : in)
    {
        if (!out.isEmpty() && std::fabs(level.value - out.last().value) <= tolerance)
        {
            out.last().value = level.value;
            out.last().end   = level.end;
        }
        else
        {
            out.push_back(level);
        }
    }
    return out;
}

QVector<double> valuesOf(const QVector<Level>& levels)
{
    QVector<double> values;
    values.reserve(levels.size());
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

/// Selects one clean calibration sweep: the first run of `expected`
/// CONSECUTIVE plateaus whose settled averages move strictly in one
/// direction (a real receiver response is monotonic — increasing or
/// decreasing dB with raw count). A genuine step transition was already
/// large enough to split the plateaus (> `threshold`), so we require each
/// consecutive level change to exceed that same threshold and keep the
/// same sign across the whole run.
/// 
/// This is robust to the messy reality of recorded cal files (US5.3): it
/// skips leading pre-roll/no-signal plateaus, ignores trailing post-roll,
/// and takes the FIRST of several back-to-back repeats. Crucially it also
/// rejects flat/inactive channels — their plateau averages only jitter
/// around the noise floor, never forming a monotonic run — instead of the
/// old "first N plateaus" rule, which paired no-signal noise with the step
/// dB values and produced degenerate (all-equal rawAvg) profiles.
int selectSweepStart(const QVector<double>& plateauAvg, int expected,
                     double threshold, int& runLevels)
{
    runLevels = 0;
    // Find the FIRST maximal run of plateaus whose settled averages move strictly
// in one direction, long enough to hold the whole sweep, and take its LAST
// `expected` plateaus.
//
// A real cal recording is, in time order:
//   [signal-generator turn-on transient] [the sweep: step 0 .. step N-1]
//   [optional operator return ramp back down]
// The sweep climbs monotonically to its peak step and then reverses (the ramp)
// or the recording ends. The turn-on transient sits on the low-signal side
// just ahead of the sweep and moves in the SAME direction into step 0, so it
// extends the first monotonic run by one (or more) plateau at its START. Taking
// the run's LAST `expected` plateaus drops that leading transient while keeping
// the genuine sweep, and pairs them with the steps in time order below.
//
// This is direction-agnostic, which matters because receiver polarity sets the
// raw-count direction: a normal receiver's raw count rises with signal, an
// inverted / negative-slope receiver's falls. Using the run's own direction
// (rather than anchoring on a max/min raw extreme) avoids locking onto the
// return ramp, which on a full pyramid/valley recording forms an equally valid
// monotonic run in the opposite half.
int run_start = -1;
for (int begin = 0; begin + 1 < plateauAvg.size() && run_start < 0; )
{
    const int dir = transitionDirection(plateauAvg[begin + 1] - plateauAvg[begin], threshold);
    if (dir == 0)
    {
        begin++; // sub-threshold step: not a real transition, skip
        continue;
    }
    int end = begin + 1;
    while (end + 1 < plateauAvg.size() &&
           transitionDirection(plateauAvg[end + 1] - plateauAvg[end], threshold) == dir)
    {
        end++;
    }
    // Maximal monotonic run is plateaus[begin..end].
    if (end - begin + 1 >= expected)
    {
        run_start = end - (expected - 1); // keep the last `expected` of the run
        runLevels = end - begin + 1;
    }
    else
    {
        begin = end; // too short; the reversal starts the next run here
    }
}
return run_start;
}

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
/// time, so plateau END times fall on a regular grid - each level's distance
/// from the first, in whole dwells, is its step index. Ends rather than starts,
/// because the pre-sweep level runs for however long the operator took to begin
/// and only its end lands on the grid. A recording whose dwells are not regular
/// fails the check and keeps the linear fallback rather than guessing.
bool calibrateSaturatedTop(const QVector<double>& rawValues,
                           const QVector<double>& deriv,
                           const QVector<Level>& levels,
                           const QVector<StepDefinition>& steps,
                           double nominal_threshold,
                           int confirm_samples,
                           StepDetector::Result& result)
{
    const int expected = steps.size();
    if (levels.size() < 2)
    {
        return false;
    }
    const QVector<double> values = valuesOf(levels);

    // Longest monotonic run, by the same rule the full-sweep selection uses
    // but of any length - here the run is short BECAUSE the top railed.
    int run_begin = 0;
    int run_end   = 0;
    for (int begin = 0; begin + 1 < values.size(); )
    {
        const int dir = transitionDirection(values[begin + 1] - values[begin], nominal_threshold);
        if (dir == 0)
        {
            begin++;
            continue;
        }
        int end = begin + 1;
        while (end + 1 < values.size()
               && transitionDirection(values[end + 1] - values[end], nominal_threshold) == dir)
        {
            end++;
        }
        if (end - begin > run_end - run_begin)
        {
            run_begin = begin;
            run_end   = end;
        }
        begin = end;
    }
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
        * medianAdjacentGap(values.mid(run_begin, run_end - run_begin + 1));
    DwellGrid grid;
    if (!buildDwellGrid(deriv, major_edge, grid))
    {
        return false;
    }
    const QVector<int>& boundaries    = grid.boundaries;
    const QVector<int>& boundary_slot = grid.slot;
    // Each level belongs to the dwell its first sample sits in. The run's
    // first level is taken to be step 0: unlike the full-sweep path there is
    // no spare level to drop as a turn-on transient, because the whole reason
    // this path runs is that levels are MISSING.
    auto slotOf = [&](int begin_sample) {
        int slot = 0;
        for (int i = 0; i < boundaries.size(); i++)
        {
            if (begin_sample > boundaries[i])
            {
                slot = boundary_slot[i] + 1;
            }
        }
        return slot;
    };
    QVector<int> step_slots;
    for (int k = run_begin; k <= last; k++)
    {
        const int slot = slotOf(levels[k].begin);
        if (slot < 0 || slot >= expected
            || (!step_slots.isEmpty() && slot <= step_slots.last()))
        {
            return false;
        }
        step_slots.push_back(slot);
    }

    CalibrationProfile& profile = result.profile;
    profile.points.clear();
    profile.points.reserve(measured);
    QVector<bool>   paired(expected, false);
    QVector<double> raw_of_slot(expected, 0.0);
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
    measureUnconfirmedDwells(rawValues, steps, grid, step_slots.last(), confirm_samples,
                             paired, raw_of_slot, profile);
    std::sort(profile.points.begin(), profile.points.end(),
              [](const CalibrationPoint& a, const CalibrationPoint& b) {
                  return a.rawAvg < b.rawAvg;
              });
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

} // namespace

// ---------------------------------------------------------------------------
// parseStepConfig
// ---------------------------------------------------------------------------

bool StepDetector::parseStepConfig(const QString& path,
                                   QVector<StepDefinition>& out,
                                   QString& error)
{
    out.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        error = "Could not open step-config file '" + QFileInfo(path).fileName() + "'.";
        return false;
    }

    QTextStream in(&file);
    bool in_step = false;
    StepDefinition current;

    auto flush = [&]() {
        if (in_step)
        {
            out.push_back(current);
        }
        current = StepDefinition{};
    };

    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
        {
            continue;
        }

        // Array-of-tables header: [[Step]] starts a new step entry.
        if (line.startsWith("[["))
        {
            flush();
            in_step = line.compare("[[Step]]", Qt::CaseInsensitive) == 0;
            continue;
        }
        // Any other section header ends the current step context.
        if (line.startsWith('['))
        {
            flush();
            in_step = false;
            continue;
        }
        if (!in_step)
        {
            continue;
        }

        const int eq = line.indexOf('=');
        if (eq < 0)
        {
            continue;
        }
        const QString key = line.left(eq).trimmed().toLower();
        const QString val = stripComment(line.mid(eq + 1));

        bool ok = false;
        const double num = val.toDouble(&ok);
        if (!ok)
        {
            continue;
        }
        if (key == "db")
        {
            current.db = num;
        }
        // Each step is pushed on the next [[Step]] header or at EOF by flush().
    }
    flush();

    if (out.isEmpty())
    {
        error = "No [[Step]] entries found in '" + QFileInfo(path).fileName() + "'.";
        return false;
    }
    return true;
}

QString StepDetector::initialStepConfigPath(const QString& appRoot, const QString& preferred)
{
    // A reference that no longer resolves - a template written on another machine,
    // a moved settings tree - falls through to the default rather than opening
    // the dialog on a file that is not there.
    if (!preferred.isEmpty() && QFileInfo(preferred).isFile())
    {
        return preferred;
    }
    if (appRoot.isEmpty())
    {
        return QString();
    }
    const QString fallback = appRoot + "/" + UIConstants::kSettingsDirName + "/"
                             + UIConstants::kRcvrCalsDirName + "/"
                             + UIConstants::kDefaultTomlFilename;
    return QFileInfo(fallback).isFile() ? fallback : QString();
}

// ---------------------------------------------------------------------------
// detect
// ---------------------------------------------------------------------------

StepDetector::Result StepDetector::detect(const QVector<double>& rawValues,
                                          double samplePeriodSec,
                                          const QVector<StepDefinition>& steps)
{
    Result result;

    const int expected = steps.size();
    if (expected == 0 || rawValues.size() < 2 || samplePeriodSec <= 0.0)
    {
        return result; // invalid by default
    }

    const QVector<double> deriv = sampleDerivative(rawValues);

    // The edge threshold comes from the robust spread of the derivative: samples
    // inside a plateau dominate, so their noise sets the baseline and real
    // transitions are large outliers above it.
    const double sigma = liveNoiseSigma(rawValues, deriv);
    const double nominal_threshold = std::max(
        CalibrationConstants::kEdgeSigmaMultiple * sigma,
        CalibrationConstants::kMinEdgeRawCounts);
    result.edgeThreshold = nominal_threshold;

    // The confirmation window is a fixed duration (not a fraction of a configured
    // dwell, since steps no longer carry one) with an absolute sample-count floor
    // for coarse sample periods where the duration alone would be one or two
    // samples.
    const int confirm_samples = std::max(
        CalibrationConstants::kMinConfirmSamples,
        static_cast<int>(std::ceil(CalibrationConstants::kStepConfirmSeconds / samplePeriodSec)));

    auto plateausAt = [&](double threshold) {
        return confirmedPlateaus(markStableSamples(deriv, rawValues.size(), threshold),
                                 rawValues.size(), confirm_samples);
    };

    // Search the edge threshold, using the expected step count as the target
    // rather than only as a pass/fail test.
    //
    // A receiver far out of tolerance does not move a uniform number of raw
    // counts per dB: one sweep can hold a 12-count step at the compressed end
    // and a 1100-count step at the expansive end. A single threshold sized by
    // the whole series' noise cannot resolve both — the small steps never
    // clear it, so adjacent levels merge and the channel loses its profile
    // even though the sweep is plainly there.
    //
    // So try progressively lower thresholds and accept the FIRST (largest,
    // most conservative) one that yields a clean sweep of `expected` levels.
    // Relaxing is safe because the two real noise discriminators are unchanged
    // and neither depends on the threshold: a candidate level must still hold
    // steady for the confirmation window, and the run must still move strictly
    // in one direction. A flat/no-signal channel's levels only jitter, so they
    // never form such a run no matter how far the threshold drops — which is
    // what stops this from resurrecting the degenerate "noise paired with step
    // dB values" profiles the monotonic-run rule was introduced to kill.
    const double floor_threshold = std::max(
        CalibrationConstants::kMinEdgeSigmaMultiple * sigma,
        CalibrationConstants::kMinEdgeRawCounts);

    QVector<Plateau> plateaus;
    QVector<double> plateauAvg;
    int selected_run_levels = 0; ///< Length of the run selectSweepStart accepted.
    QVector<Level>  nominal_levels; ///< Levels the nominal pass saw, for the saturated-top fallback.
    double accepted_threshold = nominal_threshold;
    int run_start = -1;
    bool reached_sweep_stage = false;
    int best_levels = 0; ///< Most distinct levels any attempt resolved.

    for (int attempt = 0; attempt < CalibrationConstants::kMaxEdgeRelaxAttempts; attempt++)
    {
        const double threshold =
            nominal_threshold * std::pow(CalibrationConstants::kEdgeRelaxFactor, attempt);
        if (attempt > 0 && threshold < floor_threshold)
        {
            break; // below the noise floor every sample reads as an edge
        }

        const QVector<Plateau> candidates = plateausAt(threshold);
        if (attempt == 0)
        {
            // Report the nominal pass's count, so the diagnostic describes the
            // channel as detection first saw it rather than as the last retry did.
            result.detectedPlateaus = candidates.size();
        }

        // Coalesce adjacent plateaus that sit at the SAME physical level. Noise
        // routinely splits one dwell into two stable runs whose settled averages
        // differ by a count or two — on a real recording, levels like
        //   ... 34752, 49892, 49895, 57310, 57309, 65472 ...
        // are six plateaus but only four steps. Left alone, that ~1-count wobble
        // reads as "not a real transition" and severs the monotonic run in the
        // middle of an otherwise perfect sweep, so a plainly good calibration is
        // discarded. Merging on the same threshold that defines a real edge keeps
        // genuine steps apart while healing the splits.
        //
        // It also sharpens the dead-channel rejection rather than weakening it: a
        // flat channel's plateaus all collapse into ONE level, which can never
        // form a run of `expected`.
        QVector<Level> levels =
            coalesceLevels(levelsOf(rawValues, candidates, confirm_samples), threshold);

        // ...and again against the STEP size, not only the noise. Noise grows with
        // signal on a real receiver (~3 counts on its low steps, ~60 on its high
        // ones), so a split high dwell can sit further apart than any threshold the
        // quiet steps support: the step_cal recording's L_RCVR3 split its 42 dB dwell
        // into 22797 / 22818 - 22 counts apart against a 17-count threshold, on
        // steps ~3800 counts tall. Two levels a small fraction of a typical step
        // apart are one step, whatever the noise estimate says.
        levels = coalesceLevels(levels,
                          std::max(threshold, CalibrationConstants::kSameLevelFractionOfStep
                                                  * medianAdjacentGap(valuesOf(levels))));
        if (attempt == 0)
        {
            // Recorded before the count check below, so a channel that bails out
            // early still reports what it saw — that is exactly the case where
            // the levels are the only thing that explains the verdict (one level
            // at 0 is a dead input; one level at full scale is a railed one).
            result.plateauLevels = valuesOf(levels);
            nominal_levels       = levels;
        }
        best_levels = std::max(best_levels, static_cast<int>(levels.size()));
        if (levels.size() < expected)
        {
            continue; // steps still merged; loosen and look again
        }
        reached_sweep_stage = true;

        const QVector<double> averages = valuesOf(levels);
        const int start =
            selectSweepStart(averages, expected, threshold, selected_run_levels);
        if (start >= 0)
        {
            plateaus           = candidates;
            plateauAvg         = averages;
            accepted_threshold = threshold;
            run_start          = start;
            break;
        }
    }

    result.edgeThreshold    = accepted_threshold;
    result.thresholdRelaxed = (run_start >= 0) && (accepted_threshold < nominal_threshold);

    if (run_start < 0)
    {
        if (calibrateSaturatedTop(rawValues, deriv, nominal_levels, steps,
                                  nominal_threshold, confirm_samples, result))
        {
            return result; // partial profile over the steps that stayed in range
        }
        // Report the furthest gate reached. A channel that never resolved more
        // than one level did not "merge its steps" — it never moved at all, so
        // the sweep is absent from the recording rather than beyond the
        // detector's reach, and the two must not read the same.
        result.outcome = (best_levels <= 1)      ? CalibrationOutcome::FlatNoSignal
                         : reached_sweep_stage   ? CalibrationOutcome::NoMonotonicSweep
                                                 : CalibrationOutcome::TooFewPlateaus;
        return result; // linear fallback
    }
    result.extraPlateaus = plateaus.size() > expected;
    result.sweepLevels   = selected_run_levels;

    // Pair the selected sweep with the steps in time order (Nth plateau of the
    // sweep <-> Nth step), then sort by raw so interpolateCalibration() can
    // assume ascending rawAvg.
    CalibrationProfile& profile = result.profile;
    profile.points.reserve(expected);
    for (int k = 0; k < expected; k++)
    {
        CalibrationPoint pt;
        pt.rawAvg = plateauAvg[run_start + k];
        pt.trueDb = steps[k].db;
        profile.points.push_back(pt);
    }
    std::sort(profile.points.begin(), profile.points.end(),
              [](const CalibrationPoint& a, const CalibrationPoint& b) {
                  return a.rawAvg < b.rawAvg;
              });

    profile.valid  = true;
    result.outcome = CalibrationOutcome::Calibrated;
    return result;
}

