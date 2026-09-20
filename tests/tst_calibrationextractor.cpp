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
    if (!reader.loadChannels(filepath))
    {
        skipReason = "Could not load channels from step_cal, example.ch10";
        return false;
    }
    QVector<StepDefinition> steps;
    QString step_error;
    if (!StepDetector::parseStepConfig(projectRootPath(stepToml), steps, step_error))
    {
        skipReason = "Could not parse " + stepToml + ": " + step_error;
        return false;
    }
    req.calFilename           = filepath;
    req.timeChannelId         = reader.getCurrentTimeChannelID();
    req.pcmChannelId          = reader.getFirstPCMChannelID();
    req.sync.pattern          = "FE6B2840";
    req.sync.bitsInMinorFrame = 800;
    req.sync.randomized       = false;
    req.sync.dataRateMbps     = 0.8;
    req.swapBytes             = true;
    req.receiverParamsToml    = projectRootPath(receiverParams);
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
    QVERIFY(reader.loadChannels(filepath));

    QVector<StepDefinition> steps;
    QString step_error;
    QVERIFY2(StepDetector::parseStepConfig(projectRootPath("settings/rcvr_cals/default.toml"),
                                           steps, step_error), qPrintable(step_error));
    QCOMPARE(steps.size(), 11);

    CalibrationExtractor::Request req;
    req.calFilename           = filepath;
    req.timeChannelId         = reader.getCurrentTimeChannelID();
    req.pcmChannelId          = reader.getFirstPCMChannelID();
    req.sync.pattern          = "FE6B2840";
    req.sync.bitsInMinorFrame = 800;
    req.sync.randomized       = false;
    req.sync.dataRateMbps     = 0.8;
    req.swapBytes             = true;
    // The shipped sequential word map (three words per receiver), which is what the
    // main run uses. NOT receiver_params/RASA.toml, despite the 800-bit frame
    // matching RASA's: RASA's stride-4 card layout treats words 3, 7, 11, 15 ... as
    // empty, which on this recording skips real channels and mis-attributes every
    // receiver above 1 - it reports live channels as dead with nothing pointing at
    // the map as the cause.
    req.receiverParamsToml    = projectRootPath("settings/receiver_params/default.toml");
    req.numReceivers          = 16;
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
        if (!r.profile.valid)
            continue;
        calibratedReceivers.insert(FrameSetup::receiverIndexFromName(r.name));
        calibratedChannels++;

        // The regression this fixture exists for: every step pairs with its OWN
        // dwell. Noise used to split one high dwell into two plateaus a few tens of
        // counts apart. The split took a pairing slot, the real 0 dB level was
        // dropped as "pre-roll", and every step below the split read 6 dB low -
        // L/R_RCVR3 and R_RCVR6 by one step, L_RCVR6 (split twice) by two. The
        // profile still attached, so nothing reported it: the staircase simply sat
        // a step behind its neighbours, and noise across the split swung 6 dB.
        const QVector<CalibrationPoint>& pts = r.profile.points;
        QVERIFY2(pts.first().rawAvg < 1000.0 && pts.first().trueDb == 0.0,
                 qPrintable(QString("%1: 0 dB paired with raw %2, not the pre-sweep level")
                                .arg(r.name).arg(pts.first().rawAvg)));
        for (int k = 1; k < pts.size(); k++)
        {
            // Genuine steps here are more than 1000 counts apart (the smallest is
            // ~1200, on receiver 5); a split dwell is tens.
            QVERIFY2(pts[k].rawAvg - pts[k - 1].rawAvg > 1000.0,
                     qPrintable(QString("%1: %2 dB and %3 dB are %4 counts apart - one dwell "
                                        "split in two")
                                    .arg(r.name).arg(pts[k - 1].trueDb).arg(pts[k].trueDb)
                                    .arg(pts[k].rawAvg - pts[k - 1].rawAvg)));
        }

        if (r.saturated)
        {
            // Receiver 1 alone: its gain is set too high for this sweep, so its top
            // two steps both sit on the 65472 rail and carry no measurement. It is
            // calibrated over the range it could still resolve, and readings hold
            // at the top of that range instead of being extrapolated past it.
            QCOMPARE(FrameSetup::receiverIndexFromName(r.name), 1);
            QCOMPARE(pts.last().trueDb, 48.0);
            QVERIFY2(r.unresolvedDb.contains(54.0) && r.unresolvedDb.contains(60.0),
                     qPrintable(r.name));
            QCOMPARE(interpolateCalibration(65472.0, r.profile), 48.0);
            // Every step from 0 to 48 dB is measured - including L_RCVR1's 24 dB
            // dwell, whose plateau noise shatters. Left as a hole it was bridged
            // linearly from 18 to 30 dB and read 23.09 dB; the dwell grid locates
            // it so it is measured instead.
            QCOMPARE(pts.size(), steps.size() - 2);
            QCOMPARE(r.unresolvedDb, QVector<double>({54.0, 60.0}));
        }
        else
        {
            QCOMPARE(pts.size(), steps.size());
            QCOMPARE(pts.last().trueDb, 60.0);
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
        if (FrameSetup::receiverIndexFromName(r.name) == 2)
        {
            QCOMPARE(r.outcome, CalibrationOutcome::FlatNoSignal);
        }
    }

    // And the operator-facing report names the receivers, in the unit operators
    // actually talk in, and says where a saturated one stops being trustworthy.
    const QString report = CalibrationExtractor::summarize(results, steps.size());
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
        r.name          = name;
        r.outcome       = CalibrationOutcome::Calibrated;
        r.hadData       = true;
        r.profile.valid = true;
        r.sweepLevels   = sweepLevels;
        return r;
    };
    auto flat = [](const QString& name) {
        CalibrationChannelResult r;
        r.name          = name;
        r.outcome       = CalibrationOutcome::FlatNoSignal;
        r.hadData       = true;
        r.plateauLevels = {0.0};
        return r;
    };
    const QStringList parts = {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C")};

    // An 8-step file on an 11-level sweep: every channel reports calibrated and every
    // step is paired with the wrong level. The report has to say so, because nothing
    // else will.
    QVector<CalibrationChannelResult> shortFile;
    for (const QString& p : parts)
    {
        shortFile << calibrated(p + "_RCVR3", 11);
    }
    const QString shortReport = CalibrationExtractor::summarize(shortFile, 8);
    QVERIFY2(shortReport.contains("Check the step file"), qPrintable(shortReport));
    QVERIFY2(shortReport.contains("Receiver 3 — 11 levels in the recording, 8 in the step file"),
             qPrintable(shortReport));

    // One extra level is the ordinary turn-on transient the detector is built to drop;
    // warning on it would fire on healthy recordings and teach operators to ignore it.
    QVector<CalibrationChannelResult> transient;
    for (const QString& p : parts)
    {
        transient << calibrated(p + "_RCVR3", 9);
    }
    const QString transientReport = CalibrationExtractor::summarize(transient, 8);
    QVERIFY2(!transientReport.contains("Check the step file"), qPrintable(transientReport));

    // One channel swept and two never moved: what a word map that does not match the
    // recording leaves behind, since it files one receiver's words under another's.
    QVector<CalibrationChannelResult> mixed;
    mixed << flat("L_RCVR2") << flat("R_RCVR2") << calibrated("C_RCVR2", 11);
    const QString mixedReport = CalibrationExtractor::summarize(mixed, 11);
    QVERIFY2(mixedReport.contains("Check the word map"), qPrintable(mixedReport));
    QVERIFY2(mixedReport.contains("Receiver 2 — C calibrated; L, R never moved"),
             qPrintable(mixedReport));

    // A receiver that is simply absent - every channel flat - is not a word-map hint;
    // most receivers in a real calibration recording look like that.
    QVector<CalibrationChannelResult> absent;
    for (const QString& p : parts)
    {
        absent << flat(p + "_RCVR4");
    }
    const QString absentReport = CalibrationExtractor::summarize(absent, 11);
    QVERIFY2(!absentReport.contains("Check the word map"), qPrintable(absentReport));
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

    const QString report = CalibrationExtractor::summarize(extractor.results(), req.steps.size());
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

    const QString report = CalibrationExtractor::summarize(extractor.results(), req.steps.size());
    QVERIFY2(report.contains("Check the word map"), qPrintable(report));
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
    QString summary;
    QVERIFY2(runExtraction(ignored, req, summary), qPrintable(summary));
    QVERIFY(ignored.clipIgnored());
    QVERIFY2(ignored.recordingSeconds() > 60.0 && ignored.recordingSeconds() < 70.0,
             qPrintable(QString::number(ignored.recordingSeconds())));
    QCOMPARE(calibratedCount(ignored), 12); // the whole recording was used

    // 6 s + 2 s is honoured, and is not reported.
    req.clipEndSec = 2.0;
    CalibrationExtractor honoured;
    QVERIFY2(runExtraction(honoured, req, summary), qPrintable(summary));
    QVERIFY(!honoured.clipIgnored());
    QCOMPARE(calibratedCount(honoured), 12);
}
