// Runs through the C ABI (src/core/abi/abi_run.cpp): a run is the run the core makes, what it reports and in what order,
// cancel, free and wait (and from its own callback), every setting of a stream reaching the job, bad arguments.

#include "test_abi_support.h"

// ---------------------------------------------------------------------------
// Runs
// ---------------------------------------------------------------------------

TEST_CASE("Abi: a run of several streams matches what the core makes")
{
    const StepCal f = loadStepCal();

    // Two streams over the one channel, one in each mode, with the source id the host chose.
    const std::vector<Case> cases = { lockCase(f), receiverCase(f) };
    bool coreOk = false;
    const auto want = runCore(f, 7, cases, rootUtf8(), coreOk);
    REQUIRE(coreOk);
    REQUIRE(want.size() == std::size_t{2});
    REQUIRE(want.at(0).hasSamples());
    REQUIRE(want.at(1).hasSamples());
    REQUIRE(!want.at(1).channels.empty());   // the receiver stream really has series to compare

    Request request(f, cases, 7);
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    REQUIRE(run != nullptr);
    RunGuard guard{run};
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_run_wait(run);

    CHECK(seen.finishedSuccess == 1);
    const auto got = seen.byJob();
    REQUIRE(got.size() == std::size_t{2});
    for (int job = 0; job < 2; ++job)
    {
        const std::string diff = differences(got.at(job), want.at(job));
        REQUIRE_MESSAGE(diff.empty(), ("job " + std::to_string(job) + " differs in: " + diff));
    }
    // The source id and the labels came through the request.
    tmdq_stream_info info{};
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_stream_result_info(got.at(0), &info) == int(TMDQ_OK));
    CHECK(info.source_id == 7);
    CHECK(std::string(info.label_utf8) == std::string("lock stream"));
    REQUIRE((info.lock_percent != nullptr && info.missed_frames != nullptr));   // a lock stream has both series
    REQUIRE(tmdq_stream_result_info(got.at(1), &info) == int(TMDQ_OK));
    CHECK(std::string(info.label_utf8) == std::string("receiver stream"));
    CHECK(info.mode == int(TMDQ_MODE_RECEIVER_CHANNEL_INFO));
    // A receiver stream has neither: the header promises NULL, not whatever an empty vector's data() is.
    REQUIRE(info.lock_percent == nullptr);
    REQUIRE(info.missed_frames == nullptr);
}

TEST_CASE("Abi: a run reports in the documented order")
{
    const StepCal f = loadStepCal();

    Request request(f, { lockCase(f), receiverCase(f) });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    const std::thread::id caller = std::this_thread::get_id();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    RunGuard guard{run};
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_run_wait(run);
    CHECK(tmdq_run_running(run) == 0);

    const std::vector<std::string> events = seen.snapshot();
    REQUIRE(!events.empty());
    CHECK(events.back() == std::string("finished:1"));           // LAST
    REQUIRE(seen.count("finished:1") == 1);                         // and once
    REQUIRE(seen.finishedCount == 1);
    REQUIRE(!seen.any("error:"));

    // Both streams are delivered, each once, before the end, and not from the thread that started the run.
    REQUIRE(seen.count("stream:0") == 1);
    REQUIRE(seen.count("stream:1") == 1);
    REQUIRE(seen.streamThreads.at(0) != caller);
    REQUIRE(seen.streamThreads.at(1) != caller);
    REQUIRE(seen.streamThreads.at(0) != seen.streamThreads.at(1));   // each on its own stream's thread
    REQUIRE(seen.finishedThread != caller);

    // Progress comes from one thread only (the reader's), reaches 100 once, and never goes backwards.
    REQUIRE(seen.progressThreads.size() == std::size_t{1});
    REQUIRE(*seen.progressThreads.begin() != caller);
    REQUIRE(seen.count("progress:100") == 1);
    int last = -1;
    for (const std::string& e : events)
    {
        if (e.rfind("progress:", 0) != 0) continue;
        const int p = std::stoi(e.substr(9));
        REQUIRE(p >= last);
        last = p;
    }
    CHECK(last == 100);

    // The lines the job builder produced reach `log` first, on the calling thread (before start returned),
    // and the workers' lines follow.
    REQUIRE_MESSAGE(seen.any("log:"), events.front());
}

TEST_CASE("Abi: waiting on a run from its own finished callback is an error not an exception")
{
    // `finished` is delivered on the thread tmdq_run_wait would join, so the call can never succeed; the header forbids
    // it. A host that does it anyway is told through the last error: an exception out of an exported function would
    // cross the C boundary, which is undefined into managed code.
    const StepCal f = loadStepCal();
    Request request(f, { lockCase(f) });
    Seen seen;
    seen.waitFromFinished = true;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    RunGuard guard{run};
    seen.waitOn = run;
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_run_wait(run);

    CHECK(!seen.waitThrew);
    CHECK_MESSAGE(seen.waitError == "tmdq_run_wait was called from one of the run's own callbacks and did not wait", seen.waitError);
    CHECK(seen.finishedSuccess == 1);   // and the run itself was not disturbed
}

