/**
 * @file tst_frameprocessor.cpp
 * @brief Implementation of FrameProcessor unit tests (in-memory accumulation).
 */

#include "tst_frameprocessor.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
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
#include "framesync.h"
#include "qtbridge.h"
#include "qtprocessingevents.h"
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
    // Every .ch10 fixture in this suite except the Safran recording is an ordinary
    // Chapter 10 file, which means byte-swapped; only Safran is legacy and overrides
    // this. Set explicitly rather than inherited, so a change to the DTO default
    // cannot silently retune what these tests are decoding.
    p.swapBytes = true;
    p.filename = toFsPath(filename);
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
    std::vector<ProcessingParams*> params_list = { &p };
    std::string error;
    if (!reader.prepare(p.filename, p.timeChannelId, params_list, error))
        return false;

    QFuture<void> future = QtConcurrent::run([&reader]() { reader.run(); });
    bool ok = fp.process(p, setup);
    future.waitForFinished();
    return ok;
}

/// Helper: packs a string of '1'/'0' characters MSB-first into bytes, for
/// building synthetic PCM bitstreams without a real Chapter 10 file.
static std::vector<uint8_t> packBitString(const QString& bits)
{
    std::vector<uint8_t> data(static_cast<std::size_t>((bits.size() + 7) / 8), 0);
    for (int i = 0; i < bits.size(); i++)
    {
        if (bits[i] == QChar('1'))
        {
            auto& byte = data[static_cast<std::size_t>(i / 8)];
            byte = static_cast<uint8_t>(byte | (0x80 >> (i % 8)));
        }
    }
    return data;
}

namespace {

/// Everything a FrameProcessor reports, in order. The only place the tests below touch the
/// reporting mechanism is captureEvents(), so the Qt-signals-to-event-sink change swaps that
/// one function and nothing else: the expected text was written against the signals.
struct Capture
{
    QStringList logs;
    QStringList errors;
    QList<bool> finished;
};

/// Records into a Capture. Called on the processing thread, which in every test below is
/// the test thread, so no locking is needed.
class CaptureSink final : public ProcessingEvents
{
public:
    explicit CaptureSink(Capture& c) : m_c(c) {}
    void logMessage(const std::string& m) override { m_c.logs << QString::fromStdString(m); }
    void errorOccurred(const std::string& m) override { m_c.errors << QString::fromStdString(m); }
    void processingFinished(bool ok) override { m_c.finished << ok; }

private:
    Capture& m_c;
};

void captureEvents(FrameProcessor& fp, Capture& c)
{
    fp.setEvents(std::make_shared<CaptureSink>(c));
}

/// A 4-bit-sync, 12-bit-frame synthetic stream: the same layout the off-phase test uses.
/// Fills @p p's resolved attributes; the caller sets what it varies (label, window, ...).
void syntheticParams(ProcessingParams& p, PacketQueue& queue)
{
    p.packetQueue      = &queue;
    p.pcmChannelId     = 7;
    p.streamLabel      = "Rcvr Bus";
    p.mode             = StreamMode::FrameSyncLockStats;
    p.startSeconds     = 0;
    p.stopSeconds      = UINT64_MAX;
    p.samplePeriodSec  = 1e9;
    p.useDataRateClock = true;
    p.isRandomized     = false;
    p.isInverted       = false;

    p.resolvedAttrs.syncPat      = 0x9; // 1001b
    p.resolvedAttrs.syncMask     = 0xF;
    p.resolvedAttrs.syncPatLen   = 4;
    p.resolvedAttrs.bitsInFrame  = 12;
    p.resolvedAttrs.wordsInFrame = 2;
    p.resolvedAttrs.wordLen      = 4;
    p.resolvedAttrs.wordMask     = 0xF;
    p.resolvedAttrs.minSyncs     = 1;
    p.resolvedAttrs.delta100ns   = 1.0;
    p.resolvedAttrs.needsSwap    = false;
    p.resolvedAttrs.resolved     = true;
}

/// Queues one packet carrying @p bits, MSB first.
void enqueueBits(PacketQueue& queue, const QString& bits)
{
    PacketItem item;
    item.payload        = std::make_shared<const std::vector<uint8_t>>(packBitString(bits));
    item.baseAbsSeconds = 0.0;
    item.packetBits     = static_cast<uint64_t>(bits.size());
    QVERIFY(queue.enqueue(item));
}

/// Sets a stream's display label. The one place these tests assign a QString label.
void setStreamLabel(ProcessingParams& p, const QString& label)
{
    p.streamLabel = label.toStdString();
}

void enqueueEndOfStream(PacketQueue& queue)
{
    PacketItem eos;
    eos.endOfStream = true;
    QVERIFY(queue.enqueue(eos));
}

} // namespace


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
    return setup.tryLoadingFile(toFsPath(toml_path), 49);
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

