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
