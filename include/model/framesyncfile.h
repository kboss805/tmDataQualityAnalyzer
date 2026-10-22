/**
 * @file framesyncfile.h
 * @brief Qt-free reader and writer of the frame-sync pattern file (US1.0 / US2.0 / US5.0).
 *
 * A pattern file carries what describes the recording's PCM code format and framing: the sync pattern,
 * its mask, the frame length in bits, and the two stream-format flags (Invert Data, Derandomize). It
 * does NOT carry Data Rate or Sample Rate - those are per-session operator inputs (US1.0). The dialogs
 * that load and save one, and the Receiver SNR defaults file that has the same layout
 * (`settings/framesync_patterns/framesync_rcvr_default.toml`), read it through here so the format has
 * one implementation (de-Qt, plan step 1.4). Putting the values into widgets stays with the dialogs.
 *
 * Layout: a `[Frame]` section with `FrameSync`, `FrameSyncMask`, `BitsPerFrame`, `Inverted` and
 * `Randomized`; the older `WordsInMinorFrame` (16-bit words) is still read for files saved before
 * `BitsPerFrame`.
 */

#ifndef FRAMESYNCFILE_H
#define FRAMESYNCFILE_H

#include <filesystem>
#include <optional>
#include <string>

/// What a pattern file says. Anything a file does not usefully carry is left empty, so the caller can
/// tell "leave that field alone" from "set it".
struct FrameSyncFile
{
    std::string pattern;               ///< `FrameSync`; empty if absent or empty. As written (not case-folded).
    std::string mask;                  ///< `FrameSyncMask`; empty if absent or empty. As written.
    /// The frame length in bits, only if the file gives a usable one: `BitsPerFrame` when it is at least
    /// PCMConstants::kMinFrameLengthBits, otherwise the older `WordsInMinorFrame` times 16 bits when that
    /// gives a usable length.
    std::optional<int> bitsPerFrame;
    bool bitsFromLegacyKey = false;    ///< @c bitsPerFrame came from `WordsInMinorFrame`.
    bool inverted = false;             ///< `Inverted`; false when absent (and an absent key clears the box).
    /// `Randomized`, only when the file carries the key: every file written before it was persisted - the
    /// shipped PRN patterns included - has none, and defaulting those to false would silently clear a
    /// choice the operator had already made.
    std::optional<bool> randomized;
};

/// Reads @p path. A file that is missing, empty or unreadable reads as a file that says nothing:
/// every field empty and @c inverted false.
FrameSyncFile readFrameSyncFile(const std::filesystem::path& path);

/// Writes a pattern file: the pattern and mask as given, the frame length and the two flags. Data Rate
/// and Sample Rate are deliberately not written. @return false if the file could not be written.
bool writeFrameSyncFile(const std::filesystem::path& path, const std::string& pattern, const std::string& mask,
                        int bitsPerFrame, bool inverted, bool randomized);

#endif // FRAMESYNCFILE_H
