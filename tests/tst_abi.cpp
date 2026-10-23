#include "tst_abi.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QtTest>

#include "../native/tmdq_core/abi_internal.h"
#include "../native/tmdq_core/tmdq_abi.h"
#include "chapter10reader.h"
#include "constants.h"
#include "pipelinerunner.h"
#include "processedstreamdata.h"
#include "qtbridge.h"
#include "streamjobbuilder.h"

namespace {

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

QString projectRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();   // tests/
    dir.cdUp();   // project root
    return dir.absolutePath();
}

QString dataPath(const QString& name) { return projectRoot() + "/tests/data/" + name; }

/// The small committed recording the pipeline tests use: one PCM channel, 800-bit frames, sync FE6B2840.
struct StepCal
{
    std::string path;
    int         timeId = -1;
    int         pcmId  = -1;
    bool        ok = false;
};

StepCal loadStepCal()
{
    StepCal f;
    const QString path = dataPath("test files/step_cal, example.ch10");
    if (!QFileInfo::exists(path))
        return f;
    Chapter10Reader reader;
    if (!reader.loadChannels(toFsPath(path)))
        return f;
    f.path   = path.toStdString();
    f.timeId = reader.getCurrentTimeChannelID();
    f.pcmId  = reader.getFirstPCMChannelID();
    f.ok     = f.timeId >= 0 && f.pcmId >= 0;
    return f;
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
    int         periodIndex = UIConstants::kDefaultSamplePeriodIndex;
    int         polarityIndex = UIConstants::kDefaultPolarityIndex;
    int         slopeIndex = UIConstants::kDefaultSlopeIndex;
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

Case lockCase(const StepCal& f)
{
    Case c;
    c.pcmId = f.pcmId;
    c.label = "lock stream";
    return c;
}

Case receiverCase(const StepCal& f)
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

/// Builds and runs @p cases over @p f exactly as MainViewModel + ProcessingCoordinator do, with no ABI.
std::map<int, ProcessedStreamData> runCore(const StepCal& f, int sourceId, const std::vector<Case>& cases,
                                           const std::string& appRoot, bool& ok, bool swapBytes = true)
{
    ProcessingParams base;
    base.filename      = toFsPath(QString::fromStdString(f.path));
    base.sourceId      = sourceId;
    base.timeChannelId = f.timeId;
    base.startSeconds  = 0;
    base.stopSeconds   = std::numeric_limits<uint64_t>::max();

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
        : cases(std::move(streams)), file(f.path), appRoot(projectRoot().toStdString())
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

/// Starts @p request, waits for the end, and says whether it ended successfully.
bool runToTheEnd(Request& request, Seen& seen)
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

bool sameStream(const ProcessedStreamData& a, const ProcessedStreamData& b)
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

bool sameDoubles(const double* a, std::size_t na, const std::vector<double>& b)
{
    return na == b.size() && (na == 0 || std::memcmp(a, b.data(), na * sizeof(double)) == 0);
}

/// What differs between a result the ABI delivered and the one the core made; empty if nothing.
QString differences(tmdq_stream_result* r, const ProcessedStreamData& want)
{
    tmdq_stream_info info{};
    info.struct_size = sizeof(info);
    if (tmdq_stream_result_info(r, &info) != TMDQ_OK)
        return "tmdq_stream_result_info failed";
    QStringList diff;
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
            if (std::string(s.name_utf8) != w.name) diff << "channel name " + QString::number(i);
            if (s.word_index != w.word) diff << "channel word " + QString::number(i);
            if (!sameDoubles(s.values, n, w.values)) diff << "channel values " + QString::number(i);
        }
    }
    return diff.join(", ");
}

} // namespace

// ---------------------------------------------------------------------------
// The shape of the contract
// ---------------------------------------------------------------------------