void TestFrameProcessor::nonAsciiPathOpensInBothReaders_data()
{
    QTest::addColumn<QString>("folder");
    // In the Windows-1252 code page: the ANSI path API can spell it.
    QTest::newRow("latin") << (QStringLiteral("caf") + QChar(0x00E9));
    // Outside it: no ANSI code page on a Western system can spell it at all.
    QTest::newRow("greek") << (QStringLiteral("omega-") + QChar(0x03A9));
}

void TestFrameProcessor::nonAsciiPathOpensInBothReaders()
{
    // Both readers hand the path to irig106's enI106Ch10Open(), which opens it with the
    // C runtime's narrow open(char*). They used to encode it differently - the metadata
    // reader with toLocal8Bit(), the packet reader with toUtf8() - so a file under a
    // folder like C:/Users/Jos<e-acute>/ listed its channels and then failed the moment
    // processing started. Users' own folders are exactly where recordings get copied.
    QFETCH(QString, folder);
    const QString src = testDataPath("test files/step_cal, example.ch10");
    QVERIFY2(QFileInfo::exists(src), "committed fixture missing");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(QDir(tmp.path()).mkdir(folder));
    const QString dst = QDir(tmp.path()).filePath(folder + "/recording.ch10");
    QVERIFY(QFile::copy(src, dst));

    Chapter10Reader meta;
    QVERIFY2(meta.loadChannels(toFsPath(dst)), "metadata reader could not open the path");

    Ch10PacketReader reader;
    std::vector<ProcessingParams*> none;
    std::string error;
    reader.prepare(toFsPath(dst), meta.getCurrentTimeChannelID(), none, error);
    QVERIFY2(error != "Error opening data file.",
             "packet reader could not open a path the metadata reader opened");
}

void TestFrameProcessor::reportsExactlyTheseMessages_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<bool>("returned");
    QTest::addColumn<QStringList>("logs");
    QTest::addColumn<QStringList>("errors");

    const QString tag = QStringLiteral("[CH 7 Rcvr Bus] ");

    QTest::newRow("success")
        << "success" << true
        << QStringList{tag + "processing...",
                       tag + "4 bytes processed, 3 syncs found, 2 frames extracted.",
                       tag + "complete — 1 samples extracted, elapsed N.Ns."}
        << QStringList{};

    // A bounded window is named; an unbounded one is not (an unset stop would print as
    // 18446744073709551615s on every ordinary run).
    // A label outside ASCII (an accented letter and Greek omega) goes in, is quoted in
    // every message's tag, and comes back out unchanged in the result.
    const QString unicode_tag = QStringLiteral("[CH 7 R") + QChar(0x00E9) + QStringLiteral("cepteur ")
                                + QChar(0x03A9) + QStringLiteral("] ");
    QTest::newRow("non-ascii-label")
        << "unicodelabel" << true
        << QStringList{unicode_tag + "processing...",
                       unicode_tag + "4 bytes processed, 3 syncs found, 2 frames extracted.",
                       unicode_tag + "complete — 1 samples extracted, elapsed N.Ns."}
        << QStringList{};

    QTest::newRow("bounded-window")
        << "bounded" << false
        << QStringList{tag + "processing (1s to 100s)...",
                       tag + "2 bytes processed, 2 syncs found, 0 frames extracted."}
        << QStringList{tag + "sync found but no frames extracted — pattern 0x9 (4 bits), "
                             "12 bits/frame; 2 syncs, 1 boundary-aligned."};

    // The mask is printed only when it constrains something.
    QTest::newRow("masked-pattern-no-sync")
        << "masked" << false
        << QStringList{tag + "processing...",
                       tag + "2 bytes processed, 0 syncs found, 0 frames extracted."}
        << QStringList{tag + "no frame sync found — pattern 0x9 (4 bits, mask 0xE), "
                             "12 bits/frame."};

    QTest::newRow("sync-but-no-frames")
        << "noframes" << false
        << QStringList{tag + "processing...",
                       tag + "1 bytes processed, 1 syncs found, 0 frames extracted."}
        << QStringList{tag + "sync found but no frames extracted — pattern 0x9 (4 bits), "
                             "12 bits/frame; 1 syncs, 0 boundary-aligned."};

    QTest::newRow("cancelled")
        << "cancelled" << false
        << QStringList{tag + "processing...", tag + "processing cancelled by user."}
        << QStringList{};

    // The label "Ch 7" is the fallback MainViewModel uses when TMATS has no name, so the
    // tag drops it; an empty label drops it too.
    QTest::newRow("no-packet-queue")
        << "noqueue" << false << QStringList{}
        << QStringList{QStringLiteral("[CH 7] Internal error: no packet queue for stream.")};
    QTest::newRow("attributes-unresolved")
        << "unresolved" << false << QStringList{}
        << QStringList{QStringLiteral("[CH 7] Internal error: PCM attributes not resolved.")};

    // Odd-length payloads cannot be byte-pair swapped. Every packet odd: the setting had
    // no effect at all. Some odd: one stream carrying two byte orders.
    QTest::newRow("every-payload-odd")
        << "allodd" << false
        << QStringList{tag + "processing...",
                       tag + "all 1 payloads are odd-length and cannot be byte-pair swapped — "
                             "processed as Legacy Chapter 10 regardless of the setting.",
                       tag + "3 bytes processed, 2 syncs found, 0 frames extracted."}
        << QStringList{tag + "sync found but no frames extracted — pattern 0x9 (4 bits), "
                             "12 bits/frame; 2 syncs, 0 boundary-aligned."};
    QTest::newRow("some-payloads-odd")
        << "mixed" << true
        << QStringList{tag + "processing...",
                       tag + "warning: byte swap skipped on 1 of 2 packets (odd length) — "
                             "mixed byte order, sync may be unreliable.",
                       tag + "5 bytes processed, 4 syncs found, 1 frames extracted.",
                       tag + "complete — 1 samples extracted, elapsed N.Ns."}
        << QStringList{};
}

