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

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QSet>
#include <QtTest>

#include "calibrationextractor.h"
#include "calibrationprofile.h"
#include "chapter10reader.h"
#include "framesetup.h"
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

/// A minimal but non-empty step list, so a Request clears the "no steps" guard
/// and actually drives the reader/frame-setup path.
QVector<StepDefinition> dummySteps()
{
    QVector<StepDefinition> steps;
    for (double db : { 0.0, 6.0, 12.0 })
        steps.append(StepDefinition{ db });
    return steps;
}

/// Drives an extraction to completion, pumping a nested event loop until
/// finished() fires (mirrors how the production dialog waits). @return success.
bool runExtraction(CalibrationExtractor& extractor,
                   const CalibrationExtractor::Request& req, QString& summary)
{
    QEventLoop loop;
    bool ok = false;
    bool done = false;
    QObject::connect(&extractor, &CalibrationExtractor::finished, &loop,
                     [&](bool success, const QString& msg) {
                         ok = success;
                         summary = msg;
                         done = true;
                         loop.quit();
                     });
    extractor.start(req);
    if (!done)
        loop.exec();
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

    if (!reader.loadChannels(filepath))
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
    QVector<StepDefinition> steps;
    QString step_error;
    if (!StepDetector::parseStepConfig(step_toml, steps, step_error) || steps.isEmpty())
    {
        skipReason = "Could not parse step config: " + step_error;
        return false;
    }

    req.calFilename           = filepath;
    req.timeChannelId         = time_id;
    req.pcmChannelId          = pcm_id;
    req.sync.pattern          = "FE6B2840";
    req.sync.bitsInMinorFrame = 8000; // true minor-frame length for this recording
    req.sync.randomized       = true; // RNRZ-L
    req.sync.dataRateMbps     = 0.8;  // drives the adaptive extract period
    // An ordinary Chapter 10 recording, so it needs the byte swap. Stated rather than
    // inherited, because this flag must match whatever the main run uses - that is the
    // mirror invariant this suite exists to protect.
    req.swapBytes             = true;
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
    req.sync.pattern          = "FE6B2840";
    req.sync.bitsInMinorFrame = 8000;
    req.numReceivers          = 16;
    req.receiverChannels      = 3;
    req.steps                 = dummySteps(); // clear the empty-steps guard

    CalibrationExtractor extractor;
    QString summary;
    const bool ok = runExtraction(extractor, req, summary);

    // The reader can't open the file, so the run finishes unsuccessfully with a
    // recorded error and no per-channel results — no hang, no partial state.
    QVERIFY(!ok);
    QVERIFY(!extractor.error().isEmpty());
    QVERIFY(extractor.results().isEmpty());
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

    const QVector<CalibrationChannelResult>& results = extractor.results();

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
    req.swapBytes = false;

    CalibrationExtractor extractor;
    QString summary;
    const bool ok = runExtraction(extractor, req, summary);
    QVERIFY2(!ok, "the wrong byte order must not produce a calibration");

    req.swapBytes = true;
    CalibrationExtractor extractor_ok;
    QString summary_ok;
    QVERIFY2(runExtraction(extractor_ok, req, summary_ok), qPrintable(summary_ok));
}

