// SeriesColumnSchema: the CSV column-header format and its inverse parser, pinned as a round trip so export and import
// cannot drift. No Qt; ported from the Qt TestSeriesColumnSchema, whose cases are all here under the same descriptions.
//
// The Qt suite compared the parser with the QString implementation it replaced, over 40,000 generated headers. The
// doctest binary has Qt in neither the code nor the test, so what that implementation answered was recorded once with a
// throwaway diagnostic in the Qt suite (removed again): one FNV-1a digest over its answer for every generated header
// (the header's metric, name, label, order, receiver and source), and the counters the reach check reads. The
// generator below is the Qt suite's, token for token, driven by the same mt19937 seed, so it produces the same
// headers. The formatters have no parsing in them, so for those a second way of writing the grammar stands in.

#include <cstdint>
#include <format>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "doctest.h"
#include "seriescolumnschema.h"

namespace {

using SeriesColumnSchema::ParsedColumn;

ParsedColumn parseHeader(const std::string& header)
{
    return SeriesColumnSchema::parseColumnHeader(header);
}

std::string formatHeader(MetricType type, const std::string& name, int sourceId)
{
    return SeriesColumnSchema::columnHeader(type, name, sourceId);
}

std::string formatSnrName(int id, const std::string& label, const std::string& channel)
{
    return SeriesColumnSchema::snrSeriesName(id, label, channel);
}

std::string describe(const ParsedColumn& p)
{
    return std::format("metric={} name='{}' label='{}' order={} rcvr={} source={}", static_cast<int>(p.metricType),
                       p.name, p.streamLabel, p.streamOrder, p.receiverIndex, p.sourceId);
}

struct ParseRow
{
    const char* tag;
    std::string header;
    int         metric;
    std::string name;
    std::string streamLabel;
    int         streamOrder;
    int         receiverIndex;
    int         sourceId;
};

struct FormatRow
{
    const char* tag;
    MetricType  metric;
    std::string name;
    int         sourceId;
    std::string header;
};

} // namespace

TEST_CASE("SeriesColumnSchema: the SNR series name format")
{
    CHECK(formatSnrName(40, "RCVR Data", "L_RCVR1") == "40 - RCVR Data L_RCVR1");
}

TEST_CASE("SeriesColumnSchema: the column header adds a metric suffix")
{
    // Left-axis metrics get a suffix so a stream's lock/missed columns don't collide.
    CHECK(formatHeader(MetricType::FrameSyncLock, "Ch 05", 0) == std::string("Ch 05") + SeriesColumnSchema::kLockSuffix);
    CHECK(formatHeader(MetricType::AccumulatedMissedFrames, "Ch 05", 0) ==
          std::string("Ch 05") + SeriesColumnSchema::kMissedFramesSuffix);
}

TEST_CASE("SeriesColumnSchema: the suffixes and the time header are what files already carry")
{
    // These strings are a file format: an exported CSV from any earlier release must still import.
    CHECK(std::string(SeriesColumnSchema::kTimeHeader) == "Time (DOY:HH:MM:SS.mmm)");
    CHECK(std::string(SeriesColumnSchema::kLockSuffix) == " Lock (%)");
    CHECK(std::string(SeriesColumnSchema::kMissedFramesSuffix) == " Accumulated Missed Frames");
}

TEST_CASE("SeriesColumnSchema: an SNR column header passes through")
{
    // SNR names already carry the channel id and are unique, so they are passed through as they are.
    const std::string name = "40 - RCVR Data L_RCVR1";
    CHECK(formatHeader(MetricType::SNR, name, 0) == name);
}

TEST_CASE("SeriesColumnSchema: a lock header is read back")
{
    const auto col = parseHeader(std::string("Ch 05") + SeriesColumnSchema::kLockSuffix);
    CHECK(col.metricType == MetricType::FrameSyncLock);
    CHECK(col.name == "Ch 05");
    CHECK(col.streamLabel == "Ch 05");   // re-links the lock/missed pair
    CHECK(col.receiverIndex == 0);
}

