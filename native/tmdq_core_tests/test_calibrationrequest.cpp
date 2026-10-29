// buildCalibrationRequest: what the Extract Calibration dialog (or a host) asks the extractor to do, built from the
// stream's settings as typed. No Qt; ported from the Qt TestCalibrationRequest, whose cases are all here under the same
// descriptions. The Qt suite kept the dialog's old inline code (QString, QFileInfo) beside the new function as an
// oracle for the move; the same rule is written here over std::string and std::filesystem, so the comparison over
// generated inputs stays and Qt is not needed for it.

#include <cstdint>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "calibrationrequest.h"
#include "constants.h"
#include "doctest.h"
#include "framesync.h"
#include "test_support.h"
#include "textfile.h"

namespace {

using tmdq_test::TempDir;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// The code as it was, in the Extract Calibration dialog (src/view/streamsubdialogs.cpp), with its inputs taken from a
// CalibrationInputs instead of the dialog's members, and its Qt types turned into the standard ones. Not used by the
// application any more.
// ---------------------------------------------------------------------------

struct OldResult
{
    bool                          ok = false;
    std::string                   error;
    CalibrationExtractor::Request req;
};

OldResult oldInlineCode(const CalibrationInputs& setup, const std::string& appRoot)
{
    OldResult result;
    CalibrationExtractor::Request req;
    req.calFilename      = setup.calFilename;
    req.timeChannelId    = setup.timeChannelId;
    req.pcmChannelId     = setup.pcmChannelId;
    req.numReceivers     = setup.numReceivers;
    req.receiverChannels = setup.receiverChannels;
    req.steps            = setup.steps;
    req.clipStartSec     = setup.clipStartSec;
    req.clipEndSec       = setup.clipEndSec;

    Acquisition& acquisition = req.acquisition;
    switch (resolveFrameSync(setup.syncPattern, setup.syncMask, acquisition.sync))
    {
        case FrameSyncError::None:
            break;
        case FrameSyncError::PatternEmpty:
        case FrameSyncError::PatternInvalid:
            result.error = "Invalid frame sync pattern '" + setup.syncPattern + "'.";
            return result;
        case FrameSyncError::MaskInvalid:
            result.error = "Invalid frame sync mask '" + setup.syncMask + "'.";
            return result;
    }
    acquisition.bitsInMinorFrame = setup.bitsInMinorFrame;
    acquisition.randomized       = setup.randomized;
    acquisition.inverted         = setup.inverted;
    acquisition.dataRateMbps     = setup.dataRateMbps;
    acquisition.swapBytes        = setup.swapBytes;

    std::string receiverParams = setup.receiverParamsToml.string();
    if (receiverParams.empty())
    {
        const std::string defaultRcvrParams = appRoot + "/" + UIConstants::kSettingsDirName + "/" +
                                              UIConstants::kReceiverParamsDirName + "/" +
                                              UIConstants::kDefaultTomlFilename;
        std::error_code ec;
        if (fs::exists(textfile::pathFromUtf8(defaultRcvrParams), ec))
        {
            receiverParams = defaultRcvrParams;
        }
    }
    req.receiverParamsToml = textfile::pathFromUtf8(receiverParams);

    result.ok  = true;
    result.req = req;
    return result;
}

/// Every field of a request, as text, so two requests compare in one line.
std::string describe(const CalibrationExtractor::Request& r)
{
    const Acquisition& a = r.acquisition;
    std::string steps;
    for (const StepDefinition& s : r.steps)
    {
        steps += (steps.empty() ? "" : ",") + std::format("{:.17g}", s.db);
    }
    return std::format("cal={} time={} pcm={} pattern={:x} mask={:x} len={} bits={} rnd={} inv={} rate={:.17g} swap={} "
                       "params={} rcv={} ch={} steps={} clip={:.17g},{:.17g}",
                       r.calFilename.generic_string(), r.timeChannelId, r.pcmChannelId, a.sync.pattern, a.sync.mask,
                       a.sync.patternLength, a.bitsInMinorFrame, a.randomized, a.inverted, a.dataRateMbps,
                       a.swapBytes, r.receiverParamsToml.generic_string(), r.numReceivers, r.receiverChannels, steps,
                       r.clipStartSec, r.clipEndSec);
}

CalibrationInputs typicalSetup()
{
    CalibrationInputs s;
    s.calFilename      = "cal.ch10";
    s.timeChannelId    = 1;
    s.pcmChannelId     = 26;
    s.syncPattern      = "FE6B2840";
    s.bitsInMinorFrame = 800;
    s.dataRateMbps     = 0.8;
    s.numReceivers     = 16;
    s.receiverChannels = 3;
    s.steps            = { {0.0}, {10.0}, {20.0} };
    s.clipStartSec     = 1.5;
    s.clipEndSec       = 2.5;
    return s;
}

/// An application root, with or without the shipped default word map. @p folder lets a case put the root under a
/// name of its choice.
struct AppRoot
{
    TempDir dir;
    fs::path root;