void TestAbi::theStructLayoutsArePinned()
{
    // What the managed side must reproduce, field by field (Pack = 8, natural alignment). An offset moving
    // here is an ABI break, so it is a deliberate edit to this test and a new revision, never a side effect.
    QCOMPARE(int(sizeof(tmdq_channel_info)), 24);
    QCOMPARE(int(offsetof(tmdq_channel_info, struct_size)), 0);
    QCOMPARE(int(offsetof(tmdq_channel_info, channel_id)), 4);
    QCOMPARE(int(offsetof(tmdq_channel_info, tmats_data_rate_bps)), 8);
    QCOMPARE(int(offsetof(tmdq_channel_info, name_utf8)), 16);

    QCOMPARE(int(sizeof(tmdq_cal_point)), 16);
    QCOMPARE(int(offsetof(tmdq_cal_point, true_db)), 8);

    QCOMPARE(int(sizeof(tmdq_cal_profile)), 24);
    QCOMPARE(int(offsetof(tmdq_cal_profile, word_index)), 8);
    QCOMPARE(int(offsetof(tmdq_cal_profile, point_count)), 12);
    QCOMPARE(int(offsetof(tmdq_cal_profile, valid)), 16);

    QCOMPARE(int(sizeof(tmdq_stream_config)), 104);
    QCOMPARE(int(offsetof(tmdq_stream_config, pcm_channel_id)), 4);
    QCOMPARE(int(offsetof(tmdq_stream_config, label_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_stream_config, sync_pattern_utf8)), 16);
    QCOMPARE(int(offsetof(tmdq_stream_config, sync_mask_utf8)), 24);
    QCOMPARE(int(offsetof(tmdq_stream_config, receiver_params_toml_utf8)), 32);
    QCOMPARE(int(offsetof(tmdq_stream_config, calibration)), 40);
    QCOMPARE(int(offsetof(tmdq_stream_config, data_rate_mbps)), 48);
    QCOMPARE(int(offsetof(tmdq_stream_config, scale_d_db_per_v)), 56);
    QCOMPARE(int(offsetof(tmdq_stream_config, mode)), 64);
    QCOMPARE(int(offsetof(tmdq_stream_config, bits_in_minor_frame)), 68);
    QCOMPARE(int(offsetof(tmdq_stream_config, randomized)), 72);
    QCOMPARE(int(offsetof(tmdq_stream_config, inverted)), 76);
    QCOMPARE(int(offsetof(tmdq_stream_config, sample_period_index)), 80);
    QCOMPARE(int(offsetof(tmdq_stream_config, polarity_index)), 84);
    QCOMPARE(int(offsetof(tmdq_stream_config, slope_index)), 88);
    QCOMPARE(int(offsetof(tmdq_stream_config, num_receivers)), 92);
    QCOMPARE(int(offsetof(tmdq_stream_config, receiver_channels)), 96);
    QCOMPARE(int(offsetof(tmdq_stream_config, calibration_count)), 100);

    QCOMPARE(int(sizeof(tmdq_run_request)), 48);
    QCOMPARE(int(offsetof(tmdq_run_request, time_channel_id)), 4);
    QCOMPARE(int(offsetof(tmdq_run_request, filename_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_run_request, app_root_utf8)), 16);
    QCOMPARE(int(offsetof(tmdq_run_request, streams)), 24);
    QCOMPARE(int(offsetof(tmdq_run_request, stream_count)), 32);
    QCOMPARE(int(offsetof(tmdq_run_request, source_id)), 36);
    QCOMPARE(int(offsetof(tmdq_run_request, swap_bytes)), 40);

    QCOMPARE(int(sizeof(tmdq_callbacks)), 56);
    QCOMPARE(int(offsetof(tmdq_callbacks, user)), 8);
    QCOMPARE(int(offsetof(tmdq_callbacks, log)), 16);
    QCOMPARE(int(offsetof(tmdq_callbacks, error)), 24);
    QCOMPARE(int(offsetof(tmdq_callbacks, progress)), 32);
    QCOMPARE(int(offsetof(tmdq_callbacks, stream_done)), 40);
    QCOMPARE(int(offsetof(tmdq_callbacks, finished)), 48);

    QCOMPARE(int(sizeof(tmdq_stream_info)), 64);
    QCOMPARE(int(offsetof(tmdq_stream_info, pcm_channel_id)), 4);
    QCOMPARE(int(offsetof(tmdq_stream_info, label_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_stream_info, times_sec)), 16);
    QCOMPARE(int(offsetof(tmdq_stream_info, lock_percent)), 24);
    QCOMPARE(int(offsetof(tmdq_stream_info, missed_frames)), 32);
    QCOMPARE(int(offsetof(tmdq_stream_info, job_index)), 40);
    QCOMPARE(int(offsetof(tmdq_stream_info, source_id)), 44);
    QCOMPARE(int(offsetof(tmdq_stream_info, mode)), 48);
    QCOMPARE(int(offsetof(tmdq_stream_info, sample_count)), 52);
    QCOMPARE(int(offsetof(tmdq_stream_info, channel_count)), 56);

    QCOMPARE(int(sizeof(tmdq_channel_series)), 24);
    QCOMPARE(int(offsetof(tmdq_channel_series, word_index)), 4);
    QCOMPARE(int(offsetof(tmdq_channel_series, name_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_channel_series, values)), 16);

    // And the library reports the same sizes, for the managed side to compare with Marshal.SizeOf.
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CHANNEL_INFO), 24);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CAL_POINT), 16);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CAL_PROFILE), 24);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_STREAM_CONFIG), 104);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_RUN_REQUEST), 48);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CALLBACKS), 56);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_STREAM_INFO), 64);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CHANNEL_SERIES), 24);
    QCOMPARE(tmdq_sizeof(0), 0);
    QCOMPARE(tmdq_sizeof(99), 0);

    // The spike's structs, which this revision leaves alone.
    QCOMPARE(tmdq_sizeof_lock_request(), 72);
    QCOMPARE(tmdq_sizeof_lock_result(), 40);
    QCOMPARE(tmdq_sizeof_file_info(), 136);

    // The numbers a host writes in its own copies of the enums.
    QCOMPARE(int(TMDQ_OK), 0);
    QCOMPARE(int(TMDQ_ERR_BADARG), 1);
    QCOMPARE(int(TMDQ_ERR_PREPARE), 2);
    QCOMPARE(int(TMDQ_ERR_PROCESS), 3);
    QCOMPARE(int(TMDQ_ERR_NOSAMPLES), 4);
    QCOMPARE(int(TMDQ_ERR_INTERNAL), 5);
    QCOMPARE(int(TMDQ_ERR_JOB), 6);
    QCOMPARE(int(TMDQ_STRUCT_CHANNEL_INFO), 1);
    QCOMPARE(int(TMDQ_STRUCT_CHANNEL_SERIES), 8);
    QCOMPARE(int(TMDQ_MODE_RECEIVER_CHANNEL_INFO), 0);
    QCOMPARE(int(TMDQ_MODE_FRAME_SYNC_LOCK), 1);

    // The values a host writes in its own enums.
    QCOMPARE(int(TMDQ_MODE_RECEIVER_CHANNEL_INFO), int(StreamMode::ReceiverChannelInfo));
    QCOMPARE(int(TMDQ_MODE_FRAME_SYNC_LOCK), int(StreamMode::FrameSyncLockStats));
}

void TestAbi::theVersionAndRevisionAreReported()
{
    QCOMPARE(tmdq_abi_version(), 1);
    QCOMPARE(tmdq_abi_revision(), 1);
    QCOMPARE(int(TMDQ_ABI_VERSION), tmdq_abi_version());
    QCOMPARE(int(TMDQ_ABI_REVISION), tmdq_abi_revision());
    QVERIFY(tmdq_last_error() != nullptr);
}

void TestAbi::aStructFromANewerHostIsAcceptedAndOneFromAnOlderHostIsRefused()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    // A host built against a LATER revision passes a longer struct: the fields this library knows are
    // read, and the rest is ignored. Here the request, its stream and the callbacks are all longer.
    struct BigStream { tmdq_stream_config c; char extra[24]; };
    struct BigRequest { tmdq_run_request r; char extra[40]; };
    struct BigCallbacks { tmdq_callbacks c; char extra[16]; };

    const Case c = lockCase(f);
    BigStream stream{};
    const tmdq_stream_config wired = c.wire();
    std::memcpy(&stream.c, &wired, sizeof(tmdq_stream_config));
    stream.c.struct_size = sizeof(BigStream);
    const tmdq_stream_config* streams[] = { &stream.c };

    Seen seen;
    BigCallbacks callbacks{};
    callbacks.c = seen.callbacks();
    callbacks.c.struct_size = sizeof(BigCallbacks);

    const std::string root = projectRoot().toStdString();
    BigRequest request{};
    request.r.struct_size     = sizeof(BigRequest);
    request.r.time_channel_id = f.timeId;
    request.r.filename_utf8   = f.path.c_str();
    request.r.app_root_utf8   = root.c_str();
    request.r.streams         = streams;
    request.r.stream_count    = 1;
    request.r.swap_bytes      = 1;

    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&request.r, &callbacks.c, &run), int(TMDQ_OK));
    RunGuard guard{run};
    QVERIFY2(seen.waitFinished(kTimeoutMs), seen.joined().c_str());
    QCOMPARE(seen.finishedSuccess, 1);

    // A host built against an EARLIER revision passes a shorter struct, which this library cannot read
    // safely: it is refused, with the reason, not guessed at.
    tmdq_run_request shortRequest = request.r;
    shortRequest.struct_size = sizeof(tmdq_run_request) - 1;
    tmdq_run* none = nullptr;
    QCOMPARE(tmdq_run_start(&shortRequest, &callbacks.c, &none), int(TMDQ_ERR_BADARG));
    QVERIFY(none == nullptr);
    QVERIFY(std::string(tmdq_last_error()).find("struct_size") != std::string::npos);

    tmdq_stream_config shortStream = stream.c;
    shortStream.struct_size = sizeof(tmdq_stream_config) - 1;
    const tmdq_stream_config* shortStreams[] = { &shortStream };
    tmdq_run_request withShortStream = request.r;
    withShortStream.struct_size = sizeof(tmdq_run_request);
    withShortStream.streams = shortStreams;
    QCOMPARE(tmdq_run_start(&withShortStream, &callbacks.c, &none), int(TMDQ_ERR_BADARG));
    QVERIFY(std::string(tmdq_last_error()).find("stream 0") != std::string::npos);

    tmdq_callbacks shortCallbacks = seen.callbacks();
    shortCallbacks.struct_size = sizeof(tmdq_callbacks) - 1;
    withShortStream.streams = streams;
    QCOMPARE(tmdq_run_start(&withShortStream, &shortCallbacks, &none), int(TMDQ_ERR_BADARG));

    // A struct the LIBRARY fills in: a longer one is written no further than the library's own size,
    // and what lies beyond is left alone; a shorter one is refused.
    tmdq_file* file = nullptr;
    QCOMPARE(tmdq_file_open(f.path.c_str(), &file), int(TMDQ_OK));
    struct BigInfo { tmdq_channel_info i; unsigned char tail[16]; };
    BigInfo big;
    std::memset(&big, 0xAB, sizeof(big));
    big.i.struct_size = sizeof(BigInfo);
    QCOMPARE(tmdq_file_pcm_channel(file, 0, &big.i), int(TMDQ_OK));
    QCOMPARE(big.i.channel_id, f.pcmId);
    for (unsigned char b : big.tail)
        QCOMPARE(int(b), 0xAB);
    tmdq_channel_info small{};
    small.struct_size = sizeof(small) - 1;
    QCOMPARE(tmdq_file_pcm_channel(file, 0, &small), int(TMDQ_ERR_BADARG));
    tmdq_file_close(file);
}