TEST_CASE("SeriesColumnSchema: a missed-frames header is read back")
{
    const auto col = parseHeader(std::string("Ch 05") + SeriesColumnSchema::kMissedFramesSuffix);
    CHECK(col.metricType == MetricType::AccumulatedMissedFrames);
    CHECK(col.name == "Ch 05");
    CHECK(col.streamLabel == "Ch 05");
}

TEST_CASE("SeriesColumnSchema: an SNR header is read back")
{
    const auto col = parseHeader("40 - RCVR Data L_RCVR3");
    CHECK(col.metricType == MetricType::SNR);
    CHECK(col.streamOrder == 40);
    CHECK(col.streamLabel == "RCVR Data");
    CHECK(col.receiverIndex == 3);
    CHECK(col.name == "40 - RCVR Data L_RCVR3");
}

TEST_CASE("SeriesColumnSchema: an SNR label with spaces splits at the last space")
{
    // A multi-word stream label must split at the LAST space (before the channel name), not the first - this was the
    // crux of an earlier false-positive review.
    const auto col = parseHeader("7 - Band A 10Mbps R_RCVR2");
    CHECK(col.metricType == MetricType::SNR);
    CHECK(col.streamOrder == 7);
    CHECK(col.streamLabel == "Band A 10Mbps");
    CHECK(col.receiverIndex == 2);
}

TEST_CASE("SeriesColumnSchema: the SNR shape wins over a metric suffix")
{
    // A header that both looks like SNR ("<digits> - ") and ends with a metric suffix must resolve as SNR, not be
    // misrouted to Lock/MissedFrames.
    const auto col = parseHeader(std::string("12 - Weird L_RCVR1") + SeriesColumnSchema::kLockSuffix);
    CHECK(col.metricType == MetricType::SNR);
    CHECK(col.streamOrder == 12);
}

TEST_CASE("SeriesColumnSchema: an unknown header falls back to SNR")
{
    // No metric suffix and no SNR id-prefix: SNR with a bare name (the defensive default).
    const auto col = parseHeader("PlainName");
    CHECK(col.metricType == MetricType::SNR);
    CHECK(col.name == "PlainName");
    CHECK(col.streamOrder == 0);
}

TEST_CASE("SeriesColumnSchema: lock, missed and SNR round-trip")
{
    // columnHeader() -> parseColumnHeader() recovers the identity for every metric.
    struct In { MetricType type; std::string name; };
    const std::vector<In> inputs = {
        { MetricType::FrameSyncLock, "Ch 12" },
        { MetricType::AccumulatedMissedFrames, "Ch 12" },
        { MetricType::SNR, formatSnrName(40, "RCVR Data", "C_RCVR2") },
    };
    for (const In& in : inputs)
    {
        const auto col = parseHeader(formatHeader(in.type, in.name, 0));
        CHECK(col.metricType == in.type);
        CHECK(col.name == in.name);
    }

    // The SNR round trip also recovers stream order / label / receiver.
    const auto snr = parseHeader(formatHeader(inputs[2].type, inputs[2].name, 0));
    CHECK(snr.streamOrder == 40);
    CHECK(snr.streamLabel == "RCVR Data");
    CHECK(snr.receiverIndex == 2);
}

TEST_CASE("SeriesColumnSchema: source 0 is unqualified")
{
    // Source 0 (the first/only source) must produce EXACTLY the pre-multi-file format - a single-source export stays
    // byte-identical across releases.
    const std::string name = "40 - RCVR Data L_RCVR1";
    CHECK(formatHeader(MetricType::SNR, name, 0) == name);
    CHECK(formatHeader(MetricType::FrameSyncLock, "Ch 05", 0) == std::string("Ch 05") + SeriesColumnSchema::kLockSuffix);
}

TEST_CASE("SeriesColumnSchema: a non-zero source adds a qualifier")
{
    const std::string name = "40 - RCVR Data L_RCVR1";
    CHECK(formatHeader(MetricType::SNR, name, 1) == "S1| " + name);
    CHECK(formatHeader(MetricType::FrameSyncLock, "Ch 05", 2) ==
          std::string("S2| Ch 05") + SeriesColumnSchema::kLockSuffix);
}

