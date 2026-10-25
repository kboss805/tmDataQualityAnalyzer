// FrameSetup: the word map and calibration table every stream job owns, and the receiver-parameters file it is read
// from and written to. No Qt; ported from the Qt TestFrameSetup, whose cases are all here under the same descriptions,
// plus the paths that suite did not reach (the linear calibration's arithmetic, the reserved sections, a word map that
// fails to load being replaced by the default one when a file is saved).

#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "doctest.h"
#include "framesetup.h"
#include "test_support.h"
#include "tomldocument.h"

using namespace tmdq_test;

namespace {

/// Test frame size: 48 data words + 1 (matches default 16 receivers x 3 channels).
constexpr int kTestWordsInFrame = 49;

fs::path fixture(const std::string& name) { return repoFile(fs::path("tests") / "fixtures" / name); }

fs::path shippedReceiverParams(const std::string& name)
{
    return repoFile(fs::path("settings") / "receiver_params" / name);
}

/// Writes a minimal word-map TOML: one group per entry, Word being 1-based.
void writeWordMap(const fs::path& path, const std::vector<std::pair<std::string, int>>& words)
{
    TomlDocument cfg;
    for (const auto& entry : words)
    {
        cfg.setValue(entry.first + "/Word", entry.second);
    }
    REQUIRE(cfg.save(path));
}

/// The setup's parameters as name -> zero-based word, for comparing maps whatever their order.
std::map<std::string, int> wordsByName(const FrameSetup& fs)
{
    std::map<std::string, int> out;
    for (int i = 0; i < fs.length(); i++)
    {
        out[fs.getParameter(i)->name] = fs.getParameter(i)->word;
    }
    return out;
}

/// The parameter names in order.
Strings namesInOrder(const FrameSetup& fs)
{
    Strings out;
    for (int i = 0; i < fs.length(); i++)
    {
        out.push_back(fs.getParameter(i)->name);
    }
    return out;
}

/// A calibration profile with @p n_points points, valid or not. The raw values differ per profile so a test can tell
/// which one reached which parameter.
CalibrationProfile makeProfile(bool valid, int n_points, double raw_base)
{
    CalibrationProfile profile;
    profile.valid = valid;
    for (int i = 0; i < n_points; i++)
    {
        profile.points.push_back({raw_base + i * 100.0, i * 6.0});
    }
    return profile;
}

/// Hands profiles to the setup by word index. The one place these tests build the container
/// attachCalibrationProfiles() takes, so a change of its type touches this helper only.
int attachProfiles(FrameSetup& setup, const std::vector<std::pair<int, CalibrationProfile>>& by_word)
{
    std::unordered_map<int, CalibrationProfile> container;
    for (const auto& entry : by_word)
    {
        container.emplace(entry.first, entry.second);
    }
    return setup.attachCalibrationProfiles(container);
}

} // namespace

// ---------------------------------------------------------------------------
// The parameter list.
// ---------------------------------------------------------------------------

TEST_CASE("FrameSetup: default length is zero")
{
    FrameSetup fs;
    CHECK(fs.length() == 0);
}

TEST_CASE("FrameSetup: clear parameters resets to empty")
{
    FrameSetup fs;
    REQUIRE(fs.tryLoadingFile(fixture("test_framesetup.toml"), kTestWordsInFrame));
    REQUIRE(fs.length() > 0);
    fs.clearParameters();
    CHECK(fs.length() == 0);
    CHECK(fs.getParameter(0) == nullptr);
}

TEST_CASE("FrameSetup: get parameter with an invalid index returns null")
{
    FrameSetup fs;
    CHECK(fs.getParameter(0) == nullptr);
    CHECK(fs.getParameter(100) == nullptr);

    fs.addParameter("A", 0);
    const FrameSetup& view = fs;
    CHECK(fs.getParameter(0) != nullptr);
    CHECK(view.getParameter(0) != nullptr);
    CHECK(fs.getParameter(1) == nullptr);       // one past the end, both overloads
    CHECK(view.getParameter(1) == nullptr);
}

TEST_CASE("FrameSetup: get parameter with a negative index returns null")
{
    FrameSetup fs;
    fs.addParameter("A", 0);
    const FrameSetup& view = fs;
    CHECK(fs.getParameter(-1) == nullptr);
    CHECK(view.getParameter(-1) == nullptr);
}

TEST_CASE("FrameSetup: addParameter appends an enabled parameter with nothing else set")
{
    FrameSetup fs;
    fs.addParameter("L_RCVR1", 4);
    fs.addParameter("R_RCVR1", 5);
    REQUIRE(fs.length() == 2);
    const ParameterInfo* p = fs.getParameter(1);
    CHECK(p->name == "R_RCVR1");
    CHECK(p->word == 5);
    CHECK(p->is_enabled);
    CHECK(p->slope == 0.0);
    CHECK(p->scale == 0.0);
    CHECK(p->sample_sum == 0.0);
    CHECK(!p->profile.valid);

    // The mutable overload is the same parameter, so a worker's running sum lands where the reader sees it.
    fs.getParameter(1)->sample_sum = 42.0;
    const FrameSetup& view = fs;
    CHECK(view.getParameter(1)->sample_sum == 42.0);
}

// ---------------------------------------------------------------------------
// Loading a word map.
// ---------------------------------------------------------------------------

