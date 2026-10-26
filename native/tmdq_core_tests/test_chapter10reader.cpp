// Chapter10Reader: the metadata reader that lists a recording's channels for the Configure Streams dialog, and
// ChannelData, the value it stores per channel. No Qt; ported from the Qt TestChapter10Reader and TestChannelData,
// whose cases are all here under the same descriptions (the data-driven ones are loops).

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "chapter10reader.h"
#include "channeldata.h"
#include "test_support.h"

using namespace tmdq_test;

/// Reads what no production caller reads back. Declared a friend of Chapter10Reader.
struct Chapter10ReaderTestAccess
{
    static int currentPcmChannel(const Chapter10Reader& r) { return r.m_current_pcm_channel; }
};

namespace {

int currentPcm(const Chapter10Reader& r) { return Chapter10ReaderTestAccess::currentPcmChannel(r); }

fs::path fixturePath(const std::string& name) { return repoFile(fs::path("tests") / "data" / "test files" / name); }

/// Loads a fixture and requires it to open.
void loadFixture(Chapter10Reader& reader, const std::string& name)
{
    REQUIRE_MESSAGE(reader.loadChannels(fixturePath(name)), name << ": " << reader.lastError());
    REQUIRE(reader.lastError().empty());
}

/// "<id>|<name>|<rate>" for a PCM channel, for comparing whole channel lists at once.
std::string idNameRate(int id, const std::string& name, double bits_per_second)
{
    return std::to_string(id) + "|" + name + "|" + std::to_string(static_cast<long long>(bits_per_second + 0.5));
}

Strings pcmChannelsWithRates(const Chapter10Reader& reader)
{
    Strings out;
    for (const auto& [id, name] : reader.getPCMChannelList())
    {
        out.push_back(idNameRate(id, name, reader.getTmatsDataRateBps(id)));
    }
    return out;
}

/// The length of the recording's first packet (its TMATS record), from the header's packet-length field.
std::uint32_t firstPacketLength(const std::string& bytes)
{
    std::uint32_t length = 0;
    std::memcpy(&length, bytes.data() + 4, 4);
    return length;
}

/// Copies step_cal's recording with edits made to its TMATS text. Each edit is a (needle, replacement) pair of the SAME
/// length, so no packet length or checksum changes (this recording carries no data checksum, and the header is
/// untouched). Fails the case when an edit does not apply exactly once.
fs::path writePatchedStepCal(const TempDir& dir, const std::vector<std::pair<std::string, std::string>>& edits)
{
    const std::string all = readBytes(fixturePath("step_cal, example.ch10"));
    const std::uint32_t first = firstPacketLength(all);
    REQUIRE(first > 200);
    std::string tmats = all.substr(0, first);
    for (const auto& [needle, replacement] : edits)
    {
        INFO("edit: " << needle);
        REQUIRE(needle.size() == replacement.size());
        const std::size_t at = tmats.find(needle);
        REQUIRE(at != std::string::npos);
        REQUIRE(tmats.find(needle, at + 1) == std::string::npos);
        tmats.replace(at, replacement.size(), replacement);
    }
    const fs::path path = dir.file("patched.ch10");
    writeBytes(path, tmats + all.substr(first));
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// ChannelData
// ---------------------------------------------------------------------------

TEST_CASE("ChannelData: the constructor sets the channel id, and everything else starts empty")
{
    const ChannelData cd(42);
    CHECK(cd.channelID() == 42);
    CHECK(cd.channelType().empty());
    CHECK(cd.channelName().empty());
    CHECK(cd.channelCount() == 0);
}

TEST_CASE("ChannelData: setting the type and the name stores them")
{
    ChannelData cd(1);
    cd.setChannelType("PCMIN");
    cd.setChannelName("TestChannel");
    CHECK(cd.channelType() == "PCMIN");
    CHECK(cd.channelName() == "TestChannel");
}

TEST_CASE("ChannelData: incrementing the count increments it once each time")
{
    ChannelData cd(1);
    cd.incrementChannelCount();
    CHECK(cd.channelCount() == 1);
    cd.incrementChannelCount();
    cd.incrementChannelCount();
    CHECK(cd.channelCount() == 3);
}

TEST_CASE("ChannelData: non-ASCII text round-trips")
{
    // TMATS names are free text; one with an accented letter and a Greek omega must come back exactly as set, and
    // setting again replaces rather than appends.
    const std::string name = utf8(u8"Récepteur Ω");
    const std::string type = utf8(u8"PCMIN-é");

    ChannelData cd(7);
    cd.setChannelName(name);
    cd.setChannelType(type);
    CHECK(cd.channelName() == name);
    CHECK(cd.channelType() == type);

    cd.setChannelName("plain");
    CHECK(cd.channelName() == "plain");
    cd.setChannelName("");
    CHECK(cd.channelName().empty());
}

TEST_CASE("ChannelData: copies keep every field")
{
    // The reader stores these by value and the class is declared copyable and movable.
    ChannelData original(9);
    original.setChannelName("Name");
    original.setChannelType("TIMEIN");
    original.incrementChannelCount();
    original.incrementChannelCount();

    ChannelData copy(original);
    CHECK(copy.channelID() == 9);
    CHECK(copy.channelName() == "Name");
    CHECK(copy.channelType() == "TIMEIN");
    CHECK(copy.channelCount() == 2);

    // A copy is independent of the original.
    copy.incrementChannelCount();
    copy.setChannelName("Changed");
    CHECK(original.channelCount() == 2);
    CHECK(original.channelName() == "Name");

    ChannelData moved(std::move(copy));
    CHECK(moved.channelID() == 9);
    CHECK(moved.channelName() == "Changed");
    CHECK(moved.channelCount() == 3);

    ChannelData assigned(1);
    assigned = original;
    CHECK(assigned.channelID() == 9);
    CHECK(assigned.channelName() == "Name");
}

// ---------------------------------------------------------------------------
// Chapter10Reader
// ---------------------------------------------------------------------------

TEST_CASE("Chapter10Reader: loading a valid file succeeds and lists time and PCM channels")
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");
    CHECK(!reader.getTimeChannelComboBoxList().empty());
    CHECK(!reader.getPCMChannelComboBoxList().empty());
}

TEST_CASE("Chapter10Reader: combo-box lists contain the channel id and name")
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");
    for (const Strings& entries : {reader.getTimeChannelComboBoxList(), reader.getPCMChannelComboBoxList()})
    {
        REQUIRE(!entries.empty());
        for (const std::string& entry : entries)
        {
            INFO(entry);
            const std::size_t sep = entry.find(" - ");
            REQUIRE(sep != std::string::npos);
            CHECK(sep > 0);
            CHECK(entry.size() > sep + 3);   // a name after the separator
        }
    }
}

