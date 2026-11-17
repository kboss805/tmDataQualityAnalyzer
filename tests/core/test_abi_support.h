// What the parts of the C ABI's native tests share (the follow-up audit's Q5 split test_abi.cpp, once 4,200 lines, by the
// ABI source each part exercises): the includes, the fixtures, and the helpers that run the core directly and read what
// the ABI makes. Everything is in an anonymous namespace and every function is inline, so each part has its own copy and
// none is reported unused.

#ifndef TEST_ABI_SUPPORT_H
#define TEST_ABI_SUPPORT_H

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "abi_events.h"
#include "abi_internal.h"
#include "calibrationextractor.h"
#include "calibrationrequest.h"
#include "chapter10reader.h"
#include "chartgeometry.h"
#include "clocktime.h"
#include "constants.h"
#include "csvseriesparser.h"
#include "csvserieswriter.h"
#include "doctest.h"
#include "framesetup.h"
#include "framesync.h"
#include "framesyncfile.h"
#include "pipelinerunner.h"
#include "processedstreamdata.h"
#include "seriesbuilder.h"
#include "stepdetector.h"
#include "streamjobbuilder.h"
#include "templatematcher.h"
#include "test_support.h"
#include "textfile.h"
#include "tmdq_abi.h"

using namespace tmdq_test;

namespace {

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

/// A path as UTF-8 text, the form the ABI takes it in.
inline std::string utf8Of(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

/// The repository root, where `settings/` and the committed recordings are.
inline std::string rootUtf8() { return utf8Of(repoRoot()); }

/// The system temp folder, for paths that must NOT exist (nothing is ever written there).
inline std::string tempUtf8()
{
    std::string temp = utf8Of(fs::temp_directory_path());
    while (!temp.empty() && temp.back() == '/')
        temp.pop_back();
    return temp;
}

/// A file under tests/data. The recordings there are committed and required; the CSV exports are not committed, so a
/// caller asks whether one exists.
inline std::string dataPath(const std::string& name) { return rootUtf8() + "/tests/data/" + name; }

/// A committed recording under tests/data/test files, required to exist.
inline std::string recording(const char* name) { return utf8Of(recordingFile(name)); }

/// A scratch folder whose paths come back as UTF-8 text.
struct Scratch
{
    TempDir dir;
    std::string filePath(const std::string& name) const { return utf8Of(dir.file(name)); }
    std::string path() const { return utf8Of(dir.path()); }
};

/// The small committed recording the pipeline tests use: one PCM channel, 800-bit frames, sync FE6B2840.
struct StepCal
{
    std::string path;
    int         timeId = -1;
    int         pcmId  = -1;
    bool        ok = false;
};

inline StepCal loadStepCal()
{
    StepCal f;
    f.path = recording("step_cal, example.ch10");
    Chapter10Reader reader;
    REQUIRE(reader.loadChannels(textfile::pathFromUtf8(f.path)));
    f.timeId = reader.getCurrentTimeChannelID();
    f.pcmId  = reader.getFirstPCMChannelID();
    f.ok     = f.timeId >= 0 && f.pcmId >= 0;
    REQUIRE(f.ok);
    return f;
}

/// The committed PRN sample, as a recording to run: its default time channel and first PCM channel.
inline StepCal loadPrnSample()
{
    StepCal prn;
    prn.path = recording("prn_testfile, sample.ch10");
    tmdq_file* file = nullptr;
    REQUIRE(tmdq_file_open(prn.path.c_str(), &file) == int(TMDQ_OK));
    prn.timeId = tmdq_file_default_time_channel(file);
    tmdq_channel_info info{};
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_file_pcm_channel(file, 0, &info) == int(TMDQ_OK));
    prn.pcmId = info.channel_id;
    prn.ok = true;
    tmdq_file_close(file);
    return prn;
}

constexpr int kTimeoutMs = 180000;

// ---------------------------------------------------------------------------
// One stream's settings, held as the ABI wants them and as the core's builder wants them. The two views
// are filled field by field here, so a field the ABI forgets to carry shows up as a difference.
// ---------------------------------------------------------------------------

struct Case
{
    int         pcmId = -1;
    std::string label;
    int         mode = TMDQ_MODE_FRAME_SYNC_LOCK;
    std::string pattern = "FE6B2840";
    std::string mask;
    int         bits = 800;
    bool        randomized = false;
    bool        inverted = false;
    double      rateMbps = 0.0;
    int         periodIndex = TimeConstants::kDefaultSamplePeriodIndex;
    int         polarityIndex = ReceiverConstants::kDefaultPolarityIndex;
    int         slopeIndex = ReceiverConstants::kDefaultSlopeIndex;
    double      scale = PCMConstants::kDefaultScaleDdBPerV;
    int         numReceivers = PCMConstants::kDefaultNumReceivers;
    int         receiverChannels = PCMConstants::kDefaultReceiverChannels;
    std::string receiverParams;
    std::unordered_map<int, std::vector<tmdq_cal_point>> calibration;   ///< by word; every profile is valid

    // Backing store for the wire form.
    mutable std::vector<tmdq_cal_profile> wireProfiles;

