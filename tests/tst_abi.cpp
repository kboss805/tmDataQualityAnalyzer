#include "tst_abi.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <random>
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

#include "../native/tmdq_core/abi_events.h"
#include "../native/tmdq_core/abi_internal.h"
#include "../native/tmdq_core/tmdq_abi.h"
#include "calibrationextractor.h"
#include "calibrationrequest.h"
#include "chapter10reader.h"
#include "clocktime.h"
#include "csvseriesparser.h"
#include "csvserieswriter.h"
#include "framesyncfile.h"
#include "templatematcher.h"
#include "constants.h"
#include "pipelinerunner.h"
#include "seriesbuilder.h"
#include "processedstreamdata.h"
#include "qtbridge.h"
#include "framesync.h"
#include "stepdetector.h"
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


// ---------------------------------------------------------------------------
// The formats
// ---------------------------------------------------------------------------

bool sameDoubles(const double* a, std::size_t na, const std::vector<double>& b);

QByteArray slurp(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
}

QString writeFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(bytes);
    return path;
}

/// A pattern file as text, from the core and from the ABI, so two compare in one line.
QString describeSyncFile(const FrameSyncFile& f)
{
    // An absent optional reads as 0 beside a "has" flag of 0, which is what the ABI promises.
    return QString("pattern=[%1] mask=[%2] has_bits=%3 bits=%4 legacy=%5 inverted=%6 has_randomized=%7 randomized=%8")
        .arg(QString::fromStdString(f.pattern), QString::fromStdString(f.mask))
        .arg(f.bitsPerFrame.has_value() ? 1 : 0).arg(f.bitsPerFrame.value_or(0))
        .arg(f.bitsFromLegacyKey ? 1 : 0).arg(f.inverted ? 1 : 0)
        .arg(f.randomized.has_value() ? 1 : 0).arg(f.randomized.value_or(false) ? 1 : 0);
}

QString describeSyncFile(const tmdq_frame_sync_file* file)
{
    tmdq_frame_sync_file_info i{};
    i.struct_size = sizeof(i);
    if (tmdq_frame_sync_file_info_get(file, &i) != TMDQ_OK)
        return "info failed";
    return QString("pattern=[%1] mask=[%2] has_bits=%3 bits=%4 legacy=%5 inverted=%6 has_randomized=%7 randomized=%8")
        .arg(QString::fromUtf8(i.pattern_utf8), QString::fromUtf8(i.mask_utf8))
        .arg(i.has_bits_per_frame).arg(i.bits_per_frame)
        .arg(i.bits_from_legacy_key).arg(i.inverted)
        .arg(i.has_randomized).arg(i.randomized);
}

/// What differs between a parsed CSV through the ABI and the core's own parse; empty if nothing.
QString csvDifferences(const tmdq_csv* csv, const CsvParseResult& want)
{
    QStringList diff;
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
        const QString at = QString::number(i) + ":";
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
tmdq_stream_result* makeResult(const ProcessedStreamData& data)
{
    return new tmdq_stream_result{std::make_shared<const ProcessedStreamData>(data)};
}

/// A processed stream with names and values that exercise the builder.
ProcessedStreamData randomStream(std::mt19937& rng)
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
QString seriesDifferences(const tmdq_series_set* set, const StreamSeries& want)
{
    QStringList diff;
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
        const QString at = QString::number(i) + ":";
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

std::vector<double> shippedSteps()
{
    std::vector<StepDefinition> steps;
    std::string error;
    if (!StepDetector::parseStepConfig(toFsPath(projectRoot() + "/settings/rcvr_cals/default.toml"), steps, error))
        return {};
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
        : appRoot(projectRoot().toStdString())
    {
        in.calFilename      = toFsPath(QString::fromStdString(f.path));
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
DirectCalibration runDirectCalibration(const CalCase& c)
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
QString calibrationDifferences(const tmdq_calibration* cal, const DirectCalibration& want)
{
    QStringList diff;
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
        return diff.join(", ") + (diff.isEmpty() ? "" : ", ") + "channel count";
    for (int32_t i = 0; i < info.channel_count; ++i)
    {
        tmdq_calibration_channel ch{};
        ch.struct_size = sizeof(ch);
        if (tmdq_calibration_channel_result(cal, i, &ch) != TMDQ_OK) { diff << "channel call"; continue; }
        const CalibrationChannelResult& r = want.results[std::size_t(i)];
        const QString at = QString::number(i) + ":";
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
                    diff << at + "point " + QString::number(p);
        if (std::size_t(ch.plateau_level_count) != r.plateauLevels.size() ||
            !sameDoubles(ch.plateau_levels, std::size_t(ch.plateau_level_count), r.plateauLevels)) diff << at + "levels";
        if (std::size_t(ch.unresolved_count) != r.unresolvedDb.size() ||
            !sameDoubles(ch.unresolved_db, std::size_t(ch.unresolved_count), r.unresolvedDb)) diff << at + "unresolved";
    }
    return diff.join(", ");
}

/// Runs a calibration request to its end and returns whether it finished successfully.
bool calibrateToTheEnd(CalRequest& request, Seen& seen, CalGuard& guard)
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
bool sameCalibration(const DirectCalibration& a, const DirectCalibration& b)
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

    QCOMPARE(int(sizeof(tmdq_calibration_request)), 112);
    QCOMPARE(int(offsetof(tmdq_calibration_request, time_channel_id)), 4);
    QCOMPARE(int(offsetof(tmdq_calibration_request, cal_filename_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_calibration_request, app_root_utf8)), 16);
    QCOMPARE(int(offsetof(tmdq_calibration_request, sync_pattern_utf8)), 24);
    QCOMPARE(int(offsetof(tmdq_calibration_request, sync_mask_utf8)), 32);
    QCOMPARE(int(offsetof(tmdq_calibration_request, receiver_params_toml_utf8)), 40);
    QCOMPARE(int(offsetof(tmdq_calibration_request, step_db)), 48);
    QCOMPARE(int(offsetof(tmdq_calibration_request, data_rate_mbps)), 56);
    QCOMPARE(int(offsetof(tmdq_calibration_request, clip_start_sec)), 64);
    QCOMPARE(int(offsetof(tmdq_calibration_request, clip_end_sec)), 72);
    QCOMPARE(int(offsetof(tmdq_calibration_request, pcm_channel_id)), 80);
    QCOMPARE(int(offsetof(tmdq_calibration_request, bits_in_minor_frame)), 84);
    QCOMPARE(int(offsetof(tmdq_calibration_request, randomized)), 88);
    QCOMPARE(int(offsetof(tmdq_calibration_request, inverted)), 92);
    QCOMPARE(int(offsetof(tmdq_calibration_request, swap_bytes)), 96);
    QCOMPARE(int(offsetof(tmdq_calibration_request, num_receivers)), 100);
    QCOMPARE(int(offsetof(tmdq_calibration_request, receiver_channels)), 104);
    QCOMPARE(int(offsetof(tmdq_calibration_request, step_count)), 108);

    QCOMPARE(int(sizeof(tmdq_calibration_info)), 48);
    QCOMPARE(int(offsetof(tmdq_calibration_info, channel_count)), 4);
    QCOMPARE(int(offsetof(tmdq_calibration_info, summary_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_calibration_info, error_utf8)), 16);
    QCOMPARE(int(offsetof(tmdq_calibration_info, report_utf8)), 24);
    QCOMPARE(int(offsetof(tmdq_calibration_info, recording_seconds)), 32);
    QCOMPARE(int(offsetof(tmdq_calibration_info, clip_ignored)), 40);

    QCOMPARE(int(sizeof(tmdq_calibration_channel)), 96);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, word_index)), 4);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, name_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, points)), 16);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, plateau_levels)), 24);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, unresolved_db)), 32);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, edge_threshold)), 40);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, point_count)), 48);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, plateau_level_count)), 52);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, unresolved_count)), 56);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, valid)), 60);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, had_data)), 64);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, outcome)), 68);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, detected_plateaus)), 72);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, extra_plateaus)), 76);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, threshold_relaxed)), 80);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, saturated)), 84);
    QCOMPARE(int(offsetof(tmdq_calibration_channel, sweep_levels)), 88);

    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CALIBRATION_REQUEST), 112);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CALIBRATION_INFO), 48);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CALIBRATION_CHANNEL), 96);
    QCOMPARE(int(TMDQ_STRUCT_CALIBRATION_REQUEST), 9);
    QCOMPARE(int(TMDQ_STRUCT_CALIBRATION_CHANNEL), 11);
    QCOMPARE(int(TMDQ_ERR_STATE), 7);
    QCOMPARE(int(TMDQ_CAL_CALIBRATED), int(CalibrationOutcome::Calibrated));
    QCOMPARE(int(TMDQ_CAL_NO_DATA), int(CalibrationOutcome::NoData));
    QCOMPARE(int(TMDQ_CAL_FLAT_NO_SIGNAL), int(CalibrationOutcome::FlatNoSignal));
    QCOMPARE(int(TMDQ_CAL_TOO_FEW_PLATEAUS), int(CalibrationOutcome::TooFewPlateaus));
    QCOMPARE(int(TMDQ_CAL_NO_MONOTONIC_SWEEP), int(CalibrationOutcome::NoMonotonicSweep));

    QCOMPARE(int(sizeof(tmdq_frame_sync_file_info)), 48);
    QCOMPARE(int(offsetof(tmdq_frame_sync_file_info, has_bits_per_frame)), 4);
    QCOMPARE(int(offsetof(tmdq_frame_sync_file_info, bits_per_frame)), 8);
    QCOMPARE(int(offsetof(tmdq_frame_sync_file_info, bits_from_legacy_key)), 12);
    QCOMPARE(int(offsetof(tmdq_frame_sync_file_info, inverted)), 16);
    QCOMPARE(int(offsetof(tmdq_frame_sync_file_info, has_randomized)), 20);
    QCOMPARE(int(offsetof(tmdq_frame_sync_file_info, randomized)), 24);
    QCOMPARE(int(offsetof(tmdq_frame_sync_file_info, pattern_utf8)), 32);
    QCOMPARE(int(offsetof(tmdq_frame_sync_file_info, mask_utf8)), 40);

    QCOMPARE(int(sizeof(tmdq_match_info)), 32);
    QCOMPARE(int(offsetof(tmdq_match_info, ok)), 4);
    QCOMPARE(int(offsetof(tmdq_match_info, missing_count)), 8);
    QCOMPARE(int(offsetof(tmdq_match_info, extra_count)), 12);
    QCOMPARE(int(offsetof(tmdq_match_info, missing)), 16);
    QCOMPARE(int(offsetof(tmdq_match_info, extra)), 24);

    QCOMPARE(int(sizeof(tmdq_csv_info)), 32);
    QCOMPARE(int(offsetof(tmdq_csv_info, series_count)), 4);
    QCOMPARE(int(offsetof(tmdq_csv_info, base_day)), 8);
    QCOMPARE(int(offsetof(tmdq_csv_info, skipped_rows)), 12);
    QCOMPARE(int(offsetof(tmdq_csv_info, base_time_offset)), 16);
    QCOMPARE(int(offsetof(tmdq_csv_info, x_max)), 24);

    QCOMPARE(int(sizeof(tmdq_csv_series_info)), 80);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, metric_type)), 4);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, name_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, stream_label_utf8)), 16);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, x_values)), 24);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, y_values)), 32);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, y_min)), 40);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, y_max)), 48);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, receiver_index)), 56);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, channel_index)), 60);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, stream_order)), 64);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, source_id)), 68);
    QCOMPARE(int(offsetof(tmdq_csv_series_info, point_count)), 72);

    QCOMPARE(int(sizeof(tmdq_csv_column)), 48);
    QCOMPARE(int(offsetof(tmdq_csv_column, metric_type)), 4);
    QCOMPARE(int(offsetof(tmdq_csv_column, name_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_csv_column, x_values)), 16);
    QCOMPARE(int(offsetof(tmdq_csv_column, y_values)), 24);
    QCOMPARE(int(offsetof(tmdq_csv_column, source_id)), 32);
    QCOMPARE(int(offsetof(tmdq_csv_column, x_count)), 36);
    QCOMPARE(int(offsetof(tmdq_csv_column, y_count)), 40);

    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_FRAME_SYNC_FILE_INFO), 48);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_MATCH_INFO), 32);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CSV_INFO), 32);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CSV_SERIES_INFO), 80);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_CSV_COLUMN), 48);
    QCOMPARE(int(TMDQ_STRUCT_FRAME_SYNC_FILE_INFO), 12);
    QCOMPARE(int(TMDQ_STRUCT_CSV_COLUMN), 16);
    QCOMPARE(int(TMDQ_ERR_IO), 8);
    QCOMPARE(int(TMDQ_METRIC_SNR), int(MetricType::SNR));
    QCOMPARE(int(TMDQ_METRIC_FRAME_SYNC_LOCK), int(MetricType::FrameSyncLock));
    QCOMPARE(int(TMDQ_METRIC_ACCUMULATED_MISSED_FRAMES), int(MetricType::AccumulatedMissedFrames));

    QCOMPARE(int(sizeof(tmdq_series_set_info)), 24);
    QCOMPARE(int(offsetof(tmdq_series_set_info, series_count)), 4);
    QCOMPARE(int(offsetof(tmdq_series_set_info, elapsed_count)), 8);
    QCOMPARE(int(offsetof(tmdq_series_set_info, elapsed)), 16);

    QCOMPARE(int(sizeof(tmdq_series_spec)), 72);
    QCOMPARE(int(offsetof(tmdq_series_spec, metric_type)), 4);
    QCOMPARE(int(offsetof(tmdq_series_spec, name_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_series_spec, stream_label_utf8)), 16);
    QCOMPARE(int(offsetof(tmdq_series_spec, y_values)), 24);
    QCOMPARE(int(offsetof(tmdq_series_spec, y_min)), 32);
    QCOMPARE(int(offsetof(tmdq_series_spec, y_max)), 40);
    QCOMPARE(int(offsetof(tmdq_series_spec, receiver_index)), 48);
    QCOMPARE(int(offsetof(tmdq_series_spec, channel_index)), 52);
    QCOMPARE(int(offsetof(tmdq_series_spec, stream_order)), 56);
    QCOMPARE(int(offsetof(tmdq_series_spec, stream_sequence)), 60);
    QCOMPARE(int(offsetof(tmdq_series_spec, source_id)), 64);
    QCOMPARE(int(offsetof(tmdq_series_spec, y_count)), 68);

    QCOMPARE(int(sizeof(tmdq_stream_key)), 24);
    QCOMPARE(int(offsetof(tmdq_stream_key, source_id)), 4);
    QCOMPARE(int(offsetof(tmdq_stream_key, label_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_stream_key, channel)), 16);

    QCOMPARE(int(sizeof(tmdq_time_base_info)), 24);
    QCOMPARE(int(offsetof(tmdq_time_base_info, base_day)), 4);
    QCOMPARE(int(offsetof(tmdq_time_base_info, base_abs_seconds)), 8);
    QCOMPARE(int(offsetof(tmdq_time_base_info, base_time_offset)), 16);

    QCOMPARE(int(sizeof(tmdq_imported_series)), 24);
    QCOMPARE(int(offsetof(tmdq_imported_series, metric_type)), 4);
    QCOMPARE(int(offsetof(tmdq_imported_series, stream_label_utf8)), 8);
    QCOMPARE(int(offsetof(tmdq_imported_series, source_id)), 16);
    QCOMPARE(int(offsetof(tmdq_imported_series, stream_order)), 20);

    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_SERIES_SET_INFO), 24);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_SERIES_SPEC), 72);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_STREAM_KEY), 24);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_TIME_BASE_INFO), 24);
    QCOMPARE(tmdq_sizeof(TMDQ_STRUCT_IMPORTED_SERIES), 24);
    QCOMPARE(int(TMDQ_STRUCT_SERIES_SET_INFO), 17);
    QCOMPARE(int(TMDQ_STRUCT_SERIES_SPEC), 18);
    QCOMPARE(int(TMDQ_STRUCT_STREAM_KEY), 19);
    QCOMPARE(int(TMDQ_STRUCT_TIME_BASE_INFO), 20);
    QCOMPARE(int(TMDQ_STRUCT_IMPORTED_SERIES), 21);

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
    QCOMPARE(tmdq_abi_revision(), 4);
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

