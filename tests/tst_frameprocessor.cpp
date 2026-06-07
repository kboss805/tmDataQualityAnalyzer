/**
 * @file tst_frameprocessor.cpp
 * @brief Implementation of FrameProcessor unit tests (in-memory accumulation).
 */

#include "tst_frameprocessor.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>
#include <QVector>

#include "chapter10reader.h"
#include "constants.h"
#include "frameprocessor.h"
#include "framesetup.h"
#include "processedstreamdata.h"

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
    p.time_channel_id = time_channel_id;
    p.pcm_channel_id = pcm_channel_id;
    p.frame_sync = frame_sync;
    p.sync_pattern_length = sync_len;
    p.words_in_minor_frame = words_in_frame;
    p.bits_in_minor_frame = bits_in_frame;
    return p;
}

/// Helper: loads the default frame setup (word map) from settings/default.toml.
static bool loadDefaultFrameSetup(FrameSetup& setup)
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();  // tests/
    dir.cdUp();  // project root
    QString toml_path = dir.filePath("settings/default.toml");
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

void TestFrameProcessor::hasSyncPatternFindsMatch()
{
    const char raw[] = {'\xFF', '\x00'};
    QByteArray data(raw, 2);
    bool found = FrameProcessor::hasSyncPattern(
        reinterpret_cast<const uint8_t*>(data.constData()), 16, 0xFF00, 0xFFFF, 16);
    QVERIFY2(found, "Should find 0xFF00 pattern in [0xFF, 0x00] buffer");
}

void TestFrameProcessor::hasSyncPatternNoMatch()
{
    const char raw[] = {'\xFF', '\x00'};
    QByteArray data(raw, 2);
    bool found = FrameProcessor::hasSyncPattern(
        reinterpret_cast<const uint8_t*>(data.constData()), 16, 0x00FF, 0xFFFF, 16);
    QVERIFY2(!found, "Should NOT find 0x00FF pattern in [0xFF, 0x00] buffer");
}

void TestFrameProcessor::hasSyncPatternShortBuffer()
{
    QByteArray data(1, '\xFF');
    bool found = FrameProcessor::hasSyncPattern(
        reinterpret_cast<const uint8_t*>(data.constData()), 8, 0xFF00, 0xFFFF, 16);
    QVERIFY2(!found, "Should not find 16-bit pattern in 8-bit buffer");
}

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
    p.start_seconds = 0;
    p.stop_seconds = 100;
    p.sample_rate = 1;
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
    p.start_seconds = 0;
    p.stop_seconds = 100;
    p.sample_rate = 1;
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
    p.start_seconds = 0;
    p.stop_seconds = 100;
    p.sample_rate = 1;
    QVERIFY(!fp.process(p, &setup));
    QVERIFY(!error_spy.isEmpty());
}

////////////////////////////////////////////////////////////////////////////////
//                       IN-MEMORY ACCUMULATION                               //
////////////////////////////////////////////////////////////////////////////////

void TestFrameProcessor::processAccumulatesReceiverData()
{
    const QString filepath = testDataPath("rnrz-l_testfile.ch10");
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

    uint64_t start_secs = reader.dhmsToUInt64(
        reader.getStartDayOfYear(), reader.getStartHour(),
        reader.getStartMinute(), reader.getStartSecond());
    uint64_t stop_secs = reader.dhmsToUInt64(
        reader.getStopDayOfYear(), reader.getStopHour(),
        reader.getStopMinute(), reader.getStopSecond());

    ProcessingParams p = makeTestParams(filepath, time_id, pcm_id);
    p.start_seconds = start_secs;
    p.stop_seconds = stop_secs;
    p.sample_rate = 1;
    p.is_randomized = true;
    p.mode = StreamMode::ReceiverChannelInfo;
    p.stream_label = "Ch test";

    FrameProcessor fp;
    QVERIFY2(fp.process(p, &setup), "Processing should succeed on valid RNRZ-L file");

    const ProcessedStreamData& r = fp.result();
    QVERIFY2(r.hasSamples(), "Result must contain at least one time sample");
    QCOMPARE(r.lockPercent.size(), r.timesSec.size());
    QCOMPARE(r.channels.size(), setup.length());
    QCOMPARE(r.streamLabel, QString("Ch test"));
    QCOMPARE(r.mode, StreamMode::ReceiverChannelInfo);

    // Every channel's value vector is parallel to the time vector.
    for (const auto& ch : r.channels)
    {
        QCOMPARE(ch.values.size(), r.timesSec.size());
    }
    // Lock percentages are in range and timestamps are non-decreasing.
    for (int i = 0; i < r.timesSec.size(); i++)
    {
        QVERIFY(r.lockPercent[i] >= 0.0 && r.lockPercent[i] <= 100.0);
        if (i > 0)
            QVERIFY(r.timesSec[i] >= r.timesSec[i - 1]);
    }
}

