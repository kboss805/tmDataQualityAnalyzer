/**
 * @file tst_calibrationextractor.cpp
 * @brief Unit tests for CalibrationExtractor pipeline orchestration (US5.3).
 *
 * StepDetector's pure logic is covered by TestStepDetector; this suite drives the
 * async extraction end to end (reader + FrameProcessor on worker threads, then
 * per-channel StepDetector) and asserts the extractor's own contract: the error
 * path on a bad file, and — on the real recording — which channels calibrate.
 */

#include "tst_calibrationextractor.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <QTemporaryDir>
#include <QtTest>

#include "calibrationextractor.h"
#include "containerutil.h"
#include "calibrationprofile.h"
#include "chapter10reader.h"
#include "framesetup.h"
#include "framesync.h"
#include "qtbridge.h"
#include "qtprocessingevents.h"
#include "stepdetector.h"

namespace {

/// Resolves tests/data/<filename> relative to the test executable (one level
/// under tests/, per the build-and-test convention).
QString testDataPath(const QString& filename)
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    return dir.filePath("data/" + filename);
}

/// Resolves a path under the project root (two levels above the test exe).
QString projectRootPath(const QString& rel)
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();  // tests/
    dir.cdUp();  // project root
    return dir.filePath(rel);
}

/// How the bits are recovered, from the hex text the dialogs hold. The one place these tests make an
/// Acquisition, so it is decoded the way the application decodes it.
void setAcquisition(CalibrationExtractor::Request& req, const std::string& pattern, int bits,
                    bool randomized, double rate_mbps, bool swap_bytes)
{
    Acquisition a;
    if (resolveFrameSync(pattern, std::string(), a.sync) != FrameSyncError::None)
    {
        qFatal("test sync pattern '%s' does not decode", pattern.c_str());
    }
    a.bitsInMinorFrame = bits;
    a.randomized       = randomized;
    a.dataRateMbps     = rate_mbps;
    a.swapBytes        = swap_bytes;
    req.acquisition    = a;
}

/// A result's name and the report text as QStrings. These helpers are the one place the tests touch
/// either, so a change of the string type underneath touches them and nothing else.
QString nameOf(const CalibrationChannelResult& r) { return QString::fromStdString(r.name); }
void setName(CalibrationChannelResult& r, const QString& name) { r.name = name.toStdString(); }
QString summarizeReport(const std::vector<CalibrationChannelResult>& results, int expectedSteps)
{
    return QString::fromStdString(CalibrationExtractor::summarize(results, expectedSteps));
}

/// One channel's outcome, with just the fields the report reads.
CalibrationChannelResult channelResult(const QString& name, CalibrationOutcome outcome,
                                       int plateaus = 0, bool hadData = true)
{
    CalibrationChannelResult r;
    setName(r, name);
    r.outcome          = outcome;
    r.detectedPlateaus = plateaus;
    r.hadData          = hadData;
    r.profile.valid    = (outcome == CalibrationOutcome::Calibrated);
    return r;
}

/// A calibrated channel whose profile reaches @p top_db, optionally railed with steps it never measured.
CalibrationChannelResult railedResult(const QString& name, double top_db, std::vector<double> unresolved)
{
    CalibrationChannelResult r = channelResult(name, CalibrationOutcome::Calibrated, 6);
    r.profile.points = {{1000.0, 0.0}, {2000.0, top_db / 2.0}, {3000.0, top_db}};
    r.saturated      = true;
    r.unresolvedDb   = std::move(unresolved);
    return r;
}

/// A channel that held one level for the whole recording.
CalibrationChannelResult flatResult(const QString& name, double level)
{
    CalibrationChannelResult r = channelResult(name, CalibrationOutcome::FlatNoSignal, 1);
    r.plateauLevels = {level};
    return r;
}