void TestFrameProcessor::reportsExactlyTheseMessages()
{
    // The text of every message FrameProcessor reports, exactly, in order. Written
    // against the QString/signals implementation and kept as it was across the move to
    // std::format and an event sink: a format specifier is the kind of thing a port gets
    // subtly wrong (%1 of %2, 'f' with one decimal) and nothing else would notice.
    QFETCH(QString, scenario);
    QFETCH(bool, returned);
    QFETCH(QStringList, logs);
    QFETCH(QStringList, errors);

    PacketQueue q;
    ProcessingParams p;
    FrameSetup s;
    syntheticParams(p, q);

    if (scenario == "success" || scenario == "unicodelabel")
    {
        if (scenario == "unicodelabel")
        {
            setStreamLabel(p, QStringLiteral("R") + QChar(0x00E9) + QStringLiteral("cepteur ")
                                  + QChar(0x03A9));
        }
        enqueueBits(q, "1001" "0000" "00001001" "0000" "00001001");
        enqueueEndOfStream(q);
    }
    else if (scenario == "bounded")
    {
        p.startSeconds = 1;
        p.stopSeconds  = 100;
        enqueueBits(q, "1001" "0000" "00001001");
        enqueueEndOfStream(q);
    }
    else if (scenario == "masked")
    {
        p.resolvedAttrs.syncMask = 0xE;
        enqueueBits(q, "0000" "0000" "0000");
        enqueueEndOfStream(q);
    }
    else if (scenario == "noframes")
    {
        enqueueBits(q, "1001");
        enqueueEndOfStream(q);
    }
    else if (scenario == "cancelled")
    {
        enqueueBits(q, "1001");
        enqueueEndOfStream(q);
    }
    else if (scenario == "noqueue")
    {
        p.packetQueue = nullptr;
        p.streamLabel = "Ch 7";
    }
    else if (scenario == "unresolved")
    {
        p.resolvedAttrs.resolved = false;
        p.streamLabel = "";
    }
    else if (scenario == "allodd")
    {
        p.resolvedAttrs.needsSwap = true;
        enqueueBits(q, "100100000000000000001001"); // 3 bytes
        enqueueEndOfStream(q);
    }
    else if (scenario == "mixed")
    {
        p.resolvedAttrs.needsSwap = true;
        enqueueBits(q, "100100000000000000001001"); // 3 bytes (odd)
        enqueueBits(q, "1001000000001001");         // 2 bytes (even)
        enqueueEndOfStream(q);
    }

    FrameProcessor fp;
    Capture c;
    captureEvents(fp, c);
    if (scenario == "cancelled")
    {
        fp.requestAbort();
    }
    const bool ok = fp.process(p, &s);

    // The one non-deterministic piece: how long the run took, printed to a tenth.
    const QRegularExpression elapsed(QStringLiteral("elapsed [0-9]+\\.[0-9]s"));
    QStringList actual = c.logs;
    for (QString& m : actual)
    {
        m.replace(elapsed, QStringLiteral("elapsed N.Ns"));
    }

    QCOMPARE(ok, returned);
    QCOMPARE(actual, logs);
    QCOMPARE(c.errors, errors);
    if (scenario == "unicodelabel")
    {
        // The label also travels into the result, where the plot reads it.
        QCOMPARE(QString::fromStdString(fp.result().streamLabel),
                 QStringLiteral("R") + QChar(0x00E9) + QStringLiteral("cepteur ")
                     + QChar(0x03A9));
    }
    // Exactly one completion notice per run, and it agrees with the return value.
    QCOMPARE(c.finished, QList<bool>({returned}));
}