TEST_CASE("Abi: freeing a run from its own finished callback is refused and the run is still there")
{
    // The free joins the run's threads from a destructor; from `finished` that is a join of the caller, which ends the
    // process. The header forbids the call; a host that makes it is told through the last error, and the run is left
    // whole: it still answers, and the free from outside (the guard's) is the one that releases it.
    const StepCal f = loadStepCal();
    Request request(f, { lockCase(f) });
    Seen seen;
    seen.freeFromFinished = true;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    RunGuard guard{run};
    seen.waitOn = run;
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_run_wait(run);

    CHECK(!seen.waitThrew);
    CHECK_MESSAGE(seen.waitError == "tmdq_run_free was called from one of the run's own callbacks: the run was not released", seen.waitError);
    CHECK(seen.finishedSuccess == 1);
    CHECK(seen.finishedCount == 1);
    CHECK(tmdq_run_running(run) == 0);
}

TEST_CASE("Abi: only a run's own callback is refused and only while it is running")
{
    const StepCal f = loadStepCal();
    const std::thread::id caller = std::this_thread::get_id();

    // A callback that has returned leaves nothing behind on its thread: start delivers the job builder's lines to `log`
    // on the calling thread, and that same thread may then wait, which really waits (the run has ended when it returns).
    // (a calibration profile gives the job builder something to say).
    Case profiled = receiverCase(f);
    profiled.calibration[1] = { {0.0, -10.0}, {1000.0, 0.0}, {30000.0, 40.0}, {65535.0, 60.0} };
    Request request(f, { profiled });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* other = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &other) == int(TMDQ_OK));
    RunGuard otherGuard{other};
    {
        std::lock_guard<std::mutex> l(seen.mutex);
        REQUIRE_MESSAGE(std::find(seen.logThreads.begin(), seen.logThreads.end(), caller) != seen.logThreads.end(),
                        "the fixture should log on the calling thread during start");
    }
    tmdq_run_wait(other);
    CHECK(seen.finishedCount == 1);
    CHECK(tmdq_run_running(other) == 0);

    // And a callback of one run is not a callback of another: from its `finished`, a second run frees the first (which
    // has ended), and that free is carried out, saying nothing.
    Request request2(f, { lockCase(f) });
    Seen seen2;
    seen2.freeFromFinished = true;
    const tmdq_callbacks callbacks2 = seen2.callbacks();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request2.req, &callbacks2, &run) == int(TMDQ_OK));
    RunGuard guard{run};
    otherGuard.run = nullptr;             // freed by the second run's callback instead
    seen2.waitOn = other;
    REQUIRE_MESSAGE(seen2.waitFinished(kTimeoutMs), seen2.joined());
    tmdq_run_wait(run);
    CHECK(!seen2.waitThrew);
    CHECK_MESSAGE(seen2.waitError.empty(), seen2.waitError);
}

TEST_CASE("Abi: a non-linear profile reaches the job")
{
    const StepCal f = loadStepCal();

    // The receiver stream with a profile on one word, and without. The default word map reads words 1..3;
    // a profile on word 1 changes that word's calibrated values and nothing else.
    Case plain = receiverCase(f);
    Case profiled = receiverCase(f);
    profiled.calibration[1] = { {0.0, -10.0}, {1000.0, 0.0}, {30000.0, 40.0}, {65535.0, 60.0} };

    bool coreOk = false;
    const auto want = runCore(f, 0, { plain, profiled }, rootUtf8(), coreOk);
    REQUIRE(coreOk);
    const ProcessedStreamData& a = want.at(0);
    const ProcessedStreamData& b = want.at(1);
    REQUIRE(a.channels.size() == b.channels.size());
    bool someDiffer = false, someSame = false;
    for (std::size_t i = 0; i < a.channels.size(); ++i)
        (a.channels[i].values == b.channels[i].values ? someSame : someDiffer) = true;
    REQUIRE_MESSAGE((someDiffer && someSame), "the fixture should be changed by the profile on one word and not on the others");

    Request request(f, { plain, profiled });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    RunGuard guard{run};
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_run_wait(run);
    const auto got = seen.byJob();
    REQUIRE(got.size() == std::size_t{2});
    REQUIRE_MESSAGE(differences(got.at(0), a).empty(), differences(got.at(0), a));
    REQUIRE_MESSAGE(differences(got.at(1), b).empty(), differences(got.at(1), b));

    // An invalid profile is ignored: the word is calibrated linearly, as without one.
    Request ignoredRequest(f, { profiled });
    tmdq_cal_profile ignored = ignoredRequest.configs[0].calibration[0];
    ignored.valid = 0;
    ignoredRequest.configs[0].calibration = &ignored;
    Seen seen2;
    const tmdq_callbacks callbacks2 = seen2.callbacks();
    tmdq_run* run2 = nullptr;
    REQUIRE(tmdq_run_start(&ignoredRequest.req, &callbacks2, &run2) == int(TMDQ_OK));
    RunGuard guard2{run2};
    REQUIRE_MESSAGE(seen2.waitFinished(kTimeoutMs), seen2.joined());
    tmdq_run_wait(run2);
    REQUIRE_MESSAGE(differences(seen2.byJob().at(0), a).empty(), "an invalid profile must leave the word linear");
}

