#include "tst_stepdetector.h"

#include <QTemporaryFile>
#include <QtTest>

#include "stepdetector.h"

namespace {

/// Appends @p count samples of @p level to @p series.
void appendRun(QVector<double>& series, double level, int count)
{
    for (int i = 0; i < count; i++)
    {
        series.push_back(level);
    }
}

/// Writes @p content to a temporary .toml file and returns its path. The file
/// is owned by @p file (kept alive by the caller).
QString writeTempToml(QTemporaryFile& file, const QString& content)
{
    file.setFileTemplate(QDir::tempPath() + "/stepcfg_XXXXXX.toml");
    if (!file.open())
        qWarning("writeTempToml: could not open temporary TOML file");
    file.write(content.toUtf8());
    file.flush();
    return file.fileName();
}

const double kPeriod = 0.01; // 10 ms -> a 1.0s confirmation window is 100 samples.

} // namespace

// ---------------------------------------------------------------------------
// parseStepConfig
// ---------------------------------------------------------------------------

void TestStepDetector::parseStepConfigValid()
{
    QTemporaryFile f;
    const QString path = writeTempToml(f,
        "[[Step]]\n"
        "db = 0.0\n"
        "\n"
        "[[Step]]\n"
        "db = 6.0\n");

    QVector<StepDefinition> steps;
    QString error;
    QVERIFY(StepDetector::parseStepConfig(path, steps, error));
    QCOMPARE(steps.size(), 2);
    QCOMPARE(steps[0].db, 0.0);
    QCOMPARE(steps[1].db, 6.0);
}

void TestStepDetector::parseStepConfigEmptyFails()
{
    QTemporaryFile f;
    const QString path = writeTempToml(f, "# just a comment, no steps\n");

    QVector<StepDefinition> steps;
    QString error;
    QVERIFY(!StepDetector::parseStepConfig(path, steps, error));
}

// ---------------------------------------------------------------------------
// detect
// ---------------------------------------------------------------------------

void TestStepDetector::detectCleanSteps()
{
    QVector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    QVector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 3);
    QCOMPARE(r.profile.points.size(), 3);
    // Sorted ascending by raw; dB carried from the paired steps.
    QVERIFY(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0));
    QCOMPARE(r.profile.points[0].trueDb, 0.0);
    QVERIFY(qFuzzyCompare(r.profile.points[2].rawAvg, 3000.0));
    QCOMPARE(r.profile.points[2].trueDb, 12.0);
}

void TestStepDetector::detectTooFewPlateausFails()
{
    // Three steps expected, but the data only contains two plateaus.
    QVector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    QVector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(!r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 2);
}

void TestStepDetector::detectExtraPlateausUsesLastOfMonotonicRun()
{
    // Two steps expected, three increasing plateaus present. Selection takes the
    // LAST `expected` plateaus of the first maximal monotonic run (here the run
    // is all three: 1000 < 2000 < 3000), so (2000, 3000) pair with the steps and
    // the lowest plateau (1000) is dropped as leading pre-roll / turn-on transient.
    QVector<StepDefinition> steps = {{0.0}, {6.0}};

    QVector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QVERIFY(r.extraPlateaus);
    QCOMPARE(r.profile.points.size(), 2);
    QVERIFY(qFuzzyCompare(r.profile.points[0].rawAvg, 2000.0));
    QCOMPARE(r.profile.points[0].trueDb, 0.0);
    QVERIFY(qFuzzyCompare(r.profile.points[1].rawAvg, 3000.0));
    QCOMPARE(r.profile.points[1].trueDb, 6.0);
}