TEST_CASE("FrameSetup: tryLoadingFile reads a valid file")
{
    FrameSetup fs;
    REQUIRE(fs.tryLoadingFile(fixture("test_framesetup.toml"), kTestWordsInFrame));
    REQUIRE(fs.length() == 3);

    // Parameters preserve file order: L, R, C
    const ParameterInfo* p0 = fs.getParameter(0);
    REQUIRE(p0 != nullptr);
    CHECK(p0->name == "L_RCVR1");
    CHECK(p0->word == 0);  // TOML Word=1, stored as 0
    CHECK(p0->is_enabled);
    CHECK(p0->slope == 0.0);
    CHECK(p0->scale == 0.0);
    CHECK(p0->sample_sum == 0.0);

    const ParameterInfo* p1 = fs.getParameter(1);
    REQUIRE(p1 != nullptr);
    CHECK(p1->name == "R_RCVR1");
    CHECK(p1->word == 1);  // TOML Word=2, stored as 1
    CHECK(p1->is_enabled);

    const ParameterInfo* p2 = fs.getParameter(2);
    REQUIRE(p2 != nullptr);
    CHECK(p2->name == "C_RCVR1");
    CHECK(p2->word == 2);  // TOML Word=3, stored as 2
    CHECK(p2->is_enabled);
}

TEST_CASE("FrameSetup: tryLoadingFile keeps the file's order, not a sorted one")
{
    // QSettings returned its groups sorted; a word map's meaning is the order the file lists it in.
    TempDir tmp;
    writeBytes(tmp.file("order.toml"), "[Z]\nWord = 1\n\n[A]\nWord = 7\n\n[M]\nWord = 3\n");
    FrameSetup fs;
    REQUIRE(fs.tryLoadingFile(tmp.file("order.toml"), kTestWordsInFrame));
    CHECK(namesInOrder(fs) == Strings{"Z", "A", "M"});
    CHECK(fs.getParameter(1)->word == 6);
}

TEST_CASE("FrameSetup: tryLoadingFile with a missing Word key fails")
{
    FrameSetup fs;
    CHECK(!fs.tryLoadingFile(fixture("test_framesetup_missing_word.toml"), kTestWordsInFrame));
}

TEST_CASE("FrameSetup: tryLoadingFile with an out-of-bounds word fails")
{
    FrameSetup fs;
    CHECK(!fs.tryLoadingFile(fixture("test_framesetup_out_of_bounds.toml"), kTestWordsInFrame));
}

TEST_CASE("FrameSetup: tryLoadingFile with word zero fails")
{
    TempDir tmp;
    writeBytes(tmp.file("w0.toml"), "[TestParam]\nWord = 0\n");
    FrameSetup fs;
    CHECK(!fs.tryLoadingFile(tmp.file("w0.toml"), kTestWordsInFrame));
}

TEST_CASE("FrameSetup: tryLoadingFile with word equal to the frame size fails")
{
    TempDir tmp;
    writeBytes(tmp.file("weq.toml"), "[TestParam]\nWord = " + std::to_string(kTestWordsInFrame) + "\n");
    FrameSetup fs;
    CHECK(!fs.tryLoadingFile(tmp.file("weq.toml"), kTestWordsInFrame));

    // One below is the last data word, and is accepted.
    writeBytes(tmp.file("wlast.toml"), "[TestParam]\nWord = " + std::to_string(kTestWordsInFrame - 1) + "\n");
    CHECK(fs.tryLoadingFile(tmp.file("wlast.toml"), kTestWordsInFrame));
    CHECK(fs.getParameter(0)->word == kTestWordsInFrame - 2);
}

TEST_CASE("FrameSetup: tryLoadingFile refuses a word that is not a number, a negative one, and a missing file")
{
    TempDir tmp;
    FrameSetup fs;
    for (const char* bad : {"\"one\"", "1.5", "-2", "", "abc"})
    {
        INFO("Word = " << bad);
        writeBytes(tmp.file("bad.toml"), std::string("[P]\nWord = ") + bad + "\n");
        CHECK(!fs.tryLoadingFile(tmp.file("bad.toml"), kTestWordsInFrame));
    }
    CHECK(!fs.tryLoadingFile(tmp.file("absent.toml"), kTestWordsInFrame));
    // A file with no sections at all parses to nothing a word map could be.
    writeBytes(tmp.file("bare.toml"), "Word = 1\n");
    CHECK(!fs.tryLoadingFile(tmp.file("bare.toml"), kTestWordsInFrame));
}

TEST_CASE("FrameSetup: the metadata sections and every [EMPTY] placeholder are not parameters")
{
    // These are the reserved names; an [EMPTY] section lets a word map list an unused word (the 4th channel of a
    // 4-channel card) without it becoming a plotted channel, and repeats are fine.
    TempDir tmp;
    writeBytes(tmp.file("reserved.toml"),
               "[Defaults]\nWord = 1\n\n[Frame]\nWord = 1\n\n[Parameters]\nPolarity = 1\n\n[Time]\nWord = 1\n\n"
               "[Receivers]\nCount = 2\n\n[Bounds]\nWord = 1\n\n[L_RCVR1]\nWord = 1\n\n[EMPTY]\nWord = 2\n\n"
               "[R_RCVR1]\nWord = 3\n\n[EMPTY]\nWord = 4\n");
    FrameSetup fs;
    REQUIRE(fs.tryLoadingFile(tmp.file("reserved.toml"), kTestWordsInFrame));
    CHECK(namesInOrder(fs) == Strings{"L_RCVR1", "R_RCVR1"});
    CHECK(fs.getParameter(1)->word == 2);

    // Reserved means exactly these names: a section only LIKE one is a parameter, and needs its Word.
    writeBytes(tmp.file("near.toml"), "[Empty]\nWord = 1\n\n[EMPTY2]\nWord = 2\n");
    REQUIRE(fs.tryLoadingFile(tmp.file("near.toml"), kTestWordsInFrame));
    CHECK(namesInOrder(fs) == Strings{"Empty", "EMPTY2"});
}

