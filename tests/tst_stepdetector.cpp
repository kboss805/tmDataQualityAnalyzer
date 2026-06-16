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

const double kPeriod = 0.01; // 10 ms

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
        "dwell_sec = 5.0\n"
        "\n"
        "[[Step]]\n"
        "db = 6.0\n"
        "dwell_sec = 5.0\n");

    QVector<StepDefinition> steps;
    QString error;
    QVERIFY(StepDetector::parseStepConfig(path, steps, error));
    QCOMPARE(steps.size(), 2);
    QCOMPARE(steps[0].db, 0.0);
    QCOMPARE(steps[0].dwellSec, 5.0);
    QCOMPARE(steps[1].db, 6.0);
    QCOMPARE(steps[1].dwellSec, 5.0);
}

void TestStepDetector::parseStepConfigMissingDwellFails()
{
    QTemporaryFile f;
    const QString path = writeTempToml(f,
        "[[Step]]\n"
        "db = 0.0\n"); // no dwell_sec

    QVector<StepDefinition> steps;
    QString error;
    QVERIFY(!StepDetector::parseStepConfig(path, steps, error));
    QVERIFY(!error.isEmpty());
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
    QVector<StepDefinition> steps = {{0.0, 1.0}, {6.0, 1.0}, {12.0, 1.0}};

    QVector<double> raw;
    appendRun(raw, 1000.0, 100);
    appendRun(raw, 2000.0, 100);
    appendRun(raw, 3000.0, 100);

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
    QVector<StepDefinition> steps = {{0.0, 1.0}, {6.0, 1.0}, {12.0, 1.0}};

    QVector<double> raw;
    appendRun(raw, 1000.0, 100);
    appendRun(raw, 2000.0, 100);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(!r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 2);
}

void TestStepDetector::detectExtraPlateausUsesFirstN()
{
    // Two steps expected, three plateaus present -> first two used in time order.
    QVector<StepDefinition> steps = {{0.0, 1.0}, {6.0, 1.0}};

    QVector<double> raw;
    appendRun(raw, 1000.0, 100);
    appendRun(raw, 2000.0, 100);
    appendRun(raw, 3000.0, 100);

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QVERIFY(r.extraPlateaus);
    QCOMPARE(r.profile.points.size(), 2);
    QVERIFY(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0));
    QVERIFY(qFuzzyCompare(r.profile.points[1].rawAvg, 2000.0));
}

void TestStepDetector::detectNoisyStepsStillDetected()
{
    QVector<StepDefinition> steps = {{0.0, 1.0}, {6.0, 1.0}};

    // Small in-plateau ripple (amplitude 2) << the 1000-count step transitions.
    QVector<double> raw;
    for (int i = 0; i < 100; i++) raw.push_back(1000.0 + (i % 3 == 0 ? 2.0 : 0.0));
    for (int i = 0; i < 100; i++) raw.push_back(2000.0 + (i % 3 == 0 ? 2.0 : 0.0));

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.detectedPlateaus, 2);
}

void TestStepDetector::detectEdgeTrimExcludesTransition()
{
    // One step; the first samples of the plateau ramp up gradually (settling,
    // delta <= threshold floor so it stays "stable") before reaching the true
    // core level. Edge-trimming must drop the settling region so the average
    // equals the clean core value.
    QVector<StepDefinition> steps = {{3.0, 2.0}};

    QVector<double> raw;
    appendRun(raw, 0.0, 100);          // pre-step baseline
    // Gradual settling ramp within the first ~12.5% of the 200-sample plateau.
    for (int v = 991; v <= 1000; v++) raw.push_back(static_cast<double>(v));
    appendRun(raw, 1000.0, 190);       // stable core

    StepDetector::Result r = StepDetector::detect(raw, kPeriod, steps);
    QVERIFY(r.profile.valid);
    QCOMPARE(r.profile.points.size(), 1);
    // After trimming the settling ramp, the core is a flat 1000.
    QVERIFY(qFuzzyCompare(r.profile.points[0].rawAvg, 1000.0));
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