TEST_CASE("Chapter10Reader: each fixture lists its channels exactly")
{
    // The combo-box strings are "<id> - <name>"; the channel list carries the bare name, which labels the stream in the
    // dialog and on the plot.
    struct Row
    {
        const char* file;
        Strings time_list;
        Strings pcm_list;
        Strings pcm_channels;
        int first_pcm;
    };
    const Row rows[] = {
        {"agc_nzl_rasa_testfile.ch10",
         {"1 - TimeChannel-1"},
         {"19 - 2240.5A", "20 - 2257.5A", "21 - 2257.5B", "22 - 2257.5C", "23 - 2240.5B", "24 - 2240.5C", "25 - 2240.5D",
          "26 - CH-01"},
         {idNameRate(19, "2240.5A", 2.5e6), idNameRate(20, "2257.5A", 2.1e6), idNameRate(21, "2257.5B", 2.1e6),
          idNameRate(22, "2257.5C", 2.1e6), idNameRate(23, "2240.5B", 2.5e6), idNameRate(24, "2240.5C", 2.5e6),
          idNameRate(25, "2240.5D", 2.5e6), idNameRate(26, "CH-01", 8.0e5)},
         19},
        {"agc_rnrz-l_trc_testfile.ch10",
         {"1 - IRIG-B"},
         {"13 - CH-01 2250.5MHZ AGC 800Kbps RNRZ-L"},
         {idNameRate(13, "CH-01 2250.5MHZ AGC 800Kbps RNRZ-L", 8.0e5)},
         13},
        {"prn_testfile, sample.ch10",
         {"1 - IRIG-B"},
         {"13 - CH-01 PRN 15 5M", "14 - CH-02 PRN 15 20M", "15 - CH-03 PRN 11 1M", "16 - CH-04 PRN 11 5M"},
         {idNameRate(13, "CH-01 PRN 15 5M", 5.0e6), idNameRate(14, "CH-02 PRN 15 20M", 2.0e7),
          idNameRate(15, "CH-03 PRN 11 1M", 1.0e6), idNameRate(16, "CH-04 PRN 11 5M", 5.0e6)},
         13},
        {"safran_testfile, sample.ch10",
         {"1 - Time_Pck"},
         {"11 - PCM01", "12 - PCM02", "13 - PCM03", "14 - PCM04"},
         {idNameRate(11, "PCM01", 7.0e5), idNameRate(12, "PCM02", 1.0e6), idNameRate(13, "PCM03", 1.0e6),
          idNameRate(14, "PCM04", 7.0e5)},
         11},
        {"step_cal, example.ch10",
         {"1 - IRIG-B"},
         {"26 - CH-16 AGC STEP CALS 800K"},
         {idNameRate(26, "CH-16 AGC STEP CALS 800K", 8.0e5)},
         26},
    };
    for (const Row& row : rows)
    {
        INFO(row.file);
        Chapter10Reader reader;
        loadFixture(reader, row.file);
        CHECK(reader.getTimeChannelComboBoxList() == row.time_list);
        CHECK(reader.getPCMChannelComboBoxList() == row.pcm_list);
        CHECK(pcmChannelsWithRates(reader) == row.pcm_channels);

        // Defaults after a load: the first time channel, and the first PCM channel.
        CHECK(reader.getCurrentTimeChannelID() == 1);
        CHECK(reader.getFirstPCMChannelID() == row.first_pcm);
        CHECK(currentPcm(reader) == row.first_pcm);
    }
}