TEST_CASE("Abi: settings the core rejects are refused with the core's message")
{
    const StepCal f = loadStepCal();

    Case bad = lockCase(f);
    bad.pattern = "not hex";

    // What the core says about it, directly.
    ProcessingParams base;
    base.filename = textfile::pathFromUtf8(f.path);
    base.timeChannelId = f.timeId;
    StreamJobContext ctx;
    ctx.appRoot = rootUtf8();
    StreamJob job;
    StreamJobReport report;
    REQUIRE(!buildStreamJob(bad.core(), base, ctx, job, report));
    REQUIRE(!report.error.empty());

    Request request(f, { lockCase(f), bad });   // the second stream is the bad one
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = reinterpret_cast<tmdq_run*>(std::uintptr_t{1});
    CHECK(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_ERR_JOB));
    REQUIRE(run == nullptr);
    CHECK(std::string(tmdq_last_error()) == report.error);
    // Nothing was started, so nothing but `log` can have been called.
    REQUIRE(!seen.any("error:"));
    REQUIRE(!seen.any("progress:"));
    REQUIRE(!seen.any("stream:"));
    REQUIRE(!seen.any("finished:"));
}

TEST_CASE("Abi: a recording that cannot be used fails to prepare")
{
    const StepCal f = loadStepCal();

    // A file that is not there.
    StepCal missing = f;
    missing.path = tempUtf8() + "/tst_abi_no_such_recording.ch10";
    Request request(missing, { lockCase(f) });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = reinterpret_cast<tmdq_run*>(std::uintptr_t{1});
    CHECK(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_ERR_PREPARE));
    REQUIRE(run == nullptr);
    REQUIRE(!std::string(tmdq_last_error()).empty());
    REQUIRE(!seen.any("finished:"));

    // A channel the recording does not have.
    Case ghost = lockCase(f);
    ghost.pcmId = f.pcmId + 1000;
    Request ghostRequest(f, { ghost });
    CHECK(tmdq_run_start(&ghostRequest.req, &callbacks, &run) == int(TMDQ_ERR_PREPARE));
    REQUIRE(run == nullptr);
    REQUIRE(!std::string(tmdq_last_error()).empty());
}

TEST_CASE("Abi: bad arguments to the run functions are refused")
{
    const StepCal f = loadStepCal();

    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    Request good(f, { lockCase(f) });

    CHECK(tmdq_run_start(nullptr, &callbacks, &run) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_run_start(&good.req, &callbacks, nullptr) == int(TMDQ_ERR_BADARG));

    tmdq_run_request r = good.req;
    r.filename_utf8 = nullptr;
    CHECK(tmdq_run_start(&r, &callbacks, &run) == int(TMDQ_ERR_BADARG));
    r = good.req;
    r.filename_utf8 = "";
    CHECK(tmdq_run_start(&r, &callbacks, &run) == int(TMDQ_ERR_BADARG));
    r = good.req;
    r.time_channel_id = -1;
    CHECK(tmdq_run_start(&r, &callbacks, &run) == int(TMDQ_ERR_BADARG));
    CHECK(std::string(tmdq_last_error()) == std::string("Invalid time channel."));
    r = good.req;
    r.stream_count = 0;
    CHECK(tmdq_run_start(&r, &callbacks, &run) == int(TMDQ_ERR_BADARG));
    CHECK(std::string(tmdq_last_error()) == std::string("No streams selected for processing. Configure streams first."));
    r = good.req;
    r.streams = nullptr;
    CHECK(tmdq_run_start(&r, &callbacks, &run) == int(TMDQ_ERR_BADARG));

    // A null stream pointer, an unknown mode, a calibration count with no array, a profile with no points.
    const tmdq_stream_config* nullStream[] = { nullptr };
    r = good.req;
    r.streams = nullStream;
    CHECK(tmdq_run_start(&r, &callbacks, &run) == int(TMDQ_ERR_BADARG));

    tmdq_stream_config c = good.configs[0];
    const tmdq_stream_config* one[] = { &c };
    r = good.req;
    r.streams = one;
    c.mode = 9;
    CHECK(tmdq_run_start(&r, &callbacks, &run) == int(TMDQ_ERR_BADARG));
    REQUIRE(std::string(tmdq_last_error()).find("mode") != std::string::npos);
    c = good.configs[0];
    c.calibration_count = 2;
    c.calibration = nullptr;
    CHECK(tmdq_run_start(&r, &callbacks, &run) == int(TMDQ_ERR_BADARG));
    c = good.configs[0];
    c.calibration_count = -1;
    CHECK(tmdq_run_start(&r, &callbacks, &run) == int(TMDQ_ERR_BADARG));
    c = good.configs[0];
    tmdq_cal_profile profile{};
    profile.point_count = 3;
    profile.points = nullptr;
    c.calibration = &profile;
    c.calibration_count = 1;
    CHECK(tmdq_run_start(&r, &callbacks, &run) == int(TMDQ_ERR_BADARG));
    REQUIRE(run == nullptr);
    REQUIRE(!seen.any("finished:"));

    // The remaining functions tolerate NULL.
    tmdq_run_cancel(nullptr);
    CHECK(tmdq_run_running(nullptr) == 0);
    tmdq_run_wait(nullptr);
    tmdq_run_free(nullptr);
    tmdq_stream_result_free(nullptr);
    tmdq_stream_info info{};
    info.struct_size = sizeof(info);
    CHECK(tmdq_stream_result_info(nullptr, &info) == int(TMDQ_ERR_BADARG));
    tmdq_channel_series series{};
    series.struct_size = sizeof(series);
    CHECK(tmdq_stream_result_channel(nullptr, 0, &series) == int(TMDQ_ERR_BADARG));

    // A run with no callbacks at all is allowed: it just runs.
    tmdq_run* quiet = nullptr;
    REQUIRE(tmdq_run_start(&good.req, nullptr, &quiet) == int(TMDQ_OK));
    REQUIRE(quiet != nullptr);
    tmdq_run_wait(quiet);
    CHECK(tmdq_run_running(quiet) == 0);
    tmdq_run_free(quiet);
}

