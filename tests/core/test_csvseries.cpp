// CSV series: the application's CSV, read by CsvSeriesParser and written by CsvSeriesWriter, and the clock stamp in its
// time column (clocktime::format), called directly. What a host does around them (the series a stream becomes, the
// source filter, the load warning) is the host's to test.
//
// The oracle is v2.14.2: its CSV reader and writer, over generated files and series, and its
// number reader over awkward texts. What they answered was recorded once with a throwaway diagnostic (removed again):
// one FNV-1a digest per comparison, and the counters the reach checks read. Each generator is the one that recording
// used, token for token, on the same seeds.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "clocktime.h"
#include "csvseriesparser.h"
#include "csvserieswriter.h"
#include "doctest.h"
#include "processedstreamdata.h"
#include "seriesbuilder.h"
#include "seriescolumnschema.h"
#include "test_support.h"
#include "textformat.h"
#include "textparse.h"

using namespace tmdq_test;

namespace {

const std::string kTimeHeader = "Time (DOY:HH:MM:SS.mmm)";

/// A byte string with its control and non-ASCII bytes spelled out, so an expectation reads in a log.
std::string escaped(const std::string& bytes)
{
    std::string out;
    for (const char c : bytes)
    {
        const auto u = static_cast<unsigned char>(c);
        if (c == '\r')
        {
            out += "\\r";
        }
        else if (c == '\n')
        {
            out += "\\n";
        }
        else if (c == '\\')
        {
            out += "\\\\";
        }
        else if (u < 0x20 || u >= 0x7F)
        {
            out += std::format("\\x{:02x}", u);
        }
        else
        {
            out += c;
        }
    }
    return out;
}

/// `v` in %g with 12 significant digits; a NaN prints "nan" and an infinity "inf" or "-inf".
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
    return std::format("{:.12g}", v);
}

/// A double as exact bits, with every NaN the same, so two parses can be compared for identity.
std::string bits(double v)
{
    if (std::isnan(v))
    {
        return "nan";
    }
    std::uint64_t b = 0;
    std::memcpy(&b, &v, sizeof b);
    return std::format("{:x}", b);
}

std::string bitsOf(const std::vector<double>& values)
{
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        out += (i ? " " : "") + bits(values[i]);
    }
    return out;
}

struct Fnv
{
    std::uint64_t h = 1469598103934665603ULL;
    void byte(unsigned char b) { h ^= b; h *= 1099511628211ULL; }
    void bytes(const std::string& a) { for (const char c : a) byte(static_cast<unsigned char>(c)); }
    void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) byte(static_cast<unsigned char>(v >> (8 * i))); }
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) byte(static_cast<unsigned char>(v >> (8 * i))); }
};

/// A deterministic generator (the recording's, state for state), so a failure names the same case every run.
struct Rng
{
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed) {}
    std::uint32_t next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    int below(int n) { return int(next() % std::uint32_t(n)); }
    bool chance(int percent) { return below(100) < percent; }
    template <typename T>
    const T& pick(const std::vector<T>& list) { return list[static_cast<std::size_t>(below(int(list.size())))]; }
};

using Strings = std::vector<std::string>;

// ---------------------------------------------------------------------------
// Text encodings, for the files a spreadsheet or an editor might have saved
// ---------------------------------------------------------------------------

std::string utf16be(const std::string& text)
{
    std::string out("\xFE\xFF", 2);
    for (const char16_t c : units(text))
    {
        out += static_cast<char>(c >> 8);
        out += static_cast<char>(c & 0xFF);
    }
    return out;
}

std::string utf32le(const std::string& text)
{
    std::string out("\xFF\xFE\x00\x00", 4);
    for (const char16_t c : units(text))
    {
        out += static_cast<char>(c & 0xFF);
        out += static_cast<char>(c >> 8);
        out += std::string(2, '\0');
    }
    return out;
}

// ---------------------------------------------------------------------------
// What an import produces
// ---------------------------------------------------------------------------

CsvParseResult parseBytes(const TempDir& dir, const std::string& bytes)
{
    const fs::path path = dir.file("in.csv");
    writeBytes(path, bytes);
    return CsvSeriesParser::parse(path);
}

/// Everything an import produces, as one line per fact: whether it loaded, the skipped rows, the base time, the span, and
/// every series' identity and samples.
std::string importDigest(const CsvParseResult& r)
{
    if (!r.success)
    {
        return "fail";
    }
    std::string out = std::format("ok skipped={} base={}/{} xmax={} n={}", r.skippedRows, r.baseDay, num(r.baseTimeOffset),
                                  num(r.xMax), r.series.size());
    // The parser yields a frame-sync series with no order; the plot gives it the order of its stream when it takes the
    // result in (assignImportedStreamOrders), and the order is part of what each case records.
    std::vector<ImportedSeries> imported;
    for (const CsvSeries& s : r.series)
    {
        imported.push_back({ s.metricType, s.sourceId, s.streamLabel, s.streamOrder });
    }
    assignImportedStreamOrders(imported);
    for (std::size_t i = 0; i < r.series.size(); ++i)
    {
        const CsvSeries& s = r.series[i];
        const char metric = s.metricType == MetricType::SNR            ? 'S'
                            : s.metricType == MetricType::FrameSyncLock ? 'L'
                                                                        : 'M';
        out += std::format(" | [{}] {} label={} {} order={} rcv={} ch={} src={} :", i, asciiOnly(s.name), asciiOnly(s.streamLabel),
                           metric, imported[i].streamOrder, s.receiverIndex, s.channelIndex, s.sourceId);
        for (std::size_t j = 0; j < s.xValues.size(); ++j)
        {
            out += std::format(" {},{}", num(s.xValues[j]), num(s.yValues[j]));
        }
    }
    return out;
}

/// The same result as exact bits, every field: what the generated comparison digests.
std::string bitsDigest(const CsvParseResult& r)
{
    std::string out = std::format("{} {} {} {} {} n={}", int(r.success), r.baseDay, bits(r.baseTimeOffset), bits(r.xMax),
                                  r.skippedRows, r.series.size());
    for (const CsvSeries& s : r.series)
    {
        out += std::format("\n[{}|{}|{}|{}|{}|{}|{}|{}|{}] x={} y={}", s.name, s.streamLabel, int(s.metricType), s.receiverIndex,
                           s.channelIndex, s.streamOrder, s.sourceId, bits(s.yMin), bits(s.yMax), bitsOf(s.xValues),
                           bitsOf(s.yValues));
    }
    return out;
}

// ---------------------------------------------------------------------------
// What an export writes
// ---------------------------------------------------------------------------

using Streams = std::vector<ProcessedStreamData>;

ProcessedStreamData lockStream(const std::string& label, int channel, const std::vector<double>& times,
                               const std::vector<double>& lock, const std::vector<double>& missed, int source_id = 0)
{
    ProcessedStreamData d;
    d.streamLabel = label;
    d.pcmChannelId = channel;
    d.sourceId = source_id;
    d.mode = StreamMode::FrameSyncLockStats;
    d.timesSec = times;
    d.lockPercent = lock;
    d.accumulatedMissedFrames = missed;
    return d;
}

// A base time of day 3, 07:08:09, in absolute seconds; every figure below is exactly representable.
constexpr double kBase = 3 * 86400.0 + 7 * 3600.0 + 8 * 60.0 + 9.0;

/// What a host does between a processed stream and the writer: the time base is the earliest first sample, each stream
/// becomes its series on that base (buildStreamSeries), and the writer gets one column per series, for one source or for
/// every source (a negative @p source_id). None of the cases adds a source that starts earlier than one already there,
/// so the base never moves and the streams are built once at the end.
struct Export
{
    bool wrote = false;
    std::string bytes;
};

Export exportOf(const TempDir& dir, const Streams& streams, int source_id)
{
    SeriesTimeBase base;
    bool had = false;
    for (const ProcessedStreamData& s : streams)
    {
        if (!s.hasSamples())
        {
            continue;
        }
        REQUIRE(base.adopt(s.timesSec.front(), had) == 0.0);
        had = true;
    }
    std::vector<StreamSeries> built;
    for (const ProcessedStreamData& s : streams)
    {
        if (s.hasSamples())
        {
            built.push_back(buildStreamSeries(s, base.baseAbsSeconds()));
        }
    }
    std::vector<CsvColumn> columns;
    for (const StreamSeries& b : built)
    {
        for (const SeriesSpec& spec : b.series)
        {
            if (source_id >= 0 && spec.sourceId != source_id)
            {
                continue;
            }
            columns.push_back({ spec.metricType, spec.name, spec.sourceId, std::span<const double>(b.elapsed),
                                spec.yValues });
        }
    }
    const fs::path path = dir.file("out.csv");
    std::error_code ec;
    fs::remove(path, ec);
    Export out;
    out.wrote = CsvSeriesWriter::write(path, columns, CsvTimeBase{ base.baseDay(), base.baseTimeOffset() });
    out.bytes = readBytes(path);
    return out;
}

// ---------------------------------------------------------------------------
// Import cases
// ---------------------------------------------------------------------------

struct ImportCase
{
    const char* name;
    std::string bytes;
    const char* expect;
};

const std::string kOneLock = kTimeHeader + ",A Lock (%)\n1:00:00:00.000,50\n1:00:00:01.000,60\n";