void TestFrameProcessor::constructorDefaults()
{
    FrameProcessor fp;
    fp.requestAbort();
    QVERIFY(true);
}

void TestFrameProcessor::requestAbortSetsFlag()
{
    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);
    fp.requestAbort();
    QVERIFY(true);
}

////////////////////////////////////////////////////////////////////////////////
//                         STATIC METHOD TESTS                                //
////////////////////////////////////////////////////////////////////////////////

void TestFrameProcessor::derandomizeShortBufferIdentity()
{
    std::vector<uint8_t> data(1, 0xAB);
    const std::vector<uint8_t> original = data;
    uint16_t lfsr = 0;
    FrameProcessor::derandomizeBitstream(data.data(), 8, lfsr);
    QCOMPARE(data[0], original[0]);
}

void TestFrameProcessor::derandomizeLongerBufferChanges()
{
    std::vector<uint8_t> data(4, 0xFF);
    const std::vector<uint8_t> original = data;
    uint16_t lfsr = 0;
    FrameProcessor::derandomizeBitstream(data.data(), 32, lfsr);
    QVERIFY2(data != original, "32 bits of all-1s should change after derandomization");
}

////////////////////////////////////////////////////////////////////////////////
//                          PROCESS ERROR PATHS                               //
////////////////////////////////////////////////////////////////////////////////

void TestFrameProcessor::processInvalidTimeChannel()
{
    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);

    FrameSetup setup;
    ProcessingParams p = makeTestParams("dummy.ch10", -1, 1);
    p.startSeconds = 0;
    p.stopSeconds = 100;
    p.samplePeriodSec = 1.0;
    QVERIFY(!fp.process(p, &setup));
    QVERIFY(!cap.errors.isEmpty());
    QVERIFY(!cap.finished.isEmpty());
    QCOMPARE(cap.finished.last(), false);
}

void TestFrameProcessor::processInvalidPcmChannel()
{
    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);

    FrameSetup setup;
    ProcessingParams p = makeTestParams("dummy.ch10", 1, -1);
    p.startSeconds = 0;
    p.stopSeconds = 100;
    p.samplePeriodSec = 1.0;
    QVERIFY(!fp.process(p, &setup));
    QVERIFY(!cap.errors.isEmpty());
    QVERIFY(!cap.finished.isEmpty());
    QCOMPARE(cap.finished.last(), false);
}

void TestFrameProcessor::processInvalidFile()
{
    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);

    FrameSetup setup;
    ProcessingParams p = makeTestParams("nonexistent_file.ch10", 1, 1);
    p.startSeconds = 0;
    p.stopSeconds = 100;
    p.samplePeriodSec = 1.0;
    QVERIFY(!fp.process(p, &setup));
    QVERIFY(!cap.errors.isEmpty());
}

////////////////////////////////////////////////////////////////////////////////
//                       IN-MEMORY ACCUMULATION                               //
////////////////////////////////////////////////////////////////////////////////

void TestFrameProcessor::processAccumulatesReceiverData()
{
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
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
    QVERIFY2(r.lockPercent.empty(), "Receiver mode must produce no lock series");
    QVERIFY2(r.accumulatedMissedFrames.empty(), "Receiver mode must produce no missed-frames series");
    QCOMPARE(r.channels.size(), setup.length());
    QCOMPARE(QString::fromStdString(r.streamLabel), QString("Ch test"));
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
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
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
    QVERIFY2(r.channels.empty(), "Lock-only mode must produce no receiver channels");
    QCOMPARE(r.lockPercent.size(), r.timesSec.size());
    QCOMPARE(r.accumulatedMissedFrames.size(), r.timesSec.size());
    QCOMPARE(r.mode, StreamMode::FrameSyncLockStats);
}