/// Result sets for the exact-report test, by scenario name. @p expected_steps is the step file's length.
std::vector<CalibrationChannelResult> reportScenario(const QString& scenario, int& expected_steps)
{
    using O = CalibrationOutcome;
    const QStringList lcr = {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C")};
    std::vector<CalibrationChannelResult> v;
    expected_steps = 6;

    if (scenario == "empty")
    {
        return v;
    }
    if (scenario == "one-receiver")
    {
        for (const QString& p : lcr)
            v.push_back(channelResult(p + "_RCVR3", O::Calibrated, 6));
    }
    else if (scenario == "mixed")
    {
        for (const QString& p : lcr)
            v.push_back(channelResult(p + "_RCVR1", O::Calibrated, 6));
        v.push_back(channelResult("L_RCVR2", O::Calibrated, 6));
        v.push_back(channelResult("R_RCVR2", O::Calibrated, 6));
        v.push_back(channelResult("C_RCVR2", O::TooFewPlateaus, 4));
        for (const QString& p : lcr)
            v.push_back(channelResult(p + "_RCVR3", O::Calibrated, 6));
        for (const QString& p : lcr)
            v.push_back(channelResult(p + "_RCVR4", O::NoData, 0, false));
    }
    else if (scenario == "unnamed")
    {
        v.push_back(channelResult("AGC_LEFT", O::NoMonotonicSweep, 7));
        v.push_back(channelResult("AGC_RIGHT", O::Calibrated, 6));
    }
    else if (scenario == "unnamed-all-calibrated")
    {
        v.push_back(channelResult("AGC_LEFT", O::Calibrated, 6));
        v.push_back(channelResult("AGC_RIGHT", O::Calibrated, 6));
    }
    else if (scenario == "unnamed-non-ascii")
    {
        v.push_back(channelResult(QStringLiteral("R") + QChar(0x00E9) + QStringLiteral("cepteur ") + QChar(0x03A9),
                                  O::Calibrated, 6));
        v.push_back(channelResult(QStringLiteral("Gain ") + QChar(0x03A3), O::TooFewPlateaus, 2));
    }
    else if (scenario == "checks")
    {
        expected_steps = 8;
        for (const QString& p : lcr)
        {
            CalibrationChannelResult r = channelResult(p + "_RCVR3", O::Calibrated, 8);
            r.sweepLevels = 11;
            v.push_back(r);
        }
        v.push_back(flatResult("L_RCVR2", 0.0));
        v.push_back(flatResult("R_RCVR2", 0.0));
        CalibrationChannelResult swept = channelResult("C_RCVR2", O::Calibrated, 8);
        swept.sweepLevels = 8;
        v.push_back(swept);
    }
    else if (scenario == "saturated-ties")
    {
        // Top dB 37.5 and unresolved dB 42.5 / 2.5 sit exactly halfway between two integers, which is
        // where Qt's 'f' formatting (away from zero) and a half-to-even formatter disagree.
        v.push_back(railedResult("L_RCVR1", 37.5, {42.5, 2.5}));
        v.push_back(railedResult("R_RCVR1", 37.5, {42.5, 60.0}));
        v.push_back(channelResult("C_RCVR1", O::Calibrated, 6));
        v.push_back(railedResult("L_RCVR5", 0.5, {}));
    }
    else if (scenario == "relaxed")
    {
        for (const QString& p : lcr)
        {
            CalibrationChannelResult r = channelResult(p + "_RCVR2", O::Calibrated, 6);
            r.thresholdRelaxed = (p != "R");
            v.push_back(r);
        }
    }
    else if (scenario == "flat-levels")
    {
        // Held levels are printed to the nearest count; 0.5 and 1234.5 are exact ties.
        v.push_back(flatResult("L_RCVR4", 0.5));
        v.push_back(flatResult("R_RCVR4", 65472.0));
        v.push_back(flatResult("C_RCVR4", 0.5));
        v.push_back(flatResult("L_RCVR6", 1234.5));
        v.push_back(channelResult("R_RCVR6", O::TooFewPlateaus, 3));
    }
    else if (scenario == "several-reasons")
    {
        v.push_back(channelResult("L_RCVR7", O::NoMonotonicSweep, 9));
        v.push_back(channelResult("R_RCVR7", O::NoData, 0, false));
        v.push_back(channelResult("C_RCVR7", O::TooFewPlateaus, 2));
        v.push_back(channelResult("L_RCVR8", O::TooFewPlateaus, 5));
        v.push_back(channelResult("R_RCVR8", O::TooFewPlateaus, 3));
    }
    else if (scenario == "twelve-receivers")
    {
        for (int rcvr = 1; rcvr <= 12; rcvr++)
            v.push_back(channelResult(QString("L_RCVR%1").arg(rcvr), O::Calibrated, 6));
    }
    return v;
}

/// A minimal but non-empty step list, so a Request clears the "no steps" guard
/// and actually drives the reader/frame-setup path.
std::vector<StepDefinition> dummySteps()
{
    std::vector<StepDefinition> steps;
    for (double db : { 0.0, 6.0, 12.0 })
        steps.push_back(StepDefinition{ db });
    return steps;
}

/// Drives an extraction to completion, pumping a nested event loop until
/// finished() fires (mirrors how the production dialog waits). @return success.
///
/// Observes one extraction: every report it makes, in the order they arrive, and whether it
/// finished. The ONE place these tests attach to the extractor's reports or wait for its end, so
/// a change in how it reports touches this class and nothing else.
class Recorder
{
public:
    explicit Recorder(CalibrationExtractor& extractor) : m_extractor(extractor)
    {
        // Each report is delivered to this thread, in order, by the Qt adapter.
        m_events = std::make_shared<QtProcessingEvents>(
            &m_context,
            [this](const QString& message) { log << message; note("log"); },
            [this](const QString& message) { errors << message; },
            [this](bool ok) {
                success = ok;
                summary = QString::fromStdString(m_extractor.summary());
                done    = true;
                events << "finished";
                m_loop.quit();
            },
            [this](int percent) { progress << percent; note("progress"); });
    }

    void start(const CalibrationExtractor::Request& request) { m_extractor.start(request, m_events); }

    /// Pumps events until the extraction finishes, or @p timeout_ms passes. @return whether it finished.
    bool wait(int timeout_ms = 60000)
    {
        if (!done)
        {
            QTimer::singleShot(timeout_ms, &m_loop, &QEventLoop::quit);
            m_loop.exec();
        }
        return done;
    }

    /// Starts and waits. @return true if it finished AND succeeded.
    bool run(const CalibrationExtractor::Request& request)
    {
        start(request);
        return wait() && success;
    }

    bool        cancelOnFirstReport = false;  ///< Cancel as soon as the first log or progress report arrives.
    bool        done    = false;
    bool        success = false;
    QString     summary;                      ///< The message that came with the finish.
    QStringList errors;                       ///< Errors reported (a failure is reported before it finishes).
    QStringList log;
    QList<int>  progress;
    QStringList events;                       ///< "log", "progress" and "finished", in arrival order.

private:
    void note(const char* kind)
    {
        events << kind;
        if (cancelOnFirstReport && !m_cancelled)
        {
            m_cancelled = true;
            m_extractor.cancel();
        }
    }

    CalibrationExtractor&                    m_extractor;
    QObject                                  m_context;
    QEventLoop                               m_loop;
    std::shared_ptr<ProcessingEvents>        m_events;
    bool                  m_cancelled = false;
};

bool runExtraction(CalibrationExtractor& extractor,
                   const CalibrationExtractor::Request& req, QString& summary)
{
    Recorder recorder(extractor);
    const bool ok = recorder.run(req);
    summary = recorder.summary;
    return ok;
}

/// Builds the canonical rnrz-l_testfile Request. Per the calibration test-file
/// shape, this is an 8000-bit RNRZ-L recording whose real stepped SNR sweep is
/// present only on RCVR3 = words 6/7/8; an empty receiverParamsToml means the
/// extractor builds the 16x3 sequential word map. @return false (with a skip
/// reason) if the fixtures are unavailable. The caller owns @p reader so the
/// opened Chapter 10 file outlives the request build.
bool buildRealRequest(Chapter10Reader& reader, CalibrationExtractor::Request& req,
                      QString& skipReason)
{
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
    {
        skipReason = "RNRZ-L test file not available";
        return false;
    }

    if (!reader.loadChannels(toFsPath(filepath)))
    {
        skipReason = "Could not load channels from test file";
        return false;
    }
    const int pcm_id  = reader.getFirstPCMChannelID();
    const int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
    {
        skipReason = "Missing channels in test file";
        return false;
    }

    const QString step_toml = projectRootPath("settings/rcvr_cals/default.toml");
    if (!QFileInfo::exists(step_toml))
    {
        skipReason = "step-config TOML (rcvr_cals/default.toml) not available";
        return false;
    }
    std::vector<StepDefinition> steps;
    std::string step_error;
    if (!StepDetector::parseStepConfig(toFsPath(step_toml), steps, step_error) || steps.empty())
    {
        skipReason = "Could not parse step config: " + QString::fromStdString(step_error);
        return false;
    }

    req.calFilename           = toFsPath(filepath);
    req.timeChannelId         = time_id;
    req.pcmChannelId          = pcm_id;
    // 8000 bits is this recording's true minor-frame length; RNRZ-L; 0.8 Mbps drives the adaptive
    // extract period. An ordinary Chapter 10 recording, so it needs the byte swap. Stated rather
    // than inherited, because this flag must match whatever the main run uses - that is the
    // mirror invariant this suite exists to protect.
    setAcquisition(req, "FE6B2840", 8000, /*randomized*/ true, /*rate*/ 0.8, /*swap*/ true);
    req.numReceivers          = 16;
    req.receiverChannels      = 3;
    req.steps                 = steps;
    return true;
}

/// Builds a Request over step_cal, example.ch10 with the given step file and
/// receiver-parameters word map (both relative to the project root). @return false
/// with a skip reason if the fixture or a settings file is unavailable.
bool buildStepCalRequest(Chapter10Reader& reader, CalibrationExtractor::Request& req,
                         const QString& stepToml, const QString& receiverParams,
                         QString& skipReason)
{
    const QString filepath = testDataPath("test files/step_cal, example.ch10");
    if (!QFileInfo::exists(filepath))
    {
        skipReason = "step_cal, example.ch10 not available";
        return false;
    }
    if (!reader.loadChannels(toFsPath(filepath)))
    {
        skipReason = "Could not load channels from step_cal, example.ch10";
        return false;
    }
    std::vector<StepDefinition> steps;
    std::string step_error;
    if (!StepDetector::parseStepConfig(toFsPath(projectRootPath(stepToml)), steps, step_error))
    {
        skipReason = "Could not parse " + stepToml + ": " + QString::fromStdString(step_error);
        return false;
    }
    req.calFilename           = toFsPath(filepath);
    req.timeChannelId         = reader.getCurrentTimeChannelID();
    req.pcmChannelId          = reader.getFirstPCMChannelID();
    setAcquisition(req, "FE6B2840", 800, /*randomized*/ false, /*rate*/ 0.8, /*swap*/ true);
    req.receiverParamsToml    = toFsPath(projectRootPath(receiverParams));
    req.numReceivers          = 16;
    req.receiverChannels      = 3;
    req.steps                 = steps;
    return true;
}

} // namespace