    explicit AppRoot(bool withDefault, const std::u8string& folder = std::u8string())
    {
        root = folder.empty() ? dir.path() : dir.path() / fs::path(folder);
        if (withDefault)
        {
            fs::create_directories(root / "settings" / "receiver_params");
            tmdq_test::writeBytes(root / "settings" / "receiver_params" / "default.toml", "[L_RCVR1]\nWord = 1\n");
        }
        else
        {
            fs::create_directories(root);
        }
    }

    std::string path() const
    {
        const std::u8string text = root.u8string();
        return std::string(text.begin(), text.end());
    }
};

} // namespace

TEST_CASE("CalibrationRequest: the sync text is decoded like the main run")
{
    const AppRoot root(false);
    CalibrationExtractor::Request req;
    std::string error;

    CalibrationInputs s = typicalSetup();
    REQUIRE_MESSAGE(buildCalibrationRequest(s, root.path(), req, error), error);
    CHECK(req.acquisition.sync.pattern == std::uint64_t{0xFE6B2840});
    CHECK(req.acquisition.sync.patternLength == 32);
    CHECK(req.acquisition.sync.mask == std::uint64_t{0xFFFFFFFF});   // no mask: every bit of the pattern

    // The length is the CHARACTER count times four, whatever the characters are.
    s.syncPattern = " FE6B ";
    REQUIRE_MESSAGE(buildCalibrationRequest(s, root.path(), req, error), error);
    CHECK(req.acquisition.sync.patternLength == 24);
    CHECK(req.acquisition.sync.pattern == std::uint64_t{0xFE6B});

    s.syncPattern = "FE6B2840";
    s.syncMask    = "FFFF0000";
    REQUIRE_MESSAGE(buildCalibrationRequest(s, root.path(), req, error), error);
    CHECK(req.acquisition.sync.mask == std::uint64_t{0xFFFF0000});
}

TEST_CASE("CalibrationRequest: a sync pattern or mask that cannot be read is refused with the dialog's message")
{
    const AppRoot root(false);
    CalibrationExtractor::Request req;
    std::string error;

    CalibrationInputs s = typicalSetup();
    s.syncPattern = "";
    CHECK(!buildCalibrationRequest(s, root.path(), req, error));
    CHECK(error == "Invalid frame sync pattern ''.");

    s.syncPattern = "not hex";
    CHECK(!buildCalibrationRequest(s, root.path(), req, error));
    CHECK(error == "Invalid frame sync pattern 'not hex'.");

    s.syncPattern = "FFFFFFFFFFFFFFFFF";   // wider than 64 bits
    CHECK(!buildCalibrationRequest(s, root.path(), req, error));
    CHECK(error == "Invalid frame sync pattern 'FFFFFFFFFFFFFFFFF'.");

    s.syncPattern = "FE6B2840";
    s.syncMask    = "xyz";
    CHECK(!buildCalibrationRequest(s, root.path(), req, error));
    CHECK(error == "Invalid frame sync mask 'xyz'.");

    // The pattern is checked first.
    s.syncPattern = "zz";
    CHECK(!buildCalibrationRequest(s, root.path(), req, error));
    CHECK(error == "Invalid frame sync pattern 'zz'.");
}