void TestAbi::theStepFileIsReadLikeTheCore()
{
    const std::string path = (projectRoot() + "/settings/rcvr_cals/default.toml").toStdString();
    std::vector<StepDefinition> want;
    std::string error;
    QVERIFY2(StepDetector::parseStepConfig(toFsPath(QString::fromStdString(path)), want, error), error.c_str());
    QVERIFY(want.size() > 5);

    tmdq_steps* steps = nullptr;
    QCOMPARE(tmdq_steps_parse(path.c_str(), &steps), int(TMDQ_OK));
    QVERIFY(steps != nullptr);
    QCOMPARE(tmdq_steps_count(steps), int(want.size()));
    const double* db = tmdq_steps_db(steps);
    QVERIFY(db != nullptr);
    for (std::size_t i = 0; i < want.size(); ++i)
        QCOMPARE(db[i], want[i].db);
    tmdq_steps_free(steps);

    // A file that is not there says what the parser says; a file with no step in it is the same.
    tmdq_steps* none = reinterpret_cast<tmdq_steps*>(std::uintptr_t{1});
    std::vector<StepDefinition> ignored;
    const std::string missing = (QDir::tempPath() + "/tst_abi_no_such_steps.toml").toStdString();
    QVERIFY(!StepDetector::parseStepConfig(toFsPath(QString::fromStdString(missing)), ignored, error));
    QCOMPARE(tmdq_steps_parse(missing.c_str(), &none), int(TMDQ_ERR_PREPARE));
    QVERIFY(none == nullptr);
    QCOMPARE(std::string(tmdq_last_error()), error);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile empty(dir.filePath("empty.toml"));
    QVERIFY(empty.open(QIODevice::WriteOnly));
    empty.write("# no steps here\n");
    empty.close();
    const std::string emptyPath = dir.filePath("empty.toml").toStdString();
    QVERIFY(!StepDetector::parseStepConfig(toFsPath(QString::fromStdString(emptyPath)), ignored, error));
    QCOMPARE(tmdq_steps_parse(emptyPath.c_str(), &none), int(TMDQ_ERR_PREPARE));
    QCOMPARE(std::string(tmdq_last_error()), error);

    // Arguments.
    QCOMPARE(tmdq_steps_parse(nullptr, &none), int(TMDQ_ERR_BADARG));
    QVERIFY(none == nullptr);
    QCOMPARE(tmdq_steps_parse(path.c_str(), nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_steps_count(nullptr), -1);
    QVERIFY(tmdq_steps_db(nullptr) == nullptr);
    tmdq_steps_free(nullptr);
}

void TestAbi::theInitialStepFilePathIsWrittenSnprintfStyle()
{
    const std::string root = projectRoot().toStdString();
    const std::string want = StepDetector::initialStepConfigPath(root, std::string());
    QVERIFY2(!want.empty(), "the shipped default step file should be found under the project root");

    // Measuring, then writing exactly, then writing into a buffer that is too small.
    QCOMPARE(tmdq_step_config_initial_path(root.c_str(), nullptr, nullptr, 0), int(want.size()));
    std::vector<char> exact(want.size() + 1, 'x');
    QCOMPARE(tmdq_step_config_initial_path(root.c_str(), nullptr, exact.data(), int(exact.size())), int(want.size()));
    QCOMPARE(std::string(exact.data()), want);

    std::vector<char> tight(8, 'x');
    QCOMPARE(tmdq_step_config_initial_path(root.c_str(), "", tight.data(), int(tight.size())), int(want.size()));
    QCOMPARE(std::string(tight.data()), want.substr(0, 7));   // truncated, and terminated
    QCOMPARE(int(tight[7]), 0);

    // A preferred file that exists wins; one that does not is passed over.
    const std::string preferred = root + "/settings/rcvr_cals/TRC.toml";
    std::vector<char> big(512, 'x');
    QVERIFY(tmdq_step_config_initial_path(root.c_str(), preferred.c_str(), big.data(), int(big.size())) > 0);
    QCOMPARE(std::string(big.data()), StepDetector::initialStepConfigPath(root, preferred));
    QCOMPARE(std::string(big.data()), preferred);
    std::fill(big.begin(), big.end(), 'x');
    tmdq_step_config_initial_path(root.c_str(), (root + "/no/such.toml").c_str(), big.data(), int(big.size()));
    QCOMPARE(std::string(big.data()), want);

    // No app root and nothing preferred: empty.
    std::fill(big.begin(), big.end(), 'x');
    QCOMPARE(tmdq_step_config_initial_path(nullptr, nullptr, big.data(), int(big.size())),
             int(StepDetector::initialStepConfigPath(std::string(), std::string()).size()));

    // A buffer and capacity that do not make sense are an error, not a write.
    QCOMPARE(tmdq_step_config_initial_path(root.c_str(), nullptr, nullptr, 10), -1);
    QCOMPARE(tmdq_step_config_initial_path(root.c_str(), nullptr, big.data(), -1), -1);
}

void TestAbi::aCalibrationMatchesWhatTheExtractorMakes()
{
    const StepCal f = loadStepCal();
    if (!f.ok || shippedSteps().empty())
        QSKIP("step_cal fixture or the shipped step file not available");

    const CalCase c(f);
    const DirectCalibration want = runDirectCalibration(c);
    QVERIFY2(want.ok, want.summary.c_str());
    QCOMPARE(want.results.size(), std::size_t{48});
    int calibrated = 0;
    for (const CalibrationChannelResult& r : want.results)
        calibrated += r.profile.valid ? 1 : 0;
    QVERIFY2(calibrated >= 3, "the fixture should calibrate some channels, or the comparison proves little");

    CalRequest request(c);
    Seen seen;
    CalGuard guard;
    QVERIFY2(calibrateToTheEnd(request, seen, guard), seen.joined().c_str());
    const QString diff = calibrationDifferences(guard.handle, want);
    QVERIFY2(diff.isEmpty(), qPrintable(diff));

    // The documented order: finished last and once, progress from a single thread, never a stream result.
    const std::vector<std::string> events = seen.snapshot();
    QCOMPARE(events.back(), std::string("finished:1"));
    QCOMPARE(seen.finishedCount, 1);
    QVERIFY(!seen.any("stream:"));
    QCOMPARE(seen.progressThreads.size(), std::size_t{1});
    QVERIFY(seen.any("log:"));
    QCOMPARE(tmdq_calibration_running(guard.handle), 0);

    // The profiles it found feed a run: the same receiver stream, through the ABI and directly.
    Case calibrated_case = receiverCase(f);
    for (int32_t i = 0; i < int(want.results.size()); ++i)
    {
        tmdq_calibration_channel ch{};
        ch.struct_size = sizeof(ch);
        QCOMPARE(tmdq_calibration_channel_result(guard.handle, i, &ch), int(TMDQ_OK));
        if (ch.valid)
            calibrated_case.calibration[ch.word_index] = std::vector<tmdq_cal_point>(ch.points, ch.points + ch.point_count);
    }
    QVERIFY(!calibrated_case.calibration.empty());
    bool ok = false;
    const auto direct = runCore(f, 0, { calibrated_case }, projectRoot().toStdString(), ok);
    QVERIFY(ok);
    Request run(f, { calibrated_case });
    Seen runSeen;
    QVERIFY(runToTheEnd(run, runSeen));
    QVERIFY2(differences(runSeen.byJob().at(0), direct.at(0)).isEmpty(), qPrintable(differences(runSeen.byJob().at(0), direct.at(0))));
}

void TestAbi::everySettingOfACalibrationReachesTheExtractor()
{
    const StepCal f = loadStepCal();
    if (!f.ok || shippedSteps().empty())
        QSKIP("step_cal fixture or the shipped step file not available");

    // One setting changed at a time. For each, the core's result for the changed calibration must differ from
    // its result for the unchanged one (or the row proves nothing), and the run through the ABI must equal the
    // core's. A setting the ABI forgot to carry would give the unchanged calibration's result.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto write = [](const QString& path, const char* text) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
    };
    write(dir.filePath("one.toml"), "[L_RCVR1]\nWord = 1\n");
    QVERIFY(QDir().mkpath(dir.filePath("root/settings/receiver_params")));
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
    noMap.appRoot = dir.filePath("empty-root").toStdString();

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
    add("word-map file", [&](CalCase& c) { c.in.receiverParamsToml = toFsPath(dir.filePath("one.toml")); });
    add("application root", [&](CalCase& c) { c.appRoot = dir.filePath("root").toStdString(); });

    for (Row& row : rows)
    {
        const QString name = QString::fromLatin1(row.name);
        const DirectCalibration base = runDirectCalibration(row.base);
        const DirectCalibration variant = runDirectCalibration(row.variant);
        QVERIFY2(base.built && base.ok, qPrintable(name + ": the unchanged calibration must run: " + QString::fromStdString(base.summary)));
        QVERIFY2(variant.built, qPrintable(name + ": the changed calibration must build"));
        QVERIFY2(!sameCalibration(base, variant), qPrintable(name + ": changing it should change the core's result, or the row proves nothing"));

        CalRequest request(row.variant);
        Seen seen;
        CalGuard guard;
        const bool ended = calibrateToTheEnd(request, seen, guard);
        QVERIFY2(guard.handle != nullptr, qPrintable(name + ": the calibration should start"));
        if (variant.ok)
            QVERIFY2(ended, qPrintable(name + ": the calibration should complete"));
        else
            QVERIFY2(!ended, qPrintable(name + ": the core fails, so must the ABI"));
        const QString diff = calibrationDifferences(guard.handle, variant);
        QVERIFY2(diff.isEmpty(), qPrintable(name + " differs in: " + diff));
    }
}