// ---------------------------------------------------------------------------
// The file
// ---------------------------------------------------------------------------

void TestAbi::aFileListsItsChannelsLikeTheReader()
{
    for (const QString& name : { QString("test files/prn_testfile, sample.ch10"), QString("test files/step_cal, example.ch10") })
    {
        const QString path = dataPath(name);
        if (!QFileInfo::exists(path))
            QSKIP(qPrintable(name + " not available"));
        Chapter10Reader reader;
        QVERIFY(reader.loadChannels(toFsPath(path)));

        const std::string utf8 = path.toStdString();
        tmdq_file* file = nullptr;
        QCOMPARE(tmdq_file_open(utf8.c_str(), &file), int(TMDQ_OK));
        QVERIFY(file != nullptr);

        const auto pcm = reader.getPCMChannelList();
        const auto times = reader.getTimeChannelList();
        QCOMPARE(tmdq_file_pcm_channel_count(file), int(pcm.size()));
        QCOMPARE(tmdq_file_time_channel_count(file), int(times.size()));
        QVERIFY(pcm.size() >= 1);
        QVERIFY(times.size() >= 1);
        QCOMPARE(tmdq_file_default_time_channel(file), reader.getCurrentTimeChannelID());

        for (int i = 0; i < int(pcm.size()); ++i)
        {
            tmdq_channel_info info{};
            info.struct_size = sizeof(info);
            QCOMPARE(tmdq_file_pcm_channel(file, i, &info), int(TMDQ_OK));
            QCOMPARE(info.channel_id, pcm[std::size_t(i)].first);
            QCOMPARE(std::string(info.name_utf8), pcm[std::size_t(i)].second);
            QCOMPARE(info.tmats_data_rate_bps, reader.getTmatsDataRateBps(pcm[std::size_t(i)].first));
        }
        for (int i = 0; i < int(times.size()); ++i)
        {
            tmdq_channel_info info{};
            info.struct_size = sizeof(info);
            QCOMPARE(tmdq_file_time_channel(file, i, &info), int(TMDQ_OK));
            QCOMPARE(info.channel_id, times[std::size_t(i)].first);
            QCOMPARE(std::string(info.name_utf8), times[std::size_t(i)].second);
            QCOMPARE(info.tmats_data_rate_bps, 0.0);
        }
        tmdq_file_close(file);
    }

    // The step-cal recording declares an 800 kbit/s rate in its TMATS; that is not nothing.
    const QString stepCal = dataPath("test files/step_cal, example.ch10");
    if (QFileInfo::exists(stepCal))
    {
        const std::string utf8 = stepCal.toStdString();
        tmdq_file* file = nullptr;
        QCOMPARE(tmdq_file_open(utf8.c_str(), &file), int(TMDQ_OK));
        tmdq_channel_info info{};
        info.struct_size = sizeof(info);
        QCOMPARE(tmdq_file_pcm_channel(file, 0, &info), int(TMDQ_OK));
        QCOMPARE(info.tmats_data_rate_bps, 800000.0);
        tmdq_file_close(file);
    }
}

void TestAbi::aFileThatCannotBeOpenedSaysWhy()
{
    tmdq_file* file = reinterpret_cast<tmdq_file*>(std::uintptr_t{1});   // must be overwritten
    const QString missing = QDir::tempPath() + "/tst_abi_no_such_recording.ch10";
    const std::string utf8 = missing.toStdString();
    QCOMPARE(tmdq_file_open(utf8.c_str(), &file), int(TMDQ_ERR_PREPARE));
    QVERIFY(file == nullptr);
    QVERIFY2(std::string(tmdq_last_error()).size() > 5, tmdq_last_error());
    {
        // And the reason is the reader's own, word for word.
        Chapter10Reader reader;
        QVERIFY(!reader.loadChannels(toFsPath(missing)));
        QVERIFY(!reader.lastError().empty());
        QCOMPARE(std::string(tmdq_last_error()), reader.lastError());
    }

    // A file that exists but is not a recording.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString junk = dir.filePath("junk.ch10");
    QFile f(junk);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(4096, 'x'));
    f.close();
    const std::string junkUtf8 = junk.toStdString();
    QCOMPARE(tmdq_file_open(junkUtf8.c_str(), &file), int(TMDQ_ERR_PREPARE));
    QVERIFY(file == nullptr);
    QVERIFY(!std::string(tmdq_last_error()).empty());

    // The error belongs to the calling thread: another thread's success does not erase it.
    tmdq_file* other = nullptr;
    const std::string good = dataPath("test files/step_cal, example.ch10").toStdString();
    if (QFileInfo::exists(QString::fromStdString(good)))
    {
        std::thread t([&] { tmdq_file_open(good.c_str(), &other); });
        t.join();
        QVERIFY(!std::string(tmdq_last_error()).empty());
        tmdq_file_close(other);
    }
}

void TestAbi::badArgumentsToTheFileFunctionsAreRefused()
{
    tmdq_file* file = reinterpret_cast<tmdq_file*>(std::uintptr_t{1});   // must be overwritten, even by a refusal
    QCOMPARE(tmdq_file_open(nullptr, &file), int(TMDQ_ERR_BADARG));
    QVERIFY(file == nullptr);
    QCOMPARE(tmdq_file_open("whatever", nullptr), int(TMDQ_ERR_BADARG));

    tmdq_file_close(nullptr);   // safe
    QCOMPARE(tmdq_file_time_channel_count(nullptr), -1);
    QCOMPARE(tmdq_file_pcm_channel_count(nullptr), -1);
    QCOMPARE(tmdq_file_default_time_channel(nullptr), -1);
    tmdq_channel_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_file_pcm_channel(nullptr, 0, &info), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_file_time_channel(nullptr, 0, &info), int(TMDQ_ERR_BADARG));

    const std::string good = dataPath("test files/step_cal, example.ch10").toStdString();
    if (!QFileInfo::exists(QString::fromStdString(good)))
        QSKIP("step_cal fixture not available");
    QCOMPARE(tmdq_file_open(good.c_str(), &file), int(TMDQ_OK));
    const int n = tmdq_file_pcm_channel_count(file);
    QCOMPARE(tmdq_file_pcm_channel(file, n, &info), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_file_pcm_channel(file, -1, &info), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_file_pcm_channel(file, 0, nullptr), int(TMDQ_ERR_BADARG));
    const int t = tmdq_file_time_channel_count(file);
    QCOMPARE(tmdq_file_time_channel(file, t, &info), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_file_time_channel(file, -1, &info), int(TMDQ_ERR_BADARG));
    tmdq_file_close(file);
}