TEST_CASE("SeriesColumnSchema: a source-qualified SNR header is read back")
{
    const auto col = parseHeader("S1| 40 - RCVR Data L_RCVR1");
    CHECK(col.metricType == MetricType::SNR);
    CHECK(col.sourceId == 1);
    CHECK(col.streamOrder == 40);
    CHECK(col.streamLabel == "RCVR Data");
    CHECK(col.receiverIndex == 1);
    // The recovered name matches what source 0 would produce - the qualifier is metadata, not part of the series' own
    // name.
    CHECK(col.name == "40 - RCVR Data L_RCVR1");
}

TEST_CASE("SeriesColumnSchema: a source-qualified lock header is read back")
{
    const auto col = parseHeader(std::string("S2| Ch 05") + SeriesColumnSchema::kLockSuffix);
    CHECK(col.metricType == MetricType::FrameSyncLock);
    CHECK(col.sourceId == 2);
    CHECK(col.name == "Ch 05");
    CHECK(col.streamLabel == "Ch 05");
}

TEST_CASE("SeriesColumnSchema: an unqualified header is source 0")
{
    CHECK(parseHeader("40 - RCVR Data L_RCVR1").sourceId == 0);
    CHECK(parseHeader(std::string("Ch 05") + SeriesColumnSchema::kLockSuffix).sourceId == 0);
}

TEST_CASE("SeriesColumnSchema: a header starting with the letter S that is not a qualifier is unaffected")
{
    // "Something" starts with 'S' but has no digit-then-"| " qualifier shape - must NOT be misparsed as a
    // source-qualified header.
    const auto col = parseHeader("Something");
    CHECK(col.sourceId == 0);
    CHECK(col.name == "Something");

    // "S7Percent": digits follow 'S' but aren't followed by "| ", so this must also be left alone (falls back to the
    // SNR/unknown default, not stripped).
    const auto col2 = parseHeader("S7Percent");
    CHECK(col2.sourceId == 0);
    CHECK(col2.name == "S7Percent");
}

TEST_CASE("SeriesColumnSchema: the source id round-trips")
{
    struct In { MetricType type; std::string name; int source; };
    const std::vector<In> inputs = {
        { MetricType::FrameSyncLock, "Ch 12", 3 },
        { MetricType::AccumulatedMissedFrames, "Ch 12", 3 },
        { MetricType::SNR, formatSnrName(40, "RCVR Data", "C_RCVR2"), 5 },
    };
    for (const In& in : inputs)
    {
        const auto col = parseHeader(formatHeader(in.type, in.name, in.source));
        CHECK(col.metricType == in.type);
        CHECK(col.name == in.name);        // the qualifier never leaks into the name
        CHECK(col.sourceId == in.source);
    }
}

// ---------------------------------------------------------------------------------------------
// Edge cases. Written against the QString implementation before the schema moved into the Qt-free core, so they record
// what a header resolves to today rather than what the std::string rewrite happens to do.
// ---------------------------------------------------------------------------------------------

