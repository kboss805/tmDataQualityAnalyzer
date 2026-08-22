/**
 * @file tst_frameprocessor.cpp
 * @brief Implementation of FrameProcessor unit tests (in-memory accumulation).
 */

#include "tst_frameprocessor.h"

#include <algorithm>
#include <cmath>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QSignalSpy>
#include <QtConcurrent>
#include <QtTest>
#include <QVector>

#include "calibrationextractor.h"
#include "calibrationprofile.h"
#include "ch10packetreader.h"
#include "chapter10reader.h"
#include "constants.h"
#include "frameprocessor.h"
#include "framesetup.h"
#include "packetqueue.h"
#include "processedstreamdata.h"
#include "stepdetector.h"

/// Helper: resolves a path inside tests/data/ relative to the test executable.
static QString testDataPath(const QString& filename)
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    return dir.filePath("data/" + filename);
}

/// Helper: builds a ProcessingParams with common test defaults.
static ProcessingParams makeTestParams(const QString& filename = {},
                                        int time_channel_id = -1,
                                        int pcm_channel_id = -1,
                                        uint64_t frame_sync = 0xFE6B2840,
                                        int sync_len = 32,
                                        int words_in_frame = 49,
                                        int bits_in_frame = 800)
{
    ProcessingParams p;
    p.filename = filename;
    p.timeChannelId = time_channel_id;
    p.pcmChannelId = pcm_channel_id;
    p.frameSync = frame_sync;
    p.syncPatternLength = sync_len;
    p.wordsInMinorFrame = words_in_frame;
    p.bitsInMinorFrame = bits_in_frame;
    return p;
}

/// Runs FrameProcessor through the full reader+queue pipeline synchronously.
/// Mirrors what ProcessingCoordinator does: creates a PacketQueue, calls
/// Ch10PacketReader::prepare() to resolve TMATS attributes, drives the reader
/// on a background thread while fp.process() consumes on the calling thread.
static bool runWithReader(FrameProcessor& fp, ProcessingParams& p, FrameSetup* setup)
{
    PacketQueue queue;
    p.packetQueue = &queue;

    Ch10PacketReader reader;
    QVector<ProcessingParams*> params_list = { &p };
    QString error;
    if (!reader.prepare(p.filename, p.timeChannelId, params_list, error))
        return false;

    QFuture<void> future = QtConcurrent::run([&reader]() { reader.run(); });
    bool ok = fp.process(p, setup);
    future.waitForFinished();
    return ok;
}

/// Helper: packs a string of '1'/'0' characters MSB-first into bytes, for
/// building synthetic PCM bitstreams without a real Chapter 10 file.
static QByteArray packBitString(const QString& bits)
{
    QByteArray data((bits.size() + 7) / 8, '\0');
    for (int i = 0; i < bits.size(); i++)
    {
        if (bits[i] == QChar('1'))
        {
            data[i / 8] = static_cast<char>(data[i / 8] | (0x80 >> (i % 8)));
        }
    }
    return data;
}

/// Helper: loads the default frame setup (word map) from
/// settings/receiver_params/default.toml.
static bool loadDefaultFrameSetup(FrameSetup& setup)
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();  // tests/
    dir.cdUp();  // project root
    QString toml_path = dir.filePath("settings/receiver_params/default.toml");
    if (!QFileInfo::exists(toml_path))
    {
        return false;
    }
    // 16 receivers * 3 channels = 48 data words + 1 sync word = 49
    return setup.tryLoadingFile(toml_path, 49);
}