void TestStepDetector::detectShortBlipDoesNotStealPairingSlot()
{
    // Reproduces a real failure seen on decoded hardware data: a brief startup
    // transient (55 samples = 0.55s, under the 1.0s confirmation window) ahead
    // of the genuine sweep. Without a confirmation requirement, this blip would
    // be counted as a real plateau and consume pairing slot 0, shifting every
    // subsequent plateau<->step pairing by one. It must be rejected so the
    // three genuine (confirmed) plateaus pair correctly with steps 0/6/12 dB.
    QVector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    QVector<double> raw;
    appendRun(raw, 500.0, 55);   // spurious sub-confirmation blip
    appendRun(raw, 1000.0, 150); // genuine 0 dB plateau
    appendRun(raw, 2000.0, 150); // genuine 6 dB plateau
    appendRun(raw, 3000.0, 150); // genuine 12 dB plateau

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), 3);
    QVERIFY(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0));
    QCOMPARE(r.profile.points[0].trueDb, 0.0);
    QVERIFY(qFuzzyCompare(r.profile.points[1].rawAvg, 2000.0));
    QCOMPARE(r.profile.points[1].trueDb, 6.0);
    QVERIFY(qFuzzyCompare(r.profile.points[2].rawAvg, 3000.0));
    QCOMPARE(r.profile.points[2].trueDb, 12.0);
}

void TestStepDetector::detectLongLeadingTransientDoesNotShiftPairing()
{
    // Reproduces the RASA "time skew" field failure. The signal generator is
    // switched on a few seconds into the recording, producing a confirmed
    // leading plateau (here 1.5s, ABOVE the 1.0s confirmation window, so unlike
    // detectShortBlipDoesNotStealPairingSlot it is NOT rejected) below the real
    // 0 dB level. The injected sweep then climbs 0,3,6,12,18,24,30,36 and falls
    // back down (the operator's optional down-ramp).
    //
    // With a naive first-N-consecutive-increasing selection the leading transient
    // steals pairing slot 0: the real 0 dB plateau gets paired with the 3 dB step,
    // every subsequent pairing shifts up one, and the sweep stops one step short of
    // the 36 dB peak. Applied to the main run that off-by-one makes the channel
    // climb at the wrong rate — the staircase visibly drifts in time against
    // channels whose transient was too small to register. Selection instead takes
    // the LAST `expected` plateaus of the monotonic run, which drops the leading
    // transient (it extends the run's start) and keeps the pairing correct.
    QVector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0},
                                     {18.0}, {24.0}, {30.0}, {36.0}};

    // Raw counts: a leading turn-on transient, the 8 injected levels climbing to
    // the peak, then the optional down-ramp.
    const QVector<double> up = {1000.0, 1100.0, 1300.0, 1700.0,
                                2100.0, 2500.0, 2900.0, 3300.0};
    QVector<double> raw;
    appendRun(raw, 600.0, 150); // generator-off / turn-on transient (1.5s, confirmed)
    for (double level : up)
    {
        appendRun(raw, level, 150);
    }
    // Down-ramp (a separate, opposite-direction run; excluded).
    for (int i = up.size() - 2; i >= 0; i--)
    {
        appendRun(raw, up[i], 150);
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), 8);
    // Real 0 dB level (1000), NOT the 600 transient, must pair with step 0.
    QVERIFY2(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0),
             qPrintable(QString("first point rawAvg=%1, expected 1000 (leading "
                                "transient stole the slot)").arg(r.profile.points[0].rawAvg)));
    QCOMPARE(r.profile.points[0].trueDb, 0.0);
    // Peak (3300) must pair with the top step (36 dB), not be left off the sweep.
    QVERIFY(qFuzzyCompare(r.profile.points[7].rawAvg, 3300.0));
    QCOMPARE(r.profile.points[7].trueDb, 36.0);
}