TEST_CASE("SeriesColumnSchema: parse edge cases keep their recorded behaviour")
{
    const std::vector<ParseRow> rows = {
        // --- empty and degenerate ---------------------------------------------------------------
        { "empty",                      "",                 0, "",             "",     0, 0, 0 },
        { "suffix-only-lock",           " Lock (%)",        1, "",             "",     0, 0, 0 },
        { "lock-without-leading-space", "Lock (%)",         0, "Lock (%)",     "",     0, 0, 0 },
        { "suffix-then-trailing-space", "Ch Lock (%) ",     0, "Ch Lock (%) ", "",     0, 0, 0 },

        // --- the SNR "<id> - <label> <channel>" shape --------------------------------------------
        { "label-without-space",        "7 - Solo",         0, "7 - Solo",     "Solo", 7, 0, 0 },
        { "empty-label",                "7 - ",             0, "7 - ",         "",     7, 0, 0 },
        { "leading-zeros",              "007 - A B",        0, "007 - A B",    "A",    7, 0, 0 },
        { "id-zero",                    "0 - A B",          0, "0 - A B",      "A",    0, 0, 0 },
        { "id-overflows",               "99999999999 - A B", 0, "99999999999 - A B", "A", 0, 0, 0 },
        { "negative-id",                "-5 - A B",         0, "-5 - A B",     "A",   -5, 0, 0 },
        { "plus-id",                    "+5 - A B",         0, "+5 - A B",     "A",    5, 0, 0 },
        { "leading-space-id",           " 5 - A B",         0, " 5 - A B",     "A",    5, 0, 0 },
        { "no-spaces-around-dash",      "40-A B",           0, "40-A B",       "",     0, 0, 0 },
        { "second-dash-stays-in-label", "5 - 6 - A B",      0, "5 - 6 - A B",  "6 - A", 5, 0, 0 },
        { "two-spaces-after-dash",      "5 -  A B",         0, "5 -  A B",     " A",   5, 0, 0 },

        // --- the receiver number "_RCVR<N>" ------------------------------------------------------
        { "rcvr-plain",                 "5 - A L_RCVR3",    0, "5 - A L_RCVR3", "A",   5, 3, 0 },
        { "rcvr-space-before-number",   "5 - A L_RCVR 3",   0, "5 - A L_RCVR 3", "A L_RCVR", 5, 3, 0 },
        { "rcvr-plus-number",           "5 - A L_RCVR+3",   0, "5 - A L_RCVR+3", "A",  5, 3, 0 },
        { "rcvr-trailing-junk",         "5 - A L_RCVR3x",   0, "5 - A L_RCVR3x", "A",  5, 0, 0 },
        { "rcvr-overflows",             "5 - A L_RCVR99999999999", 0, "5 - A L_RCVR99999999999", "A", 5, 0, 0 },
        { "rcvr-without-number",        "5 - A L_RCVR",     0, "5 - A L_RCVR", "A",    5, 0, 0 },
        { "last-rcvr-token-wins",       "5 - A L_RCVR1_RCVR2", 0, "5 - A L_RCVR1_RCVR2", "A", 5, 2, 0 },
        { "rcvr-without-id-prefix",     "L_RCVR4",          0, "L_RCVR4",      "",     0, 4, 0 },
        { "rcvr-token-needs-underscore", "5 - A LRCVR3",    0, "5 - A LRCVR3", "A",    5, 0, 0 },
        { "rcvr-token-is-case-exact",   "5 - A L_rcvr3",    0, "5 - A L_rcvr3", "A",   5, 0, 0 },

        // --- what is and is not a digit in an id: the characters either side of '0'..'9' are not -----
        { "colon-is-not-a-digit",       "5: - A Lock (%)",  1, "5: - A",       "5: - A", 0, 0, 0 },
        { "slash-is-not-a-digit",       "5/ - A Lock (%)",  1, "5/ - A",       "5/ - A", 0, 0, 0 },
        { "digits-nine-and-zero",       "90 - A Lock (%)",  0, "90 - A Lock (%)", "A Lock", 90, 0, 0 },

        // --- the source qualifier "S<n>| " -------------------------------------------------------
        { "qualifier-only",             "S1| ",             0, "",             "",     0, 0, 1 },
        { "qualifier-needs-space",      "S1|x",             0, "S1|x",         "",     0, 0, 0 },
        { "qualifier-overflows",        "S99999999999| a",  0, "S99999999999| a", "",  0, 0, 0 },
        { "qualifier-zero",             "S0| a",            0, "a",            "",     0, 0, 0 },
        { "qualifier-lock",             "S12| Ch 05 Lock (%)", 1, "Ch 05",     "Ch 05", 0, 0, 12 },
        { "qualifier-missed",           "S3| Ch 05 Accumulated Missed Frames", 2, "Ch 05", "Ch 05", 0, 0, 3 },
        { "qualifier-snr",              "S2| 40 - X L_RCVR1", 0, "40 - X L_RCVR1", "X", 40, 1, 2 },
        { "qualifier-lower-case-s",     "s1| a",            0, "s1| a",        "",     0, 0, 0 },
        { "qualifier-negative",         "S-1| a",           0, "S-1| a",       "",     0, 0, 0 },

        // --- text outside ASCII (UTF-16 in Qt, UTF-8 in the core) --------------------------------
        { "non-ascii-lock",             "R\xC3\xA9" "cepteur \xCE\xA9 Lock (%)", 1, "R\xC3\xA9" "cepteur \xCE\xA9",
          "R\xC3\xA9" "cepteur \xCE\xA9", 0, 0, 0 },
        { "non-ascii-snr",              "40 - R\xC3\xA9" "cepteur \xCE\xA9 L_RCVR2", 0,
          "40 - R\xC3\xA9" "cepteur \xCE\xA9 L_RCVR2", "R\xC3\xA9" "cepteur \xCE\xA9", 40, 2, 0 },
        { "non-ascii-qualified",        "S4| \xC3\x89t\xC3\xA9 Accumulated Missed Frames", 2, "\xC3\x89t\xC3\xA9",
          "\xC3\x89t\xC3\xA9", 0, 0, 4 },
    };
    for (const ParseRow& row : rows)
    {
        INFO("row " << row.tag << ": '" << row.header << "'");
        const ParsedColumn got = parseHeader(row.header);
        CHECK(static_cast<int>(got.metricType) == row.metric);
        CHECK(got.name == row.name);
        CHECK(got.streamLabel == row.streamLabel);
        CHECK(got.streamOrder == row.streamOrder);
        CHECK(got.receiverIndex == row.receiverIndex);
        CHECK(got.sourceId == row.sourceId);
    }
}

