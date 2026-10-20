#include "tst_pipelinerunner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "chapter10reader.h"
#include "pipelinerunner.h"
#include "qtbridge.h"

namespace {

/// Records everything a run reports, from whichever thread reports it, and lets a test wait for the end.
class Recorder final : public PipelineEvents
{
public:
    void logMessage(const std::string& m) override
    {
        add("log:" + m);
        maybePark(m);
    }
    void errorOccurred(const std::string& m) override { add("error:" + m); }
    void progressUpdated(int p) override { add("progress:" + std::to_string(p)); }

    void streamProcessed(ProcessedStreamData data) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back("stream:" + std::to_string(data.jobIndex));
        m_results.push_back(std::move(data));
    }

    void processingFinished(bool success) override
    {
        if (m_on_finished)
            m_on_finished();   // on the reporting thread, before the end is recorded
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_events.push_back(success ? "finished:true" : "finished:false");
            ++m_finish_count;
            m_success = success;
        }
        m_cv.notify_all();
    }

    /// Parks the first thread that logs a line containing @p needle, inside logMessage(), until
    /// release(). That freezes one worker (or the reader) at a chosen point in a real run, so a test
    /// can act at exactly that moment instead of racing for it.
    void parkOn(std::string needle)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_gate_needle = std::move(needle);
    }
    bool waitParked(int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return m_parked; });
    }
    void release()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_released = true;
        }
        m_cv.notify_all();
    }
    /// Runs @p hook on the reporting thread when the end arrives. Set before the run starts.
    void onFinished(std::function<void()> hook) { m_on_finished = std::move(hook); }

    /// Waits until some recorded event starts with one of @p prefixes.
    bool waitForEvent(const std::vector<std::string>& prefixes, int timeout_ms) const
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline)
        {
            for (const std::string& e : events())
                for (const std::string& p : prefixes)
                    if (e.rfind(p, 0) == 0)
                        return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    bool waitFinished(int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return m_finish_count > 0; });
    }

    std::vector<std::string> events() const { std::lock_guard<std::mutex> lock(m_mutex); return m_events; }
    std::vector<ProcessedStreamData> results() const { std::lock_guard<std::mutex> lock(m_mutex); return m_results; }
    int  finishCount() const { std::lock_guard<std::mutex> lock(m_mutex); return m_finish_count; }
    bool success() const { std::lock_guard<std::mutex> lock(m_mutex); return m_success; }

    int count(const std::string& exact) const
    {
        const auto all = events();
        return static_cast<int>(std::count(all.begin(), all.end(), exact));
    }
    bool anyStartsWith(const std::string& prefix) const
    {
        for (const std::string& e : events())
            if (e.rfind(prefix, 0) == 0)
                return true;
        return false;
    }
    std::string joined() const
    {
        std::string out;
        for (const std::string& e : events())
            out += e + "\n";
        return out;
    }

private:
    void add(std::string e) { std::lock_guard<std::mutex> lock(m_mutex); m_events.push_back(std::move(e)); }

    void maybePark(const std::string& message)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_gate_needle.empty() || m_parked || message.find(m_gate_needle) == std::string::npos)
            return;
        m_parked = true;
        m_cv.notify_all();
        m_cv.wait(lock, [this] { return m_released; });
    }

    mutable std::mutex               m_mutex;
    std::condition_variable          m_cv;
    std::vector<std::string>         m_events;
    std::vector<ProcessedStreamData> m_results;
    int                              m_finish_count = 0;
    bool                             m_success = false;
    std::string                      m_gate_needle;
    bool                             m_parked = false;
    bool                             m_released = false;
    std::function<void()>            m_on_finished;
};

/// Frees a parked thread however the test ends, so a failed check cannot leave the runner's
/// destructor waiting on a thread that is waiting on the test.
struct ReleaseOnExit
{
    Recorder& recorder;
    ~ReleaseOnExit() { recorder.release(); }
};

/// A small, committed, ordinary Chapter 10 recording with one PCM channel of interest.
struct Fixture
{
    QString path;
    int     time_id = -1;
    int     pcm_id  = -1;
};

bool loadFixture(Fixture& f)
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    f.path = dir.filePath("data/test files/step_cal, example.ch10");
    if (!QFileInfo::exists(f.path))
        return false;
    Chapter10Reader meta;
    if (!meta.loadChannels(toFsPath(f.path)))
        return false;
    f.time_id = meta.getCurrentTimeChannelID();
    f.pcm_id  = meta.getFirstPCMChannelID();
    return f.time_id >= 0 && f.pcm_id >= 0;
}

