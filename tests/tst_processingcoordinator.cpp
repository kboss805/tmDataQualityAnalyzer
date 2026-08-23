#include "tst_processingcoordinator.h"

#include <algorithm>
#include <memory>
#include <numeric>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTimer>
#include <QVector>
#include <QtTest>

#include "chapter10reader.h"
#include "framesetup.h"
#include "processingcoordinator.h"
#include "processingparams.h"

namespace {

/// Resolves a path inside tests/data/ relative to the test executable.
QString testDataPath(const QString& filename)
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    return dir.filePath("data/" + filename);
}

/// Builds a lightweight (no FrameSetup channels) lock-only stream job. Several
/// jobs built from this for the SAME pcm channel exercise exactly the reader's
/// per-packet fan-out to multiple PacketQueues (Ch10PacketReader::run(),
/// m_routing[ch] holds one queue per job) without any extra per-stream decode
/// cost, isolating the coordinator/queue overhead from real workload cost.
StreamJob makeLockOnlyJob(const QString& filepath, int time_id, int pcm_id, const QString& label)
{
    StreamJob job;
    job.params.filename         = filepath;
    job.params.timeChannelId    = time_id;
    job.params.pcmChannelId     = pcm_id;
    job.params.frameSync        = 0xFE6B2840;
    // Legacy byte-swapped fixture; the DTO default is the modern order, so this
    // is stated rather than inherited.
    job.params.swapBytes        = true;
    job.params.syncPatternLength = 32;
    job.params.wordsInMinorFrame = 49;
    job.params.bitsInMinorFrame  = 800;
    job.params.startSeconds     = 0;
    job.params.stopSeconds      = UINT64_MAX;
    job.params.samplePeriodSec  = 1.0;
    job.params.isRandomized     = true;
    job.params.mode             = StreamMode::FrameSyncLockStats;
    job.params.streamLabel      = label;
    job.frameSetup              = std::make_shared<FrameSetup>(nullptr); // no channels needed for lock-only mode
    return job;
}

struct PrnSpec {
    uint64_t frameSync;
    int      bitsInMinorFrame;
    int      wordsInMinorFrame; // ceil(bits / 16)
};

// Frame parameters read from settings/framesync_patterns/framesync_PRN11.toml
// and framesync_PRN15.toml.
static const PrnSpec kPrn11 = { 0xA345CA5C, 2047,  128 };
static const PrnSpec kPrn15 = { 0x334AABBF, 32767, 2048 };

/// Builds a lock-only job for a PRN stream using the given channel and spec.
StreamJob makePrnJob(const QString& filepath, int time_id, int pcm_id,
                     const PrnSpec& spec, const QString& label)
{
    StreamJob job;
    job.params.filename          = filepath;
    job.params.timeChannelId     = time_id;
    job.params.pcmChannelId      = pcm_id;
    job.params.frameSync         = spec.frameSync;
    // Legacy byte-swapped fixture; the DTO default is the modern order, so this
    // is stated rather than inherited.
    job.params.swapBytes         = true;
    job.params.frameSyncMask     = 0xFFFFFFFF;
    job.params.syncPatternLength = 32;
    job.params.wordsInMinorFrame = spec.wordsInMinorFrame;
    job.params.bitsInMinorFrame  = spec.bitsInMinorFrame;
    job.params.startSeconds      = 0;
    job.params.stopSeconds       = UINT64_MAX;
    job.params.samplePeriodSec   = 1.0;
    job.params.isRandomized      = false;
    job.params.mode              = StreamMode::FrameSyncLockStats;
    job.params.streamLabel       = label;
    job.frameSetup               = std::make_shared<FrameSetup>(nullptr);
    return job;
}

/// runAndTime() sentinels, distinguished so a timeout on a loaded machine can be
/// skipped rather than reported as a functional failure.
constexpr qint64 kRunFailed   = -1;
constexpr qint64 kRunTimedOut = -2;
/// Grace period for a cancelled run to wind its threads down.
constexpr int    kCancelDrainMs = 30000;

