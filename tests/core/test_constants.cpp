// constants.h: the application's version, the PCM frame and file constants, the UI defaults, and the calibration
// tuning, pinned by value (ported in plan step 2.4). A constant is pinned when something outside the code depends on
// its value - a file, a key, a TMATS type, a default an operator sees - because a wrong value there compiles, runs and
// looks plausible.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "constants.h"
#include "doctest.h"
#include "framesyncfile.h"
#include "test_support.h"

namespace {

using tmdq_test::contains;
using tmdq_test::readBytes;
using tmdq_test::repoFile;

bool is(const char* actual, std::string_view expected)
{
    return actual != nullptr && std::string_view(actual) == expected;
}

/// A shipped settings file, by the constants that name it.
std::filesystem::path shippedSetting(const char* folder, const char* file)
{
    return repoFile(std::filesystem::path(SettingsConstants::kSettingsDirName) / folder / file);
}

} // namespace

// --- the cases ported in plan step 2.4, each under its old name ------------------------------------------------------

TEST_CASE("Constants: pcmMaxChannelCount")
{
    CHECK(PCMConstants::kMaxChannelCount == 0x10000);
}

TEST_CASE("Constants: pcmDefaultFrameSync")
{
    // PRN15. Both halves are pinned because they are only meaningful as a pair: a pattern with the wrong frame length
    // never locks, and nothing else in the build would catch them drifting apart.
    CHECK(is(PCMConstants::kDefaultFrameSyncLockPattern, "334AABBF"));
    CHECK(PCMConstants::kDefaultFrameSyncLockBits == 32767);
}

TEST_CASE("Constants: pcmCommonWordLen")
{
    CHECK(PCMConstants::kCommonWordLen == 16);
}

TEST_CASE("Constants: pcmNumMinorFrames")
{
    CHECK(PCMConstants::kNumMinorFrames == 1);
}

TEST_CASE("Constants: pcmChannelTypeIdentifiers")
{
    CHECK(is(PCMConstants::kChannelTypeTime, "TIMEIN"));
    CHECK(is(PCMConstants::kChannelTypePcm, "PCMIN"));
}

TEST_CASE("Constants: uiDefaultSlopeIndex")
{
    CHECK(ReceiverConstants::kDefaultSlopeIndex == 3);
}

TEST_CASE("Constants: uiSamplePeriods")
{
    CHECK(TimeConstants::kSamplePeriod1s == 1.0);
    CHECK(TimeConstants::kSamplePeriod100ms == 0.1);
    CHECK(TimeConstants::kSamplePeriod10ms == 0.01);
}

TEST_CASE("Constants: uiChannelPrefixes")
{
    CHECK(is(ReceiverConstants::kChannelPrefixes[0], "L"));
    CHECK(is(ReceiverConstants::kChannelPrefixes[1], "R"));
    CHECK(is(ReceiverConstants::kChannelPrefixes[2], "C"));
}

TEST_CASE("Constants: uiNumKnownPrefixes")
{
    CHECK(ReceiverConstants::kNumKnownPrefixes == 3);   // derived from the array's length
}

TEST_CASE("Constants: appVersionValues")
{
    // No version is typed here (it changes at every release, and the managed suite's VersionTests hold the number to
    // the packaging's), so what can fail is a version that is not one: negative parts, or the 0.0.0 of an unset one.
    CHECK(AppVersion::kMajor >= 0);
    CHECK(AppVersion::kMinor >= 0);
    CHECK(AppVersion::kPatch >= 0);
    CHECK(AppVersion::kMajor + AppVersion::kMinor + AppVersion::kPatch > 0);
}

TEST_CASE("Constants: appVersionToString")
{
    // Built here from the three numbers and nothing else, so no version is typed in this test either.
    const std::string expected = std::to_string(AppVersion::kMajor) + "." + std::to_string(AppVersion::kMinor) + "." +
                                 std::to_string(AppVersion::kPatch);
    CHECK(AppVersion::toString() == expected);
}

TEST_CASE("Constants: pcmMaxRawSampleValue")
{
    CHECK(PCMConstants::kMaxRawSampleValue == static_cast<uint16_t>(0xFFFF));
}

TEST_CASE("Constants: pcmDefaultBufferSize")
{
    CHECK(PCMConstants::kDefaultBufferSize == 65536UL);
}

TEST_CASE("Constants: pcmProgressReportInterval")
{
    CHECK(PCMConstants::kProgressReportIntervalMs == 100);
}

TEST_CASE("Constants: uiTimeConversionConstants")
{
    CHECK(TimeConstants::kSecondsPerDay == 86400);
    CHECK(TimeConstants::kSecondsPerHour == 3600);
    CHECK(TimeConstants::kSecondsPerMinute == 60);
}

TEST_CASE("Constants: uiPolarityConstants")
{
    CHECK(ReceiverConstants::kDefaultPolarityIndex == 0);
}

TEST_CASE("Constants: uiDeploymentConstants")
{
    CHECK(is(SettingsConstants::kSettingsDirName, "settings"));
    CHECK(is(SettingsConstants::kDefaultTomlFilename, "default.toml"));
    CHECK(is(SettingsConstants::kReceiverParamsDirName, "receiver_params"));
    CHECK(is(SettingsConstants::kRcvrCalsDirName, "rcvr_cals"));
}

TEST_CASE("Constants: pcmFrameSyncMaskConstants")
{
    CHECK(is(PCMConstants::kDefaultFrameSyncMask, "FFFFFFFF"));
}

TEST_CASE("Constants: pcmFrameLengthBoundsConstants")
{
    CHECK(PCMConstants::kMinFrameLengthBits == 64);
}