TEST_CASE("Abi: cancelling ends a run unsuccessfully")
{
    const StepCal prn = loadPrnSample();

    Case c;
    c.pcmId   = prn.pcmId;
    c.label   = "prn";
    c.pattern = "334AABBF";
    c.bits    = 32767;
    Request request(prn, { c });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    RunGuard guard{run};
    CHECK(tmdq_run_running(run) == 1);
    tmdq_run_cancel(run);
    tmdq_run_cancel(run);   // twice is as good as once
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_run_wait(run);

    REQUIRE(seen.finishedCount == 1);
    CHECK(seen.finishedSuccess == 0);
    REQUIRE_MESSAGE(!seen.any("stream:"), seen.joined());   // a cancelled run delivers nothing
    CHECK(seen.snapshot().back() == std::string("finished:0"));
    CHECK(tmdq_run_running(run) == 0);
}

TEST_CASE("Abi: freeing a run in flight cancels and waits")
{
    const StepCal prn = loadPrnSample();

    Case c;
    c.pcmId   = prn.pcmId;
    c.pattern = "334AABBF";
    c.bits    = 32767;
    Request request(prn, { c });
    auto seen = std::make_unique<Seen>();
    const tmdq_callbacks callbacks = seen->callbacks();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    tmdq_run_free(run);   // no cancel, no wait: free does both

    // Every thread has ended by the time free returns, so `finished` has been delivered, once, last, and
    // the callback target can be destroyed straight away without a callback landing on it.
    REQUIRE(seen->finishedCount == 1);
    CHECK(seen->finishedSuccess == 0);
    CHECK(seen->snapshot().back() == std::string("finished:0"));
    seen.reset();
}

TEST_CASE("Abi: a result outlives its run and is freed from any thread")
{
    const StepCal f = loadStepCal();

    Request request(f, { receiverCase(f) });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_run_free(run);   // the run is gone

    REQUIRE(seen.results.size() == std::size_t{1});
    tmdq_stream_result* result = seen.results[0];
    tmdq_stream_info info{};
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_stream_result_info(result, &info) == int(TMDQ_OK));
    REQUIRE(info.sample_count > 0);
    REQUIRE(info.channel_count > 0);
    // The views are still good: read every value, then free the result on a different thread.
    double sum = 0.0;
    for (int i = 0; i < info.sample_count; ++i)
        sum += info.times_sec[i];
    tmdq_channel_series series{};
    series.struct_size = sizeof(series);
    REQUIRE(tmdq_stream_result_channel(result, info.channel_count - 1, &series) == int(TMDQ_OK));
    for (int i = 0; i < info.sample_count; ++i)
        sum += series.values[i];
    REQUIRE(sum == sum);   // not NaN: they were all readable
    REQUIRE(tmdq_stream_result_channel(result, info.channel_count, &series) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_stream_result_channel(result, -1, &series) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_stream_result_channel(result, 0, nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_stream_result_info(result, nullptr) == int(TMDQ_ERR_BADARG));

    seen.results.clear();
    std::thread([result] { tmdq_stream_result_free(result); }).join();
}

