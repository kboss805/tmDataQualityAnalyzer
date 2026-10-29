// Series construction: how processed streams become plot series on one shared time axis (the series a stream yields,
// their names and indices, the elapsed axis, re-basing across sources, the order an imported CSV's streams are given).
// No Qt; the core half of the Qt TestSeriesBuild. That suite pinned this logic through PlotViewModel, whose data half is
// orchestration over the core's functions (buildStreamSeries, SeriesTimeBase, assignImportedStreamOrders): drop a
// stream's earlier series, judge overlap, move the base, build, sort, range, remove a source and move the base back.
// The scenarios here run on `Plot`, a small host written over std types that does what the plot does with those
// functions, so every recorded expectation is the core's answer in the plot's sequence. The ViewModel itself (its signals,
// its storage, its colours) stays in TestSeriesBuild, which keeps one scenario for each thing the plot does.
//
// The Qt suite compared the plot with the code the core replaced (the old addStreamData / removeSource / clearData and the
// CSV commit's stream ordering) over generated sequences. That code is restated here over std types, sharing nothing with
// the core's functions but the column schema's naming, and compared with `Plot` the same way.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "constants.h"
#include "csvseriesparser.h"
#include "doctest.h"
#include "processedstreamdata.h"
#include "seriesbuilder.h"
#include "seriescolumnschema.h"
#include "test_support.h"
#include "textparse.h"

using namespace tmdq_test;

namespace {

// ---------------------------------------------------------------------------
// What a series is, and how a run of them reads in a log
// ---------------------------------------------------------------------------

struct Series
{
    std::string name;
    std::string streamLabel;
    MetricType metricType = MetricType::SNR;
    int streamOrder = 0;
    int streamSequence = 0;
    int sourceId = 0;
    int receiverIndex = 0;
    int channelIndex = 0;
    int id = 0;
    std::vector<double> xValues;
    std::vector<double> yValues;
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
};

/// `QString::number(v, 'g', 17)`, and the spellings Qt gives the specials.
std::string num(double v)
{
    if (std::isnan(v))
    {
        return "nan";
    }
    if (std::isinf(v))
    {
        return v > 0 ? "inf" : "-inf";
    }
    return std::format("{:.17g}", v);
}

/// UTF-8 text with its non-ASCII characters spelled out as <U+XXXX>.
std::string asciiOnly(const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size();)
    {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b < 0x80)
        {
            out += static_cast<char>(b);
            ++i;
            continue;
        }
        int extra = 0;
        std::uint32_t cp = 0;
        if ((b & 0xE0) == 0xC0) { extra = 1; cp = b & 0x1F; }
        else if ((b & 0xF0) == 0xE0) { extra = 2; cp = b & 0x0F; }
        else if ((b & 0xF8) == 0xF0) { extra = 3; cp = b & 0x07; }
        bool ok = extra > 0;
        for (int k = 1; ok && k <= extra; ++k)
        {
            ok = i + static_cast<std::size_t>(k) < s.size() && (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0xC0) == 0x80;
            if (ok)
            {
                cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
            }
        }
        if (!ok)
        {
            cp = 0xFFFD;
            extra = 0;
        }
        out += std::format("<U+{:04X}>", cp);
        i += 1 + static_cast<std::size_t>(extra);
    }
    return out;
}

std::string joined(const std::vector<double>& values)
{
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        out += (i ? "," : "") + num(values[i]);
    }
    return out;
}

/// Everything the plot's data layer decides, as one line: the time base, the span, the flags, the warnings raised, and
/// every series' identity, indices, cached range and samples. Presentation (colour, visibility) is deliberately not in it.
std::string format(int day, double offset, double xmin, double xmax, double view_min, double view_max, bool lock,
                   bool missed, const std::vector<int>& warnings, const std::vector<Series>& series)
{
    std::string warn;
    for (std::size_t i = 0; i < warnings.size(); ++i)
    {
        warn += (i ? "," : "") + std::to_string(warnings[i]);
    }
    std::string out = std::format("base={}/{} x={}..{} view={}..{} lock={} missed={} warn=[{}] n={}", day, num(offset), num(xmin),
                                  num(xmax), num(view_min), num(view_max), int(lock), int(missed), warn, series.size());
    for (std::size_t i = 0; i < series.size(); ++i)
    {
        const Series& s = series[i];
        out += std::format(" | [{}] id={} '{}' label='{}' M{} ord={} seq={} src={} rcv={} ch={} y={}..{} x={} y={}", i, s.id,
                           asciiOnly(s.name), asciiOnly(s.streamLabel), int(s.metricType), s.streamOrder, s.streamSequence,
                           s.sourceId, s.receiverIndex, s.channelIndex, num(s.yMin), num(s.yMax), joined(s.xValues),
                           joined(s.yValues));
    }
    return out;
}

// ---------------------------------------------------------------------------
// The host: what the plot does with the core's functions
// ---------------------------------------------------------------------------

class Plot
{
public:
    std::vector<Series> series;
    std::vector<int> warnings;   ///< The sources a non-overlapping arrival was reported for, in order.

    void addStreamData(const ProcessedStreamData& data)
    {
        if (!data.hasSamples())
        {
            return;
        }

        // Reprocessing a stream replaces its earlier series.
        const StreamKey key = streamKeyOf(data);
        series.erase(std::remove_if(series.begin(), series.end(),
                                    [&](const Series& s) { return StreamKey{ s.sourceId, s.streamLabel, s.streamOrder } == key; }),
                     series.end());

        // A source that does not overlap what is loaded is reported once, on its first stream.
        const bool had_series = !series.empty();
        if (had_series)
        {
            const bool source_already_present =
                std::any_of(series.cbegin(), series.cend(), [&](const Series& s) { return s.sourceId == data.sourceId; });
            if (!source_already_present && !m_time_base.overlaps(data.timesSec.front(), data.timesSec.back(), m_x_max))
            {
                warnings.push_back(data.sourceId);
            }
        }

        // The base is the earliest absolute sample; when it moves earlier, everything already there shifts right.
        const double shift = m_time_base.adopt(data.timesSec.front(), had_series);
        if (shift > 0.0)
        {
            for (Series& s : series)
            {
                for (double& x : s.xValues)
                {
                    x += shift;
                }
            }
        }

        const StreamSeries built = buildStreamSeries(data, m_time_base.baseAbsSeconds());
        for (const SeriesSpec& spec : built.series)
        {
            Series s;
            s.name = spec.name;
            s.streamLabel = spec.streamLabel;
            s.metricType = spec.metricType;
            s.receiverIndex = spec.receiverIndex;
            s.channelIndex = spec.channelIndex;
            s.streamOrder = spec.streamOrder;
            s.streamSequence = spec.streamSequence;
            s.sourceId = spec.sourceId;
            s.id = m_next_id++;
            s.xValues = built.elapsed;
            s.yValues.assign(spec.yValues.begin(), spec.yValues.end());
            s.yMin = spec.yMin;
            s.yMax = spec.yMax;
            series.push_back(std::move(s));
        }

        // Legend order is the job order, whichever worker finished first.
        std::stable_sort(series.begin(), series.end(),
                         [](const Series& a, const Series& b) { return a.streamSequence < b.streamSequence; });

        m_x_min = 0.0;
        for (const Series& s : series)
        {
            if (!s.xValues.empty())
            {
                m_x_max = std::max(m_x_max, s.xValues.back());
            }
        }
        refreshFlags();
    }

    void removeSource(int source_id)
    {
        const bool had_match =
            std::any_of(series.cbegin(), series.cend(), [source_id](const Series& s) { return s.sourceId == source_id; });
        if (!had_match)
        {
            return;
        }
        series.erase(std::remove_if(series.begin(), series.end(), [source_id](const Series& s) { return s.sourceId == source_id; }),
                     series.end());
        if (series.empty())
        {
            clearData();
            return;
        }

        // If the removed source held the earliest sample the base moves forward and the rest shift left.
        std::optional<double> earliest;
        for (const Series& s : series)
        {
            if (!s.xValues.empty())
            {
                earliest = earliest ? std::min(*earliest, s.xValues.front()) : s.xValues.front();
            }
        }
        const double delta = m_time_base.advance(earliest);
        if (delta > 0.0)
        {
            for (Series& s : series)
            {
                for (double& x : s.xValues)
                {
                    x -= delta;
                }
            }
        }

        // The span can shrink here, so it is recomputed from scratch.
        m_x_min = 0.0;
        m_x_max = 0.0;
        for (const Series& s : series)
        {
            if (!s.xValues.empty())
            {
                m_x_max = std::max(m_x_max, s.xValues.back());
            }
        }
        refreshFlags();
    }

