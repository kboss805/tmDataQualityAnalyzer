/**
 * @file calibrationrequest.h
 * @brief Turns what the Extract Calibration dialog knows into a `CalibrationExtractor::Request`.
 *
 * Qt-free core logic (plan step 2.2b). The dialog used to do this inline: decode the sync text the way
 * the main run decodes it (so the extraction reads the same bitstream), and fall back to the shipped
 * default word map when the stream names none (so both passes use the same word indices, or the profiles
 * attach to the wrong channels). Both are rules about the data, so they live in the core where the C ABI
 * and the Qt dialog call the same code.
 */

#ifndef CALIBRATIONREQUEST_H
#define CALIBRATIONREQUEST_H

#include <filesystem>
#include <string>
#include <vector>

#include "calibrationextractor.h"
#include "calibrationprofile.h"

/// What the dialog (or a host) knows about one calibration: the recording, the stream's settings as
/// they were typed, and the steps and clips. Text is UTF-8.
struct CalibrationInputs
{
    std::filesystem::path calFilename;      ///< The calibration .ch10 file.
    int         timeChannelId = -1;
    int         pcmChannelId  = -1;

    // The stream's acquisition, as typed. Must be the same values the main run uses.
    std::string syncPattern;                ///< Hex text.
    std::string syncMask;                   ///< Hex text; empty = every bit of the pattern.
    int         bitsInMinorFrame = 0;
    bool        randomized = false;
    bool        inverted = false;
    double      dataRateMbps = 0.0;         ///< 0 = use the TMATS rate.
    bool        swapBytes = true;           ///< File-scoped byte order.

    std::filesystem::path receiverParamsToml;   ///< Empty = the shipped default word map, if the app root has one.
    int         numReceivers = 0;           ///< Sequential-grid fallback, used only when no word-map file is resolved.
    int         receiverChannels = 0;

    std::vector<StepDefinition> steps;
    double      clipStartSec = 0.0;
    double      clipEndSec = 0.0;
};

/**
 * @brief Builds @p out from @p setup.
 *
 * The sync pattern and mask are decoded by `resolveFrameSync` (so " FE6B " is 24 bits, exactly as in the
 * main run); a pattern that is empty or not hex fails with "Invalid frame sync pattern '<text>'.", and a
 * mask that is not hex with "Invalid frame sync mask '<text>'.". When @p setup names no receiver-parameters
 * file, `<appRoot>/settings/receiver_params/default.toml` is used if it exists (and the request names no file
 * if it does not, leaving the extractor its sequential grid).
 *
 * @return false with the reason in @p error; @p out is then untouched.
 */
bool buildCalibrationRequest(const CalibrationInputs& setup, const std::string& appRoot,
                             CalibrationExtractor::Request& out, std::string& error);

#endif // CALIBRATIONREQUEST_H