TEST_CASE("Abi: a callback that throws does not break the run")
{
    const StepCal f = loadStepCal();

    Request request(f, { lockCase(f), receiverCase(f) });
    Seen seen;
    seen.throwing = true;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    RunGuard guard{run};
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_run_wait(run);

    // Every callback threw, and the run neither died nor stopped reporting.
    REQUIRE(seen.finishedCount == 1);
    CHECK(seen.finishedSuccess == 1);
    REQUIRE(seen.count("stream:0") == 1);
    REQUIRE(seen.count("stream:1") == 1);
}

TEST_CASE("Abi: two runs at once agree with one alone")
{
    const StepCal f = loadStepCal();

    bool coreOk = false;
    const auto want = runCore(f, 0, { lockCase(f), receiverCase(f) }, rootUtf8(), coreOk);
    REQUIRE(coreOk);

    Request a(f, { lockCase(f), receiverCase(f) });
    Request b(f, { lockCase(f), receiverCase(f) });
    Seen seenA, seenB;
    const tmdq_callbacks ca = seenA.callbacks();
    const tmdq_callbacks cb = seenB.callbacks();
    tmdq_run* runA = nullptr;
    tmdq_run* runB = nullptr;
    REQUIRE(tmdq_run_start(&a.req, &ca, &runA) == int(TMDQ_OK));
    RunGuard guardA{runA};
    REQUIRE(tmdq_run_start(&b.req, &cb, &runB) == int(TMDQ_OK));
    RunGuard guardB{runB};
    REQUIRE_MESSAGE(seenA.waitFinished(kTimeoutMs), seenA.joined());
    REQUIRE_MESSAGE(seenB.waitFinished(kTimeoutMs), seenB.joined());
    tmdq_run_wait(runA);
    tmdq_run_wait(runB);

    for (Seen* seen : { &seenA, &seenB })
    {
        CHECK(seen->finishedSuccess == 1);
        const auto got = seen->byJob();
        REQUIRE(got.size() == std::size_t{2});
        for (int job = 0; job < 2; ++job)
        {
            const std::string diff = differences(got.at(job), want.at(job));
            REQUIRE_MESSAGE(diff.empty(), ("job " + std::to_string(job) + " differs in: " + diff));
        }
    }
}

TEST_CASE("Abi: a successful call clears the last error")
{
    const StepCal f = loadStepCal();

    tmdq_file* file = nullptr;
    CHECK(tmdq_file_open("no-such-file.ch10", &file) == int(TMDQ_ERR_PREPARE));
    REQUIRE(!std::string(tmdq_last_error()).empty());
    REQUIRE(tmdq_file_open(f.path.c_str(), &file) == int(TMDQ_OK));
    CHECK(std::string(tmdq_last_error()) == std::string());   // the success left no stale message behind

    tmdq_channel_info info{};
    info.struct_size = sizeof(info);
    CHECK(tmdq_file_pcm_channel(file, 99, &info) == int(TMDQ_ERR_BADARG));
    REQUIRE(!std::string(tmdq_last_error()).empty());
    REQUIRE(tmdq_file_pcm_channel(file, 0, &info) == int(TMDQ_OK));
    CHECK(std::string(tmdq_last_error()) == std::string());
    CHECK(tmdq_file_time_channel(file, 99, &info) == int(TMDQ_ERR_BADARG));
    REQUIRE(tmdq_file_time_channel(file, 0, &info) == int(TMDQ_OK));
    CHECK(std::string(tmdq_last_error()) == std::string());
    tmdq_file_close(file);

    Request request(f, { lockCase(f) });
    Seen seen;
    tmdq_run_request bad = request.req;
    bad.stream_count = 0;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    CHECK(tmdq_run_start(&bad, &callbacks, &run) == int(TMDQ_ERR_BADARG));
    REQUIRE(!std::string(tmdq_last_error()).empty());
    REQUIRE(runToTheEnd(request, seen));
    CHECK(std::string(tmdq_last_error()) == std::string());

    tmdq_stream_info info2{};
    info2.struct_size = sizeof(info2);
    CHECK(tmdq_stream_result_info(nullptr, &info2) == int(TMDQ_ERR_BADARG));
    REQUIRE(!std::string(tmdq_last_error()).empty());
    REQUIRE(tmdq_stream_result_info(seen.results.at(0), &info2) == int(TMDQ_OK));
    CHECK(std::string(tmdq_last_error()) == std::string());
    tmdq_channel_series series{};
    series.struct_size = sizeof(series);
    CHECK(tmdq_stream_result_channel(seen.results.at(0), 0, &series) == int(TMDQ_ERR_BADARG));   // a lock stream has none
    REQUIRE(!std::string(tmdq_last_error()).empty());
}

