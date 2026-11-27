/**
 * @file receiverparamsfile.h
 * @brief Reader and writer of the receiver-parameters file, and the one resolution of a Receiver SNR stream's word map.
 *
 * A receiver-parameters file carries the five scalar settings a Receiver SNR stream's setup dialog shows (polarity, slope range,
 * scale, receiver count, channels per receiver) and a word map (one `[Name]` section per parameter, with its 1-based
 * `Word`). The word map is `FrameSetup`'s to load and write (`tryLoadingFile`, `writeWordMap`); the file as a whole is
 * here, beside `framesyncfile.h`, so that its two halves are read and written in one place: a save that wrote only the
 * scalars produced a file every consumer rejects for having no parameters.
 */

#ifndef RECEIVERPARAMSFILE_H
#define RECEIVERPARAMSFILE_H

#include <filesystem>
#include <string>

#include "constants.h"
#include "framesetup.h"

/**
 * @brief The scalar settings a receiver-parameters TOML carries alongside its word map.
 *
 * These are the five values a Receiver SNR stream's setup dialog shows above the word map.
 */
struct ReceiverParams
{
    int    polarityIndex    = ReceiverConstants::kDefaultPolarityIndex;    ///< 0 = positive, 1 = negative.
    int    slopeIndex       = ReceiverConstants::kDefaultSlopeIndex;       ///< Index into the voltage ranges.
    double scaleDdBPerV     = PCMConstants::kDefaultScaleDdBPerV;    ///< Calibration scale, dB per volt.
    int    numReceivers     = PCMConstants::kDefaultNumReceivers;    ///< Receivers contributing channels.
    int    receiverChannels = PCMConstants::kDefaultReceiverChannels;///< Channels per receiver.
};

/// Reads the scalar settings from a receiver-parameters TOML (the word map is loaded separately, by
/// `FrameSetup::tryLoadingFile()`). A key the file does not carry keeps its value from @p defaults, so a caller can
/// pass what it is already showing and have a partial file change only what it actually specifies.
ReceiverParams readReceiverParams(const std::filesystem::path& path, const ReceiverParams& defaults = ReceiverParams());

/// Writes a complete receiver-parameters TOML: the scalars plus @p setup's word map. Refuses (returns false) when
/// there is no word map to write, and when the file cannot be written.
bool writeReceiverParams(const std::filesystem::path& path, const ReceiverParams& params, const FrameSetup& setup);

/// Writes a complete receiver-parameters file the way a Receiver SNR stream's setup dialog needs it: the word map comes from
/// @p word_map_source when that names a readable file, and otherwise is the default map for the params' receiver
/// counts - i.e. whichever map the stream would actually process with.
/// @return false with a reason in @p error if no usable map could be built or the file could not be written.
bool saveReceiverParamsFile(const std::filesystem::path& path, const ReceiverParams& params,
                            const std::filesystem::path& word_map_source, int num_words_in_minor_frame,
                            std::string& error);

/// `<appRoot>/settings/receiver_params/default.toml`, from UTF-8 @p appRoot: the word map a Receiver SNR stream falls
/// back to when it names none.
std::filesystem::path defaultReceiverParamsFile(const std::string& appRoot);

/// What resolveReceiverWordMap() left to say besides the map.
struct WordMapResolution
{
    std::string error;   ///< Why no map could be had; empty on success.
    std::string warning; ///< Set when an unusable default file was passed over for the sequential map.
};

/**
 * @brief The word map of a Receiver SNR stream, into @p setup (emptied first), resolved the ONE way both passes use: the
 * main run's job builder and the calibration extractor. They must agree, because the extractor's profiles are matched to
 * the main run's channels by word (audit B10).
 *
 * A named @p explicitFile must exist and load ("... was not found." / "Failed to load ... Check the word map."). With
 * none named, @p defaultFile is loaded when it exists; when it is missing, or exists but cannot be used (the warning then
 * names it), the sequential map for @p numReceivers x @p receiverChannels is built. A map with no parameters is an error.
 *
 * @return false with the reason in @p out.error.
 */
bool resolveReceiverWordMap(FrameSetup& setup, const std::filesystem::path& explicitFile,
                            const std::filesystem::path& defaultFile, int numReceivers, int receiverChannels,
                            int wordsInMinorFrame, WordMapResolution& out);

#endif // RECEIVERPARAMSFILE_H