// ---------------------------------------------------------------------------
// Runs
// ---------------------------------------------------------------------------

void TestAbi::aRunOfSeveralStreamsMatchesWhatTheCoreMakes()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    // Two streams over the one channel, one in each mode, with the source id the host chose.
    const std::vector<Case> cases = { lockCase(f), receiverCase(f) };
    bool coreOk = false;
    const auto want = runCore(f, 7, cases, projectRoot().toStdString(), coreOk);
    QVERIFY(coreOk);
    QCOMPARE(want.size(), std::size_t{2});
    QVERIFY(want.at(0).hasSamples());
    QVERIFY(want.at(1).hasSamples());
    QVERIFY(!want.at(1).channels.empty());   // the receiver stream really has series to compare

    Request request(f, cases, 7);
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_OK));
    QVERIFY(run != nullptr);
    RunGuard guard{run};
    QVERIFY2(seen.waitFinished(kTimeoutMs), seen.joined().c_str());
    tmdq_run_wait(run);

    QCOMPARE(seen.finishedSuccess, 1);
    const auto got = seen.byJob();
    QCOMPARE(got.size(), std::size_t{2});
    for (int job = 0; job < 2; ++job)
    {
        const QString diff = differences(got.at(job), want.at(job));
        QVERIFY2(diff.isEmpty(), qPrintable("job " + QString::number(job) + " differs in: " + diff));
    }
    // The source id and the labels came through the request.
    tmdq_stream_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_stream_result_info(got.at(0), &info), int(TMDQ_OK));
    QCOMPARE(info.source_id, 7);
    QCOMPARE(std::string(info.label_utf8), std::string("lock stream"));
    QCOMPARE(tmdq_stream_result_info(got.at(1), &info), int(TMDQ_OK));
    QCOMPARE(std::string(info.label_utf8), std::string("receiver stream"));
    QCOMPARE(info.mode, int(TMDQ_MODE_RECEIVER_CHANNEL_INFO));
}

void TestAbi::aRunReportsInTheDocumentedOrder()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    Request request(f, { lockCase(f), receiverCase(f) });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    const std::thread::id caller = std::this_thread::get_id();
    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_OK));
    RunGuard guard{run};
    QVERIFY2(seen.waitFinished(kTimeoutMs), seen.joined().c_str());
    tmdq_run_wait(run);
    QCOMPARE(tmdq_run_running(run), 0);

    const std::vector<std::string> events = seen.snapshot();
    QVERIFY(!events.empty());
    QCOMPARE(events.back(), std::string("finished:1"));           // LAST
    QCOMPARE(seen.count("finished:1"), 1);                         // and once
    QCOMPARE(seen.finishedCount, 1);
    QVERIFY(!seen.any("error:"));

    // Both streams are delivered, each once, before the end, and not from the thread that started the run.
    QCOMPARE(seen.count("stream:0"), 1);
    QCOMPARE(seen.count("stream:1"), 1);
    QVERIFY(seen.streamThreads.at(0) != caller);
    QVERIFY(seen.streamThreads.at(1) != caller);
    QVERIFY(seen.streamThreads.at(0) != seen.streamThreads.at(1));   // each on its own stream's thread
    QVERIFY(seen.finishedThread != caller);

    // Progress comes from one thread only (the reader's), reaches 100 once, and never goes backwards.
    QCOMPARE(seen.progressThreads.size(), std::size_t{1});
    QVERIFY(*seen.progressThreads.begin() != caller);
    QCOMPARE(seen.count("progress:100"), 1);
    int last = -1;
    for (const std::string& e : events)
    {
        if (e.rfind("progress:", 0) != 0) continue;
        const int p = std::stoi(e.substr(9));
        QVERIFY(p >= last);
        last = p;
    }
    QCOMPARE(last, 100);

    // The lines the job builder produced reach `log` first, on the calling thread (before start returned),
    // and the workers' lines follow.
    QVERIFY2(seen.any("log:"), events.front().c_str());
}

void TestAbi::aNonLinearProfileReachesTheJob()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    // The receiver stream with a profile on one word, and without. The default word map reads words 1..3;
    // a profile on word 1 changes that word's calibrated values and nothing else.
    Case plain = receiverCase(f);
    Case profiled = receiverCase(f);
    profiled.calibration[1] = { {0.0, -10.0}, {1000.0, 0.0}, {30000.0, 40.0}, {65535.0, 60.0} };

    bool coreOk = false;
    const auto want = runCore(f, 0, { plain, profiled }, projectRoot().toStdString(), coreOk);
    QVERIFY(coreOk);
    const ProcessedStreamData& a = want.at(0);
    const ProcessedStreamData& b = want.at(1);
    QCOMPARE(a.channels.size(), b.channels.size());
    bool someDiffer = false, someSame = false;
    for (std::size_t i = 0; i < a.channels.size(); ++i)
        (a.channels[i].values == b.channels[i].values ? someSame : someDiffer) = true;
    QVERIFY2(someDiffer && someSame, "the fixture should be changed by the profile on one word and not on the others");

    Request request(f, { plain, profiled });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_OK));
    RunGuard guard{run};
    QVERIFY2(seen.waitFinished(kTimeoutMs), seen.joined().c_str());
    tmdq_run_wait(run);
    const auto got = seen.byJob();
    QCOMPARE(got.size(), std::size_t{2});
    QVERIFY2(differences(got.at(0), a).isEmpty(), qPrintable(differences(got.at(0), a)));
    QVERIFY2(differences(got.at(1), b).isEmpty(), qPrintable(differences(got.at(1), b)));

    // An invalid profile is ignored: the word is calibrated linearly, as without one.
    Request ignoredRequest(f, { profiled });
    tmdq_cal_profile ignored = ignoredRequest.configs[0].calibration[0];
    ignored.valid = 0;
    ignoredRequest.configs[0].calibration = &ignored;
    Seen seen2;
    const tmdq_callbacks callbacks2 = seen2.callbacks();
    tmdq_run* run2 = nullptr;
    QCOMPARE(tmdq_run_start(&ignoredRequest.req, &callbacks2, &run2), int(TMDQ_OK));
    RunGuard guard2{run2};
    QVERIFY2(seen2.waitFinished(kTimeoutMs), seen2.joined().c_str());
    tmdq_run_wait(run2);
    QVERIFY2(differences(seen2.byJob().at(0), a).isEmpty(), "an invalid profile must leave the word linear");
}