void TestStepDetector::detectInvertedPolaritySweepNotReversed()
{
    // RASA receivers are inverted polarity: the raw count FALLS as the injected
    // signal climbs, so the peak-signal step (36 dB) is the MINIMUM raw plateau,
    // not the maximum. Anchoring naively on the maximum raw plateau would treat
    // the 0 dB end as the top step and pair the whole sweep backwards, flipping
    // the calibrated staircase (the field "0.48 dB at the peak" inversion). The
    // monotonic-run selection is direction-agnostic: it uses the run's own
    // direction (here decreasing), takes its last `expected` plateaus, and pairs
    // them with the steps in time order — so step 0 stays paired with the
    // highest-raw plateau and step 7 with the lowest, without locking onto a
    // raw max/min extreme (which would instead latch onto the return ramp).
    QVector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0},
                                     {18.0}, {24.0}, {30.0}, {36.0}};

    // Raw counts descend as dB ascends. Leading turn-on transient sits on the
    // low-signal (HIGH raw) side, ahead of the sweep, then a return ramp after.
    const QVector<double> down = {3300.0, 2900.0, 2500.0, 2100.0,
                                  1700.0, 1300.0, 1100.0, 1000.0};
    QVector<double> raw;
    appendRun(raw, 3500.0, 150); // turn-on transient (highest raw = no signal)
    for (double level : down)
    {
        appendRun(raw, level, 150);
    }
    // Return ramp back up in raw (signal falling away); a separate opposite-
    // direction run, excluded.
    for (int i = down.size() - 2; i >= 0; i--)
    {
        appendRun(raw, down[i], 150);
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), 8);
    // Points are sorted ascending by raw. Lowest raw (1000) is the 36 dB peak,
    // highest raw within the sweep (3300) is the 0 dB step — NOT reversed, and the
    // 3500 transient is excluded.
    QVERIFY2(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0),
             qPrintable(QString("lowest-raw point rawAvg=%1, expected 1000")
                            .arg(r.profile.points[0].rawAvg)));
    QCOMPARE(r.profile.points[0].trueDb, 36.0);
    QVERIFY(qFuzzyCompare(r.profile.points[7].rawAvg, 3300.0));
    QCOMPARE(r.profile.points[7].trueDb, 0.0);
    // The 3500 transient must not appear as a calibration point.
    for (const CalibrationPoint& p : r.profile.points)
    {
        QVERIFY(p.rawAvg < 3400.0);
    }
}

void TestStepDetector::detectNonMonotonicPairingRejected()
{
    // Chronological plateau order (2000, 1000, 3000) paired positionally with
    // steps (0, 6, 12) gives points (2000,0),(1000,6),(3000,12). Sorted
    // ascending by rawAvg that's (1000,6),(2000,0),(3000,12) -- dB goes
    // 6 -> 0 -> 12, which is neither non-decreasing nor non-increasing. This
    // mirrors a real failure mode: extra/missed plateaus shift the positional
    // step pairing, producing two near-adjacent raw values pinned to
    // contradictory dB values. The profile must be rejected (not handed to
    // interpolateCalibration with a near-vertical interpolation slope).
    QVector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    QVector<double> raw;
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 3000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(!r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 3); // plateaus were found; pairing was just rejected
}

void TestStepDetector::detectNoisyStepsStillDetected()
{
    QVector<StepDefinition> steps = {{0.0}, {6.0}};

    // Small in-plateau ripple (amplitude 2) << the 1000-count step transitions.
    QVector<double> raw;
    for (int i = 0; i < 150; i++) raw.push_back(1000.0 + (i % 3 == 0 ? 2.0 : 0.0));
    for (int i = 0; i < 150; i++) raw.push_back(2000.0 + (i % 3 == 0 ? 2.0 : 0.0));

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 2);
}

void TestStepDetector::detectSettlingAtPlateauStartExcluded()
{
    // The plateau's first samples ramp up gradually (settling, delta small
    // enough to stay "stable") before reaching the true core level. Averaging
    // the confirmation window from the END of the plateau (immediately before
    // the next transition) rather than its start must skip that settling
    // region entirely, so the average equals the clean core value.
    QVector<StepDefinition> steps = {{0.0}, {3.0}};

    QVector<double> raw;
    appendRun(raw, 0.0, 150); // genuine 0 dB baseline plateau
    // Gradual settling ramp at the start of the second plateau.
    for (int v = 991; v <= 1000; v++) raw.push_back(static_cast<double>(v));
    appendRun(raw, 1000.0, 190); // stable core

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), 2);
    // The second plateau's confirmation window (last 100 of its 200 samples)
    // falls entirely within the flat core, so the settling ramp is excluded.
    QVERIFY(qFuzzyCompare(r.profile.points[1].rawAvg, 1000.0));
}

