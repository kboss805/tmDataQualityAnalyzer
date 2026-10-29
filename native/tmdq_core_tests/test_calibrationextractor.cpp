// CalibrationExtractor: the calibration pipeline's orchestration (US5.3), driven end to end on the committed
// recordings (the reader and a FrameProcessor on threads, then StepDetector per channel), plus the pure operator-facing
// report. No Qt; ported from the Qt TestCalibrationExtractor, whose cases are all here under the same descriptions (its
// data-driven cases are loops). StepDetector's own logic is test_stepdetector.cpp.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"
#include "calibrationextractor.h"
#include "calibrationprofile.h"
#include "chapter10reader.h"
#include "containerutil.h"
#include "framesetup.h"
#include "framesync.h"
#include "stepdetector.h"
#include "test_support.h"

using namespace tmdq_test;

namespace {

fs::path recording(const char* name)
{
    return repoFile(fs::path("tests") / "data" / "test files" / name);
}

/// How the bits are recovered, from the hex text the dialogs hold. The one place these tests make an Acquisition, so it is
/// decoded the way the application decodes it.
void setAcquisition(CalibrationExtractor::Request& req, const std::string& pattern, int bits, bool randomized,
                    double rate_mbps, bool swap_bytes)
{
    Acquisition a;
    REQUIRE_MESSAGE(resolveFrameSync(pattern, std::string(), a.sync) == FrameSyncError::None,
                    "test sync pattern '" << pattern << "' does not decode");
    a.bitsInMinorFrame = bits;
    a.randomized       = randomized;
    a.dataRateMbps     = rate_mbps;
    a.swapBytes        = swap_bytes;
    req.acquisition    = a;
}

std::string joinLines(const std::vector<std::string>& lines)
{
    std::string all;
    for (std::size_t i = 0; i < lines.size(); i++)
    {
        all += (i > 0 ? "\n" : "") + lines[i];
    }
    return all;
}

int countContaining(const std::vector<std::string>& lines, const std::string& needle)
{
    return static_cast<int>(std::count_if(lines.begin(), lines.end(),
                                          [&](const std::string& l) { return contains(l, needle); }));
}

/// One channel's outcome, with just the fields the report reads.
CalibrationChannelResult channelResult(const std::string& name, CalibrationOutcome outcome, int plateaus = 0,
                                       bool hadData = true)
{
    CalibrationChannelResult r;
    r.name             = name;
    r.outcome          = outcome;
    r.detectedPlateaus = plateaus;
    r.hadData          = hadData;
    r.profile.valid    = (outcome == CalibrationOutcome::Calibrated);
    return r;
}

/// A calibrated channel whose profile reaches @p top_db, optionally railed with steps it never measured.
CalibrationChannelResult railedResult(const std::string& name, double top_db, std::vector<double> unresolved)
{
    CalibrationChannelResult r = channelResult(name, CalibrationOutcome::Calibrated, 6);
    r.profile.points = {{1000.0, 0.0}, {2000.0, top_db / 2.0}, {3000.0, top_db}};
    r.saturated      = true;
    r.unresolvedDb   = std::move(unresolved);
    return r;
}

/// A channel that held one level for the whole recording.
CalibrationChannelResult flatResult(const std::string& name, double level)
{
    CalibrationChannelResult r = channelResult(name, CalibrationOutcome::FlatNoSignal, 1);
    r.plateauLevels = {level};
    return r;
}

const std::vector<std::string> kLcr = {"L", "R", "C"};

/// A minimal but non-empty step list, so a Request clears the "no steps" guard and actually drives the reader and the
/// frame setup.
std::vector<StepDefinition> dummySteps()
{
    std::vector<StepDefinition> steps;
    for (double db : {0.0, 6.0, 12.0})
    {
        steps.push_back(StepDefinition{db});
    }
    return steps;
}

/// Observes one extraction: every report it makes, in the order they arrive, and whether it finished. The ONE place these
/// tests attach to the extractor's reports or wait for its end. The reports arrive on the extractor's own threads (and
/// start()'s own failures on the caller's), so everything is under a lock.
class Recorder final : public ProcessingEvents, public std::enable_shared_from_this<Recorder>
{
public:
    explicit Recorder(CalibrationExtractor& extractor) : m_extractor(extractor) {}

    void logMessage(const std::string& message) override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            log.push_back(message);
            events.push_back("log");
        }
        m_cv.notify_all();
        maybePark(message);
        noteReport();
    }
    void errorOccurred(const std::string& message) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        errors.push_back(message);
    }
    void progressUpdated(int percent) override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            progress.push_back(percent);
            events.push_back("progress");
        }
        noteReport();
    }
    void processingFinished(bool ok) override
    {
        const std::string text = m_extractor.summary();
        const bool still_running = m_extractor.running();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            success      = ok;
            summary      = text;
            runningAtEnd = still_running;
            done         = true;
            events.push_back("finished");
        }
        m_cv.notify_all();
    }

    /// Parks the first thread that logs a line containing @p needle, inside logMessage(), until release(). That freezes the
    /// reader or the worker at a chosen point in a real run, so a test can act at exactly that moment instead of racing
    /// for it.
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
    /// Waits until some log line contains @p needle.
    bool waitForLog(const std::string& needle, int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
            return std::any_of(log.begin(), log.end(), [&](const std::string& l) { return contains(l, needle); });
        });
    }

    void start(const CalibrationExtractor::Request& request)
    {
        m_caller = std::this_thread::get_id();
        m_extractor.start(request, shared_from_this());
    }

    /// Waits until the extraction finishes, or @p timeout_ms passes. @return whether it finished.
    bool wait(int timeout_ms = 60000)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return done; });
    }

    /// Starts and waits. @return true if it finished AND succeeded.
    bool run(const CalibrationExtractor::Request& request)
    {
        start(request);
        return wait() && success;
    }

    bool                     cancelOnFirstReport = false; ///< Cancel at the first report that is not start()'s own.
    bool                     cancelAtOnce = false;        ///< Cancel at the very first report, start()'s own included.
    bool                     runningAtEnd = true;         ///< What running() said while the end was being reported.
    bool                     done    = false;
    bool                     success = false;
    std::string              summary;                     ///< The message that came with the finish.
    std::vector<std::string> errors;                      ///< Errors reported (a failure is reported before it finishes).
    std::vector<std::string> log;
    std::vector<int>         progress;
    std::vector<std::string> events;                      ///< "log", "progress" and "finished", in arrival order.

private:
    /// Cancels, once, at the first report that comes from a thread other than the one that called start(): that is
    /// while the extraction is reading the recording, which is the moment under test.
    void noteReport()
    {
        const bool wanted = cancelAtOnce || (cancelOnFirstReport && std::this_thread::get_id() != m_caller);
        if (wanted && !m_cancelled.exchange(true))
        {
            m_extractor.cancel();
        }
    }

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

    CalibrationExtractor&    m_extractor;
    std::thread::id          m_caller;
    std::atomic<bool>        m_cancelled{false};
    std::mutex               m_mutex;
    std::condition_variable  m_cv;
    std::string              m_gate_needle;
    bool                     m_parked = false;
    bool                     m_released = false;
};

/// Frees a parked thread however the test ends, so a failed check cannot leave the extractor's destructor waiting on a
/// thread that is waiting on the test. Declared AFTER the extractor, so it is destroyed (and releases) first.
struct ReleaseOnExit
{
    std::shared_ptr<Recorder> recorder;
    ~ReleaseOnExit() { recorder->release(); }
};

/// A sink that says nothing: for tests that only care about the extractor's own state.
class QuietEvents final : public ProcessingEvents
{
public:
    void logMessage(const std::string&) override {}
    void errorOccurred(const std::string&) override {}
    void processingFinished(bool) override {}
};

bool runExtraction(CalibrationExtractor& extractor, const CalibrationExtractor::Request& req, std::string& summary)
{
    auto recorder = std::make_shared<Recorder>(extractor);
    const bool ok = recorder->run(req);
    summary = recorder->summary;
    return ok;
}

/// The canonical rnrz-l_testfile Request: an 8000-bit RNRZ-L recording whose real stepped SNR sweep is present only on
/// RCVR3 = words 6/7/8; an empty receiverParamsToml means the extractor builds the 16x3 sequential word map.
void buildRealRequest(CalibrationExtractor::Request& req)
{
    const fs::path file = recording("agc_rnrz-l_trc_testfile.ch10");
    Chapter10Reader reader;
    REQUIRE_MESSAGE(reader.loadChannels(file), reader.lastError());
    const int pcm_id  = reader.getFirstPCMChannelID();
    const int time_id = reader.getCurrentTimeChannelID();
    REQUIRE(pcm_id >= 0);
    REQUIRE(time_id >= 0);

    std::vector<StepDefinition> steps;
    std::string step_error;
    REQUIRE_MESSAGE(StepDetector::parseStepConfig(repoFile(fs::path("settings") / "rcvr_cals" / "default.toml"), steps,
                                                  step_error),
                    step_error);
    REQUIRE_FALSE(steps.empty());

    req.calFilename   = file;
    req.timeChannelId = time_id;
    req.pcmChannelId  = pcm_id;
    // 8000 bits is this recording's true minor-frame length; RNRZ-L; 0.8 Mbps drives the adaptive extract period. An
    // ordinary Chapter 10 recording, so it needs the byte swap. Stated rather than inherited, because this flag must match
    // whatever the main run uses - that is the mirror invariant this suite exists to protect.
    setAcquisition(req, "FE6B2840", 8000, /*randomized*/ true, /*rate*/ 0.8, /*swap*/ true);
    req.numReceivers     = 16;
    req.receiverChannels = 3;
    req.steps            = steps;
}

/// A Request over step_cal, example.ch10 with the given step file and receiver-parameters word map (both relative to the
/// repository root).
void buildStepCalRequest(CalibrationExtractor::Request& req, const fs::path& step_toml, const fs::path& receiver_params)
{
    const fs::path file = recording("step_cal, example.ch10");
    Chapter10Reader reader;
    REQUIRE_MESSAGE(reader.loadChannels(file), reader.lastError());
    std::vector<StepDefinition> steps;
    std::string step_error;
    REQUIRE_MESSAGE(StepDetector::parseStepConfig(repoFile(step_toml), steps, step_error),
                    step_toml.string() << ": " << step_error);
    req.calFilename   = file;
    req.timeChannelId = reader.getCurrentTimeChannelID();
    req.pcmChannelId  = reader.getFirstPCMChannelID();
    setAcquisition(req, "FE6B2840", 800, /*randomized*/ false, /*rate*/ 0.8, /*swap*/ true);
    req.receiverParamsToml = repoFile(receiver_params);
    req.numReceivers       = 16;
    req.receiverChannels   = 3;
    req.steps              = steps;
}

