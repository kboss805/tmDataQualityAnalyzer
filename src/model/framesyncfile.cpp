/**
 * @file framesyncfile.cpp
 * @brief Implementation of the frame-sync pattern file reader and writer.
 */

#include "framesyncfile.h"

#include "constants.h"
#include "tomldocument.h"

namespace {

constexpr const char* kPatternKey      = "Frame/FrameSync";
constexpr const char* kMaskKey         = "Frame/FrameSyncMask";
constexpr const char* kBitsKey         = "Frame/BitsPerFrame";
constexpr const char* kLegacyWordsKey  = "Frame/WordsInMinorFrame";
constexpr const char* kInvertedKey     = "Frame/Inverted";
constexpr const char* kRandomizedKey   = "Frame/Randomized";

} // namespace

FrameSyncFile readFrameSyncFile(const std::filesystem::path& path)
{
    const TomlDocument cfg = TomlDocument::load(path);

    FrameSyncFile file;
    file.pattern = cfg.string(kPatternKey);
    file.mask    = cfg.string(kMaskKey);

    int bits = cfg.integer(kBitsKey, 0);
    if (bits < PCMConstants::kMinFrameLengthBits)
    {
        // Fall back to the old WordsInMinorFrame key (words x 16 bits).
        const int words = cfg.integer(kLegacyWordsKey, 0);
        if (words >= 2)
        {
            bits = words * PCMConstants::kCommonWordLen;
            file.bitsFromLegacyKey = true;
        }
    }
    if (bits >= PCMConstants::kMinFrameLengthBits)
    {
        file.bitsPerFrame = bits;
    }
    else
    {
        file.bitsFromLegacyKey = false;
    }

    file.inverted = cfg.boolean(kInvertedKey, false);
    if (cfg.contains(kRandomizedKey))
    {
        file.randomized = cfg.boolean(kRandomizedKey);
    }
    return file;
}

bool writeFrameSyncFile(const std::filesystem::path& path, const std::string& pattern, const std::string& mask,
                        int bitsPerFrame, bool inverted, bool randomized)
{
    TomlDocument cfg;
    cfg.setValue(kPatternKey,    pattern);
    cfg.setValue(kMaskKey,       mask);
    cfg.setValue(kBitsKey,       bitsPerFrame);
    cfg.setValue(kInvertedKey,   inverted);
    cfg.setValue(kRandomizedKey, randomized);
    return cfg.save(path);
}