/// A lock-only stream over the fixture. @p sync other than the recording's own finds nothing.
StreamJob lockJob(const Fixture& f, int source_id = 0, std::uint64_t sync = 0xFE6B2840)
{
    StreamJob job;
    job.params.filename          = toFsPath(f.path);
    job.params.sourceId          = source_id;
    job.params.timeChannelId     = f.time_id;
    job.params.pcmChannelId      = f.pcm_id;
    job.params.frameSync         = sync;
    job.params.syncPatternLength = 32;
    job.params.wordsInMinorFrame = 50;
    job.params.bitsInMinorFrame  = 800;
    job.params.startSeconds      = 0;
    job.params.stopSeconds       = UINT64_MAX;
    job.params.samplePeriodSec   = 1.0;
    job.params.swapBytes         = true;
    job.params.isRandomized      = false;
    job.params.mode              = StreamMode::FrameSyncLockStats;
    job.params.streamLabel       = "Ch " + std::to_string(f.pcm_id);
    job.frameSetup               = std::make_shared<FrameSetup>();
    return job;
}

constexpr int kRunTimeoutMs = 120000;

} // namespace

void TestPipelineRunner::aSingleStreamDeliversItsResultThenTheEnd()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    QVERIFY2(runner.start({lockJob(fx, 7)}, rec, error), error.c_str());
    QVERIFY2(rec->waitFinished(kRunTimeoutMs), rec->joined().c_str());
    runner.wait();

    QVERIFY(!runner.running());
    QVERIFY2(rec->success(), rec->joined().c_str());
    QCOMPARE(rec->finishCount(), 1);

    const auto results = rec->results();
    QCOMPARE(results.size(), std::size_t{1});
    QCOMPARE(results[0].jobIndex, 0);
    QCOMPARE(results[0].sourceId, 7);                 // the job's own source id is carried through
    QVERIFY(results[0].hasSamples());
    QVERIFY(!rec->anyStartsWith("error:"));

    // The end is the last thing said; the stream's worker lines all come before its result.
    const auto events = rec->events();
    QCOMPARE(events.back(), std::string("finished:true"));
    const auto result_at = std::find(events.begin(), events.end(), "stream:0") - events.begin();
    const std::string tag = "log:[CH " + std::to_string(fx.pcm_id) + "] ";
    int worker_lines = 0;
    for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(events.size()); ++i)
    {
        if (events[i].rfind(tag, 0) == 0)
        {
            ++worker_lines;
            QVERIFY2(i < result_at, ("a worker line after the result: " + events[i]).c_str());
        }
    }
    QVERIFY2(worker_lines >= 3, rec->joined().c_str());   // started, totals, complete

    // The reader's progress passes through and reaches 100 once; the runner adds none of its own.
    QVERIFY(rec->anyStartsWith("progress:"));
    QCOMPARE(rec->count("progress:100"), 1);
}

void TestPipelineRunner::severalStreamsOnOneChannelEachDeliverTheirOwnResult()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    QVERIFY2(runner.start({lockJob(fx, 10), lockJob(fx, 11), lockJob(fx, 12)}, rec, error), error.c_str());
    QVERIFY2(rec->waitFinished(kRunTimeoutMs), rec->joined().c_str());
    runner.wait();

    QVERIFY2(rec->success(), rec->joined().c_str());
    const auto results = rec->results();
    QCOMPARE(results.size(), std::size_t{3});

    // Each stream's result says which job it is and which source that job named, whatever order
    // the workers happened to finish in.
    std::set<int> indices;
    for (const ProcessedStreamData& r : results)
    {
        indices.insert(r.jobIndex);
        QCOMPARE(r.sourceId, 10 + r.jobIndex);
        QVERIFY(r.hasSamples());
        QCOMPARE(r.timesSec.size(), results[0].timesSec.size());   // same data, same samples
    }
    QCOMPARE(indices, (std::set<int>{0, 1, 2}));
    QCOMPARE(rec->finishCount(), 1);
    QCOMPARE(rec->events().back(), std::string("finished:true"));
}

void TestPipelineRunner::aStreamThatFindsNoSyncDoesNotStopTheOthers()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    // Job 0 looks for a pattern the recording does not contain; job 1 is right.
    QVERIFY2(runner.start({lockJob(fx, 0, 0x12345678), lockJob(fx, 1)}, rec, error), error.c_str());
    QVERIFY2(rec->waitFinished(kRunTimeoutMs), rec->joined().c_str());
    runner.wait();

    // One stream earned a result, so the run succeeded; the other said why it did not.
    QVERIFY2(rec->success(), rec->joined().c_str());
    const auto results = rec->results();
    QCOMPARE(results.size(), std::size_t{1});
    QCOMPARE(results[0].jobIndex, 1);
    QVERIFY2(rec->anyStartsWith("error:[CH " + std::to_string(fx.pcm_id) + "] no frame sync found"),
             rec->joined().c_str());
}