/// step_cal with the shipped 11-step file and the shipped sequential word map.
CalibrationExtractor::Request stepCalRequest()
{
    CalibrationExtractor::Request req;
    buildStepCalRequest(req, fs::path("settings") / "rcvr_cals" / "default.toml",
                        fs::path("settings") / "receiver_params" / "default.toml");
    return req;
}

// What an extraction of step_cal with the shipped 11-step file and word map comes to, channel by channel, recorded from the
// extractor when this suite was ported: "name outcome plateaus points edge-threshold sum-of-profile-raw-counts". The two
// runs differ only in their clips (none, then 6 s at the start and 2 s at the end), so an extraction that ignored a clip, or
// measured with the wrong window, would read differently. Every number is the extractor's own.
const char* const kGoldenUnclipped[] = {
    "L_RCVR1 0 9 9 39.9 265191.6",
    "R_RCVR1 0 11 9 28.5 255941.8",
    "C_RCVR1 0 11 9 39.9 262884.4",
    "L_RCVR2 2 1 0 2.0 0.0",
    "R_RCVR2 2 1 0 2.0 0.0",
    "C_RCVR2 2 1 0 2.0 0.0",
    "L_RCVR3 0 12 11 34.2 186225.0",
    "R_RCVR3 0 13 11 28.5 192699.6",
    "C_RCVR3 0 12 11 45.5 195182.0",
    "L_RCVR4 2 1 0 2.0 0.0",
    "R_RCVR4 2 1 0 28.5 0.0",
    "C_RCVR4 2 1 0 28.5 0.0",
    "L_RCVR5 0 12 11 39.9 185895.2",
    "R_RCVR5 0 13 11 28.5 180245.7",
    "C_RCVR5 0 12 11 39.9 195361.4",
    "L_RCVR6 0 15 11 28.5 212732.4",
    "R_RCVR6 0 12 11 39.9 208217.3",
    "C_RCVR6 0 12 11 34.2 217435.8",
    "L_RCVR7 2 1 0 2.0 0.0",
    "R_RCVR7 2 1 0 2.0 0.0",
    "C_RCVR7 2 1 0 2.0 0.0",
    "L_RCVR8 2 1 0 2.0 0.0",
    "R_RCVR8 2 1 0 5.7 0.0",
    "C_RCVR8 2 1 0 2.0 0.0",
    "L_RCVR9 2 1 0 330.2 0.0",
    "R_RCVR9 2 1 0 358.7 0.0",
    "C_RCVR9 2 1 0 358.7 0.0",
    "L_RCVR10 2 1 0 592.1 0.0",
    "R_RCVR10 2 1 0 666.1 0.0",
    "C_RCVR10 2 1 0 831.2 0.0",
    "L_RCVR11 4 1 0 1947.1 0.0",
    "R_RCVR11 3 2 0 2226.0 0.0",
    "C_RCVR11 3 1 0 2055.2 0.0",
    "L_RCVR12 3 1 0 1286.7 0.0",
    "R_RCVR12 3 2 0 1019.1 0.0",
    "C_RCVR12 3 2 0 1132.9 0.0",
    "L_RCVR13 2 1 0 2.0 0.0",
    "R_RCVR13 2 1 0 2.0 0.0",
    "C_RCVR13 3 6 0 239.1 0.0",
    "L_RCVR14 3 3 0 854.0 0.0",
    "R_RCVR14 4 13 0 279.0 0.0",
    "C_RCVR14 3 3 0 723.0 0.0",
    "L_RCVR15 2 1 0 2.0 0.0",
    "R_RCVR15 2 1 0 11.4 0.0",
    "C_RCVR15 2 1 0 2.0 0.0",
    "L_RCVR16 2 1 0 5.7 0.0",
    "R_RCVR16 2 1 0 2.0 0.0",
    "C_RCVR16 2 1 0 5.7 0.0",
};

const char* const kGoldenClipped[] = {
    "L_RCVR1 0 9 9 39.9 265191.6",
    "R_RCVR1 0 11 9 28.5 255941.8",
    "C_RCVR1 0 11 9 39.9 262884.4",
    "L_RCVR2 2 1 0 2.0 0.0",
    "R_RCVR2 2 1 0 2.0 0.0",
    "C_RCVR2 2 1 0 2.0 0.0",
    "L_RCVR3 0 12 11 34.2 186225.0",
    "R_RCVR3 0 13 11 28.5 192699.6",
    "C_RCVR3 0 12 11 39.9 195182.0",
    "L_RCVR4 2 1 0 2.0 0.0",
    "R_RCVR4 2 1 0 28.5 0.0",
    "C_RCVR4 2 1 0 28.5 0.0",
    "L_RCVR5 0 12 11 39.9 185895.2",
    "R_RCVR5 0 13 11 34.2 180245.7",
    "C_RCVR5 0 12 11 39.9 195361.4",
    "L_RCVR6 0 14 11 34.2 212728.8",
    "R_RCVR6 0 12 11 39.9 208217.3",
    "C_RCVR6 0 12 11 34.2 217435.8",
    "L_RCVR7 2 1 0 2.0 0.0",
    "R_RCVR7 2 1 0 2.0 0.0",
    "C_RCVR7 2 1 0 2.0 0.0",
    "L_RCVR8 2 1 0 2.0 0.0",
    "R_RCVR8 2 1 0 5.7 0.0",
    "C_RCVR8 2 1 0 2.0 0.0",
    "L_RCVR9 2 1 0 307.4 0.0",
    "R_RCVR9 2 1 0 341.6 0.0",
    "C_RCVR9 2 1 0 341.6 0.0",
    "L_RCVR10 2 1 0 597.8 0.0",
    "R_RCVR10 2 1 0 654.7 0.0",
    "C_RCVR10 2 1 0 819.8 0.0",
    "L_RCVR11 4 1 0 1918.6 0.0",
    "R_RCVR11 3 2 0 2300.0 0.0",
    "C_RCVR11 3 1 0 2146.3 0.0",
    "L_RCVR12 3 1 0 1315.1 0.0",
    "R_RCVR12 3 2 0 1115.9 0.0",
    "C_RCVR12 3 2 0 1207.0 0.0",
    "L_RCVR13 2 1 0 2.0 0.0",
    "R_RCVR13 2 1 0 2.0 0.0",
    "C_RCVR13 3 6 0 227.7 0.0",
    "L_RCVR14 3 3 0 854.0 0.0",
    "R_RCVR14 4 10 0 335.9 0.0",
    "C_RCVR14 3 3 0 734.4 0.0",
    "L_RCVR15 2 1 0 2.0 0.0",
    "R_RCVR15 2 1 0 11.4 0.0",
    "C_RCVR15 2 1 0 2.0 0.0",
    "L_RCVR16 2 1 0 5.7 0.0",
    "R_RCVR16 2 1 0 2.0 0.0",
    "C_RCVR16 2 1 0 5.7 0.0",
};


/// @p value to @p decimals places, the way the log prints it.
std::string fixed(double value, int decimals)
{
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", decimals, value);
    return text;
}