void TestAbi::aCalibrationSettingTheExtractorCannotUseIsRefused()
{
    const StepCal f = loadStepCal();
    if (!f.ok || shippedSteps().empty())
        QSKIP("step_cal fixture or the shipped step file not available");

    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    tmdq_calibration* handle = reinterpret_cast<tmdq_calibration*>(std::uintptr_t{1});

    CalCase badPattern(f);
    badPattern.in.syncPattern = "zz";
    CalRequest r1(badPattern);
    QCOMPARE(tmdq_calibration_start(&r1.req, &callbacks, &handle), int(TMDQ_ERR_JOB));
    QVERIFY(handle == nullptr);
    QCOMPARE(std::string(tmdq_last_error()), std::string("Invalid frame sync pattern 'zz'."));

    CalCase emptyPattern(f);
    emptyPattern.in.syncPattern.clear();
    CalRequest r2(emptyPattern);
    r2.req.sync_pattern_utf8 = nullptr;
    QCOMPARE(tmdq_calibration_start(&r2.req, &callbacks, &handle), int(TMDQ_ERR_JOB));
    QCOMPARE(std::string(tmdq_last_error()), std::string("Invalid frame sync pattern ''."));

    CalCase badMask(f);
    badMask.in.syncMask = "xyz";
    CalRequest r3(badMask);
    QCOMPARE(tmdq_calibration_start(&r3.req, &callbacks, &handle), int(TMDQ_ERR_JOB));
    QCOMPARE(std::string(tmdq_last_error()), std::string("Invalid frame sync mask 'xyz'."));

    // Nothing was started, so nothing was reported.
    QVERIFY(seen.snapshot().empty());
}

void TestAbi::badArgumentsToTheCalibrationFunctionsAreRefused()
{
    const StepCal f = loadStepCal();
    if (!f.ok || shippedSteps().empty())
        QSKIP("step_cal fixture or the shipped step file not available");

    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    CalRequest good{CalCase(f)};
    tmdq_calibration* handle = reinterpret_cast<tmdq_calibration*>(std::uintptr_t{1});

    QCOMPARE(tmdq_calibration_start(nullptr, &callbacks, &handle), int(TMDQ_ERR_BADARG));
    QVERIFY(handle == nullptr);
    QCOMPARE(tmdq_calibration_start(&good.req, &callbacks, nullptr), int(TMDQ_ERR_BADARG));

    tmdq_calibration_request r = good.req;
    r.struct_size = sizeof(tmdq_calibration_request) - 1;
    QCOMPARE(tmdq_calibration_start(&r, &callbacks, &handle), int(TMDQ_ERR_BADARG));
    r = good.req;
    r.cal_filename_utf8 = nullptr;
    QCOMPARE(tmdq_calibration_start(&r, &callbacks, &handle), int(TMDQ_ERR_BADARG));
    r.cal_filename_utf8 = "";
    QCOMPARE(tmdq_calibration_start(&r, &callbacks, &handle), int(TMDQ_ERR_BADARG));
    r = good.req;
    r.step_count = -1;
    QCOMPARE(tmdq_calibration_start(&r, &callbacks, &handle), int(TMDQ_ERR_BADARG));
    r = good.req;
    r.step_db = nullptr;
    QCOMPARE(tmdq_calibration_start(&r, &callbacks, &handle), int(TMDQ_ERR_BADARG));
    tmdq_callbacks shortCallbacks = callbacks;
    shortCallbacks.struct_size = sizeof(tmdq_callbacks) - 1;
    QCOMPARE(tmdq_calibration_start(&good.req, &shortCallbacks, &handle), int(TMDQ_ERR_BADARG));
    QVERIFY(handle == nullptr);
    QVERIFY(seen.snapshot().empty());

    // NULL is tolerated where it makes sense.
    tmdq_calibration_cancel(nullptr);
    QCOMPARE(tmdq_calibration_running(nullptr), 0);
    tmdq_calibration_wait(nullptr);
    tmdq_calibration_free(nullptr);
    tmdq_calibration_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_calibration_result(nullptr, &info), int(TMDQ_ERR_BADARG));
    tmdq_calibration_channel ch{};
    ch.struct_size = sizeof(ch);
    QCOMPARE(tmdq_calibration_channel_result(nullptr, 0, &ch), int(TMDQ_ERR_BADARG));

    // A run with no callbacks at all just runs; and a finished one refuses a bad index, a short struct, no output.
    CalGuard guard;
    QCOMPARE(tmdq_calibration_start(&good.req, nullptr, &guard.handle), int(TMDQ_OK));
    tmdq_calibration_wait(guard.handle);
    QCOMPARE(tmdq_calibration_running(guard.handle), 0);
    QCOMPARE(tmdq_calibration_channel_result(guard.handle, 48, &ch), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_calibration_channel_result(guard.handle, -1, &ch), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_calibration_channel_result(guard.handle, 0, nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_calibration_result(guard.handle, nullptr), int(TMDQ_ERR_BADARG));
    tmdq_calibration_info shortInfo{};
    shortInfo.struct_size = sizeof(shortInfo) - 1;
    QCOMPARE(tmdq_calibration_result(guard.handle, &shortInfo), int(TMDQ_ERR_BADARG));
    tmdq_calibration_channel shortChannel{};
    shortChannel.struct_size = sizeof(shortChannel) - 1;
    QCOMPARE(tmdq_calibration_channel_result(guard.handle, 0, &shortChannel), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_calibration_channel_result(guard.handle, 0, &ch), int(TMDQ_OK));
}