    tmdq_stream_config wire() const
    {
        wireProfiles.clear();
        for (const auto& [word, points] : calibration)
        {
            tmdq_cal_profile p{};
            p.points      = points.data();
            p.word_index  = word;
            p.point_count = static_cast<int32_t>(points.size());
            p.valid       = 1;
            wireProfiles.push_back(p);
        }
        tmdq_stream_config c{};
        c.struct_size               = sizeof(c);
        c.pcm_channel_id            = pcmId;
        c.label_utf8                = label.c_str();
        c.sync_pattern_utf8         = pattern.c_str();
        c.sync_mask_utf8            = mask.c_str();
        c.receiver_params_toml_utf8 = receiverParams.c_str();
        c.calibration               = wireProfiles.empty() ? nullptr : wireProfiles.data();
        c.data_rate_mbps            = rateMbps;
        c.scale_d_db_per_v          = scale;
        c.mode                      = mode;
        c.bits_in_minor_frame       = bits;
        c.randomized                = randomized ? 1 : 0;
        c.inverted                  = inverted ? 1 : 0;
        c.sample_period_index       = periodIndex;
        c.polarity_index            = polarityIndex;
        c.slope_index               = slopeIndex;
        c.num_receivers             = numReceivers;
        c.receiver_channels         = receiverChannels;
        c.calibration_count         = static_cast<int32_t>(wireProfiles.size());
        return c;
    }

    StreamJobConfig core() const
    {
        StreamJobConfig c;
        c.pcmChannelId      = pcmId;
        c.label             = label;
        c.mode              = mode == TMDQ_MODE_FRAME_SYNC_LOCK ? StreamMode::FrameSyncLockStats
                                                                : StreamMode::ReceiverChannelInfo;
        c.syncPattern       = pattern;
        c.syncMask          = mask;
        c.bitsInMinorFrame  = bits;
        c.randomized        = randomized;
        c.inverted          = inverted;
        c.dataRateMbps      = rateMbps;
        c.samplePeriodIndex = periodIndex;
        c.polarityIndex     = polarityIndex;
        c.slopeIndex        = slopeIndex;
        c.scaleDdBPerV      = scale;
        c.numReceivers      = numReceivers;
        c.receiverChannels  = receiverChannels;
        if (!receiverParams.empty())
            c.receiverParamsToml = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(receiverParams.c_str())));
        for (const auto& [word, points] : calibration)
        {
            CalibrationProfile p;
            p.valid = true;
            for (const tmdq_cal_point& pt : points)
                p.points.push_back({pt.raw_avg, pt.true_db});
            c.calibrationByWord.emplace(word, std::move(p));
        }
        return c;
    }
};

inline Case lockCase(const StepCal& f)
{
    Case c;
    c.pcmId = f.pcmId;
    c.label = "lock stream";
    return c;
}

inline Case receiverCase(const StepCal& f)
{
    Case c;
    c.pcmId = f.pcmId;
    c.label = "receiver stream";
    c.mode  = TMDQ_MODE_RECEIVER_CHANNEL_INFO;
    return c;
}

// ---------------------------------------------------------------------------
// What the core makes: the same jobs, built and run directly
// ---------------------------------------------------------------------------

class Collect final : public PipelineEvents
{
public:
    void logMessage(const std::string&) override {}
    void errorOccurred(const std::string& m) override { std::lock_guard<std::mutex> l(m_mutex); m_errors.push_back(m); }
    void processingFinished(bool ok) override
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_success = ok;
        m_done = true;
        m_cv.notify_all();
    }
    void streamProcessed(ProcessedStreamData d) override
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_results[d.jobIndex] = std::move(d);
    }
    bool wait(int ms)
    {
        std::unique_lock<std::mutex> l(m_mutex);
        return m_cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return m_done; });
    }
    std::map<int, ProcessedStreamData> results() const { std::lock_guard<std::mutex> l(m_mutex); return m_results; }
    bool success() const { std::lock_guard<std::mutex> l(m_mutex); return m_success; }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<int, ProcessedStreamData> m_results;
    std::vector<std::string> m_errors;
    bool m_success = false;
    bool m_done = false;
};

/// Builds and runs @p cases over @p f through buildStreamJob + PipelineRunner, exactly as abi_run.cpp does, with no ABI.
inline std::map<int, ProcessedStreamData> runCore(const StepCal& f, int sourceId, const std::vector<Case>& cases,
                                           const std::string& appRoot, bool& ok, bool swapBytes = true)
{
    ProcessingParams base;
    base.filename      = textfile::pathFromUtf8(f.path);
    base.sourceId      = sourceId;
    base.timeChannelId = f.timeId;

    StreamJobContext ctx;
    ctx.appRoot   = appRoot;
    ctx.swapBytes = swapBytes;

    std::vector<StreamJob> jobs;
    for (const Case& c : cases)
    {
        StreamJob job;
        StreamJobReport report;
        if (!buildStreamJob(c.core(), base, ctx, job, report))
        {
            ok = false;
            return {};
        }
        jobs.push_back(std::move(job));
    }
    auto sink = std::make_shared<Collect>();
    PipelineRunner runner;
    std::string error;
    ok = runner.start(std::move(jobs), sink, error) && sink->wait(kTimeoutMs);
    runner.wait();
    return sink->results();
}