void TestStepDetector::roundTripSameDataIsExact()
{
    // Round trip: calibrate from a clean stepped series, then "measure" the very
    // same raw levels back through the resulting profile. Because the data used
    // to build the profile is identical to the data fed through it, there is no
    // error between them — interpolating each step's own raw average must return
    // that step's exact dB (6, 12, 18, 24, 30). This is the zero-noise property
    // we get when the calibration file and the measured file are one and the same.
    QVector<StepDefinition> steps = {{6.0}, {12.0}, {18.0}, {24.0}, {30.0}};

    const QVector<double> levels = {1000.0, 2000.0, 3000.0, 4000.0, 5000.0};
    QVector<double> raw;
    for (double level : levels)
    {
        appendRun(raw, level, 150);
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 5);
    QCOMPARE(r.profile.points.size(), 5);

    // Each plateau's own raw average maps back to its paired step dB, exactly.
    for (int i = 0; i < levels.size(); i++)
    {
        const double db = interpolateCalibration(levels[i], r.profile);
        QVERIFY2(qFuzzyCompare(db, steps[i].db),
                 qPrintable(QString("raw %1 -> %2 dB, expected %3 dB")
                                .arg(levels[i]).arg(db).arg(steps[i].db)));
    }
}

void TestStepDetector::detectMultiChannelSweepStaysTimeAligned()
{
    // Regression for the "staircases drift in time across channels" report:
    // several receiver channels see the SAME injected dB sweep at the SAME times,
    // but with different gain, opposite polarity, or a per-channel turn-on
    // transient. Each channel is calibrated independently — yet every channel must
    // map the k-th sweep segment back to the k-th step's dB. If one channel's
    // plateau<->step pairing shifts, that channel crosses a given dB level at a
    // different segment than its peers, so when the profiles are applied on the
    // shared per-stream time axis the staircases visibly stagger. Identical dB per
    // segment across all channels == time-aligned transitions.
    const QVector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}};
    constexpr int kHold = 150; // > the 100-sample (1.0 s) confirmation window

    // Per-channel raw plateau level at each of the four sweep segments.
    const QVector<double> clean    = {1000.0, 1500.0, 2000.0, 3000.0}; // nominal gain
    const QVector<double> hotGain  = {1300.0, 1900.0, 2500.0, 3700.0}; // higher gain, same sweep
    const QVector<double> inverted = {3000.0, 2500.0, 2000.0, 1000.0}; // inverted polarity (raw falls as dB climbs)

    auto buildRaw = [&](const QVector<double>& levels, double leadingTransient) {
        QVector<double> raw;
        if (leadingTransient >= 0.0)
            appendRun(raw, leadingTransient, kHold); // confirmed transient, must be dropped
        for (double level : levels)
            appendRun(raw, level, kHold);
        return raw;
    };

    struct Channel { QVector<double> levels; double transient; };
    const QVector<Channel> channels = {
        { clean,    -1.0 },    // nominal, no transient
        { hotGain,  -1.0 },    // different gain
        { inverted, -1.0 },    // opposite polarity
        { clean,   600.0 },    // a per-channel turn-on transient below the 0 dB level
    };

    for (int c = 0; c < channels.size(); c++)
    {
        const QVector<double> raw = buildRaw(channels[c].levels, channels[c].transient);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        QVERIFY2(r.profile.valid, qPrintable(QString("channel %1 profile invalid").arg(c)));
        QCOMPARE(r.profile.points.size(), steps.size());

        // The crux: each channel's own raw level for sweep segment k must
        // calibrate back to step k's dB. Same dB per segment for every channel.
        for (int k = 0; k < steps.size(); k++)
        {
            const double db = interpolateCalibration(channels[c].levels[k], r.profile);
            QVERIFY2(qAbs(db - steps[k].db) < 1e-6,
                     qPrintable(QString("channel %1 segment %2: raw %3 -> %4 dB, expected %5 dB "
                                        "(pairing shifted -> staircase would stagger)")
                                    .arg(c).arg(k).arg(channels[c].levels[k]).arg(db).arg(steps[k].db)));
        }
    }
}

// ---------------------------------------------------------------------------
// interpolateCalibration
// ---------------------------------------------------------------------------

void TestStepDetector::interpolateMidpoint()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    QVERIFY(qFuzzyCompare(interpolateCalibration(1500.0, p), 9.0));
}

void TestStepDetector::interpolateBelowFirstClamps()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    // Below the first point clamps to its dB (out of calibrated range), not
    // extrapolated: raw 500 -> 6.0 (the first point's dB), not 3.0.
    QVERIFY(qFuzzyCompare(interpolateCalibration(500.0, p), 6.0));
}