void TestAbi::aCalibrationThatCannotStartReportsThroughTheSink()
{
    const StepCal f = loadStepCal();
    if (!f.ok || shippedSteps().empty())
        QSKIP("step_cal fixture or the shipped step file not available");

    // A recording that is not there: the extractor reports it before any thread starts, so by the time
    // start returns the error and the end have been delivered, and the handle is already finished.
    CalCase missing(f);
    missing.in.calFilename = toFsPath(QDir::tempPath() + "/tst_abi_no_such_cal.ch10");
    const DirectCalibration want = runDirectCalibration(missing);
    QVERIFY(!want.ok);
    QVERIFY(!want.error.empty());

    CalRequest request(missing);
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    CalGuard guard;
    QCOMPARE(tmdq_calibration_start(&request.req, &callbacks, &guard.handle), int(TMDQ_OK));
    QVERIFY(guard.handle != nullptr);
    QCOMPARE(seen.finishedCount, 1);
    QCOMPARE(seen.finishedSuccess, 0);
    QVERIFY(seen.any("error:"));
    QCOMPARE(seen.snapshot().back(), std::string("finished:0"));
    QCOMPARE(tmdq_calibration_running(guard.handle), 0);
    const QString diff = calibrationDifferences(guard.handle, want);
    QVERIFY2(diff.isEmpty(), qPrintable(diff));

    // No steps at all is refused the same way, with the extractor's own words.
    const CalCase none(f, {});
    const DirectCalibration noSteps = runDirectCalibration(none);
    QVERIFY(!noSteps.ok);
    CalRequest empty(none);
    Seen seen2;
    const tmdq_callbacks callbacks2 = seen2.callbacks();
    CalGuard guard2;
    QCOMPARE(tmdq_calibration_start(&empty.req, &callbacks2, &guard2.handle), int(TMDQ_OK));
    QCOMPARE(seen2.finishedSuccess, 0);
    QVERIFY2(calibrationDifferences(guard2.handle, noSteps).isEmpty(), qPrintable(calibrationDifferences(guard2.handle, noSteps)));
}

void TestAbi::resultsAreRefusedWhileACalibrationRunsAndCancelEndsIt()
{
    const StepCal f = loadStepCal();
    if (!f.ok || shippedSteps().empty())
        QSKIP("step_cal fixture or the shipped step file not available");

    CalRequest request{CalCase(f)};
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    CalGuard guard;
    QCOMPARE(tmdq_calibration_start(&request.req, &callbacks, &guard.handle), int(TMDQ_OK));
    QCOMPARE(tmdq_calibration_running(guard.handle), 1);

    tmdq_calibration_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_calibration_result(guard.handle, &info), int(TMDQ_ERR_STATE));
    QVERIFY(!std::string(tmdq_last_error()).empty());
    tmdq_calibration_channel ch{};
    ch.struct_size = sizeof(ch);
    QCOMPARE(tmdq_calibration_channel_result(guard.handle, 0, &ch), int(TMDQ_ERR_STATE));

    tmdq_calibration_cancel(guard.handle);
    tmdq_calibration_cancel(guard.handle);   // twice is as good as once
    QVERIFY2(seen.waitFinished(kTimeoutMs), seen.joined().c_str());
    tmdq_calibration_wait(guard.handle);
    QCOMPARE(seen.finishedCount, 1);
    QCOMPARE(seen.finishedSuccess, 0);
    QCOMPARE(tmdq_calibration_running(guard.handle), 0);
    QCOMPARE(tmdq_calibration_result(guard.handle, &info), int(TMDQ_OK));
    QCOMPARE(std::string(info.summary_utf8), std::string("Calibration extraction cancelled."));
    QCOMPARE(std::string(info.error_utf8), std::string());
}

void TestAbi::freeingACalibrationInFlightCancelsAndWaits()
{
    const StepCal f = loadStepCal();
    if (!f.ok || shippedSteps().empty())
        QSKIP("step_cal fixture or the shipped step file not available");

    CalRequest request{CalCase(f)};
    auto seen = std::make_unique<Seen>();
    const tmdq_callbacks callbacks = seen->callbacks();
    tmdq_calibration* handle = nullptr;
    QCOMPARE(tmdq_calibration_start(&request.req, &callbacks, &handle), int(TMDQ_OK));
    tmdq_calibration_free(handle);   // no cancel, no wait: free does both

    QCOMPARE(seen->finishedCount, 1);
    QCOMPARE(seen->finishedSuccess, 0);
    QCOMPARE(seen->snapshot().back(), std::string("finished:0"));
    seen.reset();
}

void TestAbi::waitAloneIsEnoughToSeeACalibrationEnd()
{
    const StepCal f = loadStepCal();
    if (!f.ok || shippedSteps().empty())
        QSKIP("step_cal fixture or the shipped step file not available");

    CalRequest request{CalCase(f)};
    Seen seen;
    const tmdq_callbacks callbacks = seen.callbacks();
    CalGuard guard;
    QCOMPARE(tmdq_calibration_start(&request.req, &callbacks, &guard.handle), int(TMDQ_OK));
    tmdq_calibration_wait(guard.handle);
    QCOMPARE(tmdq_calibration_running(guard.handle), 0);
    QCOMPARE(seen.finishedCount, 1);
    QCOMPARE(seen.finishedSuccess, 1);
    tmdq_calibration_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_calibration_result(guard.handle, &info), int(TMDQ_OK));
    QCOMPARE(info.channel_count, 48);
    tmdq_calibration_wait(guard.handle);   // and again is harmless
}

void TestAbi::aThresholdThatHadToBeRelaxedIsReported()
{
    const StepCal f = loadStepCal();
    if (!f.ok || shippedSteps().empty())
        QSKIP("step_cal fixture or the shipped step file not available");

    // Four expected steps against a recording that sweeps eleven: the nominal edge threshold cannot resolve
    // so few levels and some channels only pair after it is lowered. The flag says so, and it is the one
    // per-channel flag the ordinary eleven-step run never sets.
    const CalCase c(f, { 0.0, 6.0, 12.0, 18.0 });
    const DirectCalibration want = runDirectCalibration(c);
    QVERIFY(want.ok);
    int relaxed = 0;
    for (const CalibrationChannelResult& r : want.results)
        relaxed += r.thresholdRelaxed ? 1 : 0;
    QVERIFY2(relaxed > 0, "the fixture should need a relaxed threshold with four steps, or this proves nothing");

    CalRequest request(c);
    Seen seen;
    CalGuard guard;
    QVERIFY2(calibrateToTheEnd(request, seen, guard), seen.joined().c_str());
    const QString diff = calibrationDifferences(guard.handle, want);
    QVERIFY2(diff.isEmpty(), qPrintable(diff));
    int reported = 0;
    for (int32_t i = 0; i < int(want.results.size()); ++i)
    {
        tmdq_calibration_channel ch{};
        ch.struct_size = sizeof(ch);
        QCOMPARE(tmdq_calibration_channel_result(guard.handle, i, &ch), int(TMDQ_OK));
        reported += ch.threshold_relaxed;
    }
    QCOMPARE(reported, relaxed);
}

void TestAbi::aFrameSyncFileIsReadLikeTheCore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QStringList paths;
    for (const char* name : { "framesync_PRN11.toml", "framesync_PRN15.toml", "framesync_rcvr_default.toml" })
        paths << projectRoot() + "/settings/framesync_patterns/" + name;
    // Each way a file can be odd.
    const std::vector<std::pair<const char*, QByteArray>> odd = {
        {"legacy", "[Frame]\nFrameSync = \"FE6B2840\"\nWordsInMinorFrame = 50\n"},
        {"legacy-too-small", "[Frame]\nFrameSync = \"FE6B2840\"\nWordsInMinorFrame = 1\n"},
        {"bits-too-small", "[Frame]\nBitsPerFrame = 3\nFrameSync = \"AB\"\n"},
        {"bits-and-legacy", "[Frame]\nBitsPerFrame = 800\nWordsInMinorFrame = 50\n"},
        {"no-randomized", "[Frame]\nBitsPerFrame = 800\nFrameSync = \"FE6B2840\"\nInverted = true\n"},
        {"randomized", "[Frame]\nBitsPerFrame = 800\nFrameSync = \"FE6B2840\"\nFrameSyncMask = \"FFFF0000\"\nRandomized = true\nInverted = false\n"},
        {"empty-values", "[Frame]\nFrameSync = \"\"\nFrameSyncMask = \"\"\n"},
        {"bom", QByteArray("\xEF\xBB\xBF[Frame]\nBitsPerFrame = 8000\nFrameSync = \"334AABBF\"\n")},
        {"non-ascii", QByteArray("[Frame]\nFrameSync = \"\xC3\xA9\xC3\xA8\"\nBitsPerFrame = 800\n")},
        {"empty-file", QByteArray()},
        {"junk", "this is {{ not toml\n"},
    };
    for (const auto& [name, bytes] : odd)
        paths << writeFile(dir.filePath(QString::fromLatin1(name) + ".toml"), bytes);
    paths << dir.filePath("does-not-exist.toml");

    for (const QString& path : paths)
    {
        QVERIFY2(!path.isEmpty(), "a fixture could not be written");
        const std::string utf8 = path.toStdString();
        const FrameSyncFile want = readFrameSyncFile(toFsPath(path));
        tmdq_frame_sync_file* file = nullptr;
        QCOMPARE(tmdq_frame_sync_file_read(utf8.c_str(), &file), int(TMDQ_OK));
        QVERIFY(file != nullptr);
        QCOMPARE(describeSyncFile(file), describeSyncFile(want));
        tmdq_frame_sync_file_free(file);
    }

    // The shipped PRN15 file is not "nothing": the comparison above is not vacuous.
    const FrameSyncFile prn = readFrameSyncFile(toFsPath(paths[1]));
    QCOMPARE(prn.pattern, std::string("334AABBF"));
    QVERIFY(prn.bitsPerFrame.has_value());
    QVERIFY(prn.randomized.has_value());

    // Arguments.
    tmdq_frame_sync_file* none = reinterpret_cast<tmdq_frame_sync_file*>(std::uintptr_t{1});
    QCOMPARE(tmdq_frame_sync_file_read(nullptr, &none), int(TMDQ_ERR_BADARG));
    QVERIFY(none == nullptr);
    QCOMPARE(tmdq_frame_sync_file_read("x", nullptr), int(TMDQ_ERR_BADARG));
    tmdq_frame_sync_file_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_frame_sync_file_info_get(nullptr, &info), int(TMDQ_ERR_BADARG));
    const std::string good = paths[1].toStdString();
    tmdq_frame_sync_file* file = nullptr;
    QCOMPARE(tmdq_frame_sync_file_read(good.c_str(), &file), int(TMDQ_OK));
    QCOMPARE(tmdq_frame_sync_file_info_get(file, nullptr), int(TMDQ_ERR_BADARG));
    info.struct_size = sizeof(info) - 1;
    QCOMPARE(tmdq_frame_sync_file_info_get(file, &info), int(TMDQ_ERR_BADARG));
    tmdq_frame_sync_file_free(file);
    tmdq_frame_sync_file_free(nullptr);
}