TEST_CASE("CalibrationRequest: a failed build leaves the request alone")
{
    const AppRoot root(false);
    CalibrationExtractor::Request req;
    std::string error;
    REQUIRE(buildCalibrationRequest(typicalSetup(), root.path(), req, error));
    const std::string before = describe(req);

    // Refused for the pattern, and refused for the mask (the two return from different places).
    CalibrationInputs badPattern = typicalSetup();
    badPattern.pcmChannelId = 99;
    badPattern.syncPattern  = "nope";
    CHECK(!buildCalibrationRequest(badPattern, root.path(), req, error));
    CHECK(describe(req) == before);

    CalibrationInputs badMask = typicalSetup();
    badMask.pcmChannelId = 98;
    badMask.syncMask     = "xyz";
    badMask.steps        = { {1.0} };
    CHECK(!buildCalibrationRequest(badMask, root.path(), req, error));
    CHECK(describe(req) == before);
}

TEST_CASE("CalibrationRequest: every other field is carried across")
{
    const AppRoot root(false);
    CalibrationInputs s = typicalSetup();
    s.timeChannelId      = 7;
    s.pcmChannelId       = 9;
    s.randomized         = true;
    s.inverted           = true;
    s.swapBytes          = false;
    s.dataRateMbps       = 1.25;
    s.bitsInMinorFrame   = 8000;
    s.numReceivers       = 4;
    s.receiverChannels   = 2;
    s.clipStartSec       = 0.25;
    s.clipEndSec         = 0.75;
    s.steps              = { {-5.0}, {5.0} };
    s.calFilename        = "dir/some cal.ch10";
    s.receiverParamsToml = "words.toml";

    CalibrationExtractor::Request req;
    std::string error;
    REQUIRE_MESSAGE(buildCalibrationRequest(s, root.path(), req, error), error);
    CHECK(req.calFilename == fs::path("dir/some cal.ch10"));
    CHECK(req.timeChannelId == 7);
    CHECK(req.pcmChannelId == 9);
    CHECK(req.acquisition.randomized);
    CHECK(req.acquisition.inverted);
    CHECK(!req.acquisition.swapBytes);
    CHECK(req.acquisition.dataRateMbps == 1.25);
    CHECK(req.acquisition.bitsInMinorFrame == 8000);
    CHECK(req.numReceivers == 4);
    CHECK(req.receiverChannels == 2);
    CHECK(req.clipStartSec == 0.25);
    CHECK(req.clipEndSec == 0.75);
    REQUIRE(req.steps.size() == 2);
    CHECK(req.steps[0].db == -5.0);
    CHECK(req.steps[1].db == 5.0);
    CHECK(req.receiverParamsToml == fs::path("words.toml"));
}

TEST_CASE("CalibrationRequest: the default word map is used only when none is named and the app root has one")
{
    const AppRoot withDefault(true);
    const AppRoot without(false);
    CalibrationExtractor::Request req;
    std::string error;
    CalibrationInputs s = typicalSetup();

    // None named, the root has one: it is used.
    CHECK(buildCalibrationRequest(s, withDefault.path(), req, error));
    CHECK(!req.receiverParamsToml.empty());
    CHECK(req.receiverParamsToml.filename().string() == "default.toml");
    CHECK(fs::exists(req.receiverParamsToml));

    // None named, the root has none: the request names no file (the extractor builds its grid).
    CHECK(buildCalibrationRequest(s, without.path(), req, error));
    CHECK(req.receiverParamsToml.empty());

    // A named file wins, whether or not the root has a default - and is not checked for existence here.
    s.receiverParamsToml = "named.toml";
    CHECK(buildCalibrationRequest(s, withDefault.path(), req, error));
    CHECK(req.receiverParamsToml == fs::path("named.toml"));
    CHECK(buildCalibrationRequest(s, without.path(), req, error));
    CHECK(req.receiverParamsToml == fs::path("named.toml"));

    // An empty or unusable app root is no root: nothing is found.
    s.receiverParamsToml.clear();
    CHECK(buildCalibrationRequest(s, std::string(), req, error));
    CHECK(req.receiverParamsToml.empty());
    CHECK(buildCalibrationRequest(s, "Z:/no/such/place/at/all", req, error));
    CHECK(req.receiverParamsToml.empty());
}