// ---------------------------------------------------------------------------
// What the ABI makes: callbacks collected
// ---------------------------------------------------------------------------

struct Seen
{
    std::mutex              mutex;
    std::condition_variable cv;
    std::vector<std::string> events;                      ///< "log:..", "error:..", "progress:N", "stream:J", "finished:S"
    std::vector<tmdq_stream_result*> results;
    std::set<std::thread::id> progressThreads;
    std::vector<std::thread::id> logThreads;              ///< the thread of each "log:" event, in order
    std::map<int, std::thread::id> streamThreads;         ///< job index -> thread that delivered it
    std::thread::id         finishedThread;
    int                     finishedCount = 0;
    int                     finishedSuccess = -1;
    bool                    throwing = false;
    bool                    keepResults = true;

    /// A host that waits on its own run from `finished`, which the header forbids: what that call did, seen from the
    /// callback's thread. The handle (a tmdq_run, or a tmdq_calibration when @c waitOnCalibration) is set by the test
    /// once start has returned, so `finished` waits for it.
    bool                    waitFromFinished = false;
    bool                    freeFromFinished = false;    ///< as waitFromFinished, but the forbidden call is the free
    bool                    waitOnCalibration = false;
    std::atomic<void*>      waitOn{nullptr};
    bool                    waitThrew = false;
    std::string             waitError;

    ~Seen()
    {
        for (tmdq_stream_result* r : results)
            tmdq_stream_result_free(r);
    }

    static Seen* self(void* user) { return static_cast<Seen*>(user); }

    static void onLog(void* u, const char* m)
    {
        Seen* s = self(u);
        { std::lock_guard<std::mutex> l(s->mutex); s->events.push_back(std::string("log:") + m); s->logThreads.push_back(std::this_thread::get_id()); }
        if (s->throwing) throw std::runtime_error("log");
    }
    static void onError(void* u, const char* m)
    {
        Seen* s = self(u);
        { std::lock_guard<std::mutex> l(s->mutex); s->events.push_back(std::string("error:") + m); }
        if (s->throwing) throw std::runtime_error("error");
    }
    static void onProgress(void* u, int32_t p)
    {
        Seen* s = self(u);
        { std::lock_guard<std::mutex> l(s->mutex); s->events.push_back("progress:" + std::to_string(p)); s->progressThreads.insert(std::this_thread::get_id()); }
        if (s->throwing) throw std::runtime_error("progress");
    }
    static void onStream(void* u, tmdq_stream_result* r)
    {
        Seen* s = self(u);
        tmdq_stream_info info{};
        info.struct_size = sizeof(info);
        tmdq_stream_result_info(r, &info);
        {
            std::lock_guard<std::mutex> l(s->mutex);
            s->events.push_back("stream:" + std::to_string(info.job_index));
            s->streamThreads[info.job_index] = std::this_thread::get_id();
            if (s->keepResults) s->results.push_back(r); else tmdq_stream_result_free(r);
        }
        if (s->throwing) throw std::runtime_error("stream");
    }
    static void onFinished(void* u, int32_t ok)
    {
        Seen* s = self(u);
        if (s->waitFromFinished || s->freeFromFinished)
        {
            void* handle = nullptr;
            for (int i = 0; i < 1000 && (handle = s->waitOn.load()) == nullptr; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            try
            {
                if (handle == nullptr)                  s->waitError = "no handle was given to wait on";
                else if (s->freeFromFinished)
                {
                    if (s->waitOnCalibration)           tmdq_calibration_free(static_cast<tmdq_calibration*>(handle));
                    else                                tmdq_run_free(static_cast<tmdq_run*>(handle));
                }
                else if (s->waitOnCalibration)          tmdq_calibration_wait(static_cast<tmdq_calibration*>(handle));
                else                                    tmdq_run_wait(static_cast<tmdq_run*>(handle));
            }
            catch (...)
            {
                s->waitThrew = true;
            }
            if (handle != nullptr)
            {
                const char* error = tmdq_last_error();
                s->waitError = error ? error : "";
            }
        }
        {
            std::lock_guard<std::mutex> l(s->mutex);
            s->events.push_back("finished:" + std::to_string(ok));
            s->finishedThread = std::this_thread::get_id();
            s->finishedSuccess = ok;
            ++s->finishedCount;
            s->cv.notify_all();
        }
        if (s->throwing) throw std::runtime_error("finished");
    }

    tmdq_callbacks callbacks()
    {
        tmdq_callbacks c{};
        c.struct_size = sizeof(c);
        c.user        = this;
        c.log         = &onLog;
        c.error       = &onError;
        c.progress    = &onProgress;
        c.stream_done = &onStream;
        c.finished    = &onFinished;
        return c;
    }

    bool waitFinished(int ms)
    {
        std::unique_lock<std::mutex> l(mutex);
        return cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return finishedCount > 0; });
    }
    std::vector<std::string> snapshot() { std::lock_guard<std::mutex> l(mutex); return events; }
    bool any(const std::string& prefix)
    {
        for (const std::string& e : snapshot())
            if (e.rfind(prefix, 0) == 0) return true;
        return false;
    }
    int count(const std::string& exact)
    {
        int n = 0;
        for (const std::string& e : snapshot())
            if (e == exact) ++n;
        return n;
    }
    std::string joined() { std::string j; for (const std::string& e : snapshot()) j += e + "\n"; return j; }

    /// The collected results, by job index.
    std::map<int, tmdq_stream_result*> byJob()
    {
        std::map<int, tmdq_stream_result*> m;
        for (tmdq_stream_result* r : results)
        {
            tmdq_stream_info info{};
            info.struct_size = sizeof(info);
            tmdq_stream_result_info(r, &info);
            m[info.job_index] = r;
        }
        return m;
    }
};