TEST_CASE("FrameSetup: loading again replaces what the last load left, even when it fails")
{
    TempDir tmp;
    FrameSetup fs;
    REQUIRE(fs.tryLoadingFile(fixture("test_framesetup.toml"), kTestWordsInFrame));
    REQUIRE(fs.length() == 3);

    writeWordMap(tmp.file("one.toml"), {{"ONLY", 9}});
    REQUIRE(fs.tryLoadingFile(tmp.file("one.toml"), kTestWordsInFrame));
    CHECK(namesInOrder(fs) == Strings{"ONLY"});

    // A load that fails halfway keeps what it read before the bad entry - which is why every caller checks the
    // result, and why saveReceiverParamsFile() clears a failed map before building the default one.
    writeBytes(tmp.file("half.toml"), "[A]\nWord = 1\n\n[B]\nWord = 999\n");
    CHECK(!fs.tryLoadingFile(tmp.file("half.toml"), kTestWordsInFrame));
    CHECK(namesInOrder(fs) == Strings{"A"});
}

TEST_CASE("FrameSetup: writeWordMap writes the correct data")
{
    TempDir tmp;
    FrameSetup fs;
    REQUIRE(fs.tryLoadingFile(fixture("test_framesetup.toml"), kTestWordsInFrame));

    TomlDocument out_doc;
    fs.writeWordMap(out_doc);
    REQUIRE(out_doc.save(tmp.file("out.toml")));

    const TomlDocument in_doc = TomlDocument::load(tmp.file("out.toml"));
    REQUIRE(in_doc.ok());
    CHECK(in_doc.integer("L_RCVR1/Word") == 1);
    CHECK(!in_doc.contains("L_RCVR1/Enabled"));
    CHECK(in_doc.integer("R_RCVR1/Word") == 2);
    CHECK(in_doc.integer("C_RCVR1/Word") == 3);
    CHECK(!in_doc.contains("C_RCVR1/Enabled"));
    // Nothing but the map: one section per parameter, in order.
    CHECK(in_doc.sectionsInFileOrder() == Strings{"L_RCVR1", "R_RCVR1", "C_RCVR1"});
}

TEST_CASE("FrameSetup: tryLoadingFile with a smaller frame accepts")
{
    // test_framesetup.toml has 3 params with Word=1,2,3. A frame with 3 data words + 1 = 4 total words accepts them.
    FrameSetup fs;
    CHECK(fs.tryLoadingFile(fixture("test_framesetup.toml"), 4));
    CHECK(fs.length() == 3);
}

TEST_CASE("FrameSetup: tryLoadingFile with a smaller frame rejects the boundary")
{
    // test_framesetup.toml has Word=3 for C_RCVR1. A frame with 2 data words + 1 = 3 total words rejects it, because
    // parameter_word = 2 and the boundary check is parameter_word >= (3 - 1).
    FrameSetup fs;
    CHECK(!fs.tryLoadingFile(fixture("test_framesetup.toml"), 3));
}

TEST_CASE("FrameSetup: tryLoadingFile with a larger frame accepts")
{
    FrameSetup fs;
    CHECK(fs.tryLoadingFile(fixture("test_framesetup.toml"), 100));
    CHECK(fs.length() == 3);
}

TEST_CASE("FrameSetup: tryLoadingFile with a single-channel frame size")
{
    // One parameter at Word=1; a frame of 1 data word + 1 = 2 accepts it.
    TempDir tmp;
    writeBytes(tmp.file("one.toml"), "[L_RCVR1]\nWord = 1\n");
    FrameSetup fs;
    REQUIRE(fs.tryLoadingFile(tmp.file("one.toml"), 2));
    CHECK(fs.length() == 1);
    CHECK(fs.getParameter(0)->word == 0);
}

TEST_CASE("FrameSetup: tryLoadingFile with a metadata-only file yields no parameters")
{
    // A file with metadata sections but no word-map groups parses, and leaves no parameters behind. Callers must check
    // length(); this pins the contract they rely on, and is exactly the file the dialog's Save used to produce.
    TempDir tmp;
    {
        TomlDocument cfg;
        cfg.setValue("Parameters/Polarity", 1);
        cfg.setValue("Parameters/Slope", 2);
        REQUIRE(cfg.save(tmp.file("meta.toml")));
    }
    REQUIRE(fs::file_size(tmp.file("meta.toml")) > 0);

    FrameSetup fs;
    CHECK(fs.tryLoadingFile(tmp.file("meta.toml"), kTestWordsInFrame));
    CHECK(fs.length() == 0);
}

// ---------------------------------------------------------------------------
// Names.
// ---------------------------------------------------------------------------

TEST_CASE("FrameSetup: channelPrefix names known positions, then falls back")
{
    // The known channel positions, then the CH<n+1> fallback past them. FrameSetup is where the naming lives, and is
    // what the calibration extractor shares with the main run.
    CHECK(FrameSetup::channelPrefix(0) == "L");
    CHECK(FrameSetup::channelPrefix(1) == "R");
    CHECK(FrameSetup::channelPrefix(2) == "C");

    // First index past the known prefixes, and well past them.
    CHECK(FrameSetup::channelPrefix(3) == "CH4");
    CHECK(FrameSetup::channelPrefix(10) == "CH11");
    CHECK(FrameSetup::channelPrefix(99) == "CH100");
    CHECK(FrameSetup::channelPrefix(999) == "CH1000");
    CHECK(FrameSetup::channelPrefix(9999) == "CH10000");
}

