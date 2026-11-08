/**
 * @file streamjobbuilder.cpp
 * @brief Implementation of buildStreamJob — validation and frame-parameter table for one stream.
 */

#include "streamjobbuilder.h"

#include <memory>
#include <system_error>

#include "framesetup.h"
#include "framesync.h"
#include "textfile.h"

bool buildStreamJob(const StreamJobConfig& cfg, const ProcessingParams& base, const StreamJobContext& context,
                    StreamJob& out_job, StreamJobReport& report)
{
    std::string& error = report.error;
    const std::string stream_desc = cfg.label.empty() ? ("Ch " + std::to_string(cfg.pcmChannelId)) : cfg.label;

    // ---- Validate and decode inline frame sync fields ----
    // The decode and the assignment below are shared with the calibration extraction, which
    // must read the same bitstream (framesync.h).
    Acquisition acquisition;
    switch (resolveFrameSync(cfg.syncPattern, cfg.syncMask, acquisition.sync))
    {
        case FrameSyncError::None:
            break;
        case FrameSyncError::PatternEmpty:
            error = stream_desc + ": A frame sync pattern is required.";
            return false;
        case FrameSyncError::PatternInvalid:
            error = stream_desc + ": Invalid frame sync pattern '" + cfg.syncPattern + "'.";
            return false;
        case FrameSyncError::MaskInvalid:
            error = stream_desc + ": Invalid frame sync mask '" + cfg.syncMask + "'.";
            return false;
    }

    const int bits_in_minor_frame = cfg.bitsInMinorFrame;
    if (bits_in_minor_frame < PCMConstants::kMinFrameLengthBits)
    {
        error = stream_desc + ": Bits/Frame must be >= " + std::to_string(PCMConstants::kMinFrameLengthBits) + ".";
        return false;
    }
    // Derive word count for the frame-setup word-map loader.
    const int words_in_minor_frame = FrameSetup::wordsInMinorFrame(bits_in_minor_frame);

    acquisition.bitsInMinorFrame = bits_in_minor_frame;
    acquisition.randomized       = cfg.randomized;
    acquisition.inverted         = cfg.inverted;
    acquisition.dataRateMbps     = cfg.dataRateMbps;
    acquisition.swapBytes        = context.swapBytes;

    // ---- Assemble the per-stream ProcessingParams ----
    out_job.params = base;
    out_job.params.pcmChannelId = cfg.pcmChannelId;
    applyAcquisition(out_job.params, acquisition);
    out_job.params.mode         = cfg.mode;
    out_job.params.streamLabel  = stream_desc;

    // Set per-stream sample period.
    switch (cfg.samplePeriodIndex)
    {
        case 0: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod1s;    break;
        case 1: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod100ms; break;
        case 2: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod10ms;  break;
        default: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod100ms; break;
    }

    // ---- Frame parameter table (word map + calibration) ----
    out_job.frameSetup = std::make_shared<FrameSetup>();
    if (cfg.mode == StreamMode::FrameSyncLockStats)
    {
        // Lock-only: no receiver parameters needed.
        return true;
    }

    std::error_code ec; // exists() with an error code never throws on a bad path
    if (cfg.receiverParamsToml.empty())
    {
        // No Receiver Parameters TOML provided: fall back to the shipped default
        // Receiver Parameters file rather than synthesizing a sequential word map.
        const std::filesystem::path default_rcvr_params = textfile::pathFromUtf8(
            context.appRoot + "/" + UIConstants::kSettingsDirName + "/" + UIConstants::kReceiverParamsDirName + "/" +
            UIConstants::kDefaultTomlFilename);
        if (std::filesystem::exists(default_rcvr_params, ec) &&
            out_job.frameSetup->tryLoadingFile(default_rcvr_params, words_in_minor_frame))
        {
            // Loaded successfully from the default file.
        }
        else
        {
            // Default file missing or unusable: build the default word map (sequential words for
            // NumReceivers x ReceiverChannels parameters). A rejected load can leave a partial map
            // behind and buildDefaultReceiverMap appends, so start from an empty one.
            out_job.frameSetup->clearParameters();
            std::string map_error;
            if (!out_job.frameSetup->buildDefaultReceiverMap(cfg.numReceivers, cfg.receiverChannels,
                                                             words_in_minor_frame, map_error))
            {
                error = stream_desc + ": " + map_error;
                return false;
            }
        }
    }
    else if (!std::filesystem::exists(cfg.receiverParamsToml, ec))
    {
        error = stream_desc + ": Receiver Parameters file '" + textfile::utf8FileName(cfg.receiverParamsToml) +
                "' was not found.";
        return false;
    }
    else if (!out_job.frameSetup->tryLoadingFile(cfg.receiverParamsToml, words_in_minor_frame))
    {
        error = stream_desc + ": Failed to load Receiver Parameters from '" +
                textfile::utf8FileName(cfg.receiverParamsToml) + "'. Check the word map.";
        return false;
    }
    if (out_job.frameSetup->length() == 0)
    {
        error = stream_desc + ": Receiver Parameters file contains no parameters.";
        return false;
    }

    if (cfg.scaleDdBPerV <= 0)
    {
        error = stream_desc + ": Scale (dB/V) must be a positive number.";
        return false;
    }
    if (cfg.slopeIndex < 0 || cfg.slopeIndex > UIConstants::kMaxSlopeIndex)
    {
        error = stream_desc + ": Slope index is invalid.";
        return false;
    }

    // The linear voltage->dB math lives in FrameSetup so it is single-sourced and unit-testable.
    out_job.frameSetup->applyLinearCalibration(cfg.polarityIndex, cfg.slopeIndex, cfg.scaleDdBPerV);

    // Attach non-linear step-calibration profiles (US5.3) by word index.
    if (!cfg.calibrationByWord.empty())
    {
        const int attached = out_job.frameSetup->attachCalibrationProfiles(cfg.calibrationByWord);
        // Surface whether profiles actually reached the word map: provided > 0 but
        // attached == 0 means a word-index mismatch and a silent fall back to linear.
        report.log.push_back("  " + stream_desc + ": non-linear calibration \xE2\x80\x94 provided " +
                             std::to_string(cfg.calibrationByWord.size()) + " profile(s), attached " +
                             std::to_string(attached) + " to word map.");
    }
    else
    {
        report.log.push_back("  " + stream_desc +
                             ": no non-linear calibration profiles configured \xE2\x80\x94 using linear calibration.");
    }

    return true;
}
