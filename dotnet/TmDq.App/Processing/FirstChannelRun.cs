using System;
using System.Linq;
using System.Threading.Tasks;
using TmDq.Core;

namespace TmDq.App.Processing
{
    /// <summary>One stream's lock percentage over time, copied out of the core's result.</summary>
    /// <param name="Label">The stream's name as TMATS gives it.</param>
    /// <param name="TimesSec">Each window's time, in seconds of the recording's time channel.</param>
    /// <param name="LockPercent">Each window's frame-sync lock, 0 to 100.</param>
    public sealed record LockSeries(string Label, int PcmChannelId, double[] TimesSec, double[] LockPercent)
    {
        public double MeanLock => LockPercent.Length == 0 ? 0 : LockPercent.Average();

        public static LockSeries Of(ProcessedStream s) => new(s.Label, s.PcmChannelId, s.TimesSec, s.LockPercent);
    }

    /// <summary>A recording the core could not process, with the reason in words for the user.</summary>
    public class RecordingException : Exception
    {
        public RecordingException(string message) : base(message) { }

        public RecordingException(string message, Exception inner) : base(message, inner) { }
    }

    /// <summary>
    /// The run with no dialog: a recording's first PCM channel with the Frame Sync Lock defaults, over its default time
    /// channel. The walking skeleton's processing (plan step 3.4), kept for the installer's smoke test, which has no one
    /// to answer Configure Streams, and as the configuration the view model uses when it is given none.
    /// </summary>
    public sealed class FirstChannelRun : IStreamConfigurator
    {
        /// <summary>The stream it runs for a channel.</summary>
        public static StreamConfig DefaultStream(ChannelInfo channel) => new()
        {
            PcmChannelId = channel.Id,
            Label = channel.Name,
            Mode = StreamMode.FrameSyncLock,
            SyncPattern = FrameSyncDefaults.Pattern,
            SyncMask = FrameSyncDefaults.Mask,
            BitsInMinorFrame = FrameSyncDefaults.BitsInMinorFrame,
            SamplePeriod = FrameSyncDefaults.Period,
        };

        public static RunRequest Request(string path, Ch10File file) =>
            new(path, file.DefaultTimeChannel, new[] { DefaultStream(file.PcmChannels[0]) });

        public Task<RunRequest?> ConfigureAsync(string path, Ch10File file) => Task.FromResult<RunRequest?>(Request(path, file));

        /// <summary>Processes <paramref name="path"/>'s first PCM channel on a worker thread.</summary>
        /// <exception cref="RecordingException">The file is not a recording the core can read, has no PCM channel, or
        /// its stream did not complete; the message says which.</exception>
        public static async Task<LockSeries> RunAsync(string path, IProgress<int>? progress = null)
        {
            Ch10File file = await StreamsRun.OpenAsync(path);
            RunOutcome outcome = await StreamsRun.RunAsync(Request(path, file), progress);
            return LockSeries.Of(outcome.Streams[0]);
        }
    }

    /// <summary>Decides what a recording is processed with: Configure Streams, or (with no one to ask) the defaults.</summary>
    public interface IStreamConfigurator
    {
        /// <summary>The run to start for <paramref name="file"/>, or null when the user cancelled.</summary>
        Task<RunRequest?> ConfigureAsync(string path, Ch10File file);
    }
}