TEST_CASE("FrameSetup: receiverParameterName combines prefix and receiver")
{
    CHECK(FrameSetup::receiverParameterName(0, 0) == "L_RCVR1");
    CHECK(FrameSetup::receiverParameterName(1, 2) == "R_RCVR3");
    CHECK(FrameSetup::receiverParameterName(2, 15) == "C_RCVR16");

    // A channel index past the known prefixes carries the fallback through.
    CHECK(FrameSetup::receiverParameterName(3, 0) == "CH4_RCVR1");
    CHECK(FrameSetup::receiverParameterName(5, 4) == "CH6_RCVR5");
}

TEST_CASE("FrameSetup: receiverIndexFromName inverts receiverParameterName")
{
    // Round-trips against the constructor it inverts, across the known channel prefixes and past them (where the
    // prefix becomes "CH<n+1>").
    for (int receiver = 0; receiver < 16; receiver++)
    {
        for (int channel = 0; channel < 5; channel++)
        {
            const std::string name = FrameSetup::receiverParameterName(channel, receiver);
            INFO(name);
            CHECK(FrameSetup::receiverIndexFromName(name) == receiver + 1);
            CHECK(FrameSetup::channelPartOfName(name) == FrameSetup::channelPrefix(channel));
        }
    }

    // A receiver-params TOML may name parameters anything at all. Those must read as "no receiver number" (0) rather
    // than a guess, so the calibration summary falls back to listing names instead of inventing receivers.
    CHECK(FrameSetup::receiverIndexFromName("AGC_LEFT") == 0);
    CHECK(FrameSetup::receiverIndexFromName("") == 0);
    CHECK(FrameSetup::receiverIndexFromName("L_RCVR") == 0);   // token, no number
    CHECK(FrameSetup::receiverIndexFromName("L_RCVR0") == 0);  // receivers are 1-based
    CHECK(FrameSetup::receiverIndexFromName("L_RCVRx2") == 0); // not a bare number
    // An unparseable name keeps its whole self as the "channel part".
    CHECK(FrameSetup::channelPartOfName("AGC_LEFT") == "AGC_LEFT");
    CHECK(FrameSetup::channelPartOfName("L_RCVR0") == "L_RCVR0");   // receiver 0 is no receiver, so no split either
    CHECK(FrameSetup::channelPartOfName("") == "");
}

TEST_CASE("FrameSetup: receiver name parsing matches QString::toInt")
{
    // receiverIndexFromName() used to hand the text after "_RCVR" to QString::toInt, so a name written by hand in a
    // receiver-parameters TOML parsed with toInt's rules. When the helper moved to std::string these were pinned FIRST,
    // against the QString implementation, because a plain from_chars would change every one of them.
    // The token is the LAST "_RCVR": an earlier one is just part of the name.
    CHECK(FrameSetup::receiverIndexFromName("A_RCVR1_RCVR2") == 2);
    CHECK(FrameSetup::channelPartOfName("A_RCVR1_RCVR2") == "A_RCVR1");
    // toInt ignores surrounding whitespace and accepts a leading '+'...
    CHECK(FrameSetup::receiverIndexFromName("L_RCVR 3") == 3);
    CHECK(FrameSetup::receiverIndexFromName("L_RCVR3 ") == 3);
    CHECK(FrameSetup::receiverIndexFromName("L_RCVR+4") == 4);
    // ...but not a second sign, a negative number, trailing junk or an overflow.
    CHECK(FrameSetup::receiverIndexFromName("L_RCVR++4") == 0);
    CHECK(FrameSetup::receiverIndexFromName("L_RCVR-1") == 0);
    CHECK(FrameSetup::receiverIndexFromName("L_RCVR3x") == 0);
    CHECK(FrameSetup::receiverIndexFromName("L_RCVR99999999999") == 0);
    // A name that STARTS with the token has no channel part to split off: the whole name comes back, even though the
    // receiver number parses.
    CHECK(FrameSetup::receiverIndexFromName("_RCVR2") == 2);
    CHECK(FrameSetup::channelPartOfName("_RCVR2") == "_RCVR2");
    // The channel part is everything before the token, whatever it holds.
    CHECK(FrameSetup::channelPartOfName("CH12_RCVR7") == "CH12");
    CHECK(FrameSetup::channelPartOfName("L_RCVR") == "L_RCVR");
    // Non-ASCII names pass through untouched.
    const std::string alpha = "\xCE\x91";   // U+0391, Greek capital alpha
    CHECK(FrameSetup::receiverIndexFromName(alpha + "_RCVR5") == 5);
    CHECK(FrameSetup::channelPartOfName(alpha + "_RCVR5") == alpha);
}

// ---------------------------------------------------------------------------
// The default word map.
// ---------------------------------------------------------------------------