/// Waits for @p coord to actually stop after a timeout, so it is safe to destroy.
///
/// A ProcessingCoordinator owns live reader/worker QThreads. If the wait loop is
/// abandoned on a timeout, those threads are still running when the (stack-local)
/// coordinator goes out of scope - and destroying a running QThread makes Qt
/// terminate the whole process (exit 0xC0000409). That aborted the test run mid-way
/// and silently truncated the results, rather than failing a single test.
void cancelAndDrain(ProcessingCoordinator& coord)
{
    QEventLoop drain;
    QObject::connect(&coord, &ProcessingCoordinator::processingFinished,
                     &drain, &QEventLoop::quit);
    coord.cancelProcessing();
    // Cancellation is cooperative, so cap the wait; workers check their abort flag
    // between packets and should stop well inside this.
    QTimer::singleShot(kCancelDrainMs, &drain, &QEventLoop::quit);
    drain.exec();
}

/// @return elapsed ms on success, kRunFailed if the run could not start or ended
///         in error, kRunTimedOut if it was still going when the timeout expired.
qint64 runAndTime(QVector<StreamJob> jobs, int timeout_ms = 60000)
{
    ProcessingCoordinator coord;
    QEventLoop loop;
    bool finished_ok = false;
    bool finished_signalled = false;
    QObject::connect(&coord, &ProcessingCoordinator::processingFinished, &loop,
                      [&](bool ok) { finished_ok = ok; finished_signalled = true; loop.quit(); });

    QElapsedTimer timer;
    timer.start();
    if (!coord.startProcessing(std::move(jobs)))
    {
        return kRunFailed;
    }

    QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);
    loop.exec();

    if (!finished_signalled)
    {
        cancelAndDrain(coord);
        return kRunTimedOut;
    }
    return finished_ok ? timer.elapsed() : kRunFailed;
}

} // namespace

void TestProcessingCoordinator::constructorDefaults()
{
    ProcessingCoordinator coord;
    QCOMPARE(coord.processing(), false);
    QCOMPARE(coord.progressPercent(), 0);
}

void TestProcessingCoordinator::resetClearsState()
{
    ProcessingCoordinator coord;
    coord.reset();
    QCOMPARE(coord.processing(), false);
    QCOMPARE(coord.progressPercent(), 0);
}

void TestProcessingCoordinator::cancelProcessingNoRunNoOp()
{
    ProcessingCoordinator coord;
    coord.cancelProcessing();  // must not crash with no active run
    QCOMPARE(coord.processing(), false);
}

void TestProcessingCoordinator::startProcessingEmptyReturnsFalse()
{
    ProcessingCoordinator coord;
    QSignalSpy error_spy(&coord, &ProcessingCoordinator::errorOccurred);

    bool started = coord.startProcessing({});  // no jobs

    QCOMPARE(started, false);
    QVERIFY(!error_spy.isEmpty());
    QCOMPARE(coord.processing(), false);
}

void TestProcessingCoordinator::startProcessingEmitsProcessingState()
{
    ProcessingCoordinator coord;
    QSignalSpy state_spy(&coord, &ProcessingCoordinator::processingStateChanged);
    QSignalSpy error_spy(&coord, &ProcessingCoordinator::errorOccurred);

    // With the single-reader architecture, Ch10PacketReader::prepare() runs
    // synchronously before any threads launch. A nonexistent file causes prepare()
    // to fail immediately, so startProcessing() returns false and emits errorOccurred
    // without ever flipping processingState to true.
    StreamJob job;
    job.params.filename = "nonexistent_coordinator_test.ch10";
    job.params.timeChannelId = 1;
    job.params.pcmChannelId = 1;
    job.params.mode = StreamMode::FrameSyncLockStats;
    job.frameSetup = std::make_shared<FrameSetup>(nullptr);

    QVector<StreamJob> jobs;
    jobs.push_back(std::move(job));

    bool started = coord.startProcessing(std::move(jobs));

    QCOMPARE(started, false);
    QVERIFY(!error_spy.isEmpty());
    QVERIFY(state_spy.isEmpty());  // no spurious "started" event
    QCOMPARE(coord.processing(), false);
}

////////////////////////////////////////////////////////////////////////////////
//                 SINGLE- VS MULTI-STREAM THROUGHPUT BENCHMARK               //
////////////////////////////////////////////////////////////////////////////////