/// Frees a run however the test ends.
struct RunGuard
{
    tmdq_run* run = nullptr;
    ~RunGuard() { tmdq_run_free(run); }
};

/// A request over @p cases, with the wire forms kept alive as long as the object.
struct Request
{
    std::vector<Case> cases;                       ///< Kept: the wire forms point into them.
    std::vector<tmdq_stream_config> configs;
    std::vector<const tmdq_stream_config*> pointers;
    std::string file;
    std::string appRoot;
    tmdq_run_request req{};

    Request(const StepCal& f, std::vector<Case> streams, int sourceId = 0)
        : cases(std::move(streams)), file(f.path), appRoot(rootUtf8())
    {
        configs.reserve(cases.size());
        for (const Case& c : cases) configs.push_back(c.wire());
        for (const tmdq_stream_config& c : configs) pointers.push_back(&c);
        req.struct_size     = sizeof(req);
        req.time_channel_id = f.timeId;
        req.filename_utf8   = file.c_str();
        req.app_root_utf8   = appRoot.c_str();
        req.streams         = pointers.data();
        req.stream_count    = static_cast<int32_t>(pointers.size());
        req.source_id       = sourceId;
        req.swap_bytes      = 1;
    }
};


// ---------------------------------------------------------------------------
// The formats
// ---------------------------------------------------------------------------

inline bool sameDoubles(const double* a, std::size_t na, const std::vector<double>& b);

/// The names of what differs, joined into one message.
class Diff
{
public:
    Diff& operator<<(const std::string& what) { m_items.push_back(what); return *this; }
    bool empty() const { return m_items.empty(); }
    std::string join(const std::string& separator) const
    {
        std::string all;
        for (const std::string& item : m_items)
            all += (all.empty() ? "" : separator) + item;
        return all;
    }
private:
    std::vector<std::string> m_items;
};

/// A file's bytes, or a marker that cannot be confused with any file's when it cannot be read.
inline std::string slurp(const std::string& path)
{
    return fs::exists(textfile::pathFromUtf8(path)) ? readBytes(textfile::pathFromUtf8(path)) : std::string("<unreadable>");
}

inline std::string writeFile(const std::string& path, const std::string& bytes)
{
    writeBytes(textfile::pathFromUtf8(path), bytes);
    return path;
}

/// A pattern file as text, from the core and from the ABI, so two compare in one line.
inline std::string describeSyncFile(const FrameSyncFile& f)
{
    // An absent optional reads as 0 beside a "has" flag of 0, which is what the ABI promises.
    return std::format("pattern=[{}] mask=[{}] has_bits={} bits={} legacy={} inverted={} has_randomized={} randomized={}",
                       f.pattern, f.mask, f.bitsPerFrame.has_value() ? 1 : 0, f.bitsPerFrame.value_or(0),
                       f.bitsFromLegacyKey ? 1 : 0, f.inverted ? 1 : 0, f.randomized.has_value() ? 1 : 0,
                       f.randomized.value_or(false) ? 1 : 0);
}

inline std::string describeSyncFile(const tmdq_frame_sync_file* file)
{
    tmdq_frame_sync_file_info i{};
    i.struct_size = sizeof(i);
    if (tmdq_frame_sync_file_info_get(file, &i) != TMDQ_OK)
        return "info failed";
    return std::format("pattern=[{}] mask=[{}] has_bits={} bits={} legacy={} inverted={} has_randomized={} randomized={}",
                       i.pattern_utf8, i.mask_utf8, i.has_bits_per_frame, i.bits_per_frame, i.bits_from_legacy_key,
                       i.inverted, i.has_randomized, i.randomized);
}

