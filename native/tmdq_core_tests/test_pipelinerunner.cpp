// PipelineRunner: the single-reader / parallel-worker pipeline, with a plain recording sink and no event loop, on the
// committed step_cal recording. No Qt; ported from the Qt TestPipelineRunner, whose cases are all here under the same
// descriptions. The throughput guard (the Qt TestProcessingCoordinator's benchmarkHeavyWorkloadSingleVsMultiStream)
// lives here too: it measures the runner, not the coordinator around it, and it needs no Qt to do it.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "doctest.h"
#include "chapter10reader.h"
#include "frameprocessor.h"
#include "pipelinerunner.h"
#include "test_support.h"

using namespace tmdq_test;

/// The runner names this struct its friend so a test can end ONE stream's processor without cancelling the run, which
/// nothing in the runner's API can do.
struct PipelineRunnerTestAccess
{
    static void abortWorker(PipelineRunner& runner, std::size_t index) { runner.m_workers[index]->requestAbort(); }
};

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
        {
            m_on_finished(); // on the reporting thread, before the end is recorded
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_events.push_back(success ? "finished:true" : "finished:false");
            ++m_finish_count;
            m_success = success;
        }
        m_cv.notify_all();
    }

    /// Parks the first thread that logs a line containing @p needle, inside logMessage(), until release(). That
    /// freezes one worker (or the reader) at a chosen point in a real run, so a test can act at exactly that moment
    /// instead of racing for it.
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
            {
                for (const std::string& p : prefixes)
                {
                    if (e.starts_with(p))
                    {
                        return true;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    bool waitFinished(int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return m_finish_count > 0; });
    }

    std::vector<std::string>         events() const { std::lock_guard<std::mutex> lock(m_mutex); return m_events; }
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
        {
            if (e.starts_with(prefix))
            {
                return true;
            }
        }
        return false;
    }
    std::string joined() const
    {
        std::string out;
        for (const std::string& e : events())
        {
            out += e + "\n";
        }
        return out;
    }

private:
    void add(std::string e) { std::lock_guard<std::mutex> lock(m_mutex); m_events.push_back(std::move(e)); }

    void maybePark(const std::string& message)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_gate_needle.empty() || m_parked || message.find(m_gate_needle) == std::string::npos)
        {
            return;
        }
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

/// Frees a parked thread however the test ends, so a failed check cannot leave the runner's destructor waiting on a
/// thread that is waiting on the test. Declared AFTER the runner, so it is destroyed (and releases) before the runner
/// cancels and joins.
struct ReleaseOnExit
{
    Recorder& recorder;
    ~ReleaseOnExit() { recorder.release(); }
};

/// A small, committed, ordinary Chapter 10 recording with one PCM channel of interest.
struct Fixture
{
    fs::path path;
    int      time_id = -1;
    int      pcm_id  = -1;
};

Fixture loadFixture()
{
    Fixture f;
    f.path = repoFile(fs::path("tests") / "data" / "test files" / "step_cal, example.ch10");
    Chapter10Reader meta;
    REQUIRE_MESSAGE(meta.loadChannels(f.path), meta.lastError());
    f.time_id = meta.getCurrentTimeChannelID();
    f.pcm_id  = meta.getFirstPCMChannelID();
    REQUIRE(f.time_id >= 0);
    REQUIRE(f.pcm_id >= 0);
    return f;
}

/// A lock-only stream over the fixture. @p sync other than the recording's own finds nothing.
StreamJob lockJob(const Fixture& f, int source_id = 0, std::uint64_t sync = 0xFE6B2840)
{
    StreamJob job;
    job.params.filename          = f.path;
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

std::string tagOf(const Fixture& f) { return "[CH " + std::to_string(f.pcm_id) + "] "; }

} // namespace