    void clearData()
    {
        series.clear();
        m_x_min = m_x_max = 0.0;
        m_has_lock = m_has_missed = false;
        m_time_base.reset();
    }

    /// An import replaces everything with the file's series and states its own clock (and leaves the absolute base alone).
    void importCsv(const TempDir& dir, const std::string& csv)
    {
        const fs::path path = dir.file("import.csv");
        writeBytes(path, csv);
        const CsvParseResult parsed = CsvSeriesParser::parse(path);
        if (!parsed.success)
        {
            return;
        }
        series.clear();
        for (const CsvSeries& c : parsed.series)
        {
            Series s;
            s.name = c.name;
            s.streamLabel = c.streamLabel;
            s.metricType = c.metricType;
            s.receiverIndex = c.receiverIndex;
            s.channelIndex = c.channelIndex;
            s.streamOrder = c.streamOrder;
            s.sourceId = c.sourceId;
            s.xValues = c.xValues;
            s.yValues = c.yValues;
            s.yMin = c.yMin;
            s.yMax = c.yMax;
            series.push_back(std::move(s));
        }
        m_time_base.setClock(parsed.baseDay, parsed.baseTimeOffset);
        m_x_min = 0.0;
        m_x_max = parsed.xMax;

        std::vector<ImportedSeries> imported;
        for (const Series& s : series)
        {
            imported.push_back({ s.metricType, s.sourceId, s.streamLabel, s.streamOrder });
        }
        assignImportedStreamOrders(imported);
        m_has_lock = m_has_missed = false;
        for (std::size_t i = 0; i < series.size(); ++i)
        {
            series[i].id = m_next_id++;
            if (series[i].metricType != MetricType::SNR)
            {
                series[i].streamOrder = imported[i].streamOrder;
            }
            m_has_lock |= series[i].metricType == MetricType::FrameSyncLock;
            m_has_missed |= series[i].metricType == MetricType::AccumulatedMissedFrames;
        }
    }

    std::string digest() const
    {
        return format(m_time_base.baseDay(), m_time_base.baseTimeOffset(), m_x_min, m_x_max, m_x_min, m_x_max, m_has_lock,
                      m_has_missed, warnings, series);
    }

private:
    void refreshFlags()
    {
        m_has_lock = m_has_missed = false;
        for (const Series& s : series)
        {
            m_has_lock |= s.metricType == MetricType::FrameSyncLock;
            m_has_missed |= s.metricType == MetricType::AccumulatedMissedFrames;
        }
    }

    SeriesTimeBase m_time_base;
    double m_x_min = 0.0;
    double m_x_max = 0.0;
    bool m_has_lock = false;
    bool m_has_missed = false;
    int m_next_id = 1;
};

// ---------------------------------------------------------------------------
// Building a stream and the scenarios
// ---------------------------------------------------------------------------

struct Ch
{
    const char* name;
    std::vector<double> values;
};

ProcessedStreamData stream(const std::string& label, int channel, int source, int job, std::vector<double> times,
                           std::vector<double> lock, std::vector<double> missed, std::vector<Ch> channels = {})
{
    ProcessedStreamData d;
    d.streamLabel = label;
    d.pcmChannelId = channel;
    d.sourceId = source;
    d.jobIndex = job;
    d.mode = channels.empty() ? StreamMode::FrameSyncLockStats : StreamMode::ReceiverChannelInfo;
    d.timesSec = std::move(times);
    d.lockPercent = std::move(lock);
    d.accumulatedMissedFrames = std::move(missed);
    for (const Ch& c : channels)
    {
        ProcessedChannelSeries s;
        s.name = c.name;
        s.values = c.values;
        d.channels.push_back(std::move(s));
    }
    return d;
}

constexpr double kT0 = 1700000000.0;   // 2023-11-14 22:13:20 UTC, day 318

/// The scenarios are written against this one name for what the plot does, and the importer needs somewhere to put a file.
struct Host
{
    Plot plot;
    TempDir dir;

    void addStreamData(const ProcessedStreamData& d) { plot.addStreamData(d); }
    void removeSource(int id) { plot.removeSource(id); }
    void clearData() { plot.clearData(); }
};

void importCsv(Host& host, const std::string& csv)
{
    host.plot.importCsv(host.dir, csv);
}

using Scenario = std::function<void(Host&)>;

struct Case
{
    const char* name;
    Scenario run;
    const char* expect;
};

const std::string kHeader = "Time (DOY:HH:MM:SS.mmm)";