void TestAbi::aFrameSyncFileIsWrittenLikeTheCore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    struct Args { const char* pattern; const char* mask; int bits; int inverted; int randomized; };
    const Args cases[] = {
        {"FE6B2840", "", 800, 0, 0},
        {"334AABBF", "FFFFFFFF", 32767, 0, 1},
        {"A345CA5C", "FFFF0000", 2047, 1, 0},
        {"\xC3\xA9", "", 8000, 1, 1},
        {"", "", 0, 0, 0},
    };
    int n = 0;
    for (const Args& a : cases)
    {
        const QString viaAbi = dir.filePath(QString("abi%1.toml").arg(n));
        const QString viaCore = dir.filePath(QString("core%1.toml").arg(n));
        ++n;
        const std::string utf8 = viaAbi.toStdString();
        QCOMPARE(tmdq_frame_sync_file_write(utf8.c_str(), a.pattern, a.mask, a.bits, a.inverted, a.randomized), int(TMDQ_OK));
        QVERIFY(writeFrameSyncFile(toFsPath(viaCore), a.pattern, a.mask, a.bits, a.inverted != 0, a.randomized != 0));
        QCOMPARE(slurp(viaAbi), slurp(viaCore));

        // And it reads back as what was written.
        tmdq_frame_sync_file* file = nullptr;
        QCOMPARE(tmdq_frame_sync_file_read(utf8.c_str(), &file), int(TMDQ_OK));
        QCOMPARE(describeSyncFile(file), describeSyncFile(readFrameSyncFile(toFsPath(viaCore))));
        tmdq_frame_sync_file_free(file);
    }

    // NULL text reads as empty; a path that cannot be written is an I/O error, not a crash.
    const QString viaNull = dir.filePath("null.toml");
    const std::string nullUtf8 = viaNull.toStdString();
    QCOMPARE(tmdq_frame_sync_file_write(nullUtf8.c_str(), nullptr, nullptr, 800, 0, 0), int(TMDQ_OK));
    const QString viaEmpty = dir.filePath("empty.toml");
    QVERIFY(writeFrameSyncFile(toFsPath(viaEmpty), "", "", 800, false, false));
    QCOMPARE(slurp(viaNull), slurp(viaEmpty));

    const std::string dirPath = dir.path().toStdString();   // a directory is not a file
    QCOMPARE(tmdq_frame_sync_file_write(dirPath.c_str(), "AB", "", 800, 0, 0), int(TMDQ_ERR_IO));
    QVERIFY(!std::string(tmdq_last_error()).empty());
    QCOMPARE(tmdq_frame_sync_file_write(nullptr, "AB", "", 800, 0, 0), int(TMDQ_ERR_BADARG));
}

void TestAbi::templateMatchingAgreesWithTheCore()
{
    auto run = [](const std::vector<int>& t, const std::vector<int>& f) {
        tmdq_match* match = nullptr;
        const int32_t status = tmdq_template_match(t.empty() ? nullptr : t.data(), int(t.size()),
                                                   f.empty() ? nullptr : f.data(), int(f.size()), &match);
        return std::pair<int32_t, tmdq_match*>{status, match};
    };
    auto same = [](tmdq_match* m, const TemplateMatcher::MatchResult& want) {
        tmdq_match_info info{};
        info.struct_size = sizeof(info);
        if (tmdq_match_info_get(m, &info) != TMDQ_OK) return false;
        if ((info.ok != 0) != want.ok || std::size_t(info.missing_count) != want.missing.size() ||
            std::size_t(info.extra_count) != want.extra.size())
            return false;
        for (int32_t i = 0; i < info.missing_count; ++i) if (info.missing[i] != want.missing[std::size_t(i)]) return false;
        for (int32_t i = 0; i < info.extra_count; ++i) if (info.extra[i] != want.extra[std::size_t(i)]) return false;
        return true;
    };

    // Fixed cases first: equal sets, order and repeats, missing, extra, both, empty.
    const std::vector<std::pair<std::vector<int>, std::vector<int>>> fixed = {
        {{13, 14, 15}, {15, 14, 13}}, {{13, 13, 14}, {14, 13}}, {{13, 14, 15}, {13, 14}}, {{13}, {13, 14, 15}},
        {{1, 2, 3}, {3, 4, 5}}, {{}, {}}, {{}, {7}}, {{7}, {}},
    };
    for (const auto& [t, f] : fixed)
    {
        auto [status, match] = run(t, f);
        QCOMPARE(status, int(TMDQ_OK));
        QVERIFY(same(match, TemplateMatcher::match(t, f)));
        tmdq_match_free(match);
    }
    // The comparison is not vacuous: one of those really is a mismatch, another really matches.
    QVERIFY(!TemplateMatcher::match({1, 2, 3}, {3, 4, 5}).ok);
    QVERIFY(TemplateMatcher::match({13, 13, 14}, {14, 13}).ok);

    // Random sets, with repeats.
    std::mt19937 rng(20261023);
    int matched = 0, mismatched = 0;
    for (int i = 0; i < 2000; ++i)
    {
        std::vector<int> t, f;
        const int nt = int(rng() % 6), nf = int(rng() % 6);
        for (int k = 0; k < nt; ++k) t.push_back(int(rng() % 8));
        for (int k = 0; k < nf; ++k) f.push_back(int(rng() % 8));
        auto [status, match] = run(t, f);
        QCOMPARE(status, int(TMDQ_OK));
        const auto want = TemplateMatcher::match(t, f);
        QVERIFY2(same(match, want), qPrintable(QString("case %1").arg(i)));
        (want.ok ? matched : mismatched)++;
        tmdq_match_free(match);
    }
    QVERIFY2(matched > 20 && mismatched > 500, qPrintable(QString("%1 matched, %2 mismatched").arg(matched).arg(mismatched)));

    // Arguments.
    tmdq_match* none = reinterpret_cast<tmdq_match*>(std::uintptr_t{1});
    const int32_t ids[] = {1, 2};
    QCOMPARE(tmdq_template_match(ids, -1, ids, 2, &none), int(TMDQ_ERR_BADARG));
    QVERIFY(none == nullptr);
    QCOMPARE(tmdq_template_match(ids, 2, ids, -1, &none), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_template_match(nullptr, 2, ids, 2, &none), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_template_match(ids, 2, nullptr, 2, &none), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_template_match(ids, 2, ids, 2, nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_template_match(nullptr, 0, nullptr, 0, &none), int(TMDQ_OK));   // two empty sets
    tmdq_match_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_match_info_get(none, &info), int(TMDQ_OK));
    QCOMPARE(info.ok, 1);
    QCOMPARE(tmdq_match_info_get(none, nullptr), int(TMDQ_ERR_BADARG));
    info.struct_size = sizeof(info) - 1;
    QCOMPARE(tmdq_match_info_get(none, &info), int(TMDQ_ERR_BADARG));
    tmdq_match_free(none);
    QCOMPARE(tmdq_match_info_get(nullptr, &info), int(TMDQ_ERR_BADARG));
    tmdq_match_free(nullptr);
}