void TestCalibrationExtractor::missingFileFailsCleanly()
{
    CalibrationExtractor::Request req;
    req.calFilename           = "definitely_not_a_real_file.ch10";
    req.timeChannelId         = 1;
    req.pcmChannelId          = 1;
    setAcquisition(req, "FE6B2840", 8000, /*randomized*/ false, /*rate*/ 0.0, /*swap*/ true);
    req.numReceivers          = 16;
    req.receiverChannels      = 3;
    req.steps                 = dummySteps(); // clear the empty-steps guard

    CalibrationExtractor extractor;
    QString summary;
    const bool ok = runExtraction(extractor, req, summary);

    // The reader can't open the file, so the run finishes unsuccessfully with a
    // recorded error and no per-channel results — no hang, no partial state.
    QVERIFY(!ok);
    QVERIFY(!extractor.error().empty());
    QVERIFY(extractor.results().empty());
}

void TestCalibrationExtractor::realFileCalibratesRcvr3Only()
{
    Chapter10Reader reader; // keep the opened Ch10 file alive across extraction
    CalibrationExtractor::Request req;
    QString skip;
    if (!buildRealRequest(reader, req, skip))
        QSKIP(qPrintable(skip));

    CalibrationExtractor extractor;
    QString summary;
    QVERIFY2(runExtraction(extractor, req, summary), qPrintable(summary));

    const std::vector<CalibrationChannelResult>& results = extractor.results();

    // One result per word-map channel (16 receivers x 3 channels).
    QCOMPARE(results.size(), req.numReceivers * req.receiverChannels);

    // The real stepped SNR sweep is present only on RCVR3 = words 6/7/8 (L/R/C);
    // every other receiver word is low-level noise and MUST fall back to linear
    // (the detector rejects flat/inactive channels). So exactly those three words
    // get a valid non-linear profile.
    QSet<int> calibratedWords;
    for (const CalibrationChannelResult& r : results)
        if (r.profile.valid)
            calibratedWords.insert(r.word);

    QCOMPARE(calibratedWords.size(), 3);
    QVERIFY2(calibratedWords.contains(6) && calibratedWords.contains(7)
                 && calibratedWords.contains(8),
             qPrintable(QString("expected words {6,7,8}, got a different set (%1)")
                            .arg(calibratedWords.size())));
}

void TestCalibrationExtractor::byteOrderReachesTheExtraction()
{
    // The extractor must mirror every raw-affecting flag of the main run - the
    // project invariant that already cost one bug when isInverted was missing. When
    // byte order became settable, the extractor kept using the ProcessingParams
    // default instead of the operator's choice, so extracting for a stream whose
    // byte order differed from that default would build a profile from a different
    // bitstream than the run it was meant to calibrate. Nothing would have reported
    // it: the profile attaches and the numbers are simply wrong.
    //
    // Asserted behaviourally rather than by inspecting m_params: with the wrong byte
    // order the sync pattern is destroyed and extraction cannot succeed, which is
    // only observable if the flag actually reaches the scan.
    Chapter10Reader reader; // keep the opened Ch10 file alive across extraction
    CalibrationExtractor::Request req;
    QString skip;
    if (!buildRealRequest(reader, req, skip))
        QSKIP(qPrintable(skip));

    // buildRealRequest sets the byte order this fixture needs; flipping it is the
    // only difference between the two runs below.
    req.acquisition.swapBytes = false;

    CalibrationExtractor extractor;
    QString summary;
    const bool ok = runExtraction(extractor, req, summary);
    QVERIFY2(!ok, "the wrong byte order must not produce a calibration");

    req.acquisition.swapBytes = true;
    CalibrationExtractor extractor_ok;
    QString summary_ok;
    QVERIFY2(runExtraction(extractor_ok, req, summary_ok), qPrintable(summary_ok));
}

void TestCalibrationExtractor::summarizeProducesExactlyThisReport()
{
    // The whole text, not fragments: the report is what the operator reads, and the other summary
    // tests only look for phrases inside it. Includes the exact halves (37.5, 42.5, 2.5, 0.5, 1234.5)
    // that Qt rounds away from zero, which a half-to-even formatter would print differently.
    QFETCH(QString, scenario);
    QFETCH(QStringList, lines);

    int expected_steps = 0;
    const std::vector<CalibrationChannelResult> results = reportScenario(scenario, expected_steps);
    QCOMPARE(summarizeReport(results, expected_steps), lines.join('\n'));
}