void TestPipelineRunner::aRunInWhichEveryStreamFailsEndsUnsuccessfully()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    QVERIFY2(runner.start({lockJob(fx, 0, 0x12345678)}, rec, error), error.c_str());
    QVERIFY2(rec->waitFinished(kRunTimeoutMs), rec->joined().c_str());
    runner.wait();

    QVERIFY(!rec->success());
    QCOMPARE(rec->finishCount(), 1);
    QVERIFY(rec->results().empty());
    QVERIFY(rec->anyStartsWith("error:"));
    QCOMPARE(rec->events().back(), std::string("finished:false"));
}

void TestPipelineRunner::aRecordingThatCannotBeOpenedFailsBeforeAnythingRuns()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StreamJob missing = lockJob(fx);
    missing.params.filename = toFsPath(dir.filePath("no-such-recording.ch10"));

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    QVERIFY(!runner.start({missing}, rec, error));
    QVERIFY2(!error.empty(), "a failed start must say why");
    QVERIFY(!runner.running());
    QVERIFY2(rec->events().empty(), "a start that fails reports nothing to the sink");

    // And the runner is not left stuck: a good run starts straight afterwards.
    error.clear();
    QVERIFY2(runner.start({lockJob(fx)}, rec, error), error.c_str());
    QVERIFY2(rec->waitFinished(kRunTimeoutMs), rec->joined().c_str());
    runner.wait();
    QVERIFY(rec->success());
}

void TestPipelineRunner::noJobsIsRefused()
{
    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    QVERIFY(!runner.start({}, rec, error));
    QVERIFY(!error.empty());
    QVERIFY(!runner.running());
    QVERIFY(rec->events().empty());
}

void TestPipelineRunner::aSecondStartWhileRunningIsRefused()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto first = std::make_shared<Recorder>();
    auto second = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    QVERIFY2(runner.start({lockJob(fx)}, first, error), error.c_str());

    error.clear();
    QVERIFY(!runner.start({lockJob(fx)}, second, error));
    QVERIFY2(!error.empty(), "a refused start must say why");

    // The refusal did not disturb the run in flight, and the second sink heard nothing.
    QVERIFY2(first->waitFinished(kRunTimeoutMs), first->joined().c_str());
    runner.wait();
    QVERIFY(first->success());
    QCOMPARE(first->finishCount(), 1);
    QVERIFY(second->events().empty());
}

void TestPipelineRunner::cancelEndsTheRunUnsuccessfullyWithNoResult()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    QVERIFY2(runner.start({lockJob(fx), lockJob(fx, 1)}, rec, error), error.c_str());
    runner.cancel();                       // before any worker can have finished the recording
    QVERIFY2(rec->waitFinished(kRunTimeoutMs), "a cancelled run never reported its end");
    runner.wait();

    QVERIFY(!rec->success());
    QCOMPARE(rec->finishCount(), 1);
    QVERIFY2(rec->results().empty(), "a cancelled run must not deliver a result");
    QCOMPARE(rec->events().back(), std::string("finished:false"));
    QVERIFY(!runner.running());

    runner.cancel();                       // nothing in flight: a no-op, not a crash
}

// A stream can finish in the instant a cancel lands. Cancelling aborts the workers, so most of the
// time the result is dropped simply because the stream did not finish - which hides the rule under
// test: a stream that DID finish after the cancel was asked for still delivers nothing, and a run
// that was cancelled fails even though another stream had already delivered. Freeing that moment:
// job 1's worker is parked on its last log line ("complete"), after its work and before it reports,
// while job 0 runs through and delivers.
void TestPipelineRunner::cancelAfterOneStreamDeliveredDropsTheRestAndFailsTheRun()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto rec = std::make_shared<Recorder>();
    rec->parkOn("second] complete");
    ReleaseOnExit guard{*rec};

    StreamJob second = lockJob(fx, 1);
    second.params.streamLabel = "second";     // so its log lines are tagged apart from job 0's

    PipelineRunner runner;
    std::string error;
    QVERIFY2(runner.start({lockJob(fx, 0), second}, rec, error), error.c_str());
    QVERIFY2(rec->waitParked(kRunTimeoutMs), rec->joined().c_str());
    QVERIFY2(rec->waitForEvent({"stream:0"}, kRunTimeoutMs), rec->joined().c_str());

    runner.cancel();                          // job 1 has finished its work but not reported it
    rec->release();
    QVERIFY2(rec->waitFinished(kRunTimeoutMs), rec->joined().c_str());
    runner.wait();

    QVERIFY2(!rec->success(), "a cancelled run must fail even though one stream had delivered");
    const auto results = rec->results();
    QCOMPARE(results.size(), std::size_t{1});
    QCOMPARE(results[0].jobIndex, 0);
}