void TestAbi::aCsvIsParsedLikeTheCore()
{
    // The CSV exports kept in tests/data are not committed (that folder is ignored apart from the recordings), so
    // they are extra coverage where they exist and nothing is skipped or required where they do not. The files
    // written below are what the comparison stands on.
    QStringList files;
    for (const char* name : { "Framesync_SNR Plot.csv", "PRN_TEST_FILE0001 - Copy.csv" })
        if (QFileInfo::exists(dataPath(name)))
            files << dataPath(name);

    // One written by the writer too, with every metric and a second source.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CsvColumns cols;
    cols.add(TMDQ_METRIC_FRAME_SYNC_LOCK, "Ch 13", 0, {0.0, 1.0, 2.5}, {99.5, 100.0, 12.25});
    cols.add(TMDQ_METRIC_ACCUMULATED_MISSED_FRAMES, "Ch 13", 0, {0.0, 1.0, 2.5}, {0.0, 3.0, 7.0});
    cols.add(TMDQ_METRIC_SNR, "13 - Prn L_RCVR2", 2, {0.0, 1.0, 2.5}, {-3.5, 10.0, 22.125});
    cols.finish();
    const QString generated = dir.filePath("generated.csv");
    QVERIFY(CsvSeriesWriter::write(toFsPath(generated), cols.core, CsvTimeBase{120, 77125.5}));
    files << generated;

    // One with a malformed row in it, which the parser skips and counts.
    const QString skipping = writeFile(dir.filePath("skipping.csv"),
        "Time (DOY:HH:MM:SS.mmm),A Lock (%),B Accumulated Missed Frames\n"
        "120:00:00:00.000,5,1\n"
        "not-a-time,6,2\n"
        "120:00:00:01.000\n"
        "120:00:00:02.000,7,3\n");
    files << skipping;

    bool sawSkipped = false, sawStreamOrder = false;
    for (const QString& path : files)
    {
        const std::string utf8 = path.toStdString();
        const CsvParseResult want = CsvSeriesParser::parse(toFsPath(path));
        QVERIFY2(want.success, qPrintable(path));
        QVERIFY(want.series.size() >= 2);
        sawSkipped = sawSkipped || want.skippedRows > 0;
        for (const CsvSeries& series : want.series)
            sawStreamOrder = sawStreamOrder || series.streamOrder == 13;
        tmdq_csv* csv = nullptr;
        QCOMPARE(tmdq_csv_parse(utf8.c_str(), &csv), int(TMDQ_OK));
        QVERIFY(csv != nullptr);
        const QString diff = csvDifferences(csv, want);
        QVERIFY2(diff.isEmpty(), qPrintable(path + ": " + diff));
        tmdq_csv_free(csv);
    }
    QVERIFY2(sawSkipped && sawStreamOrder, "the files should include a skipped row and a stream order, or the comparison proves little");

    // Files the parser refuses: not there, junk, the wrong header, a header and no data.
    const QString junk = writeFile(dir.filePath("junk.csv"), "this is not a series file\n1,2,3\n");
    const QString wrongHeader = writeFile(dir.filePath("wrong.csv"), "Day,Time,A Lock (%)\n1,00:00:00,5\n");
    const QString noData = writeFile(dir.filePath("nodata.csv"), "Time (DOY:HH:MM:SS.mmm),A Lock (%)\n");
    for (const QString& path : { QDir::tempPath() + "/tst_abi_no_such.csv", junk, wrongHeader, noData })
    {
        QVERIFY(!CsvSeriesParser::parse(toFsPath(path)).success);
        const std::string utf8 = path.toStdString();
        tmdq_csv* csv = reinterpret_cast<tmdq_csv*>(std::uintptr_t{1});
        QCOMPARE(tmdq_csv_parse(utf8.c_str(), &csv), int(TMDQ_ERR_PREPARE));
        QVERIFY(csv == nullptr);
        QVERIFY(!std::string(tmdq_last_error()).empty());
    }

    // Arguments and indices.
    tmdq_csv* csv = nullptr;
    QCOMPARE(tmdq_csv_parse(nullptr, &csv), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_csv_parse("x", nullptr), int(TMDQ_ERR_BADARG));
    const std::string good = generated.toStdString();
    QCOMPARE(tmdq_csv_parse(good.c_str(), &csv), int(TMDQ_OK));
    tmdq_csv_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_csv_info_get(csv, &info), int(TMDQ_OK));
    tmdq_csv_series_info series{};
    series.struct_size = sizeof(series);
    QCOMPARE(tmdq_csv_series_get(csv, info.series_count, &series), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_csv_series_get(csv, -1, &series), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_csv_series_get(csv, 0, nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_csv_info_get(csv, nullptr), int(TMDQ_ERR_BADARG));
    tmdq_csv_info shortInfo{};
    shortInfo.struct_size = sizeof(shortInfo) - 1;
    QCOMPARE(tmdq_csv_info_get(csv, &shortInfo), int(TMDQ_ERR_BADARG));
    tmdq_csv_series_info shortSeries{};
    shortSeries.struct_size = sizeof(shortSeries) - 1;
    QCOMPARE(tmdq_csv_series_get(csv, 0, &shortSeries), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_csv_info_get(nullptr, &info), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_csv_series_get(nullptr, 0, &series), int(TMDQ_ERR_BADARG));
    tmdq_csv_free(csv);
    tmdq_csv_free(nullptr);
}

void TestAbi::aCsvIsWrittenLikeTheCore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Columns that exercise the writer: every metric, two sources, a name with non-ASCII text (a comma cannot be written: the format has no quoting),
    // times that fall in the same millisecond, a column with more times than values, and an empty one.
    CsvColumns cols;
    cols.add(TMDQ_METRIC_FRAME_SYNC_LOCK, "Ch 13", 0, {0.0, 1.0, 2.0, 2.0004}, {99.5, 100.0, 12.25, 13.5});
    cols.add(TMDQ_METRIC_ACCUMULATED_MISSED_FRAMES, "Ch 13", 0, {0.0, 1.0, 2.0}, {0.0, 3.0, 7.0});
    cols.add(TMDQ_METRIC_SNR, "Ch 13 L_RCVR2", 0, {0.0, 1.5}, {-3.5, 10.0});
    cols.add(TMDQ_METRIC_SNR, "S\xC3\xA9rie 2 L_RCVR1", 2, {0.5, 1.5, 2.5}, {1.0, 2.0, 3.0});
    cols.add(TMDQ_METRIC_FRAME_SYNC_LOCK, "uneven", 3, {0.0, 1.0, 2.0}, {5.0, 6.0});
    cols.add(TMDQ_METRIC_FRAME_SYNC_LOCK, "empty", 3, {}, {});
    cols.finish();

    const QString viaAbi = dir.filePath("abi.csv");
    const QString viaCore = dir.filePath("core.csv");
    const std::string utf8 = viaAbi.toStdString();
    QCOMPARE(tmdq_csv_write(utf8.c_str(), cols.pointers.data(), int(cols.pointers.size()), 120, 77125.5), int(TMDQ_OK));
    QVERIFY(CsvSeriesWriter::write(toFsPath(viaCore), cols.core, CsvTimeBase{120, 77125.5}));
    QCOMPARE(slurp(viaAbi), slurp(viaCore));
    QVERIFY(slurp(viaAbi).size() > 200);

    // The time base reaches the stamps.
    const QString viaOtherBase = dir.filePath("other.csv");
    const std::string otherUtf8 = viaOtherBase.toStdString();
    QCOMPARE(tmdq_csv_write(otherUtf8.c_str(), cols.pointers.data(), int(cols.pointers.size()), 7, 100.25), int(TMDQ_OK));
    QVERIFY(CsvSeriesWriter::write(toFsPath(dir.filePath("other-core.csv")), cols.core, CsvTimeBase{7, 100.25}));
    QCOMPARE(slurp(viaOtherBase), slurp(dir.filePath("other-core.csv")));
    QVERIFY(slurp(viaOtherBase) != slurp(viaAbi));

    // And what the ABI wrote parses back, through the ABI, as the core parses it.
    tmdq_csv* csv = nullptr;
    QCOMPARE(tmdq_csv_parse(utf8.c_str(), &csv), int(TMDQ_OK));
    const QString diff = csvDifferences(csv, CsvSeriesParser::parse(toFsPath(viaAbi)));
    QVERIFY2(diff.isEmpty(), qPrintable(diff));
    tmdq_csv_free(csv);

    // Refusals.
    QCOMPARE(tmdq_csv_write(nullptr, cols.pointers.data(), 1, 0, 0.0), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_csv_write(utf8.c_str(), cols.pointers.data(), 0, 0, 0.0), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_csv_write(utf8.c_str(), cols.pointers.data(), -1, 0, 0.0), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_csv_write(utf8.c_str(), nullptr, 1, 0, 0.0), int(TMDQ_ERR_BADARG));
    const tmdq_csv_column* withNull[] = { cols.pointers[0], nullptr };
    QCOMPARE(tmdq_csv_write(utf8.c_str(), withNull, 2, 0, 0.0), int(TMDQ_ERR_BADARG));
    QVERIFY(std::string(tmdq_last_error()).find("column 1") != std::string::npos);
    tmdq_csv_column shortColumn = cols.wire[0];
    shortColumn.struct_size = sizeof(shortColumn) - 1;
    const tmdq_csv_column* withShort[] = { &shortColumn };
    QCOMPARE(tmdq_csv_write(utf8.c_str(), withShort, 1, 0, 0.0), int(TMDQ_ERR_BADARG));
    tmdq_csv_column badMetric = cols.wire[0];
    badMetric.metric_type = 9;
    const tmdq_csv_column* withBadMetric[] = { &badMetric };
    QCOMPARE(tmdq_csv_write(utf8.c_str(), withBadMetric, 1, 0, 0.0), int(TMDQ_ERR_BADARG));
    QVERIFY(std::string(tmdq_last_error()).find("metric") != std::string::npos);
    tmdq_csv_column negativeCount = cols.wire[0];
    negativeCount.x_count = -1;
    const tmdq_csv_column* withNegative[] = { &negativeCount };
    QCOMPARE(tmdq_csv_write(utf8.c_str(), withNegative, 1, 0, 0.0), int(TMDQ_ERR_BADARG));
    tmdq_csv_column noArray = cols.wire[0];
    noArray.y_values = nullptr;
    const tmdq_csv_column* withNoArray[] = { &noArray };
    QCOMPARE(tmdq_csv_write(utf8.c_str(), withNoArray, 1, 0, 0.0), int(TMDQ_ERR_BADARG));
    tmdq_csv_column noName = cols.wire[0];
    noName.name_utf8 = nullptr;
    const tmdq_csv_column* withNoName[] = { &noName };
    QCOMPARE(tmdq_csv_write(dir.filePath("noname.csv").toStdString().c_str(), withNoName, 1, 0, 0.0), int(TMDQ_OK));   // a NULL name reads as empty

    // A path that cannot be created is an I/O error.
    const std::string dirPath = dir.path().toStdString();
    QCOMPARE(tmdq_csv_write(dirPath.c_str(), cols.pointers.data(), 1, 0, 0.0), int(TMDQ_ERR_IO));
    QVERIFY(!std::string(tmdq_last_error()).empty());
}

