using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using TmDq.Core;

namespace TmDq.App.Processing
{
    /// <summary>What a run produced: every stream that completed, in table order, and every error reported.</summary>
    public sealed record RunOutcome(IReadOnlyList<ProcessedStream> Streams, IReadOnlyList<string> Errors);

    /// <summary>Opening a recording and processing the streams a <see cref="RunRequest"/> names, through the native core,
    /// off the UI thread.</summary>
    public static class StreamsRun
    {
        /// <summary>Opens <paramref name="path"/> and reads its channels on a worker thread.</summary>
        /// <exception cref="RecordingException">The file is not a recording the core can read, or has no PCM channel.</exception>
        public static Task<Ch10File> OpenAsync(string path) => Task.Run(() => Open(path));

        public static Ch10File Open(string path)
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
            return file;
        }

        /// <summary>Runs <paramref name="request"/> on a worker thread. Progress is reported on
        /// <paramref name="progress"/>'s own context (the UI thread, for a <see cref="Progress{T}"/> made there).</summary>
        /// <exception cref="RecordingException">No stream completed; the message gives the core's reasons.</exception>
        public static Task<RunOutcome> RunAsync(RunRequest request, IProgress<int>? progress = null) =>
            Task.Run(() => Run(request, progress));

        public static RunOutcome Run(RunRequest request, IProgress<int>? progress)
        {
            NativeCore.EnsureLoaded();
            var observer = new Collector(progress);
            using (ProcessingRun run = ProcessingRun.Start(request, observer))
            {
                run.Wait();
            }
            List<ProcessedStream> streams = observer.Results.OrderBy(s => s.JobIndex).ToList();
            if (streams.Count == 0)
            {
                string why = observer.Errors.Count > 0 ? string.Join(" ", observer.Errors) : "the stream did not complete";
                string what = request.Streams.Count == 1
                    ? $"{request.Streams[0].Label} (channel {request.Streams[0].PcmChannelId})"
                    : Path.GetFileName(request.FileName);
                throw new RecordingException($"{what}: {why}");
            }
            return new RunOutcome(streams, observer.Errors.ToList());
        }

        /// <summary>Gathers one run's reports. Called on the core's threads, one call at a time per stream.</summary>
        private sealed class Collector : IRunObserver
        {
            private readonly IProgress<int>? _progress;

            public Collector(IProgress<int>? progress) => _progress = progress;

            public List<string> Errors { get; } = new();

            public List<ProcessedStream> Results { get; } = new();

            public void OnError(string message)
            {
                lock (Errors) Errors.Add(message);
            }

            public void OnProgress(int percent) => _progress?.Report(percent);

            public void OnStreamCompleted(StreamResult result)
            {
                using (result)
                {
                    ProcessedStream p = result.ToProcessed();
                    lock (Results) Results.Add(p);
                }
            }
        }
    }
}
