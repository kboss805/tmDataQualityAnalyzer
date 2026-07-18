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
    const QString filepath = testDataPath("agc_rnrz-l_trc_testfile.ch10");
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
