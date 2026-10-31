using TmDq.Core;

namespace TmDq.App.Processing
{
    /// <summary>
    /// The Frame Sync Lock settings a stream starts with: PRN15, as the Qt application's defaults in include/constants.h
    /// (kDefaultFrameSyncLockPattern, kDefaultFrameSyncMask, kDefaultFrameSyncLockBits) and the shipped
    /// settings/framesync_patterns/framesync_PRN15.toml have them. A test holds these to the header.
    /// </summary>
    public static class FrameSyncDefaults
    {
        public const string Pattern = "334AABBF";

        public const string Mask = "FFFFFFFF";

        /// <summary>PRN15's frame, 2^15 - 1 bits. Change it with the pattern or not at all: a PRN15 pattern with another
        /// frame length never locks.</summary>
        public const int BitsInMinorFrame = 32767;

        public const SamplePeriod Period = SamplePeriod.OneSecond;
    }
}