void TestAbi::settingsTheCoreRejectsAreRefusedWithTheCoresMessage()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    Case bad = lockCase(f);
    bad.pattern = "not hex";

    // What the core says about it, directly.
    ProcessingParams base;
    base.filename = toFsPath(QString::fromStdString(f.path));
    base.timeChannelId = f.timeId;
    StreamJobContext ctx;
    ctx.appRoot = projectRoot().toStdString();
    StreamJob job;
    StreamJobReport report;
    QVERIFY(!buildStreamJob(bad.core(), base, ctx, job, report));
    QVERIFY(!report.error.empty());

    Request request(f, { lockCase(f), bad });   // the second stream is the bad one
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = reinterpret_cast<tmdq_run*>(std::uintptr_t{1});
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_ERR_JOB));
    QVERIFY(run == nullptr);
    QCOMPARE(std::string(tmdq_last_error()), report.error);
    // Nothing was started, so nothing but `log` can have been called.
    QVERIFY(!seen.any("error:"));
    QVERIFY(!seen.any("progress:"));
    QVERIFY(!seen.any("stream:"));
    QVERIFY(!seen.any("finished:"));
}

void TestAbi::aRecordingThatCannotBeUsedFailsToPrepare()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    // A file that is not there.
    StepCal missing = f;
    missing.path = (QDir::tempPath() + "/tst_abi_no_such_recording.ch10").toStdString();
    Request request(missing, { lockCase(f) });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = reinterpret_cast<tmdq_run*>(std::uintptr_t{1});
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_ERR_PREPARE));
    QVERIFY(run == nullptr);
    QVERIFY(!std::string(tmdq_last_error()).empty());
    QVERIFY(!seen.any("finished:"));

    // A channel the recording does not have.
    Case ghost = lockCase(f);
    ghost.pcmId = f.pcmId + 1000;
    Request ghostRequest(f, { ghost });
    QCOMPARE(tmdq_run_start(&ghostRequest.req, &callbacks, &run), int(TMDQ_ERR_PREPARE));
    QVERIFY(run == nullptr);
    QVERIFY(!std::string(tmdq_last_error()).empty());
}

void TestAbi::badArgumentsToTheRunFunctionsAreRefused()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    Request good(f, { lockCase(f) });

    QCOMPARE(tmdq_run_start(nullptr, &callbacks, &run), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_run_start(&good.req, &callbacks, nullptr), int(TMDQ_ERR_BADARG));

    tmdq_run_request r = good.req;
    r.filename_utf8 = nullptr;
    QCOMPARE(tmdq_run_start(&r, &callbacks, &run), int(TMDQ_ERR_BADARG));
    r = good.req;
    r.filename_utf8 = "";
    QCOMPARE(tmdq_run_start(&r, &callbacks, &run), int(TMDQ_ERR_BADARG));
    r = good.req;
    r.time_channel_id = -1;
    QCOMPARE(tmdq_run_start(&r, &callbacks, &run), int(TMDQ_ERR_BADARG));
    QCOMPARE(std::string(tmdq_last_error()), std::string("Invalid time channel."));
    r = good.req;
    r.stream_count = 0;
    QCOMPARE(tmdq_run_start(&r, &callbacks, &run), int(TMDQ_ERR_BADARG));
    QCOMPARE(std::string(tmdq_last_error()), std::string("No streams selected for processing. Configure streams first."));
    r = good.req;
    r.streams = nullptr;
    QCOMPARE(tmdq_run_start(&r, &callbacks, &run), int(TMDQ_ERR_BADARG));

    // A null stream pointer, an unknown mode, a calibration count with no array, a profile with no points.
    const tmdq_stream_config* nullStream[] = { nullptr };
    r = good.req;
    r.streams = nullStream;
    QCOMPARE(tmdq_run_start(&r, &callbacks, &run), int(TMDQ_ERR_BADARG));

    tmdq_stream_config c = good.configs[0];
    const tmdq_stream_config* one[] = { &c };
    r = good.req;
    r.streams = one;
    c.mode = 9;
    QCOMPARE(tmdq_run_start(&r, &callbacks, &run), int(TMDQ_ERR_BADARG));
    QVERIFY(std::string(tmdq_last_error()).find("mode") != std::string::npos);
    c = good.configs[0];
    c.calibration_count = 2;
    c.calibration = nullptr;
    QCOMPARE(tmdq_run_start(&r, &callbacks, &run), int(TMDQ_ERR_BADARG));
    c = good.configs[0];
    c.calibration_count = -1;
    QCOMPARE(tmdq_run_start(&r, &callbacks, &run), int(TMDQ_ERR_BADARG));
    c = good.configs[0];
    tmdq_cal_profile profile{};
    profile.point_count = 3;
    profile.points = nullptr;
    c.calibration = &profile;
    c.calibration_count = 1;
    QCOMPARE(tmdq_run_start(&r, &callbacks, &run), int(TMDQ_ERR_BADARG));
    QVERIFY(run == nullptr);
    QVERIFY(!seen.any("finished:"));

    // The remaining functions tolerate NULL.
    tmdq_run_cancel(nullptr);
    QCOMPARE(tmdq_run_running(nullptr), 0);
    tmdq_run_wait(nullptr);
    tmdq_run_free(nullptr);
    tmdq_stream_result_free(nullptr);
    tmdq_stream_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_stream_result_info(nullptr, &info), int(TMDQ_ERR_BADARG));
    tmdq_channel_series series{};
    series.struct_size = sizeof(series);
    QCOMPARE(tmdq_stream_result_channel(nullptr, 0, &series), int(TMDQ_ERR_BADARG));

    // A run with no callbacks at all is allowed: it just runs.
    tmdq_run* quiet = nullptr;
    QCOMPARE(tmdq_run_start(&good.req, nullptr, &quiet), int(TMDQ_OK));
    QVERIFY(quiet != nullptr);
    tmdq_run_wait(quiet);
    QCOMPARE(tmdq_run_running(quiet), 0);
    tmdq_run_free(quiet);
}

void TestAbi::cancellingEndsARunUnsuccessfully()
{
    const QString path = dataPath("test files/prn_testfile, sample.ch10");
    if (!QFileInfo::exists(path))
        QSKIP("PRN sample fixture not available");
    const std::string utf8 = path.toStdString();
    tmdq_file* file = nullptr;
    QCOMPARE(tmdq_file_open(utf8.c_str(), &file), int(TMDQ_OK));
    StepCal prn;
    prn.path   = utf8;
    prn.timeId = tmdq_file_default_time_channel(file);
    tmdq_channel_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_file_pcm_channel(file, 0, &info), int(TMDQ_OK));
    prn.pcmId = info.channel_id;
    prn.ok = true;
    tmdq_file_close(file);

    Case c;
    c.pcmId   = prn.pcmId;
    c.label   = "prn";
    c.pattern = "334AABBF";
    c.bits    = 32767;
    Request request(prn, { c });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_OK));
    RunGuard guard{run};
    QCOMPARE(tmdq_run_running(run), 1);
    tmdq_run_cancel(run);
    tmdq_run_cancel(run);   // twice is as good as once
    QVERIFY2(seen.waitFinished(kTimeoutMs), seen.joined().c_str());
    tmdq_run_wait(run);

    QCOMPARE(seen.finishedCount, 1);
    QCOMPARE(seen.finishedSuccess, 0);
    QVERIFY2(!seen.any("stream:"), seen.joined().c_str());   // a cancelled run delivers nothing
    QCOMPARE(seen.snapshot().back(), std::string("finished:0"));
    QCOMPARE(tmdq_run_running(run), 0);
}