TEST_CASE("SeriesColumnSchema: format edge cases keep their recorded behaviour")
{
    const std::vector<FormatRow> rows = {
        { "lock-empty-name",       MetricType::FrameSyncLock,           "",           0, " Lock (%)" },
        { "missed-empty-name",     MetricType::AccumulatedMissedFrames, "",           0, " Accumulated Missed Frames" },
        { "snr-empty-name",        MetricType::SNR,                     "",           0, "" },
        { "snr-empty-qualified",   MetricType::SNR,                     "",           1, "S1| " },
        { "max-source-id",         MetricType::FrameSyncLock,           "X",          2147483647, "S2147483647| X Lock (%)" },
        { "negative-source-id",    MetricType::SNR,                     "X",         -1, "S-1| X" },
        { "non-ascii-name",        MetricType::FrameSyncLock,           "\xC3\x89t\xC3\xA9 \xCE\xA9", 2,
          "S2| \xC3\x89t\xC3\xA9 \xCE\xA9 Lock (%)" },
        { "name-already-suffixed", MetricType::FrameSyncLock,           "X Lock (%)", 0, "X Lock (%) Lock (%)" },
    };
    for (const FormatRow& row : rows)
    {
        INFO("row " << row.tag);
        CHECK(formatHeader(row.metric, row.name, row.sourceId) == row.header);
    }

    // snrSeriesName() is the same kind of formatter: the id, the label and the channel, joined.
    CHECK(formatSnrName(0, "", "") == "0 -  ");
    CHECK(formatSnrName(-3, "L C", "R") == "-3 - L C R");
    CHECK(formatSnrName(2147483647, "\xC3\x89t\xC3\xA9", "\xCE\xA9_RCVR1") == "2147483647 - \xC3\x89t\xC3\xA9 \xCE\xA9_RCVR1");
}

// ---------------------------------------------------------------------------------------------
// The generated headers. The generator is the Qt suite's, driven by the same seed; the digest and counters below are
// what the QString implementation answered for them (see the top of the file).
// ---------------------------------------------------------------------------------------------