void TestAbi::clockStampsAgreeWithTheCore()
{
    std::mt19937 rng(20261023);
    int checked = 0;
    for (int i = 0; i < 3000; ++i)
    {
        const int day = int(rng() % 400) - 5;
        const double base = double(rng() % 86400) + double(rng() % 1000) / 1000.0;
        const double elapsed = (double(int(rng() % 400000)) - 20000.0) / 7.0;
        const bool ms = rng() % 2;
        const std::string want = clocktime::format(day, base, elapsed, ms);

        char exact[64] = {};
        QCOMPARE(tmdq_clock_time_format(day, base, elapsed, ms ? 1 : 0, exact, sizeof(exact)), int(want.size()));
        QCOMPARE(std::string(exact), want);
        QCOMPARE(tmdq_clock_time_format(day, base, elapsed, ms ? 1 : 0, nullptr, 0), int(want.size()));
        ++checked;
    }
    QCOMPARE(checked, 3000);

    // A known stamp, so the oracle is not the only witness: 1 day, 1 h, 1 min, 1.5 s after 120:00:00:00.
    char buf[64] = {};
    QVERIFY(tmdq_clock_time_format(120, 0.0, 86400.0 + 3600.0 + 60.0 + 1.5, 1, buf, sizeof(buf)) > 0);
    QCOMPARE(std::string(buf), std::string("121:01:01:01.500"));
    QVERIFY(tmdq_clock_time_format(120, 0.0, 86400.0 + 3600.0 + 60.0 + 1.5, 0, buf, sizeof(buf)) > 0);
    QCOMPARE(std::string(buf), std::string("121:01:01:01"));

    // Truncation and refusals.
    char tight[6];
    std::memset(tight, 'x', sizeof(tight));
    QCOMPARE(tmdq_clock_time_format(120, 0.0, 0.0, 1, tight, sizeof(tight)), 16);
    QCOMPARE(std::string(tight), std::string("120:0"));
    QCOMPARE(int(tight[5]), 0);
    QCOMPARE(tmdq_clock_time_format(120, 0.0, 0.0, 1, nullptr, 10), -1);
    QCOMPARE(tmdq_clock_time_format(120, 0.0, 0.0, 1, buf, -1), -1);
}