void TestAbi::freeingARunInFlightCancelsAndWaits()
{
    const QString path = dataPath("test files/prn_testfile, sample.ch10");
    if (!QFileInfo::exists(path))
        QSKIP("PRN sample fixture not available");
    const std::string utf8 = path.toStdString();
    tmdq_file* file = nullptr;
    QCOMPARE(tmdq_file_open(utf8.c_str(), &file), int(TMDQ_OK));
    StepCal prn;
    prn.path   = utf8;
    prn.timeId = tmdq_file_default_time_channel(file);
    tmdq_channel_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_file_pcm_channel(file, 0, &info), int(TMDQ_OK));
    prn.pcmId = info.channel_id;
    tmdq_file_close(file);

    Case c;
    c.pcmId   = prn.pcmId;
    c.pattern = "334AABBF";
    c.bits    = 32767;
    Request request(prn, { c });
    auto seen = std::make_unique<Seen>();
    const tmdq_callbacks callbacks = seen->callbacks();
    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_OK));
    tmdq_run_free(run);   // no cancel, no wait: free does both

    // Every thread has ended by the time free returns, so `finished` has been delivered, once, last, and
    // the callback target can be destroyed straight away without a callback landing on it.
    QCOMPARE(seen->finishedCount, 1);
    QCOMPARE(seen->finishedSuccess, 0);
    QCOMPARE(seen->snapshot().back(), std::string("finished:0"));
    seen.reset();
}

void TestAbi::aResultOutlivesItsRunAndIsFreedFromAnyThread()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    Request request(f, { receiverCase(f) });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_OK));
    QVERIFY2(seen.waitFinished(kTimeoutMs), seen.joined().c_str());
    tmdq_run_free(run);   // the run is gone

    QCOMPARE(seen.results.size(), std::size_t{1});
    tmdq_stream_result* result = seen.results[0];
    tmdq_stream_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_stream_result_info(result, &info), int(TMDQ_OK));
    QVERIFY(info.sample_count > 0);
    QVERIFY(info.channel_count > 0);
    // The views are still good: read every value, then free the result on a different thread.
    double sum = 0.0;
    for (int i = 0; i < info.sample_count; ++i)
        sum += info.times_sec[i];
    tmdq_channel_series series{};
    series.struct_size = sizeof(series);
    QCOMPARE(tmdq_stream_result_channel(result, info.channel_count - 1, &series), int(TMDQ_OK));
    for (int i = 0; i < info.sample_count; ++i)
        sum += series.values[i];
    QVERIFY(sum == sum);   // not NaN: they were all readable
    QCOMPARE(tmdq_stream_result_channel(result, info.channel_count, &series), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_stream_result_channel(result, -1, &series), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_stream_result_channel(result, 0, nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_stream_result_info(result, nullptr), int(TMDQ_ERR_BADARG));

    seen.results.clear();
    std::thread([result] { tmdq_stream_result_free(result); }).join();
}

void TestAbi::aCallbackThatThrowsDoesNotBreakTheRun()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    Request request(f, { lockCase(f), receiverCase(f) });
    Seen seen;
    seen.throwing = true;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_OK));
    RunGuard guard{run};
    QVERIFY2(seen.waitFinished(kTimeoutMs), seen.joined().c_str());
    tmdq_run_wait(run);

    // Every callback threw, and the run neither died nor stopped reporting.
    QCOMPARE(seen.finishedCount, 1);
    QCOMPARE(seen.finishedSuccess, 1);
    QCOMPARE(seen.count("stream:0"), 1);
    QCOMPARE(seen.count("stream:1"), 1);
}

void TestAbi::twoRunsAtOnceAgreeWithOneAlone()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    bool coreOk = false;
    const auto want = runCore(f, 0, { lockCase(f), receiverCase(f) }, projectRoot().toStdString(), coreOk);
    QVERIFY(coreOk);

    Request a(f, { lockCase(f), receiverCase(f) });
    Request b(f, { lockCase(f), receiverCase(f) });
    Seen seenA, seenB;
    const tmdq_callbacks ca = seenA.callbacks();
    const tmdq_callbacks cb = seenB.callbacks();
    tmdq_run* runA = nullptr;
    tmdq_run* runB = nullptr;
    QCOMPARE(tmdq_run_start(&a.req, &ca, &runA), int(TMDQ_OK));
    RunGuard guardA{runA};
    QCOMPARE(tmdq_run_start(&b.req, &cb, &runB), int(TMDQ_OK));
    RunGuard guardB{runB};
    QVERIFY2(seenA.waitFinished(kTimeoutMs), seenA.joined().c_str());
    QVERIFY2(seenB.waitFinished(kTimeoutMs), seenB.joined().c_str());
    tmdq_run_wait(runA);
    tmdq_run_wait(runB);

    for (Seen* seen : { &seenA, &seenB })
    {
        QCOMPARE(seen->finishedSuccess, 1);
        const auto got = seen->byJob();
        QCOMPARE(got.size(), std::size_t{2});
        for (int job = 0; job < 2; ++job)
        {
            const QString diff = differences(got.at(job), want.at(job));
            QVERIFY2(diff.isEmpty(), qPrintable("job " + QString::number(job) + " differs in: " + diff));
        }
    }
}

void TestAbi::aSuccessfulCallClearsTheLastError()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    tmdq_file* file = nullptr;
    QCOMPARE(tmdq_file_open("no-such-file.ch10", &file), int(TMDQ_ERR_PREPARE));
    QVERIFY(!std::string(tmdq_last_error()).empty());
    QCOMPARE(tmdq_file_open(f.path.c_str(), &file), int(TMDQ_OK));
    QCOMPARE(std::string(tmdq_last_error()), std::string());   // the success left no stale message behind

    tmdq_channel_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_file_pcm_channel(file, 99, &info), int(TMDQ_ERR_BADARG));
    QVERIFY(!std::string(tmdq_last_error()).empty());
    QCOMPARE(tmdq_file_pcm_channel(file, 0, &info), int(TMDQ_OK));
    QCOMPARE(std::string(tmdq_last_error()), std::string());
    QCOMPARE(tmdq_file_time_channel(file, 99, &info), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_file_time_channel(file, 0, &info), int(TMDQ_OK));
    QCOMPARE(std::string(tmdq_last_error()), std::string());
    tmdq_file_close(file);

    Request request(f, { lockCase(f) });
    Seen seen;
    tmdq_run_request bad = request.req;
    bad.stream_count = 0;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&bad, &callbacks, &run), int(TMDQ_ERR_BADARG));
    QVERIFY(!std::string(tmdq_last_error()).empty());
    QVERIFY(runToTheEnd(request, seen));
    QCOMPARE(std::string(tmdq_last_error()), std::string());

    tmdq_stream_info info2{};
    info2.struct_size = sizeof(info2);
    QCOMPARE(tmdq_stream_result_info(nullptr, &info2), int(TMDQ_ERR_BADARG));
    QVERIFY(!std::string(tmdq_last_error()).empty());
    QCOMPARE(tmdq_stream_result_info(seen.results.at(0), &info2), int(TMDQ_OK));
    QCOMPARE(std::string(tmdq_last_error()), std::string());
    tmdq_channel_series series{};
    series.struct_size = sizeof(series);
    QCOMPARE(tmdq_stream_result_channel(seen.results.at(0), 0, &series), int(TMDQ_ERR_BADARG));   // a lock stream has none
    QVERIFY(!std::string(tmdq_last_error()).empty());
}