TEST_CASE("Abi: the exception guard turns a throw into a status")
{
    // The one place a failure the library did not foresee is caught, so it is tried directly.
    tmdq::setError("stale");
    REQUIRE(tmdq::guarded("a call", []() -> int32_t { return TMDQ_OK; }) == int(TMDQ_OK));
    CHECK(tmdq::guarded("a call", []() -> int32_t { throw std::runtime_error("boom"); }) == int(TMDQ_ERR_INTERNAL));
    REQUIRE(std::string(tmdq_last_error()).find("boom") != std::string::npos);
    REQUIRE(std::string(tmdq_last_error()).find("a call") != std::string::npos);
    CHECK(tmdq::guarded("another", []() -> int32_t { throw 42; }) == int(TMDQ_ERR_INTERNAL));
    REQUIRE(std::string(tmdq_last_error()).find("unknown exception") != std::string::npos);
    REQUIRE(std::string(tmdq_last_error()).find("another") != std::string::npos);
    tmdq::clearError();
    CHECK(std::string(tmdq_last_error()) == std::string());

    // And a path that is not valid UTF-8 is a failure the host is told about, not a crash.
    tmdq_file* file = nullptr;
    const int32_t status = tmdq_file_open("\xff\xfe\xc3.ch10", &file);
    REQUIRE_MESSAGE((status == TMDQ_ERR_PREPARE || status == TMDQ_ERR_INTERNAL), std::to_string(status));
    REQUIRE(file == nullptr);
    REQUIRE(!std::string(tmdq_last_error()).empty());
}

TEST_CASE("Abi: the byte order of the file reaches the job")
{
    const StepCal f = loadStepCal();

    // Byte order is file-scoped and comes from the request, not from a stream. Without the swap this
    // recording decodes to something else entirely; both must match what the core makes with the same flag.
    const std::vector<Case> cases = { lockCase(f) };
    bool okSwap = false, okNoSwap = false;
    const auto swapped = runCore(f, 0, cases, rootUtf8(), okSwap, true);
    const auto plain = runCore(f, 0, cases, rootUtf8(), okNoSwap, false);
    REQUIRE(okSwap);
    REQUIRE((okNoSwap || plain.empty()));   // an unswapped run may find no lock at all, which is also a result
    REQUIRE_MESSAGE((plain.empty() || plain.at(0).lockPercent != swapped.at(0).lockPercent), "the fixture should decode differently without the swap");

    Request request(f, cases);
    request.req.swap_bytes = 0;
    Seen seenPlain;
    const bool endedWell = runToTheEnd(request, seenPlain);
    if (plain.empty())
    {
        REQUIRE(!endedWell);
        REQUIRE(seenPlain.results.empty());
    }
    else
    {
        REQUIRE(endedWell);
        REQUIRE_MESSAGE(differences(seenPlain.byJob().at(0), plain.at(0)).empty(), differences(seenPlain.byJob().at(0), plain.at(0)));
    }

    Request swappedRequest(f, cases);
    swappedRequest.req.swap_bytes = 1;
    Seen seenSwapped;
    REQUIRE(runToTheEnd(swappedRequest, seenSwapped));
    REQUIRE_MESSAGE(differences(seenSwapped.byJob().at(0), swapped.at(0)).empty(), differences(seenSwapped.byJob().at(0), swapped.at(0)));
}

TEST_CASE("Abi: a receiver parameters file and the app root reach the job")
{
    const StepCal f = loadStepCal();

    Scratch dir;
    const std::string two = dir.filePath("two.toml");
    REQUIRE(fs::create_directories(textfile::pathFromUtf8(dir.filePath("root/settings/receiver_params"))));
    auto write = [](const std::string& path, const char* text) { writeBytes(textfile::pathFromUtf8(path), text); };
    write(two, "[L_RCVR1]\nWord = 5\n[R_RCVR1]\nWord = 6\n");
    write(dir.filePath("root/settings/receiver_params/default.toml"), "[C_RCVR1]\nWord = 7\n");
    const std::string rootWithDefault = dir.filePath("root");

    // (1) A word-map file named on the stream.
    Case named = receiverCase(f);
    named.receiverParams = two;
    bool ok = false;
    const auto wantNamed = runCore(f, 0, { named }, rootUtf8(), ok);
    REQUIRE(ok);
    REQUIRE(wantNamed.at(0).channels.size() == std::size_t{2});
    Request namedRequest(f, { named });
    Seen seenNamed;
    REQUIRE(runToTheEnd(namedRequest, seenNamed));
    REQUIRE_MESSAGE(differences(seenNamed.byJob().at(0), wantNamed.at(0)).empty(), differences(seenNamed.byJob().at(0), wantNamed.at(0)));

    // (2) No file named: the one under the application root, if it has one.
    const Case plain = receiverCase(f);
    const auto wantRoot = runCore(f, 0, { plain }, rootWithDefault, ok);
    REQUIRE(ok);
    REQUIRE(wantRoot.at(0).channels.size() == std::size_t{1});
    Request rootRequest(f, { plain });
    rootRequest.appRoot = rootWithDefault;
    rootRequest.req.app_root_utf8 = rootRequest.appRoot.c_str();
    Seen seenRoot;
    REQUIRE(runToTheEnd(rootRequest, seenRoot));
    REQUIRE_MESSAGE(differences(seenRoot.byJob().at(0), wantRoot.at(0)).empty(), differences(seenRoot.byJob().at(0), wantRoot.at(0)));

    // (3) No application root at all (NULL): the default word map, which is not either of the above.
    const auto wantNone = runCore(f, 0, { plain }, std::string(), ok);
    REQUIRE(ok);
    REQUIRE(wantNone.at(0).channels.size() != wantRoot.at(0).channels.size());
    Request noneRequest(f, { plain });
    noneRequest.req.app_root_utf8 = nullptr;
    Seen seenNone;
    REQUIRE(runToTheEnd(noneRequest, seenNone));
    REQUIRE_MESSAGE(differences(seenNone.byJob().at(0), wantNone.at(0)).empty(), differences(seenNone.byJob().at(0), wantNone.at(0)));
}