/// What differs between a parsed CSV through the ABI and the core's own parse; empty if nothing.
inline std::string csvDifferences(const tmdq_csv* csv, const CsvParseResult& want)
{
    Diff diff;
    tmdq_csv_info info{};
    info.struct_size = sizeof(info);
    if (tmdq_csv_info_get(csv, &info) != TMDQ_OK)
        return "info failed";
    if (std::size_t(info.series_count) != want.series.size()) return "series count";
    if (info.base_day != want.baseDay) diff << "base day";
    if (info.skipped_rows != want.skippedRows) diff << "skipped rows";
    if (info.base_time_offset != want.baseTimeOffset) diff << "base time offset";
    if (info.x_max != want.xMax) diff << "x max";
    for (int32_t i = 0; i < info.series_count; ++i)
    {
        tmdq_csv_series_info s{};
        s.struct_size = sizeof(s);
        if (tmdq_csv_series_get(csv, i, &s) != TMDQ_OK) { diff << "series call"; continue; }
        const CsvSeries& w = want.series[std::size_t(i)];
        const std::string at = std::to_string(i) + ":";
        if (s.metric_type != int(w.metricType)) diff << at + "metric";
        if (std::string(s.name_utf8) != w.name) diff << at + "name";
        if (std::string(s.stream_label_utf8) != w.streamLabel) diff << at + "label";
        if (s.receiver_index != w.receiverIndex) diff << at + "receiver";
        if (s.channel_index != w.channelIndex) diff << at + "channel";
        if (s.stream_order != w.streamOrder) diff << at + "order";
        if (s.source_id != w.sourceId) diff << at + "source";
        if (s.y_min != w.yMin) diff << at + "ymin";
        if (s.y_max != w.yMax) diff << at + "ymax";
        if (std::size_t(s.point_count) != w.xValues.size() || !sameDoubles(s.x_values, std::size_t(s.point_count), w.xValues)) diff << at + "x";
        if (std::size_t(s.point_count) != w.yValues.size() || !sameDoubles(s.y_values, std::size_t(s.point_count), w.yValues)) diff << at + "y";
    }
    return diff.join(", ");
}

/// Columns that own their samples, with the wire form pointing into them.
struct CsvColumns
{
    struct Owned
    {
        int         metric = TMDQ_METRIC_SNR;
        std::string name;
        int         source = 0;
        std::vector<double> x, y;
    };
    std::vector<Owned>                 owned;
    std::vector<tmdq_csv_column>       wire;
    std::vector<const tmdq_csv_column*> pointers;
    std::vector<CsvColumn>             core;

    void add(int metric, const std::string& name, int source, std::vector<double> x, std::vector<double> y)
    {
        owned.push_back({metric, name, source, std::move(x), std::move(y)});
    }
    void finish()
    {
        wire.clear(); pointers.clear(); core.clear();
        wire.reserve(owned.size());
        for (const Owned& o : owned)
        {
            tmdq_csv_column c{};
            c.struct_size = sizeof(c);
            c.metric_type = o.metric;
            c.name_utf8   = o.name.c_str();
            c.x_values    = o.x.data();
            c.y_values    = o.y.data();
            c.source_id   = o.source;
            c.x_count     = int(o.x.size());
            c.y_count     = int(o.y.size());
            wire.push_back(c);
            CsvColumn k;
            k.metricType = MetricType(o.metric);
            k.name       = o.name;
            k.sourceId   = o.source;
            k.xValues    = std::span<const double>(o.x);
            k.yValues    = std::span<const double>(o.y);
            core.push_back(k);
        }
        for (const tmdq_csv_column& c : wire) pointers.push_back(&c);
    }
};


// ---------------------------------------------------------------------------
// Series construction
// ---------------------------------------------------------------------------

/// A finished stream the host holds, made the way the run's callback makes one.
inline tmdq_stream_result* makeResult(const ProcessedStreamData& data)
{
    return new tmdq_stream_result{std::make_shared<const ProcessedStreamData>(data)};
}

/// A processed stream with names and values that exercise the builder.
inline ProcessedStreamData randomStream(std::mt19937& rng)
{
    static const char* const labels[] = { "A", "Ch 13", "R\xC3\xA9""cepteur", "", "Prn 15 5M", "same" };
    static const char* const names[] = { "L_RCVR1", "R_RCVR1", "C_RCVR1", "L_RCVR2", "X_RCVR2_RCVR3", "_RCVR5", "L_RCVR",
                                         "L_RCVRx", "L_RCVR 7", "L_RCVR+4", "L_RCVR-2", "L_RCVR0", "L_RCVR+-3",
                                         "L_RCVR2147483647", "L_RCVR2147483648", "L_RCVR99999999999" };
    static const double specials[] = { 0.0, -0.0, 1.0, -80.5, 99.9, 1e300,
                                       std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() };
    ProcessedStreamData d;
    d.streamLabel = labels[rng() % 6];
    d.pcmChannelId = int(rng() % 40);
    d.jobIndex = int(rng() % 5);
    d.sourceId = int(rng() % 3);
    const std::size_t n = rng() % 5;
    auto values = [&](std::size_t count) {
        std::vector<double> v;
        for (std::size_t i = 0; i < count; ++i)
            v.push_back(rng() % 6 == 0 ? specials[rng() % 8] : double(int(rng() % 2000) - 1000) / 8.0);
        return v;
    };
    for (std::size_t i = 0; i < n; ++i)
        d.timesSec.push_back(1.7e9 + double(rng() % 1000) / 4.0);
    if (rng() % 4 != 0) d.lockPercent = values(n);
    if (rng() % 3 != 0) d.accumulatedMissedFrames = values(n);
    const int channels = int(rng() % 5);
    for (int i = 0; i < channels; ++i)
    {
        ProcessedChannelSeries ch;
        ch.name = names[rng() % 16];
        ch.word = int(rng() % 50);
        ch.values = values(n);
        d.channels.push_back(std::move(ch));
    }
    d.mode = channels > 0 ? StreamMode::ReceiverChannelInfo : StreamMode::FrameSyncLockStats;
    return d;
}