void TestAbi::theExceptionGuardTurnsAThrowIntoAStatus()
{
    // The one place a failure the library did not foresee is caught, so it is tried directly.
    tmdq::setError("stale");
    QCOMPARE(tmdq::guarded("a call", []() -> int32_t { return TMDQ_OK; }), int(TMDQ_OK));
    QCOMPARE(tmdq::guarded("a call", []() -> int32_t { throw std::runtime_error("boom"); }), int(TMDQ_ERR_INTERNAL));
    QVERIFY(std::string(tmdq_last_error()).find("boom") != std::string::npos);
    QVERIFY(std::string(tmdq_last_error()).find("a call") != std::string::npos);
    QCOMPARE(tmdq::guarded("another", []() -> int32_t { throw 42; }), int(TMDQ_ERR_INTERNAL));
    QVERIFY(std::string(tmdq_last_error()).find("unknown exception") != std::string::npos);
    QVERIFY(std::string(tmdq_last_error()).find("another") != std::string::npos);
    tmdq::clearError();
    QCOMPARE(std::string(tmdq_last_error()), std::string());

    // And a path that is not valid UTF-8 is a failure the host is told about, not a crash.
    tmdq_file* file = nullptr;
    const int32_t status = tmdq_file_open("\xff\xfe\xc3.ch10", &file);
    QVERIFY2(status == TMDQ_ERR_PREPARE || status == TMDQ_ERR_INTERNAL, qPrintable(QString::number(status)));
    QVERIFY(file == nullptr);
    QVERIFY(!std::string(tmdq_last_error()).empty());
}

void TestAbi::theByteOrderOfTheFileReachesTheJob()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    // Byte order is file-scoped and comes from the request, not from a stream. Without the swap this
    // recording decodes to something else entirely; both must match what the core makes with the same flag.
    const std::vector<Case> cases = { lockCase(f) };
    bool okSwap = false, okNoSwap = false;
    const auto swapped = runCore(f, 0, cases, projectRoot().toStdString(), okSwap, true);
    const auto plain = runCore(f, 0, cases, projectRoot().toStdString(), okNoSwap, false);
    QVERIFY(okSwap);
    QVERIFY(okNoSwap || plain.empty());   // an unswapped run may find no lock at all, which is also a result
    QVERIFY2(plain.empty() || plain.at(0).lockPercent != swapped.at(0).lockPercent,
             "the fixture should decode differently without the swap");

    Request request(f, cases);
    request.req.swap_bytes = 0;
    Seen seenPlain;
    const bool endedWell = runToTheEnd(request, seenPlain);
    if (plain.empty())
    {
        QVERIFY(!endedWell);
        QVERIFY(seenPlain.results.empty());
    }
    else
    {
        QVERIFY(endedWell);
        QVERIFY2(differences(seenPlain.byJob().at(0), plain.at(0)).isEmpty(),
                 qPrintable(differences(seenPlain.byJob().at(0), plain.at(0))));
    }

    Request swappedRequest(f, cases);
    swappedRequest.req.swap_bytes = 1;
    Seen seenSwapped;
    QVERIFY(runToTheEnd(swappedRequest, seenSwapped));
    QVERIFY2(differences(seenSwapped.byJob().at(0), swapped.at(0)).isEmpty(),
             qPrintable(differences(seenSwapped.byJob().at(0), swapped.at(0))));
}

void TestAbi::aReceiverParametersFileAndTheAppRootReachTheJob()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString two = dir.filePath("two.toml");
    QVERIFY(QDir().mkpath(dir.filePath("root/settings/receiver_params")));
    auto write = [](const QString& path, const char* text) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
    };
    write(two, "[L_RCVR1]\nWord = 5\n[R_RCVR1]\nWord = 6\n");
    write(dir.filePath("root/settings/receiver_params/default.toml"), "[C_RCVR1]\nWord = 7\n");
    const std::string rootWithDefault = dir.filePath("root").toStdString();

    // (1) A word-map file named on the stream.
    Case named = receiverCase(f);
    named.receiverParams = two.toStdString();
    bool ok = false;
    const auto wantNamed = runCore(f, 0, { named }, projectRoot().toStdString(), ok);
    QVERIFY(ok);
    QCOMPARE(wantNamed.at(0).channels.size(), std::size_t{2});
    Request namedRequest(f, { named });
    Seen seenNamed;
    QVERIFY(runToTheEnd(namedRequest, seenNamed));
    QVERIFY2(differences(seenNamed.byJob().at(0), wantNamed.at(0)).isEmpty(), qPrintable(differences(seenNamed.byJob().at(0), wantNamed.at(0))));

    // (2) No file named: the one under the application root, if it has one.
    const Case plain = receiverCase(f);
    const auto wantRoot = runCore(f, 0, { plain }, rootWithDefault, ok);
    QVERIFY(ok);
    QCOMPARE(wantRoot.at(0).channels.size(), std::size_t{1});
    Request rootRequest(f, { plain });
    rootRequest.appRoot = rootWithDefault;
    rootRequest.req.app_root_utf8 = rootRequest.appRoot.c_str();
    Seen seenRoot;
    QVERIFY(runToTheEnd(rootRequest, seenRoot));
    QVERIFY2(differences(seenRoot.byJob().at(0), wantRoot.at(0)).isEmpty(), qPrintable(differences(seenRoot.byJob().at(0), wantRoot.at(0))));

    // (3) No application root at all (NULL): the default word map, which is not either of the above.
    const auto wantNone = runCore(f, 0, { plain }, std::string(), ok);
    QVERIFY(ok);
    QVERIFY(wantNone.at(0).channels.size() != wantRoot.at(0).channels.size());
    Request noneRequest(f, { plain });
    noneRequest.req.app_root_utf8 = nullptr;
    Seen seenNone;
    QVERIFY(runToTheEnd(noneRequest, seenNone));
    QVERIFY2(differences(seenNone.byJob().at(0), wantNone.at(0)).isEmpty(), qPrintable(differences(seenNone.byJob().at(0), wantNone.at(0))));
}

