// Calibration through the C ABI (src/core/abi/abi_calibration.cpp): the step file, a calibration is the extractor's, every
// setting reaching it, cancel, free and wait (and from its own callback), bad arguments, the relaxed threshold.

#include "test_abi_support.h"

TEST_CASE("Abi: the step file is read like the core")
{
    const std::string path = rootUtf8() + "/settings/rcvr_cals/default.toml";
    std::vector<StepDefinition> want;
    std::string error;
    REQUIRE_MESSAGE(StepDetector::parseStepConfig(textfile::pathFromUtf8(path), want, error), error);
    REQUIRE(want.size() > 5);

    tmdq_steps* steps = nullptr;
    REQUIRE(tmdq_steps_parse(path.c_str(), &steps) == int(TMDQ_OK));
    REQUIRE(steps != nullptr);
    REQUIRE(tmdq_steps_count(steps) == int(want.size()));
    const double* db = tmdq_steps_db(steps);
    REQUIRE(db != nullptr);
    for (std::size_t i = 0; i < want.size(); ++i)
        CHECK(db[i] == want[i].db);
    tmdq_steps_free(steps);

    // A file that is not there says what the parser says; a file with no step in it is the same.
    tmdq_steps* none = reinterpret_cast<tmdq_steps*>(std::uintptr_t{1});
    std::vector<StepDefinition> ignored;
    const std::string missing = tempUtf8() + "/tst_abi_no_such_steps.toml";
    REQUIRE(!StepDetector::parseStepConfig(textfile::pathFromUtf8(missing), ignored, error));
    CHECK(tmdq_steps_parse(missing.c_str(), &none) == int(TMDQ_ERR_PREPARE));
    REQUIRE(none == nullptr);
    CHECK(std::string(tmdq_last_error()) == error);

    Scratch dir;
    const std::string emptyPath = writeFile(dir.filePath("empty.toml"), "# no steps here\n");
    REQUIRE(!StepDetector::parseStepConfig(textfile::pathFromUtf8(emptyPath), ignored, error));
    CHECK(tmdq_steps_parse(emptyPath.c_str(), &none) == int(TMDQ_ERR_PREPARE));
    CHECK(std::string(tmdq_last_error()) == error);

    // Arguments.
    CHECK(tmdq_steps_parse(nullptr, &none) == int(TMDQ_ERR_BADARG));
    REQUIRE(none == nullptr);
    CHECK(tmdq_steps_parse(path.c_str(), nullptr) == int(TMDQ_ERR_BADARG));
    REQUIRE(tmdq_steps_count(nullptr) == -1);
    REQUIRE(tmdq_steps_db(nullptr) == nullptr);
    tmdq_steps_free(nullptr);
}

TEST_CASE("Abi: the initial step file path is written snprintf-style")
{
    const std::string root = rootUtf8();
    const std::string want = StepDetector::initialStepConfigPath(root, std::string());
    REQUIRE_MESSAGE(!want.empty(), "the shipped default step file should be found under the project root");

    // Measuring, then writing exactly, then writing into a buffer that is too small.
    REQUIRE(tmdq_step_config_initial_path(root.c_str(), nullptr, nullptr, 0) == int(want.size()));
    std::vector<char> exact(want.size() + 1, 'x');
    REQUIRE(tmdq_step_config_initial_path(root.c_str(), nullptr, exact.data(), int(exact.size())) == int(want.size()));
    CHECK(std::string(exact.data()) == want);

    std::vector<char> tight(8, 'x');
    REQUIRE(tmdq_step_config_initial_path(root.c_str(), "", tight.data(), int(tight.size())) == int(want.size()));
    CHECK(std::string(tight.data()) == want.substr(0, 7));   // truncated, and terminated
    CHECK(int(tight[7]) == 0);

    // A preferred file that exists wins; one that does not is passed over.
    const std::string preferred = root + "/settings/rcvr_cals/TRC.toml";
    std::vector<char> big(512, 'x');
    REQUIRE(tmdq_step_config_initial_path(root.c_str(), preferred.c_str(), big.data(), int(big.size())) > 0);
    CHECK(std::string(big.data()) == StepDetector::initialStepConfigPath(root, preferred));
    CHECK(std::string(big.data()) == preferred);
    std::fill(big.begin(), big.end(), 'x');
    tmdq_step_config_initial_path(root.c_str(), (root + "/no/such.toml").c_str(), big.data(), int(big.size()));
    CHECK(std::string(big.data()) == want);

    // No app root and nothing preferred: empty.
    std::fill(big.begin(), big.end(), 'x');
    REQUIRE(tmdq_step_config_initial_path(nullptr, nullptr, big.data(), int(big.size())) == int(StepDetector::initialStepConfigPath(std::string(), std::string()).size()));

    // A buffer and capacity that do not make sense are an error, not a write.
    CHECK(tmdq_step_config_initial_path(root.c_str(), nullptr, nullptr, 10) == -1);
    CHECK(tmdq_step_config_initial_path(root.c_str(), nullptr, big.data(), -1) == -1);
}