void TestStepDetector::interpolateAboveLastClamps()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    // Above the last point clamps to its dB: raw 2500 -> 12.0, not 15.0.
    QVERIFY(qFuzzyCompare(interpolateCalibration(2500.0, p), 12.0));
}

void TestStepDetector::interpolateCoincidentRawNoCrash()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {1000.0, 12.0}};
    p.valid = true;
    // Divide-by-zero guard returns the first point's dB rather than crashing.
    QVERIFY(qFuzzyCompare(interpolateCalibration(1000.0, p), 6.0));
}

void TestStepDetector::detectOutcomeNamesTheGateThatRejected()
{
    QVector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    // Success carries Calibrated, and reports the threshold it judged edges by -
    // the quantity a merged step is measured against.
    {
        QVector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, 150);
        appendRun(raw, 3000.0, 150);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        QVERIFY(r.profile.valid);
        QCOMPARE(r.outcome, CalibrationOutcome::Calibrated);
        QVERIFY(r.edgeThreshold > 0.0);
    }

    // Too few plateaus: only two levels for three expected steps. This is the
    // shape a compressed / near-saturation receiver produces, where adjacent
    // steps merge because their separation never clears the edge threshold.
    {
        QVector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 2000.0, 150);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        QVERIFY(!r.profile.valid);
        QCOMPARE(r.outcome, CalibrationOutcome::TooFewPlateaus);
    }

    // Enough plateaus, but they zig-zag instead of sweeping one way, so no run of
    // three consecutive levels moves in a single direction. Distinct from the
    // case above and pointing at a different cause, which is exactly why the
    // plateau count alone cannot explain a fallback.
    {
        QVector<double> raw;
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 3000.0, 150);
        appendRun(raw, 1000.0, 150);
        appendRun(raw, 3000.0, 150);
        StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
        QVERIFY(!r.profile.valid);
        QVERIFY(r.detectedPlateaus >= steps.size()); // count alone looked fine
        QCOMPARE(r.outcome, CalibrationOutcome::NoMonotonicSweep);
    }
}

void TestStepDetector::detectCompressedOutOfToleranceReceiverStillCalibrates()
{
    // A receiver far out of tolerance does not move a uniform number of raw
    // counts per dB: it is compressed at one end and expansive at the other, so
    // one sweep can hold both 12-count and 1100-count steps. A single global edge
    // threshold sized for the large steps cannot see the small ones, which is how
    // these channels lose their profile and fall back to linear.
    QVector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0}, {24.0}, {36.0}};

    const QVector<double> levels = {800.0, 812.0, 826.0, 900.0, 1500.0, 2600.0};

    // Pseudo-random (not periodic) dither, seeded for reproducibility. A periodic
    // pattern would be a poor stand-in for receiver noise: its median absolute
    // deviation collapses to zero even when the swings are large, which drives the
    // MAD-derived edge threshold to its floor and marks every sample an edge.
    quint32 seed = 12345u;
    auto dither = [&seed]() {
        seed = seed * 1664525u + 1013904223u;      // Numerical Recipes LCG
        return (static_cast<double>((seed >> 16) & 0xFF) / 255.0 - 0.5) * 8.0; // +/- 4 counts
    };

    QVector<double> raw;
    for (double level : levels)
    {
        for (int i = 0; i < 150; i++)
        {
            raw.push_back(level + dither());
        }
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);

    QVERIFY2(r.profile.valid,
             qPrintable(QString("outcome=%1 plateaus=%2 threshold=%3")
                            .arg(calibrationOutcomeText(r.outcome))
                            .arg(r.detectedPlateaus)
                            .arg(r.edgeThreshold)));
    QCOMPARE(r.profile.points.size(), steps.size());

    // Every step must keep its own dB: a merged pair would shift the pairing and
    // silently mis-map the whole channel rather than fail loudly.
    QCOMPARE(r.profile.points.first().trueDb, 0.0);
    QCOMPARE(r.profile.points.last().trueDb, 36.0);

    // It only resolved because the threshold was loosened, and that fact is
    // reported rather than swallowed: a channel whose steps sit this close to its
    // own noise is the one drifting toward not calibrating at all.
    QVERIFY(r.thresholdRelaxed);

    // A receiver with well-separated steps must NOT be reported as relaxed - the
    // signal is only meaningful if the ordinary case stays quiet.
    QVector<double> healthy;
    appendRun(healthy, 800.0, 150);
    appendRun(healthy, 1200.0, 150);
    appendRun(healthy, 1600.0, 150);
    appendRun(healthy, 2000.0, 150);
    appendRun(healthy, 2400.0, 150);
    appendRun(healthy, 2800.0, 150);
    StepDetector::Result h = StepDetector::detect(healthy, kPeriod, steps);
    QVERIFY(h.profile.valid);
    QVERIFY(!h.thresholdRelaxed);
}