void TestAbi::theFirstProfileOfAWordWins()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    // Two profiles for the same word are a host mistake; the first is used, as documented.
    Case first = receiverCase(f);
    first.calibration[1] = { {0.0, -10.0}, {1000.0, 0.0}, {30000.0, 40.0}, {65535.0, 60.0} };
    bool ok = false;
    const auto want = runCore(f, 0, { first }, projectRoot().toStdString(), ok);
    QVERIFY(ok);

    Request request(f, { first });
    const tmdq_cal_point other[] = { {0.0, 0.0}, {65535.0, 1.0} };
    tmdq_cal_profile two[2] = { request.cases[0].wireProfiles[0], request.cases[0].wireProfiles[0] };
    two[1].points = other;
    two[1].point_count = 2;
    request.configs[0].calibration = two;
    request.configs[0].calibration_count = 2;
    Seen seen;
    QVERIFY(runToTheEnd(request, seen));
    QVERIFY2(differences(seen.byJob().at(0), want.at(0)).isEmpty(), qPrintable(differences(seen.byJob().at(0), want.at(0))));

    // And in the other order the other profile is the one that counts.
    std::swap(two[0], two[1]);
    Seen swapped;
    QVERIFY(runToTheEnd(request, swapped));
    QVERIFY(!differences(swapped.byJob().at(0), want.at(0)).isEmpty());
}

void TestAbi::waitAloneIsEnoughToSeeTheEnd()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    // A host that does not use the callback to learn the end calls tmdq_run_wait; when it returns, the run
    // is over and everything it was going to report has been reported.
    Request request(f, { lockCase(f), receiverCase(f) });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_OK));
    RunGuard guard{run};
    tmdq_run_wait(run);
    QCOMPARE(tmdq_run_running(run), 0);
    QCOMPARE(seen.finishedCount, 1);
    QCOMPARE(seen.results.size(), std::size_t{2});
    tmdq_run_wait(run);   // and again is harmless
    QCOMPARE(seen.finishedCount, 1);
}

void TestAbi::optionalTextMayBeLeftNull()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    // A label, a mask and a word-map file are all optional: NULL reads the same as empty.
    Case blank = lockCase(f);
    blank.label.clear();
    bool ok = false;
    const auto want = runCore(f, 0, { blank }, projectRoot().toStdString(), ok);
    QVERIFY(ok);
    QCOMPARE(want.at(0).streamLabel, "Ch " + std::to_string(f.pcmId));   // the label an empty one is given

    Request request(f, { blank });
    request.configs[0].label_utf8 = nullptr;
    request.configs[0].sync_mask_utf8 = nullptr;
    request.configs[0].receiver_params_toml_utf8 = nullptr;
    Seen seen;
    QVERIFY(runToTheEnd(request, seen));
    QVERIFY2(differences(seen.byJob().at(0), want.at(0)).isEmpty(), qPrintable(differences(seen.byJob().at(0), want.at(0))));
}

void TestAbi::theLinesBuildingJobsProducedReachLogFirst()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

    // A stream with a non-linear profile makes the job builder write log lines; they are reported through
    // `log`, in order, on the thread that called start, before it returns - ahead of anything the run says.
    Case profiled = receiverCase(f);
    profiled.calibration[1] = { {0.0, -10.0}, {1000.0, 0.0}, {30000.0, 40.0}, {65535.0, 60.0} };

    ProcessingParams base;
    base.filename = toFsPath(QString::fromStdString(f.path));
    base.timeChannelId = f.timeId;
    StreamJobContext ctx;
    ctx.appRoot = projectRoot().toStdString();
    StreamJob job;
    StreamJobReport report;
    QVERIFY(buildStreamJob(profiled.core(), base, ctx, job, report));
    QVERIFY2(!report.log.empty(), "the job builder should have something to say about a profile");

    Request request(f, { lockCase(f), profiled });
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    const std::thread::id caller = std::this_thread::get_id();
    tmdq_run* run = nullptr;
    QCOMPARE(tmdq_run_start(&request.req, &callbacks, &run), int(TMDQ_OK));
    RunGuard guard{run};
    QVERIFY2(seen.waitFinished(kTimeoutMs), seen.joined().c_str());
    tmdq_run_wait(run);

    const std::vector<std::string> events = seen.snapshot();
    QVERIFY(events.size() > report.log.size());
    for (std::size_t i = 0; i < report.log.size(); ++i)
    {
        QCOMPARE(events[i], "log:" + report.log[i]);
        QVERIFY(seen.logThreads[i] == caller);
    }
}

void TestAbi::everySettingOfAStreamReachesTheJob()
{
    const StepCal f = loadStepCal();
    if (!f.ok)
        QSKIP("step_cal fixture not available");

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
    const std::string project = projectRoot().toStdString();
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
    add("sample period", lockCase(f), [](Case& c) { c.periodIndex = UIConstants::kDefaultSamplePeriodIndex + 1; }, project);
    add("scale", receiverCase(f), [](Case& c) { c.scale = c.scale * 2.0; }, project);
    add("polarity", receiverCase(f), [](Case& c) { c.polarityIndex = c.polarityIndex == 0 ? 1 : 0; }, project);
    add("slope", receiverCase(f), [](Case& c) { c.slopeIndex = c.slopeIndex - 1; }, project);
    // The receiver grid is only used when there is no word-map file, so these two run with no application root.
    add("receiver count", receiverCase(f), [](Case& c) { c.numReceivers = 2; }, std::string());
    add("receiver channels", receiverCase(f), [](Case& c) { c.receiverChannels = 1; }, std::string());

    for (Row& row : rows)
    {
        const QString name = QString::fromLatin1(row.name);
        bool okBase = false, okVariant = false;
        const auto base = runCore(f, 0, { row.base }, row.appRoot, okBase);
        const auto variant = runCore(f, 0, { row.variant }, row.appRoot, okVariant);
        QVERIFY2(okBase && base.size() == 1, qPrintable(name + ": the unchanged stream must run"));
        QVERIFY2(okVariant, qPrintable(name + ": the changed stream must build and run"));
        QVERIFY2(variant.empty() || !sameStream(variant.at(0), base.at(0)),
                 qPrintable(name + ": changing it should change the core's result, or the row proves nothing"));

        Request request(f, { row.variant });
        request.appRoot = row.appRoot;
        request.req.app_root_utf8 = request.appRoot.c_str();
        Seen seen;
        const bool ended = runToTheEnd(request, seen);
        if (variant.empty())
        {
            QVERIFY2(!ended && seen.results.empty(), qPrintable(name + ": the core finds nothing, so must the ABI"));
        }
        else
        {
            QVERIFY2(ended, qPrintable(name + ": the run should complete"));
            const QString diff = differences(seen.byJob().at(0), variant.at(0));
            QVERIFY2(diff.isEmpty(), qPrintable(name + " differs in: " + diff));
        }
    }
}

void TestAbi::readingAHostStructNeverStraysBeyondTheKnownOne()
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
    QVERIFY(tmdq::readStruct(&source.info, sizeof(tmdq_channel_info), dest.info));
    QCOMPARE(dest.info.channel_id, 42);
    QCOMPARE(int(dest.info.struct_size), int(sizeof(tmdq_channel_info)));   // the size this library read it as
    for (unsigned char b : dest.guard)
        QCOMPARE(int(b), 0xCD);

    // A shorter struct than the first revision is refused, a NULL too.
    source.info.struct_size = sizeof(tmdq_channel_info) - 1;
    QVERIFY(!tmdq::readStruct(&source.info, sizeof(tmdq_channel_info), dest.info));
    QVERIFY(!tmdq::readStruct(static_cast<const tmdq_channel_info*>(nullptr), sizeof(tmdq_channel_info), dest.info));
}