void TestCalibrationExtractor::summarizeProducesExactlyThisReport_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<QStringList>("lines");

    QTest::newRow("empty") << QStringLiteral("empty")
        << QStringList{
            "No receiver channels were extracted."};

    QTest::newRow("one-receiver") << QStringLiteral("one-receiver")
        << QStringList{
            "Applied non-linear calibration to 3 of 3 channel(s).",
            "",
            "Calibrated: receiver 3."};

    QTest::newRow("mixed") << QStringLiteral("mixed")
        << QStringList{
            "Applied non-linear calibration to 8 of 12 channel(s).",
            "",
            "Calibrated: receivers 1, 3.",
            "",
            "Fell back to linear calibration:",
            QString::fromUtf8("  Receiver 2 — L, R calibrated; C: too few plateaus (4 of 6 steps resolved)"),
            QString::fromUtf8("  Receiver 4 — L, R, C: no data")};

    QTest::newRow("unnamed") << QStringLiteral("unnamed")
        << QStringList{
            "Applied non-linear calibration to 1 of 2 channel(s).",
            "",
            "Fell back to linear calibration:",
            QString::fromUtf8("  Unnamed receiver — AGC_RIGHT calibrated; AGC_LEFT: no monotonic sweep")};

    // Pinned as the old code printed it: a group of unnamed channels that ALL calibrated is listed
    // under the "Fell back to linear calibration:" heading, which reads wrongly for channels that did
    // not fall back. A refactor does not change wording, so it stays until it is fixed on purpose.
    QTest::newRow("unnamed-all-calibrated") << QStringLiteral("unnamed-all-calibrated")
        << QStringList{
            "Applied non-linear calibration to 2 of 2 channel(s).",
            "",
            "Fell back to linear calibration:",
            QString::fromUtf8("  Unnamed receiver (AGC_LEFT, AGC_RIGHT) — calibrated")};

    QTest::newRow("unnamed-non-ascii") << QStringLiteral("unnamed-non-ascii")
        << QStringList{
            "Applied non-linear calibration to 1 of 2 channel(s).",
            "",
            "Fell back to linear calibration:",
            QString::fromUtf8("  Unnamed receiver — Récepteur Ω calibrated; Gain Σ: too few plateaus (2 of 6 steps resolved)")};

    QTest::newRow("checks") << QStringLiteral("checks")
        << QStringList{
            "Applied non-linear calibration to 4 of 6 channel(s).",
            "",
            "Calibrated: receiver 3.",
            "",
            QString::fromUtf8("Check the step file — these receivers stepped through more levels than it lists, so the lowest were set aside as lead-in. If those are real steps, every step is paired with the wrong level:"),
            QString::fromUtf8("  Receiver 3 — 11 levels in the recording, 8 in the step file"),
            "",
            QString::fromUtf8("Check the word map — in these receivers one channel swept and another never moved. A word map that does not match the recording files one receiver's words under another's name; otherwise a channel may be dead:"),
            QString::fromUtf8("  Receiver 2 — C calibrated; L, R never moved"),
            "",
            "Fell back to linear calibration:",
            QString::fromUtf8("  Receiver 2 — C calibrated; L, R: flat / no signal (held at 0)")};

    QTest::newRow("saturated-ties") << QStringLiteral("saturated-ties")
        << QStringList{
            "Applied non-linear calibration to 4 of 4 channel(s).",
            "",
            "Calibrated: receivers 1, 5.",
            "",
            QString::fromUtf8("Calibrated only up to a saturated top — these receivers ran out of range partway through the sweep, so their readings hold at the highest step still measurable:"),
            QString::fromUtf8("  Receiver 1 — L, R: readings hold at 38 dB (no measurement for 43, 3, 60 dB)"),
            QString::fromUtf8("  Receiver 5 — L: readings hold at 1 dB")};

    QTest::newRow("relaxed") << QStringLiteral("relaxed")
        << QStringList{
            "Applied non-linear calibration to 3 of 3 channel(s).",
            "",
            "Calibrated: receiver 2.",
            "",
            QString::fromUtf8("Calibrated only with a relaxed threshold — steps are close to the noise floor, so these receivers are worth checking:"),
            QString::fromUtf8("  Receiver 2 — L, C")};

    QTest::newRow("flat-levels") << QStringLiteral("flat-levels")
        << QStringList{
            "Applied non-linear calibration to 0 of 5 channel(s).",
            "",
            "Fell back to linear calibration:",
            QString::fromUtf8("  Receiver 4 — L, R, C: flat / no signal (held at 1, 65472)"),
            QString::fromUtf8("  Receiver 6 — L: flat / no signal (held at 1235); R: too few plateaus (1 of 6 steps resolved)")};

    QTest::newRow("several-reasons") << QStringLiteral("several-reasons")
        << QStringList{
            "Applied non-linear calibration to 0 of 5 channel(s).",
            "",
            "Fell back to linear calibration:",
            QString::fromUtf8("  Receiver 7 — R: no data; C: too few plateaus (2 of 6 steps resolved); L: no monotonic sweep"),
            QString::fromUtf8("  Receiver 8 — L, R: too few plateaus (3 of 6 steps resolved)")};

    QTest::newRow("twelve-receivers") << QStringLiteral("twelve-receivers")
        << QStringList{
            "Applied non-linear calibration to 12 of 12 channel(s).",
            "",
            "Calibrated: receivers 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12."};
}