namespace {

using Strings = std::vector<std::string>;

/// Picks one entry of a list with the test's fixed-seed generator.
const std::string& pickOf(std::mt19937& rng, const Strings& list)
{
    return list[static_cast<std::size_t>(rng() % static_cast<unsigned>(list.size()))];
}

/// One generated column header. Half are built from the schema's own grammar (qualifier, id, separator, label, channel,
/// suffix) with the awkward variants of each part; the rest are random strings of the tokens the parser looks for, so
/// shapes nobody wrote down get tried too.
std::string generateHeader(std::mt19937& rng)
{
    static const Strings qualifiers = {
        "", "", "", "S1| ", "S2| ", "S12| ", "S0| ", "S007| ", "S1|", "S1 | ", "s1| ", "S| ", "S-1| ",
        "S+1| ", "S99999999999| ", "S2147483647| ", "S2147483648| ", "SS1| ", "S1| S2| "
    };
    static const Strings ids = {
        "", "0", "1", "7", "40", "007", "12", "99999999999", "2147483647", "2147483648", "-5", "+5",
        " 5", "5 ", "1a", "a1", "5.5", "-", "+", "+-5"
    };
    static const Strings separators = {
        " - ", " - ", " - ", "-", " -", "- ", " -  ", "  - ", " --", " - - ", " \xE2\x80\x93 "
    };
    static const Strings labels = {
        "", "A", "A B", "RCVR Data", " A", "A ", "6 - A", "Lock (%)", "A  B", "S1| A",
        "R\xC3\xA9" "cepteur \xCE\xA9", "\xC3\x89t\xC3\xA9"
    };
    static const Strings channels = {
        "", " L_RCVR1", " R_RCVR2", " C_RCVR3", " L_RCVR 3", " L_RCVR+4", " L_RCVR-1", " L_RCVR3x", " L_RCVR",
        "_RCVR2_RCVR9", " L_RCVR99999999999", " L_RCVR2147483648", " L_rcvr3", " X", " X Y", "\t_RCVR5"
    };
    static const Strings suffixes = {
        "", "", " Lock (%)", " Accumulated Missed Frames", "Lock (%)", " Lock (%) ", " Lock (%) Lock (%)",
        " Accumulated Missed Frames Lock (%)", " lock (%)", " Lock(%)"
    };
    static const Strings tokens = {
        " ", "  ", " - ", "-", "_RCVR", "_RCVR1", "S", "S1| ", "|", "| ", "0", "1", "42", "-7", "+3",
        " Lock (%)", " Accumulated Missed Frames", "Lock", "(%)", "A", "B C", "R\xC3\xA9", "\xCE\xA9", "\t"
    };

    if (rng() % 2 == 0)
    {
        return pickOf(rng, qualifiers) + pickOf(rng, ids) + pickOf(rng, separators) + pickOf(rng, labels) +
               pickOf(rng, channels) + pickOf(rng, suffixes);
    }
    std::string out;
    const int parts = 1 + static_cast<int>(rng() % 7);
    for (int i = 0; i < parts; ++i)
    {
        out += pickOf(rng, tokens);
    }
    return out;
}

} // namespace

TEST_CASE("SeriesColumnSchema: reads every generated header as the QString implementation did")
{
    std::mt19937 rng(20261021u);
    constexpr int kHeaders = 40000;

    // FNV-1a over what each header resolved to, one line per header.
    std::uint64_t digest = 1469598103934665603ULL;
    int metricSeen[3] = { 0, 0, 0 };
    int withSource = 0, withOrder = 0, withReceiver = 0, withLabel = 0, qualifierLeftInName = 0;

    for (int n = 0; n < kHeaders; ++n)
    {
        const std::string header = generateHeader(rng);
        const ParsedColumn got = parseHeader(header);

        for (unsigned char c : describe(got) + "\n")
        {
            digest ^= c;
            digest *= 1099511628211ULL;
        }

        ++metricSeen[static_cast<int>(got.metricType)];
        withSource += got.sourceId != 0;
        withOrder += got.streamOrder != 0;
        withReceiver += got.receiverIndex != 0;
        withLabel += !got.streamLabel.empty();
        qualifierLeftInName += got.sourceId == 0 && header.starts_with('S') && got.name == header;
    }

    CHECK(digest == 6058731668784468133ULL);

    // The counters the QString implementation reached, and so the volume the generator reaches.
    CHECK(metricSeen[0] == 31209);   // SNR
    CHECK(metricSeen[1] == 6181);    // lock
    CHECK(metricSeen[2] == 2610);    // missed frames
    CHECK(withSource == 7249);
    CHECK(withOrder == 2384);
    CHECK(withReceiver == 3243);
    CHECK(withLabel == 18064);
    CHECK(qualifierLeftInName == 5805);
}