/// What differs between a series set through the ABI and the core's own StreamSeries; empty if nothing.
inline std::string seriesDifferences(const tmdq_series_set* set, const StreamSeries& want)
{
    Diff diff;
    tmdq_series_set_info info{};
    info.struct_size = sizeof(info);
    if (tmdq_series_set_info_get(set, &info) != TMDQ_OK)
        return "info failed";
    if (std::size_t(info.series_count) != want.series.size()) return "series count";
    if (!sameDoubles(info.elapsed, std::size_t(info.elapsed_count), want.elapsed)) diff << "elapsed";
    for (int32_t i = 0; i < info.series_count; ++i)
    {
        tmdq_series_spec s{};
        s.struct_size = sizeof(s);
        if (tmdq_series_set_spec(set, i, &s) != TMDQ_OK) { diff << "spec call"; continue; }
        const SeriesSpec& w = want.series[std::size_t(i)];
        const std::string at = std::to_string(i) + ":";
        if (s.metric_type != int(w.metricType)) diff << at + "metric";
        if (std::string(s.name_utf8) != w.name) diff << at + "name";
        if (std::string(s.stream_label_utf8) != w.streamLabel) diff << at + "label";
        if (s.receiver_index != w.receiverIndex) diff << at + "receiver";
        if (s.channel_index != w.channelIndex) diff << at + "channel";
        if (s.stream_order != w.streamOrder) diff << at + "order";
        if (s.stream_sequence != w.streamSequence) diff << at + "sequence";
        if (s.source_id != w.sourceId) diff << at + "source";
        if (std::memcmp(&s.y_min, &w.yMin, sizeof(double)) != 0) diff << at + "ymin";
        if (std::memcmp(&s.y_max, &w.yMax, sizeof(double)) != 0) diff << at + "ymax";
        if (std::size_t(s.y_count) != w.yValues.size() ||
            (s.y_count > 0 && std::memcmp(s.y_values, w.yValues.data(), w.yValues.size() * sizeof(double)) != 0))
            diff << at + "values";
    }
    return diff.join(", ");
}

/// Starts @p request, waits for the end, and says whether it ended successfully.
inline bool runToTheEnd(Request& request, Seen& seen)
{
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    if (tmdq_run_start(&request.req, &callbacks, &run) != TMDQ_OK)
        return false;
    RunGuard guard{run};
    if (!seen.waitFinished(kTimeoutMs))
        return false;
    tmdq_run_wait(run);
    return seen.finishedSuccess == 1;
}

inline bool sameStream(const ProcessedStreamData& a, const ProcessedStreamData& b)
{
    if (a.timesSec != b.timesSec || a.lockPercent != b.lockPercent ||
        a.accumulatedMissedFrames != b.accumulatedMissedFrames || a.channels.size() != b.channels.size())
        return false;
    for (std::size_t i = 0; i < a.channels.size(); ++i)
        if (a.channels[i].name != b.channels[i].name || a.channels[i].word != b.channels[i].word ||
            a.channels[i].values != b.channels[i].values)
            return false;
    return true;
}

inline bool sameDoubles(const double* a, std::size_t na, const std::vector<double>& b)
{
    return na == b.size() && (na == 0 || std::memcmp(a, b.data(), na * sizeof(double)) == 0);
}

/// What differs between a result the ABI delivered and the one the core made; empty if nothing.
inline std::string differences(tmdq_stream_result* r, const ProcessedStreamData& want)
{
    tmdq_stream_info info{};
    info.struct_size = sizeof(info);
    if (tmdq_stream_result_info(r, &info) != TMDQ_OK)
        return "tmdq_stream_result_info failed";
    Diff diff;
    if (info.pcm_channel_id != want.pcmChannelId) diff << "pcm channel";
    if (std::string(info.label_utf8) != want.streamLabel) diff << "label";
    if (info.job_index != want.jobIndex) diff << "job index";
    if (info.source_id != want.sourceId) diff << "source id";
    if ((info.mode == TMDQ_MODE_FRAME_SYNC_LOCK) != (want.mode == StreamMode::FrameSyncLockStats)) diff << "mode";
    const std::size_t n = static_cast<std::size_t>(info.sample_count);
    if (!sameDoubles(info.times_sec, n, want.timesSec)) diff << "times";
    if (!sameDoubles(info.lock_percent, want.lockPercent.empty() ? 0 : n, want.lockPercent)) diff << "lock";
    if (!sameDoubles(info.missed_frames, want.accumulatedMissedFrames.empty() ? 0 : n, want.accumulatedMissedFrames)) diff << "missed";
    if (static_cast<std::size_t>(info.channel_count) != want.channels.size())
    {
        diff << "channel count";
    }
    else
    {
        for (int32_t i = 0; i < info.channel_count; ++i)
        {
            tmdq_channel_series s{};
            s.struct_size = sizeof(s);
            if (tmdq_stream_result_channel(r, i, &s) != TMDQ_OK) { diff << "channel call"; continue; }
            const ProcessedChannelSeries& w = want.channels[static_cast<std::size_t>(i)];
            if (std::string(s.name_utf8) != w.name) diff << "channel name " + std::to_string(i);
            if (s.word_index != w.word) diff << "channel word " + std::to_string(i);
            if (!sameDoubles(s.values, n, w.values)) diff << "channel values " + std::to_string(i);
        }
    }
    return diff.join(", ");
}