TEST_CASE("FrameSetup: the default receiver map numbers words receiver by receiver")
{
    FrameSetup fs;
    std::string error = "untouched";
    REQUIRE(fs.buildDefaultReceiverMap(2, 3, kTestWordsInFrame, error));
    CHECK(error == "untouched");
    CHECK(namesInOrder(fs) == Strings{"L_RCVR1", "R_RCVR1", "C_RCVR1", "L_RCVR2", "R_RCVR2", "C_RCVR2"});
    for (int i = 0; i < fs.length(); i++)
    {
        CHECK(fs.getParameter(i)->word == i);
    }

    // Four channels per receiver: the fourth takes the CH<n> name, and the stride is four.
    FrameSetup four;
    REQUIRE(four.buildDefaultReceiverMap(2, 4, kTestWordsInFrame, error));
    CHECK(four.getParameter(3)->name == "CH4_RCVR1");
    CHECK(four.getParameter(4)->name == "L_RCVR2");
    CHECK(four.getParameter(4)->word == 4);
}

TEST_CASE("FrameSetup: the default receiver map must leave the sync word free, and needs at least one parameter")
{
    std::string error;
    FrameSetup fits;
    CHECK(fits.buildDefaultReceiverMap(16, 3, 49, error));     // 48 parameters, 49 words: the last slot is the sync word's
    CHECK(fits.length() == 48);

    FrameSetup full;
    error.clear();
    CHECK(!full.buildDefaultReceiverMap(7, 7, 49, error));     // 49 parameters into 49 words
    CHECK(error == "Num Receivers x Receiver Channels exceeds the words available in the minor frame.");
    CHECK(full.length() == 0);

    for (const auto [r, c] : {std::pair{0, 3}, std::pair{3, 0}, std::pair{-1, 3}, std::pair{-2, -2}})
    {
        FrameSetup none;
        error.clear();
        INFO(r << " x " << c);
        CHECK(!none.buildDefaultReceiverMap(r, c, 49, error));
        CHECK(!error.empty());
        CHECK(none.length() == 0);
    }
}

TEST_CASE("FrameSetup: the default receiver map appends to what is already there")
{
    // Documented: it appends. That is why saveReceiverParamsFile() clears a failed load before building it.
    FrameSetup fs;
    fs.addParameter("EXISTING", 40);
    std::string error;
    REQUIRE(fs.buildDefaultReceiverMap(1, 2, kTestWordsInFrame, error));
    CHECK(namesInOrder(fs) == Strings{"EXISTING", "L_RCVR1", "R_RCVR1"});
}

// ---------------------------------------------------------------------------
// Calibration.
// ---------------------------------------------------------------------------

TEST_CASE("FrameSetup: the linear calibration maps the raw range onto the voltage range in dB")
{
    // The four voltage ranges are -10..10, -5..5, 0..10 and 0..5 V; times the dB/V scale they are the dB range the
    // 0..65535 raw counts span. slope is dB per count; scale is the raw offset the worker adds before multiplying.
    // Worked by hand: 0..5 V at 20 dB/V is 0..100 dB, so slope = 100 / 65535 and scale = 0 (positive), or slope =
    // -100 / 65535 and scale = -65535 (negative: the top count reads the bottom of the range).
    struct Case { int polarity; int slope_index; double scale_dB_per_V; double slope; double scale; };
    const Case cases[] = {
        {0, 3, 20.0, 100.0 / 65535.0, 0.0},
        {1, 3, 20.0, -100.0 / 65535.0, -65535.0},
        {0, 0, 20.0, 400.0 / 65535.0, -32767.5},     // -200..200 dB: zero dB sits at mid-scale
        {1, 0, 20.0, -400.0 / 65535.0, -32767.5},
        {0, 1, 10.0, 100.0 / 65535.0, -32767.5},     // -50..50 dB
        {0, 2, 10.0, 100.0 / 65535.0, 0.0},          // 0..100 dB
        {1, 2, 10.0, -100.0 / 65535.0, -65535.0},
        {0, 3, 1.0, 5.0 / 65535.0, 0.0},
    };
    for (const Case& c : cases)
    {
        INFO("polarity " << c.polarity << ", slope index " << c.slope_index << ", " << c.scale_dB_per_V << " dB/V");
        FrameSetup fs;
        fs.addParameter("A", 0);
        fs.addParameter("B", 1);
        fs.getParameter(0)->is_enabled = false;
        fs.getParameter(0)->sample_sum = 123.0;
        fs.applyLinearCalibration(c.polarity, c.slope_index, c.scale_dB_per_V);
        for (int i = 0; i < fs.length(); i++)
        {
            const ParameterInfo* p = fs.getParameter(i);
            CHECK(p->slope == doctest::Approx(c.slope).epsilon(1e-12));
            CHECK(p->scale == doctest::Approx(c.scale).epsilon(1e-12));
            CHECK(p->is_enabled);              // re-enabled, whatever it was
            CHECK(p->sample_sum == 0.0);       // a fresh window
        }
        // What the worker computes from a raw count: (raw + scale) * slope, so the two ends of the raw range land on
        // the two ends of the dB range in the order polarity says.
        const double lowest  = (0.0 + fs.getParameter(1)->scale) * fs.getParameter(1)->slope;
        const double highest = (65535.0 + fs.getParameter(1)->scale) * fs.getParameter(1)->slope;
        const double range_lo = (c.slope_index == 0 ? -10.0 : c.slope_index == 1 ? -5.0 : 0.0) * c.scale_dB_per_V;
        const double range_hi = (c.slope_index == 0 || c.slope_index == 2 ? 10.0 : 5.0) * c.scale_dB_per_V;
        CHECK(lowest == doctest::Approx(c.polarity == 0 ? range_lo : range_hi));
        CHECK(highest == doctest::Approx(c.polarity == 0 ? range_hi : range_lo));
    }
}