TEST_CASE("CalibrationRequest: an application root with non-ASCII text in its name still finds the default word map")
{
    // The root arrives as UTF-8 text; the path is built from it by pathFromUtf8, not by the process code page.
    const AppRoot root(true, u8"Kalibrierung éè 日本");
    CalibrationExtractor::Request req;
    std::string error;
    REQUIRE(buildCalibrationRequest(typicalSetup(), root.path(), req, error));
    CHECK(req.receiverParamsToml.filename().string() == "default.toml");
    CHECK(fs::exists(req.receiverParamsToml));
    CHECK(req.receiverParamsToml.parent_path().parent_path().parent_path() == root.root);
}

TEST_CASE("CalibrationRequest: the whole request, field by field, for a typical setup")
{
    // One request read out in full, so a field carried to the wrong place (or not at all) shows by its value.
    const AppRoot root(true);
    CalibrationExtractor::Request req;
    std::string error;
    REQUIRE(buildCalibrationRequest(typicalSetup(), root.path(), req, error));
    const std::string params = req.receiverParamsToml.generic_string();
    CHECK(describe(req) ==
          "cal=cal.ch10 time=1 pcm=26 pattern=fe6b2840 mask=ffffffff len=32 bits=800 rnd=false inv=false rate=0.80000000000000004 "
          "swap=true params=" + params + " rcv=16 ch=3 steps=0,10,20 clip=1.5,2.5");
}

TEST_CASE("CalibrationRequest: the new code agrees with the old inline code over generated inputs")
{
    const AppRoot withDefault(true);
    const AppRoot without(false);
    const std::vector<std::string> patterns = { "FE6B2840", "", "zz", " FE6B ", "FFFFFFFFFFFFFFFFF", "-1", "0x1F", "+AB", "a345ca5c", "334AABBF" };
    const std::vector<std::string> masks = { "", "FFFF0000", "xyz", "0xFF", "FFFFFFFFFFFFFFFFF", "00000000", " F0 " };
    const std::vector<std::string> params = { "", "words.toml", "dir/other words.toml" };
    const std::vector<std::string> roots = { withDefault.path(), without.path(), std::string(), "Z:/nowhere" };

    std::mt19937 rng(20261022);
    auto pick = [&](const auto& v) -> const auto& { return v[rng() % v.size()]; };

    int compared = 0, accepted = 0, refused = 0, usedDefault = 0;
    for (int i = 0; i < 3000; ++i)
    {
        CalibrationInputs s;
        s.calFilename        = "cal" + std::to_string(rng() % 5) + ".ch10";
        s.timeChannelId      = static_cast<int>(rng() % 4) - 1;
        s.pcmChannelId       = static_cast<int>(rng() % 40);
        s.syncPattern        = pick(patterns);
        s.syncMask           = pick(masks);
        s.bitsInMinorFrame   = static_cast<int>(rng() % 9000);
        s.randomized         = rng() % 2;
        s.inverted           = rng() % 2;
        s.dataRateMbps       = static_cast<double>(rng() % 40) / 8.0;
        s.swapBytes          = rng() % 2;
        s.receiverParamsToml = pick(params);
        s.numReceivers       = static_cast<int>(rng() % 20);
        s.receiverChannels   = static_cast<int>(rng() % 5);
        const int steps = static_cast<int>(rng() % 4);
        for (int k = 0; k < steps; ++k)
        {
            s.steps.push_back({ static_cast<double>(static_cast<int>(rng() % 80) - 20) / 2.0 });
        }
        s.clipStartSec = static_cast<double>(rng() % 100) / 10.0;
        s.clipEndSec   = static_cast<double>(rng() % 100) / 10.0;
        const std::string& root = pick(roots);

        const OldResult old = oldInlineCode(s, root);
        CalibrationExtractor::Request req;
        std::string error;
        const bool ok = buildCalibrationRequest(s, root, req, error);

        INFO("input " << i << ": ok " << ok << " vs old " << old.ok);
        REQUIRE(ok == old.ok);
        if (ok)
        {
            REQUIRE(describe(req) == describe(old.req));
            ++accepted;
            if (s.receiverParamsToml.empty() && !req.receiverParamsToml.empty())
            {
                ++usedDefault;
            }
        }
        else
        {
            REQUIRE(error == old.error);
            ++refused;
        }
        ++compared;
    }
    // The generator really reached each branch.
    INFO(accepted << " accepted, " << refused << " refused, " << usedDefault << " used the default");
    CHECK((accepted > 300 && refused > 300 && usedDefault > 30));
    CHECK(compared == 3000);
}
