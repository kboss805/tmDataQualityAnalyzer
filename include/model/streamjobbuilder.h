/**
 * @file streamjobbuilder.h
 * @brief Turns one stream's configuration into a processing job: validates it, decodes the frame-sync
 *        fields, and builds the frame-parameter table (word map and calibration).
 *
 * Qt-free core logic (de-Qt, plan step 1.5). The view model holds the user's settings as a
 * `StreamConfig` (QString, QHash) and converts it to a `StreamJobConfig` at the call; what the job is
 * built from, and every message a rejected configuration produces, is decided here.
 */

#ifndef STREAMJOBBUILDER_H
#define STREAMJOBBUILDER_H

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "calibrationprofile.h"
#include "constants.h"
#include "pipelinerunner.h"
#include "processingparams.h"
#include "streammode.h"

/// One stream's settings, as the job builder reads them. Text is UTF-8.
struct StreamJobConfig
{
    int         pcmChannelId = -1;
    std::string label;                 ///< Display label; empty = "Ch <id>".
    StreamMode  mode = StreamMode::FrameSyncLockStats;

    // Frame-sync fields, as typed (hex text) and as chosen.
    std::string syncPattern;
    std::string syncMask;              ///< Empty = every bit of the pattern.
    int         bitsInMinorFrame = PCMConstants::kDefaultBitsPerFrame;
    bool        randomized = false;
    bool        inverted = false;
    double      dataRateMbps = 0.0;    ///< 0 = use the TMATS-declared rate.
    int         samplePeriodIndex = UIConstants::kDefaultSamplePeriodIndex;

    // Receiver SNR only.
    int         polarityIndex = UIConstants::kDefaultPolarityIndex;
    int         slopeIndex = UIConstants::kDefaultSlopeIndex;
    double      scaleDdBPerV = PCMConstants::kDefaultScaleDdBPerV;
    int         numReceivers = PCMConstants::kDefaultNumReceivers;
    int         receiverChannels = PCMConstants::kDefaultReceiverChannels;
    std::filesystem::path receiverParamsToml;   ///< Empty = the shipped default file, else a default word map.
    /// Non-linear step-calibration profiles by zero-based word index.
    std::unordered_map<int, CalibrationProfile> calibrationByWord;
};

/// What is true of the whole recording and the installation rather than of one stream.
struct StreamJobContext
{
    /// The application root (UTF-8), under which `settings/receiver_params/default.toml` is looked for.
    std::string appRoot;
    /// File-scoped byte order: every job built for one recording gets the same value, so it is not
    /// taken from the stream (one file's channels must not disagree about their own byte order).
    bool swapBytes = true;
};

/// What building a job left to say besides the job itself.
struct StreamJobReport
{
    std::string error;                 ///< Why the configuration was rejected; empty on success.
    std::vector<std::string> log;      ///< Lines for the processing log (only on success), in order.
};

/**
 * @brief Builds @p out_job from @p cfg on top of the per-file parameters in @p base.
 *
 * Checks, in this order, and stops at the first that fails: the sync pattern and mask, the frame
 * length, then for a Receiver SNR stream the receiver-parameters file (named, else the default file,
 * else a default word map), the scale and the slope index. Each message leads with the stream's label,
 * or "Ch <id>" when it has none.
 *
 * @return false with the reason in @p report.error; @p out_job is then unspecified.
 */
bool buildStreamJob(const StreamJobConfig& cfg, const ProcessingParams& base, const StreamJobContext& context,
                    StreamJob& out_job, StreamJobReport& report);

#endif // STREAMJOBBUILDER_H