TEST_CASE("FrameSetup: the linear calibration leaves names, words and profiles alone")
{
    FrameSetup fs;
    fs.addParameter("L_RCVR1", 7);
    attachProfiles(fs, {{7, makeProfile(true, 3, 1000.0)}});
    fs.applyLinearCalibration(0, 3, 20.0);
    CHECK(fs.getParameter(0)->name == "L_RCVR1");
    CHECK(fs.getParameter(0)->word == 7);
    CHECK(fs.getParameter(0)->profile.valid);
}

TEST_CASE("FrameSetup: attachCalibrationProfiles attaches only valid, matching words")
{
    // The non-linear calibration reaches a parameter by its word index. Three things must NOT attach, each of which
    // would be invisible in the plot: a profile that is not valid (the channel falls back to linear on purpose), a
    // profile for a word the map has no parameter for, and a parameter nobody supplied a profile for.
    FrameSetup setup;
    setup.addParameter("L_RCVR1", 1);
    setup.addParameter("R_RCVR1", 2);
    setup.addParameter("C_RCVR1", 3);
    setup.addParameter("L_RCVR2", 4);

    const CalibrationProfile good_a = makeProfile(true, 3, 1000.0);
    const CalibrationProfile good_b = makeProfile(true, 4, 5000.0);

    const int attached = attachProfiles(setup, {
        {1, good_a},                     // matches word 1, valid     -> attaches
        {2, makeProfile(false, 3, 0.0)}, // matches word 2, NOT valid -> stays linear
        {4, good_b},                     // matches word 4, valid     -> attaches
        {9, makeProfile(true, 3, 0.0)},  // no parameter at word 9    -> ignored
    });                                  // word 3 has no profile at all

    CHECK(attached == 2);

    const ParameterInfo* p1 = setup.getParameter(0);
    CHECK(p1->profile.valid);
    REQUIRE(p1->profile.points.size() == good_a.points.size());
    CHECK(p1->profile.points.front().rawAvg == 1000.0);
    CHECK(p1->profile.points.back().rawAvg == 1200.0);   // the whole profile, not its first point
    CHECK(p1->profile.points.back().trueDb == 12.0);

    CHECK(!setup.getParameter(1)->profile.valid);   // invalid profile not attached
    CHECK(setup.getParameter(1)->profile.points.empty());
    CHECK(!setup.getParameter(2)->profile.valid);   // no profile supplied

    const ParameterInfo* p4 = setup.getParameter(3);
    CHECK(p4->profile.valid);
    CHECK(p4->profile.points.size() == good_b.points.size());
    CHECK(p4->profile.points.front().rawAvg == 5000.0);
}

TEST_CASE("FrameSetup: attachCalibrationProfiles with nothing to attach returns zero")
{
    FrameSetup setup;
    setup.addParameter("L_RCVR1", 1);

    CHECK(attachProfiles(setup, {}) == 0);                                  // nothing supplied
    CHECK(attachProfiles(setup, {{5, makeProfile(true, 3, 0.0)}}) == 0);    // word not in the map
    CHECK(!setup.getParameter(0)->profile.valid);

    FrameSetup empty;                                                       // no parameters
    CHECK(attachProfiles(empty, {{1, makeProfile(true, 3, 0.0)}}) == 0);
}

TEST_CASE("FrameSetup: two parameters on one word both get its profile, and count twice")
{
    FrameSetup setup;
    setup.addParameter("A", 3);
    setup.addParameter("B", 3);
    CHECK(attachProfiles(setup, {{3, makeProfile(true, 2, 10.0)}}) == 2);
    CHECK(setup.getParameter(0)->profile.valid);
    CHECK(setup.getParameter(1)->profile.valid);
}

// ---------------------------------------------------------------------------
// Receiver-parameters files: the word map and the scalars are one file, and saving must produce a file that loading
// accepts (v2.12.1).
// ---------------------------------------------------------------------------

TEST_CASE("FrameSetup: wordsInMinorFrame rounds up")
{
    // A partially filled last word still occupies a word slot, so the division rounds up - the word map's bounds check
    // depends on this.
    CHECK(FrameSetup::wordsInMinorFrame(800) == 50);
    CHECK(FrameSetup::wordsInMinorFrame(801) == 51);
    CHECK(FrameSetup::wordsInMinorFrame(16) == 1);
    CHECK(FrameSetup::wordsInMinorFrame(1) == 1);
    CHECK(FrameSetup::wordsInMinorFrame(0) == 0);
    CHECK(FrameSetup::wordsInMinorFrame(17) == 2);
    CHECK(FrameSetup::wordsInMinorFrame(32) == 2);
}

TEST_CASE("FrameSetup: readReceiverParams reads the shipped [Receivers] block")
{
    // The shipped files carry their receiver counts under [Receivers], which the dialog never read - loading RASA left
    // the counts showing the defaults.
    const ReceiverParams params = FrameSetup::readReceiverParams(shippedReceiverParams("RASA.toml"));
    CHECK(params.numReceivers == 12);
    CHECK(params.receiverChannels == 3);
    CHECK(params.polarityIndex == 1);
    CHECK(params.slopeIndex == 2);
    CHECK(params.scaleDdBPerV == 10.0);
}