const std::vector<ImportCase>& importCases()
{
    static const std::vector<ImportCase> cases = {
        { "basic-lock", kOneLock, "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "no-trailing-newline", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,50\n1:00:00:01.000,60", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "crlf", kTimeHeader + ",A Lock (%)\r\n1:00:00:00.000,50\r\n1:00:00:01.000,60\r\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "lone-cr-line-ends", kTimeHeader + ",A Lock (%)\r1:00:00:00.000,50\r1:00:00:01.000,60\r", "fail" },
        { "mixed-line-ends", kTimeHeader + ",A Lock (%)\r\n1:00:00:00.000,50\n1:00:00:01.000,60\r\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "blank-lines-between-rows", kTimeHeader + ",A Lock (%)\n\n1:00:00:00.000,50\n\n\n1:00:00:01.000,60\n\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "whitespace-only-line-is-a-short-row", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,50\n   \n1:00:00:01.000,60\n", "ok skipped=1 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "utf8-bom", std::string("\xEF\xBB\xBF") + kOneLock, "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "utf16le-bom", utf16le(kOneLock), "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        { "utf16be-bom", utf16be(kOneLock), "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60" },
        // Changed on purpose when the importer moved into the core (plan step 1.2); it loaded as "basic-lock"
        // before: v2.14.2 detected a UTF-32 byte-order mark. Nothing writes a CSV in UTF-32, and the core
        // reads UTF-8 and UTF-16 (like TomlDocument), so it is now refused as not this format.
        { "utf32le-bom", utf32le(kOneLock), "fail" },
        { "utf16le-no-bom-is-garbage", utf16le(kOneLock, false), "fail" },
        // Two bytes of the UTF-8 mark and a third that is not its last are not a mark, and not a header either.
        { "utf8-mark-lookalike", std::string("\xEF\xBB\x20", 3) + kOneLock, "fail" },
        // The last line of a UTF-16 file is one character with no line end: a short row, and still a line.
        { "utf16-one-character-last-line", utf16le(kTimeHeader + ",A Lock (%)\n1:00:00:00.000,50\nx"), "ok skipped=1 base=1/0 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50" },

        // --- the header line
        { "empty-file", std::string(), "fail" },
        { "blank-first-line", std::string("\n") + kOneLock, "fail" },
        { "header-only", kTimeHeader + ",A Lock (%)\n", "fail" },
        { "header-only-no-newline", kTimeHeader + ",A Lock (%)", "fail" },
        { "time-column-alone", kTimeHeader + "\n1:00:00:00.000\n", "fail" },
        { "wrong-time-header", std::string("Day,Time,A Lock (%)\n1,00:00:00,50\n"), "fail" },
        { "time-header-is-case-sensitive", std::string("time (DOY:HH:MM:SS.mmm),A Lock (%)\n1:00:00:00.000,50\n"), "fail" },
        { "time-header-padded", std::string("  ") + kTimeHeader + " \t,A Lock (%)\n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50" },
        // Changed on purpose (plan step 1.2); it read as the lock series "A" before. Trimming is ASCII-only
        // in the core, as in textparse.h everywhere else, so a no-break space stays part of the name and
        // the column no longer ends in " Lock (%)". The application never writes one.
        { "header-padded-with-a-unicode-space", kTimeHeader + ",\xC2\xA0" "A Lock (%)\xC2\xA0\n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] <U+00A0>A Lock (%)<U+00A0> label= S order=0 rcv=0 ch=0 src=0 : 0,50" },
        { "series-header-padded", kTimeHeader + ",  A Lock (%) \n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50" },
        { "trailing-comma-in-header", kTimeHeader + ",A Lock (%),\n1:00:00:00.000,50,7\n", "ok skipped=0 base=1/0 xmax=0 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50"
            " | [1]  label= S order=0 rcv=0 ch=0 src=0 : 0,7" },
        { "empty-series-header", kTimeHeader + ",,A Lock (%)\n1:00:00:00.000,1,2\n", "ok skipped=0 base=1/0 xmax=0 n=2"
            " | [0]  label= S order=0 rcv=0 ch=0 src=0 : 0,1"
            " | [1] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "duplicate-series-headers", kTimeHeader + ",A Lock (%),A Lock (%)\n1:00:00:00.000,1,2\n", "ok skipped=0 base=1/0 xmax=0 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1"
            " | [1] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "non-ascii-names", kTimeHeader + ",R\xC3\xA9" "cepteur \xCE\xA9 Lock (%)\n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] R<U+00E9>cepteur <U+03A9> label=R<U+00E9>cepteur <U+03A9> L order=1 rcv=0 ch=0 src=0 : 0,50" },
        // The same names saved as UTF-16: a character above U+00FF carries a high byte, which ASCII text never does.
        { "non-ascii-names-utf16le", utf16le(kTimeHeader + ",R\xC3\xA9" "cepteur \xCE\xA9 Lock (%)\n1:00:00:00.000,50\n"), "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] R<U+00E9>cepteur <U+03A9> label=R<U+00E9>cepteur <U+03A9> L order=1 rcv=0 ch=0 src=0 : 0,50" },
        // Changed on purpose (plan step 1.2); the whole file failed to load before. A byte that is not
        // valid UTF-8 (a Latin-1 "e-acute" from a re-saved file) made v2.14.2's text read give up on
        // the file. The core keeps the bytes; the plot shows the replacement character in that one name.
        { "invalid-utf8-in-a-name", kTimeHeader + ",R\xE9" "cepteur Lock (%)\n1:00:00:00.000,50\n", "ok skipped=0 base=1/0 xmax=0 n=1"
            " | [0] R<U+FFFD>cepteur label=R<U+FFFD>cepteur L order=1 rcv=0 ch=0 src=0 : 0,50" },
        { "no-csv-quoting", kTimeHeader + ",\"A,B Lock (%)\"\n1:00:00:00.000,\"5\",6\n", "ok skipped=0 base=1/0 xmax=0 n=2"
            " | [0] \"A label= S order=0 rcv=0 ch=0 src=0 :"
            " | [1] B Lock (%)\" label= S order=0 rcv=0 ch=1 src=0 : 0,6" },

        // --- the series' identity
        { "all-three-kinds", kTimeHeader + ",A Lock (%),A Accumulated Missed Frames,40 - Ch A L_RCVR2\n"
                                              "1:00:00:00.000,90,0,-80.5\n1:00:00:01.000,91,1,-79.5\n", "ok skipped=0 base=1/0 xmax=1 n=3"
            " | [0] A label=A L order=41 rcv=0 ch=0 src=0 : 0,90 1,91"
            " | [1] A label=A M order=41 rcv=0 ch=0 src=0 : 0,0 1,1"
            " | [2] 40 - Ch A L_RCVR2 label=Ch A S order=40 rcv=2 ch=0 src=0 : 0,-80.5 1,-79.5" },
        { "channel-index-counts-per-receiver-and-stream",
          kTimeHeader + ",40 - A L_RCVR1,40 - A R_RCVR1,40 - A C_RCVR1,40 - A L_RCVR2,41 - B L_RCVR1,41 - B R_RCVR1\n"
                        "1:00:00:00.000,1,2,3,4,5,6\n", "ok skipped=0 base=1/0 xmax=0 n=6"
            " | [0] 40 - A L_RCVR1 label=A S order=40 rcv=1 ch=0 src=0 : 0,1"
            " | [1] 40 - A R_RCVR1 label=A S order=40 rcv=1 ch=1 src=0 : 0,2"
            " | [2] 40 - A C_RCVR1 label=A S order=40 rcv=1 ch=2 src=0 : 0,3"
            " | [3] 40 - A L_RCVR2 label=A S order=40 rcv=2 ch=0 src=0 : 0,4"
            " | [4] 41 - B L_RCVR1 label=B S order=41 rcv=1 ch=0 src=0 : 0,5"
            " | [5] 41 - B R_RCVR1 label=B S order=41 rcv=1 ch=1 src=0 : 0,6" },
        { "sources-keep-their-own-channel-counts",
          kTimeHeader + ",40 - A L_RCVR1,40 - A R_RCVR1,S1| 40 - A L_RCVR1,S1| 40 - A R_RCVR1\n"
                        "1:00:00:00.000,1,2,3,4\n", "ok skipped=0 base=1/0 xmax=0 n=4"
            " | [0] 40 - A L_RCVR1 label=A S order=40 rcv=1 ch=0 src=0 : 0,1"
            " | [1] 40 - A R_RCVR1 label=A S order=40 rcv=1 ch=1 src=0 : 0,2"
            " | [2] 40 - A L_RCVR1 label=A S order=40 rcv=1 ch=0 src=1 : 0,3"
            " | [3] 40 - A R_RCVR1 label=A S order=40 rcv=1 ch=1 src=1 : 0,4" },
        { "lock-only-streams-get-orders-after-the-snr-ones",
          kTimeHeader + ",Zed Lock (%),40 - A L_RCVR1,A Lock (%),Zed Accumulated Missed Frames\n"
                        "1:00:00:00.000,1,2,3,4\n", "ok skipped=0 base=1/0 xmax=0 n=4"
            " | [0] Zed label=Zed L order=41 rcv=0 ch=0 src=0 : 0,1"
            " | [1] 40 - A L_RCVR1 label=A S order=40 rcv=1 ch=0 src=0 : 0,2"
            " | [2] A label=A L order=40 rcv=0 ch=0 src=0 : 0,3"
            " | [3] Zed label=Zed M order=41 rcv=0 ch=0 src=0 : 0,4" },
        { "two-sources-with-one-label",
          kTimeHeader + ",A Lock (%),S2| A Lock (%),S2| A Accumulated Missed Frames\n"
                        "1:00:00:00.000,1,2,3\n", "ok skipped=0 base=1/0 xmax=0 n=3"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1"
            " | [1] A label=A L order=2 rcv=0 ch=0 src=2 : 0,2"
            " | [2] A label=A M order=2 rcv=0 ch=0 src=2 : 0,3" },

        // --- the data rows
        { "short-row-skipped", kTimeHeader + ",A Lock (%),B Lock (%)\n1:00:00:00.000,1,2\n1:00:00:01.000,3\n1:00:00:02.000,5,6\n", "ok skipped=1 base=1/0 xmax=2 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 2,5"
            " | [1] B label=B L order=2 rcv=0 ch=0 src=0 : 0,2 2,6" },
        { "extra-fields-ignored", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,1,2,3\n1:00:00:01.000,4,5\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 1,4" },
        { "empty-cell-leaves-the-series-sparse", kTimeHeader + ",A Lock (%),B Lock (%)\n1:00:00:00.000,1,\n1:00:00:01.000,,4\n1:00:00:02.000,5,6\n", "ok skipped=0 base=1/0 xmax=2 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 2,5"
            " | [1] B label=B L order=2 rcv=0 ch=0 src=0 : 1,4 2,6" },
        { "unparseable-cell-skipped", kTimeHeader + ",A Lock (%),B Lock (%)\n1:00:00:00.000,x,2\n1:00:00:01.000,3,4\n", "ok skipped=0 base=1/0 xmax=1 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 1,3"
            " | [1] B label=B L order=2 rcv=0 ch=0 src=0 : 0,2 1,4" },
        { "cell-padding-and-signs", kTimeHeader + ",A Lock (%)\n1:00:00:00.000, 5 \n1:00:00:01.000,+6\n1:00:00:02.000,-7\n1:00:00:03.000,\t8\t\n", "ok skipped=0 base=1/0 xmax=3 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,5 1,6 2,-7 3,8" },
        // Changed on purpose (plan step 1.2); the cell read as 5 before. v2.14.2's number parser ignored
        // Unicode spaces around a number; the core's (textparse.h) ignores ASCII ones only, so this cell
        // is not a number and is skipped like any other. The application never writes one.
        { "cell-padded-with-a-unicode-space", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,\xC2\xA0" "5\xC2\xA0\n1:00:00:01.000,6\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 1,6" },
        { "cell-number-forms", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,1e2\n1:00:00:01.000,.5\n1:00:00:02.000,5.\n1:00:00:03.000,1E-1\n1:00:00:04.000,0x10\n1:00:00:05.000,1_0\n", "ok skipped=0 base=1/0 xmax=3 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,100 1,0.5 2,5 3,0.1" },
        { "cell-special-values", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,nan\n1:00:00:01.000,inf\n1:00:00:02.000,-inf\n1:00:00:03.000,NaN\n1:00:00:04.000,Infinity\n1:00:00:05.000,+inf\n", "ok skipped=0 base=1/0 xmax=5 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,nan 1,inf 2,-inf 3,nan 5,inf" },
        { "cell-overflow-and-denormal", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,1e999\n1:00:00:01.000,1e-999\n1:00:00:02.000,4.9e-324\n", "ok skipped=0 base=1/0 xmax=2 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 2,4.94065645841e-324" },
        { "cell-with-a-decimal-comma", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,1;5\n1:00:00:01.000,2\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 1,2" },
        { "cell-with-non-ascii-digit", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,\xD9\xA5\n1:00:00:01.000,2\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 1,2" },
        { "every-row-bad", kTimeHeader + ",A Lock (%)\nnot a time,1\nalso not,2\n", "fail" },
        { "every-cell-bad", kTimeHeader + ",A Lock (%)\n1:00:00:00.000,x\n1:00:00:01.000,y\n", "fail" },

        // --- the timestamp
        { "time-fields-unpadded", kTimeHeader + ",A Lock (%)\n1:0:0:0,1\n1:0:0:1.5,2\n", "ok skipped=0 base=1/0 xmax=1.5 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 1.5,2" },
        { "time-fields-padded-wide", kTimeHeader + ",A Lock (%)\n001:00:00:00.000,1\n0001:00:00:01.000,2\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 1,2" },
        { "time-fields-spaced", kTimeHeader + ",A Lock (%)\n 1 : 0 : 0 : 0 ,1\n1:00:00: 1.5,2\n", "ok skipped=0 base=1/0 xmax=1.5 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 1.5,2" },
        { "time-fields-signed", kTimeHeader + ",A Lock (%)\n+1:+0:+0:+0,1\n1:00:00:01,2\n", "ok skipped=0 base=1/0 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 1,2" },
        { "time-negative-day", kTimeHeader + ",A Lock (%)\n-1:00:00:00,1\n0:00:00:00,2\n", "ok skipped=0 base=-1/0 xmax=86400 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 86400,2" },
        { "time-out-of-range-fields", kTimeHeader + ",A Lock (%)\n1:25:61:75.5,1\n1:00:00:00,2\n", "ok skipped=0 base=1/93735.5 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 -93735.5,2" },
        { "time-three-fields", kTimeHeader + ",A Lock (%)\n1:00:00,1\n1:00:00:01,2\n", "ok skipped=1 base=1/1 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "time-five-fields", kTimeHeader + ",A Lock (%)\n1:00:00:00:00,1\n1:00:00:01,2\n", "ok skipped=1 base=1/1 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "time-fraction-in-a-whole-field", kTimeHeader + ",A Lock (%)\n1.5:00:00:00,1\n1:00:00:01,2\n", "ok skipped=1 base=1/1 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "time-exponent-seconds", kTimeHeader + ",A Lock (%)\n1:00:00:1e1,1\n1:00:00:01,2\n", "ok skipped=0 base=1/10 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 -9,2" },
        { "time-empty-field", kTimeHeader + ",A Lock (%)\n1::00:00,1\n1:00:00:01,2\n", "ok skipped=1 base=1/1 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2" },
        { "time-crossing-midnight", kTimeHeader + ",A Lock (%)\n1:23:59:59.500,1\n2:00:00:00.250,2\n", "ok skipped=0 base=1/86399.5 xmax=0.75 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 0.75,2" },
        { "time-going-backwards", kTimeHeader + ",A Lock (%)\n1:00:00:05,1\n1:00:00:01,2\n1:00:00:09,3\n", "ok skipped=0 base=1/5 xmax=4 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 -4,2 4,3" },
        { "time-repeated", kTimeHeader + ",A Lock (%)\n1:00:00:01,1\n1:00:00:01,2\n", "ok skipped=0 base=1/1 xmax=0 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,1 0,2" },
        { "first-good-row-sets-the-base", kTimeHeader + ",A Lock (%)\nbad,1\n2:03:04:05.250,2\n2:03:04:06.250,3\n", "ok skipped=1 base=2/11045.25 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2 1,3" },
        { "first-row-with-an-unparseable-value-still-sets-the-base",
          kTimeHeader + ",A Lock (%)\n2:03:04:05.250,x\n2:03:04:06.250,3\n", "ok skipped=0 base=2/11045.25 xmax=1 n=1"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 1,3" },
        { "a-first-row-with-too-few-fields-does-not-set-the-base",
          kTimeHeader + ",A Lock (%),B Lock (%)\n2:03:04:05.250,1\n2:03:04:06.250,2,3\n", "ok skipped=1 base=2/11046.25 xmax=0 n=2"
            " | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,2"
            " | [1] B label=B L order=2 rcv=0 ch=0 src=0 : 0,3" },
        { "many-columns", kTimeHeader + ",c1 Lock (%),c2 Lock (%),c3 Lock (%),c4 Lock (%),c5 Lock (%),c6 Lock (%),c7 Lock (%),c8 Lock (%)\n"
                                         "1:00:00:00,1,2,3,4,5,6,7,8\n1:00:00:01,9,10,11,12,13,14,15,16\n", "ok skipped=0 base=1/0 xmax=1 n=8"
            " | [0] c1 label=c1 L order=1 rcv=0 ch=0 src=0 : 0,1 1,9"
            " | [1] c2 label=c2 L order=2 rcv=0 ch=0 src=0 : 0,2 1,10"
            " | [2] c3 label=c3 L order=3 rcv=0 ch=0 src=0 : 0,3 1,11"
            " | [3] c4 label=c4 L order=4 rcv=0 ch=0 src=0 : 0,4 1,12"
            " | [4] c5 label=c5 L order=5 rcv=0 ch=0 src=0 : 0,5 1,13"
            " | [5] c6 label=c6 L order=6 rcv=0 ch=0 src=0 : 0,6 1,14"
            " | [6] c7 label=c7 L order=7 rcv=0 ch=0 src=0 : 0,7 1,15"
            " | [7] c8 label=c8 L order=8 rcv=0 ch=0 src=0 : 0,8 1,16" },
    };
    return cases;
}

}  // namespace

TEST_CASE("csvseries: every import case reads as recorded")
{
    TempDir dir;
    for (const ImportCase& c : importCases())
    {
        INFO("case " << c.name);
        CHECK(importDigest(parseBytes(dir, c.bytes)) == std::string(c.expect));
    }
}

// ---------------------------------------------------------------------------
// Export cases
// ---------------------------------------------------------------------------

namespace {

struct ExportCase
{
    const char* name;
    std::function<void(Streams&)> build;
    int source_id;
    const char* expect;
};

const std::vector<ExportCase>& exportCases()
{
    static const std::vector<ExportCase> cases = {
        { "one-stream",
          [](Streams& streams) { streams.push_back(lockStream("Ch 5", 5, { kBase, kBase + 1, kBase + 2 }, { 90, 95, 100 }, { 0, 1, 1 })); },
          -1, "Time (DOY:HH:MM:SS.mmm),Ch 5 Lock (%),Ch 5 Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,90.000000,0.000000\\r\\n"
            "004:07:08:10.000,95.000000,1.000000\\r\\n"
            "004:07:08:11.000,100.000000,1.000000\\r\\n"
            "" },
        { "two-streams-on-different-clocks-leave-cells-empty",
          [](Streams& streams) {
              streams.push_back(lockStream("A", 1, { kBase, kBase + 1 }, { 10, 20 }, { 0, 1 }));
              streams.push_back(lockStream("B", 2, { kBase + 0.5, kBase + 1 }, { 30, 40 }, { 2, 3 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames,B Lock (%),B Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,10.000000,0.000000,,\\r\\n"
            "004:07:08:09.500,,,30.000000,2.000000\\r\\n"
            "004:07:08:10.000,20.000000,1.000000,40.000000,3.000000\\r\\n"
            "" },
        { "milliseconds-round-half-up",
          [](Streams& streams) {
              streams.push_back(lockStream("A", 1, { kBase, kBase + 0.0004, kBase + 0.0005, kBase + 0.0006, kBase + 0.0015, kBase + 0.0025 },
                                          { 1, 2, 3, 4, 5, 6 }, { 0, 0, 0, 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,2.000000,0.000000\\r\\n"
            "004:07:08:09.001,4.000000,0.000000\\r\\n"
            "004:07:08:09.002,5.000000,0.000000\\r\\n"
            "004:07:08:09.003,6.000000,0.000000\\r\\n"
            "" },
        { "times-either-side-of-the-base-on-the-grid-of-their-rate",
          // Out of order, steps of 1/16 and 1/8 s: a median step of 125 ms is a 100 ms rate, so each time moves to the
          // nearest tenth of a second (.8125 to .8, .9375 to .9, 1.0625 to 1.1, 1.1875 to 1.2).
          [](Streams& streams) {
              streams.push_back(lockStream("A", 1, { kBase + 1, kBase + 1 - 0.0625, kBase + 1 - 0.1875, kBase + 1 + 0.0625, kBase + 1 + 0.1875 },
                                          { 1, 2, 3, 4, 5 }, { 0, 0, 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.800,3.000000,0.000000\\r\\n"
            "004:07:08:09.900,2.000000,0.000000\\r\\n"
            "004:07:08:10.000,1.000000,0.000000\\r\\n"
            "004:07:08:10.100,4.000000,0.000000\\r\\n"
            "004:07:08:10.200,5.000000,0.000000\\r\\n"
            "" },
        { "six-decimal-ties-and-extremes",
          [](Streams& streams) {
              streams.push_back(lockStream("A", 1, { kBase, kBase + 1, kBase + 2, kBase + 3, kBase + 4, kBase + 5, kBase + 6, kBase + 7 },
                                          { 0.0078125, 0.0234375, 99.9999995, 1e-7, -0.0000005, 1e10, 123456789.123456789, -0.0 },
                                          { 0, 0, 0, 0, 0, 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,0.007813,0.000000\\r\\n"
            "004:07:08:10.000,0.023438,0.000000\\r\\n"
            "004:07:08:11.000,100.000000,0.000000\\r\\n"
            "004:07:08:12.000,0.000000,0.000000\\r\\n"
            "004:07:08:13.000,-0.000000,0.000000\\r\\n"
            "004:07:08:14.000,10000000000.000000,0.000000\\r\\n"
            "004:07:08:15.000,123456789.123457,0.000000\\r\\n"
            "004:07:08:16.000,0.000000,0.000000\\r\\n"
            "" },
        { "not-a-number-and-infinity",
          [](Streams& streams) {
              streams.push_back(lockStream("A", 1, { kBase, kBase + 1, kBase + 2 },
                                          { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                                            -std::numeric_limits<double>::infinity() },
                                          { 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,nan,0.000000\\r\\n"
            "004:07:08:10.000,inf,0.000000\\r\\n"
            "004:07:08:11.000,-inf,0.000000\\r\\n"
            "" },
        { "two-samples-in-one-millisecond-the-last-wins",
          [](Streams& streams) {
              streams.push_back(lockStream("A", 1, { kBase, kBase + 1, kBase + 1.0001, kBase + 2 }, { 1, 2, 3, 4 }, { 0, 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,1.000000,0.000000\\r\\n"
            "004:07:08:10.000,3.000000,0.000000\\r\\n"
            "004:07:08:11.000,4.000000,0.000000\\r\\n"
            "" },
        { "samples-out-of-order-are-written-in-time-order",
          [](Streams& streams) {
              streams.push_back(lockStream("A", 1, { kBase + 2, kBase, kBase + 1 }, { 3, 1, 2 }, { 30, 10, 20 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,1.000000,10.000000\\r\\n"
            "004:07:08:10.000,2.000000,20.000000\\r\\n"
            "004:07:08:11.000,3.000000,30.000000\\r\\n"
            "" },
        { "a-day-boundary",
          [](Streams& streams) {
              streams.push_back(lockStream("A", 1, { 5 * 86400.0 + 86399.5, 6 * 86400.0 + 0.25 }, { 1, 2 }, { 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "006:23:59:59.500,1.000000,0.000000\\r\\n"
            "007:00:00:00.250,2.000000,0.000000\\r\\n"
            "" },
        { "a-recording-starting-just-before-midnight-and-a-long-one",
          [](Streams& streams) {
              streams.push_back(lockStream("A", 1, { 86399.999, 86400.0, 2 * 86400.0 + 3661.5 }, { 1, 2, 3 }, { 0, 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\\r\\n"
            "001:23:59:59.999,1.000000,0.000000\\r\\n"
            "002:00:00:00.000,2.000000,0.000000\\r\\n"
            "003:01:01:01.500,3.000000,0.000000\\r\\n"
            "" },
        { "a-stream-with-no-missed-frames-series",
          [](Streams& streams) { streams.push_back(lockStream("A", 1, { kBase, kBase + 1 }, { 1, 2 }, {})); },
          -1, "Time (DOY:HH:MM:SS.mmm),A Lock (%)\\r\\n"
            "004:07:08:09.000,1.000000\\r\\n"
            "004:07:08:10.000,2.000000\\r\\n"
            "" },
        { "an-empty-and-a-non-ascii-label",
          [](Streams& streams) {
              streams.push_back(lockStream("", 1, { kBase, kBase + 1 }, { 1, 2 }, { 0, 0 }));
              streams.push_back(lockStream("R\xC3\xA9" "cepteur \xCE\xA9", 2, { kBase, kBase + 1 }, { 3, 4 }, { 0, 0 }));
          },
          -1, "Time (DOY:HH:MM:SS.mmm), Lock (%), Accumulated Missed Frames,R\\xc3\\xa9cepteur \\xce\\xa9 Lock (%),R\\xc3\\xa9cepteur \\xce\\xa9 Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,1.000000,0.000000,3.000000,0.000000\\r\\n"
            "004:07:08:10.000,2.000000,0.000000,4.000000,0.000000\\r\\n"
            "" },
        { "two-sources-all",
          [](Streams& streams) {
              streams.push_back(lockStream("Ch 5", 5, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 }, 0));
              streams.push_back(lockStream("Ch 5", 5, { kBase + 1, kBase + 2 }, { 3, 4 }, { 0, 2 }, 2));
          },
          -1, "Time (DOY:HH:MM:SS.mmm),Ch 5 Lock (%),Ch 5 Accumulated Missed Frames,S2| Ch 5 Lock (%),S2| Ch 5 Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,1.000000,0.000000,,\\r\\n"
            "004:07:08:10.000,2.000000,1.000000,3.000000,0.000000\\r\\n"
            "004:07:08:11.000,,,4.000000,2.000000\\r\\n"
            "" },
        { "two-sources-first-only",
          [](Streams& streams) {
              streams.push_back(lockStream("Ch 5", 5, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 }, 0));
              streams.push_back(lockStream("Ch 5", 5, { kBase + 1, kBase + 2 }, { 3, 4 }, { 0, 2 }, 2));
          },
          0, "Time (DOY:HH:MM:SS.mmm),Ch 5 Lock (%),Ch 5 Accumulated Missed Frames\\r\\n"
            "004:07:08:09.000,1.000000,0.000000\\r\\n"
            "004:07:08:10.000,2.000000,1.000000\\r\\n"
            "" },
        { "two-sources-second-only",
          [](Streams& streams) {
              streams.push_back(lockStream("Ch 5", 5, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 }, 0));
              streams.push_back(lockStream("Ch 5", 5, { kBase + 1, kBase + 2 }, { 3, 4 }, { 0, 2 }, 2));
          },
          2, "Time (DOY:HH:MM:SS.mmm),S2| Ch 5 Lock (%),S2| Ch 5 Accumulated Missed Frames\\r\\n"
            "004:07:08:10.000,3.000000,0.000000\\r\\n"
            "004:07:08:11.000,4.000000,2.000000\\r\\n"
            "" },
        { "a-source-nothing-belongs-to",
          [](Streams& streams) { streams.push_back(lockStream("A", 1, { kBase, kBase + 1 }, { 1, 2 }, { 0, 1 })); },
          7, "none" },
        { "no-series-at-all", [](Streams&) {}, -1, "none" },
    };
    return cases;
}

}  // namespace

TEST_CASE("csvseries: every export case writes the bytes recorded")
{
    TempDir dir;
    for (const ExportCase& c : exportCases())
    {
        INFO("case " << c.name);
        Streams streams;
        c.build(streams);
        const Export e = exportOf(dir, streams, c.source_id);
        CHECK((e.wrote ? escaped(e.bytes) : std::string(e.bytes.empty() ? "none" : "none (but a file was left)")) ==
              std::string(c.expect));
    }
}

// ---------------------------------------------------------------------------
// The code the core replaced, compared with it by what it answered
// ---------------------------------------------------------------------------

namespace {

/// A CSV as a hand-edited, re-saved or damaged file might be: headers and cells from pools of the awkward and the
/// ordinary, short and long rows, blank lines, the three line endings, a byte-order mark.
std::string generateCsv(Rng& rng)
{
    static const Strings headers = {
        "A Lock (%)", "A Accumulated Missed Frames", "B Lock (%)", "40 - Ch A L_RCVR1", "40 - Ch A R_RCVR1",
        "40 - Ch A C_RCVR1", "41 - B L_RCVR2", "S2| A Lock (%)", "S1| 40 - Ch A L_RCVR1",
        "S3| B Accumulated Missed Frames", "R\xC3\xA9" "cepteur \xCE\xA9 Lock (%)", "",
        " padded Lock (%) ", "x", "12 - ", "S| x", "1 - y z_RCVR", "A Lock (%) extra", "40 - Ch A L_RCVR1",
    };
    static const Strings cells = {
        "12.5", "-3", "1e2", "+4", " 7 ", "", "", "", "x", "nan", "inf", "-inf", ".5", "5.", "0x10", "1,5",
        "1e999", "00012", "-0", "\t3", "4\t", "99.999999", "0.1", "100", "Infinity", "NaN", "-nan", "1e-3",
    };
    static const Strings odd_stamps = {
        "1:2:3:4", " 1:00:00:00 ", "1:00:00", "1:00:00:00:00", "a:b:c:d", "1::2:3", "-1:00:00:00", "1:25:61:75.5",
        "1:00:00:1e1", "+1:+0:+0:+0", "", "x", "1.5:00:00:00", "1:00:00:00,5",
    };
    static const Strings first_headers = { "Day", "time (DOY:HH:MM:SS.mmm)", " Time (DOY:HH:MM:SS.mmm) ", "" };

    std::string text;
    const int nseries = 1 + rng.below(6);
    const std::string first = rng.chance(92) ? kTimeHeader : rng.pick(first_headers);
    text += first;
    for (int i = 0; i < nseries; ++i)
    {
        text += "," + rng.pick(headers);
    }
    if (rng.chance(3))
    {
        text += ",";
    }

    auto eol = [&]() -> std::string {
        const int r = rng.below(100);
        if (r < 70) return "\n";
        if (r < 90) return "\r\n";
        if (r < 93) return "\r\r\n";
        if (r < 96) return "\n\n";
        if (r < 98) return "\r";
        return "\r\n\r\n";
    };
    text += eol();

    const int rows = rng.below(31);
    int day = rng.below(400);
    int seconds_of_day = rng.below(86400);
    for (int r = 0; r < rows; ++r)
    {
        if (rng.chance(4))
        {
            text += rng.chance(50) ? "" : "   ";
            text += eol();
            continue;
        }
        // time advances most rows, and sometimes jumps back or across a day
        seconds_of_day += rng.below(3) == 0 ? 0 : rng.below(5);
        if (rng.chance(5)) seconds_of_day -= rng.below(10);
        if (seconds_of_day >= 86400) { seconds_of_day -= 86400; ++day; }
        if (seconds_of_day < 0) seconds_of_day = 0;
        std::string stamp;
        if (rng.chance(8))
        {
            stamp = rng.pick(odd_stamps);
        }
        else
        {
            const int day_width = rng.chance(80) ? 3 : 1;
            const std::string day_text = std::format("{:0{}}", day, day_width);
            const std::string hh = std::format("{:02}", seconds_of_day / 3600);
            const std::string mm = std::format("{:02}", seconds_of_day / 60 % 60);
            const std::string ss = std::format("{:02}", seconds_of_day % 60);
            stamp = day_text + ":" + hh + ":" + mm + ":" + ss + "." + std::format("{:03}", rng.below(1000));
        }
        text += stamp;
        int fields = nseries;
        if (rng.chance(8)) fields = rng.below(nseries + 2);   // short or long row
        for (int i = 0; i < fields; ++i)
        {
            text += "," + rng.pick(cells);
        }
        if (r + 1 < rows || rng.chance(70))
        {
            text += eol();
        }
    }

    // encoding
    const int enc = rng.below(100);
    if (enc < 5)
    {
        return std::string("\xEF\xBB\xBF", 3) + text;
    }
    if (enc < 15)
    {
        const bool little = enc < 10;
        std::string out = little ? std::string("\xFF\xFE", 2) : std::string("\xFE\xFF", 2);
        for (const char16_t c : units(text))
        {
            const char lo = static_cast<char>(c & 0xFF);
            const char hi = static_cast<char>(c >> 8);
            out += little ? lo : hi;
            out += little ? hi : lo;
        }
        return out;
    }
    return text;
}

}  // namespace

TEST_CASE("csvseries: the core reads every generated file as the code it replaced did")
{
    constexpr int kFiles = 400;
    Rng rng(20261022u);
    TempDir dir;
    Fnv f;
    int loaded = 0, failed = 0, with_skips = 0, multi = 0, bom = 0, utf16 = 0, snr = 0, lock = 0;
    for (int n = 0; n < kFiles; ++n)
    {
        const std::string bytes = generateCsv(rng);
        const CsvParseResult r = parseBytes(dir, bytes);
        f.bytes(bitsDigest(r));
        f.byte(0x1F);
        (r.success ? loaded : failed)++;
        with_skips += r.skippedRows > 0;
        multi += r.series.size() > 1;
        for (const CsvSeries& s : r.series)
        {
            snr += s.metricType == MetricType::SNR;
            lock += s.metricType == MetricType::FrameSyncLock;
        }
        bom += bytes.starts_with("\xEF\xBB\xBF");
        utf16 += bytes.starts_with("\xFF\xFE") || bytes.starts_with("\xFE\xFF");
    }
    // What v2.14.2's importer answered for the same 400 files (recorded), and the counts of what they were.
    CHECK(f.h == 7563245506736825023ULL);
    CHECK(loaded == 350);
    CHECK(failed == 50);
    CHECK(with_skips == 268);
    CHECK(multi == 292);
    CHECK(bom == 17);
    CHECK(utf16 == 31);
    CHECK(snr == 737);
    CHECK(lock == 304);
}

namespace {

/// A series for the writer: names, kinds and sources from small pools, samples on a grid that is full of exact
/// millisecond ties (multiples of 1/16 s are halves of a millisecond at x.5) and repeated, out-of-order and negative
/// times, values that are ties at six decimals, huge, tiny, negative zero, infinite and not a number.
struct GeneratedSeries
{
    std::string name;
    MetricType metricType = MetricType::SNR;
    int sourceId = 0;
    std::vector<double> xValues;
    std::vector<double> yValues;
};

std::vector<GeneratedSeries> generateSeries(Rng& rng)
{
    static const Strings names = {
        "Ch 5", "A", "", "40 - Ch 5 L_RCVR1", "40 - Ch 5 R_RCVR1", "S1| odd", "R\xC3\xA9" "cepteur \xCE\xA9",
        "a, b", "tab\there",
    };
    static const std::vector<double> values = {
        0.0, -0.0, 1.0, -1.0, 99.9999995, 0.0078125, 0.0234375, -0.0078125, 1e-7, -5e-7, 1e10, 123456789.123456789,
        0.5, 2.5, -2.5, 100.0, 0.000001, 0.0000005, std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 1e300, 4.9e-324, 12345.6789012,
    };
    std::vector<GeneratedSeries> all;
    const int count = rng.below(6);
    for (int i = 0; i < count; ++i)
    {
        GeneratedSeries s;
        s.name = rng.pick(names);
        s.metricType = MetricType(rng.below(3));
        s.sourceId = rng.chance(60) ? 0 : rng.below(4);
        const int samples = rng.below(14);
        double t = rng.chance(30) ? -rng.below(5) * 0.25 : 0.0;
        for (int j = 0; j < samples; ++j)
        {
            switch (rng.below(6))
            {
            case 0: t += 1.0; break;
            case 1: t += rng.below(40) / 16.0; break;
            case 2: t += rng.below(1000) / 1000.0; break;
            case 3: t += rng.below(2000) / 2000.0; break;    // exact thousandths and their neighbours
            case 4: t -= rng.below(20) / 16.0; break;        // out of order
            default: break;                                  // the same time again
            }
            s.xValues.push_back(t + (rng.chance(10) ? 1e-9 * rng.below(5) : 0.0));
            s.yValues.push_back(rng.chance(30) ? rng.pick(values) : rng.below(100000) / 1000.0 - 20.0);
        }
        all.push_back(s);
    }
    return all;
}

std::vector<CsvColumn> columnsOf(const std::vector<GeneratedSeries>& all, int source_id = -1)
{
    std::vector<CsvColumn> columns;
    for (const GeneratedSeries& s : all)
    {
        if (source_id >= 0 && s.sourceId != source_id)
        {
            continue;
        }
        columns.push_back({ s.metricType, s.name, s.sourceId, std::span<const double>(s.xValues),
                            std::span<const double>(s.yValues) });
    }
    return columns;
}

}  // namespace

namespace {

/// A column's sample rate by the export's rule, written out the slow and plain way (everything sorted, a table of the
/// steps): the median gap between its distinct times, nearest on a log scale of 1, 2, 5, 10 ... 100,000 ms; 1 ms for a
/// column with under three distinct times or whose gaps are not steady (two thirds within half a median of the median).
double referencePeriodMs(std::vector<double> times)
{
    times.erase(std::remove_if(times.begin(), times.end(), [](double t) { return !std::isfinite(t); }), times.end());
    std::sort(times.begin(), times.end());
    std::vector<double> gaps;
    for (std::size_t i = 1; i < times.size(); ++i)
    {
        if (times[i] > times[i - 1])
        {
            gaps.push_back(times[i] - times[i - 1]);
        }
    }
    if (gaps.size() < 2)
    {
        return 1.0;
    }
    std::sort(gaps.begin(), gaps.end());
    const double median = gaps[gaps.size() / 2];
    std::size_t steady = 0;
    for (const double g : gaps)
    {
        steady += g >= 0.5 * median && g <= 1.5 * median;
    }
    if (steady * 3 < gaps.size() * 2)
    {
        return 1.0;
    }
    if (median * 1000.0 > 100000.0)
    {
        return 1.0;
    }
    static const double steps[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000 };
    double best = steps[0];
    for (const double step : steps)
    {
        if (std::fabs(std::log10(median * 1000.0 / step)) < std::fabs(std::log10(median * 1000.0 / best)))
        {
            best = step;
        }
    }
    return best;
}

/// The whole export by the rule above: every sample to the nearest multiple of its column's period on the clock, one row
/// per distinct time held in a map, the last sample of a column in a slot winning.
std::string referenceExport(const std::vector<CsvColumn>& columns, const CsvTimeBase& base)
{
    std::string out = kTimeHeader;
    for (const CsvColumn& c : columns)
    {
        out += "," + SeriesColumnSchema::columnHeader(c.metricType, c.name, c.sourceId);
    }
    out += "\r\n";
    std::map<std::int64_t, std::map<std::size_t, double>> rows;
    for (std::size_t i = 0; i < columns.size(); ++i)
    {
        const CsvColumn& c = columns[i];
        const std::size_t n = std::min(c.xValues.size(), c.yValues.size());
        const double period = referencePeriodMs(std::vector<double>(c.xValues.begin(), c.xValues.begin() + static_cast<std::ptrdiff_t>(n)));
        for (std::size_t j = 0; j < n; ++j)
        {
            const double slots = (base.baseTimeOffset + c.xValues[j]) * 1000.0 / period;
            const double whole = slots >= 0.0 ? std::floor(slots + 0.5 + 1e-4) : -std::floor(-slots + 0.5 + 1e-4);   // 1e-4: the tie tolerance
            rows[static_cast<std::int64_t>(whole * period)][i] = c.yValues[j];
        }
    }
    for (const auto& [ms, cells] : rows)
    {
        out += clocktime::formatMilliseconds(base.baseDay, ms);
        for (std::size_t i = 0; i < columns.size(); ++i)
        {
            out += ',';
            if (const auto cell = cells.find(i); cell != cells.end())
            {
                out += textformat::formatFixed(cell->second, 6);
            }
        }
        out += "\r\n";
    }
    return out;
}

}  // namespace

TEST_CASE("csvseries: the core writes every generated series as the plain reference of the grid rule does")
{
    constexpr int kCases = 500;
    Rng rng(20261023u);
    TempDir dir;
    const fs::path path = dir.file("export.csv");
    int written = 0, declined = 0, multi_source = 0, with_nonfinite = 0, with_empty_cells = 0, big = 0, coarser_grid = 0;
    for (int n = 0; n < kCases; ++n)
    {
        const std::vector<GeneratedSeries> all = generateSeries(rng);
        const int base_day = rng.below(400);
        const double base_offset = rng.chance(20) ? 0.0 : (rng.chance(20) ? 86399.999 : rng.below(86400 * 100) / 100.0);
        const int source_id = rng.chance(60) ? -1 : rng.below(4);

        const std::vector<CsvColumn> columns = columnsOf(all, source_id);
        std::error_code ec;
        fs::remove(path, ec);
        const CsvTimeBase base{ base_day, base_offset };
        const bool ok = CsvSeriesWriter::write(path, columns, base);
        const std::string bytes = readBytes(path);
        INFO("case " << n);
        CHECK(ok == !columns.empty());
        if (ok)
        {
            CHECK(escaped(bytes) == escaped(referenceExport(columns, base)));
        }

        (ok ? written : declined)++;
        multi_source += columns.size() > 1 && source_id < 0;
        with_nonfinite += contains(bytes, "nan") || contains(bytes, "inf");
        with_empty_cells += contains(bytes, ",,") || contains(bytes, ",\r\n");
        big += bytes.size() > 400;
        coarser_grid += std::any_of(columns.begin(), columns.end(), [](const CsvColumn& c) {
            return referencePeriodMs(std::vector<double>(c.xValues.begin(), c.xValues.end())) > 1.0;
        });
    }
    // What the 500 generated cases were, so the comparison above is known to have reached each kind.
    CHECK(written == 346);
    CHECK(declined == 154);
    CHECK(multi_source == 207);
    CHECK(with_nonfinite == 123);
    CHECK(with_empty_cells == 243);
    CHECK(big == 192);   // 193 before the tie tolerance: one case had a time near halfway, whose two rows now share one
    CHECK(coarser_grid == 165);   // cases with a column the grid moved (the rest stay to the millisecond)
}

TEST_CASE("csvseries: the clock stamp is what v2.14 wrote, for every generated time")
{
    constexpr int kCases = 8000;
    Rng rng(20261024u);
    Fnv f;
    int negative = 0, past_a_day = 0, with_ms = 0;
    for (int n = 0; n < kCases; ++n)
    {
        const int base_day = rng.chance(5) ? -rng.below(5) : rng.below(1000);
        const double base_offset = rng.chance(20) ? 0.0 : rng.below(86400 * 1000) / 1000.0;
        double elapsed;
        switch (rng.below(4))
        {
        case 0: elapsed = rng.below(2000000) / 1000.0; break;
        case 1: elapsed = -rng.below(200000) / 1000.0; break;
        case 2: elapsed = (rng.below(2000000) - 1000000) / 1024.0; break;
        default:
        {
            // Two draws, one statement each: in one expression C++ leaves their order to the compiler, and a Debug
            // build drew them the other way round from the Release build these answers were recorded with.
            const double whole = (double(rng.below(1000000)) - 500000.0) * 0.001;
            elapsed = whole + rng.below(1000) * 1e-7;
            break;
        }
        }
        for (const bool ms : { false, true })
        {
            f.bytes(clocktime::format(base_day, base_offset, elapsed, ms));
            f.byte(0x1F);
        }
        negative += elapsed < 0;
        past_a_day += (base_offset + elapsed) >= 86400.0 || (base_offset + elapsed) < 0.0;
        with_ms += (elapsed - std::floor(elapsed)) != 0.0;
    }
    CHECK(f.h == 17922469416739586873ULL);
    CHECK(negative == 4154);
    CHECK(past_a_day == 891);
    CHECK(with_ms == 7994);
}

TEST_CASE("csvseries: the numbers a cell or a stamp field may hold read as v2.14 read them")
{
    // The importer reads every cell and every timestamp field with textparse. A text v2.14.2 took must read to the
    // same bits, and one it refused must be refused: a spreadsheet's "Infinity" or "NaN(1)" was not a number to it,
    // and std::from_chars would say it is. The answer v2.14.2 gave for each text (accepted or not, and the double and
    // the int it read) is recorded as one digest.
    Strings texts = {
        "", " ", "+", "-", "nan", "NaN", "NAN", "-nan", "+nan", "inf", "Inf", "INF", "+inf", "-inf", "-INF",
        "infinity", "Infinity", "-Infinity", "+infinity", "nan(1)", "NaN(abc)", "nan()", "infin", "in", "na", "n", "i",
        "1e5", "1E5", "1e", "1e+", "1e-", "e5", ".", "5.", ".5", "-.5", "+.5", "-.", "+.", "0x10", "0X1p3", "0b101",
        "1_0", "1 0", "1,5", "1.5.5", "--5", "+-5", "-+5", "++5", "1e400", "-1e400", "1e-400", "4.9e-324",
        "2.2250738585072011e-308", "1.7976931348623157e308", "1.7976931348623159e308", "00005", "5e0005", "5e+05",
        " 5", "5 ", " 5 ", "\t5\n", "5\x0b", "5\x0c", "5\r", "\n5", "5f", "5d", "5l", "1e1e1", "0", "-0", "+0", "0.0", "-0.0",
        "123456789012345678901234567890", "0.1", "0.30000000000000004", "1e22", "1e23", "9007199254740993", "100", "007",
        "2147483647", "2147483648", "-2147483648", "-2147483649", "+2147483647", "1.", "1.e5", ".e5", "E5",
    };
    // Plus a few thousand strings over the alphabet where the cases above live.
    const std::string alphabet = "0123456789+-.eE  infaINFNAx(),_\t";
    std::uint32_t state = 20261022u;
    auto next = [&state]() { state = state * 1664525u + 1013904223u; return state >> 8; };
    for (int i = 0; i < 20000; ++i)
    {
        std::string t;
        const int len = int(next() % 9);
        for (int k = 0; k < len; ++k)
        {
            t += alphabet[next() % std::uint32_t(alphabet.size())];
        }
        texts.push_back(t);
    }

    Fnv f;
    int accepted = 0, refused = 0, int_ok = 0;
    std::size_t untouched = 0;
    for (const std::string& text : texts)
    {
        double value = 12345.5;   // a refused text must leave this alone
        const bool ok = textparse::parseDouble(text, value);
        untouched += ok || value == 12345.5;
        f.byte(ok ? 1 : 0);
        if (ok)
        {
            if (std::isnan(value))
            {
                f.u64(~0ULL);
            }
            else
            {
                std::uint64_t b = 0;
                std::memcpy(&b, &value, sizeof b);
                f.u64(b);
            }
        }
        int integer = 777;
        const bool iok = textparse::parseInteger(text, integer);
        untouched += iok || integer == 777;
        f.byte(iok ? 1 : 0);
        if (iok)
        {
            f.u32(static_cast<std::uint32_t>(integer));
        }
        (ok ? accepted : refused)++;
        int_ok += iok;
    }
    CHECK(texts.size() == 20095);
    CHECK(f.h == 13245540123430937018ULL);
    CHECK(accepted == 1487);
    CHECK(refused == 18608);
    CHECK(int_ok == 1331);
    // Both readers leave their output alone when they refuse (the double and the int of every text).
    CHECK(untouched == 2 * texts.size());
}

TEST_CASE("csvseries: a UTF-16 file with CR LF line ends loads")
{
    // A spreadsheet's "Unicode text" is UTF-16 with CR LF: the core decodes before it looks for line ends.
    TempDir dir;
    const CsvParseResult r =
        parseBytes(dir, utf16le(kTimeHeader + ",A Lock (%)\r\n1:00:00:00.000,50\r\n1:00:00:01.000,60\r\n"));
    REQUIRE(r.success);
    REQUIRE(r.series.size() == 1);
    REQUIRE(r.series[0].xValues.size() == 2);
    CHECK(r.series[0].yValues[1] == 60.0);
    CHECK(importDigest(r) == "ok skipped=0 base=1/0 xmax=1 n=1 | [0] A label=A L order=1 rcv=0 ch=0 src=0 : 0,50 1,60");
}

// ---------------------------------------------------------------------------
// The writer's file handling
// ---------------------------------------------------------------------------

TEST_CASE("csvseries: a column whose times and values differ in number writes the shorter of the two")
{
    // A plot's columns are views of one series, so their two arrays have one length. A host may hand the
    // writer any two arrays; the writer uses as many samples as both have.
    TempDir dir;
    const fs::path path = dir.file("uneven.csv");
    const CsvTimeBase base{ 4, 25689.0 };   // 07:08:09 on day 4, as the export cases above read
    const std::string header = "Time (DOY:HH:MM:SS.mmm),A Lock (%)\r\n";

    const std::vector<double> long_x = { 0, 1, 2, 3 }, short_y = { 10, 20 };
    REQUIRE(CsvSeriesWriter::write(path, std::vector<CsvColumn>{ { MetricType::FrameSyncLock, "A", 0, long_x, short_y } }, base));
    CHECK(readBytes(path) == header + "004:07:08:09.000,10.000000\r\n004:07:08:10.000,20.000000\r\n");

    const std::vector<double> short_x = { 0, 1 }, long_y = { 1, 2, 3, 4 };
    REQUIRE(CsvSeriesWriter::write(path, std::vector<CsvColumn>{ { MetricType::FrameSyncLock, "A", 0, short_x, long_y } }, base));
    CHECK(readBytes(path) == header + "004:07:08:09.000,1.000000\r\n004:07:08:10.000,2.000000\r\n");

    // A column with no samples still has its header, and with no sample anywhere that is all the file holds.
    const std::vector<double> none;
    REQUIRE(CsvSeriesWriter::write(path, std::vector<CsvColumn>{ { MetricType::FrameSyncLock, "A", 0, none, none } }, base));
    CHECK(readBytes(path) == header);
}

TEST_CASE("csvseries: a large export crosses the flush threshold intact")
{
    // The writer hands its buffer to the file whenever it passes a mebibyte; every other test writes a few hundred bytes,
    // so a flush that lost or repeated a chunk would pass them all. 30,000 rows of three columns is about 1.5 MB, and
    // its digest is the one v2.14.2's exported bytes had.
    constexpr int kRows = 30000;
    std::vector<GeneratedSeries> all(3);
    for (int c = 0; c < 3; ++c)
    {
        all[static_cast<std::size_t>(c)].name = "Ch " + std::to_string(c);
        all[static_cast<std::size_t>(c)].metricType = MetricType::FrameSyncLock;
        for (int r = 0; r < kRows; ++r)
        {
            all[static_cast<std::size_t>(c)].xValues.push_back(r * 0.01);
            all[static_cast<std::size_t>(c)].yValues.push_back(50.0 + (r % 997) * 0.0625 + c);
        }
    }
    TempDir dir;
    const fs::path path = dir.file("big.csv");
    REQUIRE(CsvSeriesWriter::write(path, columnsOf(all), CsvTimeBase{ 3, 1234.5 }));
    const std::string bytes = readBytes(path);
    CHECK(bytes.size() > (1u << 20) + 100000u);   // past the threshold
    CHECK(bytes.size() == 1459237u);
    Fnv f;
    f.bytes(bytes);
    CHECK(f.h == 5120933182832587767ULL);
}

TEST_CASE("csvseries: an export replaces the file that was there")
{
    TempDir dir;
    const fs::path path = dir.file("replace.csv");
    const std::vector<double> longer_x = { 0, 1, 2, 3 }, longer_y = { 1, 2, 3, 4 }, shorter_x = { 0 }, shorter_y = { 9 };
    REQUIRE(CsvSeriesWriter::write(path, std::vector<CsvColumn>{ { MetricType::FrameSyncLock, "A", 0, longer_x, longer_y } },
                                   CsvTimeBase{ 3, 25689.0 }));
    REQUIRE(CsvSeriesWriter::write(path, std::vector<CsvColumn>{ { MetricType::FrameSyncLock, "B", 0, shorter_x, shorter_y } },
                                   CsvTimeBase{ 3, 25689.0 }));
    const std::string replaced = readBytes(path);
    CHECK(contains(replaced, "B Lock"));
    CHECK_FALSE(contains(replaced, "A Lock"));
    CHECK(std::count(replaced.begin(), replaced.end(), '\n') == 2);   // the header and the one row, nothing left of the longer file
}

TEST_CASE("csvseries: a CSV in a folder with a non-ASCII name is written and read back")
{
    // A folder whose name no Western code page spells.
    TempDir dir;
    const fs::path folder = dir.path() / fs::path(utf8(u8"café-Ω_tmdq_csv"));
    fs::create_directories(folder);
    const fs::path path = folder / fs::path(utf8(u8"résumé.csv"));
    const std::vector<double> x = { 0, 1 }, y = { 1, 2 };
    REQUIRE(CsvSeriesWriter::write(path, std::vector<CsvColumn>{ { MetricType::FrameSyncLock, "A", 0, x, y } },
                                   CsvTimeBase{ 3, 25689.0 }));
    CHECK(fs::exists(path));
    const CsvParseResult r = CsvSeriesParser::parse(path);
    REQUIRE(r.success);
    CHECK(r.series.size() == 1);
}

TEST_CASE("csvseries: writing nothing writes no file, and an unwritable path fails")
{
    TempDir dir;
    const std::vector<double> x = { 0, 1 }, y = { 1, 2 };
    const std::vector<CsvColumn> one = { { MetricType::FrameSyncLock, "A", 0, x, y } };

    // Declined before the file is opened: no columns leaves no file.
    const fs::path none = dir.file("nothing.csv");
    CHECK_FALSE(CsvSeriesWriter::write(none, {}, CsvTimeBase{ 3, 25689.0 }));
    CHECK_FALSE(fs::exists(none));

    CHECK_FALSE(CsvSeriesWriter::write(dir.file("no_such_folder_for_tmdq") / "out.csv", one, CsvTimeBase{ 3, 25689.0 }));
    CHECK_FALSE(fs::exists(dir.file("no_such_folder_for_tmdq")));
}

TEST_CASE("csvseries: export, import and export again is byte for byte the same")
{
    // The format's two directions are inverses: what the writer writes the parser reads back into series that write the
    // same bytes. Receiver SNR columns, two sources and a sparse column are all in it.
    const std::string canonical =
        kTimeHeader + ",Ch 5 Lock (%),Ch 5 Accumulated Missed Frames,40 - Ch 5 L_RCVR1,40 - Ch 5 R_RCVR1,"
                      "S2| Ch 5 Lock (%),S2| 40 - Ch 5 L_RCVR2\r\n"
                      "003:07:08:09.000,90.000000,0.000000,-80.500000,-70.250000,,\r\n"
                      "003:07:08:10.000,91.000000,1.000000,,-71.000000,80.000000,-60.125000\r\n"
                      "003:07:08:11.500,,,-79.500000,,81.000000,\r\n";
    TempDir dir;
    const CsvParseResult r = parseBytes(dir, canonical);
    REQUIRE(r.success);
    std::vector<CsvColumn> columns;
    for (const CsvSeries& s : r.series)
    {
        columns.push_back({ s.metricType, s.name, s.sourceId, std::span<const double>(s.xValues), std::span<const double>(s.yValues) });
    }
    const fs::path path = dir.file("again.csv");
    REQUIRE(CsvSeriesWriter::write(path, columns, CsvTimeBase{ r.baseDay, r.baseTimeOffset }));
    CHECK(escaped(readBytes(path)) == escaped(canonical));
}

// ---------------------------------------------------------------------------
// The time grid: every column on the clock rounded to its own sample rate
// ---------------------------------------------------------------------------

TEST_CASE("csvseries: channels whose windows end a few milliseconds apart share one row per second")
{
    // One recording, three channels each reporting once a second, their windows ending at .946, .961 and .976 of each
    // second (the first sample is not on a second, as a recording's never is). Each channel's samples used to take a row
    // of their own, three rows a second with two of the three cells empty; on the grid of their rate they share one.
    const std::vector<double> a = { kBase + 0.946, kBase + 1.946, kBase + 2.946 };
    const std::vector<double> b = { kBase + 0.961, kBase + 1.961, kBase + 2.961 };
    const std::vector<double> c = { kBase + 0.976, kBase + 1.976, kBase + 2.976 };
    TempDir dir;
    const Export e = exportOf(dir, { lockStream("A", 1, a, { 1, 4, 7 }, { 0, 0, 0 }), lockStream("B", 2, b, { 2, 5, 8 }, { 0, 0, 1 }),
                                     lockStream("C", 3, c, { 3, 6, 9 }, { 0, 1, 2 }) }, -1);
    REQUIRE(e.wrote);
    CHECK(escaped(e.bytes) ==
          "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames,B Lock (%),B Accumulated Missed Frames,"
          "C Lock (%),C Accumulated Missed Frames\\r\\n"
          "004:07:08:10.000,1.000000,0.000000,2.000000,0.000000,3.000000,0.000000\\r\\n"
          "004:07:08:11.000,4.000000,0.000000,5.000000,0.000000,6.000000,1.000000\\r\\n"
          "004:07:08:12.000,7.000000,0.000000,8.000000,1.000000,9.000000,2.000000\\r\\n");
}

TEST_CASE("csvseries: a column whose samples sit exactly half a period off the grid loses none")
{
    // A recording that starts half a period past a grid line (a 100 ms column whose windows end at .050, .150, ...) has times that
    // are halfway between two slots, made by adding the period over and over, so each is halfway only to within the noise of
    // the sums. Rounded on their own they fell to either side of halfway by chance: two samples in one slot, the second winning,
    // and none in the next. Seven rates, 40 starts each, 600 samples: every sample has a row, and the rows are in order.
    std::uint64_t seed = 12345;
    const auto next = [&seed] {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(seed >> 11) / 9007199254740992.0;
    };
    for (const double period : { 1.0, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01 })
    {
        INFO("period " << period);
        for (int trial = 0; trial < 40; ++trial)
        {
            const double start = std::floor(next() * 80000.0) + 0.5 * period;   // whole seconds into the day, then half a period
            std::vector<double> times;
            std::vector<double> values;
            double x = start;
            for (int k = 0; k < 600; ++k)
            {
                times.push_back(x);
                values.push_back(k);
                x += period;
            }
            TempDir dir;
            const Export e = exportOf(dir, { lockStream("A", 1, times, values, {}) }, -1);
            REQUIRE(e.wrote);
            const auto rows = static_cast<long>(std::count(e.bytes.begin(), e.bytes.end(), static_cast<char>(10))) - 1;
            CHECK(rows == 600);
        }
    }
}

TEST_CASE("csvseries: a time a little off halfway rounds to the nearer slot, and only one at halfway goes up")
{
    // The tie tolerance (1e-4 of a period) must not widen into rounding that is wrong: on a 100 ms grid 49.9 ms is nearer the
    // slot before and 50.1 ms the one after, and exactly 50 ms goes up, as halfway always did.
    TempDir dir;
    const auto rows = [&](double offset) {
        return escaped(exportOf(dir, { lockStream("A", 1, { kBase + offset, kBase + offset + 0.1, kBase + offset + 0.2 }, { 1, 2, 3 }, {}) }, -1).bytes);
    };
    CHECK(rows(0.0499) ==
          "Time (DOY:HH:MM:SS.mmm),A Lock (%)\\r\\n"
          "004:07:08:09.000,1.000000\\r\\n"
          "004:07:08:09.100,2.000000\\r\\n"
          "004:07:08:09.200,3.000000\\r\\n");
    CHECK(rows(0.0501) ==
          "Time (DOY:HH:MM:SS.mmm),A Lock (%)\\r\\n"
          "004:07:08:09.100,1.000000\\r\\n"
          "004:07:08:09.200,2.000000\\r\\n"
          "004:07:08:09.300,3.000000\\r\\n");
    CHECK(rows(0.05) == rows(0.0501));
}

TEST_CASE("csvseries: a 100 ms column and a 10 ms column are written on tenths and on hundredths of a second")
{
    TempDir dir;
    // Tenths, each time a few milliseconds off its tenth, and a second channel a few more off.
    const Export tenths = exportOf(
        dir, { lockStream("A", 1, { kBase, kBase + 0.103, kBase + 0.198, kBase + 0.304, kBase + 0.399 }, { 1, 2, 3, 4, 5 }, {}),
               lockStream("B", 2, { kBase + 0.012, kBase + 0.111, kBase + 0.213, kBase + 0.309, kBase + 0.412 }, { 6, 7, 8, 9, 10 }, {}) }, -1);
    REQUIRE(tenths.wrote);
    CHECK(escaped(tenths.bytes) ==
          "Time (DOY:HH:MM:SS.mmm),A Lock (%),B Lock (%)\\r\\n"
          "004:07:08:09.000,1.000000,6.000000\\r\\n"
          "004:07:08:09.100,2.000000,7.000000\\r\\n"
          "004:07:08:09.200,3.000000,8.000000\\r\\n"
          "004:07:08:09.300,4.000000,9.000000\\r\\n"
          "004:07:08:09.400,5.000000,10.000000\\r\\n");

    const Export hundredths = exportOf(
        dir, { lockStream("A", 1, { kBase, kBase + 0.0104, kBase + 0.0197, kBase + 0.0303, kBase + 0.0398 }, { 1, 2, 3, 4, 5 }, {}) }, -1);
    REQUIRE(hundredths.wrote);
    CHECK(escaped(hundredths.bytes) ==
          "Time (DOY:HH:MM:SS.mmm),A Lock (%)\\r\\n"
          "004:07:08:09.000,1.000000\\r\\n"
          "004:07:08:09.010,2.000000\\r\\n"
          "004:07:08:09.020,3.000000\\r\\n"
          "004:07:08:09.030,4.000000\\r\\n"
          "004:07:08:09.040,5.000000\\r\\n");
}

TEST_CASE("csvseries: columns of different rates share a row only where their grids meet")
{
    // A reports once a second and B ten times a second: B has a row each tenth, A fills the whole seconds.
    std::vector<double> a = { kBase + 0.003, kBase + 1.002, kBase + 2.001 };
    std::vector<double> b, bLock;
    for (int i = 0; i <= 10; ++i)
    {
        b.push_back(kBase + 0.004 + i * 0.1);
        bLock.push_back(100 + i);
    }
    TempDir dir;
    const Export e = exportOf(dir, { lockStream("A", 1, a, { 1, 2, 3 }, {}), lockStream("B", 2, b, bLock, {}) }, -1);
    REQUIRE(e.wrote);
    std::string expected = "Time (DOY:HH:MM:SS.mmm),A Lock (%),B Lock (%)\\r\\n";
    for (int i = 0; i <= 10; ++i)
    {
        expected += std::format("004:07:08:{:02}.{:03},{},{}.000000\\r\\n", 9 + i / 10, i % 10 * 100,
                                i == 0 ? "1.000000" : (i == 10 ? "2.000000" : ""), 100 + i);
    }
    expected += "004:07:08:11.000,3.000000,\\r\\n";
    CHECK(escaped(e.bytes) == expected);
}

TEST_CASE("csvseries: a column with no steady rate keeps its times to the millisecond")
{
    // Gaps of .3, 1.4, .05 and 3.25 s have no rate to read; moving them to a grid would invent one.
    TempDir dir;
    const Export e = exportOf(dir, { lockStream("A", 1, { kBase, kBase + 0.3, kBase + 1.7, kBase + 1.75, kBase + 5.0 }, { 1, 2, 3, 4, 5 }, {}) }, -1);
    REQUIRE(e.wrote);
    CHECK(escaped(e.bytes) ==
          "Time (DOY:HH:MM:SS.mmm),A Lock (%)\\r\\n"
          "004:07:08:09.000,1.000000\\r\\n"
          "004:07:08:09.300,2.000000\\r\\n"
          "004:07:08:10.700,3.000000\\r\\n"
          "004:07:08:10.750,4.000000\\r\\n"
          "004:07:08:14.000,5.000000\\r\\n");
}

TEST_CASE("csvseries: a column slower than the longest grid keeps its times to the millisecond")
{
    // A sample every ten minutes is past the 100 s grid, and rounding it to that would move a time by up to 50 s; the
    // same column every 100 s is on the grid, whose multiples of the clock it takes.
    TempDir dir;
    const Export slow = exportOf(dir, { lockStream("A", 1, { kBase + 1.25, kBase + 601.25, kBase + 1201.25 }, { 1, 2, 3 }, {}) }, -1);
    REQUIRE(slow.wrote);
    CHECK(escaped(slow.bytes) ==
          "Time (DOY:HH:MM:SS.mmm),A Lock (%)\\r\\n"
          "004:07:08:10.250,1.000000\\r\\n"
          "004:07:18:10.250,2.000000\\r\\n"
          "004:07:28:10.250,3.000000\\r\\n");

    const Export edge = exportOf(dir, { lockStream("A", 1, { kBase + 1.25, kBase + 101.25, kBase + 201.25 }, { 1, 2, 3 }, {}) }, -1);
    REQUIRE(edge.wrote);
    CHECK(escaped(edge.bytes) ==
          "Time (DOY:HH:MM:SS.mmm),A Lock (%)\\r\\n"
          "004:07:08:20.000,1.000000\\r\\n"
          "004:07:10:00.000,2.000000\\r\\n"
          "004:07:11:40.000,3.000000\\r\\n");
}

TEST_CASE("csvseries: an export read back and exported again is the same bytes, however the first was gridded")
{
    // The grid is where a sample sits afterwards, so a second export finds each column's times already on it.
    TempDir dir;
    const std::vector<double> a = { kBase + 0.946, kBase + 1.946, kBase + 2.946, kBase + 3.946 };
    const std::vector<double> b = { kBase + 0.976, kBase + 1.976, kBase + 2.976, kBase + 3.976 };
    const std::vector<double> c = { kBase + 1.0, kBase + 1.103, kBase + 1.198, kBase + 1.304 };
    const Export first = exportOf(dir, { lockStream("A", 1, a, { 1, 2, 3, 4 }, { 0, 0, 1, 1 }), lockStream("B", 2, b, { 5, 6, 7, 8 }, { 0, 1, 1, 2 }),
                                         lockStream("C", 3, c, { 9, 10, 11, 12 }, {}) }, -1);
    REQUIRE(first.wrote);
    const CsvParseResult r = parseBytes(dir, first.bytes);
    REQUIRE(r.success);
    std::vector<CsvColumn> columns;
    for (const CsvSeries& s : r.series)
    {
        columns.push_back({ s.metricType, s.name, s.sourceId, std::span<const double>(s.xValues), std::span<const double>(s.yValues) });
    }
    const fs::path path = dir.file("regridded.csv");
    REQUIRE(CsvSeriesWriter::write(path, columns, CsvTimeBase{ r.baseDay, r.baseTimeOffset }));
    CHECK(escaped(readBytes(path)) == escaped(first.bytes));
}

TEST_CASE("csvseries: the integer clock stamp reads as the floating-point one")
{
    for (const std::int64_t ms : { std::int64_t{ 0 }, std::int64_t{ 1 }, std::int64_t{ 999 }, std::int64_t{ 1000 }, std::int64_t{ 86399999 },
                                   std::int64_t{ 86400000 }, std::int64_t{ 172800001 }, std::int64_t{ 31536000000 }, std::int64_t{ -1 },
                                   std::int64_t{ -1000 }, std::int64_t{ -86400000 }, std::int64_t{ -86400001 } })
    {
        INFO("ms " << ms);
        CHECK(clocktime::formatMilliseconds(5, ms) == clocktime::format(5, 0.0, static_cast<double>(ms) / 1000.0, true));
    }
}

TEST_CASE("csvseries: days and hours far apart are counted wide, not wrapped")
{
    // The stamp's day, hour and minute are file-sourced ints, and their seconds were counted in int: 24 856 days apart,
    // or 596 524 hours, passed INT_MAX and wrapped. Which rows load is unchanged; only the time no longer wraps.
    TempDir dir;
    const CsvParseResult days = parseBytes(dir, kTimeHeader + ",A Lock (%)\n1:00:00:00.000,1\n100000:00:00:00.000,2\n");
    REQUIRE(days.success);
    REQUIRE(days.series.size() == 1);
    CHECK(days.series[0].xValues == std::vector<double>{0.0, 99999.0 * 86400.0});
    CHECK(days.xMax == 99999.0 * 86400.0);

    const CsvParseResult hours = parseBytes(dir, kTimeHeader + ",A Lock (%)\n0:00:00:00.000,1\n0:1000000:30:00.000,2\n");
    REQUIRE(hours.success);
    REQUIRE(hours.series.size() == 1);
    CHECK(hours.series[0].xValues == std::vector<double>{0.0, 1000000.0 * 3600.0 + 1800.0});
}

TEST_CASE("csvseries: a wide file with little data reserves no more than its data needs")
{
    // Every column reserves the estimated row count up front. With a floor of 100 rows, 20 000 columns and one row
    // reserved two million samples' room; the estimate from the file's size is all each column gets now.
    constexpr int kColumns = 20000;
    std::string bytes = kTimeHeader;
    for (int i = 0; i < kColumns; ++i)
    {
        bytes += std::format(",C{} Lock (%)", i);
    }
    bytes += "\n1:00:00:00.000,5" + std::string(kColumns - 1, ',') + "\n";
    TempDir dir;
    const CsvParseResult r = parseBytes(dir, bytes);
    REQUIRE(r.success);
    REQUIRE(r.series.size() == static_cast<std::size_t>(kColumns));
    CHECK(r.series[0].xValues == std::vector<double>{0.0});
    std::size_t reserved = 0;
    for (const CsvSeries& s : r.series)
    {
        reserved += s.xValues.capacity() + s.yValues.capacity();
    }
    CHECK_MESSAGE(reserved < static_cast<std::size_t>(kColumns), reserved << " samples reserved");
}