void TestCalibrationExtractor::summaryNamesReceiversAndWhyTheyFellBack()
{
    // summarize() is pure, so this needs no .ch10 fixture and always runs.
    auto channel = [](const QString& name, CalibrationOutcome outcome,
                      int plateaus, bool hadData) {
        CalibrationChannelResult r;
        setName(r, name);
        r.outcome          = outcome;
        r.detectedPlateaus = plateaus;
        r.hadData          = hadData;
        r.profile.valid    = (outcome == CalibrationOutcome::Calibrated);
        return r;
    };

    std::vector<CalibrationChannelResult> results;
    // Receiver 1: fully calibrated. Receiver 2: partly - C fell back on merged
    // steps. Receiver 3: fully calibrated. Receiver 4: absent from the recording.
    for (const QString& p : {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C")})
    {
        results.push_back(channel(p + "_RCVR1", CalibrationOutcome::Calibrated, 6, true));
    }
    results.push_back(channel("L_RCVR2", CalibrationOutcome::Calibrated, 6, true));
    results.push_back(channel("R_RCVR2", CalibrationOutcome::Calibrated, 6, true));
    results.push_back(channel("C_RCVR2", CalibrationOutcome::TooFewPlateaus, 4, true));
    for (const QString& p : {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C")})
    {
        results.push_back(channel(p + "_RCVR3", CalibrationOutcome::Calibrated, 6, true));
    }
    for (const QString& p : {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C")})
    {
        results.push_back(channel(p + "_RCVR4", CalibrationOutcome::NoData, 0, false));
    }

    const QString summary = summarizeReport(results, 6);

    // The headline count still reports channels, since that is the unit profiles
    // are keyed by.
    // 3 (rcvr 1) + 2 (rcvr 2's L and R) + 3 (rcvr 3) = 8 of 12.
    QVERIFY2(summary.contains("8 of 12 channel(s)"), qPrintable(summary));

    // Receivers whose every channel calibrated are named together, in one line -
    // the point of the request. Receiver 2 is NOT in that list: it was partial.
    QVERIFY2(summary.contains("Calibrated: receivers 1, 3."), qPrintable(summary));

    // A partly calibrated receiver is spelled out rather than hidden in the count,
    // naming both the channels that worked and the one that did not, with a reason
    // measured against the expected step count.
    QVERIFY2(summary.contains("Receiver 2"), qPrintable(summary));
    QVERIFY2(summary.contains("L, R calibrated"), qPrintable(summary));
    QVERIFY2(summary.contains("C: too few plateaus (4 of 6 steps resolved)"),
             qPrintable(summary));

    // A receiver that was never in the recording reads as such, not as a
    // detection failure - different problem, different remedy.
    QVERIFY2(summary.contains("Receiver 4"), qPrintable(summary));
    QVERIFY2(summary.contains("no data"), qPrintable(summary));

    // Channels from a hand-written receiver-params TOML carry no "_RCVR<N>", so
    // they must be reported by name rather than assigned a receiver number.
    std::vector<CalibrationChannelResult> unnamed;
    unnamed.push_back(channel("AGC_LEFT", CalibrationOutcome::NoMonotonicSweep, 7, true));
    const QString unnamedSummary = summarizeReport(unnamed, 6);
    QVERIFY2(unnamedSummary.contains("AGC_LEFT"), qPrintable(unnamedSummary));
    QVERIFY2(unnamedSummary.contains("no monotonic sweep"), qPrintable(unnamedSummary));
}

void TestCalibrationExtractor::realStepCalRecordingPairsEveryStepCorrectly()
{
    // step_cal, example.ch10: a real step-calibration recording from receivers far
    // out of alignment. Its sweep is 0 to 60 dB in 6 dB steps - eleven levels, the
    // first being the level each channel holds before the generator steps up - so
    // it is extracted with the 11-step settings/rcvr_cals/default.toml. Each dwell
    // lasts ~4.9 s, and a channel's noise grows with its level: ~3 counts on its
    // low steps, ~60 on its high ones.
    //
    // Acquisition parameters (found by probing; nothing else locks): NRZ-L,
    // byte-swapped, 800-bit minor frames at 800 kbps on PCM channel 26.
    const QString filepath = testDataPath("test files/step_cal, example.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("step_cal, example.ch10 not available");

    Chapter10Reader reader; // keep the opened Ch10 file alive across extraction
    QVERIFY(reader.loadChannels(toFsPath(filepath)));

    std::vector<StepDefinition> steps;
    std::string step_error;
    QVERIFY2(StepDetector::parseStepConfig(toFsPath(projectRootPath("settings/rcvr_cals/default.toml")),
                                           steps, step_error), step_error.c_str());
    QCOMPARE(steps.size(), 11);

    CalibrationExtractor::Request req;
    req.calFilename           = toFsPath(filepath);
    req.timeChannelId         = reader.getCurrentTimeChannelID();
    req.pcmChannelId          = reader.getFirstPCMChannelID();
    setAcquisition(req, "FE6B2840", 800, /*randomized*/ false, /*rate*/ 0.8, /*swap*/ true);
    // The shipped sequential word map (three words per receiver), which is what the
    // main run uses. NOT receiver_params/RASA.toml, despite the 800-bit frame
    // matching RASA's: RASA's stride-4 card layout treats words 3, 7, 11, 15 ... as
    // empty, which on this recording skips real channels and mis-attributes every
    // receiver above 1 - it reports live channels as dead with nothing pointing at
    // the map as the cause.
    req.receiverParamsToml    = toFsPath(projectRootPath("settings/receiver_params/default.toml"));
    req.numReceivers          = 16;
    req.receiverChannels      = 3;
    req.steps                 = steps;

    CalibrationExtractor extractor;
    QString summary;
    QVERIFY2(runExtraction(extractor, req, summary), qPrintable(summary));

    const std::vector<CalibrationChannelResult>& results = extractor.results();
    QCOMPARE(results.size(), req.numReceivers * req.receiverChannels);

    QSet<int> calibratedReceivers;
    int calibratedChannels = 0;
    for (const CalibrationChannelResult& r : results)
    {
        if (!r.profile.valid)
            continue;
        calibratedReceivers.insert(FrameSetup::receiverIndexFromName(nameOf(r).toStdString()));
        calibratedChannels++;

        // The regression this fixture exists for: every step pairs with its OWN
        // dwell. Noise used to split one high dwell into two plateaus a few tens of
        // counts apart. The split took a pairing slot, the real 0 dB level was
        // dropped as "pre-roll", and every step below the split read 6 dB low -
        // L/R_RCVR3 and R_RCVR6 by one step, L_RCVR6 (split twice) by two. The
        // profile still attached, so nothing reported it: the staircase simply sat
        // a step behind its neighbours, and noise across the split swung 6 dB.
        const std::vector<CalibrationPoint>& pts = r.profile.points;
        QVERIFY2(pts.front().rawAvg < 1000.0 && pts.front().trueDb == 0.0,
                 qPrintable(QString("%1: 0 dB paired with raw %2, not the pre-sweep level")
                                .arg(nameOf(r)).arg(pts.front().rawAvg)));
        for (int k = 1; k < pts.size(); k++)
        {
            // Genuine steps here are more than 1000 counts apart (the smallest is
            // ~1200, on receiver 5); a split dwell is tens.
            QVERIFY2(pts[k].rawAvg - pts[k - 1].rawAvg > 1000.0,
                     qPrintable(QString("%1: %2 dB and %3 dB are %4 counts apart - one dwell "
                                        "split in two")
                                    .arg(nameOf(r)).arg(pts[k - 1].trueDb).arg(pts[k].trueDb)
                                    .arg(pts[k].rawAvg - pts[k - 1].rawAvg)));
        }

        if (r.saturated)
        {
            // Receiver 1 alone: its gain is set too high for this sweep, so its top
            // two steps both sit on the 65472 rail and carry no measurement. It is
            // calibrated over the range it could still resolve, and readings hold
            // at the top of that range instead of being extrapolated past it.
            QCOMPARE(FrameSetup::receiverIndexFromName(nameOf(r).toStdString()), 1);
            QCOMPARE(pts.back().trueDb, 48.0);
            QVERIFY2(std::count(r.unresolvedDb.begin(), r.unresolvedDb.end(), 54.0) > 0
                         && std::count(r.unresolvedDb.begin(), r.unresolvedDb.end(), 60.0) > 0,
                     qPrintable(nameOf(r)));
            QCOMPARE(interpolateCalibration(65472.0, r.profile), 48.0);
            // Every step from 0 to 48 dB is measured - including L_RCVR1's 24 dB
            // dwell, whose plateau noise shatters. Left as a hole it was bridged
            // linearly from 18 to 30 dB and read 23.09 dB; the dwell grid locates
            // it so it is measured instead.
            QCOMPARE(pts.size(), steps.size() - 2);
            QCOMPARE(r.unresolvedDb, std::vector<double>({54.0, 60.0}));
        }
        else
        {
            QCOMPARE(pts.size(), steps.size());
            QCOMPARE(pts.back().trueDb, 60.0);
        }
    }

    // All three channels of receivers 1, 3, 5 and 6 carry the sweep - the operator's
    // own count - and no channel that carries none gets a false profile.
    QCOMPARE(calibratedChannels, 12);
    QCOMPARE(calibratedReceivers, (QSet<int>{1, 3, 5, 6}));

    // A receiver that carries no sweep says so plainly, not as a detection
    // shortfall: receiver 2 holds 0 for the whole recording.
    for (const CalibrationChannelResult& r : results)
    {
        if (FrameSetup::receiverIndexFromName(nameOf(r).toStdString()) == 2)
        {
            QCOMPARE(r.outcome, CalibrationOutcome::FlatNoSignal);
        }
    }

    // And the operator-facing report names the receivers, in the unit operators
    // actually talk in, and says where a saturated one stops being trustworthy.
    const QString report = summarizeReport(results, intSize(steps));
    QVERIFY2(report.contains("Calibrated: receivers 1, 3, 5, 6."), qPrintable(report));
    QVERIFY2(report.contains("Receiver 1") && report.contains("hold at 48 dB"), qPrintable(report));

    // And neither "check this" prompt fires on a correct setup. They exist to catch a
    // wrong step file or word map; a false alarm here would teach operators to ignore
    // them. (Receiver 1's saturated sweep holds FEWER levels than the file, not more.)
    QVERIFY2(!report.contains("Check the step file"), qPrintable(report));
    QVERIFY2(!report.contains("Check the word map"), qPrintable(report));
}

void TestCalibrationExtractor::summaryFlagsAShortStepFileAndAMismatchedWordMap()
{
    // summarize() is pure, so this needs no .ch10 fixture and always runs.
    auto calibrated = [](const QString& name, int sweepLevels) {
        CalibrationChannelResult r;
        setName(r, name);
        r.outcome       = CalibrationOutcome::Calibrated;
        r.hadData       = true;
        r.profile.valid = true;
        r.sweepLevels   = sweepLevels;
        return r;
    };
    auto flat = [](const QString& name) {
        CalibrationChannelResult r;
        setName(r, name);
        r.outcome       = CalibrationOutcome::FlatNoSignal;
        r.hadData       = true;
        r.plateauLevels = {0.0};
        return r;
    };
    const QStringList parts = {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C")};

    // An 8-step file on an 11-level sweep: every channel reports calibrated and every
    // step is paired with the wrong level. The report has to say so, because nothing
    // else will.
    std::vector<CalibrationChannelResult> shortFile;
    for (const QString& p : parts)
    {
        shortFile.push_back(calibrated(p + "_RCVR3", 11));
    }
    const QString shortReport = summarizeReport(shortFile, 8);
    QVERIFY2(shortReport.contains("Check the step file"), qPrintable(shortReport));
    QVERIFY2(shortReport.contains("Receiver 3 — 11 levels in the recording, 8 in the step file"),
             qPrintable(shortReport));

    // One extra level is the ordinary turn-on transient the detector is built to drop;
    // warning on it would fire on healthy recordings and teach operators to ignore it.
    std::vector<CalibrationChannelResult> transient;
    for (const QString& p : parts)
    {
        transient.push_back(calibrated(p + "_RCVR3", 9));
    }
    const QString transientReport = summarizeReport(transient, 8);
    QVERIFY2(!transientReport.contains("Check the step file"), qPrintable(transientReport));

    // One channel swept and two never moved: what a word map that does not match the
    // recording leaves behind, since it files one receiver's words under another's.
    const std::vector<CalibrationChannelResult> mixed{
        flat("L_RCVR2"), flat("R_RCVR2"), calibrated("C_RCVR2", 11)};
    const QString mixedReport = summarizeReport(mixed, 11);
    QVERIFY2(mixedReport.contains("Check the word map"), qPrintable(mixedReport));
    QVERIFY2(mixedReport.contains("Receiver 2 — C calibrated; L, R never moved"),
             qPrintable(mixedReport));

    // A receiver that is simply absent - every channel flat - is not a word-map hint;
    // most receivers in a real calibration recording look like that.
    std::vector<CalibrationChannelResult> absent;
    for (const QString& p : parts)
    {
        absent.push_back(flat(p + "_RCVR4"));
    }
    const QString absentReport = summarizeReport(absent, 11);
    QVERIFY2(!absentReport.contains("Check the word map"), qPrintable(absentReport));
}

void TestCalibrationExtractor::extractionLogsOneLinePerChannelWithData()
{
    // The log the dialog shows while it works: one line per channel that had samples, naming the
    // channel and its word, and saying how many plateaus were found against the steps expected.
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!buildStepCalRequest(reader, req, "settings/rcvr_cals/default.toml",
                             "settings/receiver_params/default.toml", skip))
        QSKIP(qPrintable(skip));

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    QVERIFY2(recorder.run(req), qPrintable(recorder.summary));
    const QStringList& log = recorder.log;

    int with_data = 0;
    for (const CalibrationChannelResult& r : extractor.results())
    {
        if (!r.hadData)
            continue;
        with_data++;
        const QString prefix = QString("  %1 (word %2): ").arg(nameOf(r)).arg(r.word);
        int lines = 0;
        for (const QString& line : log)
        {
            if (line.startsWith(prefix))
            {
                lines++;
                QVERIFY2(line.contains(QString("%1 step(s) expected").arg(intSize(req.steps))), qPrintable(line));
                QVERIFY2(line.contains(QString("%1 plateau(s) detected").arg(r.detectedPlateaus)), qPrintable(line));
            }
        }
        QVERIFY2(lines == 1, qPrintable(QString("%1: %2 log lines").arg(nameOf(r)).arg(lines)));
    }
    QVERIFY(with_data > 0);
}

void TestCalibrationExtractor::aNonAsciiChannelNameSurvivesExtractionItsLogAndItsReport()
{
    // A hand-written receiver-parameters TOML may name its channels anything. Here three live words
    // of step_cal are named with an accented letter, a Greek capital sigma and an omega, so the name
    // goes through the word map, the per-channel log line and the report.
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!buildStepCalRequest(reader, req, "settings/rcvr_cals/default.toml",
                             "settings/receiver_params/default.toml", skip))
        QSKIP(qPrintable(skip));

    const QStringList names = {QStringLiteral("R") + QChar(0x00E9) + QStringLiteral("cepteur ") + QChar(0x03A9),
                               QStringLiteral("Gain ") + QChar(0x03A3),
                               QStringLiteral("Plain")};
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString toml_path = dir.filePath("accent.toml");
    {
        QFile f(toml_path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        for (int i = 0; i < names.size(); i++)
        {
            const QByteArray section = "[" + names[i].toUtf8() + "]\nWord = " + QByteArray::number(i + 1) + "\n";
            QVERIFY(f.write(section) == section.size());
        }
    }
    req.receiverParamsToml = toFsPath(toml_path);

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    QVERIFY2(recorder.run(req), qPrintable(recorder.summary));
    const QStringList& log = recorder.log;

    const std::vector<CalibrationChannelResult>& results = extractor.results();
    QCOMPARE(int(results.size()), 3);
    for (int i = 0; i < 3; i++)
    {
        QCOMPARE(nameOf(results[i]), names[i]);
        QVERIFY(results[i].hadData);
        const QString prefix = QString("  %1 (word %2): ").arg(names[i]).arg(results[i].word);
        int lines = 0;
        for (const QString& line : log)
            lines += line.startsWith(prefix) ? 1 : 0;
        QVERIFY2(lines == 1, qPrintable(QString("%1: %2 log lines").arg(names[i]).arg(lines)));
    }

    // None carries "_RCVR<N>", so the report names them rather than numbering a receiver.
    const QString report = summarizeReport(results, intSize(req.steps));
    for (const QString& name : names)
        QVERIFY2(report.contains(name), qPrintable(report));
    QVERIFY2(report.contains("Unnamed receiver"), qPrintable(report));
}

/// A step_cal request, or skip the test. Shared by the lifecycle tests below.
static bool stepCalRequestOrSkip(Chapter10Reader& reader, CalibrationExtractor::Request& req, QString& skip)
{
    return buildStepCalRequest(reader, req, "settings/rcvr_cals/default.toml",
                               "settings/receiver_params/default.toml", skip);
}

void TestCalibrationExtractor::anExtractionWithNoStepsFailsBeforeItStarts()
{
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!stepCalRequestOrSkip(reader, req, skip))
        QSKIP(qPrintable(skip));
    req.steps.clear();

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    QVERIFY(!recorder.run(req));
    QVERIFY(recorder.done);
    QCOMPARE(recorder.summary, QString("No calibration steps were provided."));
    QCOMPARE(extractor.error(), QString("No calibration steps were provided."));
    QVERIFY(extractor.results().empty());
    // Nothing was started, so nothing was reported but the end.
    QCOMPARE(recorder.events, QStringList{"finished"});
}

void TestCalibrationExtractor::aWordMapThatCannotBeUsedIsReportedBeforeAnythingRuns()
{
    QFETCH(QString, scenario);
    QFETCH(QString, expected);

    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!stepCalRequestOrSkip(reader, req, skip))
        QSKIP(qPrintable(skip));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString toml = dir.filePath("map.toml");
    if (scenario == "missing")
    {
        // no file is written
    }
    else
    {
        QFile f(toml);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray text = (scenario == "word-outside-frame")
                                    ? QByteArray("[L_RCVR1]\nWord = 9999\n")
                                    : QByteArray("[Parameters]\nPolarity = 0\n");   // no word map at all
        QVERIFY(f.write(text) == text.size());
    }
    req.receiverParamsToml = toFsPath(toml);

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    QVERIFY(!recorder.run(req));
    QCOMPARE(recorder.summary, expected);
    QCOMPARE(extractor.error(), expected);
    QCOMPARE(recorder.events, QStringList{"finished"});
}

void TestCalibrationExtractor::aWordMapThatCannotBeUsedIsReportedBeforeAnythingRuns_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<QString>("expected");
    QTest::newRow("missing")            << "missing" << "Receiver Parameters file 'map.toml' was not found.";
    QTest::newRow("word-outside-frame") << "word-outside-frame" << "Failed to load Receiver Parameters word map. Check the file.";
    QTest::newRow("no-parameters")      << "no-parameters" << "Receiver Parameters file contains no parameters.";
}

void TestCalibrationExtractor::aDefaultWordMapThatDoesNotFitTheFrameIsReported()
{
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!stepCalRequestOrSkip(reader, req, skip))
        QSKIP(qPrintable(skip));
    req.receiverParamsToml.clear();     // no file: the sequential grid is built from the counts
    req.numReceivers     = 40;
    req.receiverChannels = 3;           // 120 words into an 800-bit (50-word) frame

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    QVERIFY(!recorder.run(req));
    QVERIFY2(!recorder.summary.isEmpty(), "a reason is given");
    QCOMPARE(extractor.error(), recorder.summary);
    QCOMPARE(recorder.events, QStringList{"finished"});
    QCOMPARE(recorder.summary,
             QString("Num Receivers x Receiver Channels exceeds the words available in the minor frame."));
}

void TestCalibrationExtractor::aRecordingThatCannotBeOpenedIsReported()
{
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!stepCalRequestOrSkip(reader, req, skip))
        QSKIP(qPrintable(skip));
    const QString gone = QDir::tempPath() + "/tst_calext_missing.ch10";
    QVERIFY(!QFileInfo::exists(gone));
    req.calFilename = toFsPath(gone);

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    QVERIFY(!recorder.run(req));
    QVERIFY(extractor.results().empty());
    QCOMPARE(recorder.summary, QString("Error opening data file."));
    QCOMPARE(extractor.error(), recorder.summary);
    QCOMPARE(recorder.events, QStringList{"finished"});
}

void TestCalibrationExtractor::aCancelledExtractionSaysSoAndLeavesNoResults()
{
    // Cancelled from the first report that arrives, which is while the (second-long) extraction
    // is still reading the recording.
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!stepCalRequestOrSkip(reader, req, skip))
        QSKIP(qPrintable(skip));

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    recorder.cancelOnFirstReport = true;
    QVERIFY(!recorder.run(req));
    QVERIFY(recorder.done);
    QCOMPARE(recorder.summary, QString("Calibration extraction cancelled."));
    QVERIFY(extractor.results().empty());
}

void TestCalibrationExtractor::startingWhileRunningIsIgnoredAndAnExtractorCanRunAgain()
{
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!stepCalRequestOrSkip(reader, req, skip))
        QSKIP(qPrintable(skip));

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    recorder.start(req);
    recorder.start(req);           // already running: ignored, not a second run
    QVERIFY(recorder.wait());
    QVERIFY(recorder.success);
    QCOMPARE(recorder.events.count("finished"), 1);
    QCOMPARE(recorder.log.filter("Calibration extraction sample period").size(), 1);
    const size_t first_results = extractor.results().size();
    QVERIFY(first_results > 0);

    // A finished extractor starts a fresh run, and its results replace the last one's.
    Recorder again(extractor);
    QVERIFY2(again.run(req), qPrintable(again.summary));
    QCOMPARE(extractor.results().size(), first_results);
    QCOMPARE(again.events.count("finished"), 1);
}