TEST_CASE("Abi: the first profile of a word wins")
{
    const StepCal f = loadStepCal();

    // Two profiles for the same word are a host mistake; the first is used, as documented.
    Case first = receiverCase(f);
    first.calibration[1] = { {0.0, -10.0}, {1000.0, 0.0}, {30000.0, 40.0}, {65535.0, 60.0} };
    bool ok = false;
    const auto want = runCore(f, 0, { first }, rootUtf8(), ok);
    REQUIRE(ok);

    Request request(f, { first });
    const tmdq_cal_point other[] = { {0.0, 0.0}, {65535.0, 1.0} };
    tmdq_cal_profile two[2] = { request.cases[0].wireProfiles[0], request.cases[0].wireProfiles[0] };
    two[1].points = other;
    two[1].point_count = 2;
    request.configs[0].calibration = two;
    request.configs[0].calibration_count = 2;
    Seen seen;
    REQUIRE(runToTheEnd(request, seen));
    REQUIRE_MESSAGE(differences(seen.byJob().at(0), want.at(0)).empty(), differences(seen.byJob().at(0), want.at(0)));

    // And in the other order the other profile is the one that counts.
    std::swap(two[0], two[1]);
    Seen swapped;
    REQUIRE(runToTheEnd(request, swapped));
    REQUIRE(!differences(swapped.byJob().at(0), want.at(0)).empty());
}

TEST_CASE("Abi: wait alone is enough to see the end")
{
    const StepCal f = loadStepCal();

    // A host that does not use the callback to learn the end calls tmdq_run_wait; when it returns, the run
    // is over and everything it was going to report has been reported.
    Request request(f, { lockCase(f), receiverCase(f) });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    RunGuard guard{run};
    tmdq_run_wait(run);
    CHECK(tmdq_run_running(run) == 0);
    REQUIRE(seen.finishedCount == 1);
    REQUIRE(seen.results.size() == std::size_t{2});
    tmdq_run_wait(run);   // and again is harmless
    REQUIRE(seen.finishedCount == 1);
}

TEST_CASE("Abi: optional text may be left null")
{
    const StepCal f = loadStepCal();

    // A label, a mask and a word-map file are all optional: NULL reads the same as empty.
    Case blank = lockCase(f);
    blank.label.clear();
    bool ok = false;
    const auto want = runCore(f, 0, { blank }, rootUtf8(), ok);
    REQUIRE(ok);
    CHECK(want.at(0).streamLabel == "Ch " + std::to_string(f.pcmId));   // the label an empty one is given

    Request request(f, { blank });
    request.configs[0].label_utf8 = nullptr;
    request.configs[0].sync_mask_utf8 = nullptr;
    request.configs[0].receiver_params_toml_utf8 = nullptr;
    Seen seen;
    REQUIRE(runToTheEnd(request, seen));
    REQUIRE_MESSAGE(differences(seen.byJob().at(0), want.at(0)).empty(), differences(seen.byJob().at(0), want.at(0)));
}

TEST_CASE("Abi: the lines building jobs produced reach log first")
{
    const StepCal f = loadStepCal();

    // A stream with a non-linear profile makes the job builder write log lines; they are reported through
    // `log`, in order, on the thread that called start, before it returns - ahead of anything the run says.
    Case profiled = receiverCase(f);
    profiled.calibration[1] = { {0.0, -10.0}, {1000.0, 0.0}, {30000.0, 40.0}, {65535.0, 60.0} };

    ProcessingParams base;
    base.filename = textfile::pathFromUtf8(f.path);
    base.timeChannelId = f.timeId;
    StreamJobContext ctx;
    ctx.appRoot = rootUtf8();
    StreamJob job;
    StreamJobReport report;
    REQUIRE(buildStreamJob(profiled.core(), base, ctx, job, report));
    REQUIRE_MESSAGE(!report.log.empty(), "the job builder should have something to say about a profile");

    Request request(f, { lockCase(f), profiled });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    const std::thread::id caller = std::this_thread::get_id();
    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.req, &callbacks, &run) == int(TMDQ_OK));
    RunGuard guard{run};
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_run_wait(run);

    const std::vector<std::string> events = seen.snapshot();
    REQUIRE(events.size() > report.log.size());
    for (std::size_t i = 0; i < report.log.size(); ++i)
    {
        CHECK(events[i] == "log:" + report.log[i]);
        REQUIRE(seen.logThreads[i] == caller);
    }
}