void TestAbi::aStreamBecomesTheSeriesTheCoreBuilds()
{
    std::mt19937 rng(20261024);
    int withLock = 0, withMissed = 0, withChannels = 0, empty = 0, nan = 0;
    for (int i = 0; i < 1500; ++i)
    {
        const ProcessedStreamData data = randomStream(rng);
        const double base = (rng() % 3 == 0) ? 0.0 : 1.7e9 + double(rng() % 400) / 4.0;
        const StreamSeries want = buildStreamSeries(data, base);

        tmdq_stream_result* result = makeResult(data);
        tmdq_series_set* set = nullptr;
        QCOMPARE(tmdq_stream_result_series(result, base, &set), int(TMDQ_OK));
        QVERIFY(set != nullptr);
        const QString diff = seriesDifferences(set, want);
        QVERIFY2(diff.isEmpty(), qPrintable(QString("case %1: ").arg(i) + diff));
        tmdq_series_set_free(set);
        tmdq_stream_result_free(result);

        withLock += !data.lockPercent.empty();
        withMissed += !data.accumulatedMissedFrames.empty();
        withChannels += !data.channels.empty();
        empty += want.series.empty();
        for (const SeriesSpec& s : want.series)
            nan += std::isnan(s.yMin) || std::isinf(s.yMax);
    }
    // The generator really reached each kind of stream.
    QVERIFY2(withLock > 800 && withMissed > 700 && withChannels > 900 && empty > 20 && nan > 20,
             qPrintable(QString("%1 %2 %3 %4 %5").arg(withLock).arg(withMissed).arg(withChannels).arg(empty).arg(nan)));

    // A known stream, so the oracle is not the only witness: lock %, missed frames and two receiver channels.
    ProcessedStreamData known;
    known.streamLabel = "Rcvr";
    known.pcmChannelId = 40;
    known.jobIndex = 2;
    known.sourceId = 1;
    known.timesSec = { 1000.0, 1001.0 };
    known.lockPercent = { 99.0, 98.0 };
    known.accumulatedMissedFrames = { 0.0, 4.0 };
    known.channels.push_back({"L_RCVR1", 1, {-80.0, -79.0}});
    known.channels.push_back({"R_RCVR2", 2, {-70.0, -71.0}});
    tmdq_stream_result* result = makeResult(known);
    tmdq_series_set* set = nullptr;
    QCOMPARE(tmdq_stream_result_series(result, 990.0, &set), int(TMDQ_OK));
    tmdq_series_set_info info{};
    info.struct_size = sizeof(info);
    QCOMPARE(tmdq_series_set_info_get(set, &info), int(TMDQ_OK));
    QCOMPARE(info.series_count, 4);
    QCOMPARE(info.elapsed_count, 2);
    QCOMPARE(info.elapsed[0], 10.0);
    QCOMPARE(info.elapsed[1], 11.0);
    tmdq_series_spec spec{};
    spec.struct_size = sizeof(spec);
    QCOMPARE(tmdq_series_set_spec(set, 3, &spec), int(TMDQ_OK));
    QCOMPARE(spec.receiver_index, 2);
    QCOMPARE(spec.stream_order, 40);
    QCOMPARE(spec.stream_sequence, 2);
    QCOMPARE(spec.source_id, 1);
    QCOMPARE(spec.y_min, -71.0);
    QCOMPARE(spec.y_max, -70.0);

    // The set keeps the stream's data alive after the host has freed the result.
    tmdq_stream_result_free(result);
    QCOMPARE(spec.y_values[0], -70.0);
    QCOMPARE(tmdq_series_set_spec(set, 2, &spec), int(TMDQ_OK));
    QCOMPARE(spec.y_values[1], -79.0);
    QCOMPARE(std::string(spec.name_utf8).empty(), false);
    tmdq_series_set_free(set);

    // Arguments.
    tmdq_series_set* none = reinterpret_cast<tmdq_series_set*>(std::uintptr_t{1});
    QCOMPARE(tmdq_stream_result_series(nullptr, 0.0, &none), int(TMDQ_ERR_BADARG));
    QVERIFY(none == nullptr);
    tmdq_stream_result* again = makeResult(known);
    QCOMPARE(tmdq_stream_result_series(again, 0.0, nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_stream_result_series(again, 0.0, &set), int(TMDQ_OK));
    QCOMPARE(tmdq_series_set_info_get(nullptr, &info), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_series_set_info_get(set, nullptr), int(TMDQ_ERR_BADARG));
    info.struct_size = sizeof(info) - 1;
    QCOMPARE(tmdq_series_set_info_get(set, &info), int(TMDQ_ERR_BADARG));
    spec.struct_size = sizeof(spec);
    QCOMPARE(tmdq_series_set_spec(set, 4, &spec), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_series_set_spec(set, -1, &spec), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_series_set_spec(set, 0, nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_series_set_spec(nullptr, 0, &spec), int(TMDQ_ERR_BADARG));
    spec.struct_size = sizeof(spec) - 1;
    QCOMPARE(tmdq_series_set_spec(set, 0, &spec), int(TMDQ_ERR_BADARG));
    tmdq_series_set_free(set);
    tmdq_series_set_free(nullptr);
    tmdq_stream_result_free(again);
}

void TestAbi::theTimeBaseMovesLikeTheCore()
{
    std::mt19937 rng(20261024);
    auto info = [](const tmdq_time_base* b) {
        tmdq_time_base_info i{};
        i.struct_size = sizeof(i);
        tmdq_time_base_info_get(b, &i);
        return i;
    };
    int adopted = 0, shifted = 0, advanced = 0, moved = 0, overlapped = 0, apart = 0;
    for (int run = 0; run < 700; ++run)
    {
        tmdq_time_base* abi = nullptr;
        QCOMPARE(tmdq_time_base_create(&abi), int(TMDQ_OK));
        SeriesTimeBase core;
        for (int step = 0; step < 14; ++step)
        {
            const int op = int(rng() % 6);
            if (op == 0)
            {
                QCOMPARE(tmdq_time_base_reset(abi), int(TMDQ_OK));
                core.reset();
            }
            else if (op == 1)
            {
                const int day = int(rng() % 400) - 5;
                const double offset = double(rng() % 86400) / 3.0;
                QCOMPARE(tmdq_time_base_set_clock(abi, day, offset), int(TMDQ_OK));
                core.setClock(day, offset);
            }
            else if (op == 2 || op == 3)
            {
                const double first = (rng() % 5 == 0) ? 0.0 : 1.7e9 + double(rng() % 100000) / 8.0 + (rng() % 2) * 86400.0 * 400;
                const bool has = rng() % 2;
                double shift = -1.0;
                QCOMPARE(tmdq_time_base_adopt(abi, first, has ? 1 : 0, &shift), int(TMDQ_OK));
                const double want = core.adopt(first, has);
                QCOMPARE(shift, want);
                ++adopted;
                shifted += want != 0.0;
            }
            else if (op == 4)
            {
                const bool has = rng() % 3 != 0;
                const double earliest = double(int(rng() % 4000) - 500) / 8.0;
                double shift = -1.0;
                QCOMPARE(tmdq_time_base_advance(abi, has ? 1 : 0, earliest, &shift), int(TMDQ_OK));
                const double want = core.advance(has ? std::optional<double>(earliest) : std::nullopt);
                QCOMPARE(shift, want);
                ++advanced;
                moved += want != 0.0;
            }
            else
            {
                // Around the base, so the new source sometimes touches what is loaded and sometimes does not.
                const double lo = core.baseAbsSeconds() + double(int(rng() % 600) - 200) / 4.0;
                const double hi = lo + double(rng() % 200) / 4.0;
                const double existing = double(rng() % 200) / 4.0;
                int32_t out = -1;
                QCOMPARE(tmdq_time_base_overlaps(abi, lo, hi, existing, &out), int(TMDQ_OK));
                QCOMPARE(out != 0, core.overlaps(lo, hi, existing));
                ++(out ? overlapped : apart);
            }
            const tmdq_time_base_info i = info(abi);
            QCOMPARE(i.base_day, core.baseDay());
            QCOMPARE(i.base_abs_seconds, core.baseAbsSeconds());
            QCOMPARE(i.base_time_offset, core.baseTimeOffset());
        }
        tmdq_time_base_free(abi);
    }
    QVERIFY2(adopted > 2000 && shifted > 200 && advanced > 1000 && moved > 100 && overlapped > 100 && apart > 100,
             qPrintable(QString("%1 %2 %3 %4 %5 %6").arg(adopted).arg(shifted).arg(advanced).arg(moved).arg(overlapped).arg(apart)));

    // Arguments.
    tmdq_time_base* base = reinterpret_cast<tmdq_time_base*>(std::uintptr_t{1});
    QCOMPARE(tmdq_time_base_create(nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_time_base_create(&base), int(TMDQ_OK));
    tmdq_time_base_info bi{};
    bi.struct_size = sizeof(bi);
    QCOMPARE(tmdq_time_base_info_get(base, &bi), int(TMDQ_OK));
    QCOMPARE(bi.base_day, 0);
    QCOMPARE(bi.base_abs_seconds, 0.0);
    QCOMPARE(bi.base_time_offset, 0.0);
    double shift = 0.0;
    int32_t out = 0;
    QCOMPARE(tmdq_time_base_info_get(nullptr, &bi), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_time_base_info_get(base, nullptr), int(TMDQ_ERR_BADARG));
    bi.struct_size = sizeof(bi) - 1;
    QCOMPARE(tmdq_time_base_info_get(base, &bi), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_time_base_reset(nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_time_base_set_clock(nullptr, 1, 2.0), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_time_base_adopt(nullptr, 1.0, 1, &shift), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_time_base_adopt(base, 1.0, 1, nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_time_base_advance(nullptr, 1, 1.0, &shift), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_time_base_advance(base, 1, 1.0, nullptr), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_time_base_overlaps(nullptr, 0.0, 1.0, 1.0, &out), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_time_base_overlaps(base, 0.0, 1.0, 1.0, nullptr), int(TMDQ_ERR_BADARG));
    tmdq_time_base_free(base);
    tmdq_time_base_free(nullptr);
}

void TestAbi::streamKeysIdentifyTheSameStream()
{
    ProcessedStreamData a;
    a.sourceId = 1;
    a.streamLabel = "Ch 13";
    a.pcmChannelId = 13;
    tmdq_stream_result* ra = makeResult(a);
    tmdq_stream_key ka{};
    ka.struct_size = sizeof(ka);
    QCOMPARE(tmdq_stream_result_key(ra, &ka), int(TMDQ_OK));
    QCOMPARE(ka.source_id, 1);
    QCOMPARE(std::string(ka.label_utf8), std::string("Ch 13"));
    QCOMPARE(ka.channel, 13);

    // Every difference of one field is a different stream; none is the same one.
    auto keyOf = [](ProcessedStreamData d, tmdq_stream_result** keep) {
        *keep = makeResult(d);
        tmdq_stream_key k{};
        k.struct_size = sizeof(k);
        tmdq_stream_result_key(*keep, &k);
        return k;
    };
    tmdq_stream_result *r1 = nullptr, *r2 = nullptr, *r3 = nullptr, *r4 = nullptr;
    ProcessedStreamData same = a;
    ProcessedStreamData otherSource = a; otherSource.sourceId = 2;
    ProcessedStreamData otherLabel = a; otherLabel.streamLabel = "Ch 14";
    ProcessedStreamData otherChannel = a; otherChannel.pcmChannelId = 14;
    const tmdq_stream_key ksame = keyOf(same, &r1), ksource = keyOf(otherSource, &r2), klabel = keyOf(otherLabel, &r3),
                          kchannel = keyOf(otherChannel, &r4);
    QCOMPARE(tmdq_stream_key_equal(&ka, &ksame), 1);
    QCOMPARE(tmdq_stream_key_equal(&ka, &ka), 1);
    QCOMPARE(tmdq_stream_key_equal(&ka, &ksource), 0);
    QCOMPARE(tmdq_stream_key_equal(&ka, &klabel), 0);
    QCOMPARE(tmdq_stream_key_equal(&ka, &kchannel), 0);
    QCOMPARE(tmdq_stream_key_equal(&ksource, &ka), 0);
    // The same answers as the core's own key.
    QVERIFY(streamKeyOf(a) == streamKeyOf(same));
    QVERIFY(!(streamKeyOf(a) == streamKeyOf(otherSource)));

    // A NULL label reads as empty; arguments.
    tmdq_stream_key nolabel = ka;
    nolabel.label_utf8 = nullptr;
    tmdq_stream_key emptyLabel = ka;
    emptyLabel.label_utf8 = "";
    QCOMPARE(tmdq_stream_key_equal(&nolabel, &emptyLabel), 1);
    QCOMPARE(tmdq_stream_key_equal(nullptr, &ka), -1);
    QCOMPARE(tmdq_stream_key_equal(&ka, nullptr), -1);
    tmdq_stream_key shortKey = ka;
    shortKey.struct_size = sizeof(shortKey) - 1;
    QCOMPARE(tmdq_stream_key_equal(&shortKey, &ka), -1);
    QCOMPARE(tmdq_stream_key_equal(&ka, &shortKey), -1);
    QCOMPARE(tmdq_stream_result_key(nullptr, &ka), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_stream_result_key(ra, nullptr), int(TMDQ_ERR_BADARG));
    ka.struct_size = sizeof(ka) - 1;
    QCOMPARE(tmdq_stream_result_key(ra, &ka), int(TMDQ_ERR_BADARG));
    for (tmdq_stream_result* r : { ra, r1, r2, r3, r4 })
        tmdq_stream_result_free(r);
}

void TestAbi::importedStreamOrdersAreAssignedLikeTheCore()
{
    std::mt19937 rng(20261024);
    static const char* const labels[] = { "A", "B", "", "R\xC3\xA9", "same" };
    int lockWithSibling = 0, lockWithoutSibling = 0, snr = 0;
    for (int run = 0; run < 1500; ++run)
    {
        const int n = int(rng() % 9);
        std::vector<ImportedSeries> want;
        for (int i = 0; i < n; ++i)
        {
            ImportedSeries s;
            s.metricType = MetricType(rng() % 3);
            s.sourceId = int(rng() % 3);
            s.streamLabel = labels[rng() % 5];
            s.streamOrder = s.metricType == MetricType::SNR ? int(rng() % 30) : int(rng() % 4);
            want.push_back(s);
        }
        std::vector<tmdq_imported_series> wire(want.size());
        std::vector<tmdq_imported_series*> pointers;
        for (std::size_t i = 0; i < want.size(); ++i)
        {
            wire[i].struct_size = sizeof(tmdq_imported_series);
            wire[i].metric_type = int(want[i].metricType);
            wire[i].stream_label_utf8 = want[i].streamLabel.c_str();
            wire[i].source_id = want[i].sourceId;
            wire[i].stream_order = want[i].streamOrder;
            pointers.push_back(&wire[i]);
        }
        const std::vector<ImportedSeries> before = want;
        assignImportedStreamOrders(want);
        QCOMPARE(tmdq_assign_imported_stream_orders(pointers.empty() ? nullptr : pointers.data(), int(pointers.size())), int(TMDQ_OK));
        for (std::size_t i = 0; i < want.size(); ++i)
        {
            QVERIFY2(wire[i].stream_order == want[i].streamOrder, qPrintable(QString("run %1 series %2").arg(run).arg(i)));
            // Only the order is written.
            QCOMPARE(wire[i].metric_type, int(before[i].metricType));
            QCOMPARE(wire[i].source_id, before[i].sourceId);
            QCOMPARE(std::string(wire[i].stream_label_utf8), before[i].streamLabel);
            if (before[i].metricType == MetricType::SNR)
            {
                ++snr;
                continue;
            }
            // Whether a lock or missed-frames series has an SNR series of the same label in the same source.
            bool sibling = false;
            for (const ImportedSeries& other : before)
                sibling = sibling || (other.metricType == MetricType::SNR && other.sourceId == before[i].sourceId &&
                                      other.streamLabel == before[i].streamLabel);
            ++(sibling ? lockWithSibling : lockWithoutSibling);
        }
    }
    QVERIFY2(snr > 1000 && lockWithSibling > 300 && lockWithoutSibling > 500,
             qPrintable(QString("%1 %2 %3").arg(snr).arg(lockWithSibling).arg(lockWithoutSibling)));

    // Arguments.
    tmdq_imported_series one{};
    one.struct_size = sizeof(one);
    one.metric_type = TMDQ_METRIC_FRAME_SYNC_LOCK;
    one.stream_label_utf8 = "x";
    tmdq_imported_series* list[] = { &one };
    QCOMPARE(tmdq_assign_imported_stream_orders(list, 1), int(TMDQ_OK));
    QCOMPARE(tmdq_assign_imported_stream_orders(nullptr, 0), int(TMDQ_OK));   // nothing to order
    QCOMPARE(tmdq_assign_imported_stream_orders(list, -1), int(TMDQ_ERR_BADARG));
    QCOMPARE(tmdq_assign_imported_stream_orders(nullptr, 1), int(TMDQ_ERR_BADARG));
    tmdq_imported_series* withNull[] = { &one, nullptr };
    QCOMPARE(tmdq_assign_imported_stream_orders(withNull, 2), int(TMDQ_ERR_BADARG));
    QVERIFY(std::string(tmdq_last_error()).find("series 1") != std::string::npos);
    tmdq_imported_series shortOne = one;
    shortOne.struct_size = sizeof(shortOne) - 1;
    tmdq_imported_series* withShort[] = { &shortOne };
    QCOMPARE(tmdq_assign_imported_stream_orders(withShort, 1), int(TMDQ_ERR_BADARG));
    tmdq_imported_series badMetric = one;
    badMetric.metric_type = 7;
    tmdq_imported_series* withBad[] = { &one, &badMetric };
    const int before = one.stream_order;
    QCOMPARE(tmdq_assign_imported_stream_orders(withBad, 2), int(TMDQ_ERR_BADARG));
    QCOMPARE(one.stream_order, before);   // a refused list is left alone
    QVERIFY(std::string(tmdq_last_error()).find("metric") != std::string::npos);
}