void TestCalibrationExtractor::aRunBeginsWithItsSamplePeriodAndSummarisesWhatItCalibrated()
{
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!stepCalRequestOrSkip(reader, req, skip))
        QSKIP(qPrintable(skip));

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    QVERIFY2(recorder.run(req), qPrintable(recorder.summary));

    // The first thing reported is the extraction window it chose.
    QVERIFY(!recorder.log.isEmpty());
    QCOMPARE(recorder.log.first(),
             QString("Calibration extraction sample period: 100.00 ms (~100 frames/window at 800-bit frames)."));

    // The message that comes with a success counts the channels that calibrated.
    int calibrated = 0;
    for (const CalibrationChannelResult& r : extractor.results())
        calibrated += r.profile.valid ? 1 : 0;
    QCOMPARE(recorder.summary, QString("Calibrated %1 of %2 channel(s).").arg(calibrated).arg(int(extractor.results().size())));
    QVERIFY(extractor.error().empty());
}

void TestCalibrationExtractor::theEndIsAlwaysTheLastReportAndOnlyAFailureIsAnError()
{
    // New with the plain extractor: it waits for its reader and worker before it reports the end,
    // so the end really is last. The QObject version finished while the reader's last progress
    // report was still queued behind it (the progress test above waits for it for that reason).
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!stepCalRequestOrSkip(reader, req, skip))
        QSKIP(qPrintable(skip));

    {
        CalibrationExtractor extractor;
        Recorder recorder(extractor);
        QVERIFY2(recorder.run(req), qPrintable(recorder.summary));
        QCOMPARE(recorder.events.last(), QString("finished"));
        QCOMPARE(recorder.events.count("finished"), 1);
        QVERIFY(recorder.errors.isEmpty());                       // a success reports no error
        QVERIFY(!recorder.progress.isEmpty());
        QCOMPARE(recorder.progress.last(), 100);                  // ...and the last progress came first
    }
    {
        CalibrationExtractor extractor;
        Recorder recorder(extractor);
        recorder.cancelOnFirstReport = true;
        QVERIFY(!recorder.run(req));
        QCOMPARE(recorder.events.last(), QString("finished"));
        QVERIFY(recorder.errors.isEmpty());                       // a cancel is not an error
    }
    {
        CalibrationExtractor::Request bad = req;
        bad.steps.clear();
        CalibrationExtractor extractor;
        Recorder recorder(extractor);
        QVERIFY(!recorder.run(bad));
        QCOMPARE(recorder.errors, QStringList{recorder.summary});  // the reason, once, before the end
    }
}