TEST_CASE("Abi: a calibration matches what the extractor makes")
{
    const StepCal f = loadStepCal();

    const CalCase c(f);
    const DirectCalibration want = runDirectCalibration(c);
    REQUIRE_MESSAGE(want.ok, want.summary);
    REQUIRE(want.results.size() == std::size_t{48});
    int calibrated = 0;
    for (const CalibrationChannelResult& r : want.results)
        calibrated += r.profile.valid ? 1 : 0;
    REQUIRE_MESSAGE(calibrated >= 3, "the fixture should calibrate some channels, or the comparison proves little");

    CalRequest request(c);
    Seen seen;
    CalGuard guard;
    REQUIRE_MESSAGE(calibrateToTheEnd(request, seen, guard), seen.joined());
    const std::string diff = calibrationDifferences(guard.handle, want);
    REQUIRE_MESSAGE(diff.empty(), diff);

    // The documented order: finished last and once, progress from a single thread, never a stream result.
    const std::vector<std::string> events = seen.snapshot();
    CHECK(events.back() == std::string("finished:1"));
    REQUIRE(seen.finishedCount == 1);
    REQUIRE(!seen.any("stream:"));
    REQUIRE(seen.progressThreads.size() == std::size_t{1});
    REQUIRE(seen.any("log:"));
    CHECK(tmdq_calibration_running(guard.handle) == 0);

    // The profiles it found feed a run: the same receiver stream, through the ABI and directly.
    Case calibrated_case = receiverCase(f);
    for (int32_t i = 0; i < int(want.results.size()); ++i)
    {
        tmdq_calibration_channel ch{};
        ch.struct_size = sizeof(ch);
        REQUIRE(tmdq_calibration_channel_result(guard.handle, i, &ch) == int(TMDQ_OK));
        if (ch.valid)
            calibrated_case.calibration[ch.word_index] = std::vector<tmdq_cal_point>(ch.points, ch.points + ch.point_count);
    }
    REQUIRE(!calibrated_case.calibration.empty());
    bool ok = false;
    const auto direct = runCore(f, 0, { calibrated_case }, rootUtf8(), ok);
    REQUIRE(ok);
    Request run(f, { calibrated_case });
    Seen runSeen;
    REQUIRE(runToTheEnd(run, runSeen));
    REQUIRE_MESSAGE(differences(runSeen.byJob().at(0), direct.at(0)).empty(), differences(runSeen.byJob().at(0), direct.at(0)));
}