void TestFrameProcessor::processFrameSyncErrorsMonotonic()
{
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
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
    QVERIFY(r.accumulatedMissedFrames.front() >= 0.0);
    for (int i = 1; i < r.accumulatedMissedFrames.size(); i++)
    {
        QVERIFY2(r.accumulatedMissedFrames[i] >= r.accumulatedMissedFrames[i - 1],
                 "Frame sync error accumulation must be monotonically non-decreasing");
    }
}

void TestFrameProcessor::processSlopeAffectsValues()
{
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
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
        if (!runWithReader(fp, p, &setup) || fp.result().channels.empty()
            || fp.result().channels[0].values.empty())
            return 0.0;
        return fp.result().channels[0].values.front();
    };

    double v1 = run(1.0);
    double v2 = run(2.0);
    if (qAbs(v1) < 1e-10)
        QSKIP("First AGC sample is zero — cannot test slope ratio");
    QVERIFY2(qAbs(v2 - 2.0 * v1) < 1e-6,
             qPrintable(QString("Expected v2 (%1) = 2 * v1 (%2)").arg(v2).arg(v1)));
}

void TestFrameProcessor::aSyncMaskDoesNotMaskReceiverWords()
{
    // The sync mask says which bits of the SYNC PATTERN must match; it has nothing to do with the receiver words that
    // follow it. irig106's Set_Attributes_Ext_PcmF1 ANDs its word mask with the sync mask, and the reader used to pass
    // that on, so a mask that ignored even the lowest sync bit cleared the lowest bit of every AGC sample (0x00FF00FF
    // kept only the low byte; 0xFFFF0000 would have zeroed them all). Ignoring sync bits must change no value.
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
    const int pcm_id = reader.getFirstPCMChannelID();
    const int time_id = reader.getCurrentTimeChannelID();
    QVERIFY(pcm_id >= 0 && time_id >= 0);

    auto run = [&](uint64_t sync_pattern, uint64_t sync_mask, ProcessedStreamData& out) -> bool {
        FrameSetup setup;
        if (!setupParams(setup, 1.0, 0.0))   // slope 1, offset 0: the values are the averaged raw counts
            return false;
        ProcessingParams p = makeTestParams(filepath, time_id, pcm_id);
        p.startSeconds = 0;
        p.stopSeconds = UINT64_MAX;
        p.samplePeriodSec = 1.0;
        p.isRandomized = true;
        p.mode = StreamMode::ReceiverChannelInfo;
        p.frameSync = sync_pattern;
        p.frameSyncMask = sync_mask;
        FrameProcessor fp;
        if (!runWithReader(fp, p, &setup))
            return false;
        out = fp.result();
        return true;
    };

    ProcessedStreamData full;
    ProcessedStreamData masked;
    QVERIFY(run(0xFE6B2840ull, 0, full));      // every bit of the pattern
    // Sync bits 8-15 ignored: the other 24 still find every frame of this recording, and the old code's word mask
    // would have been 0x00FF, keeping only each word's low byte. The pattern carries zeros where the mask ignores - the
    // matcher compares the MASKED stream with the pattern as written, as irig106's own decoder does, so a pattern with
    // ones there could never match. (Ignoring only the lowest sync bit shows nothing: this fixture's raw words never
    // set their lowest bit.)
    QVERIFY(run(0xFE6B0040ull, 0xFFFF00FFull, masked));
    QVERIFY(full.hasSamples());
    QCOMPARE(masked.timesSec.size(), full.timesSec.size());
    QCOMPARE(masked.channels.size(), full.channels.size());

    // The fixture's averaged counts reach far above 255, so a cleared high byte would show.
    bool any_above_a_byte = false;
    for (std::size_t c = 0; c < full.channels.size(); c++)
    {
        QCOMPARE(masked.channels[c].values.size(), full.channels[c].values.size());
        for (std::size_t i = 0; i < full.channels[c].values.size(); i++)
        {
            QVERIFY2(masked.channels[c].values[i] == full.channels[c].values[i],
                     qPrintable(QString("channel %1 sample %2: %3 with the mask, %4 without")
                                    .arg(c).arg(i).arg(masked.channels[c].values[i]).arg(full.channels[c].values[i])));
            any_above_a_byte = any_above_a_byte || full.channels[c].values[i] > 255.0;
        }
    }
    QVERIFY2(any_above_a_byte, "no value exceeded 255, so this fixture could not show a cleared high byte");
}