const std::vector<Case>& cases()
{
    static const std::vector<Case> list = {
        // --- what one stream becomes
        { "a-lock-only-stream", Scenario([](Host& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, { kT0, kT0 + 1, kT0 + 2 }, { 90, 95, 100 }, {})); }), "base=318/80000 x=0..2 view=0..2 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'Ch 5' label='Ch 5' M1 ord=5 seq=0 src=0 rcv=0 ch=0 y=90..100 x=0,1,2 y=90,95,100" },
        { "lock-and-missed-frames", Scenario([](Host& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, { kT0, kT0 + 1 }, { 90, 95 }, { 0, 3 })); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=1 warn=[] n=2"
            " | [0] id=1 'Ch 5' label='Ch 5' M1 ord=5 seq=0 src=0 rcv=0 ch=0 y=90..95 x=0,1 y=90,95"
            " | [1] id=2 'Ch 5' label='Ch 5' M2 ord=5 seq=0 src=0 rcv=0 ch=0 y=0..3 x=0,1 y=0,3" },
        { "missed-frames-only", Scenario([](Host& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, { kT0, kT0 + 1 }, {}, { 0, 3 })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=1 warn=[] n=1"
            " | [0] id=1 'Ch 5' label='Ch 5' M2 ord=5 seq=0 src=0 rcv=0 ch=0 y=0..3 x=0,1 y=0,3" },
        { "a-receiver-stream", Scenario([](Host& vm) {
              vm.addStreamData(stream("Rcvr", 40, 0, 0, { kT0, kT0 + 1 }, { 99, 98 }, { 0, 1 },
                                      { { "L_RCVR1", { -80, -79 } }, { "R_RCVR1", { -81, -78 } }, { "C_RCVR1", { -82, -77 } },
                                        { "L_RCVR2", { -70, -71 } }, { "R_RCVR2", { -72, -73 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=1 warn=[] n=7"
            " | [0] id=1 'Rcvr' label='Rcvr' M1 ord=40 seq=0 src=0 rcv=0 ch=0 y=98..99 x=0,1 y=99,98"
            " | [1] id=2 'Rcvr' label='Rcvr' M2 ord=40 seq=0 src=0 rcv=0 ch=0 y=0..1 x=0,1 y=0,1"
            " | [2] id=3 '40 - Rcvr L_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=-80..-79 x=0,1 y=-80,-79"
            " | [3] id=4 '40 - Rcvr R_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=1 y=-81..-78 x=0,1 y=-81,-78"
            " | [4] id=5 '40 - Rcvr C_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=2 y=-82..-77 x=0,1 y=-82,-77"
            " | [5] id=6 '40 - Rcvr L_RCVR2' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=2 ch=0 y=-71..-70 x=0,1 y=-70,-71"
            " | [6] id=7 '40 - Rcvr R_RCVR2' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=2 ch=1 y=-73..-72 x=0,1 y=-72,-73" },
        { "a-receiver-stream-without-lock-series", Scenario([](Host& vm) {
              vm.addStreamData(stream("Rcvr", 40, 0, 0, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", { -80, -79 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=1"
            " | [0] id=1 '40 - Rcvr L_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=-80..-79 x=0,1 y=-80,-79" },
        { "a-stream-with-no-samples-is-ignored", Scenario([](Host& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, {}, { 1 }, { 2 })); }), "base=0/0 x=0..0 view=0..0 lock=0 missed=0 warn=[] n=0" },
        { "a-stream-with-no-samples-leaves-the-others", Scenario([](Host& vm) {
              vm.addStreamData(stream("Ch 5", 5, 0, 0, { kT0, kT0 + 1 }, { 1, 2 }, {}));
              vm.addStreamData(stream("Ch 6", 6, 0, 1, {}, { 1 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'Ch 5' label='Ch 5' M1 ord=5 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,1 y=1,2" },
        { "the-unlabelled-and-the-non-ascii-label", Scenario([](Host& vm) {
              vm.addStreamData(stream("", 5, 0, 0, { kT0, kT0 + 1 }, { 1, 2 }, {}));
              vm.addStreamData(stream("R\xC3\xA9" "cepteur \xCE\xA9", 6, 0, 1, { kT0, kT0 + 1 }, { 3, 4 }, {},
                                      { { "L_RCVR1", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=3"
            " | [0] id=1 '' label='' M1 ord=5 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 'R<U+00E9>cepteur <U+03A9>' label='R<U+00E9>cepteur <U+03A9>' M1 ord=6 seq=1 src=0 rcv=0 ch=0 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '6 - R<U+00E9>cepteur <U+03A9> L_RCVR1' label='R<U+00E9>cepteur <U+03A9>' M0 ord=6 seq=1 src=0 rcv=1 ch=0 y=5..6 x=0,1 y=5,6" },

        // --- the receiver number and channel index of an SNR series
        { "receiver-number-from-the-last-token", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "X_RCVR2_RCVR3", { 1, 2 } }, { "_RCVR5", { 3, 4 } }, { "L_RCVR12", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=3"
            " | [0] id=1 '1 - S X_RCVR2_RCVR3' label='S' M0 ord=1 seq=0 src=0 rcv=3 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S _RCVR5' label='S' M0 ord=1 seq=0 src=0 rcv=5 ch=0 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S L_RCVR12' label='S' M0 ord=1 seq=0 src=0 rcv=12 ch=0 y=5..6 x=0,1 y=5,6" },
        { "no-receiver-token", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "NAME", { 1, 2 } }, { "rcvr3", { 3, 4 } }, { "", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=3"
            " | [0] id=1 '1 - S NAME' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S rcvr3' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=1 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S ' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=2 y=5..6 x=0,1 y=5,6" },
        { "receiver-token-without-a-number", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "L_RCVR", { 1, 2 } }, { "L_RCVRx", { 3, 4 } }, { "L_RCVR2x", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=3"
            " | [0] id=1 '1 - S L_RCVR' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S L_RCVRx' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=1 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S L_RCVR2x' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=2 y=5..6 x=0,1 y=5,6" },
        { "receiver-number-spelled-oddly", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "L_RCVR 7", { 1, 2 } }, { "L_RCVR+4", { 3, 4 } }, { "L_RCVR-2", { 5, 6 } },
                                        { "L_RCVR0", { 7, 8 } }, { "L_RCVR+-3", { 9, 10 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=5"
            " | [0] id=1 '1 - S L_RCVR 7' label='S' M0 ord=1 seq=0 src=0 rcv=7 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S L_RCVR+4' label='S' M0 ord=1 seq=0 src=0 rcv=4 ch=0 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S L_RCVR-2' label='S' M0 ord=1 seq=0 src=0 rcv=-2 ch=0 y=5..6 x=0,1 y=5,6"
            " | [3] id=4 '1 - S L_RCVR0' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=7..8 x=0,1 y=7,8"
            " | [4] id=5 '1 - S L_RCVR+-3' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=1 y=9..10 x=0,1 y=9,10" },
        { "receiver-number-out-of-range", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "L_RCVR2147483647", { 1, 2 } }, { "L_RCVR2147483648", { 3, 4 } }, { "L_RCVR99999999999", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=3"
            " | [0] id=1 '1 - S L_RCVR2147483647' label='S' M0 ord=1 seq=0 src=0 rcv=2147483647 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S L_RCVR2147483648' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S L_RCVR99999999999' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=1 y=5..6 x=0,1 y=5,6" },
        { "receiver-token-with-non-ascii-text", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "\xCE\xA9_RCVR2", { 1, 2 } }, { "\xCE\xA9\xC3\xA9", { 3, 4 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=2"
            " | [0] id=1 '1 - S <U+03A9>_RCVR2' label='S' M0 ord=1 seq=0 src=0 rcv=2 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S <U+03A9><U+00E9>' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=3..4 x=0,1 y=3,4" },
        { "channel-index-counts-within-a-receiver", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {},
                                      { { "L_RCVR1", { 1, 2 } }, { "R_RCVR1", { 3, 4 } }, { "L_RCVR2", { 5, 6 } },
                                        { "C_RCVR1", { 7, 8 } }, { "R_RCVR2", { 9, 10 } }, { "NAME", { 11, 12 } }, { "OTHER", { 13, 14 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=7"
            " | [0] id=1 '1 - S L_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - S R_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=1 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '1 - S L_RCVR2' label='S' M0 ord=1 seq=0 src=0 rcv=2 ch=0 y=5..6 x=0,1 y=5,6"
            " | [3] id=4 '1 - S C_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=2 y=7..8 x=0,1 y=7,8"
            " | [4] id=5 '1 - S R_RCVR2' label='S' M0 ord=1 seq=0 src=0 rcv=2 ch=1 y=9..10 x=0,1 y=9,10"
            " | [5] id=6 '1 - S NAME' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=0 y=11..12 x=0,1 y=11,12"
            " | [6] id=7 '1 - S OTHER' label='S' M0 ord=1 seq=0 src=0 rcv=0 ch=1 y=13..14 x=0,1 y=13,14" },
        { "channel-index-restarts-for-each-stream", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", { 1, 2 } }, { "R_RCVR1", { 3, 4 } } }));
              vm.addStreamData(stream("B", 2, 0, 1, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=3"
            " | [0] id=1 '1 - A L_RCVR1' label='A' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '1 - A R_RCVR1' label='A' M0 ord=1 seq=0 src=0 rcv=1 ch=1 y=3..4 x=0,1 y=3,4"
            " | [2] id=3 '2 - B L_RCVR1' label='B' M0 ord=2 seq=1 src=0 rcv=1 ch=0 y=5..6 x=0,1 y=5,6" },
        { "the-snr-name-carries-the-channel-and-label", Scenario([](Host& vm) {
              vm.addStreamData(stream("Safran 2", 40, 0, 0, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", { 1, 2 } } }));
              vm.addStreamData(stream("Safran 2", 41, 1, 1, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", { 3, 4 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=2"
            " | [0] id=1 '40 - Safran 2 L_RCVR1' label='Safran 2' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=1..2 x=0,1 y=1,2"
            " | [1] id=2 '41 - Safran 2 L_RCVR1' label='Safran 2' M0 ord=41 seq=1 src=1 rcv=1 ch=0 y=3..4 x=0,1 y=3,4" },

        // --- sample arrays that do not line up
        { "lock-shorter-than-the-times", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1, kT0 + 2 }, { 1, 2 }, { 5, 6, 7 })); }), "base=318/80000 x=0..2 view=0..2 lock=1 missed=1 warn=[] n=2"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,1,2 y=1,2"
            " | [1] id=2 'S' label='S' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=5..7 x=0,1,2 y=5,6,7" },
        { "lock-longer-than-the-times", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 2, 3 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..3 x=0,1 y=1,2,3" },
        { "a-channel-with-no-values", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1 }, {}, {}, { { "L_RCVR1", {} }, { "R_RCVR1", { 1, 2 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=0 missed=0 warn=[] n=2"
            " | [0] id=1 '1 - S L_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=1.7976931348623157e+308..-1.7976931348623157e+308 x=0,1 y="
            " | [1] id=2 '1 - S R_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=1 y=1..2 x=0,1 y=1,2" },
        { "values-with-not-a-number-and-infinity", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0 + 1, kT0 + 2, kT0 + 3 },
                                      { std::numeric_limits<double>::quiet_NaN(), 5, std::numeric_limits<double>::infinity(), -1 }, {},
                                      { { "L_RCVR1", { -std::numeric_limits<double>::infinity(), 2, std::numeric_limits<double>::quiet_NaN(), 1 } } })); }), "base=318/80000 x=0..3 view=0..3 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=-1..inf x=0,1,2,3 y=nan,5,inf,-1"
            " | [1] id=2 '1 - S L_RCVR1' label='S' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=1..2 x=0,1,2,3 y=-inf,2,nan,1" },
        { "times-out-of-order", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0 + 5, kT0, kT0 + 2 }, { 1, 2, 3 }, {})); }), "base=318/80005 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..3 x=0,-5,-3 y=1,2,3" },
        { "repeated-times", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0, kT0, kT0 }, { 1, 2, 3 }, {})); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..3 x=0,0,0 y=1,2,3" },
        { "a-single-sample", Scenario([](Host& vm) {
              vm.addStreamData(stream("S", 1, 0, 0, { kT0 }, { 50 }, { 0 })); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=1 warn=[] n=2"
            " | [0] id=1 'S' label='S' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=50..50 x=0 y=50"
            " | [1] id=2 'S' label='S' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=0..0 x=0 y=0" },

        // --- the order series are kept in
        { "streams-are-kept-in-job-order", Scenario([](Host& vm) {
              vm.addStreamData(stream("C", 3, 0, 2, { kT0, kT0 + 1 }, { 3, 3 }, {}));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 0, 1, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=3"
            " | [0] id=2 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=3 'B' label='B' M1 ord=2 seq=1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2"
            " | [2] id=1 'C' label='C' M1 ord=3 seq=2 src=0 rcv=0 ch=0 y=3..3 x=0,1 y=3,3" },
        { "equal-job-indices-keep-their-arrival-order", Scenario([](Host& vm) {
              vm.addStreamData(stream("B", 2, 0, 0, { kT0, kT0 + 1 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("C", 3, 1, 0, { kT0, kT0 + 1 }, { 3, 3 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=3"
            " | [0] id=1 'B' label='B' M1 ord=2 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [2] id=3 'C' label='C' M1 ord=3 seq=0 src=1 rcv=0 ch=0 y=3..3 x=0,1 y=3,3" },
        { "negative-job-index", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 1, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 0, -1, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=2"
            " | [0] id=2 'B' label='B' M1 ord=2 seq=-1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2"
            " | [1] id=1 'A' label='A' M1 ord=1 seq=1 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1" },

        // --- running a stream again
        { "reprocessing-replaces-the-same-stream", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, { 0, 0 }));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1, kT0 + 2 }, { 9, 9, 9 }, {})); }), "base=318/80000 x=0..2 view=0..2 lock=1 missed=0 warn=[] n=1"
            " | [0] id=3 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=9..9 x=0,1,2 y=9,9,9" },
        { "a-different-label-is-another-stream", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 1, 0, 1, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=2 'B' label='B' M1 ord=1 seq=1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2" },
        { "a-different-channel-is-another-stream", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 2, 0, 1, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=2 seq=1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2" },
        { "a-different-source-is-another-stream", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 1 }, { 2, 2 }, {})); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,1 y=2,2" },
        { "a-stream-changes-from-lock-only-to-receiver", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, { 0, 0 }));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1 }, { 1, 1 }, {}, { { "L_RCVR1", { 5, 6 } } })); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=2"
            " | [0] id=3 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1"
            " | [1] id=4 '1 - A L_RCVR1' label='A' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=5..6 x=0,1 y=5,6" },
        { "reprocessing-the-first-stream-can-move-the-base-later", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 0, 1, { kT0 + 5, kT0 + 6 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 20, kT0 + 21 }, { 3, 3 }, {})); }), "base=318/80000 x=0..21 view=0..21 lock=1 missed=0 warn=[] n=2"
            " | [0] id=3 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=3..3 x=20,21 y=3,3"
            " | [1] id=2 'B' label='B' M1 ord=2 seq=1 src=0 rcv=0 ch=0 y=2..2 x=5,6 y=2,2" },

        // --- the shared time axis across sources
        { "a-later-source-starts-after", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 6 }, { 2, 2 }, {})); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=4,6 y=2,2" },
        { "a-later-source-starts-before-so-the-others-shift", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 10, kT0 + 20 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 6 }, { 2, 2 }, {})); }), "base=318/80004 x=0..16 view=0..16 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=6,16 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,2 y=2,2" },
        { "a-source-starting-at-the-same-instant", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 3 }, { 2, 2 }, {})); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,3 y=2,2" },
        { "three-sources-each-earlier-than-the-last", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 30, kT0 + 40 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 20, kT0 + 25 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 2, 0, { kT0 + 5, kT0 + 6 }, { 3, 3 }, {})); }), "base=318/80005 x=0..35 view=0..35 lock=1 missed=0 warn=[1,2] n=3"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=25,35 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=15,20 y=2,2"
            " | [2] id=3 'A' label='A' M1 ord=1 seq=0 src=2 rcv=0 ch=0 y=3..3 x=0,1 y=3,3" },
        { "a-second-stream-of-a-source-that-starts-earlier", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 10, kT0 + 20 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 0, 1, { kT0 + 2, kT0 + 3 }, { 2, 2 }, {})); }), "base=318/80002 x=0..18 view=0..18 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=8,18 y=1,1"
            " | [1] id=2 'B' label='B' M1 ord=2 seq=1 src=0 rcv=0 ch=0 y=2..2 x=0,1 y=2,2" },
        { "shifting-by-fractions-of-a-second", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 0.3, kT0 + 0.7 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 0.1, kT0 + 0.2 }, { 2, 2 }, {})); }), "base=318/80000.099999904633 x=0..0.60000014305114746 view=0..0.60000014305114746 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0.20000004768371582,0.60000014305114746 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,0.10000014305114746 y=2,2" },
        { "one-source-with-several-channels-shared-axis", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 1, kT0 + 2 }, { 1, 2, 3 }, { 0, 0, 1 },
                                      { { "L_RCVR1", { 4, 5, 6 } } })); }), "base=318/80000 x=0..2 view=0..2 lock=1 missed=1 warn=[] n=3"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..3 x=0,1,2 y=1,2,3"
            " | [1] id=2 'A' label='A' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=0..1 x=0,1,2 y=0,0,1"
            " | [2] id=3 '1 - A L_RCVR1' label='A' M0 ord=1 seq=0 src=0 rcv=1 ch=0 y=4..6 x=0,1,2 y=4,5,6" },

        // --- sources that do not overlap
        { "overlapping-sources-raise-no-warning", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 5, kT0 + 15 }, { 2, 2 }, {})); }), "base=318/80000 x=0..15 view=0..15 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=5,15 y=2,2" },
        { "a-source-that-starts-where-the-last-ends", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 10, kT0 + 12 }, { 2, 2 }, {})); }), "base=318/80000 x=0..12 view=0..12 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=10,12 y=2,2" },
        { "a-source-that-starts-just-after", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 10.5, kT0 + 12 }, { 2, 2 }, {})); }), "base=318/80000 x=0..12 view=0..12 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=10.5,12 y=2,2" },
        { "a-source-that-ends-just-before", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 20, kT0 + 30 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 19.5 }, { 2, 2 }, {})); }), "base=318/80000 x=0..30 view=0..30 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=20,30 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,19.5 y=2,2" },
        { "a-source-that-ends-exactly-where-the-first-starts", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 20, kT0 + 30 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 20 }, { 2, 2 }, {})); }), "base=318/80000 x=0..30 view=0..30 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=20,30 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,20 y=2,2" },
        { "a-source-entirely-inside-another", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 100 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 40, kT0 + 50 }, { 2, 2 }, {})); }), "base=318/80000 x=0..100 view=0..100 lock=1 missed=0 warn=[] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,100 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=40,50 y=2,2" },
        { "the-warning-is-raised-once-per-source", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 1000, kT0 + 1010 }, { 2, 2 }, {}));
              vm.addStreamData(stream("B", 2, 1, 1, { kT0 + 2000, kT0 + 2010 }, { 3, 3 }, {})); }), "base=318/80000 x=0..2010 view=0..2010 lock=1 missed=0 warn=[1] n=3"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=1000,1010 y=2,2"
            " | [2] id=3 'B' label='B' M1 ord=2 seq=1 src=1 rcv=0 ch=0 y=3..3 x=2000,2010 y=3,3" },
        { "a-third-source-is-judged-against-everything-so-far", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 1000, kT0 + 1010 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 2, 0, { kT0 + 500, kT0 + 505 }, { 3, 3 }, {})); }), "base=318/80000 x=0..1010 view=0..1010 lock=1 missed=0 warn=[1] n=3"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=1000,1010 y=2,2"
            " | [2] id=3 'A' label='A' M1 ord=1 seq=0 src=2 rcv=0 ch=0 y=3..3 x=500,505 y=3,3" },
        { "the-first-stream-of-a-reprocessed-source-is-not-new", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 5000, kT0 + 5010 }, { 2, 2 }, {})); }), "base=318/85000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0,10 y=2,2" },

        // --- the base day and time of day
        { "base-at-the-epoch", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { 0.0, 1.0 }, { 1, 1 }, {})); }), "base=1/0 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1" },
        { "base-with-a-fraction", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { 86399.999, 86401.0 }, { 1, 1 }, {})); }), "base=1/86399.998999999996 x=0..1.0010000000038417 view=0..1.0010000000038417 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1.0010000000038417 y=1,1" },
        { "base-on-the-last-day-of-a-year", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { 1704067199.5, 1704067201.0 }, { 1, 1 }, {})); }), "base=365/86399.5 x=0..1.5 view=0..1.5 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1.5 y=1,1" },
        { "base-in-a-leap-year", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { 1709164800.0, 1709164801.0 }, { 1, 1 }, {})); }), "base=60/0 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1" },
        { "base-far-in-the-future", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { 4102444800.0, 4102444801.0 }, { 1, 1 }, {})); }), "base=1/0 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,1 y=1,1" },
        { "base-before-the-epoch", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { -100.0, 50.0 }, { 1, 1 }, {})); }), "base=365/86300 x=0..150 view=0..150 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,150 y=1,1" },
        { "base-moves-to-an-earlier-day", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 86400 * 3, kT0 + 86400 * 3 + 5 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 5 }, { 2, 2 }, {})); }), "base=318/80000 x=0..259205 view=0..259205 lock=1 missed=0 warn=[1] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=259200,259205 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,5 y=2,2" },
        { "base-seconds-and-fraction", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 0.25, kT0 + 1.0 }, { 1, 1 }, {})); }), "base=318/80000.25 x=0..0.75 view=0..0.75 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,0.75 y=1,1" },

        // --- taking a source away
        { "remove-the-earliest-source", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 6 }, { 2, 2 }, {}));
              vm.removeSource(0); }), "base=318/80004 x=0..2 view=0..2 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,2 y=2,2" },
        { "remove-a-later-source", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 6 }, { 2, 2 }, {}));
              vm.removeSource(1); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1" },
        { "remove-the-source-with-the-latest-end", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 4, kT0 + 60 }, { 2, 2 }, {}));
              vm.removeSource(1); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1" },
        { "remove-the-only-source", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.removeSource(0); }), "base=0/0 x=0..0 view=0..0 lock=0 missed=0 warn=[] n=0" },
        { "remove-a-source-that-is-not-there", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.removeSource(7); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1" },
        { "remove-one-of-three-sources", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 100, kT0 + 110 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 50, kT0 + 60 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 2, 0, { kT0 + 10, kT0 + 20 }, { 3, 3 }, {}));
              vm.removeSource(2); }), "base=318/80050 x=0..60 view=0..60 lock=1 missed=0 warn=[1,2] n=2"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=50,60 y=1,1"
            " | [1] id=2 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,10 y=2,2" },
        { "remove-two-sources-in-turn", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 100, kT0 + 110 }, { 1, 1 }, {}));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0 + 50, kT0 + 60 }, { 2, 2 }, {}));
              vm.addStreamData(stream("A", 1, 2, 0, { kT0 + 10, kT0 + 20 }, { 3, 3 }, {}));
              vm.removeSource(2); vm.removeSource(1); }), "base=318/80100 x=0..10 view=0..10 lock=1 missed=0 warn=[1,2] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1" },
        { "remove-a-source-with-several-streams", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.addStreamData(stream("B", 2, 1, 1, { kT0 + 2, kT0 + 5 }, { 2, 2 }, {}, { { "L_RCVR1", { 3, 4 } } }));
              vm.addStreamData(stream("C", 3, 1, 2, { kT0 + 3, kT0 + 9 }, { 5, 5 }, {}));
              vm.removeSource(1); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0,10 y=1,1" },
        { "remove-then-add-again", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.removeSource(0);
              vm.addStreamData(stream("A", 1, 0, 0, { kT0 + 500, kT0 + 510 }, { 2, 2 }, {})); }), "base=318/80500 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0,10 y=2,2" },
        { "clear-then-add-again", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              vm.clearData();
              vm.addStreamData(stream("B", 2, 0, 0, { kT0 + 500, kT0 + 510 }, { 2, 2 }, {})); }), "base=318/80500 x=0..10 view=0..10 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'B' label='B' M1 ord=2 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0,10 y=2,2" },
        { "ids-keep-counting-after-a-removal", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, { 0, 0 }));
              vm.addStreamData(stream("A", 1, 1, 0, { kT0, kT0 + 10 }, { 2, 2 }, { 0, 0 }));
              vm.removeSource(0);
              vm.addStreamData(stream("B", 2, 2, 1, { kT0, kT0 + 10 }, { 3, 3 }, {})); }), "base=318/80000 x=0..10 view=0..10 lock=1 missed=1 warn=[] n=3"
            " | [0] id=3 'A' label='A' M1 ord=1 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0,10 y=2,2"
            " | [1] id=4 'A' label='A' M2 ord=1 seq=0 src=1 rcv=0 ch=0 y=0..0 x=0,10 y=0,0"
            " | [2] id=5 'B' label='B' M1 ord=2 seq=1 src=2 rcv=0 ch=0 y=3..3 x=0,10 y=3,3" },

        // --- an imported CSV: the order given to its streams
        { "import-lock-only-streams", Scenario([](Host& vm) {
              importCsv(vm, kHeader + ",Zed Lock (%),A Lock (%),A Accumulated Missed Frames,Zed Accumulated Missed Frames\n"
                                      "318:22:13:20.000,1,2,3,4\n318:22:13:21.000,5,6,7,8\n"); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=1 warn=[] n=4"
            " | [0] id=1 'Zed' label='Zed' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..5 x=0,1 y=1,5"
            " | [1] id=2 'A' label='A' M1 ord=2 seq=0 src=0 rcv=0 ch=0 y=2..6 x=0,1 y=2,6"
            " | [2] id=3 'A' label='A' M2 ord=2 seq=0 src=0 rcv=0 ch=0 y=3..7 x=0,1 y=3,7"
            " | [3] id=4 'Zed' label='Zed' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=4..8 x=0,1 y=4,8" },
        { "import-lock-streams-after-an-snr-one", Scenario([](Host& vm) {
              importCsv(vm, kHeader + ",Zed Lock (%),40 - Rcvr L_RCVR1,A Lock (%),40 - Rcvr R_RCVR1\n"
                                      "318:22:13:20.000,1,2,3,4\n318:22:13:21.000,5,6,7,8\n"); }), "base=318/80000 x=0..1 view=0..1 lock=1 missed=0 warn=[] n=4"
            " | [0] id=1 'Zed' label='Zed' M1 ord=41 seq=0 src=0 rcv=0 ch=0 y=1..5 x=0,1 y=1,5"
            " | [1] id=2 '40 - Rcvr L_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=2..6 x=0,1 y=2,6"
            " | [2] id=3 'A' label='A' M1 ord=42 seq=0 src=0 rcv=0 ch=0 y=3..7 x=0,1 y=3,7"
            " | [3] id=4 '40 - Rcvr R_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=1 y=4..8 x=0,1 y=4,8" },
        { "import-a-lock-stream-named-like-an-snr-stream", Scenario([](Host& vm) {
              importCsv(vm, kHeader + ",Rcvr Lock (%),40 - Rcvr L_RCVR1,Other Lock (%)\n"
                                      "318:22:13:20.000,1,2,3\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=3"
            " | [0] id=1 'Rcvr' label='Rcvr' M1 ord=40 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 '40 - Rcvr L_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=2..2 x=0 y=2"
            " | [2] id=3 'Other' label='Other' M1 ord=41 seq=0 src=0 rcv=0 ch=0 y=3..3 x=0 y=3" },
        { "import-two-sources-with-one-label", Scenario([](Host& vm) {
              importCsv(vm, kHeader + ",A Lock (%),S1| A Lock (%),S1| A Accumulated Missed Frames,A Accumulated Missed Frames\n"
                                      "318:22:13:20.000,1,2,3,4\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=1 warn=[] n=4"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 'A' label='A' M1 ord=2 seq=0 src=1 rcv=0 ch=0 y=2..2 x=0 y=2"
            " | [2] id=3 'A' label='A' M2 ord=2 seq=0 src=1 rcv=0 ch=0 y=3..3 x=0 y=3"
            " | [3] id=4 'A' label='A' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=4..4 x=0 y=4" },
        { "import-snr-orders-are-kept-and-skipped", Scenario([](Host& vm) {
              importCsv(vm, kHeader + ",40 - A L_RCVR1,45 - B L_RCVR1,X Lock (%),Y Lock (%)\n"
                                      "318:22:13:20.000,1,2,3,4\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=4"
            " | [0] id=1 '40 - A L_RCVR1' label='A' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 '45 - B L_RCVR1' label='B' M0 ord=45 seq=0 src=0 rcv=1 ch=0 y=2..2 x=0 y=2"
            " | [2] id=3 'X' label='X' M1 ord=46 seq=0 src=0 rcv=0 ch=0 y=3..3 x=0 y=3"
            " | [3] id=4 'Y' label='Y' M1 ord=47 seq=0 src=0 rcv=0 ch=0 y=4..4 x=0 y=4" },
        { "import-lock-and-missed-of-one-stream-share-an-order", Scenario([](Host& vm) {
              importCsv(vm, kHeader + ",A Lock (%),B Lock (%),B Accumulated Missed Frames,A Accumulated Missed Frames\n"
                                      "318:22:13:20.000,1,2,3,4\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=1 warn=[] n=4"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 'B' label='B' M1 ord=2 seq=0 src=0 rcv=0 ch=0 y=2..2 x=0 y=2"
            " | [2] id=3 'B' label='B' M2 ord=2 seq=0 src=0 rcv=0 ch=0 y=3..3 x=0 y=3"
            " | [3] id=4 'A' label='A' M2 ord=1 seq=0 src=0 rcv=0 ch=0 y=4..4 x=0 y=4" },
        { "import-an-empty-label-lock-stream", Scenario([](Host& vm) {
              importCsv(vm, kHeader + ", Lock (%),40 - Rcvr L_RCVR1\n"
                                      "318:22:13:20.000,1,2\n"); }), "base=318/80000 x=0..0 view=0..0 lock=0 missed=0 warn=[] n=2"
            " | [0] id=1 'Lock (%)' label='' M0 ord=0 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1"
            " | [1] id=2 '40 - Rcvr L_RCVR1' label='Rcvr' M0 ord=40 seq=0 src=0 rcv=1 ch=0 y=2..2 x=0 y=2" },
        { "import-keeps-the-files-own-clock", Scenario([](Host& vm) {
              importCsv(vm, kHeader + ",A Lock (%)\n"
                                      "003:07:08:09.250,1\n003:07:08:11.500,2\n"); }), "base=3/25689.25 x=0..2.25 view=0..2.25 lock=1 missed=0 warn=[] n=1"
            " | [0] id=1 'A' label='A' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..2 x=0,2.25 y=1,2" },
        { "import-after-streams-replaces-them", Scenario([](Host& vm) {
              vm.addStreamData(stream("A", 1, 0, 0, { kT0, kT0 + 10 }, { 1, 1 }, {}));
              importCsv(vm, kHeader + ",B Lock (%)\n318:22:13:20.000,1\n"); }), "base=318/80000 x=0..0 view=0..0 lock=1 missed=0 warn=[] n=1"
            " | [0] id=2 'B' label='B' M1 ord=1 seq=0 src=0 rcv=0 ch=0 y=1..1 x=0 y=1" },
    };
    return list;
}

}  // namespace

TEST_CASE("seriesbuild: every scenario reads as recorded")
{
    for (const Case& c : cases())
    {
        INFO("scenario " << c.name);
        Host host;
        c.run(host);
        CHECK(host.plot.digest() == std::string(c.expect));
    }
}

// ---------------------------------------------------------------------------
// The code the core replaced, restated over std types, beside the core over generated sequences
// ---------------------------------------------------------------------------

namespace oracle {

/// The data half of PlotViewModel's old addStreamData / removeSource / clearData, as it was apart from the types: it
/// builds each series itself (the lock, the missed frames, the receiver channels, their indices), moves the base with
/// its own arithmetic and reads the clock with `gmtime_s`, and shares with the core only the column schema's naming.
class OldPlot
{
public:
    std::vector<Series> m_series;
    std::vector<int> warnings;
    double m_x_min = 0.0, m_x_max = 0.0, m_x_view_min = 0.0, m_x_view_max = 0.0;
    bool m_has_lock_series = false, m_has_missed_frames_series = false;
    int m_base_day = 0;
    double m_base_time_offset = 0.0;
    double m_base_abs_seconds = 0.0;
    int m_next_series_id = 1;

    void recomputeBaseDayAndOffset()
    {
        std::time_t epoch = static_cast<std::time_t>(m_base_abs_seconds);
        std::tm tm_buf{};
        const bool ok = (gmtime_s(&tm_buf, &epoch) == 0);
        if (ok)
        {
            m_base_day = tm_buf.tm_yday + 1;
            m_base_time_offset = (tm_buf.tm_hour * UIConstants::kSecondsPerHour) + (tm_buf.tm_min * UIConstants::kSecondsPerMinute) +
                                 tm_buf.tm_sec + (m_base_abs_seconds - static_cast<double>(epoch));
        }
    }

    void clearData()
    {
        m_series.clear();
        m_x_min = m_x_max = 0.0;
        m_x_view_min = m_x_view_max = 0.0;
        m_has_lock_series = false;
        m_has_missed_frames_series = false;
        m_base_day = 0;
        m_base_time_offset = 0.0;
        m_base_abs_seconds = 0.0;
    }

    void addStreamData(const ProcessedStreamData& data)
    {
        if (!data.hasSamples())
        {
            return;
        }

        const std::string stream_label = data.streamLabel;

        m_series.erase(std::remove_if(m_series.begin(), m_series.end(),
                                      [&](const Series& s) {
                                          return s.sourceId == data.sourceId && s.streamLabel == stream_label &&
                                                 s.streamOrder == data.pcmChannelId;
                                      }),
                       m_series.end());

        if (!m_series.empty())
        {
            const bool source_already_present =
                std::any_of(m_series.cbegin(), m_series.cend(), [&](const Series& s) { return s.sourceId == data.sourceId; });
            if (!source_already_present)
            {
                const double existing_min_abs = m_base_abs_seconds;
                const double existing_max_abs = m_base_abs_seconds + m_x_max;
                const double new_min_abs = data.timesSec.front();
                const double new_max_abs = data.timesSec.back();
                const bool overlaps = new_min_abs <= existing_max_abs && existing_min_abs <= new_max_abs;
                if (!overlaps)
                {
                    warnings.push_back(data.sourceId);
                }
            }
        }

        if (m_series.empty())
        {
            m_base_abs_seconds = data.timesSec.front();
            recomputeBaseDayAndOffset();
        }
        else if (data.timesSec.front() < m_base_abs_seconds)
        {
            const double delta = m_base_abs_seconds - data.timesSec.front();
            for (Series& s : m_series)
            {
                for (double& x : s.xValues)
                {
                    x += delta;
                }
            }
            m_base_abs_seconds = data.timesSec.front();
            recomputeBaseDayAndOffset();
        }

        std::vector<double> elapsed(data.timesSec.size());
        for (std::size_t i = 0; i < elapsed.size(); i++)
        {
            elapsed[i] = data.timesSec[i] - m_base_abs_seconds;
        }

        auto fillCaches = [](Series& s) {
            for (double v : s.yValues)
            {
                s.yMin = (s.yMin < v) ? s.yMin : v;
                s.yMax = (s.yMax < v) ? v : s.yMax;
            }
        };

        if (!data.lockPercent.empty())
        {
            Series lock;
            lock.name = stream_label;
            lock.streamLabel = stream_label;
            lock.metricType = MetricType::FrameSyncLock;
            lock.streamOrder = data.pcmChannelId;
            lock.streamSequence = data.jobIndex;
            lock.sourceId = data.sourceId;
            lock.id = m_next_series_id++;
            lock.xValues = elapsed;
            lock.yValues = data.lockPercent;
            fillCaches(lock);
            m_series.push_back(lock);
        }

        if (!data.accumulatedMissedFrames.empty())
        {
            Series errors;
            errors.name = stream_label;
            errors.streamLabel = stream_label;
            errors.metricType = MetricType::AccumulatedMissedFrames;
            errors.streamOrder = data.pcmChannelId;
            errors.streamSequence = data.jobIndex;
            errors.sourceId = data.sourceId;
            errors.id = m_next_series_id++;
            errors.xValues = elapsed;
            errors.yValues = data.accumulatedMissedFrames;
            fillCaches(errors);
            m_series.push_back(errors);
        }

        std::map<int, int> receiver_channel_count;
        for (const auto& ch : data.channels)
        {
            Series s;
            s.name = SeriesColumnSchema::snrSeriesName(data.pcmChannelId, stream_label, ch.name);
            s.streamLabel = stream_label;
            s.metricType = MetricType::SNR;
            const std::size_t rcvr_pos = ch.name.rfind("_RCVR");
            if (rcvr_pos != std::string::npos)
            {
                int rcvr_num = 0;
                const bool ok = textparse::parseInteger(ch.name.substr(rcvr_pos + 5), rcvr_num);
                s.receiverIndex = ok ? rcvr_num : 0;
            }
            s.channelIndex = receiver_channel_count.count(s.receiverIndex) ? receiver_channel_count[s.receiverIndex] : 0;
            receiver_channel_count[s.receiverIndex]++;
            s.streamOrder = data.pcmChannelId;
            s.streamSequence = data.jobIndex;
            s.sourceId = data.sourceId;
            s.id = m_next_series_id++;
            s.xValues = elapsed;
            s.yValues = ch.values;
            fillCaches(s);
            m_series.push_back(s);
        }

        std::stable_sort(m_series.begin(), m_series.end(),
                         [](const Series& a, const Series& b) { return a.streamSequence < b.streamSequence; });

        refreshRange();
    }

    void refreshRange()
    {
        m_x_min = 0.0;
        for (const auto& s : m_series)
        {
            if (!s.xValues.empty())
            {
                m_x_max = (m_x_max < s.xValues.back()) ? s.xValues.back() : m_x_max;
            }
        }
        m_x_view_min = m_x_min;
        m_x_view_max = m_x_max;

        m_has_lock_series = false;
        m_has_missed_frames_series = false;
        for (const auto& s : m_series)
        {
            if (s.metricType == MetricType::FrameSyncLock)
            {
                m_has_lock_series = true;
            }
            else if (s.metricType == MetricType::AccumulatedMissedFrames)
            {
                m_has_missed_frames_series = true;
            }
        }
    }

    void removeSource(int sourceId)
    {
        const bool had_match =
            std::any_of(m_series.cbegin(), m_series.cend(), [sourceId](const Series& s) { return s.sourceId == sourceId; });
        if (!had_match)
        {
            return;
        }

        m_series.erase(std::remove_if(m_series.begin(), m_series.end(), [sourceId](const Series& s) { return s.sourceId == sourceId; }),
                       m_series.end());

        if (m_series.empty())
        {
            clearData();
            return;
        }

        double new_base_abs = m_base_abs_seconds + m_series.front().xValues.front();
        for (const Series& s : m_series)
        {
            if (!s.xValues.empty())
            {
                new_base_abs = std::min(new_base_abs, m_base_abs_seconds + s.xValues.front());
            }
        }
        const double delta = new_base_abs - m_base_abs_seconds;   // >= 0
        if (delta > 0.0)
        {
            for (Series& s : m_series)
            {
                for (double& x : s.xValues)
                {
                    x -= delta;
                }
            }
            m_base_abs_seconds = new_base_abs;
            recomputeBaseDayAndOffset();
        }

        m_x_min = 0.0;
        m_x_max = 0.0;
        refreshRange();
    }

    std::string snapshot() const
    {
        return format(m_base_day, m_base_time_offset, m_x_min, m_x_max, m_x_view_min, m_x_view_max, m_has_lock_series,
                      m_has_missed_frames_series, warnings, m_series);
    }
};

/// The CSV commit's stream ordering, as it was.
void assignImportedOrders(std::vector<Series>& series)
{
    std::map<int, std::map<std::string, int>> label_to_stream_order;
    int next_stream_order = 0;
    for (const auto& s : series)
    {
        if (s.metricType == MetricType::SNR && !s.streamLabel.empty())
        {
            label_to_stream_order[s.sourceId][s.streamLabel] = s.streamOrder;   // QMap::insert replaces, std::map::insert does not
        }
        next_stream_order = std::max(next_stream_order, s.streamOrder + 1);
    }

    for (auto& s : series)
    {
        switch (s.metricType)
        {
        case MetricType::FrameSyncLock:
        case MetricType::AccumulatedMissedFrames:
        {
            std::map<std::string, int>& source_stream_order = label_to_stream_order[s.sourceId];
            auto it = source_stream_order.find(s.streamLabel);
            if (it == source_stream_order.end())
            {
                it = source_stream_order.insert({ s.streamLabel, next_stream_order++ }).first;
            }
            s.streamOrder = it->second;
            break;
        }
        default:
            break;
        }
    }
}

}  // namespace oracle

namespace {

/// A deterministic generator (the Qt suite's, state for state), so a failure names the same case every run.
struct Rng
{
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed) {}
    std::uint32_t next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    int below(int n) { return int(next() % std::uint32_t(n)); }
    bool chance(int percent) { return below(100) < percent; }
};

std::vector<double> generateValues(Rng& rng, int count, bool finite_only = false)
{
    static const std::vector<double> specials = { 0.0, -0.0, 1e300, -1e300, 4.9e-324, 99.5, -80.25,
                                                  std::numeric_limits<double>::infinity(),
                                                  -std::numeric_limits<double>::infinity(),
                                                  std::numeric_limits<double>::quiet_NaN() };
    std::vector<double> v;
    for (int i = 0; i < count; ++i)
    {
        v.push_back(!finite_only && rng.chance(15) ? specials[static_cast<std::size_t>(rng.below(int(specials.size())))]
                                                   : rng.below(20000) / 100.0 - 50.0);
    }
    return v;
}

ProcessedStreamData generateStream(Rng& rng)
{
    static const char* labels[] = { "A", "B", "Ch 5", "", "R\xC3\xA9" "cepteur \xCE\xA9", "Safran 2", "x y" };
    static const char* channel_names[] = { "L_RCVR1", "R_RCVR1", "C_RCVR1", "L_RCVR2", "R_RCVR2", "NAME", "", "L_RCVR", "L_RCVRx",
                                           "X_RCVR2_RCVR3", "L_RCVR 7", "L_RCVR+4", "L_RCVR-2", "L_RCVR2147483648", "\xCE\xA9_RCVR2",
                                           "L_RCVR\xD9\xA5" };
    ProcessedStreamData d;
    d.streamLabel = labels[rng.below(7)];
    d.pcmChannelId = rng.chance(10) ? -rng.below(3) : rng.below(6);
    d.sourceId = rng.below(4);
    d.jobIndex = rng.chance(10) ? -rng.below(3) : rng.below(4);

    // a clock: epoch-ish, a day boundary, a leap day, a far-future date, a fraction of a second
    static const double bases[] = { 1700000000.0, 0.0, 86399.5, 1709164800.0, 4102444800.0, 1704067199.75, -50.0, 1700000000.3 };
    double t = bases[rng.below(8)] + (rng.chance(50) ? 0.0 : rng.below(200000) / 10.0);
    const int n = rng.chance(5) ? 0 : 1 + rng.below(6);
    for (int i = 0; i < n; ++i)
    {
        d.timesSec.push_back(t);
        const int step = rng.below(10);
        t += step < 2 ? 0.0 : step < 8 ? rng.below(500) / 100.0 : -rng.below(300) / 100.0;
    }
    auto len = [&]() { return rng.chance(85) ? n : rng.below(n + 3); };
    if (rng.chance(75))
    {
        d.lockPercent = generateValues(rng, len());
    }
    if (rng.chance(50))
    {
        d.accumulatedMissedFrames = generateValues(rng, len());
    }
    if (rng.chance(45))
    {
        d.mode = StreamMode::ReceiverChannelInfo;
        for (int c = rng.below(5); c >= 0; --c)
        {
            ProcessedChannelSeries ch;
            ch.name = channel_names[rng.below(16)];
            ch.values = generateValues(rng, len(), true);
            d.channels.push_back(std::move(ch));
        }
    }
    return d;
}

}  // namespace

TEST_CASE("seriesbuild: the plot's sequences are what the code the core replaced made, for every generated sequence")
{
    constexpr int kSequences = 700;
    Rng rng(20261029u);
    int adds = 0, removes = 0, clears = 0, rebased = 0, warned = 0, multi_source = 0, replaced = 0, snr = 0, nonfinite = 0;
    for (int n = 0; n < kSequences; ++n)
    {
        Host host;
        oracle::OldPlot old_plot;

        const int ops = 1 + rng.below(8);
        for (int k = 0; k < ops; ++k)
        {
            const int kind = rng.below(100);
            std::string what;
            if (kind < 76)
            {
                const ProcessedStreamData d = generateStream(rng);
                const std::size_t before = old_plot.m_series.size();
                const double base_before = old_plot.m_base_abs_seconds;
                old_plot.addStreamData(d);
                host.addStreamData(d);
                ++adds;
                what = std::format("add {} src{} ch{}", d.streamLabel, d.sourceId, d.pcmChannelId);
                rebased += d.hasSamples() && before > 0 && old_plot.m_base_abs_seconds < base_before;
                replaced += d.hasSamples() && before > 0 && old_plot.m_series.size() <= before;
                snr += int(d.channels.size());
                for (double v : d.lockPercent)
                {
                    nonfinite += !std::isfinite(v);
                }
            }
            else if (kind < 94)
            {
                const int source = rng.below(5);
                old_plot.removeSource(source);
                host.removeSource(source);
                ++removes;
                what = std::format("remove {}", source);
            }
            else
            {
                old_plot.clearData();
                host.clearData();
                ++clears;
                what = "clear";
            }
            std::vector<int> sources;
            for (const auto& s : old_plot.m_series)
            {
                if (std::find(sources.begin(), sources.end(), s.sourceId) == sources.end())
                {
                    sources.push_back(s.sourceId);
                }
            }
            multi_source += sources.size() > 1;
            warned += int(old_plot.warnings.size());

            // Ids come from a counter that clearData() does not reset; the old class starts at 1 too, so ids agree.
            INFO("sequence " << n << " step " << k << " (" << what << ")");
            const std::string want = old_plot.snapshot();
            const std::string got = host.plot.digest();
            REQUIRE(want == got);
        }
    }
    // The generator must reach each outcome in volume, or "no difference" proves nothing.
    CHECK(adds > 2000);
    CHECK(removes > 400);
    CHECK(clears > 50);
    CHECK(rebased > 100);
    CHECK(warned > 100);
    CHECK(multi_source > 1000);
    CHECK(replaced > 50);
    CHECK(snr > 1000);
    CHECK(nonfinite > 20);
}

TEST_CASE("seriesbuild: an imported file's streams are ordered as the code the core replaced ordered them")
{
    Rng rng(20261030u);
    static const char* labels[] = { "A", "B", "C", "", "R\xC3\xA9", "Zed" };
    int fresh = 0, shared = 0, multi_source = 0;
    for (int n = 0; n < 4000; ++n)
    {
        std::vector<Series> old_series;
        std::vector<ImportedSeries> core;
        const int count = rng.below(10);
        for (int i = 0; i < count; ++i)
        {
            Series s;
            const int m = rng.below(3);
            s.metricType = MetricType(m);
            s.sourceId = rng.below(3);
            s.streamLabel = labels[rng.below(6)];
            s.streamOrder = m == 0 ? rng.below(8) * 5 : 0;   // SNR columns carry a channel id, the others 0
            old_series.push_back(s);
            core.push_back({ s.metricType, s.sourceId, s.streamLabel, s.streamOrder });
        }
        oracle::assignImportedOrders(old_series);
        assignImportedStreamOrders(core);
        for (int i = 0; i < count; ++i)
        {
            INFO("case " << n << " series " << i);
            REQUIRE(old_series[static_cast<std::size_t>(i)].streamOrder == core[static_cast<std::size_t>(i)].streamOrder);
        }
        std::vector<int> sources;
        for (const auto& s : old_series)
        {
            if (std::find(sources.begin(), sources.end(), s.sourceId) == sources.end())
            {
                sources.push_back(s.sourceId);
            }
            if (s.metricType != MetricType::SNR)
            {
                fresh += s.streamOrder >= 1 && s.streamOrder % 5 != 0;
                shared += s.streamOrder % 5 == 0 && s.streamOrder > 0;
            }
        }
        multi_source += sources.size() > 1;
    }
    CHECK(fresh > 3000);
    CHECK(shared > 500);
    CHECK(multi_source > 2000);
}

TEST_CASE("seriesbuild: a spec views the processed data without copying it")
{
    // The y values of a spec are spans over the processed stream's own vectors: a host copies them once, into its own
    // storage, and the core makes no second copy.
    ProcessedStreamData d = stream("Rcvr", 40, 0, 0, { kT0, kT0 + 1 }, { 90, 95 }, { 0, 3 }, { { "L_RCVR1", { -80, -79 } } });
    const StreamSeries built = buildStreamSeries(d, kT0);
    REQUIRE(built.series.size() == 3);
    CHECK(built.series[0].yValues.data() == d.lockPercent.data());
    CHECK(built.series[1].yValues.data() == d.accumulatedMissedFrames.data());
    CHECK(built.series[2].yValues.data() == d.channels[0].values.data());
    CHECK(built.elapsed == std::vector<double>{ 0.0, 1.0 });
    CHECK(built.series[2].name == "40 - Rcvr L_RCVR1");
}

TEST_CASE("seriesbuild: the time base reports how far the series must shift")
{
    SeriesTimeBase base;
    CHECK(base.adopt(1000.0, false) == 0.0);             // the first stream: the base is its first sample
    CHECK(base.baseAbsSeconds() == 1000.0);
    CHECK(base.adopt(1200.0, true) == 0.0);              // a later one does not move it
    CHECK(base.baseAbsSeconds() == 1000.0);
    CHECK(base.adopt(940.0, true) == 60.0);              // an earlier one does, and existing series shift right by the gap
    CHECK(base.baseAbsSeconds() == 940.0);
    CHECK(base.adopt(2000.0, false) == 0.0);             // with nothing left the base may move LATER
    CHECK(base.baseAbsSeconds() == 2000.0);

    CHECK(base.overlaps(2000.0, 2010.0, 5.0));           // touching at one instant counts
    CHECK(base.overlaps(2005.0, 2100.0, 5.0));
    CHECK_FALSE(base.overlaps(2005.5, 2100.0, 5.0));
    CHECK_FALSE(base.overlaps(1000.0, 1999.5, 5.0));
    CHECK(base.overlaps(1000.0, 2000.0, 5.0));

    CHECK(base.advance(std::nullopt) == 0.0);            // nothing remaining has a sample
    CHECK(base.advance(0.0) == 0.0);                     // the earliest is still the base
    CHECK(base.advance(7.5) == 7.5);                     // the earliest remaining starts later: the base follows, the rest shift left
    CHECK(base.baseAbsSeconds() == 2007.5);

    base.setClock(3, 100.0);                             // an import states its own clock and leaves the absolute base alone
    CHECK(base.baseDay() == 3);
    CHECK(base.baseTimeOffset() == 100.0);
    CHECK(base.baseAbsSeconds() == 2007.5);
    base.reset();
    CHECK(base.baseDay() == 0);
    CHECK(base.baseAbsSeconds() == 0.0);
}