TEST_CASE("Abi: every setting of a calibration reaches the extractor")
{
    const StepCal f = loadStepCal();

    // One setting changed at a time. For each, the core's result for the changed calibration must differ from
    // its result for the unchanged one (or the row proves nothing), and the run through the ABI must equal the
    // core's. A setting the ABI forgot to carry would give the unchanged calibration's result.
    Scratch dir;
    auto write = [](const std::string& path, const char* text) { writeBytes(textfile::pathFromUtf8(path), text); };
    write(dir.filePath("one.toml"), "[L_RCVR1]\nWord = 1\n");
    REQUIRE(fs::create_directories(textfile::pathFromUtf8(dir.filePath("root/settings/receiver_params"))));
    write(dir.filePath("root/settings/receiver_params/default.toml"), "[C_RCVR1]\nWord = 2\n[R_RCVR1]\nWord = 3\n");

    struct Row { const char* name; CalCase base; CalCase variant; };
    std::vector<Row> rows;
    auto addWith = [&](const char* name, auto change, const CalCase& base) {
        CalCase variant = base;
        change(variant);
        rows.push_back({name, base, variant});
    };
    auto add = [&](const char* name, auto change) { addWith(name, change, CalCase(f)); };
    // The grid fallback only applies when no word map is resolved: an application root with no default file.
    CalCase noMap(f);
    noMap.appRoot = dir.filePath("empty-root");

    add("randomized", [](CalCase& c) { c.in.randomized = true; });
    add("inverted", [](CalCase& c) { c.in.inverted = true; });
    add("byte order", [](CalCase& c) { c.in.swapBytes = false; });
    add("frame length", [](CalCase& c) { c.in.bitsInMinorFrame = 1600; });
    add("data rate", [](CalCase& c) { c.in.dataRateMbps = 0.2; });
    add("sync mask", [](CalCase& c) { c.in.syncMask = "FFFF0000"; });
    add("pcm channel", [](CalCase& c) { c.in.pcmChannelId += 1000; });
    add("clip start", [](CalCase& c) { c.in.clipStartSec = 3.0; });
    add("clip end", [](CalCase& c) { c.in.clipEndSec = 3.0; });
    add("clips beyond the recording", [](CalCase& c) { c.in.clipStartSec = 1.0e6; c.in.clipEndSec = 1.0e6; });
    add("steps", [](CalCase& c) { c.in.steps.pop_back(); });
    addWith("receiver count", [](CalCase& c) { c.in.numReceivers = 2; }, noMap);
    addWith("receiver channels", [](CalCase& c) { c.in.receiverChannels = 1; }, noMap);
    add("word-map file", [&](CalCase& c) { c.in.receiverParamsToml = textfile::pathFromUtf8(dir.filePath("one.toml")); });
    add("application root", [&](CalCase& c) { c.appRoot = dir.filePath("root"); });

    for (Row& row : rows)
    {
        const std::string name = row.name;
        const DirectCalibration base = runDirectCalibration(row.base);
        const DirectCalibration variant = runDirectCalibration(row.variant);
        REQUIRE_MESSAGE((base.built && base.ok), (name + ": the unchanged calibration must run: " + base.summary));
        REQUIRE_MESSAGE(variant.built, (name + ": the changed calibration must build"));
        REQUIRE_MESSAGE(!sameCalibration(base, variant), (name + ": changing it should change the core's result, or the row proves nothing"));

        CalRequest request(row.variant);
        Seen seen;
        CalGuard guard;
        const bool ended = calibrateToTheEnd(request, seen, guard);
        REQUIRE_MESSAGE(guard.handle != nullptr, (name + ": the calibration should start"));
        if (variant.ok)
            REQUIRE_MESSAGE(ended, (name + ": the calibration should complete"));
        else
            REQUIRE_MESSAGE(!ended, (name + ": the core fails, so must the ABI"));
        const std::string diff = calibrationDifferences(guard.handle, variant);
        REQUIRE_MESSAGE(diff.empty(), (name + " differs in: " + diff));
    }
}

TEST_CASE("Abi: a calibration setting the extractor cannot use is refused")
{
    const StepCal f = loadStepCal();

    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_calibration* handle = reinterpret_cast<tmdq_calibration*>(std::uintptr_t{1});

    CalCase badPattern(f);
    badPattern.in.syncPattern = "zz";
    CalRequest r1(badPattern);
    CHECK(tmdq_calibration_start(&r1.req, &callbacks, &handle) == int(TMDQ_ERR_JOB));
    REQUIRE(handle == nullptr);
    CHECK(std::string(tmdq_last_error()) == std::string("Invalid frame sync pattern 'zz'."));

    CalCase emptyPattern(f);
    emptyPattern.in.syncPattern.clear();
    CalRequest r2(emptyPattern);
    r2.req.sync_pattern_utf8 = nullptr;
    CHECK(tmdq_calibration_start(&r2.req, &callbacks, &handle) == int(TMDQ_ERR_JOB));
    CHECK(std::string(tmdq_last_error()) == std::string("Invalid frame sync pattern ''."));

    CalCase badMask(f);
    badMask.in.syncMask = "xyz";
    CalRequest r3(badMask);
    CHECK(tmdq_calibration_start(&r3.req, &callbacks, &handle) == int(TMDQ_ERR_JOB));
    CHECK(std::string(tmdq_last_error()) == std::string("Invalid frame sync mask 'xyz'."));

    // Nothing was started, so nothing was reported.
    REQUIRE(seen.snapshot().empty());
}