void TestCalibrationExtractor::summaryNamesReceiversAndWhyTheyFellBack()
{
    // summarize() is pure, so this needs no .ch10 fixture and always runs.
    auto channel = [](const QString& name, CalibrationOutcome outcome,
                      int plateaus, bool hadData) {
        CalibrationChannelResult r;
        r.name             = name;
        r.outcome          = outcome;
        r.detectedPlateaus = plateaus;
        r.hadData          = hadData;
        r.profile.valid    = (outcome == CalibrationOutcome::Calibrated);
        return r;
    };

    QVector<CalibrationChannelResult> results;
    // Receiver 1: fully calibrated. Receiver 2: partly - C fell back on merged
    // steps. Receiver 3: fully calibrated. Receiver 4: absent from the recording.
    for (const QString& p : {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C")})
    {
        results << channel(p + "_RCVR1", CalibrationOutcome::Calibrated, 6, true);
    }
    results << channel("L_RCVR2", CalibrationOutcome::Calibrated, 6, true);
    results << channel("R_RCVR2", CalibrationOutcome::Calibrated, 6, true);
    results << channel("C_RCVR2", CalibrationOutcome::TooFewPlateaus, 4, true);
    for (const QString& p : {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C")})
    {
        results << channel(p + "_RCVR3", CalibrationOutcome::Calibrated, 6, true);
    }
    for (const QString& p : {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C")})
    {
        results << channel(p + "_RCVR4", CalibrationOutcome::NoData, 0, false);
    }

    const QString summary = CalibrationExtractor::summarize(results, 6);

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
    QVector<CalibrationChannelResult> unnamed;
    unnamed << channel("AGC_LEFT", CalibrationOutcome::NoMonotonicSweep, 7, true);
    const QString unnamedSummary = CalibrationExtractor::summarize(unnamed, 6);
    QVERIFY2(unnamedSummary.contains("AGC_LEFT"), qPrintable(unnamedSummary));
    QVERIFY2(unnamedSummary.contains("no monotonic sweep"), qPrintable(unnamedSummary));
}

void TestCalibrationExtractor::realStepCalRecordingCalibratesReceivers1356()
{
    // STEP_CAL_EXAMPLE.ch10: a real step-calibration recording whose receivers are
    // far out of alignment. Its known truth, from the operator who recorded it:
    // exactly 12 channels carry the cal sweep - L, R and C of receivers 1, 3, 5
    // and 6 - and nothing else does.
    //
    // Acquisition parameters (found by probing; nothing else locks): NRZ-L,
    // byte-swapped, 800-bit minor frames at 800 kbps on PCM channel 26.
    const QString filepath = testDataPath("test files/STEP_CAL_EXAMPLE.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("STEP_CAL_EXAMPLE.ch10 not available");

    Chapter10Reader reader; // keep the opened Ch10 file alive across extraction
    QVERIFY(reader.loadChannels(filepath));

    QVector<StepDefinition> steps;
    QString step_error;
    QVERIFY2(StepDetector::parseStepConfig(projectRootPath("settings/rcvr_cals/RASA.toml"),
                                           steps, step_error), qPrintable(step_error));
    QCOMPARE(steps.size(), 8);

    CalibrationExtractor::Request req;
    req.calFilename           = filepath;
    req.timeChannelId         = reader.getCurrentTimeChannelID();
    req.pcmChannelId          = reader.getFirstPCMChannelID();
    req.sync.pattern          = "FE6B2840";
    req.sync.bitsInMinorFrame = 800;
    req.sync.randomized       = false;
    req.sync.dataRateMbps     = 0.8;
    req.swapBytes             = true;
    // SEQUENTIAL word map (empty receiverParamsToml -> words 0,1,2 / 3,4,5 / ...).
    // NOT receiver_params/RASA.toml, despite the 800-bit frame matching RASA's:
    // RASA's stride-4 card layout treats words 3, 7, 11, 15 ... as empty, which on
    // this recording skips real channels (receiver 3's word 7, receiver 6's word
    // 15) and mis-attributes everything above receiver 1 - it reports live
    // channels as dead with nothing pointing at the map as the cause.
    req.receiverParamsToml    = QString();
    req.numReceivers          = 12;
    req.receiverChannels      = 3;
    req.steps                 = steps;

    CalibrationExtractor extractor;
    QString summary;
    QVERIFY2(runExtraction(extractor, req, summary), qPrintable(summary));

    const QVector<CalibrationChannelResult>& results = extractor.results();
    QCOMPARE(results.size(), req.numReceivers * req.receiverChannels);

    QSet<int> calibratedReceivers;
    int calibratedChannels = 0;
    for (const CalibrationChannelResult& r : results)
    {
        if (r.profile.valid)
        {
            calibratedReceivers.insert(FrameSetup::receiverIndexFromName(r.name));
            calibratedChannels++;
        }
    }

    // Exactly the known truth: all three channels of receivers 1, 3, 5 and 6, and
    // no false profile on any of the 24 channels that carry no sweep.
    QCOMPARE(calibratedChannels, 12);
    QCOMPARE(calibratedReceivers, (QSet<int>{1, 3, 5, 6}));

    // The regression this fixture exists for. R_RCVR1 and C_RCVR1 carry clean
    // 8-step sweeps, but noise split two dwells into plateau pairs 1-3 raw counts
    // apart; before plateau coalescing, the sweep selector read those wobbles as
    // "no real transition", severed the run mid-sweep, and both fell back to
    // linear - 10 of 12 instead of 12.
    for (const CalibrationChannelResult& r : results)
    {
        if (r.name == QLatin1String("R_RCVR1") || r.name == QLatin1String("C_RCVR1"))
        {
            QVERIFY2(r.profile.valid, qPrintable(r.name + ": "
                     + QString::fromLatin1(calibrationOutcomeText(r.outcome))));
        }
        // A receiver that carries no sweep must say so plainly, not report a
        // detection shortfall: receiver 2 holds 0 for the whole recording.
        if (FrameSetup::receiverIndexFromName(r.name) == 2)
        {
            QCOMPARE(r.outcome, CalibrationOutcome::FlatNoSignal);
        }
    }

    // And the operator-facing report names the receivers, in the unit operators
    // actually talk in.
    const QString report = CalibrationExtractor::summarize(results, steps.size());
    QVERIFY2(report.contains("Calibrated: receivers 1, 3, 5, 6."), qPrintable(report));
}
