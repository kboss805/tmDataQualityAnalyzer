#include "tst_processingcoordinator.h"

#include <algorithm>
#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>
#include <QVector>

#include "chapter10reader.h"
#include "qtbridge.h"
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
    job.params.filename         = toFsPath(filepath);
    job.params.timeChannelId    = time_id;
    job.params.pcmChannelId     = pcm_id;
    job.params.frameSync        = 0xFE6B2840;
    // Ordinary Chapter 10 fixture, so it needs the byte swap. Stated rather than
    // inherited, so a change to the DTO default cannot silently retune it.
    job.params.swapBytes        = true;
    job.params.syncPatternLength = 32;
    job.params.wordsInMinorFrame = 49;
    job.params.bitsInMinorFrame  = 800;
    job.params.startSeconds     = 0;
    job.params.stopSeconds      = UINT64_MAX;
    job.params.samplePeriodSec  = 1.0;
    job.params.isRandomized     = true;
    job.params.mode             = StreamMode::FrameSyncLockStats;
    job.params.streamLabel      = label.toStdString();
    job.frameSetup              = std::make_shared<FrameSetup>(); // no channels needed for lock-only mode
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

void TestProcessingCoordinator::workerReportsPrecedeTheResultAndFinishComesLast()
{
    // The worker raises its log lines on its own thread; the coordinator relays them, then
    // the stream's result, then the end of the run, all on this thread. The result is taken
    // the moment the worker says it is finished, so every line the worker raised must
    // already have been relayed - which holds only if the hand-off preserves order across
    // event kinds. It used to be Qt's queued signals; it is now QtProcessingEvents.
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
    const int pcm_id  = reader.getFirstPCMChannelID();
    const int time_id = reader.getCurrentTimeChannelID();
    if (pcm_id < 0 || time_id < 0)
        QSKIP("Missing channels in test file");

    ProcessingCoordinator coord;
    QStringList order;           // one entry per relayed event, in arrival order
    QList<int> progress;         // every progressChanged value, in arrival order
    bool run_ok = false;
    QEventLoop loop;
    QObject::connect(&coord, &ProcessingCoordinator::logMessageReceived,
                     [&](const QString& m) { order << "log:" + m; });
    QObject::connect(&coord, &ProcessingCoordinator::errorOccurred,
                     [&](const QString& m) { order << "error:" + m; });
    QObject::connect(&coord, &ProcessingCoordinator::streamProcessed,
                     [&](const ProcessedStreamData&) { order << "result"; });
    QObject::connect(&coord, &ProcessingCoordinator::progressChanged,
                     [&](int percent) { progress << percent; });
    QObject::connect(&coord, &ProcessingCoordinator::processingFinished, &loop,
                     [&](bool ok) { run_ok = ok; order << "finished"; loop.quit(); });

    QVector<StreamJob> jobs = { makeLockOnlyJob(filepath, time_id, pcm_id, "Ch " + QString::number(pcm_id)) };
    QVERIFY(coord.startProcessing(std::move(jobs)));
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    loop.exec();
    if (order.isEmpty() || order.last() != "finished")
    {
        cancelAndDrain(coord);
        QFAIL("the run did not finish in time");
    }

    QVERIFY2(run_ok, qPrintable(order.join("\n")));
    // The run ends with completion, and the result is the event just before it.
    QCOMPARE(order.last(), QStringLiteral("finished"));
    QCOMPARE(order.count("result"), 1);
    QCOMPARE(order.at(order.size() - 2), QStringLiteral("result"));
    // The coordinator's own opening line, then the worker's (tagged "[CH <id>"), and the
    // worker's last line - its completion summary - is relayed before the result.
    QVERIFY2(order.first().startsWith("log:--- Processing 1 stream(s)"), qPrintable(order.first()));
    const QString tag = QStringLiteral("log:[CH %1] ").arg(pcm_id);
    const int result_at = order.indexOf("result");
    int worker_lines = 0;
    int complete_at = -1;
    for (int i = 0; i < result_at; ++i)
    {
        if (order.at(i).startsWith(tag))
        {
            ++worker_lines;
            if (order.at(i).contains("complete —"))
                complete_at = i;
        }
    }
    QVERIFY2(worker_lines >= 3, qPrintable(order.join("\n")));       // started, totals, complete
    QVERIFY2(complete_at >= 0, qPrintable(order.join("\n")));       // its summary came first
    for (int i = result_at + 1; i < order.size() - 1; ++i)
    {
        QVERIFY2(!order.at(i).startsWith(tag), qPrintable("worker line after the result: " + order.at(i)));
    }
    QVERIFY(std::none_of(order.begin(), order.end(),
                         [](const QString& e) { return e.startsWith("error:"); }));

    // The progress bar: 0 when the run starts, and 100 reported TWICE - once by the reader
    // when it has read the whole file, once by the coordinator when it finishes. A reader
    // whose progress never reached the coordinator would leave exactly one. The reader's
    // last event is queued by a thread that is still ending when the run's own finish is
    // delivered, so it may land just after - wait for it rather than race it.
    QCOMPARE(progress.first(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(std::count(progress.begin(), progress.end(), 100)), 2, 5000);
}

void TestProcessingCoordinator::aRecordingUnderANonAsciiFolderProcessesEndToEnd()
{
    // The whole path from a job's file name to a finished stream, with the recording under
    // folders whose names no Western code page can spell. The two readers used to disagree
    // about such a path (one encoded it as the local code page, the other as UTF-8), so a
    // file would list its channels and then fail at Process; the coordinator is where the
    // job's file name is handed to the reader.
    const QString source = testDataPath("test files/step_cal, example.ch10");
    if (!QFileInfo::exists(source))
        QSKIP("step_cal fixture not available");

    QTemporaryDir dir;
    const QString folder = QStringLiteral("caf") + QChar(0x00E9) + QStringLiteral("-")
                           + QChar(0x03A9);
    QVERIFY(QDir(dir.path()).mkdir(folder));
    const QString copy = QDir(dir.path()).filePath(folder + "/recording.ch10");
    QVERIFY(QFile::copy(source, copy));

    Chapter10Reader reader;
    QVERIFY2(reader.loadChannels(toFsPath(copy)), "the metadata reader must open the non-ASCII path");
    const int pcm_id  = reader.getFirstPCMChannelID();
    const int time_id = reader.getCurrentTimeChannelID();
    QVERIFY(pcm_id >= 0 && time_id >= 0);

    // step_cal's acquisition parameters: 800-bit frames, ordinary byte order, not randomized.
    StreamJob job = makeLockOnlyJob(copy, time_id, pcm_id, "Ch " + QString::number(pcm_id));
    job.params.isRandomized     = false;
    job.params.wordsInMinorFrame = 50;

    ProcessingCoordinator coord;
    QEventLoop loop;
    bool finished_ok = false;
    bool got_result = false;
    QStringList errors;
    QObject::connect(&coord, &ProcessingCoordinator::streamProcessed,
                     [&](const ProcessedStreamData& d) { got_result = d.hasSamples(); });
    QObject::connect(&coord, &ProcessingCoordinator::errorOccurred,
                     [&](const QString& m) { errors << m; });
    QObject::connect(&coord, &ProcessingCoordinator::processingFinished, &loop,
                     [&](bool ok) { finished_ok = ok; loop.quit(); });

    QVector<StreamJob> jobs = { job };
    QVERIFY2(coord.startProcessing(std::move(jobs)), qPrintable(errors.join("\n")));
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY2(errors.isEmpty(), qPrintable(errors.join("\n")));
    QVERIFY2(finished_ok, "the run did not finish successfully");
    QVERIFY2(got_result, "the stream produced no samples");
}

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

// What the Cancel button does: the run ends, unsuccessfully, with no stream's result delivered. This
// path had no test of its own - the suite only cancelled to clean up after a timeout - so it is
// pinned here, on the recording the other end-to-end cases use.
void TestProcessingCoordinator::cancelProcessingEndsTheRunWithoutAResult()
{
    const QString source = testDataPath("test files/step_cal, example.ch10");
    if (!QFileInfo::exists(source))
        QSKIP("step_cal fixture not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(source)));
    const int pcm_id  = reader.getFirstPCMChannelID();
    const int time_id = reader.getCurrentTimeChannelID();
    QVERIFY(pcm_id >= 0 && time_id >= 0);

    StreamJob job = makeLockOnlyJob(source, time_id, pcm_id, "Ch " + QString::number(pcm_id));
    job.params.isRandomized      = false;
    job.params.wordsInMinorFrame = 50;

    ProcessingCoordinator coord;
    QEventLoop loop;
    bool finished = false;
    bool finished_ok = true;
    int results = 0;
    QList<bool> states;
    QObject::connect(&coord, &ProcessingCoordinator::streamProcessed,
                     [&](const ProcessedStreamData&) { ++results; });
    QObject::connect(&coord, &ProcessingCoordinator::processingStateChanged,
                     [&](bool active) { states << active; });
    QObject::connect(&coord, &ProcessingCoordinator::processingFinished, &loop,
                     [&](bool ok) { finished = true; finished_ok = ok; loop.quit(); });

    QVector<StreamJob> jobs = { job };
    QVERIFY(coord.startProcessing(std::move(jobs)));
    QVERIFY(coord.processing());
    coord.cancelProcessing();               // before the recording can have been read through
    QTimer::singleShot(kCancelDrainMs, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY2(finished, "a cancelled run never reported its end");
    QVERIFY(!finished_ok);
    QCOMPARE(results, 0);
    QVERIFY(!coord.processing());
    QCOMPARE(coord.progressPercent(), 100);
    QCOMPARE(states, (QList<bool>{true, false}));
}

// Closing the window mid-run destroys the coordinator while its threads are going. They post their
// reports to it, so they have to be gone before it is; a report delivered to a dead object, or a
// thread outliving it, would crash here or in whatever test came next.
void TestProcessingCoordinator::destroyingTheCoordinatorMidRunIsSafe()
{
    const QString source = testDataPath("test files/step_cal, example.ch10");
    if (!QFileInfo::exists(source))
        QSKIP("step_cal fixture not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(source)));
    const int pcm_id  = reader.getFirstPCMChannelID();
    const int time_id = reader.getCurrentTimeChannelID();
    QVERIFY(pcm_id >= 0 && time_id >= 0);

    StreamJob job = makeLockOnlyJob(source, time_id, pcm_id, "Ch " + QString::number(pcm_id));
    job.params.isRandomized      = false;
    job.params.wordsInMinorFrame = 50;

    int deliveries = 0;
    {
        ProcessingCoordinator coord;
        QObject::connect(&coord, &ProcessingCoordinator::logMessageReceived,
                         [&](const QString&) { ++deliveries; });
        QVector<StreamJob> jobs = { job };
        QVERIFY(coord.startProcessing(std::move(jobs)));
    }   // destroyed while the run is going

    // Anything still queued for the dead object must be discarded, not delivered.
    const int before = deliveries;
    QCoreApplication::processEvents();
    QThread::msleep(100);
    QCoreApplication::processEvents();
    QCOMPARE(deliveries, before);
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
    job.frameSetup = std::make_shared<FrameSetup>();

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
    const QString filepath = testDataPath("test files/agc_rnrz-l_trc_testfile.ch10");
    if (!QFileInfo::exists(filepath))
        QSKIP("RNRZ-L test file not available");

    Chapter10Reader reader;
    QVERIFY(reader.loadChannels(toFsPath(filepath)));
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
