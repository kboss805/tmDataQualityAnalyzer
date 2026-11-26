/**
 * @file streamjobbuilder.cpp
 * @brief Implementation of buildStreamJob — validation and frame-parameter table for one stream.
 */

#include "streamjobbuilder.h"

#include <memory>
#include <string>

#include "constants.h"
#include "framesetup.h"
#include "receiverparamsfile.h"
#include "framesync.h"

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

    // Set per-stream sample period: 0 = 1 s, 1 = 100 ms, 2 = 10 ms (tmdq_abi.h). Any other index is refused: it used to
    // mean 100 ms in silence, which no host could have meant (the application clamps what it reads).
    switch (cfg.samplePeriodIndex)
    {
        case 0: out_job.params.samplePeriodSec = TimeConstants::kSamplePeriod1s;    break;
        case 1: out_job.params.samplePeriodSec = TimeConstants::kSamplePeriod100ms; break;
        case 2: out_job.params.samplePeriodSec = TimeConstants::kSamplePeriod10ms;  break;
        default:
            error = stream_desc + ": Sample period index is invalid.";
            return false;
    }

    // ---- Frame parameter table (word map + calibration) ----
    out_job.frameSetup = std::make_shared<FrameSetup>();
    if (cfg.mode == StreamMode::FrameSyncLockStats)
    {
        // Lock-only: no receiver parameters needed.
        return true;
    }

    // The word map: the named file, else the shipped default, else the sequential map - resolved exactly as the
    // calibration extractor resolves it, since its profiles are matched to these channels by word.
    WordMapResolution map;
    if (!resolveReceiverWordMap(*out_job.frameSetup, cfg.receiverParamsToml, defaultReceiverParamsFile(context.appRoot),
                                cfg.numReceivers, cfg.receiverChannels, words_in_minor_frame, map))
    {
        error = stream_desc + ": " + map.error;
        return false;
    }

    if (cfg.scaleDdBPerV <= 0)
    {
        error = stream_desc + ": Scale (dB/V) must be a positive number.";
        return false;
    }
    if (cfg.slopeIndex < 0 || cfg.slopeIndex > ReceiverConstants::kMaxSlopeIndex)
    {
        error = stream_desc + ": Slope index is invalid.";
        return false;
    }

    // The linear voltage->dB math lives in FrameSetup so it is single-sourced and unit-testable.
    out_job.frameSetup->applyLinearCalibration(cfg.polarityIndex, cfg.slopeIndex, cfg.scaleDdBPerV);

    // The log is written only once the job is accepted: a passed-over default word map first.
    if (!map.warning.empty())
    {
        report.log.push_back("  " + stream_desc + ": " + map.warning);
    }

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