TEST_CASE("Abi: bad arguments to the calibration functions are refused")
{
    const StepCal f = loadStepCal();

    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    CalRequest good{CalCase(f)};
    tmdq_calibration* handle = reinterpret_cast<tmdq_calibration*>(std::uintptr_t{1});

    CHECK(tmdq_calibration_start(nullptr, &callbacks, &handle) == int(TMDQ_ERR_BADARG));
    REQUIRE(handle == nullptr);
    CHECK(tmdq_calibration_start(&good.req, &callbacks, nullptr) == int(TMDQ_ERR_BADARG));

    tmdq_calibration_request r = good.req;
    r.struct_size = sizeof(tmdq_calibration_request) - 1;
    CHECK(tmdq_calibration_start(&r, &callbacks, &handle) == int(TMDQ_ERR_BADARG));
    r = good.req;
    r.cal_filename_utf8 = nullptr;
    CHECK(tmdq_calibration_start(&r, &callbacks, &handle) == int(TMDQ_ERR_BADARG));
    r.cal_filename_utf8 = "";
    CHECK(tmdq_calibration_start(&r, &callbacks, &handle) == int(TMDQ_ERR_BADARG));
    r = good.req;
    r.step_count = -1;
    CHECK(tmdq_calibration_start(&r, &callbacks, &handle) == int(TMDQ_ERR_BADARG));
    r = good.req;
    r.step_db = nullptr;
    CHECK(tmdq_calibration_start(&r, &callbacks, &handle) == int(TMDQ_ERR_BADARG));
    tmdq_callbacks shortCallbacks = callbacks;
    shortCallbacks.struct_size = sizeof(tmdq_callbacks) - 1;
    CHECK(tmdq_calibration_start(&good.req, &shortCallbacks, &handle) == int(TMDQ_ERR_BADARG));
    REQUIRE(handle == nullptr);
    REQUIRE(seen.snapshot().empty());

    // NULL is tolerated where it makes sense.
    tmdq_calibration_cancel(nullptr);
    CHECK(tmdq_calibration_running(nullptr) == 0);
    tmdq_calibration_wait(nullptr);
    tmdq_calibration_free(nullptr);
    tmdq_calibration_info info{};
    info.struct_size = sizeof(info);
    CHECK(tmdq_calibration_result(nullptr, &info) == int(TMDQ_ERR_BADARG));
    tmdq_calibration_channel ch{};
    ch.struct_size = sizeof(ch);
    CHECK(tmdq_calibration_channel_result(nullptr, 0, &ch) == int(TMDQ_ERR_BADARG));

    // A run with no callbacks at all just runs; and a finished one refuses a bad index, a short struct, no output.
    CalGuard guard;
    REQUIRE(tmdq_calibration_start(&good.req, nullptr, &guard.handle) == int(TMDQ_OK));
    tmdq_calibration_wait(guard.handle);
    CHECK(tmdq_calibration_running(guard.handle) == 0);
    CHECK(tmdq_calibration_channel_result(guard.handle, 48, &ch) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_calibration_channel_result(guard.handle, -1, &ch) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_calibration_channel_result(guard.handle, 0, nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_calibration_result(guard.handle, nullptr) == int(TMDQ_ERR_BADARG));
    tmdq_calibration_info shortInfo{};
    shortInfo.struct_size = sizeof(shortInfo) - 1;
    CHECK(tmdq_calibration_result(guard.handle, &shortInfo) == int(TMDQ_ERR_BADARG));
    tmdq_calibration_channel shortChannel{};
    shortChannel.struct_size = sizeof(shortChannel) - 1;
    CHECK(tmdq_calibration_channel_result(guard.handle, 0, &shortChannel) == int(TMDQ_ERR_BADARG));
    REQUIRE(tmdq_calibration_channel_result(guard.handle, 0, &ch) == int(TMDQ_OK));
}

TEST_CASE("Abi: a calibration that cannot start reports through the sink")
{
    const StepCal f = loadStepCal();

    // A recording that is not there: the extractor reports it before any thread starts, so by the time
    // start returns the error and the end have been delivered, and the handle is already finished.
    CalCase missing(f);
    missing.in.calFilename = textfile::pathFromUtf8(tempUtf8() + "/tst_abi_no_such_cal.ch10");
    const DirectCalibration want = runDirectCalibration(missing);
    REQUIRE(!want.ok);
    REQUIRE(!want.error.empty());

    CalRequest request(missing);
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    CalGuard guard;
    REQUIRE(tmdq_calibration_start(&request.req, &callbacks, &guard.handle) == int(TMDQ_OK));
    REQUIRE(guard.handle != nullptr);
    REQUIRE(seen.finishedCount == 1);
    CHECK(seen.finishedSuccess == 0);
    REQUIRE(seen.any("error:"));
    CHECK(seen.snapshot().back() == std::string("finished:0"));
    CHECK(tmdq_calibration_running(guard.handle) == 0);
    const std::string diff = calibrationDifferences(guard.handle, want);
    REQUIRE_MESSAGE(diff.empty(), diff);

    // No steps at all is refused the same way, with the extractor's own words.
    const CalCase none(f, {});
    const DirectCalibration noSteps = runDirectCalibration(none);
    REQUIRE(!noSteps.ok);
    CalRequest empty(none);
    Seen seen2;
    const tmdq_callbacks callbacks2 = seen2.callbacks();
    CalGuard guard2;
    REQUIRE(tmdq_calibration_start(&empty.req, &callbacks2, &guard2.handle) == int(TMDQ_OK));
    CHECK(seen2.finishedSuccess == 0);
    REQUIRE_MESSAGE(calibrationDifferences(guard2.handle, noSteps).empty(), calibrationDifferences(guard2.handle, noSteps));
}

TEST_CASE("Abi: results are refused while a calibration runs and cancel ends it")
{
    const StepCal f = loadStepCal();

    CalRequest request{CalCase(f)};
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    CalGuard guard;
    REQUIRE(tmdq_calibration_start(&request.req, &callbacks, &guard.handle) == int(TMDQ_OK));
    CHECK(tmdq_calibration_running(guard.handle) == 1);

    tmdq_calibration_info info{};
    info.struct_size = sizeof(info);
    CHECK(tmdq_calibration_result(guard.handle, &info) == int(TMDQ_ERR_STATE));
    REQUIRE(!std::string(tmdq_last_error()).empty());
    tmdq_calibration_channel ch{};
    ch.struct_size = sizeof(ch);
    CHECK(tmdq_calibration_channel_result(guard.handle, 0, &ch) == int(TMDQ_ERR_STATE));

    tmdq_calibration_cancel(guard.handle);
    tmdq_calibration_cancel(guard.handle);   // twice is as good as once
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_calibration_wait(guard.handle);
    REQUIRE(seen.finishedCount == 1);
    CHECK(seen.finishedSuccess == 0);
    CHECK(tmdq_calibration_running(guard.handle) == 0);
    REQUIRE(tmdq_calibration_result(guard.handle, &info) == int(TMDQ_OK));
    CHECK(std::string(info.summary_utf8) == std::string("Calibration extraction cancelled."));
    CHECK(std::string(info.error_utf8) == std::string());
}

TEST_CASE("Abi: freeing a calibration in flight cancels and waits")
{
    const StepCal f = loadStepCal();

    CalRequest request{CalCase(f)};
    auto seen = std::make_unique<Seen>();
    const tmdq_callbacks callbacks = seen->callbacks();
    tmdq_calibration* handle = nullptr;
    REQUIRE(tmdq_calibration_start(&request.req, &callbacks, &handle) == int(TMDQ_OK));
    tmdq_calibration_free(handle);   // no cancel, no wait: free does both

    REQUIRE(seen->finishedCount == 1);
    CHECK(seen->finishedSuccess == 0);
    CHECK(seen->snapshot().back() == std::string("finished:0"));
    seen.reset();
}

TEST_CASE("Abi: wait alone is enough to see a calibration end")
{
    const StepCal f = loadStepCal();

    CalRequest request{CalCase(f)};
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    CalGuard guard;
    REQUIRE(tmdq_calibration_start(&request.req, &callbacks, &guard.handle) == int(TMDQ_OK));
    tmdq_calibration_wait(guard.handle);
    CHECK(tmdq_calibration_running(guard.handle) == 0);
    REQUIRE(seen.finishedCount == 1);
    CHECK(seen.finishedSuccess == 1);
    tmdq_calibration_info info{};
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_calibration_result(guard.handle, &info) == int(TMDQ_OK));
    REQUIRE(info.channel_count == 48);
    tmdq_calibration_wait(guard.handle);   // and again is harmless
}

