using System;
using System.Collections.Generic;
using System.IO;
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
    }

    /// <summary>A recording the core could not process, with the reason in words for the user.</summary>
    public sealed class RecordingException : Exception
    {
        public RecordingException(string message) : base(message) { }

        public RecordingException(string message, Exception inner) : base(message, inner) { }
    }

    /// <summary>
    /// The walking skeleton's processing (plan step 3.4): open a recording, take its first PCM channel, and run it with
    /// the Frame Sync Lock defaults through the native core, end to end. Configuring streams is plan step 4.1.
    /// </summary>
    public static class FirstChannelRun
    {
        /// <summary>The stream the skeleton runs for a channel.</summary>
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

        /// <summary>Processes <paramref name="path"/>'s first PCM channel on a worker thread. Progress is reported on
        /// <paramref name="progress"/>'s own context (the UI thread, for a <see cref="Progress{T}"/> made there).</summary>
        /// <exception cref="RecordingException">The file is not a recording the core can read, has no PCM channel, or
        /// its stream did not complete; the message says which.</exception>
        public static Task<LockSeries> RunAsync(string path, IProgress<int>? progress = null) => Task.Run(() => Run(path, progress));

        private static LockSeries Run(string path, IProgress<int>? progress)
        {
            NativeCore.EnsureLoaded();
            Ch10File file;
            try
            {
                file = Ch10File.Open(path);
            }
            catch (TmdqException e)
            {
                throw new RecordingException($"{Path.GetFileName(path)} could not be opened: {e.Message}", e);
            }
            if (file.PcmChannels.Count == 0)
                throw new RecordingException($"{Path.GetFileName(path)} has no PCM channel to process.");

            ChannelInfo channel = file.PcmChannels[0];
            var observer = new Collector(progress);
            using (ProcessingRun run = ProcessingRun.Start(new RunRequest(path, file.DefaultTimeChannel, new[] { DefaultStream(channel) }), observer))
            {
                run.Wait();
            }
            if (observer.Result is not ProcessedStream stream)
            {
                string why = observer.Errors.Count > 0 ? string.Join(" ", observer.Errors) : "the stream did not complete";
                throw new RecordingException($"{channel.Name} (channel {channel.Id}): {why}");
            }
            return new LockSeries(stream.Label, stream.PcmChannelId, stream.TimesSec, stream.LockPercent);
        }

        /// <summary>Gathers one run's reports. Called on the core's threads, one call at a time per stream.</summary>
        private sealed class Collector : IRunObserver
        {
            private readonly IProgress<int>? _progress;

            public Collector(IProgress<int>? progress) => _progress = progress;

            public List<string> Errors { get; } = new();

            public ProcessedStream? Result { get; private set; }

            public void OnError(string message)
            {
                lock (Errors) Errors.Add(message);
            }

            public void OnProgress(int percent) => _progress?.Report(percent);

            public void OnStreamCompleted(StreamResult result)
            {
                using (result) Result = result.ToProcessed();
            }
        }
    }
}