TEST_CASE("Chapter10Reader: a channel TMATS does not describe has no data rate")
{
    Chapter10Reader reader;
    loadFixture(reader, "prn_testfile, sample.ch10");
    CHECK(reader.getTmatsDataRateBps(13) == 5.0e6);   // described
    CHECK(reader.getTmatsDataRateBps(999) == 0.0);    // not in the file at all
    CHECK(reader.getTmatsDataRateBps(1) == 0.0);      // the time channel: no PCM record
    CHECK(reader.getTmatsDataRateBps(-1) == 0.0);
}

TEST_CASE("Chapter10Reader: the current time and PCM channels are set after a load")
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");
    CHECK(reader.getCurrentTimeChannelID() != -1);
    // No production caller reads the PCM selection back through a getter, so the test access struct reaches it.
    CHECK(currentPcm(reader) != -1);
    CHECK(reader.getFirstPCMChannelID() == currentPcm(reader));
}

TEST_CASE("Chapter10Reader: clearSettings resets the channels")
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");
    REQUIRE(!reader.getTimeChannelComboBoxList().empty());
    REQUIRE(!reader.getPCMChannelComboBoxList().empty());

    reader.clearSettings();
    CHECK(reader.getTimeChannelComboBoxList().empty());
    CHECK(reader.getPCMChannelComboBoxList().empty());
    CHECK(reader.getPCMChannelList().empty());
    CHECK(reader.getTimeChannelList().empty());
    CHECK(reader.getCurrentTimeChannelID() == -1);
    CHECK(reader.getFirstPCMChannelID() == -1);
    CHECK(currentPcm(reader) == -1);
}

TEST_CASE("Chapter10Reader: a combo-box index selects the channel at that position")
{
    // Four PCM channels (13..16) and one time channel (1). The combo box has a placeholder at index 0, so index N picks
    // the (N-1)th channel; the placeholder and anything past the end mean "none selected".
    Chapter10Reader reader;
    loadFixture(reader, "prn_testfile, sample.ch10");

    const int pcm_expected[][2] = {{0, -1}, {1, 13}, {2, 14}, {3, 15}, {4, 16}, {5, -1}, {-1, -1}, {100, -1}};
    for (const auto& row : pcm_expected)
    {
        reader.pcmChannelChanged(row[0]);
        INFO("pcm index " << row[0]);
        CHECK(currentPcm(reader) == row[1]);
    }

    const int time_expected[][2] = {{0, -1}, {1, 1}, {2, -1}, {-1, -1}};
    for (const auto& row : time_expected)
    {
        reader.timeChannelChanged(row[0]);
        INFO("time index " << row[0]);
        CHECK(reader.getCurrentTimeChannelID() == row[1]);
    }

    // Selecting does not change what is listed, and the first-PCM accessor is about the list, not the selection.
    CHECK(reader.getPCMChannelComboBoxList().size() == 4);
    CHECK(reader.getFirstPCMChannelID() == 13);
}