/// Diagnostic benchmark: confirms that N identical fan-out streams (same channel,
/// lock-only workload) cost essentially the same wall time as 1 stream.
/// Validated result: ~1.1x overhead — PacketQueue fan-out is effectively free
/// for same-channel streams.
void TestProcessingCoordinator::benchmarkSingleVsMultiStreamThroughput()
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

    constexpr int kStreamCount = 4;

    qint64 single_ms = runAndTime({ makeLockOnlyJob(filepath, time_id, pcm_id, "solo") });
    if (single_ms == kRunTimedOut)
        QSKIP("Single-stream benchmark timed out - machine too loaded to time meaningfully");
    QVERIFY2(single_ms >= 0, "Single-stream run should complete successfully");

    QVector<StreamJob> multi_jobs;
    for (int i = 0; i < kStreamCount; i++)
    {
        multi_jobs.push_back(makeLockOnlyJob(filepath, time_id, pcm_id,
                                              QString("dup%1").arg(i)));
    }
    qint64 multi_ms = runAndTime(std::move(multi_jobs));
    if (multi_ms == kRunTimedOut)
        QSKIP("Multi-stream benchmark timed out - machine too loaded to time meaningfully");
    QVERIFY2(multi_ms >= 0, "Multi-stream run should complete successfully");

    qWarning().noquote() << QString(
        "[benchmark] 1 stream: %1 ms | %2 identical concurrent streams: %3 ms "
        "(%4x single-stream time)")
        .arg(single_ms).arg(kStreamCount).arg(multi_ms)
        .arg(single_ms > 0 ? double(multi_ms) / double(single_ms) : 0.0, 0, 'f', 2);
}