void TestStepDetector::detectNoiseSplitPlateausDoNotSeverTheSweep()
{
    // Levels taken verbatim from a real out-of-tolerance receiver
    // (step_cal, example.ch10, R_RCVR1): a clean climb from no-signal to a
    // saturated ceiling, except noise split two dwells into pairs of plateaus
    // differing by 3 and -1 raw counts.
    //
    // Those sub-count wobbles are not transitions, but the sweep selector read
    // them as "no real change" and severed the monotonic run mid-sweep, so a
    // plainly good calibration was discarded and the channel fell back to linear.
    // Adjacent plateaus within the edge threshold are the SAME physical level and
    // must be coalesced before the run is sought.
    QVector<StepDefinition> steps = {{0.0}, {3.0}, {6.0}, {12.0},
                                     {18.0}, {24.0}, {30.0}, {36.0}};

    const QVector<double> levels = {0.0,     7255.0,  14746.0, 22314.0,
                                    27315.0, 34752.0, 49892.0, 49895.0,
                                    57310.0, 57309.0, 65472.0, 0.0};

    // Carry receiver noise too, because it is load-bearing: the noise is what
    // lifts the edge threshold above the 1-3 count split, and a noiseless copy of
    // these levels would sit at the 2.0-count floor where a 3-count split is a
    // "real" transition. The bug only exists in the presence of the noise that
    // created the splits in the first place.
    quint32 seed = 987u;
    auto dither = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<double>((seed >> 16) & 0xFF) / 255.0 - 0.5) * 4.0; // +/- 2 counts
    };

    QVector<double> raw;
    for (double level : levels)
    {
        for (int i = 0; i < 150; i++)
        {
            raw.push_back(level + dither());
        }
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY2(r.profile.valid,
             qPrintable(QString("outcome=%1 plateaus=%2 threshold=%3")
                            .arg(calibrationOutcomeText(r.outcome))
                            .arg(r.detectedPlateaus)
                            .arg(r.edgeThreshold)));
    QCOMPARE(r.profile.points.size(), steps.size());

    // Nine levels for eight steps, so the run's last `expected` are kept: 7255 ->
    // 0 dB up to the ceiling -> 36 dB, each split pair contributing exactly one
    // point. (These levels are a coalescing fixture, not a correct calibration of
    // that receiver: its true sweep is 0-60 dB in eleven steps with the top two
    // on the rail - see TestCalibrationExtractor.) Compared with a tolerance
    // because the dither moves each settled average slightly.
    QVERIFY2(qAbs(r.profile.points.first().rawAvg - 7255.0) < 5.0,
             qPrintable(QString::number(r.profile.points.first().rawAvg)));
    QCOMPARE(r.profile.points.first().trueDb, 0.0);
    QVERIFY2(qAbs(r.profile.points.last().rawAvg - 65472.0) < 5.0,
             qPrintable(QString::number(r.profile.points.last().rawAvg)));
    QCOMPARE(r.profile.points.last().trueDb, 36.0);
}