TEST_CASE("FrameSetup: readReceiverParams accepts the older [Parameters] keys")
{
    // Files the dialog wrote before this change put the counts under [Parameters].
    TempDir tmp;
    const fs::path path = tmp.file("legacy.toml");
    {
        TomlDocument cfg;
        cfg.setValue("Parameters/NumReceivers", 4);
        cfg.setValue("Parameters/ReceiverChannels", 2);
        REQUIRE(cfg.save(path));
    }

    const ReceiverParams params = FrameSetup::readReceiverParams(path);
    CHECK(params.numReceivers == 4);
    CHECK(params.receiverChannels == 2);

    // A key the file does not carry keeps the value the caller passed in - the dialog passes what it is showing, so a
    // partial file changes only what it actually specifies instead of resetting the rest to the app defaults.
    ReceiverParams on_screen;
    on_screen.polarityIndex    = 1;
    on_screen.scaleDdBPerV     = 33.0;
    on_screen.numReceivers     = 9;
    on_screen.receiverChannels = 5;
    const ReceiverParams merged = FrameSetup::readReceiverParams(path, on_screen);
    CHECK(merged.polarityIndex == 1);       // absent from the file
    CHECK(merged.scaleDdBPerV == 33.0);     // absent from the file
    CHECK(merged.numReceivers == 4);        // the file wins where it speaks
    CHECK(merged.receiverChannels == 2);
}

TEST_CASE("FrameSetup: readReceiverParams prefers [Receivers], and ignores counts and scales that are not positive")
{
    TempDir tmp;
    const fs::path path = tmp.file("both.toml");
    {
        TomlDocument cfg;
        cfg.setValue("Receivers/Count", 6);
        cfg.setValue("Receivers/ChannelsPerReceiver", 4);
        cfg.setValue("Parameters/NumReceivers", 2);       // the older keys lose when both are there
        cfg.setValue("Parameters/ReceiverChannels", 2);
        REQUIRE(cfg.save(path));
    }
    ReceiverParams p = FrameSetup::readReceiverParams(path);
    CHECK(p.numReceivers == 6);
    CHECK(p.receiverChannels == 4);

    ReceiverParams shown;
    shown.numReceivers     = 9;
    shown.receiverChannels = 5;
    shown.scaleDdBPerV     = 33.0;
    for (const char* bad : {"0", "-3"})
    {
        INFO("value " << bad);
        writeBytes(tmp.file("bad.toml"), std::string("[Parameters]\nScale = ") + bad + "\n\n[Receivers]\nCount = " + bad +
                                             "\nChannelsPerReceiver = " + bad + "\n");
        p = FrameSetup::readReceiverParams(tmp.file("bad.toml"), shown);
        CHECK(p.numReceivers == 9);
        CHECK(p.receiverChannels == 5);
        CHECK(p.scaleDdBPerV == 33.0);
    }

    // A file that is not there reads as one that says nothing.
    p = FrameSetup::readReceiverParams(tmp.file("absent.toml"), shown);
    CHECK(p.numReceivers == 9);
    CHECK(p.scaleDdBPerV == 33.0);
    const ReceiverParams defaults = FrameSetup::readReceiverParams(tmp.file("absent.toml"));
    CHECK(defaults.polarityIndex == 0);
    CHECK(defaults.slopeIndex == 3);
    CHECK(defaults.scaleDdBPerV == 20.0);
    CHECK(defaults.numReceivers == 16);
    CHECK(defaults.receiverChannels == 3);
}

TEST_CASE("FrameSetup: a receiver-parameters file round-trips its scalars and word map")
{
    // Save then load: the scalars come back, and so does the word map that was carried over from the file the stream
    // had loaded. Without the map the saved file loads with zero parameters and the stream cannot process.
    // The source map is deliberately NOT the default one for these counts (one receiver of three channels would be
    // L/R/C at words 0..2): an [EMPTY] gap and a fourth parameter, so a save that ignored the source and wrote the
    // default map instead would be seen.
    TempDir tmp;
    const fs::path source = tmp.file("source.toml");
    writeBytes(source, "[L_RCVR1]\nWord = 1\n\n[R_RCVR1]\nWord = 2\n\n[C_RCVR1]\nWord = 3\n\n[EMPTY]\nWord = 4\n\n"
                       "[L_RCVR2]\nWord = 5\n");

    ReceiverParams params;
    params.polarityIndex    = 1;
    params.slopeIndex       = 2;
    params.scaleDdBPerV     = 12.5;
    params.numReceivers     = 1;
    params.receiverChannels = 3;

    const fs::path saved = tmp.file("saved.toml");
    std::string error;
    REQUIRE_MESSAGE(FrameSetup::saveReceiverParamsFile(saved, params, source, kTestWordsInFrame, error), error);

    const ReceiverParams back = FrameSetup::readReceiverParams(saved);
    CHECK(back.polarityIndex == 1);
    CHECK(back.slopeIndex == 2);
    CHECK(back.scaleDdBPerV == 12.5);
    CHECK(back.numReceivers == 1);
    CHECK(back.receiverChannels == 3);

    FrameSetup fs;
    REQUIRE(fs.tryLoadingFile(saved, kTestWordsInFrame));
    CHECK(namesInOrder(fs) == Strings{"L_RCVR1", "R_RCVR1", "C_RCVR1", "L_RCVR2"});   // the [EMPTY] gap is not a parameter
    CHECK(wordsByName(fs) == std::map<std::string, int>{{"L_RCVR1", 0}, {"R_RCVR1", 1}, {"C_RCVR1", 2}, {"L_RCVR2", 4}});
}