TEST_CASE("PipelineRunner: a single stream delivers its result, then the end")
{
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    REQUIRE_MESSAGE(runner.start({lockJob(fx, 7)}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();

    CHECK_FALSE(runner.running());
    CHECK_MESSAGE(rec->success(), rec->joined());
    CHECK(rec->finishCount() == 1);

    const auto results = rec->results();
    REQUIRE(results.size() == 1);
    CHECK(results[0].jobIndex == 0);
    CHECK(results[0].sourceId == 7); // the job's own source id is carried through
    CHECK(results[0].hasSamples());
    CHECK_FALSE(rec->anyStartsWith("error:"));

    // The end is the last thing said; the stream's worker lines all come before its result.
    const auto events = rec->events();
    REQUIRE_FALSE(events.empty());
    CHECK(events.back() == "finished:true");
    const auto result_at = std::find(events.begin(), events.end(), "stream:0") - events.begin();
    const std::string tag = "log:" + tagOf(fx);
    int worker_lines = 0;
    for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(events.size()); ++i)
    {
        if (events[static_cast<std::size_t>(i)].starts_with(tag))
        {
            ++worker_lines;
            CHECK_MESSAGE(i < result_at, "a worker line after the result: " << events[static_cast<std::size_t>(i)]);
        }
    }
    CHECK_MESSAGE(worker_lines >= 3, rec->joined()); // started, totals, complete

    // The reader's progress passes through and reaches 100 once; the runner adds none of its own.
    CHECK(rec->anyStartsWith("progress:"));
    CHECK(rec->count("progress:100") == 1);
}

TEST_CASE("PipelineRunner: several streams on one channel each deliver their own result")
{
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    REQUIRE_MESSAGE(runner.start({lockJob(fx, 10), lockJob(fx, 11), lockJob(fx, 12)}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();

    CHECK_MESSAGE(rec->success(), rec->joined());
    const auto results = rec->results();
    REQUIRE(results.size() == 3);

    // Each stream's result says which job it is and which source that job named, whatever order the workers happened
    // to finish in.
    std::set<int> indices;
    for (const ProcessedStreamData& r : results)
    {
        indices.insert(r.jobIndex);
        CHECK(r.sourceId == 10 + r.jobIndex);
        CHECK(r.hasSamples());
        // Same data, same samples: every stream saw every packet, not a share of them (a queue shared between the
        // streams would hand each of them only the packets it won).
        CHECK(r.timesSec == results[0].timesSec);
        CHECK(r.lockPercent == results[0].lockPercent);
        CHECK(r.accumulatedMissedFrames == results[0].accumulatedMissedFrames);
    }
    CHECK(indices == std::set<int>{0, 1, 2});
    CHECK(rec->finishCount() == 1);
    CHECK(rec->events().back() == "finished:true");
}

TEST_CASE("PipelineRunner: a stream that finds no sync does not stop the others")
{
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    // Job 0 looks for a pattern the recording does not contain; job 1 is right.
    REQUIRE_MESSAGE(runner.start({lockJob(fx, 0, 0x12345678), lockJob(fx, 1)}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();

    // One stream earned a result, so the run succeeded; the other said why it did not.
    CHECK_MESSAGE(rec->success(), rec->joined());
    const auto results = rec->results();
    REQUIRE(results.size() == 1);
    CHECK(results[0].jobIndex == 1);
    CHECK_MESSAGE(rec->anyStartsWith("error:" + tagOf(fx) + "no frame sync found"), rec->joined());
}

TEST_CASE("PipelineRunner: a stream that fails after another delivered does not undo the run's success")
{
    // Which stream finishes first is up to the scheduler, so the order is made: the stream that looks for the wrong
    // pattern is parked on its report ("0 syncs found" is said by it alone) until the good stream has delivered. The
    // run succeeded because one stream earned a result; a later failure must not take that back.
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    rec->parkOn("processed, 0 syncs found");

    StreamJob nosync = lockJob(fx, 0, 0x12345678);
    nosync.params.streamLabel = "nosync"; // so its log lines are tagged apart from job 1's

    PipelineRunner runner;
    ReleaseOnExit guard{*rec};
    std::string error;
    REQUIRE_MESSAGE(runner.start({nosync, lockJob(fx, 1)}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitParked(kRunTimeoutMs), rec->joined());
    REQUIRE_MESSAGE(rec->waitForEvent({"stream:1"}, kRunTimeoutMs), rec->joined());

    rec->release(); // only now does the failing stream finish
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();

    CHECK_MESSAGE(rec->success(), rec->joined());
    CHECK(rec->results().size() == 1);
}

TEST_CASE("PipelineRunner: the first job names the recording")
{
    // Every job must name the same recording and time channel; the first job's are the ones used. A later job naming
    // something else is not consulted (and not an error), and a first job naming a recording that is not there
    // refuses the start whatever the rest say.
    const Fixture fx = loadFixture();
    TempDir dir;

    {
        StreamJob other = lockJob(fx, 1);
        other.params.filename = dir.file("no-such-recording.ch10");
        auto rec = std::make_shared<Recorder>();
        PipelineRunner runner;
        std::string error;
        REQUIRE_MESSAGE(runner.start({lockJob(fx, 0), other}, rec, error), error);
        REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
        runner.wait();
        CHECK_MESSAGE(rec->success(), rec->joined());
    }
    {
        StreamJob first = lockJob(fx, 0);
        first.params.filename = dir.file("no-such-recording.ch10");
        auto rec = std::make_shared<Recorder>();
        PipelineRunner runner;
        std::string error;
        CHECK_FALSE(runner.start({first, lockJob(fx, 1)}, rec, error));
        CHECK_FALSE(error.empty());
        CHECK_FALSE(runner.running());
    }
}

TEST_CASE("PipelineRunner: a run in which every stream fails ends unsuccessfully")
{
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    REQUIRE_MESSAGE(runner.start({lockJob(fx, 0, 0x12345678)}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();

    CHECK_FALSE(rec->success());
    CHECK(rec->finishCount() == 1);
    CHECK(rec->results().empty());
    CHECK(rec->anyStartsWith("error:"));
    CHECK(rec->events().back() == "finished:false");
}

TEST_CASE("PipelineRunner: a recording that cannot be opened fails before anything runs")
{
    const Fixture fx = loadFixture();

    TempDir dir;
    StreamJob missing = lockJob(fx);
    missing.params.filename = dir.file("no-such-recording.ch10");

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    CHECK_FALSE(runner.start({missing}, rec, error));
    CHECK_MESSAGE(!error.empty(), "a failed start must say why");
    CHECK_FALSE(runner.running());
    CHECK_MESSAGE(rec->events().empty(), "a start that fails reports nothing to the sink");

    // And the runner is not left stuck: a good run starts straight afterwards.
    error.clear();
    REQUIRE_MESSAGE(runner.start({lockJob(fx)}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();
    CHECK(rec->success());
}

TEST_CASE("PipelineRunner: no jobs is refused")
{
    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    CHECK_FALSE(runner.start({}, rec, error));
    CHECK(error == "No streams were given to process.");
    CHECK_FALSE(runner.running());
    CHECK(rec->events().empty());

    // Refused, not stuck: the runner is free for a real run straight afterwards.
    const Fixture fx = loadFixture();
    error.clear();
    REQUIRE_MESSAGE(runner.start({lockJob(fx)}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();
    CHECK(rec->success());
}

TEST_CASE("PipelineRunner: a second start while running is refused")
{
    const Fixture fx = loadFixture();

    auto first = std::make_shared<Recorder>();
    auto second = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    REQUIRE_MESSAGE(runner.start({lockJob(fx)}, first, error), error);

    error.clear();
    CHECK_FALSE(runner.start({lockJob(fx)}, second, error));
    CHECK(error == "A processing run is already in progress.");

    // The refusal did not disturb the run in flight, and the second sink heard nothing.
    REQUIRE_MESSAGE(first->waitFinished(kRunTimeoutMs), first->joined());
    runner.wait();
    CHECK(first->success());
    CHECK(first->finishCount() == 1);
    CHECK(second->events().empty());
}

TEST_CASE("PipelineRunner: cancel ends the run unsuccessfully with no result")
{
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;
    REQUIRE_MESSAGE(runner.start({lockJob(fx), lockJob(fx, 1)}, rec, error), error);
    runner.cancel(); // before any worker can have finished the recording
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), "a cancelled run never reported its end");
    runner.wait();

    CHECK_FALSE(rec->success());
    CHECK(rec->finishCount() == 1);
    CHECK_MESSAGE(rec->results().empty(), "a cancelled run must not deliver a result");
    CHECK(rec->events().back() == "finished:false");
    CHECK_FALSE(runner.running());

    runner.cancel(); // nothing in flight: a no-op, not a crash
}

TEST_CASE("PipelineRunner: a cancel after one stream delivered drops the rest and fails the run")
{
    // A stream can finish in the instant a cancel lands. Cancelling aborts the workers, so most of the time the result
    // is dropped simply because the stream did not finish - which hides the rule under test: a stream that DID finish
    // after the cancel was asked for still delivers nothing, and a run that was cancelled fails even though another
    // stream had already delivered. Freeing that moment: job 1's worker is parked on its last log line ("complete"),
    // after its work and before it reports, while job 0 runs through and delivers.
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    rec->parkOn("second] complete");

    StreamJob second = lockJob(fx, 1);
    second.params.streamLabel = "second"; // so its log lines are tagged apart from job 0's

    PipelineRunner runner;
    ReleaseOnExit guard{*rec};
    std::string error;
    REQUIRE_MESSAGE(runner.start({lockJob(fx, 0), second}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitParked(kRunTimeoutMs), rec->joined());
    REQUIRE_MESSAGE(rec->waitForEvent({"stream:0"}, kRunTimeoutMs), rec->joined());

    runner.cancel(); // job 1 has finished its work but not reported it
    rec->release();
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();

    CHECK_MESSAGE(!rec->success(), "a cancelled run must fail even though one stream had delivered");
    const auto results = rec->results();
    REQUIRE(results.size() == 1);
    CHECK(results[0].jobIndex == 0);
}

TEST_CASE("PipelineRunner: a cancel wakes a worker that is waiting for packets")
{
    // A worker waiting on an empty queue is woken by closing it; the abort flag alone cannot reach it. The reader is
    // parked before it has produced anything, so nothing else can end the worker: it has to be the cancel, and it has
    // to be seen before the reader is released.
    const Fixture fx = loadFixture();

    // Two streams, so that closing only some of the queues would leave one worker waiting.
    auto rec = std::make_shared<Recorder>();
    rec->parkOn("Reading Chapter 10 file"); // the reader's first line: it reads nothing until released

    StreamJob first = lockJob(fx, 0);
    first.params.streamLabel = "first";
    StreamJob second = lockJob(fx, 1);
    second.params.streamLabel = "second";

    PipelineRunner runner;
    ReleaseOnExit guard{*rec};
    std::string error;
    REQUIRE_MESSAGE(runner.start({first, second}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitParked(kRunTimeoutMs), rec->joined());
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // the workers reach their empty queues and block there

    runner.cancel();
    const std::string base = "[CH " + std::to_string(fx.pcm_id) + " ";
    bool workers_ended = true;
    for (const char* label : {"first", "second"})
    {
        const std::string tag = base + label + "] ";
        workers_ended = rec->waitForEvent(
            {"error:" + tag + "no frame sync found", "log:" + tag + "processing cancelled by user."}, 15000)
            && workers_ended;
    }
    rec->release(); // only now may the reader go on
    REQUIRE_MESSAGE(workers_ended, "a worker was left waiting for packets: " << rec->joined());

    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();
    CHECK_FALSE(rec->success());
    // The cancel reached the reader too: it stopped at its first look rather than reading the recording out.
    CHECK_MESSAGE(rec->anyStartsWith("log:Reading cancelled by user."), rec->joined());
}

TEST_CASE("PipelineRunner: a cancel stops a worker that has not started reading")
{
    // The other half of cancelling a worker: its own abort flag. One parked just before its read loop would otherwise
    // go on to drain whatever is already queued and finish as if nothing had happened.
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    rec->parkOn("processing..."); // the worker's line just before its loop

    PipelineRunner runner;
    ReleaseOnExit guard{*rec};
    std::string error;
    REQUIRE_MESSAGE(runner.start({lockJob(fx)}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitParked(kRunTimeoutMs), rec->joined());

    runner.cancel();
    rec->release();
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();

    CHECK_MESSAGE(rec->anyStartsWith("log:" + tagOf(fx) + "processing cancelled by user."), rec->joined());
    CHECK_FALSE(rec->success());
    CHECK(rec->results().empty());
}

TEST_CASE("PipelineRunner: the runner is not running by the time it reports the end")
{
    // `running()` goes false BEFORE the end is reported, so a handler that wants to start the next run is not turned
    // away by the run that is still reporting it. A race to start again cannot pin that, so the sink looks at the
    // runner from inside the report.
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::atomic<bool> reported{false};
    std::atomic<bool> running_at_the_end{true};
    rec->onFinished([&]() {
        running_at_the_end = runner.running();
        reported = true;
    });

    std::string error;
    REQUIRE_MESSAGE(runner.start({lockJob(fx)}, rec, error), error);
    REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
    runner.wait();

    CHECK(reported.load());
    CHECK_MESSAGE(!running_at_the_end.load(), "the runner still said it was running while it reported the end");
}

TEST_CASE("PipelineRunner: a stream that ends early does not leave the reader blocked on its queue")
{
    // The one way a stream can end before the recording does without anyone cancelling the run is that its own
    // processor stops early. Nothing in the runner's API can cause that, so this reaches in (PipelineRunnerTestAccess)
    // and aborts ONE stream's processor. That stream stops reading, its queue fills to its depth, and nobody will ever
    // drain it - so the reader, which hands every packet to every stream, has to be let go by the stream's thread
    // closing its own queue. Without that the reader blocks forever on the dead stream's full queue and the OTHER
    // stream, which is perfectly healthy, never finishes.
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    StreamJob doomed = lockJob(fx, 1);
    doomed.params.streamLabel = "doomed"; // so its log lines are tagged apart from job 0's

    PipelineRunner runner;
    std::string error;
    REQUIRE_MESSAGE(runner.start({lockJob(fx, 0), doomed}, rec, error), error);
    PipelineRunnerTestAccess::abortWorker(runner, 1); // job 1 ends early; the run itself is NOT cancelled

    const bool ended = rec->waitFinished(30000);
    REQUIRE_MESSAGE(ended, "the run never ended - the reader is stuck on the dead stream's queue: " << rec->joined());
    runner.wait();

    // The healthy stream finished and was delivered; the run succeeded; the doomed one said why it stopped.
    CHECK_MESSAGE(rec->success(), rec->joined());
    const auto results = rec->results();
    REQUIRE(results.size() == 1);
    CHECK(results[0].jobIndex == 0);
    CHECK_MESSAGE(rec->anyStartsWith("log:[CH " + std::to_string(fx.pcm_id) + " doomed] processing cancelled by user."),
                  rec->joined());
}

TEST_CASE("PipelineRunner: the runner runs again after a run ends")
{
    const Fixture fx = loadFixture();

    PipelineRunner runner;
    for (int round = 0; round < 2; ++round)
    {
        INFO("round " << round);
        auto rec = std::make_shared<Recorder>();
        std::string error;
        REQUIRE_MESSAGE(runner.start({lockJob(fx, 20 + round)}, rec, error), error);
        REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
        CHECK_MESSAGE(rec->success(), rec->joined());
        REQUIRE(rec->results().size() == 1);
        CHECK(rec->results()[0].sourceId == 20 + round);
        CHECK(rec->finishCount() == 1);
        // The second start comes straight after the end is reported, without waiting for the thread that reported it:
        // the runner has to tolerate that.
    }
    runner.wait();
}

TEST_CASE("PipelineRunner: nothing of one run is left over in the next")
{
    // A success, a failure and a cancel in turn: a run starts from a clean slate, so a stream that delivered in one run
    // does not make the next run (in which none does) succeed, and a cancel does not make the run after it fail.
    const Fixture fx = loadFixture();
    PipelineRunner runner;

    auto runOnce = [&](StreamJob job, bool cancel_at_once) {
        auto rec = std::make_shared<Recorder>();
        std::string error;
        REQUIRE_MESSAGE(runner.start({std::move(job)}, rec, error), error);
        if (cancel_at_once)
        {
            runner.cancel();
        }
        REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
        runner.wait();
        return rec;
    };

    CHECK(runOnce(lockJob(fx), false)->success());
    const auto failed = runOnce(lockJob(fx, 0, 0x12345678), false);
    CHECK_FALSE(failed->success());
    CHECK(failed->results().empty());
    CHECK_FALSE(runOnce(lockJob(fx), true)->success());
    const auto after_cancel = runOnce(lockJob(fx), false);
    CHECK_MESSAGE(after_cancel->success(), after_cancel->joined());
    CHECK(after_cancel->results().size() == 1);
}

TEST_CASE("PipelineRunner: a run lets go of its frame setups, however it ends")
{
    // The runner keeps each job's frame setup alive only as long as its stream can run: after the end is reported, and
    // after a start that failed, nothing of the jobs is held.
    const Fixture fx = loadFixture();

    auto released = [](StreamJob job, std::weak_ptr<FrameSetup>& watch) {
        watch = job.frameSetup;
        return job;
    };

    // A run that ends normally.
    {
        std::weak_ptr<FrameSetup> watch;
        auto rec = std::make_shared<Recorder>();
        PipelineRunner runner;
        std::string error;
        REQUIRE_MESSAGE(runner.start({released(lockJob(fx), watch)}, rec, error), error);
        REQUIRE_MESSAGE(rec->waitFinished(kRunTimeoutMs), rec->joined());
        runner.wait();
        CHECK_MESSAGE(watch.expired(), "the finished run still holds its frame setup");
    }
    // A start that fails before anything runs.
    {
        TempDir dir;
        std::weak_ptr<FrameSetup> watch;
        StreamJob missing = lockJob(fx);
        missing.params.filename = dir.file("no-such-recording.ch10");
        auto rec = std::make_shared<Recorder>();
        PipelineRunner runner;
        std::string error;
        CHECK_FALSE(runner.start({released(std::move(missing), watch)}, rec, error));
        CHECK_MESSAGE(watch.expired(), "the refused start still holds its frame setup");
    }
}

TEST_CASE("PipelineRunner: destroying a running runner stops it and reports the end")
{
    const Fixture fx = loadFixture();

    auto rec = std::make_shared<Recorder>();
    {
        PipelineRunner runner;
        std::string error;
        REQUIRE_MESSAGE(runner.start({lockJob(fx), lockJob(fx, 1)}, rec, error), error);
    } // destroyed while the run is going: it cancels, then waits for every thread

    CHECK(rec->finishCount() == 1); // already reported: the destructor returned only after the end
    CHECK_FALSE(rec->success());
    CHECK(rec->results().empty());
}

// ---------------------------------------------------------------------------------------------------------------------
// The throughput guard
// ---------------------------------------------------------------------------------------------------------------------
//
// The decommutation pipeline - one reader, N parallel workers, a bit-serial frame-sync scanner over every bit of the
// recording - has a performance guard only because this case exists. It was the Qt TestProcessingCoordinator's
// benchmarkHeavyWorkloadSingleVsMultiStream, and was inert three ways: it asserted nothing, nothing set the variable
// that enabled it, and it read a recording that could not satisfy it. See scripts\run_throughput_bench.ps1.
//
// It walks a ~640 MB recording nine times (four channel probes, four solo runs, one parallel run) and takes minutes,
// so it is opt-in: set TMDQA_RUN_HEAVY_BENCH=1 (the release script does) and it runs; unset, it is skipped and listed
// as skipped. Once asked for it can no longer skip quietly: a recording that cannot satisfy it is a FAILURE, because a
// guard that passes by not running is the failure this exists to close.
//
// The guard is on RATIOS, never on milliseconds. Absolute times belong to the machine - the heaviest channel in the
// recording runs ~39 s on the development box - while the ratios belong to the ARCHITECTURE, and measured stable to
// within 0.01 across four separate runs.
//
// Measured baseline (release build, `prn_testfile, full.ch10`, four streams):
//   parallel / slowest-solo     1.00 - 1.01
//   parallel / all-sequential   0.64 - 0.65
//
// The single-reader design's entire claim is that N streams cost about what the slowest ONE costs, because the file is
// read once and the workers overlap it. The regression this catches is worker back-pressure serializing the reader,
// which would drive the first ratio toward 4.0 - the sum of the solos over the slowest. Both limits carry deliberate
// headroom over the measurement, because a real serialization regression is a multiple rather than a few percent, and a
// merely busy machine must not fail a release.

namespace {

constexpr double kMaxParallelVsSlowestSolo = 1.50;
constexpr double kMaxParallelVsSequential  = 0.85;
/// Four concurrent streams plus a reader thread. Below this the machine cannot overlap them at all, so the ratios would
/// measure the core count rather than the architecture - reported, but not asserted.
constexpr unsigned kMinCoresForThroughputGuard = 4;

/// 5 min per run. The heaviest channel is PRN15 at 20 Mbps over a ~640 MB recording and the scanner is bit-serial, so
/// one pass measures ~39 s on the dev box.
constexpr int kBenchTimeoutMs = 300000;

/// runAndTime() sentinels, distinguished so a timeout is not reported as a functional failure.
constexpr long long kRunFailed   = -1;
constexpr long long kRunTimedOut = -2;

bool heavyBenchRequested()
{
    char value[8] = {};
    return GetEnvironmentVariableA("TMDQA_RUN_HEAVY_BENCH", value, sizeof value) != 0;
}

/// What the benchmark prints, and the release script reads: one `[benchmark-heavy]` line per measurement.
void report(const std::string& line)
{
    std::fputs(("[benchmark-heavy] " + line + "\n").c_str(), stdout);
    std::fflush(stdout);
}

struct PrnSpec
{
    std::uint64_t frameSync;
    int           bitsInMinorFrame;
    int           wordsInMinorFrame; // ceil(bits / 16)
};

// Frame parameters read from settings/framesync_patterns/framesync_PRN11.toml and framesync_PRN15.toml.
constexpr PrnSpec kPrn11 = {0xA345CA5C, 2047, 128};
constexpr PrnSpec kPrn15 = {0x334AABBF, 32767, 2048};

/// A lock-only job for a PRN stream on the given channel.
StreamJob makePrnJob(const fs::path& file, int time_id, int pcm_id, const PrnSpec& spec, const std::string& label)
{
    StreamJob job;
    job.params.filename          = file;
    job.params.timeChannelId     = time_id;
    job.params.pcmChannelId      = pcm_id;
    job.params.frameSync         = spec.frameSync;
    // Ordinary Chapter 10 recording, so it needs the byte swap. Stated rather than inherited, so a change to the DTO
    // default cannot silently retune it.
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
    job.frameSetup               = std::make_shared<FrameSetup>();
    return job;
}

/// @return elapsed ms on success, kRunFailed if the run could not start or ended in error, kRunTimedOut if it was
///         still going when the timeout expired (then cancelled and joined, so the runner is safe to destroy).
long long runAndTime(std::vector<StreamJob> jobs, int timeout_ms)
{
    auto rec = std::make_shared<Recorder>();
    PipelineRunner runner;
    std::string error;

    const auto began = std::chrono::steady_clock::now();
    if (!runner.start(std::move(jobs), rec, error))
    {
        return kRunFailed;
    }
    if (!rec->waitFinished(timeout_ms))
    {
        runner.cancel();
        runner.wait();
        return kRunTimedOut;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began);
    runner.wait();
    return rec->success() ? elapsed.count() : kRunFailed;
}

} // namespace

TEST_CASE("PipelineRunner throughput: four streams on one recording cost about what the slowest costs"
          * doctest::skip(!heavyBenchRequested()))
{
    // WHICH RECORDING, AND WHY IT MATTERS
    //
    // The committed `prn_testfile, sample.ch10` is a 77 MB excerpt that carries PCM packets for channels 13 and 15
    // ONLY - 14 and 16 are described in its TMATS but hold no data at all - so it cannot produce a number. The full
    // ~640 MB recording sits beside it on the development box; the self-hosted runner copies it OVER the sample path
    // (see .github/workflows/ci.yml), so on that machine the sample path IS the full recording. Checking for the full
    // file by name must not defeat that, so prefer the full file where it exists and fall back to the sample path.
    const fs::path root        = repoRoot();
    const fs::path full_path   = root / "tests" / "data" / "test files" / "prn_testfile, full.ch10";
    const fs::path sample_path = root / "tests" / "data" / "test files" / "prn_testfile, sample.ch10";
    const fs::path file        = fs::exists(full_path) ? full_path : sample_path;
    REQUIRE_MESSAGE(fs::exists(file), "no PRN recording found beside the tests: " << file.string());

    Chapter10Reader reader;
    REQUIRE_MESSAGE(reader.loadChannels(file), reader.lastError());
    const int time_id = reader.getCurrentTimeChannelID();
    REQUIRE_MESSAGE(time_id >= 0, "no time channel in the PRN recording");
    const auto pcm_channels = reader.getPCMChannelList();
    REQUIRE_MESSAGE(pcm_channels.size() >= 4, "the PRN recording does not have 4 PCM channels");

    // Assign channels by trying each against both PRN specs. This handles any channel ordering the recording may use
    // and is self-documenting in the log.
    auto probeChannel = [&](int ch_id, const PrnSpec& spec) -> bool {
        const long long t = runAndTime({makePrnJob(file, time_id, ch_id, spec, "probe")}, kBenchTimeoutMs);
        REQUIRE_MESSAGE(t != kRunTimedOut, "the channel probe timed out - the machine is too loaded to time anything");
        return t >= 0;
    };

    std::vector<int> prn11_ids;
    std::vector<int> prn15_ids;
    for (const auto& [ch_id, label] : pcm_channels)
    {
        const bool is11 = probeChannel(ch_id, kPrn11);
        const bool is15 = !is11 && probeChannel(ch_id, kPrn15);
        report(std::format("channel {} ({}) -> {}", ch_id, label, is11 ? "PRN11" : is15 ? "PRN15" : "unrecognized"));
        if (is11)
        {
            prn11_ids.push_back(ch_id);
        }
        else if (is15)
        {
            prn15_ids.push_back(ch_id);
        }
    }

    // Name the usual cause rather than only the symptom: it is almost always the recording, not the decode. Against
    // the committed sample this is the EXPECTED outcome.
    REQUIRE_MESSAGE((prn11_ids.size() >= 2 && prn15_ids.size() >= 2),
                    "need 2 PRN11 and 2 PRN15 channels in " << file.filename().string() << ", found " << prn11_ids.size()
                    << " and " << prn15_ids.size() << ". The committed 'prn_testfile, sample.ch10' holds PCM data for 2 "
                    "of the 4 channels this times, so it cannot produce a throughput number; the full ~640 MB recording "
                    "is needed, either beside it as 'prn_testfile, full.ch10' or overlaid onto the sample path via "
                    "TMDQA_CH10_FULL.");

    // Time every discovered channel individually so we know the true single-stream cost for each, including the
    // heavier PRN15 20M channel.
    std::vector<long long> solo_times;
    for (const int id : prn11_ids)
    {
        const long long t = runAndTime({makePrnJob(file, time_id, id, kPrn11, std::format("PRN11-solo-{}", id))},
                                       kBenchTimeoutMs);
        REQUIRE_MESSAGE(t != kRunTimedOut, "the heavy benchmark timed out - the machine is too loaded to time anything");
        REQUIRE_MESSAGE(t >= 0, "PRN11 channel " << id << " solo run failed");
        solo_times.push_back(t);
        report(std::format("PRN11 ch{} solo: {} ms", id, t));
    }
    for (const int id : prn15_ids)
    {
        const long long t = runAndTime({makePrnJob(file, time_id, id, kPrn15, std::format("PRN15-solo-{}", id))},
                                       kBenchTimeoutMs);
        REQUIRE_MESSAGE(t != kRunTimedOut, "the heavy benchmark timed out - the machine is too loaded to time anything");
        REQUIRE_MESSAGE(t >= 0, "PRN15 channel " << id << " solo run failed");
        solo_times.push_back(t);
        report(std::format("PRN15 ch{} solo: {} ms", id, t));
    }

    std::vector<StreamJob> all_jobs;
    for (const int id : prn11_ids)
    {
        all_jobs.push_back(makePrnJob(file, time_id, id, kPrn11, std::format("PRN11-{}", id)));
    }
    for (const int id : prn15_ids)
    {
        all_jobs.push_back(makePrnJob(file, time_id, id, kPrn15, std::format("PRN15-{}", id)));
    }
    const long long parallel_ms = runAndTime(std::move(all_jobs), kBenchTimeoutMs);
    REQUIRE_MESSAGE(parallel_ms != kRunTimedOut, "the heavy benchmark timed out - the machine is too loaded to time anything");
    REQUIRE_MESSAGE(parallel_ms >= 0, "the parallel run should complete successfully");

    // Ideal parallel time = slowest individual stream (reader does one pass, workers run together). A ratio near 1.0x
    // means the architecture is working correctly. A large ratio points to worker back-pressure serializing the reader.
    const long long ideal_ms = *std::max_element(solo_times.begin(), solo_times.end());
    const long long sequential_all_ms = std::accumulate(solo_times.begin(), solo_times.end(), 0LL);
    const double ratio_vs_ideal = ideal_ms > 0 ? double(parallel_ms) / double(ideal_ms) : 0.0;
    const double ratio_vs_seq   = sequential_all_ms > 0 ? double(parallel_ms) / double(sequential_all_ms) : 0.0;
    const unsigned cores = std::thread::hardware_concurrency();

    report(std::format("4-stream parallel: {} ms | vs slowest solo ({} ms): {:.2f}x | vs all-sequential ({} ms): "
                       "{:.2f}x | {} cores",
                       parallel_ms, ideal_ms, ratio_vs_ideal, sequential_all_ms, ratio_vs_seq, cores));

    // Everything above this line is reporting, and until the assertions below existed that was ALL this benchmark did:
    // it printed its numbers and asserted nothing, so the decommutation pipeline had no throughput guard and a
    // regression would have scrolled past in a log nobody reads. Reporting is not guarding.
    if (cores < kMinCoresForThroughputGuard)
    {
        report(std::format("NOT GUARDED: {} cores, below the {} these four concurrent streams need - the ratios above "
                           "describe this machine, not the architecture.",
                           cores, kMinCoresForThroughputGuard));
        return;
    }

    CHECK_MESSAGE(ratio_vs_ideal <= kMaxParallelVsSlowestSolo,
                  "four concurrent streams cost " << std::format("{:.2f}", ratio_vs_ideal)
                  << "x the slowest one alone (limit " << std::format("{:.2f}", kMaxParallelVsSlowestSolo)
                  << "x) - the reader is being serialized by worker back-pressure instead of reading the file once");
    CHECK_MESSAGE(ratio_vs_seq <= kMaxParallelVsSequential,
                  "four concurrent streams cost " << std::format("{:.2f}", ratio_vs_seq)
                  << "x what running them one at a time costs (limit " << std::format("{:.2f}", kMaxParallelVsSequential)
                  << "x) - parallel processing has stopped buying anything");
}