// ---------------------------------------------------------------------------
// Calibration
// ---------------------------------------------------------------------------

/// A sink for the direct extraction: remembers the end.
class CalSink final : public ProcessingEvents
{
public:
    void logMessage(const std::string&) override {}
    void errorOccurred(const std::string&) override {}
    void processingFinished(bool ok) override { m_ok = ok; }
    bool ok() const { return m_ok; }
private:
    std::atomic<bool> m_ok{false};
};

struct DirectCalibration
{
    std::vector<CalibrationChannelResult> results;
    std::string summary, error, report;
    double      seconds = 0.0;
    bool        clipIgnored = false;
    bool        ok = false;
    bool        built = false;   ///< the request could be built (the sync text was readable)
};

inline std::vector<double> shippedSteps()
{
    std::vector<StepDefinition> steps;
    std::string error;
    REQUIRE_MESSAGE(StepDetector::parseStepConfig(repoFile("settings/rcvr_cals/default.toml"), steps, error), error);
    std::vector<double> db;
    for (const StepDefinition& s : steps)
        db.push_back(s.db);
    return db;
}

/// One calibration: what the dialog gathers, with the application root. Held in the core's own terms, and
/// given to the ABI as the wire struct, so a field the ABI forgets to carry shows up as a difference.
struct CalCase
{
    CalibrationInputs in;
    std::string       appRoot;

    explicit CalCase(const StepCal& f, std::vector<double> stepDb = shippedSteps())
        : appRoot(rootUtf8())
    {
        in.calFilename      = textfile::pathFromUtf8(f.path);
        in.timeChannelId    = f.timeId;
        in.pcmChannelId     = f.pcmId;
        in.syncPattern      = "FE6B2840";
        in.bitsInMinorFrame = 800;
        in.dataRateMbps     = 0.8;
        in.swapBytes        = true;
        in.numReceivers     = 16;
        in.receiverChannels = 3;
        for (double db : stepDb)
            in.steps.push_back({db});
    }
};

/// The extraction run directly through the core: the request built by the (separately tested)
/// buildCalibrationRequest, then CalibrationExtractor.
inline DirectCalibration runDirectCalibration(const CalCase& c)
{
    DirectCalibration out;
    CalibrationExtractor::Request req;
    std::string why;
    out.built = buildCalibrationRequest(c.in, c.appRoot, req, why);
    if (!out.built)
    {
        out.error = why;
        return out;
    }
    CalibrationExtractor extractor;
    auto sink = std::make_shared<CalSink>();
    extractor.start(req, sink);
    extractor.wait();
    out.ok          = sink->ok();
    out.results     = extractor.results();
    out.summary     = extractor.summary();
    out.error       = extractor.error();
    out.seconds     = extractor.recordingSeconds();
    out.clipIgnored = extractor.clipIgnored();
    out.report      = CalibrationExtractor::summarize(out.results, int(c.in.steps.size()));
    return out;
}

/// The wire form of a CalCase, with the text and the steps kept alive beside it.
struct CalRequest
{
    CalCase                 source;
    std::string             file, appRoot, pattern, mask, params;
    std::vector<double>     steps;
    tmdq_calibration_request req{};

    explicit CalRequest(const CalCase& c)
        : source(c), file(c.in.calFilename.string()), appRoot(c.appRoot), pattern(c.in.syncPattern),
          mask(c.in.syncMask), params(c.in.receiverParamsToml.string())
    {
        for (const StepDefinition& s : c.in.steps)
            steps.push_back(s.db);
        req.struct_size               = sizeof(req);
        req.time_channel_id           = c.in.timeChannelId;
        req.cal_filename_utf8         = file.c_str();
        req.app_root_utf8             = appRoot.c_str();
        req.sync_pattern_utf8         = pattern.c_str();
        req.sync_mask_utf8            = mask.c_str();
        req.receiver_params_toml_utf8 = params.c_str();
        req.step_db                   = steps.data();
        req.data_rate_mbps            = c.in.dataRateMbps;
        req.clip_start_sec            = c.in.clipStartSec;
        req.clip_end_sec              = c.in.clipEndSec;
        req.pcm_channel_id            = c.in.pcmChannelId;
        req.bits_in_minor_frame       = c.in.bitsInMinorFrame;
        req.randomized                = c.in.randomized ? 1 : 0;
        req.inverted                  = c.in.inverted ? 1 : 0;
        req.swap_bytes                = c.in.swapBytes ? 1 : 0;
        req.num_receivers             = c.in.numReceivers;
        req.receiver_channels         = c.in.receiverChannels;
        req.step_count                = static_cast<int32_t>(steps.size());
    }
};