// A worker waiting on an empty queue is woken by closing it; the abort flag alone cannot reach it. The
// reader is parked before it has produced anything, so nothing else can end the worker: it has to be
// the cancel, and it has to be seen before the reader is released.
void TestPipelineRunner::cancelWakesAWorkerThatIsWaitingForPackets()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto rec = std::make_shared<Recorder>();
    rec->parkOn("Reading Chapter 10 file");   // the reader's first line: it reads nothing until released
    ReleaseOnExit guard{*rec};

    PipelineRunner runner;
    std::string error;
    QVERIFY2(runner.start({lockJob(fx)}, rec, error), error.c_str());
    QVERIFY2(rec->waitParked(kRunTimeoutMs), rec->joined().c_str());
    QThread::msleep(300);                     // the worker reaches its empty queue and blocks there

    runner.cancel();
    const std::string tag = "[CH " + std::to_string(fx.pcm_id) + "] ";
    const bool worker_ended = rec->waitForEvent(
        {"error:" + tag + "no frame sync found", "log:" + tag + "processing cancelled by user."}, 15000);
    rec->release();                           // only now may the reader go on
    QVERIFY2(worker_ended, ("the worker was left waiting for packets: " + rec->joined()).c_str());

    QVERIFY2(rec->waitFinished(kRunTimeoutMs), rec->joined().c_str());
    runner.wait();
    QVERIFY(!rec->success());
}

// The other half of cancelling a worker: its own abort flag. One parked just before its read loop
// would otherwise go on to drain whatever is already queued and finish as if nothing had happened.
void TestPipelineRunner::cancelStopsAWorkerThatHasNotStartedReading()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto rec = std::make_shared<Recorder>();
    rec->parkOn("processing...");             // the worker's line just before its loop
    ReleaseOnExit guard{*rec};

    PipelineRunner runner;
    std::string error;
    QVERIFY2(runner.start({lockJob(fx)}, rec, error), error.c_str());
    QVERIFY2(rec->waitParked(kRunTimeoutMs), rec->joined().c_str());

    runner.cancel();
    rec->release();
    QVERIFY2(rec->waitFinished(kRunTimeoutMs), rec->joined().c_str());
    runner.wait();

    QVERIFY2(rec->anyStartsWith("log:[CH " + std::to_string(fx.pcm_id) + "] processing cancelled by user."),
             rec->joined().c_str());
    QVERIFY(!rec->success());
    QVERIFY(rec->results().empty());
}

// `running()` goes false BEFORE the end is reported, so a handler that wants to start the next run is
// not turned away by the run that is still reporting it. A race to start again cannot pin that, so
// the sink looks at the runner from inside the report.
void TestPipelineRunner::theRunnerIsNotRunningByTheTimeItReportsTheEnd()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::atomic<bool> reported{false};
    std::atomic<bool> running_at_the_end{true};
    rec->onFinished([&]() {
        running_at_the_end = runner.running();
        reported = true;
    });

    std::string error;
    QVERIFY2(runner.start({lockJob(fx)}, rec, error), error.c_str());
    QVERIFY2(rec->waitFinished(kRunTimeoutMs), rec->joined().c_str());
    runner.wait();

    QVERIFY(reported.load());
    QVERIFY2(!running_at_the_end.load(), "the runner still said it was running while it reported the end");
}

void TestPipelineRunner::theRunnerRunsAgainAfterARunEnds()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    PipelineRunner runner;
    for (int round = 0; round < 2; ++round)
    {
        auto rec = std::make_shared<Recorder>();
        std::string error;
        QVERIFY2(runner.start({lockJob(fx, 20 + round)}, rec, error), error.c_str());
        QVERIFY2(rec->waitFinished(kRunTimeoutMs), rec->joined().c_str());
        QVERIFY2(rec->success(), rec->joined().c_str());
        QCOMPARE(rec->results().size(), std::size_t{1});
        QCOMPARE(rec->results()[0].sourceId, 20 + round);
        QCOMPARE(rec->finishCount(), 1);
        // The second start comes straight after the end is reported, without waiting for the thread
        // that reported it: the runner has to tolerate that.
    }
    runner.wait();
}

void TestPipelineRunner::destroyingARunningRunnerStopsItAndReportsTheEnd()
{
    Fixture fx;
    if (!loadFixture(fx))
        QSKIP("step_cal fixture not available");

    auto rec = std::make_shared<Recorder>();
    {
        PipelineRunner runner;
        std::string error;
        QVERIFY2(runner.start({lockJob(fx), lockJob(fx, 1)}, rec, error), error.c_str());
    }   // destroyed while the run is going: it cancels, then waits for every thread

    QCOMPARE(rec->finishCount(), 1);       // already reported: the destructor returned only after the end
    QVERIFY(!rec->success());
    QVERIFY(rec->results().empty());
}