TEST_CASE("Constants: pcmMaxPacketBufferSize")
{
    // Guard against malformed Ch10 headers; 100 MB cap.
    CHECK(static_cast<long long>(PCMConstants::kMaxPacketBufferSize) == 100LL * 1024 * 1024);
}

TEST_CASE("Constants: uiDefaultSamplePeriodIndex")
{
    // Default is 1 s (index 0 in the 1 s / 100 ms / 10 ms combo).
    CHECK(TimeConstants::kDefaultSamplePeriodIndex == 0);
}

TEST_CASE("Constants: uiMaxSlopeIndex")
{
    // Four voltage-range options (0-indexed); max valid index is 3.
    CHECK(ReceiverConstants::kMaxSlopeIndex == 3);
}

TEST_CASE("Constants: uiSlopeVoltageBounds")
{
    // Voltage bounds for each slope index (US2.1, voltage range options).
    CHECK(ReceiverConstants::kSlopeVoltageLower[0] == -10.0);
    CHECK(ReceiverConstants::kSlopeVoltageLower[1] == -5.0);
    CHECK(ReceiverConstants::kSlopeVoltageLower[2] == 0.0);
    CHECK(ReceiverConstants::kSlopeVoltageLower[3] == 0.0);

    CHECK(ReceiverConstants::kSlopeVoltageUpper[0] == 10.0);
    CHECK(ReceiverConstants::kSlopeVoltageUpper[1] == 5.0);
    CHECK(ReceiverConstants::kSlopeVoltageUpper[2] == 10.0);
    CHECK(ReceiverConstants::kSlopeVoltageUpper[3] == 5.0);
}

// --- cases added in the port: each closes a gap the port's mutation check found --------------------------------------
// Every constant in constants.h was altered in turn and run against the whole native suite. These are the ones that
// survived it (the version numbers aside: the version case builds its expectation from them, and the release
// consistency check pins the version across the shipped files). Where a shipped file carries the same value, the case
// reads the file, so the two cannot drift apart unseen.

TEST_CASE("Constants: the Receiver SNR defaults compiled in are the ones the shipped default file holds")
{
    // The dialog loads this file for a Receiver SNR stream's frame sync and falls back to the compiled constants when
    // it is missing or a field is absent - so the fallback must say what the file says, or deleting the file changes
    // the defaults silently.
    const FrameSyncFile file = readFrameSyncFile(shippedSetting("framesync_patterns", "framesync_rcvr_default.toml"));
    CHECK(file.pattern == PCMConstants::kDefaultReceiverSNRPattern);
    CHECK(file.mask == PCMConstants::kDefaultFrameSyncMask);
    REQUIRE(file.bitsPerFrame.has_value());
    CHECK(*file.bitsPerFrame == PCMConstants::kDefaultReceiverSNRBits);

    CHECK(is(PCMConstants::kDefaultReceiverSNRPattern, "FE6B2840"));
    CHECK(PCMConstants::kDefaultReceiverSNRBits == 800);
}

TEST_CASE("Constants: the Frame Sync Lock default is the shipped PRN15 pattern file")
{
    const FrameSyncFile file =
        readFrameSyncFile(shippedSetting("framesync_patterns", "framesync_PRN15.toml"));
    CHECK(file.pattern == PCMConstants::kDefaultFrameSyncLockPattern);
    CHECK(file.mask == PCMConstants::kDefaultFrameSyncMask);
    REQUIRE(file.bitsPerFrame.has_value());
    CHECK(*file.bitsPerFrame == PCMConstants::kDefaultFrameSyncLockBits);
}

TEST_CASE("Constants: each settings folder that has a default ships it under the default name")
{
    // The receiver parameters and the step calibration each fall back to <folder>/default.toml; the frame sync
    // patterns have no default.toml (their default is the file above), so they are not listed.
    for (const char* folder : {SettingsConstants::kReceiverParamsDirName, SettingsConstants::kRcvrCalsDirName})
    {
        INFO(folder);
        CHECK(std::filesystem::is_regular_file(shippedSetting(folder, SettingsConstants::kDefaultTomlFilename)));
    }
}

TEST_CASE("Constants: calibration tuning the detector's own suites cannot tell from a neighbouring value")
{
    // Each of these survived a small change against every recording and synthetic sweep the native suite holds, for a
    // reason that is about the fixtures, not the constant: the start-up period is replaced as soon as the reader
    // knows the bit rate (every recording states one); the relaxation search reaches its floor in four attempts, so
    // a ninth is never taken; no relaxed channel needs a threshold below 2 sigma; and the sample floor only binds at
    // sample periods coarser than any test uses. So they are pinned here, with the relations their comments state.
    CHECK(CalibrationConstants::kExtractSamplePeriodSec == 0.01);
    CHECK(CalibrationConstants::kMinEdgeSigmaMultiple == 1.5);
    CHECK(CalibrationConstants::kMaxEdgeRelaxAttempts == 8);
    CHECK(CalibrationConstants::kMinConfirmSamples == 3);

    CHECK(CalibrationConstants::kMinEdgeSigmaMultiple < CalibrationConstants::kEdgeSigmaMultiple);
    CHECK(CalibrationConstants::kMinAdaptiveExtractPeriodSec < CalibrationConstants::kMaxExtractSamplePeriodSec);
    // "with the factor above this spans roughly a 12x reduction, well past the floor for any realistic sigma": the
    // attempt cap must let the search get from the starting multiple down past the floor.
    CHECK(CalibrationConstants::kEdgeSigmaMultiple *
              std::pow(CalibrationConstants::kEdgeRelaxFactor, CalibrationConstants::kMaxEdgeRelaxAttempts) <
          CalibrationConstants::kMinEdgeSigmaMultiple);
}