TEST_CASE("FrameSetup: a receiver-parameters file falls back to the default word map")
{
    // No file was loaded, so the saved file carries the default map for the counts on screen - the same map the run
    // would have built for them.
    TempDir tmp;
    ReceiverParams params;
    params.numReceivers     = 2;
    params.receiverChannels = 3;

    const fs::path saved = tmp.file("default_map.toml");
    std::string error;
    REQUIRE_MESSAGE(FrameSetup::saveReceiverParamsFile(saved, params, fs::path(), kTestWordsInFrame, error), error);

    FrameSetup fs;
    REQUIRE(fs.tryLoadingFile(saved, kTestWordsInFrame));
    CHECK(fs.length() == 6);

    FrameSetup expected;
    std::string build_error;
    REQUIRE(expected.buildDefaultReceiverMap(2, 3, kTestWordsInFrame, build_error));
    CHECK(wordsByName(fs) == wordsByName(expected));
}

TEST_CASE("FrameSetup: a word map that fails to load is replaced by the default one, not mixed with it")
{
    // A named source that is not there, that will not load (it leaves a partial map behind), or that loads to no
    // parameters: in each case the file written carries the default map for the counts on screen and nothing else.
    TempDir tmp;
    writeBytes(tmp.file("half.toml"), "[STALE]\nWord = 1\n\n[BAD]\nWord = 999\n");
    writeBytes(tmp.file("meta.toml"), "[Parameters]\nPolarity = 1\n");
    ReceiverParams params;
    params.numReceivers     = 2;
    params.receiverChannels = 3;

    FrameSetup expected;
    std::string build_error;
    REQUIRE(expected.buildDefaultReceiverMap(2, 3, kTestWordsInFrame, build_error));

    for (const fs::path& source : {tmp.file("absent.toml"), tmp.file("half.toml"), tmp.file("meta.toml")})
    {
        INFO("source " << source.filename().string());
        const fs::path saved = tmp.file("saved.toml");
        std::string error;
        REQUIRE_MESSAGE(FrameSetup::saveReceiverParamsFile(saved, params, source, kTestWordsInFrame, error), error);
        FrameSetup fs;
        REQUIRE(fs.tryLoadingFile(saved, kTestWordsInFrame));
        CHECK(wordsByName(fs) == wordsByName(expected));
        CHECK(namesInOrder(fs) == namesInOrder(expected));
    }
}

TEST_CASE("FrameSetup: a receiver-parameters file is the five scalars and the map, and replaces what was there")
{
    TempDir tmp;
    const fs::path path = tmp.file("written.toml");
    writeBytes(path, "[OLD_RCVR1]\nWord = 5\n\n[Leftover]\nKey = 1\n");   // a previous file: none of it may survive

    FrameSetup fs;
    fs.addParameter("L_RCVR1", 0);
    fs.addParameter("R_RCVR1", 1);
    ReceiverParams params;
    params.polarityIndex    = 1;
    params.slopeIndex       = 0;
    params.scaleDdBPerV     = 7.25;
    params.numReceivers     = 1;
    params.receiverChannels = 2;
    REQUIRE(fs.writeReceiverParams(path, params));

    const TomlDocument doc = TomlDocument::load(path);
    REQUIRE(doc.ok());
    CHECK(doc.integer("Parameters/Polarity") == 1);
    CHECK(doc.integer("Parameters/Slope") == 0);
    CHECK(doc.number("Parameters/Scale") == 7.25);
    CHECK(doc.integer("Receivers/Count") == 1);
    CHECK(doc.integer("Receivers/ChannelsPerReceiver") == 2);
    CHECK(doc.integer("L_RCVR1/Word") == 1);
    CHECK(doc.integer("R_RCVR1/Word") == 2);
    CHECK(doc.sectionsInFileOrder() == Strings{"Parameters", "Receivers", "L_RCVR1", "R_RCVR1"});
    CHECK(!doc.contains("OLD_RCVR1/Word"));
    CHECK(!doc.contains("Leftover/Key"));
}

TEST_CASE("FrameSetup: a receiver-parameters file is never written without a word map")
{
    // Counts that cannot fit the frame leave no map to write, and a scalars-only file is one the application rejects -
    // so nothing is written at all.
    TempDir tmp;
    ReceiverParams params;
    params.numReceivers     = 40;
    params.receiverChannels = 3;   // 120 words into a 49-word frame

    const fs::path saved = tmp.file("nomap.toml");
    std::string error;
    CHECK(!FrameSetup::saveReceiverParamsFile(saved, params, fs::path(), kTestWordsInFrame, error));
    // The reason is the map's, not a write failure's: nothing was attempted.
    CHECK(error == "Num Receivers x Receiver Channels exceeds the words available in the minor frame.");
    CHECK(!fs::exists(saved));

    // The instance-level writer refuses an empty map for the same reason.
    FrameSetup empty;
    CHECK(!empty.writeReceiverParams(saved, params));
    CHECK(!fs::exists(saved));
}

TEST_CASE("FrameSetup: a receiver-parameters file that cannot be written says so, naming it")
{
    TempDir tmp;
    ReceiverParams params;
    params.numReceivers     = 1;
    params.receiverChannels = 3;
    const fs::path inside_a_missing_folder = tmp.path() / "no such folder" / "params.toml";
    std::string error;
    CHECK(!FrameSetup::saveReceiverParamsFile(inside_a_missing_folder, params, fs::path(), kTestWordsInFrame, error));
    CHECK(error == "Could not write 'params.toml'.");
}