TEST_CASE("Abi: waiting on a calibration from its own finished callback is an error not an exception")
{
    // As for a run: `finished` comes from the thread the wait would join, and the failure is a last error.
    const StepCal f = loadStepCal();

    CalRequest request{CalCase(f)};
    Seen seen;
    seen.waitFromFinished = true;
    seen.waitOnCalibration = true;
    const tmdq_callbacks callbacks = seen.callbacks();
    CalGuard guard;
    REQUIRE(tmdq_calibration_start(&request.req, &callbacks, &guard.handle) == int(TMDQ_OK));
    seen.waitOn = guard.handle;
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_calibration_wait(guard.handle);

    CHECK(!seen.waitThrew);
    CHECK_MESSAGE(seen.waitError == "tmdq_calibration_wait was called from one of the extraction's own callbacks and did not wait",
                  seen.waitError);
    CHECK(seen.finishedSuccess == 1);
}

TEST_CASE("Abi: freeing a calibration from its own finished callback is refused and its result is still there")
{
    // As for a run. What shows the extraction was not released is that its result can still be read afterwards.
    const StepCal f = loadStepCal();

    CalRequest request{CalCase(f)};
    Seen seen;
    seen.freeFromFinished = true;
    seen.waitOnCalibration = true;
    const tmdq_callbacks callbacks = seen.callbacks();
    CalGuard guard;
    REQUIRE(tmdq_calibration_start(&request.req, &callbacks, &guard.handle) == int(TMDQ_OK));
    seen.waitOn = guard.handle;
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    tmdq_calibration_wait(guard.handle);

    CHECK(!seen.waitThrew);
    CHECK_MESSAGE(seen.waitError == "tmdq_calibration_free was called from one of the extraction's own callbacks: the extraction was not released",
                  seen.waitError);
    CHECK(seen.finishedSuccess == 1);
    tmdq_calibration_info info{};
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_calibration_result(guard.handle, &info) == int(TMDQ_OK));
    CHECK(info.channel_count == 48);
}

