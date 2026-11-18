// The receiver-parameters file: the five scalar settings and the word map, read and written as one file (ported in
// plan step 2.4 with the FrameSetup suite it came out of), and what a save does when the word map it was asked to keep
// cannot be loaded.

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "framesetup.h"
#include "receiverparamsfile.h"
#include "test_support.h"
#include "tomldocument.h"

using namespace tmdq_test;

namespace {

/// Test frame size: 48 data words + 1 (matches default 16 receivers x 3 channels).
constexpr int kTestWordsInFrame = 49;

fs::path shippedReceiverParams(const std::string& name)
{
    return repoFile(fs::path("settings") / "receiver_params" / name);
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

} // namespace

TEST_CASE("ReceiverParamsFile: readReceiverParams reads the shipped [Receivers] block")
{
    // The shipped files carry their receiver counts under [Receivers], which the dialog never read - loading RASA left
    // the counts showing the defaults.
    const ReceiverParams params = readReceiverParams(shippedReceiverParams("RASA.toml"));
    CHECK(params.numReceivers == 12);
    CHECK(params.receiverChannels == 3);
    CHECK(params.polarityIndex == 1);
    CHECK(params.slopeIndex == 2);
    CHECK(params.scaleDdBPerV == 10.0);
}

TEST_CASE("ReceiverParamsFile: readReceiverParams accepts the older [Parameters] keys")
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

    const ReceiverParams params = readReceiverParams(path);
    CHECK(params.numReceivers == 4);
    CHECK(params.receiverChannels == 2);

    // A key the file does not carry keeps the value the caller passed in - the dialog passes what it is showing, so a
    // partial file changes only what it actually specifies instead of resetting the rest to the app defaults.
    ReceiverParams on_screen;
    on_screen.polarityIndex    = 1;
    on_screen.scaleDdBPerV     = 33.0;
    on_screen.numReceivers     = 9;
    on_screen.receiverChannels = 5;
    const ReceiverParams merged = readReceiverParams(path, on_screen);
    CHECK(merged.polarityIndex == 1);       // absent from the file
    CHECK(merged.scaleDdBPerV == 33.0);     // absent from the file
    CHECK(merged.numReceivers == 4);        // the file wins where it speaks
    CHECK(merged.receiverChannels == 2);
}

TEST_CASE("ReceiverParamsFile: readReceiverParams prefers [Receivers], and ignores counts and scales that are not positive")
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
    ReceiverParams p = readReceiverParams(path);
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
        p = readReceiverParams(tmp.file("bad.toml"), shown);
        CHECK(p.numReceivers == 9);
        CHECK(p.receiverChannels == 5);
        CHECK(p.scaleDdBPerV == 33.0);
    }

    // A file that is not there reads as one that says nothing.
    p = readReceiverParams(tmp.file("absent.toml"), shown);
    CHECK(p.numReceivers == 9);
    CHECK(p.scaleDdBPerV == 33.0);
    const ReceiverParams defaults = readReceiverParams(tmp.file("absent.toml"));
    CHECK(defaults.polarityIndex == 0);
    CHECK(defaults.slopeIndex == 3);
    CHECK(defaults.scaleDdBPerV == 20.0);
    CHECK(defaults.numReceivers == 16);
    CHECK(defaults.receiverChannels == 3);
}

TEST_CASE("ReceiverParamsFile: a receiver-parameters file round-trips its scalars and word map")
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
    REQUIRE_MESSAGE(saveReceiverParamsFile(saved, params, source, kTestWordsInFrame, error), error);

    const ReceiverParams back = readReceiverParams(saved);
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

TEST_CASE("ReceiverParamsFile: a receiver-parameters file falls back to the default word map")
{
    // No file was loaded, so the saved file carries the default map for the counts on screen - the same map the run
    // would have built for them.
    TempDir tmp;
    ReceiverParams params;
    params.numReceivers     = 2;
    params.receiverChannels = 3;

    const fs::path saved = tmp.file("default_map.toml");
    std::string error;
    REQUIRE_MESSAGE(saveReceiverParamsFile(saved, params, fs::path(), kTestWordsInFrame, error), error);

    FrameSetup fs;
    REQUIRE(fs.tryLoadingFile(saved, kTestWordsInFrame));
    CHECK(fs.length() == 6);

    FrameSetup expected;
    std::string build_error;
    REQUIRE(expected.buildDefaultReceiverMap(2, 3, kTestWordsInFrame, build_error));
    CHECK(wordsByName(fs) == wordsByName(expected));
}

TEST_CASE("ReceiverParamsFile: a word map that fails to load is replaced by the default one, not mixed with it")
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
        REQUIRE_MESSAGE(saveReceiverParamsFile(saved, params, source, kTestWordsInFrame, error), error);
        FrameSetup fs;
        REQUIRE(fs.tryLoadingFile(saved, kTestWordsInFrame));
        CHECK(wordsByName(fs) == wordsByName(expected));
        CHECK(namesInOrder(fs) == namesInOrder(expected));
    }
}

TEST_CASE("ReceiverParamsFile: a receiver-parameters file is the five scalars and the map, and replaces what was there")
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
    REQUIRE(writeReceiverParams(path, params, fs));

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

TEST_CASE("ReceiverParamsFile: a receiver-parameters file is never written without a word map")
{
    // Counts that cannot fit the frame leave no map to write, and a scalars-only file is one the application rejects -
    // so nothing is written at all.
    TempDir tmp;
    ReceiverParams params;
    params.numReceivers     = 40;
    params.receiverChannels = 3;   // 120 words into a 49-word frame

    const fs::path saved = tmp.file("nomap.toml");
    std::string error;
    CHECK(!saveReceiverParamsFile(saved, params, fs::path(), kTestWordsInFrame, error));
    // The reason is the map's, not a write failure's: nothing was attempted.
    CHECK(error == "Num Receivers x Receiver Channels exceeds the words available in the minor frame.");
    CHECK(!fs::exists(saved));

    // The instance-level writer refuses an empty map for the same reason.
    FrameSetup empty;
    CHECK(!writeReceiverParams(saved, params, empty));
    CHECK(!fs::exists(saved));
}

TEST_CASE("ReceiverParamsFile: a receiver-parameters file that cannot be written says so, naming it")
{
    TempDir tmp;
    ReceiverParams params;
    params.numReceivers     = 1;
    params.receiverChannels = 3;
    const fs::path inside_a_missing_folder = tmp.path() / "no such folder" / "params.toml";
    std::string error;
    CHECK(!saveReceiverParamsFile(inside_a_missing_folder, params, fs::path(), kTestWordsInFrame, error));
    CHECK(error == "Could not write 'params.toml'.");
}