TEST_CASE("Abi: every setting of a stream reaches the job")
{
    const StepCal f = loadStepCal();

    // One stream, changed in one setting at a time. For each, the core's own run of the changed stream must
    // differ from its run of the unchanged one (or the row proves nothing), and the run through the ABI must
    // equal the core's. A setting the ABI forgot to carry would give the unchanged stream's result.
    struct Row
    {
        const char* name;
        Case        base;
        Case        variant;
        std::string appRoot;
    };
    const std::string project = rootUtf8();
    std::vector<Row> rows;
    auto add = [&](const char* name, Case base, auto change, const std::string& appRoot) {
        Case variant = base;
        change(variant);
        rows.push_back({name, base, variant, appRoot});
    };

    add("sync mask", lockCase(f), [](Case& c) { c.mask = "FFFF0000"; }, project);
    add("randomized", lockCase(f), [](Case& c) { c.randomized = true; }, project);
    add("inverted", lockCase(f), [](Case& c) { c.inverted = true; }, project);
    add("data rate", lockCase(f), [](Case& c) { c.rateMbps = 0.4; }, project);
    add("sample period", lockCase(f), [](Case& c) { c.periodIndex = TimeConstants::kDefaultSamplePeriodIndex + 1; }, project);
    add("scale", receiverCase(f), [](Case& c) { c.scale = c.scale * 2.0; }, project);
    add("polarity", receiverCase(f), [](Case& c) { c.polarityIndex = c.polarityIndex == 0 ? 1 : 0; }, project);
    add("slope", receiverCase(f), [](Case& c) { c.slopeIndex = c.slopeIndex - 1; }, project);
    // The receiver grid is only used when there is no word-map file, so these two run with no application root.
    add("receiver count", receiverCase(f), [](Case& c) { c.numReceivers = 2; }, std::string());
    add("receiver channels", receiverCase(f), [](Case& c) { c.receiverChannels = 1; }, std::string());

    for (Row& row : rows)
    {
        const std::string name = row.name;
        bool okBase = false, okVariant = false;
        const auto base = runCore(f, 0, { row.base }, row.appRoot, okBase);
        const auto variant = runCore(f, 0, { row.variant }, row.appRoot, okVariant);
        REQUIRE_MESSAGE((okBase && base.size() == 1), (name + ": the unchanged stream must run"));
        REQUIRE_MESSAGE(okVariant, (name + ": the changed stream must build and run"));
        REQUIRE_MESSAGE((variant.empty() || !sameStream(variant.at(0), base.at(0))), (name + ": changing it should change the core's result, or the row proves nothing"));

        Request request(f, { row.variant });
        request.appRoot = row.appRoot;
        request.req.app_root_utf8 = request.appRoot.c_str();
        Seen seen;
        const bool ended = runToTheEnd(request, seen);
        if (variant.empty())
        {
            REQUIRE_MESSAGE((!ended && seen.results.empty()), (name + ": the core finds nothing, so must the ABI"));
        }
        else
        {
            REQUIRE_MESSAGE(ended, (name + ": the run should complete"));
            const std::string diff = differences(seen.byJob().at(0), variant.at(0));
            REQUIRE_MESSAGE(diff.empty(), (name + " differs in: " + diff));
        }
    }
}

TEST_CASE("Abi: reading a host struct never strays beyond the known one")
{
    // A newer host's longer struct is read only as far as this library knows it; the bytes after it, in the
    // destination, are never touched.
    struct Source { tmdq_channel_info info; unsigned char extra[32]; };
    Source source{};
    source.info.struct_size = sizeof(Source);
    source.info.channel_id = 42;
    std::memset(source.extra, 0x5A, sizeof(source.extra));

    struct Destination { tmdq_channel_info info; unsigned char guard[64]; };
    Destination dest;
    std::memset(&dest, 0xCD, sizeof(dest));
    REQUIRE(tmdq::readStruct(&source.info, sizeof(tmdq_channel_info), dest.info));
    CHECK(dest.info.channel_id == 42);
    CHECK(int(dest.info.struct_size) == int(sizeof(tmdq_channel_info)));   // the size this library read it as
    for (unsigned char b : dest.guard)
        CHECK(int(b) == 0xCD);

    // A shorter struct than the first revision is refused, a NULL too.
    source.info.struct_size = sizeof(tmdq_channel_info) - 1;
    REQUIRE(!tmdq::readStruct(&source.info, sizeof(tmdq_channel_info), dest.info));
    REQUIRE(!tmdq::readStruct(static_cast<const tmdq_channel_info*>(nullptr), sizeof(tmdq_channel_info), dest.info));
}