struct CalGuard
{
    tmdq_calibration* handle = nullptr;
    ~CalGuard() { tmdq_calibration_free(handle); }
};

/// What differs between a finished calibration and the core's own run; empty if nothing.
inline std::string calibrationDifferences(const tmdq_calibration* cal, const DirectCalibration& want)
{
    Diff diff;
    tmdq_calibration_info info{};
    info.struct_size = sizeof(info);
    if (tmdq_calibration_result(cal, &info) != TMDQ_OK)
        return "tmdq_calibration_result failed";
    if (std::string(info.summary_utf8) != want.summary) diff << "summary";
    if (std::string(info.error_utf8) != want.error) diff << "error";
    if (std::string(info.report_utf8) != want.report) diff << "report";
    if (info.recording_seconds != want.seconds) diff << "recording seconds";
    if ((info.clip_ignored != 0) != want.clipIgnored) diff << "clip ignored";
    if (std::size_t(info.channel_count) != want.results.size())
        return diff.join(", ") + (diff.empty() ? "" : ", ") + "channel count";
    for (int32_t i = 0; i < info.channel_count; ++i)
    {
        tmdq_calibration_channel ch{};
        ch.struct_size = sizeof(ch);
        if (tmdq_calibration_channel_result(cal, i, &ch) != TMDQ_OK) { diff << "channel call"; continue; }
        const CalibrationChannelResult& r = want.results[std::size_t(i)];
        const std::string at = std::to_string(i) + ":";
        if (std::string(ch.name_utf8) != r.name) diff << at + "name";
        if (ch.word_index != r.word) diff << at + "word";
        if ((ch.valid != 0) != r.profile.valid) diff << at + "valid";
        if ((ch.had_data != 0) != r.hadData) diff << at + "had data";
        if (ch.outcome != int(r.outcome)) diff << at + "outcome";
        if (ch.detected_plateaus != r.detectedPlateaus) diff << at + "plateaus";
        if ((ch.extra_plateaus != 0) != r.extraPlateaus) diff << at + "extra";
        if (ch.edge_threshold != r.edgeThreshold) diff << at + "threshold";
        if ((ch.threshold_relaxed != 0) != r.thresholdRelaxed) diff << at + "relaxed";
        if ((ch.saturated != 0) != r.saturated) diff << at + "saturated";
        if (ch.sweep_levels != r.sweepLevels) diff << at + "sweep levels";
        if (std::size_t(ch.point_count) != r.profile.points.size()) { diff << at + "point count"; }
        else
            for (int32_t p = 0; p < ch.point_count; ++p)
                if (ch.points[p].raw_avg != r.profile.points[std::size_t(p)].rawAvg ||
                    ch.points[p].true_db != r.profile.points[std::size_t(p)].trueDb)
                    diff << at + "point " + std::to_string(p);
        if (std::size_t(ch.plateau_level_count) != r.plateauLevels.size() ||
            !sameDoubles(ch.plateau_levels, std::size_t(ch.plateau_level_count), r.plateauLevels)) diff << at + "levels";
        if (std::size_t(ch.unresolved_count) != r.unresolvedDb.size() ||
            !sameDoubles(ch.unresolved_db, std::size_t(ch.unresolved_count), r.unresolvedDb)) diff << at + "unresolved";
    }
    return diff.join(", ");
}

/// Runs a calibration request to its end and returns whether it finished successfully.
inline bool calibrateToTheEnd(CalRequest& request, Seen& seen, CalGuard& guard)
{
    const tmdq_callbacks callbacks = seen.callbacks();
    if (tmdq_calibration_start(&request.req, &callbacks, &guard.handle) != TMDQ_OK)
        return false;
    if (!seen.waitFinished(kTimeoutMs))
        return false;
    tmdq_calibration_wait(guard.handle);
    return seen.finishedSuccess == 1;
}

/// Whether two direct runs say the same thing (so a setting that changes nothing is noticed).
inline bool sameCalibration(const DirectCalibration& a, const DirectCalibration& b)
{
    if (a.ok != b.ok || a.summary != b.summary || a.error != b.error || a.report != b.report ||
        a.clipIgnored != b.clipIgnored || a.seconds != b.seconds || a.results.size() != b.results.size())
        return false;
    for (std::size_t i = 0; i < a.results.size(); ++i)
    {
        const CalibrationChannelResult& x = a.results[i];
        const CalibrationChannelResult& y = b.results[i];
        if (x.name != y.name || x.word != y.word || x.profile.valid != y.profile.valid ||
            x.hadData != y.hadData || x.detectedPlateaus != y.detectedPlateaus ||
            x.plateauLevels != y.plateauLevels || x.edgeThreshold != y.edgeThreshold ||
            x.profile.points.size() != y.profile.points.size())
            return false;
        for (std::size_t p = 0; p < x.profile.points.size(); ++p)
            if (x.profile.points[p].rawAvg != y.profile.points[p].rawAvg)
                return false;
    }
    return true;
}

} // namespace

#endif // TEST_ABI_SUPPORT_H
