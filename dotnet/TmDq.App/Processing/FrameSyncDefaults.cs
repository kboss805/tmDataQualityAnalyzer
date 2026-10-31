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

    /// <summary>
    /// The frame-sync settings a stream switched to Receiver SNR starts with when the shipped
    /// settings/framesync_patterns/framesync_rcvr_default.toml cannot supply them: kDefaultReceiverSNRPattern and
    /// kDefaultReceiverSNRBits in include/constants.h, which a test holds these to.
    /// </summary>
    public static class ReceiverSnrDefaults
    {
        public const string Pattern = "FE6B2840";

        public const int BitsInMinorFrame = 800;

        /// <summary>The file, under the application root, that gives them when it is there.</summary>
        public const string FileName = "framesync_rcvr_default.toml";
    }
}
