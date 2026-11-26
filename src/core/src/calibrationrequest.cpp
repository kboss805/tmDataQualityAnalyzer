/**
 * @file calibrationrequest.cpp
 * @brief Implementation of buildCalibrationRequest - the Extract Calibration dialog's settings as an extraction request.
 */

#include "calibrationrequest.h"

#include <string>
#include <system_error>

#include "constants.h"
#include "framesync.h"
#include "receiverparamsfile.h"
#include "textfile.h"

bool buildCalibrationRequest(const CalibrationInputs& setup, const std::string& appRoot,
                             CalibrationExtractor::Request& out, std::string& error)
{
    CalibrationExtractor::Request req;
    req.calFilename      = setup.calFilename;
    req.timeChannelId    = setup.timeChannelId;
    req.pcmChannelId     = setup.pcmChannelId;
    req.numReceivers     = setup.numReceivers;
    req.receiverChannels = setup.receiverChannels;
    req.steps            = setup.steps;
    req.clipStartSec     = setup.clipStartSec;
    req.clipEndSec       = setup.clipEndSec;

    // The sync text is decoded the way the main run decodes it (framesync.h), so the extraction reads
    // the same bitstream. A field that cannot be read is reported before anything starts.
    Acquisition& acquisition = req.acquisition;
    switch (resolveFrameSync(setup.syncPattern, setup.syncMask, acquisition.sync))
    {
        case FrameSyncError::None:
            break;
        case FrameSyncError::PatternEmpty:
        case FrameSyncError::PatternInvalid:
            error = "Invalid frame sync pattern '" + setup.syncPattern + "'.";
            return false;
        case FrameSyncError::MaskInvalid:
            error = "Invalid frame sync mask '" + setup.syncMask + "'.";
            return false;
    }
    // Refused as the main run refuses it (buildStreamJob), before the recording is opened: a shorter frame is no frame.
    if (setup.bitsInMinorFrame < PCMConstants::kMinFrameLengthBits)
    {
        error = "Bits/Frame must be >= " + std::to_string(PCMConstants::kMinFrameLengthBits) + ".";
        return false;
    }
    acquisition.bitsInMinorFrame = setup.bitsInMinorFrame;
    acquisition.randomized       = setup.randomized;
    acquisition.inverted         = setup.inverted;
    acquisition.dataRateMbps     = setup.dataRateMbps;
    acquisition.swapBytes        = setup.swapBytes;

    // Resolve the word map the SAME way the main processing run does: when no explicit Receiver
    // Parameters file is picked, fall back to the shipped default.toml rather than letting the extractor
    // synthesize a sequential grid. The two passes MUST agree on word indices, because the resulting
    // profiles are matched to plot channels by word - a divergent map silently misattaches every
    // profile and the plot falls back to linear calibration.
    req.receiverParamsToml = setup.receiverParamsToml;
    req.receiverParamsIsDefault = false;
    if (req.receiverParamsToml.empty())
    {
        const std::filesystem::path defaultParams = defaultReceiverParamsFile(appRoot);
        std::error_code ec;   // exists() with an error code never throws on a bad path
        if (std::filesystem::exists(defaultParams, ec))
        {
            req.receiverParamsToml = defaultParams;
            // Passed over for the sequential map if it cannot be used, as the main run does (resolveReceiverWordMap).
            req.receiverParamsIsDefault = true;
        }
    }

    out = std::move(req);
    return true;
}