TEST_CASE("Abi: a threshold that had to be relaxed is reported")
{
    const StepCal f = loadStepCal();

    // Four expected steps against a recording that sweeps eleven: the nominal edge threshold cannot resolve
    // so few levels and some channels only pair after it is lowered. The flag says so, and it is the one
    // per-channel flag the ordinary eleven-step run never sets.
    const CalCase c(f, { 0.0, 6.0, 12.0, 18.0 });
    const DirectCalibration want = runDirectCalibration(c);
    REQUIRE(want.ok);
    int relaxed = 0;
    for (const CalibrationChannelResult& r : want.results)
        relaxed += r.thresholdRelaxed ? 1 : 0;
    REQUIRE_MESSAGE(relaxed > 0, "the fixture should need a relaxed threshold with four steps, or this proves nothing");

    CalRequest request(c);
    Seen seen;
    CalGuard guard;
    REQUIRE_MESSAGE(calibrateToTheEnd(request, seen, guard), seen.joined());
    const std::string diff = calibrationDifferences(guard.handle, want);
    REQUIRE_MESSAGE(diff.empty(), diff);
    int reported = 0;
    for (int32_t i = 0; i < int(want.results.size()); ++i)
    {
        tmdq_calibration_channel ch{};
        ch.struct_size = sizeof(ch);
        REQUIRE(tmdq_calibration_channel_result(guard.handle, i, &ch) == int(TMDQ_OK));
        reported += ch.threshold_relaxed;
    }
    CHECK(reported == relaxed);
}