/// Helper: enables all params with the given slope/offset calibration.
static bool setupParams(FrameSetup& setup, double slope, double offset)
{
    if (!loadDefaultFrameSetup(setup))
        return false;
    for (int i = 0; i < setup.length(); i++)
    {
        ParameterInfo* param = setup.getParameter(i);
        param->is_enabled = true;
        param->slope = slope;
        param->scale = offset;
        param->sample_sum = 0.0;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
//                          CONSTRUCTOR / ABORT                               //
////////////////////////////////////////////////////////////////////////////////

void TestFrameProcessor::constructorDefaults()
{
    FrameProcessor fp;
    fp.requestAbort();
    QVERIFY(true);
}

void TestFrameProcessor::requestAbortSetsFlag()
{
    FrameProcessor fp;
    QSignalSpy log_spy(&fp, &FrameProcessor::logMessage);
    fp.requestAbort();
    QVERIFY(true);
}

////////////////////////////////////////////////////////////////////////////////
//                         STATIC METHOD TESTS                                //
////////////////////////////////////////////////////////////////////////////////

void TestFrameProcessor::derandomizeShortBufferIdentity()
{
    QByteArray data(1, '\xAB');
    QByteArray original = data;
    uint16_t lfsr = 0;
    FrameProcessor::derandomizeBitstream(reinterpret_cast<uint8_t*>(data.data()), 8, lfsr);
    QCOMPARE(data[0], original[0]);
}

void TestFrameProcessor::derandomizeLongerBufferChanges()
{
    QByteArray data(4, '\xFF');
    QByteArray original = data;
    uint16_t lfsr = 0;
    FrameProcessor::derandomizeBitstream(reinterpret_cast<uint8_t*>(data.data()), 32, lfsr);
    QVERIFY2(data != original, "32 bits of all-1s should change after derandomization");
}

////////////////////////////////////////////////////////////////////////////////
//                          PROCESS ERROR PATHS                               //
////////////////////////////////////////////////////////////////////////////////

void TestFrameProcessor::processInvalidTimeChannel()
{
    FrameProcessor fp;
    QSignalSpy error_spy(&fp, &FrameProcessor::errorOccurred);
    QSignalSpy finished_spy(&fp, &FrameProcessor::processingFinished);

    FrameSetup setup;
    ProcessingParams p = makeTestParams("dummy.ch10", -1, 1);
    p.startSeconds = 0;
    p.stopSeconds = 100;
    p.samplePeriodSec = 1.0;
    QVERIFY(!fp.process(p, &setup));
    QVERIFY(!error_spy.isEmpty());
    QVERIFY(!finished_spy.isEmpty());
    QCOMPARE(finished_spy.last().at(0).toBool(), false);
}

void TestFrameProcessor::processInvalidPcmChannel()
{
    FrameProcessor fp;
    QSignalSpy error_spy(&fp, &FrameProcessor::errorOccurred);
    QSignalSpy finished_spy(&fp, &FrameProcessor::processingFinished);

    FrameSetup setup;
    ProcessingParams p = makeTestParams("dummy.ch10", 1, -1);
    p.startSeconds = 0;
    p.stopSeconds = 100;
    p.samplePeriodSec = 1.0;
    QVERIFY(!fp.process(p, &setup));
    QVERIFY(!error_spy.isEmpty());
    QVERIFY(!finished_spy.isEmpty());
    QCOMPARE(finished_spy.last().at(0).toBool(), false);
}

void TestFrameProcessor::processInvalidFile()
{
    FrameProcessor fp;
    QSignalSpy error_spy(&fp, &FrameProcessor::errorOccurred);

    FrameSetup setup;
    ProcessingParams p = makeTestParams("nonexistent_file.ch10", 1, 1);
    p.startSeconds = 0;
    p.stopSeconds = 100;
    p.samplePeriodSec = 1.0;
    QVERIFY(!fp.process(p, &setup));
    QVERIFY(!error_spy.isEmpty());
}

////////////////////////////////////////////////////////////////////////////////
//                       IN-MEMORY ACCUMULATION                               //
////////////////////////////////////////////////////////////////////////////////

void TestFrameProcessor::processAccumulatesReceiverData()
{
    const QString filepath = testDataPath("agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    int pcm_id = reader.getFirstPCMChannelID();
    int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    FrameSetup setup;
    if (!setupParams(setup, 1.0, 0.0))
        QSKIP("Could not load default frame setup");

    uint64_t start_secs = 0;
    uint64_t stop_secs = UINT64_MAX;

    ProcessingParams p = makeTestParams(filepath, time_id, pcm_id);
    p.startSeconds = start_secs;
    p.stopSeconds = stop_secs;
    p.samplePeriodSec = 1.0;
    p.isRandomized = true;
    p.mode = StreamMode::ReceiverChannelInfo;
    p.streamLabel = "Ch test";

    FrameProcessor fp;
    QVERIFY2(runWithReader(fp, p, &setup), "Processing should succeed on valid RNRZ-L file");

    const ProcessedStreamData& r = fp.result();
    QVERIFY2(r.hasSamples(), "Result must contain at least one time sample");
    // Receiver (SNR) streams are always considered locked (they come from a
    // separate piece of equipment), so no lock/error series is produced.
    QVERIFY2(r.lockPercent.isEmpty(), "Receiver mode must produce no lock series");
    QVERIFY2(r.accumulatedMissedFrames.isEmpty(), "Receiver mode must produce no missed-frames series");
    QCOMPARE(r.channels.size(), setup.length());
    QCOMPARE(r.streamLabel, QString("Ch test"));
    QCOMPARE(r.mode, StreamMode::ReceiverChannelInfo);

    // Every channel's value vector is parallel to the time vector.
    for (const auto& ch : r.channels)
    {
        QCOMPARE(ch.values.size(), r.timesSec.size());
    }
    // Timestamps are non-decreasing.
    for (int i = 1; i < r.timesSec.size(); i++)
    {
        QVERIFY(r.timesSec[i] >= r.timesSec[i - 1]);
    }
}

void TestFrameProcessor::processLockOnlyModeHasNoChannels()
{
    const QString filepath = testDataPath("agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    int pcm_id = reader.getFirstPCMChannelID();
    int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    uint64_t start_secs = 0;
    uint64_t stop_secs = UINT64_MAX;

    ProcessingParams p = makeTestParams(filepath, time_id, pcm_id);
    p.startSeconds = start_secs;
    p.stopSeconds = stop_secs;
    p.samplePeriodSec = 1.0;
    p.isRandomized = true;
    p.mode = StreamMode::FrameSyncLockStats;

    FrameSetup empty_setup;  // No word map for lock-only mode.
    FrameProcessor fp;
    QVERIFY2(runWithReader(fp, p, &empty_setup), "Lock-only processing should succeed");

    const ProcessedStreamData& r = fp.result();
    QVERIFY2(r.hasSamples(), "Lock-only result must contain time samples");
    QVERIFY2(r.channels.isEmpty(), "Lock-only mode must produce no receiver channels");
    QCOMPARE(r.lockPercent.size(), r.timesSec.size());
    QCOMPARE(r.accumulatedMissedFrames.size(), r.timesSec.size());
    QCOMPARE(r.mode, StreamMode::FrameSyncLockStats);
}

void TestFrameProcessor::processFrameSyncErrorsMonotonic()
{
    const QString filepath = testDataPath("agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    int pcm_id = reader.getFirstPCMChannelID();
    int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    uint64_t start_secs = 0;
    uint64_t stop_secs = UINT64_MAX;

    ProcessingParams p = makeTestParams(filepath, time_id, pcm_id);
    p.startSeconds = start_secs;
    p.stopSeconds = stop_secs;
    p.samplePeriodSec = 1.0;
    p.isRandomized = true;
    p.mode = StreamMode::FrameSyncLockStats;

    FrameSetup empty_setup;
    FrameProcessor fp;
    QVERIFY2(runWithReader(fp, p, &empty_setup), "Lock-only processing should succeed");

    const ProcessedStreamData& r = fp.result();
    QVERIFY2(r.hasSamples(), "Result must contain time samples");
    QCOMPARE(r.accumulatedMissedFrames.size(), r.timesSec.size());

    // The accumulated count starts at >= 0 and never decreases.
    QVERIFY(r.accumulatedMissedFrames.first() >= 0.0);
    for (int i = 1; i < r.accumulatedMissedFrames.size(); i++)
    {
        QVERIFY2(r.accumulatedMissedFrames[i] >= r.accumulatedMissedFrames[i - 1],
                 "Frame sync error accumulation must be monotonically non-decreasing");
    }
}

void TestFrameProcessor::processSlopeAffectsValues()
{
    const QString filepath = testDataPath("agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    int pcm_id = reader.getFirstPCMChannelID();
    int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    uint64_t start_secs = 0;
    uint64_t stop_secs = UINT64_MAX;

    auto run = [&](double slope) -> double {
        FrameSetup setup;
        if (!setupParams(setup, slope, 0.0))
            return 0.0;
        ProcessingParams p = makeTestParams(filepath, time_id, pcm_id);
        p.startSeconds = start_secs;
        p.stopSeconds = stop_secs;
        p.samplePeriodSec = 1.0;
        p.isRandomized = true;
        FrameProcessor fp;
        if (!runWithReader(fp, p, &setup) || fp.result().channels.isEmpty()
            || fp.result().channels[0].values.isEmpty())
            return 0.0;
        return fp.result().channels[0].values.first();
    };

    double v1 = run(1.0);
    double v2 = run(2.0);
    if (qAbs(v1) < 1e-10)
        QSKIP("First AGC sample is zero — cannot test slope ratio");
    QVERIFY2(qAbs(v2 - 2.0 * v1) < 1e-6,
             qPrintable(QString("Expected v2 (%1) = 2 * v1 (%2)").arg(v2).arg(v1)));
}

void TestFrameProcessor::processShortPeriodMoreSamples()
{
    const QString filepath = testDataPath("agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    int pcm_id = reader.getFirstPCMChannelID();
    int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    uint64_t start_secs = 0;
    uint64_t stop_secs = UINT64_MAX;

    auto countAt = [&](double period) -> int {
        FrameSetup setup;
        if (!setupParams(setup, 1.0, 0.0))
            return -1;
        ProcessingParams p = makeTestParams(filepath, time_id, pcm_id);
        p.startSeconds = start_secs;
        p.stopSeconds = stop_secs;
        p.samplePeriodSec = period;
        p.isRandomized = true;
        FrameProcessor fp;
        if (!runWithReader(fp, p, &setup))
            return -1;
        return static_cast<int>(fp.result().timesSec.size());
    };

    int n1s  = countAt(UIConstants::kSamplePeriod1s);
    int n10ms = countAt(UIConstants::kSamplePeriod10ms);
    QVERIFY2(n1s > 0, "1 s period run should produce samples");
    QVERIFY2(n10ms > n1s,
             qPrintable(QString("10 ms period (%1) should produce more samples than 1 s period (%2)")
                            .arg(n10ms).arg(n1s)));
}

////////////////////////////////////////////////////////////////////////////////
//                  REAL-FILE CALIBRATION ROUND TRIP (US5.3)                  //
////////////////////////////////////////////////////////////////////////////////

/// End-to-end check on real decoded data (not synthetic, unlike
/// TestStepDetector::roundTripSameDataIsExact): use agc_rnrz-l_trc_testfile.ch10 as BOTH
/// the calibration file and the "main" file being measured. Since they are the
/// same recording, the resulting calibration should reproduce the exact step
/// values (0, 6, 12, ... 60 dB) when re-applied — this is the full real
/// pipeline (derandomization, frame sync, word extraction, CalibrationExtractor,
/// StepDetector, interpolateCalibration) exercised together on genuine decoded
/// RNRZ-L data, rather than hand-built raw series.
void TestFrameProcessor::calibrationRoundTripOnRealFileProducesCleanSteps()
{
    const QString filepath = testDataPath("agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    int pcm_id = reader.getFirstPCMChannelID();
    int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    // Resolve the step-config TOML shipped under settings/rcvr_cals/.
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();  // tests/
    dir.cdUp();  // project root
    const QString step_toml = dir.filePath("settings/rcvr_cals/default.toml");
    if (!QFileInfo::exists(step_toml))
        QSKIP("default.toml (rcvr_cals) not available");

    QVector<StepDefinition> steps;
    QString step_error;
    QVERIFY2(StepDetector::parseStepConfig(step_toml, steps, step_error),
             qPrintable(step_error));
    QVERIFY(!steps.isEmpty());

    // ---- Extraction pass: build calibration profiles from this same file ----
    CalibrationExtractor::Request req;
    req.calFilename       = filepath;
    req.timeChannelId      = time_id;
    req.pcmChannelId       = pcm_id;
    req.sync.pattern       = "FE6B2840";
    req.sync.bitsInMinorFrame   = 8000; // true minor-frame length for this recording
    req.sync.randomized         = true; // RNRZ-L
    req.sync.dataRateMbps       = 0.8;   // 800 kbps -> ~100 frames/s; drives the adaptive extract period
    req.numReceivers       = 16;
    req.receiverChannels   = 3;
    req.steps              = steps;

    CalibrationExtractor extractor;
    QEventLoop loop;
    bool extract_ok = false;
    bool finished_already = false;
    QString summary;
    connect(&extractor, &CalibrationExtractor::finished, &loop,
            [&](bool ok, const QString& msg) {
                extract_ok = ok;
                summary = msg;
                finished_already = true;
                loop.quit();
            });
    extractor.start(req);
    if (!finished_already)
    {
        loop.exec();
    }
    QVERIFY2(extract_ok, qPrintable(summary));

    QHash<int, CalibrationProfile> calibration_by_word;
    for (const CalibrationChannelResult& r : extractor.results())
    {
        if (r.profile.valid)
        {
            calibration_by_word.insert(r.word, r.profile);
        }
    }
    QVERIFY2(!calibration_by_word.isEmpty(),
             "Expected at least one channel to calibrate from the real file");

    // ---- Main pass: re-measure the SAME file with the extracted profiles ----
    FrameSetup setup;
    static const char* kPrefixes[] = {"L", "R", "C"};
    for (int r = 0; r < req.numReceivers; r++)
    {
        for (int c = 0; c < req.receiverChannels; c++)
        {
            setup.addParameter(QString(kPrefixes[c]) + "_RCVR" + QString::number(r + 1),
                               r * req.receiverChannels + c);
        }
    }
    for (int i = 0; i < setup.length(); i++)
    {
        ParameterInfo* param = setup.getParameter(i);
        param->is_enabled = true;
        param->slope       = 1.0;
        param->scale       = 0.0;
        param->sample_sum  = 0.0;
        auto it = calibration_by_word.constFind(param->word);
        if (it != calibration_by_word.constEnd())
        {
            param->profile = it.value();
        }
    }

    uint64_t start_secs = 0;
    uint64_t stop_secs = UINT64_MAX;

    ProcessingParams p = makeTestParams(filepath, time_id, pcm_id, 0xFE6B2840, 32, 500, 8000);
    p.startSeconds     = start_secs;
    p.stopSeconds      = stop_secs;
    // Use a 100 ms window: at this file's ~8000-bit / ~100 frames-per-second
    // rate the shipped 10 ms period averages only ~1 frame (a sparse, gap-laced
    // series), while 1 s leaves only a few samples per ~4.5 s dwell. 100 ms is
    // the same neighbourhood the extractor now sizes itself to, giving clean
    // plateaus for the re-detection check below. Rate-based clock matches the
    // extraction pass (avoids IRIG-time fragility).
    p.samplePeriodSec  = 0.1;
    p.useDataRateClock = true;
    p.isRandomized     = true;
    p.mode             = StreamMode::ReceiverChannelInfo;

    FrameProcessor fp;
    QVERIFY2(runWithReader(fp, p, &setup), "Main-pass processing should succeed");
    const ProcessedStreamData& result = fp.result();

    // ---- Verify: each detected plateau in the calibrated output lands within
    //      a small tolerance of SOME multiple of 6 dB. We scope the check to
    //      StepDetector's own plateau detection (rather than the raw 198 s
    //      series, only ~55 s of which is the actual step sweep — the rest is
    //      pre/post-roll content never meant to land on a clean multiple), and
    //      we check each detected point against its nearest multiple of 6
    //      rather than against a chronologically-paired step index: detect()
    //      sorts its output points ascending by value, so a pre-roll plateau
    //      ahead of the official sweep would misalign index-based pairing even
    //      though the calibration itself is correct. "Close to *a* multiple of
    //      6" sidesteps that ordering ambiguity and tests the literal property
    //      the user cares about.
    constexpr double kToleranceDb = 1.0;
    bool any_channel_checked = false;
    QStringList failures;
    for (int ci = 0; ci < result.channels.size(); ci++)
    {
        const ProcessedChannelSeries& series = result.channels[ci];
        if (!calibration_by_word.contains(series.word) || series.values.isEmpty())
        {
            continue;
        }

        StepDetector::Result det = StepDetector::detect(series.values, p.samplePeriodSec, steps);
        if (!det.profile.valid)
        {
            continue; // This word's main-pass series didn't show clean re-detectable plateaus.
        }

        any_channel_checked = true;
        for (const CalibrationPoint& pt : det.profile.points)
        {
            double nearest_multiple_of_6 = std::round(pt.rawAvg / 6.0) * 6.0;
            if (qAbs(pt.rawAvg - nearest_multiple_of_6) > kToleranceDb)
            {
                failures << QString("%1 (word %2): plateau at %3 dB is not close to a 6 dB multiple")
                                .arg(series.name).arg(series.word).arg(pt.rawAvg);
            }
        }
    }

    QVERIFY2(any_channel_checked, "No calibrated channel produced a re-detectable step series");
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join("; ")));
}

////////////////////////////////////////////////////////////////////////////////
//                    OFF-PHASE SYNC / LOCK-LOSS REGRESSION                   //
////////////////////////////////////////////////////////////////////////////////

/// Regression test for a frame-sync bug: a sync-word match that is NOT
/// boundary-aligned (i.e. a false positive while not locked, such as one
/// occurring right after a loss of lock) must never trigger sample
/// extraction. Builds a synthetic 4-bit sync / 4-bit word stream by hand:
///   1. An initial sync match (never boundary-aligned by definition) that
///      starts collecting word A = 0.
///   2. A run of zero filler bits long enough to exceed bitsInFrame and
///      force a "loss of lock" (sync_count reset).
///   3. An off-phase sync-pattern collision, not aligned to the frame
///      boundary, while still unlocked — this must be rejected rather than
///      extracting the stale word A.
///   4. Word B = 15, followed by a genuine boundary-aligned sync that
///      confirms the new frame and legitimately extracts word B.
/// If the off-phase match in step 3 is wrongly treated as a confirmed frame,
/// the averaged result mixes in the stale word A (0) and reads 7.5 instead
/// of the correct 15.
void TestFrameProcessor::offPhaseSyncAfterLockLossIsNotExtracted()
{
    QString bits;
    bits += "1001";       // initial sync acquisition (not boundary-aligned)
    bits += "0000";       // word 0 of frame 1 = 0
    bits += "000000000";  // filler -> minor_frame_bit_count exceeds bitsInFrame, losing lock
    bits += "1001";       // off-phase false-positive sync while unlocked (NOT boundary-aligned)
    bits += "1111";       // word 0 of frame 2 = 15
    bits += "00001001";   // word 1 filler + genuine sync, boundary-aligned -> confirms frame 2

    QByteArray payload = packBitString(bits);

    FrameSetup setup;
    setup.addParameter("W0", 0);
    ParameterInfo* param = setup.getParameter(0);
    param->is_enabled = true;
    param->slope = 1.0;
    param->scale = 0.0;
    param->sample_sum = 0.0;

    PacketQueue queue;
    ProcessingParams p;
    p.packetQueue       = &queue;
    p.streamLabel       = "off-phase-test";
    p.mode              = StreamMode::ReceiverChannelInfo;
    p.startSeconds      = 0;
    p.stopSeconds       = UINT64_MAX;
    p.samplePeriodSec   = 1e9; // huge: keep every extracted sample in one output window
    p.useDataRateClock  = true;
    p.isRandomized      = false;
    p.isInverted        = false;

    p.resolvedAttrs.syncPat     = 0x9; // 1001b
    p.resolvedAttrs.syncMask    = 0xF;
    p.resolvedAttrs.syncPatLen  = 4;
    p.resolvedAttrs.bitsInFrame = 12;
    p.resolvedAttrs.wordsInFrame = 2;
    p.resolvedAttrs.wordLen     = 4;
    p.resolvedAttrs.wordMask    = 0xF;
    p.resolvedAttrs.minSyncs    = 1;
    p.resolvedAttrs.delta100ns  = 1.0;
    p.resolvedAttrs.needsSwap   = false;
    p.resolvedAttrs.resolved    = true;

    PacketItem item;
    item.payload     = payload;
    item.baseAbsSeconds = 0.0;
    item.packetBits   = static_cast<uint64_t>(bits.size());
    QVERIFY(queue.enqueue(item));

    PacketItem eos;
    eos.endOfStream = true;
    QVERIFY(queue.enqueue(eos));

    FrameProcessor fp;
    QVERIFY2(fp.process(p, &setup), "Synthetic off-phase-sync stream should process successfully");

    const ProcessedStreamData& r = fp.result();
    QVERIFY2(!r.channels.isEmpty() && !r.channels[0].values.isEmpty(),
             "Expected at least one extracted sample");
    // Only the boundary-aligned sync should yield a sample (word B = 15). If the
    // off-phase false positive after lock loss were wrongly extracted, the stale
    // word A (0) would be averaged in, producing 7.5 instead of 15.
    QCOMPARE(r.channels[0].values.first(), 15.0);
}

////////////////////////////////////////////////////////////////////////////////
//              NON-LINEAR CALIBRATION AVERAGING-ORDER REGRESSION             //
////////////////////////////////////////////////////////////////////////////////

/// Regression test for the non-linear calibration averaging-order bug. A
/// CalibrationProfile maps the AVERAGED raw count of a plateau to its true dB,
/// so within an output window the processor must average the raw counts first
/// and apply interpolateCalibration() once -- NOT interpolate every frame and
/// average the results. For a kinked (non-linear) profile the two orders differ:
/// mean(interpolate(raw)) != interpolate(mean(raw)).
///
/// We feed two frames into a single output window with word 0 = 0 and word 0 =
/// 15, and attach a convex profile with a flat low segment and a steep high
/// segment:
///     raw  0.0 -> 10 dB
///     raw  7.5 -> 10 dB   (vertex)
///     raw 15.0 -> 160 dB
/// The mean raw is 7.5, so the correct (raw-first) output is
/// interpolate(7.5) = 10 dB. The buggy (per-frame-first) order would yield
/// mean(interpolate(0), interpolate(15)) = mean(10, 160) = 85 dB.
void TestFrameProcessor::nonLinearCalibrationAveragesRawBeforeInterpolating()
{
    // Two boundary-aligned frames, 4-bit sync (1001) / 4-bit words, layout
    // [w0][w1][sync] per 12-bit minor frame (mirrors the off-phase test geometry).
    QString bits;
    bits += "1001";       // initial sync acquisition (not boundary-aligned)
    bits += "0000";       // frame 1, word 0 = 0
    bits += "0000";       // frame 1, word 1 (unused)
    bits += "1001";       // boundary sync -> extract frame 1 (word 0 = 0)
    bits += "1111";       // frame 2, word 0 = 15
    bits += "0000";       // frame 2, word 1 (unused)
    bits += "1001";       // boundary sync -> extract frame 2 (word 0 = 15)

    QByteArray payload = packBitString(bits);

    FrameSetup setup;
    setup.addParameter("W0", 0);
    ParameterInfo* param = setup.getParameter(0);
    param->is_enabled = true;
    param->slope = 1.0;   // ignored once a valid profile is present
    param->scale = 0.0;
    param->sample_sum = 0.0;

    CalibrationProfile profile;
    profile.points.push_back({0.0, 10.0});
    profile.points.push_back({7.5, 10.0});
    profile.points.push_back({15.0, 160.0});
    profile.valid = true;
    param->profile = profile;

    PacketQueue queue;
    ProcessingParams p;
    p.packetQueue       = &queue;
    p.streamLabel       = "nonlinear-avg-test";
    p.mode              = StreamMode::ReceiverChannelInfo;
    p.startSeconds      = 0;
    p.stopSeconds       = UINT64_MAX;
    p.samplePeriodSec   = 1e9; // huge: keep both extracted samples in one output window
    p.useDataRateClock  = true;
    p.isRandomized      = false;
    p.isInverted        = false;

    p.resolvedAttrs.syncPat     = 0x9; // 1001b
    p.resolvedAttrs.syncMask    = 0xF;
    p.resolvedAttrs.syncPatLen  = 4;
    p.resolvedAttrs.bitsInFrame = 12;
    p.resolvedAttrs.wordsInFrame = 2;
    p.resolvedAttrs.wordLen     = 4;
    p.resolvedAttrs.wordMask    = 0xF;
    p.resolvedAttrs.minSyncs    = 1;
    p.resolvedAttrs.delta100ns  = 1.0;
    p.resolvedAttrs.needsSwap   = false;
    p.resolvedAttrs.resolved    = true;

    PacketItem item;
    item.payload        = payload;
    item.baseAbsSeconds = 0.0;
    item.packetBits     = static_cast<uint64_t>(bits.size());
    QVERIFY(queue.enqueue(item));

    PacketItem eos;
    eos.endOfStream = true;
    QVERIFY(queue.enqueue(eos));

    FrameProcessor fp;
    QVERIFY2(fp.process(p, &setup), "Synthetic non-linear-calibration stream should process successfully");

    const ProcessedStreamData& r = fp.result();
    QVERIFY2(!r.channels.isEmpty() && !r.channels[0].values.isEmpty(),
             "Expected at least one extracted sample");
    // Correct (average raw, then calibrate once): interpolate(mean(0,15)=7.5) = 10 dB.
    // Buggy (calibrate each frame, then average): mean(10, 160) = 85 dB.
    QVERIFY2(qAbs(r.channels[0].values.first() - 10.0) < 1e-9,
             qPrintable(QString("Expected 10.0 dB (raw averaged before calibration), got %1")
                            .arg(r.channels[0].values.first())));
}

void TestFrameProcessor::swapBytesOffFindsSyncThatTheSwapDestroys()
{
    // The Safran recording is byte-ordered the opposite way to every other fixture
    // here: channel 13 carries PRN-15 (334AABBF, 32767-bit frames) in the raw stream,
    // and applying the byte swap destroys the pattern outright.
    //
    // Before ProcessingParams::swapBytes existed the swap was unconditional - the
    // reader filled needsSwap from an irig106 attribute that nothing ever assigns, so
    // it was a constant `true` wearing the costume of a lookup - and this recording
    // could not be processed at all.
    //
    // Both directions are asserted. Checking only that swap=false locks would pass
    // against a build that ignored the flag and never swapped anything, which would
    // silently break every other fixture in this suite.
    const QString filepath = testDataPath("safran_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("Safran test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    const int time_id = reader.getCurrentTimeChannelID();
    if (time_id < 0)
        QSKIP("Missing time channel in test file");

    // Channel 13 specifically. Channels 11 and 12 carry no PRN-15 under any transform,
    // and channel 14's payloads are an odd number of bytes - which SwapBytes_PcmF1
    // declines to swap - so 14 locks either way and would prove nothing about the flag.
    constexpr int kPrnChannelId = 13;

    // PRN-15 is 2^15-1 bits, deliberately NOT a whole number of 8-bit words.
    // MainViewModel::buildStreamJob resolves that with ceiling division; mirroring the
    // expression keeps this test honest if the frame geometry ever changes. The floor
    // finds the sync and then extracts no frames, which is a different failure.
    constexpr int kPrnFrameBits  = 32767;
    constexpr int kPrnFrameWords =
        (kPrnFrameBits + PCMConstants::kCommonWordLen - 1) / PCMConstants::kCommonWordLen;

    struct Outcome { bool processed = false; double peakLock = 0.0; };

    auto run = [&](bool swap) -> Outcome {
        ProcessingParams p = makeTestParams(filepath, time_id, kPrnChannelId,
                                            0x334AABBF, 32, kPrnFrameWords, kPrnFrameBits);
        p.startSeconds    = 0;
        p.stopSeconds     = UINT64_MAX;
        p.samplePeriodSec = 1.0;
        p.isRandomized    = false;
        p.swapBytes       = swap;
        p.mode            = StreamMode::FrameSyncLockStats;

        // Open-coded rather than runWithReader() so a prepare() failure - a broken
        // fixture rather than a byte-order result - is reported as itself.
        PacketQueue queue;
        p.packetQueue = &queue;
        Ch10PacketReader rdr;
        QVector<ProcessingParams*> params_list = { &p };
        QString error;
        if (!rdr.prepare(p.filename, p.timeChannelId, params_list, error))
        {
            qWarning("prepare() failed (swap=%d): %s", int(swap), qPrintable(error));
            return {};
        }

        FrameSetup empty_setup;  // No word map for lock-only mode.
        FrameProcessor fp;
        QFuture<void> future = QtConcurrent::run([&rdr]() { rdr.run(); });
        Outcome out;
        out.processed = fp.process(p, &empty_setup);
        future.waitForFinished();

        for (double v : fp.result().lockPercent)
            out.peakLock = std::max(out.peakLock, v);
        return out;
    };

    const Outcome unswapped = run(false);
    const Outcome swapped   = run(true);

    QVERIFY2(unswapped.processed,
             "with swapBytes off the PRN-15 sync is present and processing must succeed");
    QVERIFY2(unswapped.peakLock > 50.0,
             qPrintable(QStringLiteral("expected lock with swapBytes off, peak was %1%")
                            .arg(unswapped.peakLock)));

    // The swap destroys the pattern, so the processor reports "frame sync pattern was
    // not found" and returns false. That failure IS the expected result here - it is
    // exactly what the operator saw before the setting existed.
    QVERIFY2(!swapped.processed,
             "with swapBytes on the sync must be destroyed and processing must fail");
    QCOMPARE(swapped.peakLock, 0.0);
}
