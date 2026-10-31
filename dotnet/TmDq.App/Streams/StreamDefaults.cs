using System.IO;
using TmDq.App.Processing;
using TmDq.Core;

namespace TmDq.App.Streams
{
    /// <summary>
    /// The settings a stream starts with, and those a Mode change brings, as the Qt application has them
    /// (MainViewModel::buildDefaultStreamConfigs, StreamConfigDialog's mode-switch defaults, loadReceiverFrameDefaults).
    /// </summary>
    public static class StreamDefaults
    {
        /// <summary>A recording's PCM channel as a new row: not processed, Frame Sync Lock, PRN15, the receiver
        /// defaults, not yet configured.</summary>
        public static StreamSetup NewStream(ChannelInfo channel)
        {
            ReceiverParams receiver = ReceiverParamsFile.Defaults();
            return new StreamSetup
            {
                PcmChannelId = channel.Id,
                Label = channel.Name,
                TmatsDataRateMbps = channel.TmatsDataRateBps > 0 ? channel.TmatsDataRateBps / 1e6 : 0,
                PolarityIndex = receiver.PolarityIndex,
                SlopeIndex = receiver.SlopeIndex,
                ScaleDdBPerV = receiver.ScaleDdBPerV,
                NumReceivers = receiver.NumReceivers,
                ReceiverChannels = receiver.ReceiverChannels,
            };
        }

        /// <summary>The frame-sync pattern, mask and frame length a row switched to <paramref name="mode"/> starts with.</summary>
        /// <param name="appRoot">Where settings/ is; null or empty means nowhere, and the compiled defaults apply.</param>
        public static (string Pattern, string Mask, int BitsInMinorFrame) FrameSyncFor(StreamMode mode, string? appRoot)
        {
            if (mode == StreamMode.FrameSyncLock)
                return (FrameSyncDefaults.Pattern, FrameSyncDefaults.Mask, FrameSyncDefaults.BitsInMinorFrame);

            // Receiver SNR: the shipped defaults file when it says something, field by field (pattern and mask
            // upper-cased), else the constants. Its Randomized and Inverted keys are not used, and a length from the
            // older WordsInMinorFrame key is ignored, exactly as the Qt dialog reads it (loadReceiverFrameDefaults).
            string pattern = ReceiverSnrDefaults.Pattern, mask = FrameSyncDefaults.Mask;
            int bits = ReceiverSnrDefaults.BitsInMinorFrame;
            if (!string.IsNullOrEmpty(appRoot))
            {
                FrameSyncFileData file = FrameSyncFile.Read(Path.Combine(appRoot, "settings", "framesync_patterns", ReceiverSnrDefaults.FileName));
                if (file.Pattern.Length > 0) pattern = file.Pattern.ToUpperInvariant();
                if (file.Mask.Length > 0) mask = file.Mask.ToUpperInvariant();
                if (file.BitsPerFrame is int b && !file.BitsFromLegacyKey) bits = b;
            }
            return (pattern, mask, bits);
        }
    }
}