void TestStepDetector::detectFlatChannelReportsNoSignalNotMergedSteps()
{
    // A dead input holds one value for the whole recording. Before, this reported
    // "too few plateaus" - the same verdict a compressed receiver gets - which
    // sends the operator looking for a detection problem when the sweep simply is
    // not in the recording. The two must read differently.
    QVector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}};

    QVector<double> flat;
    appendRun(flat, 0.0, 600);
    StepDetector::Result r = StepDetector::detect(flat, kPeriod, steps);
    QVERIFY(!r.profile.valid);
    QCOMPARE(r.outcome, CalibrationOutcome::FlatNoSignal);
    // The stuck level is carried out so the report can name it: 0 is a dead
    // input, full scale is a railed one, and the operator needs to tell them apart.
    QCOMPARE(r.plateauLevels.size(), 1);
    QCOMPARE(r.plateauLevels.first(), 0.0);

    // A railed channel is equally flat, just at the other end of the range.
    QVector<double> railed;
    appendRun(railed, 65535.0, 600);
    StepDetector::Result railedResult = StepDetector::detect(railed, kPeriod, steps);
    QCOMPARE(railedResult.outcome, CalibrationOutcome::FlatNoSignal);
    QCOMPARE(railedResult.plateauLevels.first(), 65535.0);

    // Two genuine levels is NOT "flat" - it is a real but incomplete sweep, and
    // still reports as merged steps rather than as a dead channel.
    QVector<double> twoLevels;
    appendRun(twoLevels, 1000.0, 300);
    appendRun(twoLevels, 2000.0, 300);
    StepDetector::Result partial = StepDetector::detect(twoLevels, kPeriod, steps);
    QCOMPARE(partial.outcome, CalibrationOutcome::TooFewPlateaus);
}

void TestStepDetector::detectPinnedSamplesDoNotShrinkTheNoiseEstimate()
{
    // A receiver parked at the converter floor repeats 0 exactly: noise-free, and
    // on a real cal recording a large share of the series (the pre-sweep, and the
    // post-sweep once the generator drops). Counted into the noise estimate, those
    // perfect-zero derivatives drag it far under the live steps' jitter; the edge
    // threshold falls to its 2-count floor, every jittering sample reads as an
    // edge, and the dwells shatter into runs too short to confirm. The channel
    // then reads as flat with its sweep plainly present.
    //
    // The pinned share is exaggerated here (a majority) so the failure is
    // deterministic rather than dependent on where a median happens to land.
    QVector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}};

    QVector<double> raw;
    appendRun(raw, 0.0, 1000); // pinned at the floor: the 0 dB level
    for (double level : {1000.0, 2000.0, 3000.0, 4000.0})
    {
        for (int i = 0; i < 150; i++)
        {
            raw.push_back(level + ((i % 2 == 0) ? 5.0 : -5.0)); // +/-5 count jitter
        }
    }

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY2(r.profile.valid, qPrintable(QString("outcome=%1 threshold=%2")
                                             .arg(calibrationOutcomeText(r.outcome))
                                             .arg(r.edgeThreshold)));
    QCOMPARE(r.profile.points.size(), steps.size());
    QCOMPARE(r.profile.points.first().rawAvg, 0.0);
    QCOMPARE(r.profile.points.first().trueDb, 0.0);
    QVERIFY(qAbs(r.profile.points.last().rawAvg - 4000.0) < 6.0);
    QCOMPARE(r.profile.points.last().trueDb, 24.0);
}

void TestStepDetector::detectSplitDwellFarAboveNoiseIsStillOneStep()
{
    // Noise grows with signal on a real receiver, so a high dwell can split into
    // two plateaus further apart than any threshold the quiet low steps support:
    // the step_cal recording's L_RCVR3 split its 42 dB dwell 22 counts apart against a
    // 17-count threshold, on steps ~3800 counts tall. The split took a pairing
    // slot, the real 0 dB level was dropped as "pre-roll", and every step below the
    // split read 6 dB low. Levels a small fraction of a typical step apart are one
    // step, whatever the noise estimate says.
    //
    // Noiseless here, so the edge threshold sits at its 2-count floor and only the
    // step-relative merge can heal the 30-count split.
    QVector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}};

    QVector<double> raw;
    appendRun(raw, 0.0, 150);    // 0 dB
    appendRun(raw, 1000.0, 150); // 6 dB
    appendRun(raw, 2000.0, 150); // 12 dB
    appendRun(raw, 3000.0, 150); // 18 dB, first half of the dwell...
    appendRun(raw, 3030.0, 150); // ...second half, split 30 counts high
    appendRun(raw, 4000.0, 150); // 24 dB

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), steps.size());
    // The pre-sweep level keeps its 0 dB slot instead of being dropped as pre-roll.
    QCOMPARE(r.profile.points.first().rawAvg, 0.0);
    QCOMPARE(r.profile.points.first().trueDb, 0.0);
    // The split dwell is ONE point at 18 dB, carried by its later, settled half.
    QCOMPARE(r.profile.points[3].rawAvg, 3030.0);
    QCOMPARE(r.profile.points[3].trueDb, 18.0);
    QCOMPARE(r.profile.points.last().trueDb, 24.0);
}