TEST_CASE("Chapter10Reader: loading a file that does not exist fails")
{
    Chapter10Reader reader;
    CHECK(!reader.loadChannels("nonexistent_file.ch10"));
    CHECK(!reader.lastError().empty());
}

TEST_CASE("Chapter10Reader: a failed load reports its reason and leaves no channels")
{
    // A real recording to cut up: its first packet is the TMATS record.
    const std::string all = readBytes(fixturePath("step_cal, example.ch10"));
    const std::uint32_t first = firstPacketLength(all);
    REQUIRE((first > 200 && first < all.size()));

    // The library refuses to open the first five, and the reader says which file. A recording whose first packet is not
    // TMATS is refused at open as well: the reader's own "Failed to find TMATS message" branch sits behind a check the
    // library has already made, so no file reaches it. The last two open, and then cannot establish time from what is
    // left of the file.
    struct Row { const char* scenario; const char* exact_message; };
    const Row rows[] = {
        {"missing", ""},      {"empty", ""}, {"text", ""}, {"short-header", ""}, {"no-tmats-first", ""},
        {"only-tmats", "Error establishing time sync."},
        {"cut-in-tmats", "Error establishing time sync."},
    };
    for (const Row& row : rows)
    {
        const std::string scenario = row.scenario;
        INFO(scenario);
        TempDir dir;
        const fs::path path = dir.file(scenario + ".ch10");
        if (scenario == "empty")               writeBytes(path, "");
        else if (scenario == "text")           writeBytes(path, "this is not a chapter 10 file\n");
        else if (scenario == "no-tmats-first") writeBytes(path, all.substr(first));
        else if (scenario == "short-header")   writeBytes(path, all.substr(0, 10));
        else if (scenario == "only-tmats")     writeBytes(path, all.substr(0, first));
        else if (scenario == "cut-in-tmats")   writeBytes(path, all.substr(0, first - 100));

        Chapter10Reader reader;
        CHECK(!reader.loadChannels(path));
        const std::string expected = std::string(row.exact_message).empty() ? "Error opening file: " + path.string()
                                                                             : std::string(row.exact_message);
        CHECK(reader.lastError() == expected);
        CHECK(reader.getTimeChannelComboBoxList().empty());
        CHECK(reader.getPCMChannelComboBoxList().empty());
        CHECK(reader.getFirstPCMChannelID() == -1);
        CHECK(reader.getCurrentTimeChannelID() == -1);
    }
}