void TestFrameProcessor::processShortPeriodMoreSamples()
{
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
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
/// TestStepDetector::roundTripSameDataIsExact): use test files/agc_rnrz-l_trc_testfile.ch10 as BOTH
/// the calibration file and the "main" file being measured. Since they are the
/// same recording, the resulting calibration should reproduce the exact step
/// values (0, 6, 12, ... 60 dB) when re-applied — this is the full real
/// pipeline (derandomization, frame sync, word extraction, CalibrationExtractor,
/// StepDetector, interpolateCalibration) exercised together on genuine decoded
/// RNRZ-L data, rather than hand-built raw series.
void TestFrameProcessor::calibrationRoundTripOnRealFileProducesCleanSteps()
{
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
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

    std::vector<StepDefinition> steps;
    std::string step_error;
    QVERIFY2(StepDetector::parseStepConfig(toFsPath(step_toml), steps, step_error),
             step_error.c_str());
    QVERIFY(!steps.empty());

    // ---- Extraction pass: build calibration profiles from this same file ----
    CalibrationExtractor::Request req;
    req.calFilename       = toFsPath(filepath);
    req.timeChannelId      = time_id;
    req.pcmChannelId       = pcm_id;
    // 8000 bits is this recording's true minor-frame length; RNRZ-L; 0.8 Mbps (800 kbps, ~100
    // frames/s) drives the adaptive extract period. An ordinary Chapter 10 fixture, so it needs the
    // byte swap. Stated rather than inherited, so a change to the DTO default cannot silently
    // retune it.
    QVERIFY(resolveFrameSync("FE6B2840", std::string(), req.acquisition.sync) == FrameSyncError::None);
    req.acquisition.bitsInMinorFrame = 8000;
    req.acquisition.randomized       = true;
    req.acquisition.dataRateMbps     = 0.8;
    req.acquisition.swapBytes        = true;
    req.numReceivers       = 16;
    req.receiverChannels   = 3;
    req.steps              = steps;

    CalibrationExtractor extractor;
    QEventLoop loop;
    bool extract_ok = false;
    bool finished_already = false;
    QString summary;
    // The extraction reports from its own threads; the adapter delivers the end to this one.
    auto events = std::make_shared<QtProcessingEvents>(
        this, nullptr, nullptr,
        [&](bool ok) {
            extract_ok = ok;
            summary = QString::fromStdString(extractor.summary());
            finished_already = true;
            loop.quit();
        });
    extractor.start(req, events);
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
            setup.addParameter(std::string(kPrefixes[c]) + "_RCVR" + std::to_string(r + 1),
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
        if (!calibration_by_word.contains(series.word) || series.values.empty())
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

    const std::vector<uint8_t> payload = packBitString(bits);

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
    item.payload     = std::make_shared<const std::vector<uint8_t>>(payload);
    item.baseAbsSeconds = 0.0;
    item.packetBits   = static_cast<uint64_t>(bits.size());
    QVERIFY(queue.enqueue(item));

    PacketItem eos;
    eos.endOfStream = true;
    QVERIFY(queue.enqueue(eos));

    FrameProcessor fp;
    QVERIFY2(fp.process(p, &setup), "Synthetic off-phase-sync stream should process successfully");

    const ProcessedStreamData& r = fp.result();
    QVERIFY2(!r.channels.empty() && !r.channels[0].values.empty(),
             "Expected at least one extracted sample");
    // Only the boundary-aligned sync should yield a sample (word B = 15). If the
    // off-phase false positive after lock loss were wrongly extracted, the stale
    // word A (0) would be averaged in, producing 7.5 instead of 15.
    QCOMPARE(r.channels[0].values.front(), 15.0);
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

    const std::vector<uint8_t> payload = packBitString(bits);

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
    item.payload        = std::make_shared<const std::vector<uint8_t>>(payload);
    item.baseAbsSeconds = 0.0;
    item.packetBits     = static_cast<uint64_t>(bits.size());
    QVERIFY(queue.enqueue(item));

    PacketItem eos;
    eos.endOfStream = true;
    QVERIFY(queue.enqueue(eos));

    FrameProcessor fp;
    QVERIFY2(fp.process(p, &setup), "Synthetic non-linear-calibration stream should process successfully");

    const ProcessedStreamData& r = fp.result();
    QVERIFY2(!r.channels.empty() && !r.channels[0].values.empty(),
             "Expected at least one extracted sample");
    // Correct (average raw, then calibrate once): interpolate(mean(0,15)=7.5) = 10 dB.
    // Buggy (calibrate each frame, then average): mean(10, 160) = 85 dB.
    QVERIFY2(qAbs(r.channels[0].values.front() - 10.0) < 1e-9,
             qPrintable(QString("Expected 10.0 dB (raw averaged before calibration), got %1")
                            .arg(r.channels[0].values.front())));
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
    const QString filepath = testDataPath("test files/safran_testfile, sample.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("Safran test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
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
        std::vector<ProcessingParams*> params_list = { &p };
        std::string error;
        if (!rdr.prepare(p.filename, p.timeChannelId, params_list, error))
        {
            qWarning("prepare() failed (swap=%d): %s", int(swap), error.c_str());
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

void TestFrameProcessor::oddPayloadSwapSkipIsReported()
{
    // SwapBytes_PcmF1 refuses an odd byte count and returns before touching the
    // buffer. FrameProcessor used to discard that return, so with the swap ON an
    // odd-length payload was silently left unswapped while even-length packets in
    // the same recording were swapped - one stream carrying two byte orders, with
    // nothing in the log to say so. That silence is what made the Safran recording
    // hard to diagnose: channel 14 locked and channel 13 did not, under identical
    // settings, for a reason nothing surfaced.
    //
    // Channel 14 is the all-odd case: every payload is 875 bytes, so the swap is
    // inert for the whole stream and the operator's setting has no effect at all.
    const QString filepath = testDataPath("test files/safran_testfile, sample.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("Safran test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
    const int time_id = reader.getCurrentTimeChannelID();
    if (time_id < 0)
        QSKIP("Missing time channel in test file");

    constexpr int kOddPayloadChannelId = 14;
    constexpr int kPrnFrameBits  = 32767;
    constexpr int kPrnFrameWords =
        (kPrnFrameBits + PCMConstants::kCommonWordLen - 1) / PCMConstants::kCommonWordLen;

    ProcessingParams p = makeTestParams(filepath, time_id, kOddPayloadChannelId,
                                        0x334AABBF, 32, kPrnFrameWords, kPrnFrameBits);
    p.startSeconds    = 0;
    p.stopSeconds     = UINT64_MAX;
    p.samplePeriodSec = 1.0;
    p.isRandomized    = false;
    p.swapBytes       = true;   // asked for - and silently not delivered
    p.mode            = StreamMode::FrameSyncLockStats;

    PacketQueue queue;
    p.packetQueue = &queue;
    Ch10PacketReader rdr;
    std::vector<ProcessingParams*> params_list = { &p };
    std::string error;
    QVERIFY2(rdr.prepare(p.filename, p.timeChannelId, params_list, error), error.c_str());

    FrameSetup empty_setup;
    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);

    QFuture<void> future = QtConcurrent::run([&rdr]() { rdr.run(); });
    fp.process(p, &empty_setup);
    future.waitForFinished();
    const QStringList& log = cap.logs;

    const QString joined = log.join(" | ");
    QVERIFY2(joined.contains("odd-length"),
             qPrintable(QStringLiteral("the skip must be reported; log was: %1").arg(joined)));
    // Every packet on this channel is odd, so the message must say the stream was
    // forced to Legacy Chapter 10 regardless of the setting - not warn about a
    // half-swapped stream, which is the other, genuinely corrupting case.
    QVERIFY2(joined.contains("processed as Legacy Chapter 10 regardless of the setting"),
             qPrintable(QStringLiteral("all-odd stream must report the setting as unreachable, "
                                       "not a mixed-order warning; log was: %1").arg(joined)));
}

void TestFrameProcessor::offPhaseSyncsAreNotCountedAsBoundaryAligned()
{
    // The off-phase branch of the scanner used to increment boundary_syncs, so a match
    // that was NOT on a frame boundary was reported as "boundary-aligned" - the exact
    // opposite of the truth - and it set current_sync_run without ever folding it into
    // max_sync_run. A real failure report read "1 total | 1 boundary-aligned |
    // longest run=0", which is self-contradictory and pointed the reader at the wrong
    // cause.
    //
    // Reproduced deterministically with the RIGHT sync pattern and a WRONG frame
    // length: matches occur at the real 800-bit spacing, so minor_frame_bit_count
    // never equals the configured 1234 and no match is ever boundary-aligned.
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
    const int pcm_id  = reader.getFirstPCMChannelID();
    const int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    constexpr int kWrongFrameBits = 1234;   // real frames are 800 bits
    ProcessingParams p = makeTestParams(filepath, time_id, pcm_id,
                                        0xFE6B2840, 32, 49, kWrongFrameBits);
    p.startSeconds    = 0;
    p.stopSeconds     = UINT64_MAX;
    p.samplePeriodSec = 1.0;
    p.isRandomized    = true;
    p.mode            = StreamMode::FrameSyncLockStats;

    PacketQueue queue;
    p.packetQueue = &queue;
    Ch10PacketReader rdr;
    std::vector<ProcessingParams*> params_list = { &p };
    std::string error;
    QVERIFY2(rdr.prepare(p.filename, p.timeChannelId, params_list, error), error.c_str());

    FrameSetup empty_setup;
    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);

    QFuture<void> future = QtConcurrent::run([&rdr]() { rdr.run(); });
    const bool ok = fp.process(p, &empty_setup);
    future.waitForFinished();
    const QString reported = cap.errors.isEmpty() ? QString() : cap.errors.last();

    QVERIFY2(!ok, "a wrong frame length must not yield extracted frames");
    QVERIFY2(reported.contains("no frames extracted"),
             qPrintable(QStringLiteral("unexpected error: %1").arg(reported)));

    // The fix: every match here is off-phase, so the count must be zero. Before the
    // fix this read as thousands of "boundary-aligned" syncs.
    QVERIFY2(reported.contains("0 boundary-aligned"),
             qPrintable(QStringLiteral("off-phase matches must not be counted as "
                                       "boundary-aligned; message was:\n%1").arg(reported)));

}

void TestFrameProcessor::failureReportNamesChannelAndSyncPattern()
{
    // Four streams are processed by parallel workers into one shared log, so their
    // messages interleave in an order that is not deterministic between runs. Before
    // this, a failure report carried no identity at all: four "pattern was not found"
    // lines, no way to tell which channel each belonged to.
    //
    // The report also printed sync_len but never the pattern itself - and PRN-11 and
    // PRN-15 are both 32-bit patterns, so a stream running the wrong one was invisible
    // in the report. That is exactly how a PRN-11 config on a PRN-15 stream went
    // undiagnosed: every printed field looked reasonable.
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
    const int pcm_id  = reader.getFirstPCMChannelID();
    const int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    // Right pattern, wrong frame length: matches occur at the real 800-bit spacing so
    // none is ever boundary-aligned, which lands in the detailed report.
    ProcessingParams p = makeTestParams(filepath, time_id, pcm_id,
                                        0xFE6B2840, 32, 49, 1234);
    p.startSeconds    = 0;
    p.stopSeconds     = UINT64_MAX;
    p.samplePeriodSec = 1.0;
    p.isRandomized    = true;
    p.mode            = StreamMode::FrameSyncLockStats;
    p.streamLabel     = "PCM03";

    PacketQueue queue;
    p.packetQueue = &queue;
    Ch10PacketReader rdr;
    std::vector<ProcessingParams*> params_list = { &p };
    std::string error;
    QVERIFY2(rdr.prepare(p.filename, p.timeChannelId, params_list, error), error.c_str());

    FrameSetup empty_setup;
    FrameProcessor fp;
    Capture cap;
    captureEvents(fp, cap);

    QFuture<void> future = QtConcurrent::run([&rdr]() { rdr.run(); });
    fp.process(p, &empty_setup);
    future.waitForFinished();
    const QStringList& log = cap.logs;
    const QString reported = cap.errors.isEmpty() ? QString() : cap.errors.last();

    const QString expected_tag = QStringLiteral("[CH %1 PCM03]").arg(pcm_id);

    // Identity on the failure report...
    QVERIFY2(reported.startsWith(expected_tag),
             qPrintable(QStringLiteral("report must lead with %1; got:\n%2")
                            .arg(expected_tag, reported)));

    // ...and on EVERY routine message too, not just the error. A tagged error among
    // untagged progress lines is still ambiguous once four streams interleave.
    for (const QString& m : log)
    {
        QVERIFY2(m.startsWith(expected_tag),
                 qPrintable(QStringLiteral("untagged log line: %1").arg(m)));
    }
    QVERIFY2(!log.isEmpty(), "expected at least one progress message");

    // The pattern itself, uppercase hex - the field that distinguishes PRN-11 from
    // PRN-15 when both report sync_len=32.
    QVERIFY2(reported.contains(QStringLiteral("pattern 0xFE6B2840")),
             qPrintable(QStringLiteral("the sync pattern must be printed; got:\n%1").arg(reported)));
    // An all-ones mask is every ordinary configuration, so printing it is noise. It
    // appears only when it is actually constraining the match.
    QVERIFY2(!reported.contains(QStringLiteral("mask")),
             qPrintable(QStringLiteral("a default mask must not be printed; got:\n%1")
                            .arg(reported)));

    qInfo("report reads:\n%s", qPrintable(reported));
}