void TestCalibrationExtractor::aWorkerFailureIsReportedWithItsOwnReasonAsAnError()
{
    // The wrong byte order destroys the sync pattern, so the worker fails with its own reason
    // ("...pattern was not found..."), which is what the run then reports - not a generic message.
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!buildRealRequest(reader, req, skip))
        QSKIP(qPrintable(skip));
    req.acquisition.swapBytes = false;

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    QVERIFY(!recorder.run(req));
    QVERIFY2(!recorder.summary.isEmpty(), "a reason is given");
    QVERIFY2(recorder.summary != "Calibration extraction failed.", qPrintable(recorder.summary));
    QCOMPARE(QString::fromStdString(extractor.error()), recorder.summary);
    QCOMPARE(recorder.errors, QStringList{recorder.summary});
    QCOMPARE(recorder.events.last(), QString("finished"));
    // The worker's own message also went out as a log line, as it always has.
    QVERIFY2(recorder.log.contains(recorder.summary), qPrintable(recorder.log.join("\n")));
}

void TestCalibrationExtractor::cancelCanComeFromAnotherThread()
{
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!stepCalRequestOrSkip(reader, req, skip))
        QSKIP(qPrintable(skip));

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    recorder.start(req);
    // The extraction takes about a second; cancel a tenth of one in, from a thread that is not
    // the caller's and not the extraction's.
    std::thread canceller([&extractor]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        extractor.cancel();
    });
    const bool finished = recorder.wait();
    canceller.join();
    QVERIFY(finished);
    QVERIFY(!recorder.success);
    QCOMPARE(recorder.summary, QString("Calibration extraction cancelled."));
}