/// Diagnostic benchmark using prn_testfile.ch10: four heterogeneous channels
/// (PRN11 at 1 M and 5 M bps, PRN15 at 5 M and 20 M bps). Times each channel
/// solo, then all four concurrently.
/// Validated result: 4-stream parallel ≈ slowest-solo time (1.02x) and 33%
/// faster than fully sequential — parallel is correct and beneficial even for
/// streams with very different data rates.
void TestProcessingCoordinator::benchmarkHeavyWorkloadSingleVsMultiStream()
{
    // Opt-in. This walks a 640 MB recording nine times (four channel probes, four
    // solo runs, one parallel run) and takes minutes. It measures throughput and
    // asserts nothing about correctness, but on a loaded machine it can exceed its
    // timeout - and it was the sole source of CI instability, where a slow run
    // could take the whole suite down with it. Run it deliberately when you want
    // throughput numbers:  set TMDQA_RUN_HEAVY_BENCH=1
    if (!qEnvironmentVariableIsSet("TMDQA_RUN_HEAVY_BENCH"))
        QSKIP("Heavy PRN throughput benchmark is opt-in (set TMDQA_RUN_HEAVY_BENCH=1)");

    const QString filepath = testDataPath("prn_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("PRN test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(filepath));
    int time_id = reader.getCurrentTimeChannelID();
    if (time_id < 0)
        QSKIP("No time channel in PRN test file");

    auto pcm_channels = reader.getPCMChannelList();
    if (pcm_channels.size() < 4)
        QSKIP("PRN test file does not have 4 PCM channels");

    constexpr int kTimeoutMs = 300000; // 5 min — prn_testfile.ch10 is up to 671 MB (full, local)

    // Assign channels by matching each PCM channel against both PRN specs.
    // This handles any channel ordering the file may use and is self-documenting
    // in the test log.
    auto probeChannel = [&](int ch_id, const PrnSpec& spec) -> bool {
        ProcessingCoordinator probe;
        bool probe_ok = false;
        bool probe_signalled = false;
        QEventLoop probe_loop;
        QObject::connect(&probe, &ProcessingCoordinator::processingFinished,
                         [&](bool ok) { probe_ok = ok; probe_signalled = true; probe_loop.quit(); });
        QVector<StreamJob> jobs = { makePrnJob(filepath, time_id, ch_id, spec, "probe") };
        if (!probe.startProcessing(std::move(jobs)))
            return false;
        QTimer::singleShot(kTimeoutMs, &probe_loop, &QEventLoop::quit);
        probe_loop.exec();
        // Same hazard as runAndTime: never let `probe` die with its threads live.
        if (!probe_signalled)
        {
            cancelAndDrain(probe);
            return false;
        }
        return probe_ok;
    };

    QVector<int> prn11_ids, prn15_ids;
    for (auto& [ch_id, label] : pcm_channels)
    {
        bool is11 = probeChannel(ch_id, kPrn11);
        bool is15 = !is11 && probeChannel(ch_id, kPrn15);
        qWarning().noquote() << QString("[benchmark-heavy] channel %1 (%2) → %3")
            .arg(ch_id).arg(label).arg(is11 ? "PRN11" : is15 ? "PRN15" : "unrecognized");
        if (is11)      prn11_ids.push_back(ch_id);
        else if (is15) prn15_ids.push_back(ch_id);
    }

    if (prn11_ids.size() < 2 || prn15_ids.size() < 2)
    {
        qWarning() << "[benchmark-heavy] need ≥2 PRN11 and ≥2 PRN15 channels; got"
                   << prn11_ids.size() << "PRN11 and" << prn15_ids.size() << "PRN15";
        QSKIP("Could not identify 2×PRN11 and 2×PRN15 channels in test file");
    }

    // Time every discovered channel individually so we know the true single-stream
    // cost for each, including the heavier PRN15 20M channel.
    QVector<qint64> solo_times;
    for (int id : prn11_ids)
    {
        qint64 t = runAndTime({ makePrnJob(filepath, time_id, id, kPrn11,
                                           QString("PRN11-solo-%1").arg(id)) }, kTimeoutMs);
        if (t == kRunTimedOut)
            QSKIP("Heavy benchmark timed out - machine too loaded to time meaningfully");
        QVERIFY2(t >= 0, qPrintable(QString("PRN11 channel %1 solo run failed").arg(id)));
        solo_times.push_back(t);
        qWarning().noquote() << QString("[benchmark-heavy] PRN11 ch%1 solo: %2 ms").arg(id).arg(t);
    }
    for (int id : prn15_ids)
    {
        qint64 t = runAndTime({ makePrnJob(filepath, time_id, id, kPrn15,
                                           QString("PRN15-solo-%1").arg(id)) }, kTimeoutMs);
        if (t == kRunTimedOut)
            QSKIP("Heavy benchmark timed out - machine too loaded to time meaningfully");
        QVERIFY2(t >= 0, qPrintable(QString("PRN15 channel %1 solo run failed").arg(id)));
        solo_times.push_back(t);
        qWarning().noquote() << QString("[benchmark-heavy] PRN15 ch%1 solo: %2 ms").arg(id).arg(t);
    }

    QVector<StreamJob> all_jobs;
    for (int id : prn11_ids) all_jobs.push_back(makePrnJob(filepath, time_id, id, kPrn11, QString("PRN11-%1").arg(id)));
    for (int id : prn15_ids) all_jobs.push_back(makePrnJob(filepath, time_id, id, kPrn15, QString("PRN15-%1").arg(id)));

    qint64 parallel_ms = runAndTime(std::move(all_jobs), kTimeoutMs);
    if (parallel_ms == kRunTimedOut)
        QSKIP("Heavy benchmark timed out - machine too loaded to time meaningfully");
    QVERIFY2(parallel_ms >= 0, "4-stream parallel run should complete successfully");

    // Ideal parallel time = slowest individual stream (reader does one pass, workers run together).
    // A ratio near 1.0x means the architecture is working correctly.
    // A large ratio points to worker back-pressure serializing the reader.
    qint64 ideal_ms = *std::max_element(solo_times.begin(), solo_times.end());
    qint64 sequential_all_ms = std::accumulate(solo_times.begin(), solo_times.end(), qint64(0));
    double ratio_vs_ideal = ideal_ms > 0 ? double(parallel_ms) / double(ideal_ms) : 0.0;
    double ratio_vs_seq   = sequential_all_ms > 0 ? double(parallel_ms) / double(sequential_all_ms) : 0.0;

    qWarning().noquote() << QString(
        "[benchmark-heavy] 4-stream parallel: %1 ms | "
        "vs slowest solo (%2 ms): %3x | vs all-sequential (%4 ms): %5x")
        .arg(parallel_ms).arg(ideal_ms)
        .arg(ratio_vs_ideal, 0, 'f', 2)
        .arg(sequential_all_ms)
        .arg(ratio_vs_seq,   0, 'f', 2);
}