TEST_CASE("Chapter10Reader: TMATS text decides which channels are listed and what they are called")
{
    const Strings shown = {"26 - CH-16 AGC STEP CALS 800K"};
    const Strings none  = {};
    const Strings irig  = {"1 - IRIG-B"};
    const std::string che = R"(R-1\CHE-27:)";
    const std::string cdt = R"(R-1\CDT-27:)";
    // Whitespace around the flag needs a spare byte: the unused TK4-27 value gives one up ("27" -> "2").
    const std::string tk4       = std::string(R"(R-1\TK4-27:27;)") + "\r\n";
    const std::string tk4_short = std::string(R"(R-1\TK4-27:2;)") + "\r\n";
    // The channel's name: the data-source id (DSI-n) first, then the channel data link name (CDLN-n), then the PCM link
    // name (PDLN-n). The recording has the first two; PDLN is made by renaming a same-length key (PDTF-27 -> PDLN-27).
    const std::string dsi_off = R"(R-1\DSI-27:)";
    const std::string dsi_broken = R"(R-1\DSX-27:)";
    const std::string cdln_on = R"(R-1\CDLN-27:)";
    const std::string cdln_broken = R"(R-1\CDLX-27:)";
    const std::pair<std::string, std::string> pdln = {R"(R-1\PDTF-27:1;)", R"(R-1\PDLN-27:Q;)"};

    using Edits = std::vector<std::pair<std::string, std::string>>;
    struct Row { const char* name; Edits edits; Strings pcm_list; Strings time_list; };
    const Row rows[] = {
        // "Channel enabled" (CHE-n): T, Y and 1 enable, in either case; F, N, 0 and anything else disable, so the
        // channel never appears; no CHE-n at all is treated as enabled.
        {"che-F", {{che + "T;", che + "F;"}}, none, irig},
        {"che-N", {{che + "T;", che + "N;"}}, none, irig},
        {"che-0", {{che + "T;", che + "0;"}}, none, irig},
        {"che-Q", {{che + "T;", che + "Q;"}}, none, irig},
        {"che-Y", {{che + "T;", che + "Y;"}}, shown, irig},
        {"che-1", {{che + "T;", che + "1;"}}, shown, irig},
        {"che-t", {{che + "T;", che + "t;"}}, shown, irig},
        {"che-y", {{che + "T;", che + "y;"}}, shown, irig},
        {"che-missing", {{R"(R-1\CHE-27:T;)", R"(R-1\CHX-27:T;)"}}, shown, irig},
        {"che-space-before", {{tk4 + che + "T;", tk4_short + che + " T;"}}, shown, irig},
        {"che-space-after", {{tk4 + che + "T;", tk4_short + che + "T ;"}}, shown, irig},
        {"che-space-before-F", {{tk4 + che + "T;", tk4_short + che + " F;"}}, none, irig},
        // Channel type (CDT-n): PCMIN, 01 and 09 are PCM; TIMEIN and 11 are time; the match is case-sensitive and
        // anything else is some other kind of channel that is not listed.
        {"type-01", {{cdt + "PCMIN;", cdt + "01   ;"}}, shown, irig},
        {"type-09", {{cdt + "PCMIN;", cdt + "09   ;"}}, shown, irig},
        {"type-pcmin-lower", {{cdt + "PCMIN;", cdt + "pcmin;"}}, none, irig},
        {"type-other", {{cdt + "PCMIN;", cdt + "XYZ  ;"}}, none, irig},
        {"time-11", {{R"(R-1\CDT-1:TIMEIN;)", R"(R-1\CDT-1:11    ;)"}}, shown, irig},
        {"time-lower", {{R"(R-1\CDT-1:TIMEIN;)", R"(R-1\CDT-1:timein;)"}}, shown, none},
        // A second time channel (the PCM channel retyped as 11): both are listed in id order, and the default
        // selection is the FIRST, not the last one seen (the row check below requires channel 1).
        {"two-time-channels", {{cdt + "PCMIN;", cdt + "11   ;"}}, none, {"1 - IRIG-B", "26 - CH-16 AGC STEP CALS 800K"}},
        {"name-cdln-when-no-dsi", {{dsi_off, dsi_broken}}, {"26 - MrgStream-16"}, irig},
        {"name-empty-when-neither", {{dsi_off, dsi_broken}, {cdln_on, cdln_broken}}, {"26 - "}, irig},
        {"name-pdln-when-neither", {{dsi_off, dsi_broken}, {cdln_on, cdln_broken}, pdln}, {"26 - Q"}, irig},
        {"name-dsi-beats-pdln", {pdln}, shown, irig},
        {"name-cdln-beats-pdln", {{dsi_off, dsi_broken}, pdln}, {"26 - MrgStream-16"}, irig},
        // A name outside ASCII is UTF-8 in the file (an accented letter is two bytes, so the edit trades "H-" for it
        // and keeps the length) and has to come out as the same text.
        {"name-non-ascii",
         {{R"(R-1\DSI-27:CH-16 AGC)", std::string(R"(R-1\DSI-27:C)") + utf8(u8"é") + "-1 AGC"}},
         {std::string("26 - C") + utf8(u8"é") + "-1 AGC STEP CALS 800K"},
         irig},
    };
    for (const Row& row : rows)
    {
        INFO(row.name);
        TempDir dir;
        const fs::path path = writePatchedStepCal(dir, row.edits);
        Chapter10Reader reader;
        REQUIRE_MESSAGE(reader.loadChannels(path), reader.lastError());
        CHECK(reader.getPCMChannelComboBoxList() == row.pcm_list);
        CHECK(reader.getTimeChannelComboBoxList() == row.time_list);
        CHECK(reader.getFirstPCMChannelID() == (row.pcm_list.empty() ? -1 : 26));
        CHECK(reader.getCurrentTimeChannelID() == (row.time_list.empty() ? -1 : 1));
    }
}

TEST_CASE("Chapter10Reader: a file that fails to open leaves the previous channels alone")
{
    Chapter10Reader reader;
    loadFixture(reader, "prn_testfile, sample.ch10");
    const Strings before = reader.getPCMChannelComboBoxList();

    TempDir dir;
    CHECK(!reader.loadChannels(dir.file("missing.ch10")));
    CHECK(!reader.lastError().empty());
    CHECK(reader.getPCMChannelComboBoxList() == before);
}

