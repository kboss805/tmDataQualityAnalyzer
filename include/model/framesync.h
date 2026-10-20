/**
 * @file framesync.h
 * @brief The raw-affecting acquisition parameters, decoded once, for every run that has to agree.
 *
 * A stream's frame sync arrives from the dialogs as text (a hex pattern and mask, a frame length,
 * two code-format flags, a data rate) and from the file-level byte-order setting. The main
 * processing run and the calibration extraction must see the SAME bitstream, or the profiles
 * the extraction builds are matched against data it never measured: that mirror invariant was
 * broken twice already (the inversion flag, then the byte order), each time by one site setting a
 * field the other forgot. So both build their `ProcessingParams` from this one description, in
 * this one function, and a new raw-affecting field has one place to go.
 *
 * Plain C++: the text is UTF-8 `std::string`; the layers that hold `QString`s convert at their own
 * boundary.
 */

#ifndef FRAMESYNC_H
#define FRAMESYNC_H

#include <cstdint>
#include <string>

#include "framesetup.h"
#include "processingparams.h"
#include "textparse.h"

/// A sync pattern and mask as numbers, with the pattern's length in bits.
struct ResolvedFrameSync
{
    std::uint64_t pattern       = 0;
    std::uint64_t mask          = 0;
    int           patternLength = 0;
};

/// Why a pattern / mask pair could not be resolved.
enum class FrameSyncError
{
    None,
    PatternEmpty,    ///< No pattern at all.
    PatternInvalid,  ///< Not hex, negative, or wider than 64 bits.
    MaskInvalid,     ///< The same, for the mask.
};

/**
 * @brief Decodes a hex sync pattern and mask.
 *
 * Hex is read like `QString::toULongLong(16)` (see `textparse::parseUnsignedHex`). The pattern's
 * length in bits is its CHARACTER count times four, whatever the characters are, so " FE6B " is 24
 * bits: that is what the main run has always done, and the extraction must not differ. An empty
 * mask means "every bit of the pattern".
 *
 * The pattern is checked before the mask. On any error @p out is left untouched.
 */
inline FrameSyncError resolveFrameSync(const std::string& pattern_hex, const std::string& mask_hex,
                                       ResolvedFrameSync& out)
{
    ResolvedFrameSync resolved;
    if (pattern_hex.empty())
    {
        return FrameSyncError::PatternEmpty;
    }
    if (!textparse::parseUnsignedHex(pattern_hex, resolved.pattern))
    {
        return FrameSyncError::PatternInvalid;
    }
    resolved.patternLength = static_cast<int>(pattern_hex.length()) * 4;

    if (mask_hex.empty())
    {
        resolved.mask = fullSyncMask(resolved.patternLength);
    }
    else if (!textparse::parseUnsignedHex(mask_hex, resolved.mask))
    {
        return FrameSyncError::MaskInvalid;
    }
    out = resolved;
    return FrameSyncError::None;
}

/// Everything about how a stream's bits are recovered from the recording. Raw-affecting: two runs
/// that disagree on any of it read different bitstreams.
struct Acquisition
{
    ResolvedFrameSync sync;
    int    bitsInMinorFrame = 0;       ///< Total bits per minor frame, including the sync word.
    bool   randomized       = false;   ///< true = RNRZ-L descrambler; false = NRZ-L.
    bool   inverted         = false;   ///< Bit-for-bit inversion before derandomization.
    double dataRateMbps     = 0.0;     ///< 0 = use the TMATS-derived rate.
    bool   swapBytes        = true;    ///< File-level byte order: the byte-pair swap.
};

/// Copies @p acquisition into @p params. The only place the two runs' raw-affecting fields are set.
inline void applyAcquisition(ProcessingParams& params, const Acquisition& acquisition)
{
    params.frameSync         = acquisition.sync.pattern;
    params.frameSyncMask     = acquisition.sync.mask;
    params.syncPatternLength = acquisition.sync.patternLength;
    params.wordsInMinorFrame = FrameSetup::wordsInMinorFrame(acquisition.bitsInMinorFrame);
    params.bitsInMinorFrame  = acquisition.bitsInMinorFrame;
    params.isRandomized      = acquisition.randomized;
    params.isInverted        = acquisition.inverted;
    params.swapBytes         = acquisition.swapBytes;
    params.dataRateBps       = (acquisition.dataRateMbps > 0.0) ? acquisition.dataRateMbps * 1e6 : 0.0;
}

#endif // FRAMESYNC_H