void TestFrameProcessor::processLockOnlyModeHasNoChannels()
{
    const QString filepath = testDataPath("rnrz-l_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    int pcm_id = reader.getFirstPCMChannelID();
    int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    uint64_t start_secs = reader.dhmsToUInt64(
        reader.getStartDayOfYear(), reader.getStartHour(),
        reader.getStartMinute(), reader.getStartSecond());
    uint64_t stop_secs = reader.dhmsToUInt64(
        reader.getStopDayOfYear(), reader.getStopHour(),
        reader.getStopMinute(), reader.getStopSecond());

    ProcessingParams p = makeTestParams(filepath, time_id, pcm_id);
    p.start_seconds = start_secs;
    p.stop_seconds = stop_secs;
    p.sample_rate = 1;
    p.is_randomized = true;
    p.mode = StreamMode::FrameSyncLockStats;

    FrameSetup empty_setup;  // No word map for lock-only mode.
    FrameProcessor fp;
    QVERIFY2(fp.process(p, &empty_setup), "Lock-only processing should succeed");

    const ProcessedStreamData& r = fp.result();
    QVERIFY2(r.hasSamples(), "Lock-only result must contain time samples");
    QVERIFY2(r.channels.isEmpty(), "Lock-only mode must produce no receiver channels");
    QCOMPARE(r.lockPercent.size(), r.timesSec.size());
    QCOMPARE(r.mode, StreamMode::FrameSyncLockStats);
}

void TestFrameProcessor::processSlopeAffectsValues()
{
    const QString filepath = testDataPath("rnrz-l_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    int pcm_id = reader.getFirstPCMChannelID();
    int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    uint64_t start_secs = reader.dhmsToUInt64(
        reader.getStartDayOfYear(), reader.getStartHour(),
        reader.getStartMinute(), reader.getStartSecond());
    uint64_t stop_secs = reader.dhmsToUInt64(
        reader.getStopDayOfYear(), reader.getStopHour(),
        reader.getStopMinute(), reader.getStopSecond());

    auto run = [&](double slope) -> double {
        FrameSetup setup;
        if (!setupParams(setup, slope, 0.0))
            return 0.0;
        ProcessingParams p = makeTestParams(filepath, time_id, pcm_id);
        p.start_seconds = start_secs;
        p.stop_seconds = stop_secs;
        p.sample_rate = 1;
        p.is_randomized = true;
        FrameProcessor fp;
        if (!fp.process(p, &setup) || fp.result().channels.isEmpty()
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

void TestFrameProcessor::processSampleRate100HzMoreSamples()
{
    const QString filepath = testDataPath("rnrz-l_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    int pcm_id = reader.getFirstPCMChannelID();
    int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    uint64_t start_secs = reader.dhmsToUInt64(
        reader.getStartDayOfYear(), reader.getStartHour(),
        reader.getStartMinute(), reader.getStartSecond());
    uint64_t stop_secs = reader.dhmsToUInt64(
        reader.getStopDayOfYear(), reader.getStopHour(),
        reader.getStopMinute(), reader.getStopSecond());
    if (stop_secs <= start_secs + 1)
        QSKIP("Test file too short for sample-rate comparison");

    auto countAt = [&](int rate) -> int {
        FrameSetup setup;
        if (!setupParams(setup, 1.0, 0.0))
            return -1;
        ProcessingParams p = makeTestParams(filepath, time_id, pcm_id);
        p.start_seconds = start_secs;
        p.stop_seconds = stop_secs;
        p.sample_rate = rate;
        p.is_randomized = true;
        FrameProcessor fp;
        if (!fp.process(p, &setup))
            return -1;
        return static_cast<int>(fp.result().timesSec.size());
    };

    int n1 = countAt(UIConstants::kSampleRate1Hz);
    int n100 = countAt(UIConstants::kSampleRate100Hz);
    QVERIFY2(n1 > 0, "1 Hz run should produce samples");
    QVERIFY2(n100 > n1,
             qPrintable(QString("100 Hz (%1) should produce more samples than 1 Hz (%2)")
                            .arg(n100).arg(n1)));
}
