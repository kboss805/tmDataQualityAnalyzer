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
    file.open();
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

void TestStepDetector::detectExtraPlateausUsesFirstN()
{
    // Two steps expected, three plateaus present -> first two used in time order.
    QVector<StepDefinition> steps = {{0.0}, {6.0}};

    QVector<double> raw;
    appendRun(raw, 1000.0, 150);
    appendRun(raw, 2000.0, 150);
    appendRun(raw, 3000.0, 150);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QVERIFY(r.extraPlateaus);
    QCOMPARE(r.profile.points.size(), 2);
    QVERIFY(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0));
    QVERIFY(qFuzzyCompare(r.profile.points[1].rawAvg, 2000.0));
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

void TestStepDetector::detectNonMonotonicPairingRejected()
{
    // Chronological plateau order (2000, 1000, 3000) paired positionally with
    // steps (0, 6, 12) gives points (2000,0),(1000,6),(3000,12). Sorted
    // ascending by rawAvg that's (1000,6),(2000,0),(3000,12) -- dB goes
    // 6 -> 0 -> 12, which is neither non-decreasing nor non-increasing. This
    // mirrors a real failure mode: extra/missed plateaus shift the positional
    // step pairing, producing two near-adjacent raw values pinned to
    // contradictory dB values. The profile must be rejected (not handed to
    // interpolateCalibration with a near-vertical extrapolation slope).
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

void TestStepDetector::interpolateBelowFirstExtrapolates()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    // slope = 6 dB / 1000 counts; at raw 500 -> 6 - 3 = 3.
    QVERIFY(qFuzzyCompare(interpolateCalibration(500.0, p), 3.0));
}

void TestStepDetector::interpolateAboveLastExtrapolates()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {2000.0, 12.0}};
    p.valid = true;
    // at raw 2500 -> 12 + 3 = 15.
    QVERIFY(qFuzzyCompare(interpolateCalibration(2500.0, p), 15.0));
}

void TestStepDetector::interpolateCoincidentRawNoCrash()
{
    CalibrationProfile p;
    p.points = {{1000.0, 6.0}, {1000.0, 12.0}};
    p.valid = true;
    // Divide-by-zero guard returns the first point's dB rather than crashing.
    QVERIFY(qFuzzyCompare(interpolateCalibration(1000.0, p), 6.0));
}