void TestCalibrationExtractor::destroyingARunningExtractorCancelsItAndWaits()
{
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!stepCalRequestOrSkip(reader, req, skip))
        QSKIP(qPrintable(skip));

    // The object the reports are delivered to must outlive the extractor's threads (the adapter's
    // contract), so it is made first and the extractor's scope is inside it.
    QObject target;
    QElapsedTimer timer;
    timer.start();
    {
        CalibrationExtractor extractor;
        extractor.start(req, std::make_shared<QtProcessingEvents>(&target, nullptr, nullptr, nullptr, nullptr));
        QVERIFY(extractor.running());
        // Leave the scope with the run in flight: the destructor has to stop the extractor's
        // threads and wait for them, not leave them running on freed state.
    }
    QVERIFY2(timer.elapsed() < 30000, "destroying a running extractor did not return promptly");
    QCoreApplication::processEvents();
}

void TestCalibrationExtractor::realStepCalWithShortStepFileIsFlagged()
{
    // The real trap this guard exists for. rcvr_cals/RASA.toml lists 8 steps (0, 3,
    // 6, 12 ... 36 dB); this recording steps through 11 (0 to 60 dB). Extraction
    // "succeeds" on all twelve swept channels while pairing every one of them with
    // the wrong level - which is exactly how it first reported "12 of 12".
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!buildStepCalRequest(reader, req, "settings/rcvr_cals/RASA.toml",
                             "settings/receiver_params/default.toml", skip))
        QSKIP(qPrintable(skip));

    CalibrationExtractor extractor;
    QString summary;
    QVERIFY2(runExtraction(extractor, req, summary), qPrintable(summary));

    const QString report = summarizeReport(extractor.results(), intSize(req.steps));
    QVERIFY2(report.contains("Check the step file"), qPrintable(report));
    QVERIFY2(report.contains("Receiver 3 — 11 levels in the recording, 8 in the step file"),
             qPrintable(report));
}

void TestCalibrationExtractor::realStepCalWithWrongWordMapIsFlagged()
{
    // The other real trap. This recording lays its receivers out three words apiece;
    // receiver_params/RASA.toml assumes four-word cards with an unused fourth word, so
    // it files words under the wrong receivers and reports live channels as dead with
    // nothing pointing at the map. The report now points at it.
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!buildStepCalRequest(reader, req, "settings/rcvr_cals/default.toml",
                             "settings/receiver_params/RASA.toml", skip))
        QSKIP(qPrintable(skip));

    CalibrationExtractor extractor;
    QString summary;
    QVERIFY2(runExtraction(extractor, req, summary), qPrintable(summary));

    const QString report = summarizeReport(extractor.results(), intSize(req.steps));
    QVERIFY2(report.contains("Check the word map"), qPrintable(report));
}

void TestCalibrationExtractor::extractionReportsItsProgress()
{
    // The Extract Calibration dialog shows a progress dialog fed by progressChanged(), which
    // the extractor relays from the packet reader. Nothing else observed it, so a break in
    // that relay would leave the dialog at 0% for the whole extraction.
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!buildStepCalRequest(reader, req, "settings/rcvr_cals/default.toml",
                             "settings/receiver_params/default.toml", skip))
        QSKIP(qPrintable(skip));

    CalibrationExtractor extractor;
    Recorder recorder(extractor);
    QVERIFY2(recorder.run(req), qPrintable(recorder.summary));
    const QList<int>& progress = recorder.progress;

    // The reader's last report (100) is queued by a thread that is still ending when the
    // extraction's own finish is delivered, so it may land just after - wait for it.
    QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty() && progress.last() == 100, 5000);
    for (int i = 0; i < progress.size(); ++i)
    {
        QVERIFY(progress.at(i) >= 0 && progress.at(i) <= 100);
        if (i > 0)
            QVERIFY2(progress.at(i) >= progress.at(i - 1), "progress went backwards");
    }
}

void TestCalibrationExtractor::clipsBeyondTheRecordingAreReported()
{
    // Clip Start + Clip End that add up to more than the recording are ignored and the
    // whole recording is used - so the result looks fine while the values the operator
    // typed did nothing. The extractor now says so, and the dialog shows it.
    Chapter10Reader reader;
    CalibrationExtractor::Request req;
    QString skip;
    if (!buildStepCalRequest(reader, req, "settings/rcvr_cals/default.toml",
                             "settings/receiver_params/default.toml", skip))
        QSKIP(qPrintable(skip));

    auto calibratedCount = [](const CalibrationExtractor& e) {
        int n = 0;
        for (const CalibrationChannelResult& r : e.results())
        {
            n += r.profile.valid ? 1 : 0;
        }
        return n;
    };

    // 6 s + 60 s on a ~65 s recording: nothing would be left, so both are ignored.
    req.clipStartSec = 6.0;
    req.clipEndSec   = 60.0;
    CalibrationExtractor ignored;
    Recorder ignored_recorder(ignored);
    QVERIFY2(ignored_recorder.run(req), qPrintable(ignored_recorder.summary));
    QString summary;
    QVERIFY(ignored.clipIgnored());
    QVERIFY2(ignored.recordingSeconds() > 60.0 && ignored.recordingSeconds() < 70.0,
             qPrintable(QString::number(ignored.recordingSeconds())));
    QCOMPARE(calibratedCount(ignored), 12); // the whole recording was used
    // ...and it says so, with the clip total and the recording's length, to one decimal.
    const QString warning = QString("Clip Start + Clip End (66.0 s) exceed the recording (%1 s); "
                                    "both were ignored and the whole recording was used.")
                                .arg(ignored.recordingSeconds(), 0, 'f', 1);
    QVERIFY2(ignored_recorder.log.contains(warning), qPrintable(ignored_recorder.log.join("\n")));

    // 6 s + 2 s is honoured, and is not reported.
    req.clipEndSec = 2.0;
    CalibrationExtractor honoured;
    Recorder honoured_recorder(honoured);
    QVERIFY2(honoured_recorder.run(req), qPrintable(honoured_recorder.summary));
    QVERIFY(!honoured.clipIgnored());
    QVERIFY2(honoured_recorder.log.filter("Clip Start").isEmpty(), "an honoured clip is not reported");
    QCOMPARE(calibratedCount(honoured), 12);
}