namespace {

/// The formatting grammar written out a second way, sharing nothing with the schema's code.
std::string expectedHeader(MetricType metric, const std::string& name, int sourceId)
{
    std::string base = name;
    if (metric == MetricType::FrameSyncLock)
    {
        base += " Lock (%)";
    }
    else if (metric == MetricType::AccumulatedMissedFrames)
    {
        base += " Accumulated Missed Frames";
    }
    return sourceId == 0 ? base : "S" + std::to_string(sourceId) + "| " + base;
}

} // namespace

TEST_CASE("SeriesColumnSchema: the formatters agree with the grammar written another way")
{
    static const Strings names = {
        "", "A", "Ch 05", "40 - RCVR Data L_RCVR1", " Lock (%)", "X Lock (%)", "S1| A", "R\xC3\xA9" "cepteur \xCE\xA9",
        "\xC3\x89t\xC3\xA9 - 7"
    };
    static const int sourceIds[] = { 0, 1, 2, 12, 99, -1, 2147483647, std::numeric_limits<int>::min() };
    static const int channelIds[] = { 0, 1, 7, 40, -3, 2147483647, std::numeric_limits<int>::min() };
    static const MetricType metrics[] = { MetricType::SNR, MetricType::FrameSyncLock,
                                          MetricType::AccumulatedMissedFrames };

    int checked = 0;
    for (const std::string& name : names)
    {
        for (MetricType metric : metrics)
        {
            for (int source : sourceIds)
            {
                REQUIRE(formatHeader(metric, name, source) == expectedHeader(metric, name, source));
                ++checked;
            }
        }
        for (int id : channelIds)
        {
            for (const std::string& channel : names)
            {
                REQUIRE(formatSnrName(id, name, channel) == std::to_string(id) + " - " + name + " " + channel);
                ++checked;
            }
        }
    }
    // Every combination ran: 9 names x (3 metrics x 8 source ids + 7 channel ids x 9 channel names).
    CHECK(checked == static_cast<int>(names.size()) * (3 * 8 + 7 * static_cast<int>(names.size())));

    // And the pair stays an inverse for what the application itself writes: the source always comes back.
    for (const std::string& name : names)
    {
        if (name.starts_with("S1| "))
        {
            continue;   // the format's one blind spot: see the next case
        }
        for (MetricType metric : metrics)
        {
            for (int source : { 0, 1, 12 })
            {
                INFO("name '" << name << "' source " << source);
                CHECK(parseHeader(formatHeader(metric, name, source)).sourceId == source);
            }
        }
    }
}

TEST_CASE("SeriesColumnSchema: a name that looks like a source qualifier does not round-trip")
{
    // A limitation of the FORMAT, present before the schema moved into the Qt-free core and kept as it was: source 0 is
    // written with no qualifier, so a series whose own NAME begins with "S<n>| " is read back as source <n> with the
    // rest as its name. Series names come from TMATS stream labels, so this needs a recording labelled like that;
    // pinned so a change to the format is a decision, not an accident.
    const std::string header = formatHeader(MetricType::SNR, "S1| A", /*sourceId=*/0);
    CHECK(header == "S1| A");

    const ParsedColumn got = parseHeader(header);
    CHECK(got.sourceId == 1);
    CHECK(got.name == "A");

    // Qualified on purpose it does round-trip, whatever the name (the qualifier is stripped once).
    const ParsedColumn qualified = parseHeader(formatHeader(MetricType::SNR, "S1| A", /*sourceId=*/2));
    CHECK(qualified.sourceId == 2);
    CHECK(qualified.name == "S1| A");
}