int calibratedCount(const CalibrationExtractor& e)
{
    int n = 0;
    for (const CalibrationChannelResult& r : e.results())
    {
        n += r.profile.valid ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("CalibrationExtractor: wait returns once the driver has ended")
{
    // An extractor that was never started has nothing to wait for.
    {
        CalibrationExtractor idle;
        idle.wait();
        CHECK_FALSE(idle.running());
    }

    // One that failed before any thread started has none either.
    {
        CalibrationExtractor::Request req;
        req.calFilename   = "definitely_not_a_real_file.ch10";
        req.timeChannelId = 1;
        req.pcmChannelId  = 1;
        setAcquisition(req, "FE6B2840", 800, false, 0.8, true);
        req.steps = dummySteps();
        CalibrationExtractor failed;
        failed.start(req, std::make_shared<QuietEvents>());
        failed.wait();
        CHECK_FALSE(failed.running());
        CHECK_FALSE(failed.error().empty());
    }

    // And a real run: when wait() returns the driver is over and the results are all there.
    const CalibrationExtractor::Request req = stepCalRequest();
    CalibrationExtractor extractor;
    extractor.start(req, std::make_shared<QuietEvents>());
    extractor.wait();
    CHECK_FALSE(extractor.running());
    CHECK(static_cast<int>(extractor.results().size()) == req.numReceivers * req.receiverChannels);
    extractor.wait(); // and again is harmless

    // A cancelled one is waited for the same way.
    CalibrationExtractor cancelled;
    cancelled.start(req, std::make_shared<QuietEvents>());
    cancelled.cancel();
    cancelled.wait();
    CHECK_FALSE(cancelled.running());
}

TEST_CASE("CalibrationExtractor: a missing file fails cleanly")
{
    CalibrationExtractor::Request req;
    req.calFilename   = "definitely_not_a_real_file.ch10";
    req.timeChannelId = 1;
    req.pcmChannelId  = 1;
    setAcquisition(req, "FE6B2840", 8000, /*randomized*/ false, /*rate*/ 0.0, /*swap*/ true);
    req.numReceivers     = 16;
    req.receiverChannels = 3;
    req.steps            = dummySteps(); // clear the empty-steps guard

    CalibrationExtractor extractor;
    std::string summary;
    const bool ok = runExtraction(extractor, req, summary);

    // The reader can't open the file, so the run finishes unsuccessfully with a recorded error and no per-channel
    // results - no hang, no partial state.
    CHECK_FALSE(ok);
    CHECK_FALSE(extractor.error().empty());
    CHECK(extractor.results().empty());
}

TEST_CASE("CalibrationExtractor: the real file calibrates receiver 3 only")
{
    CalibrationExtractor::Request req;
    buildRealRequest(req);

    CalibrationExtractor extractor;
    std::string summary;
    REQUIRE_MESSAGE(runExtraction(extractor, req, summary), summary);

    const std::vector<CalibrationChannelResult>& results = extractor.results();

    // One result per word-map channel (16 receivers x 3 channels).
    CHECK(static_cast<int>(results.size()) == req.numReceivers * req.receiverChannels);

    // The real stepped SNR sweep is present only on RCVR3 = words 6/7/8 (L/R/C); every other receiver word is low-level
    // noise and MUST fall back to linear (the detector rejects flat/inactive channels). So exactly those three words get
    // a valid non-linear profile.
    std::set<int> calibrated_words;
    for (const CalibrationChannelResult& r : results)
    {
        if (r.profile.valid)
        {
            calibrated_words.insert(r.word);
        }
    }
    CHECK(calibrated_words == std::set<int>{6, 7, 8});

    // With no word-map file the extractor builds the sequential grid from the counts: receivers in order, three channels
    // each, named L, R, C. (Swap the two counts and the same 48 words are named differently.)
    for (const CalibrationChannelResult& r : results)
    {
        if (r.word == 6)  CHECK(r.name == "L_RCVR3");
        if (r.word == 7)  CHECK(r.name == "R_RCVR3");
        if (r.word == 8)  CHECK(r.name == "C_RCVR3");
    }
}

TEST_CASE("CalibrationExtractor: the byte order reaches the extraction")
{
    // The extractor must mirror every raw-affecting flag of the main run - the project invariant that already cost one bug
    // when isInverted was missing. When byte order became settable, the extractor kept using the ProcessingParams default
    // instead of the operator's choice, so extracting for a stream whose byte order differed from that default would build
    // a profile from a different bitstream than the run it was meant to calibrate. Nothing would have reported it: the
    // profile attaches and the numbers are simply wrong.
    //
    // Asserted behaviourally rather than by inspecting the params: with the wrong byte order the sync pattern is destroyed
    // and extraction cannot succeed, which is only observable if the flag actually reaches the scan.
    CalibrationExtractor::Request req;
    buildRealRequest(req);

    // buildRealRequest sets the byte order this recording needs; flipping it is the only difference between the two runs.
    req.acquisition.swapBytes = false;
    CalibrationExtractor extractor;
    std::string summary;
    CHECK_MESSAGE(!runExtraction(extractor, req, summary), "the wrong byte order must not produce a calibration");

    req.acquisition.swapBytes = true;
    CalibrationExtractor extractor_ok;
    std::string summary_ok;
    CHECK_MESSAGE(runExtraction(extractor_ok, req, summary_ok), summary_ok);
}

// ---------------------------------------------------------------------------------------------------------------------
// The report, which is pure
// ---------------------------------------------------------------------------------------------------------------------

namespace {

/// Result sets for the exact-report test, by scenario name. @p expected_steps is the step file's length.
std::vector<CalibrationChannelResult> reportScenario(const std::string& scenario, int& expected_steps)
{
    using O = CalibrationOutcome;
    std::vector<CalibrationChannelResult> v;
    expected_steps = 6;

    if (scenario == "empty")
    {
        return v;
    }
    if (scenario == "one-receiver")
    {
        for (const std::string& p : kLcr)
        {
            v.push_back(channelResult(p + "_RCVR3", O::Calibrated, 6));
        }
    }
    else if (scenario == "mixed")
    {
        for (const std::string& p : kLcr)
        {
            v.push_back(channelResult(p + "_RCVR1", O::Calibrated, 6));
        }
        v.push_back(channelResult("L_RCVR2", O::Calibrated, 6));
        v.push_back(channelResult("R_RCVR2", O::Calibrated, 6));
        v.push_back(channelResult("C_RCVR2", O::TooFewPlateaus, 4));
        for (const std::string& p : kLcr)
        {
            v.push_back(channelResult(p + "_RCVR3", O::Calibrated, 6));
        }
        for (const std::string& p : kLcr)
        {
            v.push_back(channelResult(p + "_RCVR4", O::NoData, 0, false));
        }
    }
    else if (scenario == "unnamed")
    {
        v.push_back(channelResult("AGC_LEFT", O::NoMonotonicSweep, 7));
        v.push_back(channelResult("AGC_RIGHT", O::Calibrated, 6));
    }
    else if (scenario == "unnamed-all-calibrated")
    {
        v.push_back(channelResult("AGC_LEFT", O::Calibrated, 6));
        v.push_back(channelResult("AGC_RIGHT", O::Calibrated, 6));
    }
    else if (scenario == "unnamed-and-numbered")
    {
        for (const std::string& p : kLcr)
        {
            v.push_back(channelResult(p + "_RCVR3", O::Calibrated, 6));
        }
        v.push_back(channelResult("AGC_LEFT", O::Calibrated, 6));
        v.push_back(channelResult("AGC_RIGHT", O::Calibrated, 6));
    }
    else if (scenario == "unnamed-calibrated-beside-a-failure")
    {
        for (const std::string& p : kLcr)
        {
            v.push_back(channelResult(p + "_RCVR4", O::NoData, 0, false));
        }
        v.push_back(channelResult("AGC_LEFT", O::Calibrated, 6));
        v.push_back(channelResult("AGC_RIGHT", O::Calibrated, 6));
    }
    else if (scenario == "unnamed-calibrated-saturated")
    {
        v.push_back(railedResult("AGC_LEFT", 37.5, {42.5}));
        v.push_back(channelResult("AGC_RIGHT", O::Calibrated, 6));
    }
    else if (scenario == "unnamed-non-ascii")
    {
        v.push_back(channelResult(utf8(u8"Récepteur Ω"), O::Calibrated, 6));
        v.push_back(channelResult(utf8(u8"Gain Σ"), O::TooFewPlateaus, 2));
    }
    else if (scenario == "checks")
    {
        expected_steps = 8;
        for (const std::string& p : kLcr)
        {
            CalibrationChannelResult r = channelResult(p + "_RCVR3", O::Calibrated, 8);
            r.sweepLevels = 11;
            v.push_back(r);
        }
        v.push_back(flatResult("L_RCVR2", 0.0));
        v.push_back(flatResult("R_RCVR2", 0.0));
        CalibrationChannelResult swept = channelResult("C_RCVR2", O::Calibrated, 8);
        swept.sweepLevels = 8;
        v.push_back(swept);
    }
    else if (scenario == "saturated-ties")
    {
        // Top dB 37.5 and unresolved dB 42.5 / 2.5 sit exactly halfway between two integers, which is where Qt's 'f'
        // formatting (away from zero) and a half-to-even formatter disagree.
        v.push_back(railedResult("L_RCVR1", 37.5, {42.5, 2.5}));
        v.push_back(railedResult("R_RCVR1", 37.5, {42.5, 60.0}));
        v.push_back(channelResult("C_RCVR1", O::Calibrated, 6));
        v.push_back(railedResult("L_RCVR5", 0.5, {}));
    }
    else if (scenario == "relaxed")
    {
        for (const std::string& p : kLcr)
        {
            CalibrationChannelResult r = channelResult(p + "_RCVR2", O::Calibrated, 6);
            r.thresholdRelaxed = (p != "R");
            v.push_back(r);
        }
    }
    else if (scenario == "flat-levels")
    {
        // Held levels are printed to the nearest count; 0.5 and 1234.5 are exact ties.
        v.push_back(flatResult("L_RCVR4", 0.5));
        v.push_back(flatResult("R_RCVR4", 65472.0));
        v.push_back(flatResult("C_RCVR4", 0.5));
        v.push_back(flatResult("L_RCVR6", 1234.5));
        v.push_back(channelResult("R_RCVR6", O::TooFewPlateaus, 3));
    }
    else if (scenario == "several-reasons")
    {
        v.push_back(channelResult("L_RCVR7", O::NoMonotonicSweep, 9));
        v.push_back(channelResult("R_RCVR7", O::NoData, 0, false));
        v.push_back(channelResult("C_RCVR7", O::TooFewPlateaus, 2));
        v.push_back(channelResult("L_RCVR8", O::TooFewPlateaus, 5));
        v.push_back(channelResult("R_RCVR8", O::TooFewPlateaus, 3));
    }
    else if (scenario == "twelve-receivers")
    {
        for (int rcvr = 1; rcvr <= 12; rcvr++)
        {
            v.push_back(channelResult("L_RCVR" + std::to_string(rcvr), O::Calibrated, 6));
        }
    }
    else if (scenario == "step-file-levels")
    {
        // Receiver 3: one level over the step file is the ordinary lead-in (no warning from that channel), and the warning
        // names the MOST levels any of its channels held. Receiver 4: exactly two over, which is where the warning starts.
        expected_steps = 8;
        const int levels3[] = {9, 12, 10};
        for (std::size_t i = 0; i < 3; i++)
        {
            CalibrationChannelResult r = channelResult(kLcr[i] + "_RCVR3", O::Calibrated, 8);
            r.sweepLevels = levels3[i];
            v.push_back(r);
        }
        for (const std::string& p : kLcr)
        {
            CalibrationChannelResult r = channelResult(p + "_RCVR4", O::Calibrated, 8);
            r.sweepLevels = 10;
            v.push_back(r);
        }
    }
    else if (scenario == "saturated-tops")
    {
        // Two railed channels of one receiver: the report holds at the HIGHER top, and lists every step either could not
        // measure, once each, in the order met.
        v.push_back(railedResult("L_RCVR2", 48.0, {54.0, 60.0}));
        v.push_back(railedResult("R_RCVR2", 37.5, {42.5}));
    }
    else if (scenario == "saturated-inverted")
    {
        // An inverted receiver's raw count falls as the signal climbs, so its profile (sorted by raw count) has its highest
        // dB at the FIRST point, not the last.
        CalibrationChannelResult r = railedResult("L_RCVR3", 37.5, {42.5});
        std::reverse(r.profile.points.begin(), r.profile.points.end());
        v.push_back(r);
    }
    else if (scenario == "flat-without-levels")
    {
        // A flat channel that reports no level says only that it is flat.
        v.push_back(channelResult("L_RCVR5", O::FlatNoSignal, 1));
        v.push_back(channelResult("R_RCVR5", O::Calibrated, 6));
    }
    else if (scenario == "unnamed-flat-beside-calibrated")
    {
        // The word-map hint is about numbered receivers: unnamed channels are one group by construction, so one swept and
        // one flat is nothing to compare.
        v.push_back(channelResult("AGC_A", O::Calibrated, 6));
        v.push_back(flatResult("AGC_B", 7.0));
    }
    else if (scenario == "too-few-zero-plateaus")
    {
        v.push_back(channelResult("L_RCVR6", O::TooFewPlateaus, 0));
    }
    else if (scenario == "too-few-without-data")
    {
        // No channel of the receiver had data, so there is no count to put against the steps.
        v.push_back(channelResult("L_RCVR7", O::TooFewPlateaus, 3, false));
    }
    return v;
}

} // namespace

TEST_CASE("CalibrationExtractor: summarize produces exactly this report")
{
    // The whole text, not fragments: the report is what the operator reads, and the other summary tests only look for
    // phrases inside it. Includes the exact halves (37.5, 42.5, 2.5, 0.5, 1234.5) that Qt rounds away from zero, which a
    // half-to-even formatter would print differently.
    struct Row
    {
        const char*              scenario;
        std::vector<std::string> lines;
    };
    const Row rows[] = {
        {"empty", {"No receiver channels were extracted."}},
        {"one-receiver",
         {"Applied non-linear calibration to 3 of 3 channel(s).", "", "Calibrated: receiver 3."}},
        {"mixed",
         {"Applied non-linear calibration to 8 of 12 channel(s).",
          "",
          "Calibrated: receivers 1, 3.",
          "",
          "Fell back to linear calibration:",
          "  Receiver 2 — L, R calibrated; C: too few plateaus (4 of 6 steps resolved)",
          "  Receiver 4 — L, R, C: no data"}},
        {"unnamed",
         {"Applied non-linear calibration to 1 of 2 channel(s).",
          "",
          "Fell back to linear calibration:",
          "  Unnamed receiver — AGC_RIGHT calibrated; AGC_LEFT: no monotonic sweep"}},
        // Channels with no receiver number are one group. When every one of them calibrated, they are listed with the
        // calibrated receivers, by name - not under "Fell back to linear calibration:", which they did not. (A group
        // where some channels did fall back stays below, as above.)
        {"unnamed-all-calibrated",
         {"Applied non-linear calibration to 2 of 2 channel(s).",
          "",
          "Calibrated (unnamed channels): AGC_LEFT, AGC_RIGHT."}},
        {"unnamed-and-numbered",
         {"Applied non-linear calibration to 5 of 5 channel(s).",
          "",
          "Calibrated: receiver 3.",
          "Calibrated (unnamed channels): AGC_LEFT, AGC_RIGHT."}},
        {"unnamed-calibrated-beside-a-failure",
         {"Applied non-linear calibration to 2 of 5 channel(s).",
          "",
          "Calibrated (unnamed channels): AGC_LEFT, AGC_RIGHT.",
          "",
          "Fell back to linear calibration:",
          "  Receiver 4 — L, R, C: no data"}},
        {"unnamed-calibrated-saturated",
         {"Applied non-linear calibration to 2 of 2 channel(s).",
          "",
          "Calibrated (unnamed channels): AGC_LEFT, AGC_RIGHT.",
          "",
          "Calibrated only up to a saturated top — these receivers ran out of range partway through the sweep, so "
          "their readings hold at the highest step still measurable:",
          "  Unnamed receiver — AGC_LEFT: readings hold at 38 dB (no measurement for 43 dB)"}},
        {"unnamed-non-ascii",
         {"Applied non-linear calibration to 1 of 2 channel(s).",
          "",
          "Fell back to linear calibration:",
          utf8(u8"  Unnamed receiver — Récepteur Ω calibrated; Gain Σ: too few plateaus (2 of 6 steps "
                 "resolved)")}},
        {"checks",
         {"Applied non-linear calibration to 4 of 6 channel(s).",
          "",
          "Calibrated: receiver 3.",
          "",
          "Check the step file — these receivers stepped through more levels than it lists, so the lowest were set "
          "aside as lead-in. If those are real steps, every step is paired with the wrong level:",
          "  Receiver 3 — 11 levels in the recording, 8 in the step file",
          "",
          "Check the word map — in these receivers one channel swept and another never moved. A word map that does not "
          "match the recording files one receiver's words under another's name; otherwise a channel may be dead:",
          "  Receiver 2 — C calibrated; L, R never moved",
          "",
          "Fell back to linear calibration:",
          "  Receiver 2 — C calibrated; L, R: flat / no signal (held at 0)"}},
        {"saturated-ties",
         {"Applied non-linear calibration to 4 of 4 channel(s).",
          "",
          "Calibrated: receivers 1, 5.",
          "",
          "Calibrated only up to a saturated top — these receivers ran out of range partway through the sweep, so "
          "their readings hold at the highest step still measurable:",
          "  Receiver 1 — L, R: readings hold at 38 dB (no measurement for 43, 3, 60 dB)",
          "  Receiver 5 — L: readings hold at 1 dB"}},
        {"relaxed",
         {"Applied non-linear calibration to 3 of 3 channel(s).",
          "",
          "Calibrated: receiver 2.",
          "",
          "Calibrated only with a relaxed threshold — steps are close to the noise floor, so these receivers are worth "
          "checking:",
          "  Receiver 2 — L, C"}},
        {"flat-levels",
         {"Applied non-linear calibration to 0 of 5 channel(s).",
          "",
          "Fell back to linear calibration:",
          "  Receiver 4 — L, R, C: flat / no signal (held at 1, 65472)",
          "  Receiver 6 — L: flat / no signal (held at 1235); R: too few plateaus (1 of 6 steps resolved)"}},
        {"several-reasons",
         {"Applied non-linear calibration to 0 of 5 channel(s).",
          "",
          "Fell back to linear calibration:",
          "  Receiver 7 — R: no data; C: too few plateaus (2 of 6 steps resolved); L: no monotonic sweep",
          "  Receiver 8 — L, R: too few plateaus (3 of 6 steps resolved)"}},
        {"twelve-receivers",
         {"Applied non-linear calibration to 12 of 12 channel(s).",
          "",
          "Calibrated: receivers 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12."}},
        {"step-file-levels",
         {"Applied non-linear calibration to 6 of 6 channel(s).",
          "",
          "Calibrated: receivers 3, 4.",
          "",
          "Check the step file — these receivers stepped through more levels than it lists, so the lowest were set "
          "aside as lead-in. If those are real steps, every step is paired with the wrong level:",
          "  Receiver 3 — 12 levels in the recording, 8 in the step file",
          "  Receiver 4 — 10 levels in the recording, 8 in the step file"}},
        {"saturated-tops",
         {"Applied non-linear calibration to 2 of 2 channel(s).",
          "",
          "Calibrated: receiver 2.",
          "",
          "Calibrated only up to a saturated top — these receivers ran out of range partway through the sweep, so "
          "their readings hold at the highest step still measurable:",
          "  Receiver 2 — L, R: readings hold at 48 dB (no measurement for 54, 60, 43 dB)"}},
        {"saturated-inverted",
         {"Applied non-linear calibration to 1 of 1 channel(s).",
          "",
          "Calibrated: receiver 3.",
          "",
          "Calibrated only up to a saturated top — these receivers ran out of range partway through the sweep, so "
          "their readings hold at the highest step still measurable:",
          "  Receiver 3 — L: readings hold at 38 dB (no measurement for 43 dB)"}},
        {"flat-without-levels",
         {"Applied non-linear calibration to 1 of 2 channel(s).",
          "",
          "Check the word map — in these receivers one channel swept and another never moved. A word map that does not "
          "match the recording files one receiver's words under another's name; otherwise a channel may be dead:",
          "  Receiver 5 — R calibrated; L never moved",
          "",
          "Fell back to linear calibration:",
          "  Receiver 5 — R calibrated; L: flat / no signal"}},
        {"unnamed-flat-beside-calibrated",
         {"Applied non-linear calibration to 1 of 2 channel(s).",
          "",
          "Fell back to linear calibration:",
          "  Unnamed receiver — AGC_A calibrated; AGC_B: flat / no signal (held at 7)"}},
        {"too-few-zero-plateaus",
         {"Applied non-linear calibration to 0 of 1 channel(s).",
          "",
          "Fell back to linear calibration:",
          "  Receiver 6 — L: too few plateaus (0 of 6 steps resolved)"}},
        {"too-few-without-data",
         {"Applied non-linear calibration to 0 of 1 channel(s).",
          "",
          "Fell back to linear calibration:",
          "  Receiver 7 — L: too few plateaus"}},
    };

    for (const Row& row : rows)
    {
        INFO("scenario " << row.scenario);
        int expected_steps = 0;
        const std::vector<CalibrationChannelResult> results = reportScenario(row.scenario, expected_steps);
        CHECK(CalibrationExtractor::summarize(results, expected_steps) == joinLines(row.lines));
    }
}

TEST_CASE("CalibrationExtractor: the summary names receivers and why they fell back")
{
    using O = CalibrationOutcome;
    std::vector<CalibrationChannelResult> results;
    // Receiver 1: fully calibrated. Receiver 2: partly - C fell back on merged steps. Receiver 3: fully calibrated.
    // Receiver 4: absent from the recording.
    for (const std::string& p : kLcr)
    {
        results.push_back(channelResult(p + "_RCVR1", O::Calibrated, 6));
    }
    results.push_back(channelResult("L_RCVR2", O::Calibrated, 6));
    results.push_back(channelResult("R_RCVR2", O::Calibrated, 6));
    results.push_back(channelResult("C_RCVR2", O::TooFewPlateaus, 4));
    for (const std::string& p : kLcr)
    {
        results.push_back(channelResult(p + "_RCVR3", O::Calibrated, 6));
    }
    for (const std::string& p : kLcr)
    {
        results.push_back(channelResult(p + "_RCVR4", O::NoData, 0, false));
    }

    const std::string summary = CalibrationExtractor::summarize(results, 6);

    // The headline count still reports channels, since that is the unit profiles are keyed by.
    // 3 (rcvr 1) + 2 (rcvr 2's L and R) + 3 (rcvr 3) = 8 of 12.
    CHECK_MESSAGE(contains(summary, "8 of 12 channel(s)"), summary);

    // Receivers whose every channel calibrated are named together, in one line - the point of the request. Receiver 2 is
    // NOT in that list: it was partial.
    CHECK_MESSAGE(contains(summary, "Calibrated: receivers 1, 3."), summary);

    // A partly calibrated receiver is spelled out rather than hidden in the count, naming both the channels that worked
    // and the one that did not, with a reason measured against the expected step count.
    CHECK_MESSAGE(contains(summary, "Receiver 2"), summary);
    CHECK_MESSAGE(contains(summary, "L, R calibrated"), summary);
    CHECK_MESSAGE(contains(summary, "C: too few plateaus (4 of 6 steps resolved)"), summary);

    // A receiver that was never in the recording reads as such, not as a detection failure - different problem,
    // different remedy.
    CHECK_MESSAGE(contains(summary, "Receiver 4"), summary);
    CHECK_MESSAGE(contains(summary, "no data"), summary);

    // Channels from a hand-written receiver-params TOML carry no "_RCVR<N>", so they must be reported by name rather
    // than assigned a receiver number.
    const std::vector<CalibrationChannelResult> unnamed = {channelResult("AGC_LEFT", O::NoMonotonicSweep, 7)};
    const std::string unnamed_summary = CalibrationExtractor::summarize(unnamed, 6);
    CHECK_MESSAGE(contains(unnamed_summary, "AGC_LEFT"), unnamed_summary);
    CHECK_MESSAGE(contains(unnamed_summary, "no monotonic sweep"), unnamed_summary);
}

TEST_CASE("CalibrationExtractor: the summary flags a short step file and a mismatched word map")
{
    using O = CalibrationOutcome;
    auto calibrated = [](const std::string& name, int sweep_levels) {
        CalibrationChannelResult r = channelResult(name, O::Calibrated);
        r.sweepLevels = sweep_levels;
        return r;
    };
    auto flat = [](const std::string& name) { return flatResult(name, 0.0); };

    // An 8-step file on an 11-level sweep: every channel reports calibrated and every step is paired with the wrong
    // level. The report has to say so, because nothing else will.
    std::vector<CalibrationChannelResult> short_file;
    for (const std::string& p : kLcr)
    {
        short_file.push_back(calibrated(p + "_RCVR3", 11));
    }
    const std::string short_report = CalibrationExtractor::summarize(short_file, 8);
    CHECK_MESSAGE(contains(short_report, "Check the step file"), short_report);
    CHECK_MESSAGE(contains(short_report, "Receiver 3 — 11 levels in the recording, 8 in the step file"), short_report);

    // One extra level is the ordinary turn-on transient the detector is built to drop; warning on it would fire on
    // healthy recordings and teach operators to ignore it.
    std::vector<CalibrationChannelResult> transient;
    for (const std::string& p : kLcr)
    {
        transient.push_back(calibrated(p + "_RCVR3", 9));
    }
    CHECK_FALSE(contains(CalibrationExtractor::summarize(transient, 8), "Check the step file"));

    // One channel swept and two never moved: what a word map that does not match the recording leaves behind, since it
    // files one receiver's words under another's.
    const std::vector<CalibrationChannelResult> mixed = {flat("L_RCVR2"), flat("R_RCVR2"), calibrated("C_RCVR2", 11)};
    const std::string mixed_report = CalibrationExtractor::summarize(mixed, 11);
    CHECK_MESSAGE(contains(mixed_report, "Check the word map"), mixed_report);
    CHECK_MESSAGE(contains(mixed_report, "Receiver 2 — C calibrated; L, R never moved"), mixed_report);

    // A receiver that is simply absent - every channel flat - is not a word-map hint; most receivers in a real
    // calibration recording look like that.
    std::vector<CalibrationChannelResult> absent;
    for (const std::string& p : kLcr)
    {
        absent.push_back(flat(p + "_RCVR4"));
    }
    CHECK_FALSE(contains(CalibrationExtractor::summarize(absent, 11), "Check the word map"));
}

// ---------------------------------------------------------------------------------------------------------------------
// Extraction on the step_cal recording
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("CalibrationExtractor: the real step_cal recording pairs every step correctly")
{
    // step_cal, example.ch10: a real step-calibration recording from receivers far out of alignment. Its sweep is 0 to
    // 60 dB in 6 dB steps - eleven levels, the first being the level each channel holds before the generator steps up -
    // so it is extracted with the 11-step settings/rcvr_cals/default.toml. Each dwell lasts ~4.9 s, and a channel's noise
    // grows with its level: ~3 counts on its low steps, ~60 on its high ones.
    //
    // Acquisition parameters (found by probing; nothing else locks): NRZ-L, byte-swapped, 800-bit minor frames at
    // 800 kbps on PCM channel 26. The shipped sequential word map (three words per receiver) is what the main run uses -
    // NOT receiver_params/RASA.toml, despite the 800-bit frame matching RASA's: RASA's stride-4 card layout treats words
    // 3, 7, 11, 15 ... as empty, which on this recording skips real channels and mis-attributes every receiver above 1 -
    // it reports live channels as dead with nothing pointing at the map as the cause.
    const CalibrationExtractor::Request req = stepCalRequest();
    const std::vector<StepDefinition>& steps = req.steps;
    REQUIRE(steps.size() == 11);

    CalibrationExtractor extractor;
    std::string summary;
    REQUIRE_MESSAGE(runExtraction(extractor, req, summary), summary);

    const std::vector<CalibrationChannelResult>& results = extractor.results();
    REQUIRE(static_cast<int>(results.size()) == req.numReceivers * req.receiverChannels);

    std::set<int> calibrated_receivers;
    int calibrated_channels = 0;
    for (const CalibrationChannelResult& r : results)
    {
        if (!r.profile.valid)
        {
            continue;
        }
        calibrated_receivers.insert(FrameSetup::receiverIndexFromName(r.name));
        calibrated_channels++;

        // The regression this recording exists for: every step pairs with its OWN dwell. Noise used to split one high
        // dwell into two plateaus a few tens of counts apart. The split took a pairing slot, the real 0 dB level was
        // dropped as "pre-roll", and every step below the split read 6 dB low - L/R_RCVR3 and R_RCVR6 by one step,
        // L_RCVR6 (split twice) by two. The profile still attached, so nothing reported it: the staircase simply sat a
        // step behind its neighbours, and noise across the split swung 6 dB.
        const std::vector<CalibrationPoint>& pts = r.profile.points;
        REQUIRE_FALSE(pts.empty());
        CHECK_MESSAGE((pts.front().rawAvg < 1000.0 && pts.front().trueDb == 0.0),
                      r.name << ": 0 dB paired with raw " << pts.front().rawAvg << ", not the pre-sweep level");
        for (std::size_t k = 1; k < pts.size(); k++)
        {
            // Genuine steps here are more than 1000 counts apart (the smallest is ~1200, on receiver 5); a split dwell
            // is tens.
            CHECK_MESSAGE(pts[k].rawAvg - pts[k - 1].rawAvg > 1000.0,
                          r.name << ": " << pts[k - 1].trueDb << " dB and " << pts[k].trueDb << " dB are "
                                 << pts[k].rawAvg - pts[k - 1].rawAvg << " counts apart - one dwell split in two");
        }

        if (r.saturated)
        {
            // Receiver 1 alone: its gain is set too high for this sweep, so its top two steps both sit on the 65472 rail
            // and carry no measurement. It is calibrated over the range it could still resolve, and readings hold at the
            // top of that range instead of being extrapolated past it.
            CHECK(FrameSetup::receiverIndexFromName(r.name) == 1);
            CHECK(pts.back().trueDb == 48.0);
            CHECK_MESSAGE((std::count(r.unresolvedDb.begin(), r.unresolvedDb.end(), 54.0) > 0
                           && std::count(r.unresolvedDb.begin(), r.unresolvedDb.end(), 60.0) > 0),
                          r.name);
            CHECK(interpolateCalibration(65472.0, r.profile) == 48.0);
            // Every step from 0 to 48 dB is measured - including L_RCVR1's 24 dB dwell, whose plateau noise shatters.
            // Left as a hole it was bridged linearly from 18 to 30 dB and read 23.09 dB; the dwell grid locates it so it
            // is measured instead.
            CHECK(pts.size() == steps.size() - 2);
            CHECK(r.unresolvedDb == std::vector<double>({54.0, 60.0}));
        }
        else
        {
            CHECK(pts.size() == steps.size());
            CHECK(pts.back().trueDb == 60.0);
        }
    }

    // All three channels of receivers 1, 3, 5 and 6 carry the sweep - the operator's own count - and no channel that
    // carries none gets a false profile.
    CHECK(calibrated_channels == 12);
    CHECK(calibrated_receivers == std::set<int>{1, 3, 5, 6});

    // A receiver that carries no sweep says so plainly, not as a detection shortfall: receiver 2 holds 0 for the whole
    // recording.
    for (const CalibrationChannelResult& r : results)
    {
        if (FrameSetup::receiverIndexFromName(r.name) == 2)
        {
            CHECK(r.outcome == CalibrationOutcome::FlatNoSignal);
        }
    }

    // And the operator-facing report names the receivers, in the unit operators actually talk in, and says where a
    // saturated one stops being trustworthy.
    const std::string report = CalibrationExtractor::summarize(results, intSize(steps));
    CHECK_MESSAGE(contains(report, "Calibrated: receivers 1, 3, 5, 6."), report);
    CHECK_MESSAGE((contains(report, "Receiver 1") && contains(report, "hold at 48 dB")), report);

    // And neither "check this" prompt fires on a correct setup. They exist to catch a wrong step file or word map; a
    // false alarm here would teach operators to ignore them. (Receiver 1's saturated sweep holds FEWER levels than the
    // file, not more.)
    CHECK_FALSE(contains(report, "Check the step file"));
    CHECK_FALSE(contains(report, "Check the word map"));
}

TEST_CASE("CalibrationExtractor: the extraction logs one line per channel with data")
{
    // The log the dialog shows while it works: one line per channel that had samples, naming the channel and its word,
    // and saying how many plateaus were found against the steps expected.
    const CalibrationExtractor::Request req = stepCalRequest();
    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    REQUIRE_MESSAGE(recorder->run(req), recorder->summary);

    int with_data = 0;
    for (const CalibrationChannelResult& r : extractor.results())
    {
        if (!r.hadData)
        {
            continue;
        }
        with_data++;
        const std::string prefix = "  " + r.name + " (word " + std::to_string(r.word) + "): ";
        int lines = 0;
        for (const std::string& line : recorder->log)
        {
            if (line.starts_with(prefix))
            {
                lines++;
                CHECK_MESSAGE(contains(line, std::to_string(req.steps.size()) + " step(s) expected"), line);
                CHECK_MESSAGE(contains(line, std::to_string(r.detectedPlateaus) + " plateau(s) detected"), line);

                // The marker says so when the plateaus found are not the steps expected, and only then.
                const bool mismatch = r.detectedPlateaus != intSize(req.steps);
                CHECK_MESSAGE(contains(line, " — MISMATCH") == mismatch, line);

                // And the note that goes with the outcome: why a channel fell back, how far a railed one is calibrated, or
                // nothing at all for an ordinary success.
                if (!r.profile.valid)
                {
                    CHECK_MESSAGE(contains(line, std::string(" — ") + calibrationOutcomeText(r.outcome) + ", edge threshold " +
                                                     fixed(r.edgeThreshold, 1) + " raw counts"),
                                  line);
                }
                else if (r.saturated)
                {
                    double top = 0.0;
                    for (const CalibrationPoint& p : r.profile.points)
                    {
                        top = std::max(top, p.trueDb);
                    }
                    CHECK_MESSAGE(contains(line, " — top step(s) railed; calibrated " + std::to_string(r.profile.points.size()) +
                                                     " of " + std::to_string(req.steps.size()) +
                                                     " step(s), readings hold at " + fixed(top, 0) + " dB"),
                                  line);
                }
                else
                {
                    CHECK_MESSAGE(!contains(line, "railed"), line);
                    CHECK_MESSAGE(!contains(line, "relaxed"), line);
                    CHECK_MESSAGE(!contains(line, "edge threshold"), line);
                }
            }
        }
        CHECK_MESSAGE(lines == 1, r.name << ": " << lines << " log lines");
    }
    CHECK(with_data > 0);

    // The worker's own lines are tagged with the channel and the stream's label, and so are the reader's.
    const std::string tag = "[CH " + std::to_string(req.pcmChannelId) + " Calibration] ";
    CHECK_MESSAGE(countContaining(recorder->log, tag + "processing...") == 1, joinLines(recorder->log));
    CHECK_MESSAGE(countContaining(recorder->log, "Reading Chapter 10 file") == 1, joinLines(recorder->log));
}

TEST_CASE("CalibrationExtractor: a non-ASCII channel name survives the extraction, its log and its report")
{
    // A hand-written receiver-parameters TOML may name its channels anything. Here three live words of step_cal are named
    // with an accented letter, a Greek capital sigma and an omega, so the name goes through the word map, the per-channel
    // log line and the report.
    CalibrationExtractor::Request req = stepCalRequest();

    const std::vector<std::string> names = {utf8(u8"Récepteur Ω"), utf8(u8"Gain Σ"), "Plain"};
    TempDir dir;
    std::string toml;
    for (std::size_t i = 0; i < names.size(); i++)
    {
        toml += "[" + names[i] + "]\nWord = " + std::to_string(i + 1) + "\n";
    }
    writeBytes(dir.file("accent.toml"), toml);
    req.receiverParamsToml = dir.file("accent.toml");

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    REQUIRE_MESSAGE(recorder->run(req), recorder->summary);

    const std::vector<CalibrationChannelResult>& results = extractor.results();
    REQUIRE(results.size() == 3);
    for (std::size_t i = 0; i < 3; i++)
    {
        INFO("channel " << i);
        CHECK(results[i].name == names[i]);
        CHECK(results[i].hadData);
        const std::string prefix = "  " + names[i] + " (word " + std::to_string(results[i].word) + "): ";
        int lines = 0;
        for (const std::string& line : recorder->log)
        {
            lines += line.starts_with(prefix) ? 1 : 0;
        }
        CHECK_MESSAGE(lines == 1, names[i] << ": " << lines << " log lines");
    }

    // None carries "_RCVR<N>", so the report names them rather than numbering a receiver.
    const std::string report = CalibrationExtractor::summarize(results, intSize(req.steps));
    for (const std::string& name : names)
    {
        CHECK_MESSAGE(contains(report, name), report);
    }
    CHECK_MESSAGE(contains(report, "Unnamed receiver"), report);
    // All three calibrated, so they are listed as calibrated, not as having fallen back.
    CHECK_MESSAGE(contains(report, "Calibrated (unnamed channels): "), report);
    CHECK_MESSAGE(!contains(report, "Fell back to linear calibration"), report);
}

// ---------------------------------------------------------------------------------------------------------------------
// The lifecycle: what is reported, in what order, and how it can be stopped
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("CalibrationExtractor: an extraction with no steps fails before it starts")
{
    CalibrationExtractor::Request req = stepCalRequest();
    req.steps.clear();

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    CHECK_FALSE(recorder->run(req));
    CHECK(recorder->done);
    CHECK(recorder->summary == "No calibration steps were provided.");
    CHECK(extractor.error() == "No calibration steps were provided.");
    CHECK(extractor.results().empty());
    // Nothing was started, so nothing was reported but the end.
    CHECK(recorder->events == std::vector<std::string>{"finished"});
}

TEST_CASE("CalibrationExtractor: a word map that cannot be used is reported before anything runs")
{
    struct Row
    {
        const char* scenario;
        const char* expected;
    };
    const Row rows[] = {
        {"missing", "Receiver Parameters file 'map.toml' was not found."},
        {"word-outside-frame", "Failed to load Receiver Parameters word map. Check the file."},
        {"word-just-past-the-frame", "Failed to load Receiver Parameters word map. Check the file."},
        {"no-parameters", "Receiver Parameters file contains no parameters."},
    };
    for (const Row& row : rows)
    {
        const std::string scenario = row.scenario;
        INFO("scenario " << scenario);

        CalibrationExtractor::Request req = stepCalRequest();
        TempDir dir;
        const fs::path toml = dir.file("map.toml");
        if (scenario != "missing")
        {
            // An 800-bit frame is 50 words, so word 60 is past it by a little and word 9999 by a lot; the third has no word
            // map at all.
            writeBytes(toml, scenario == "word-outside-frame"         ? "[L_RCVR1]\nWord = 9999\n"
                             : scenario == "word-just-past-the-frame" ? "[L_RCVR1]\nWord = 60\n"
                                                                      : "[Parameters]\nPolarity = 0\n");
        }
        req.receiverParamsToml = toml;

        CalibrationExtractor extractor;
        auto recorder = std::make_shared<Recorder>(extractor);
        CHECK_FALSE(recorder->run(req));
        CHECK(recorder->summary == row.expected);
        CHECK(extractor.error() == row.expected);
        CHECK(recorder->events == std::vector<std::string>{"finished"});
    }
}

TEST_CASE("CalibrationExtractor: a default word map that does not fit the frame is reported")
{
    CalibrationExtractor::Request req = stepCalRequest();
    req.receiverParamsToml.clear(); // no file: the sequential grid is built from the counts
    req.numReceivers     = 40;
    req.receiverChannels = 3;       // 120 words into an 800-bit (50-word) frame

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    CHECK_FALSE(recorder->run(req));
    CHECK_MESSAGE(!recorder->summary.empty(), "a reason is given");
    CHECK(extractor.error() == recorder->summary);
    CHECK(recorder->events == std::vector<std::string>{"finished"});
    CHECK(recorder->summary == "Num Receivers x Receiver Channels exceeds the words available in the minor frame.");
}

TEST_CASE("CalibrationExtractor: a recording that cannot be opened is reported")
{
    CalibrationExtractor::Request req = stepCalRequest();
    TempDir dir;
    req.calFilename = dir.file("tst_calext_missing.ch10");
    REQUIRE_FALSE(fs::exists(req.calFilename));

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    CHECK_FALSE(recorder->run(req));
    CHECK(extractor.results().empty());
    CHECK(recorder->summary == "Error opening data file.");
    CHECK(extractor.error() == recorder->summary);
    CHECK(recorder->events == std::vector<std::string>{"finished"});
}

TEST_CASE("CalibrationExtractor: a cancelled extraction says so and leaves no results")
{
    // Cancelled from the first report that arrives off the calling thread, which is while the (second-long) extraction is
    // still reading the recording.
    const CalibrationExtractor::Request req = stepCalRequest();

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    recorder->cancelOnFirstReport = true;
    CHECK_FALSE(recorder->run(req));
    CHECK(recorder->done);
    CHECK(recorder->summary == "Calibration extraction cancelled.");
    CHECK(extractor.results().empty());
}

TEST_CASE("CalibrationExtractor: starting while running is ignored and an extractor can run again")
{
    const CalibrationExtractor::Request req = stepCalRequest();

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    recorder->start(req);
    recorder->start(req); // already running: ignored, not a second run
    REQUIRE(recorder->wait());
    CHECK(recorder->success);
    CHECK(std::count(recorder->events.begin(), recorder->events.end(), "finished") == 1);
    CHECK(countContaining(recorder->log, "Calibration extraction sample period") == 1);
    const std::size_t first_results = extractor.results().size();
    CHECK(first_results > 0);

    // A finished extractor starts a fresh run, and its results replace the last one's.
    auto again = std::make_shared<Recorder>(extractor);
    CHECK_MESSAGE(again->run(req), again->summary);
    CHECK(extractor.results().size() == first_results);
    CHECK(std::count(again->events.begin(), again->events.end(), "finished") == 1);
}

TEST_CASE("CalibrationExtractor: a run begins with its sample period and summarises what it calibrated")
{
    const CalibrationExtractor::Request req = stepCalRequest();

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    REQUIRE_MESSAGE(recorder->run(req), recorder->summary);

    // The first thing reported is the extraction window it chose.
    REQUIRE_FALSE(recorder->log.empty());
    CHECK(recorder->log.front() ==
          "Calibration extraction sample period: 100.00 ms (~100 frames/window at 800-bit frames).");

    // The message that comes with a success counts the channels that calibrated.
    CHECK(recorder->summary ==
          "Calibrated " + std::to_string(calibratedCount(extractor)) + " of " + std::to_string(extractor.results().size()) +
              " channel(s).");
    CHECK(extractor.error().empty());
}

TEST_CASE("CalibrationExtractor: the end is always the last report and only a failure is an error")
{
    // The extractor waits for its reader and worker before it reports the end, so the end really is last.
    const CalibrationExtractor::Request req = stepCalRequest();

    {
        CalibrationExtractor extractor;
        auto recorder = std::make_shared<Recorder>(extractor);
        REQUIRE_MESSAGE(recorder->run(req), recorder->summary);
        CHECK(recorder->events.back() == "finished");
        CHECK(std::count(recorder->events.begin(), recorder->events.end(), "finished") == 1);
        CHECK(recorder->errors.empty()); // a success reports no error
        REQUIRE_FALSE(recorder->progress.empty());
        CHECK(recorder->progress.back() == 100); // ...and the last progress came first
    }
    {
        CalibrationExtractor extractor;
        auto recorder = std::make_shared<Recorder>(extractor);
        recorder->cancelOnFirstReport = true;
        CHECK_FALSE(recorder->run(req));
        CHECK(recorder->events.back() == "finished");
        CHECK(recorder->errors.empty()); // a cancel is not an error
    }
    {
        CalibrationExtractor::Request bad = req;
        bad.steps.clear();
        CalibrationExtractor extractor;
        auto recorder = std::make_shared<Recorder>(extractor);
        CHECK_FALSE(recorder->run(bad));
        CHECK(recorder->errors == std::vector<std::string>{recorder->summary}); // the reason, once, before the end
    }
}

TEST_CASE("CalibrationExtractor: a worker failure is reported with its own reason, as an error")
{
    // The wrong byte order destroys the sync pattern, so the worker fails with its own reason ("...pattern was not
    // found..."), which is what the run then reports - not a generic message.
    CalibrationExtractor::Request req;
    buildRealRequest(req);
    req.acquisition.swapBytes = false;

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    CHECK_FALSE(recorder->run(req));
    CHECK_MESSAGE(!recorder->summary.empty(), "a reason is given");
    CHECK_MESSAGE(recorder->summary != "Calibration extraction failed.", recorder->summary);
    CHECK(extractor.error() == recorder->summary);
    CHECK(recorder->errors == std::vector<std::string>{recorder->summary});
    CHECK(recorder->events.back() == "finished");
    // The worker's own message also went out as a log line, as it always has.
    CHECK_MESSAGE(std::count(recorder->log.begin(), recorder->log.end(), recorder->summary) > 0, joinLines(recorder->log));
}

TEST_CASE("CalibrationExtractor: a cancel can come from another thread")
{
    const CalibrationExtractor::Request req = stepCalRequest();

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    recorder->start(req);
    // The extraction takes about a second; cancel a tenth of one in, from a thread that is not the caller's and not the
    // extraction's.
    std::thread canceller([&extractor]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        extractor.cancel();
    });
    const bool finished = recorder->wait();
    canceller.join();
    CHECK(finished);
    CHECK_FALSE(recorder->success);
    CHECK(recorder->summary == "Calibration extraction cancelled.");
}

TEST_CASE("CalibrationExtractor: destroying a running extractor cancels it and waits")
{
    const CalibrationExtractor::Request req = stepCalRequest();

    std::shared_ptr<Recorder> recorder;
    const auto began = std::chrono::steady_clock::now();
    {
        CalibrationExtractor extractor;
        recorder = std::make_shared<Recorder>(extractor);
        recorder->start(req);
        CHECK(extractor.running());
        // Leave the scope with the run in flight: the destructor has to stop the extractor's threads and wait for them,
        // not leave them running on freed state.
    }
    const auto took = std::chrono::steady_clock::now() - began;
    CHECK_MESSAGE(took < std::chrono::seconds(30), "destroying a running extractor did not return promptly");
    // It was waited for to the end, and the end was a cancel: the destructor stopped the run rather than letting it
    // finish (the extraction would have gone on to calibrate twelve channels).
    CHECK(recorder->done);
    CHECK(recorder->summary == "Calibration extraction cancelled.");
}

TEST_CASE("CalibrationExtractor: the real step_cal with a short step file is flagged")
{
    // The real trap this guard exists for. rcvr_cals/RASA.toml lists 8 steps (0, 3, 6, 12 ... 36 dB); this recording
    // steps through 11 (0 to 60 dB). Extraction "succeeds" on all twelve swept channels while pairing every one of them
    // with the wrong level - which is exactly how it first reported "12 of 12".
    CalibrationExtractor::Request req;
    buildStepCalRequest(req, fs::path("settings") / "rcvr_cals" / "RASA.toml",
                        fs::path("settings") / "receiver_params" / "default.toml");

    CalibrationExtractor extractor;
    std::string summary;
    REQUIRE_MESSAGE(runExtraction(extractor, req, summary), summary);

    const std::string report = CalibrationExtractor::summarize(extractor.results(), intSize(req.steps));
    CHECK_MESSAGE(contains(report, "Check the step file"), report);
    CHECK_MESSAGE(contains(report, "Receiver 3 — 11 levels in the recording, 8 in the step file"), report);
}

TEST_CASE("CalibrationExtractor: the real step_cal with the wrong word map is flagged")
{
    // The other real trap. This recording lays its receivers out three words apiece; receiver_params/RASA.toml assumes
    // four-word cards with an unused fourth word, so it files words under the wrong receivers and reports live channels
    // as dead with nothing pointing at the map. The report now points at it.
    CalibrationExtractor::Request req;
    buildStepCalRequest(req, fs::path("settings") / "rcvr_cals" / "default.toml",
                        fs::path("settings") / "receiver_params" / "RASA.toml");

    CalibrationExtractor extractor;
    std::string summary;
    REQUIRE_MESSAGE(runExtraction(extractor, req, summary), summary);

    const std::string report = CalibrationExtractor::summarize(extractor.results(), intSize(req.steps));
    CHECK_MESSAGE(contains(report, "Check the word map"), report);
}

TEST_CASE("CalibrationExtractor: the extraction reports its progress")
{
    // The Extract Calibration dialog shows a progress dialog fed by the extractor's progress reports, which it relays
    // from the packet reader. Nothing else observed it, so a break in that relay would leave the dialog at 0% for the
    // whole extraction.
    const CalibrationExtractor::Request req = stepCalRequest();

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    REQUIRE_MESSAGE(recorder->run(req), recorder->summary);

    // The end is reported after the reader has ended, so its last report (100) is already in.
    const std::vector<int>& progress = recorder->progress;
    REQUIRE_FALSE(progress.empty());
    CHECK(progress.back() == 100);
    for (std::size_t i = 0; i < progress.size(); ++i)
    {
        CHECK((progress[i] >= 0 && progress[i] <= 100));
        if (i > 0)
        {
            CHECK_MESSAGE(progress[i] >= progress[i - 1], "progress went backwards");
        }
    }
}

TEST_CASE("CalibrationExtractor: clips beyond the recording are reported")
{
    // Clip Start + Clip End that add up to more than the recording are ignored and the whole recording is used - so the
    // result looks fine while the values the operator typed did nothing. The extractor now says so, and the dialog shows
    // it.
    CalibrationExtractor::Request req = stepCalRequest();

    // 6 s + 60 s on a ~65 s recording: nothing would be left, so both are ignored.
    req.clipStartSec = 6.0;
    req.clipEndSec   = 60.0;
    CalibrationExtractor ignored;
    auto ignored_recorder = std::make_shared<Recorder>(ignored);
    REQUIRE_MESSAGE(ignored_recorder->run(req), ignored_recorder->summary);
    CHECK(ignored.clipIgnored());
    // 6,539,016 bytes of 8 bits at 800 kbit/s, in 100 ms windows.
    CHECK_MESSAGE(std::abs(ignored.recordingSeconds() - 65.4) < 0.05, ignored.recordingSeconds());
    CHECK(calibratedCount(ignored) == 12); // the whole recording was used
    // ...and it says so, with the clip total and the recording's length, to one decimal.
    char seconds[32];
    std::snprintf(seconds, sizeof seconds, "%.1f", ignored.recordingSeconds());
    const std::string warning = std::string("Clip Start + Clip End (66.0 s) exceed the recording (") + seconds +
                                " s); both were ignored and the whole recording was used.";
    CHECK_MESSAGE(std::count(ignored_recorder->log.begin(), ignored_recorder->log.end(), warning) == 1,
                  joinLines(ignored_recorder->log));

    // 6 s + 2 s is honoured, and is not reported.
    req.clipEndSec = 2.0;
    CalibrationExtractor honoured;
    auto honoured_recorder = std::make_shared<Recorder>(honoured);
    REQUIRE_MESSAGE(honoured_recorder->run(req), honoured_recorder->summary);
    CHECK_FALSE(honoured.clipIgnored());
    CHECK_MESSAGE(countContaining(honoured_recorder->log, "Clip Start") == 0, "an honoured clip is not reported");
    CHECK(calibratedCount(honoured) == 12);
    // The recording's length is the recording's, not what was left of it after the clips.
    CHECK_MESSAGE(std::abs(honoured.recordingSeconds() - 65.4) < 0.05, honoured.recordingSeconds());
}

TEST_CASE("CalibrationExtractor: a clip that takes the whole recording from one end is reported and ignored")
{
    // Either clip alone can be more than the recording. It is then ignored (the whole recording is used), and the warning
    // names the total of the two, which here is the one that was typed.
    for (const bool at_start : {true, false})
    {
        INFO((at_start ? "clip start" : "clip end"));
        CalibrationExtractor::Request req = stepCalRequest();
        (at_start ? req.clipStartSec : req.clipEndSec) = 1000.0;

        CalibrationExtractor extractor;
        auto recorder = std::make_shared<Recorder>(extractor);
        REQUIRE_MESSAGE(recorder->run(req), recorder->summary);
        CHECK(extractor.clipIgnored());
        CHECK(calibratedCount(extractor) == 12);
        CHECK_MESSAGE(std::count(recorder->log.begin(), recorder->log.end(),
                                 "Clip Start + Clip End (1000.0 s) exceed the recording (65.4 s); both were ignored and "
                                 "the whole recording was used.") == 1,
                      joinLines(recorder->log));
    }
}

TEST_CASE("CalibrationExtractor: a clip really removes the seconds it names")
{
    // Taking 20 s off the start of the 65.4 s recording removes the first four of the eleven levels, so only receiver 1
    // (whose pairing survives with fewer plateaus than steps because it rails) still calibrates; taking 20 s off the end
    // removes the top ones and nothing calibrates. Unclipped, all twelve do.
    {
        CalibrationExtractor::Request req = stepCalRequest();
        req.clipStartSec = 20.0;
        CalibrationExtractor extractor;
        std::string summary;
        REQUIRE_MESSAGE(runExtraction(extractor, req, summary), summary);
        CHECK_FALSE(extractor.clipIgnored());
        std::set<int> receivers;
        for (const CalibrationChannelResult& r : extractor.results())
        {
            if (r.profile.valid)
            {
                receivers.insert(FrameSetup::receiverIndexFromName(r.name));
            }
        }
        CHECK(calibratedCount(extractor) == 3);
        CHECK(receivers == std::set<int>{1});
    }
    {
        CalibrationExtractor::Request req = stepCalRequest();
        req.clipEndSec = 20.0;
        CalibrationExtractor extractor;
        std::string summary;
        REQUIRE_MESSAGE(runExtraction(extractor, req, summary), summary);
        CHECK_FALSE(extractor.clipIgnored());
        CHECK(calibratedCount(extractor) == 0);
    }
}

TEST_CASE("CalibrationExtractor: each channel of the step_cal extraction comes to the recorded numbers")
{
    // The whole extraction, channel by channel, against what the extractor made when this suite was ported (the tables
    // are above). The second run is clipped, so a clip that was dropped, applied to the wrong end, or measured with the
    // wrong window reads differently from either.
    struct Run
    {
        const char* what;
        double      clip_start;
        double      clip_end;
        const char* const* golden;
    };
    const Run runs[] = {{"unclipped", 0.0, 0.0, kGoldenUnclipped}, {"clipped 6 s and 2 s", 6.0, 2.0, kGoldenClipped}};
    for (const Run& run : runs)
    {
        INFO(run.what);
        CalibrationExtractor::Request req = stepCalRequest();
        req.clipStartSec = run.clip_start;
        req.clipEndSec   = run.clip_end;
        CalibrationExtractor extractor;
        std::string summary;
        REQUIRE_MESSAGE(runExtraction(extractor, req, summary), summary);
        const std::vector<CalibrationChannelResult>& results = extractor.results();
        REQUIRE(results.size() == 48);
        for (std::size_t i = 0; i < results.size(); i++)
        {
            const CalibrationChannelResult& r = results[i];
            double sum = 0.0;
            for (const CalibrationPoint& p : r.profile.points)
            {
                sum += p.rawAvg;
            }
            char token[160];
            std::snprintf(token, sizeof token, "%s %d %d %zu %.1f %.1f", r.name.c_str(), static_cast<int>(r.outcome),
                          r.detectedPlateaus, r.profile.points.size(), r.edgeThreshold, sum);
            CHECK_MESSAGE(std::string(token) == run.golden[i], "channel " << i);
        }
    }
}

TEST_CASE("CalibrationExtractor: the extraction window is sized from the frame rate")
{
    // The window averages ten frames, kept between 100 and 200 ms. The line that says so is reported while start() is still
    // setting up, so each row only needs the first report and then a cancel.
    struct Row
    {
        int         bits;
        double      mbps;
        const char* line;
    };
    const Row rows[] = {
        // 1 ms frames: ten of them are 10 ms, below the floor.
        {800, 0.8, "Calibration extraction sample period: 100.00 ms (~100 frames/window at 800-bit frames)."},
        // 10 ms frames: ten are exactly the floor.
        {8000, 0.8, "Calibration extraction sample period: 100.00 ms (~10 frames/window at 8000-bit frames)."},
        // 16 ms and 20 ms frames: ten of them are between the bounds, and exactly the ceiling.
        {8000, 0.5, "Calibration extraction sample period: 160.00 ms (~10 frames/window at 8000-bit frames)."},
        {8000, 0.4, "Calibration extraction sample period: 200.00 ms (~10 frames/window at 8000-bit frames)."},
        // 40 ms frames: ten of them are over the ceiling, so the window holds only five.
        {8000, 0.2, "Calibration extraction sample period: 200.00 ms (~5 frames/window at 8000-bit frames)."},
        // 8 ms frames: the floor again, holding 12.5 frames, which prints as 13 (half away from zero).
        {800, 0.1, "Calibration extraction sample period: 100.00 ms (~13 frames/window at 800-bit frames)."},
    };
    for (const Row& row : rows)
    {
        INFO(row.bits << " bits at " << row.mbps << " Mbps");
        CalibrationExtractor::Request req = stepCalRequest();
        req.acquisition.bitsInMinorFrame = row.bits;
        req.acquisition.dataRateMbps     = row.mbps;

        CalibrationExtractor extractor;
        auto recorder = std::make_shared<Recorder>(extractor);
        recorder->start(req);
        extractor.cancel();
        REQUIRE(recorder->wait());
        REQUIRE_FALSE(recorder->log.empty());
        CHECK(recorder->log.front() == row.line);
    }
}

TEST_CASE("CalibrationExtractor: a slower recording is windowed at the longer period and measured in its own time")
{
    // At 40 kbit/s an 800-bit frame lasts 20 ms, so ten of them are the 200 ms ceiling, and the same 6,539,016 bytes last
    // twenty times as long as at 800 kbit/s: 1,308 s of recording, in 200 ms windows.
    CalibrationExtractor::Request req = stepCalRequest();
    req.acquisition.dataRateMbps = 0.04;

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    REQUIRE_MESSAGE(recorder->run(req), recorder->summary);
    REQUIRE_FALSE(recorder->log.empty());
    CHECK(recorder->log.front() == "Calibration extraction sample period: 200.00 ms (~10 frames/window at 800-bit frames).");
    CHECK_MESSAGE(std::abs(extractor.recordingSeconds() - 1308.0) < 1.0, extractor.recordingSeconds());
}

TEST_CASE("CalibrationExtractor: a cancel from the very first report, before the worker exists, is a clean cancel")
{
    // The window line is reported by start() itself, before the worker is made. A sink that cancels on it cancels a run
    // that has a reader and a queue but no worker yet.
    const CalibrationExtractor::Request req = stepCalRequest();

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    recorder->cancelAtOnce = true;
    CHECK_FALSE(recorder->run(req));
    CHECK(recorder->done);
    CHECK(recorder->summary == "Calibration extraction cancelled.");
    CHECK(recorder->errors.empty());
    CHECK(extractor.results().empty());
}

TEST_CASE("CalibrationExtractor: a cancel wakes a worker that is waiting for packets")
{
    // A worker waiting on an empty queue is woken by closing it; its abort flag alone cannot reach it. The reader is parked
    // before it has produced anything, so nothing else can end the worker: it has to be the cancel, and it has to be seen
    // before the reader is released.
    const CalibrationExtractor::Request req = stepCalRequest();

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    recorder->parkOn("Reading Chapter 10 file"); // the reader's first line: it reads nothing until released
    ReleaseOnExit guard{recorder};
    recorder->start(req);
    REQUIRE(recorder->waitParked(60000));
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // the worker reaches its empty queue and blocks there

    extractor.cancel();
    const bool worker_ended = recorder->waitForLog("no frame sync found", 15000);
    recorder->release(); // only now may the reader go on
    REQUIRE_MESSAGE(worker_ended, "the worker was left waiting for packets: " << joinLines(recorder->log));

    REQUIRE(recorder->wait());
    CHECK(recorder->summary == "Calibration extraction cancelled.");
    // The reader saw it too, at its first look.
    CHECK_MESSAGE(countContaining(recorder->log, "Reading cancelled by user.") == 1, joinLines(recorder->log));
}

TEST_CASE("CalibrationExtractor: a cancel that lands after the worker has finished still cancels")
{
    // The worker is parked on its last line, after its work and before it reports that it succeeded; the cancel arrives
    // then. The run is a cancel all the same: no results, no success.
    const CalibrationExtractor::Request req = stepCalRequest();

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    recorder->parkOn("complete —");
    ReleaseOnExit guard{recorder};
    recorder->start(req);
    REQUIRE(recorder->waitParked(60000));

    extractor.cancel();
    recorder->release();
    REQUIRE(recorder->wait());
    CHECK_FALSE(recorder->success);
    CHECK(recorder->summary == "Calibration extraction cancelled.");
    CHECK(extractor.results().empty());
    CHECK(recorder->errors.empty());
}

TEST_CASE("CalibrationExtractor: an extractor starts clean after any kind of run")
{
    // One extractor, several runs in turn. Nothing of one is left in the next: not its clip flag, its length, its error,
    // its results, or the cancel that ended it.
    const CalibrationExtractor::Request good = stepCalRequest();
    CalibrationExtractor extractor;
    auto run = [&](const CalibrationExtractor::Request& req, bool cancel_first = false) {
        auto recorder = std::make_shared<Recorder>(extractor);
        recorder->cancelOnFirstReport = cancel_first;
        recorder->run(req);
        return recorder;
    };

    // A run whose clips were ignored...
    CalibrationExtractor::Request clipped = good;
    clipped.clipStartSec = 6.0;
    clipped.clipEndSec   = 60.0;
    REQUIRE(run(clipped)->success);
    CHECK(extractor.clipIgnored());
    CHECK_MESSAGE(std::abs(extractor.recordingSeconds() - 65.4) < 0.05, extractor.recordingSeconds());

    // ...then an ordinary one: the flag is the new run's.
    const auto second = run(good);
    REQUIRE(second->success);
    CHECK_FALSE(extractor.clipIgnored());

    // ...then one that fails before it starts: the old results, length and flag are gone, and the reason is the new one's.
    CalibrationExtractor::Request bad = good;
    bad.steps.clear();
    CHECK_FALSE(run(bad)->success);
    CHECK(extractor.results().empty());
    CHECK(extractor.recordingSeconds() == 0.0);
    CHECK_FALSE(extractor.clipIgnored());
    CHECK(extractor.error() == "No calibration steps were provided.");

    // ...then a good one: no error left over from the failure.
    const auto third = run(good);
    REQUIRE_MESSAGE(third->success, third->summary);
    CHECK(extractor.error().empty());
    CHECK(third->summary == "Calibrated 12 of 48 channel(s).");

    // ...then one that is cancelled, and a good one after it: the cancel does not outlive its run.
    CHECK_FALSE(run(good, /*cancel_first*/ true)->success);
    const auto last = run(good);
    CHECK_MESSAGE(last->success, last->summary);
    CHECK(extractor.results().size() == 48);
}

TEST_CASE("CalibrationExtractor: the default word map takes every word of the frame but the sync")
{
    // An 800-bit frame is 50 words, one of them the sync, so 49 channels fit and 50 do not.
    for (const int channels : {49, 50})
    {
        INFO(channels << " channels");
        CalibrationExtractor::Request req = stepCalRequest();
        req.receiverParamsToml.clear();
        req.numReceivers     = channels;
        req.receiverChannels = 1;

        CalibrationExtractor extractor;
        auto recorder = std::make_shared<Recorder>(extractor);
        recorder->start(req);
        extractor.cancel();
        REQUIRE(recorder->wait());
        if (channels == 49)
        {
            CHECK(recorder->errors.empty());
            CHECK(recorder->summary == "Calibration extraction cancelled."); // it got as far as running
        }
        else
        {
            CHECK(recorder->errors ==
                  std::vector<std::string>{"Num Receivers x Receiver Channels exceeds the words available in the minor frame."});
        }
    }
}

TEST_CASE("CalibrationExtractor: a channel calibrated with a relaxed threshold says so in its log line")
{
    // Four expected steps against a recording that sweeps eleven: the nominal edge threshold cannot resolve so few levels,
    // and some channels only pair after it is lowered. The note on their line gives the threshold that worked.
    CalibrationExtractor::Request req = stepCalRequest();
    req.steps = {StepDefinition{0.0}, StepDefinition{6.0}, StepDefinition{12.0}, StepDefinition{18.0}};

    CalibrationExtractor extractor;
    auto recorder = std::make_shared<Recorder>(extractor);
    REQUIRE_MESSAGE(recorder->run(req), recorder->summary);

    int relaxed = 0;
    for (const CalibrationChannelResult& r : extractor.results())
    {
        if (!(r.profile.valid && r.thresholdRelaxed && !r.saturated))
        {
            continue;
        }
        relaxed++;
        const std::string prefix = "  " + r.name + " (word " + std::to_string(r.word) + "): ";
        int lines = 0;
        for (const std::string& line : recorder->log)
        {
            if (line.starts_with(prefix))
            {
                lines++;
                CHECK_MESSAGE(contains(line, " — calibrated with a relaxed edge threshold (" + fixed(r.edgeThreshold, 1) +
                                                 " raw counts)"),
                              line);
            }
        }
        CHECK(lines == 1);
    }
    CHECK_MESSAGE(relaxed > 0, "the recording should need a relaxed threshold with four steps, or this proves nothing");
}

TEST_CASE("CalibrationExtractor: the extractor is no longer running while it reports the end")
{
    // running() goes false BEFORE the end is reported, so a handler that wants to start the next run is not turned away by
    // the run that is still reporting it. Looked at from inside the report.
    const CalibrationExtractor::Request req = stepCalRequest();

    {
        CalibrationExtractor extractor;
        auto recorder = std::make_shared<Recorder>(extractor);
        REQUIRE_MESSAGE(recorder->run(req), recorder->summary);
        CHECK_FALSE(recorder->runningAtEnd);
    }
    {
        // And a run that fails before it starts reports its end from start() itself.
        CalibrationExtractor::Request bad = req;
        bad.steps.clear();
        CalibrationExtractor extractor;
        auto recorder = std::make_shared<Recorder>(extractor);
        CHECK_FALSE(recorder->run(bad));
        CHECK_FALSE(recorder->runningAtEnd);
    }
}