void TestStepDetector::detectRailedTopStepsCalibrateWhatStayedInRange()
{
    // A receiver whose gain is set too high for the injected levels runs off the
    // top of its converter partway up the sweep: the last dwells all read full
    // scale, so the recording cannot hold `expected` distinct levels and no
    // threshold will find them. Before, the whole channel fell back to linear and
    // lost the steps it HAD measured. Those railed dwells carry no information -
    // the receiver had no headroom - so they are dropped, the steps below them are
    // calibrated, and interpolateCalibration() clamps above the top measured step
    // rather than inventing values by extrapolating past the data.
    QVector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}};

    QVector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);
    appendRun(raw, 65535.0, 300); // 18 and 24 dB both peg the converter

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY2(r.profile.valid, qPrintable(QString::fromLatin1(calibrationOutcomeText(r.outcome))));
    QVERIFY(r.saturated);
    QCOMPARE(r.outcome, CalibrationOutcome::Calibrated);

    // The three measured dwells keep their own dB - the sweep starts at step 0.
    QCOMPARE(r.profile.points.size(), 3);
    QCOMPARE(r.profile.points.first().rawAvg, 1000.0);
    QCOMPARE(r.profile.points.first().trueDb, 0.0);
    QCOMPARE(r.profile.points.last().rawAvg, 3000.0);
    QCOMPARE(r.profile.points.last().trueDb, 12.0);
    QCOMPARE(r.unresolvedDb, QVector<double>({18.0, 24.0}));

    // A raw value at the rail reads the top step that was actually measured. It
    // is not 24 dB: the recording cannot say whether the signal was 18, 24, or
    // anything above, so the honest answer is the last one it could see.
    QCOMPARE(interpolateCalibration(65535.0, r.profile), 12.0);
}

void TestStepDetector::detectUnconfirmedDwellIsMeasuredFromTheGrid()
{
    // The 18 dB dwell is in the recording but its plateau never confirms - noise
    // shatters it into runs too short to hold. Two failures to guard against:
    //  - position alone would pair the 24 dB dwell with the 18 dB step and slide
    //    everything after it down one, the silent mis-calibration a split dwell
    //    used to cause;
    //  - leaving the hole and interpolating across it assumes the receiver is
    //    linear there, which is exactly what an out-of-tolerance receiver is not
    //    (L_RCVR1's 24 dB dwell read 23.09 dB that way on the step_cal recording).
    // The dwell grid says where the missing dwell is, so it is measured directly.
    QVector<StepDefinition> steps = {{0.0}, {6.0}, {12.0}, {18.0}, {24.0}, {30.0}};

    QVector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);
    // 18 dB: jitters by a few counts for its whole dwell, so no run of it is ever
    // stable long enough to confirm - but the transitions either side still are.
    // Deliberately NOT the midpoint of its neighbours, so a linear bridge across
    // the hole would read the wrong dB.
    for (int i = 0; i < 150; i++)
    {
        raw.push_back(3600.0 + ((i % 2 == 0) ? 0.0 : 6.0));
    }
    appendRun(raw, 5000.0, 150);
    appendRun(raw, 65535.0, 150); // 30 dB pegs the converter

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY2(r.profile.valid, qPrintable(QString::fromLatin1(calibrationOutcomeText(r.outcome))));
    QVERIFY(r.saturated);
    QCOMPARE(r.profile.points.size(), 5);

    // The unconfirmed dwell is measured at its own level and keeps its own dB...
    QCOMPARE(r.profile.points[3].rawAvg, 3603.0);
    QCOMPARE(r.profile.points[3].trueDb, 18.0);
    QCOMPARE(interpolateCalibration(3603.0, r.profile), 18.0);
    // ...the step after it keeps ITS own dB rather than sliding down one...
    QCOMPARE(r.profile.points.last().rawAvg, 5000.0);
    QCOMPARE(r.profile.points.last().trueDb, 24.0);
    // ...and only the railed step is left unresolved.
    QCOMPARE(r.unresolvedDb, QVector<double>({30.0}));
}