TEST_CASE("Chapter10Reader: a second load replaces what the first left behind")
{
    Chapter10Reader reader;
    loadFixture(reader, "prn_testfile, sample.ch10");
    const Strings prn_pcm = reader.getPCMChannelComboBoxList();
    REQUIRE(prn_pcm.size() == 4);

    // Move the selection off its default, then load the same file again: the lists are what one load gives (not
    // doubled) and the selection is back at its default.
    reader.pcmChannelChanged(3);
    CHECK(currentPcm(reader) == 15);
    loadFixture(reader, "prn_testfile, sample.ch10");
    CHECK(reader.getPCMChannelComboBoxList() == prn_pcm);
    CHECK(reader.getTimeChannelComboBoxList() == Strings{"1 - IRIG-B"});
    CHECK(currentPcm(reader) == 13);

    // A different file replaces the channels outright.
    loadFixture(reader, "step_cal, example.ch10");
    CHECK(reader.getPCMChannelComboBoxList() == Strings{"26 - CH-16 AGC STEP CALS 800K"});
    CHECK(reader.getFirstPCMChannelID() == 26);
    CHECK(currentPcm(reader) == 26);
    CHECK(reader.getPCMChannelList().size() == 1);

    // After a failure, a load that works reports nothing.
    TempDir dir;
    CHECK(!reader.loadChannels(dir.file("missing.ch10")));
    CHECK(!reader.lastError().empty());
    CHECK(reader.loadChannels(fixturePath("prn_testfile, sample.ch10")));
    CHECK(reader.lastError().empty());
}

TEST_CASE("Chapter10Reader: the time channel list names the time channels")
{
    // The list a C ABI hands a host: the same channels the combo box shows, as (id, bare name) pairs.
    Chapter10Reader reader;
    CHECK(reader.getTimeChannelList().empty());   // nothing loaded, nothing listed

    loadFixture(reader, "prn_testfile, sample.ch10");
    const auto times = reader.getTimeChannelList();
    REQUIRE(times.size() == 1);
    CHECK(times[0].first == 1);
    CHECK(times[0].second == "IRIG-B");
    CHECK(times[0].first == reader.getCurrentTimeChannelID());

    // The combo box is this list, spelled "<id> - <name>".
    Strings spelled;
    for (const auto& [id, name] : times)
    {
        spelled.push_back(std::to_string(id) + " - " + name);
    }
    CHECK(reader.getTimeChannelComboBoxList() == spelled);

    // And a different file replaces it, as it replaces the PCM list.
    loadFixture(reader, "step_cal, example.ch10");
    CHECK(reader.getTimeChannelList().size() == reader.getTimeChannelComboBoxList().size());
}

TEST_CASE("Chapter10Reader: a TMATS data rate is read like QString::toDouble")
{
    // Six characters, replacing "800000" in P-16\D2. A rate that is not a positive number is "not described", not a rate.
    struct Row { const char* name; const char* value; double expected; };
    const Row rows[] = {
        {"unchanged", "800000", 800000.0},     {"exponent", "8e5   ", 800000.0},
        {"leading-spaces", "  4000", 4000.0},  {"trailing-spaces", "4000  ", 4000.0},
        {"leading-plus", "+80000", 80000.0},   {"fraction", "1234.5", 1234.5},
        {"negative", "-80000", 0.0},           {"zero", "0     ", 0.0},
        {"junk", "80000x", 0.0},               {"word", "abcdef", 0.0},
        {"group-separator", "1,0000", 0.0},    {"nan", "nan   ", 0.0},
    };
    for (const Row& row : rows)
    {
        INFO(row.name);
        TempDir dir;
        const fs::path path =
            writePatchedStepCal(dir, {{R"(P-16\D2:800000;)", std::string(R"(P-16\D2:)") + row.value + ";"}});
        Chapter10Reader reader;
        REQUIRE_MESSAGE(reader.loadChannels(path), reader.lastError());
        CHECK(reader.getTmatsDataRateBps(26) == row.expected);
    }
}

TEST_CASE("Chapter10Reader: getFirstPCMChannelID returns a valid id")
{
    Chapter10Reader reader;
    loadFixture(reader, "agc_nzl_rasa_testfile.ch10");
    const int first_pcm = reader.getFirstPCMChannelID();
    CHECK(first_pcm != -1);
    CHECK(first_pcm == currentPcm(reader));
}
