using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using TmDq.App.Models.Streams;
using TmDq.Core;

namespace TmDq.App.Models.Processing
{
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

        /// <summary>The run with the rows it stands for: the first channel processed with the defaults, the rest not.</summary>
        public Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file)
        {
            var rows = file.PcmChannels.Select((c, i) => i == 0
                ? StreamDefaults.NewStream(c) with { Process = true, Configured = true }
                : StreamDefaults.NewStream(c)).ToList();
            int timeIndex = Math.Max(0, file.TimeChannels.ToList().FindIndex(c => c.Id == file.DefaultTimeChannel));
            return Task.FromResult<StreamsChoice?>(new StreamsChoice(Request(path, file), rows, timeIndex));
        }
    }

    /// <summary>Decides what a recording is processed with: Configure Streams, or (with no one to ask) the defaults.</summary>
    public interface IStreamConfigurator
    {
        /// <summary>The run to start for <paramref name="file"/>, and the choices it came from; null when the user cancelled.</summary>
        Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file);
    }

    /// <summary>What Configure Streams settled: the run, every row (processed or not) and the time channel's place in its
    /// list, which a template captures (plan step 4.7).</summary>
    public sealed record StreamsChoice(RunRequest Request, IReadOnlyList<StreamSetup> Rows, int TimeChannelIndex);
}
