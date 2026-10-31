using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using TmDq.Core;

namespace TmDq.App.Processing
{
    /// <summary>What a run produced: every stream that completed, in table order, and every error reported.</summary>
    public sealed record RunOutcome(IReadOnlyList<ProcessedStream> Streams, IReadOnlyList<string> Errors);

    /// <summary>Where a run's log goes, a line at a time, in the order the lines happen. Called on the core's threads,
    /// never two calls at once.</summary>
    public interface IRunLog
    {
        /// <summary>A status line; the log classifies it by its text.</summary>
        void Line(string message);

        /// <summary>A failure the core reported.</summary>
        void Error(string message);
    }

    /// <summary>A run in which no stream completed, though nothing was cancelled. The core's reasons were logged as they
    /// came, and the message repeats them for the status line.</summary>
    public sealed class NothingProcessedException : RecordingException
    {
        public NothingProcessedException(string message) : base(message) { }
    }

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
        /// <exception cref="OperationCanceledException"><paramref name="cancel"/> stopped the run before any stream
        /// completed.</exception>
        /// <exception cref="RecordingException">The run could not start, or no stream completed; the message gives the
        /// core's reasons.</exception>
        public static Task<RunOutcome> RunAsync(RunRequest request, IProgress<int>? progress = null, IRunLog? log = null,
                                                CancellationToken cancel = default) =>
            Task.Run(() => Run(request, progress, log, cancel));

        /// <summary>
        /// Runs <paramref name="request"/> and waits for it, logging as the Qt application does: the lines the core
        /// writes while it builds each stream's job, then the summary (<see cref="Summary"/>), then the run's own lines
        /// and errors, and a line for each stream as it completes. A cancel stops every stream (US1.0).
        /// </summary>
        public static RunOutcome Run(RunRequest request, IProgress<int>? progress, IRunLog? log = null,
                                     CancellationToken cancel = default)
        {
            NativeCore.EnsureLoaded();
            cancel.ThrowIfCancellationRequested();
            var observer = new Collector(progress, log);
            observer.Log.Starting();
            ProcessingRun run;
            try
            {
                run = ProcessingRun.Start(request, observer);
            }
            catch (TmdqException e)
            {
                observer.Log.StartFailed();
                throw new RecordingException(e.Message, e);
            }
            using (run)
            {
                observer.Log.Started(Summary(request));
                using (cancel.Register(run.Cancel))
                {
                    run.Wait();
                }
            }
            List<ProcessedStream> streams = observer.Results.OrderBy(s => s.JobIndex).ToList();
            if (streams.Count == 0)
            {
                cancel.ThrowIfCancellationRequested();
                string why = observer.Errors.Count > 0 ? string.Join(" ", observer.Errors) : "the stream did not complete";
                string what = request.Streams.Count == 1
                    ? $"{request.Streams[0].Label} (channel {request.Streams[0].PcmChannelId})"
                    : Path.GetFileName(request.FileName);
                throw new NothingProcessedException($"{what}: {why}");
            }
            return new RunOutcome(streams, observer.Errors.ToList());
        }

        /// <summary>The lines the Qt application logs when a run starts (<c>MainViewModel::startProcessing</c> and the
        /// coordinator's): the file, the streams with their sample periods, and how many run at once.</summary>
        public static IReadOnlyList<string> Summary(RunRequest request)
        {
            var lines = new List<string>
            {
                "--- Processing Summary ---",
                "  Input: " + Path.GetFileName(request.FileName),
                "  Streams: " + request.Streams.Count.ToString(CultureInfo.InvariantCulture),
                "  Time range: all",
            };
            foreach (StreamConfig s in request.Streams)
                lines.Add($"  {LabelOf(s)}: {PeriodMilliseconds(s.SamplePeriod).ToString(CultureInfo.InvariantCulture)} ms period");
            lines.Add($"--- Processing {request.Streams.Count.ToString(CultureInfo.InvariantCulture)} stream(s) concurrently ---");
            return lines;
        }

        /// <summary>A stream's name as the core gives it: its label, or "Ch &lt;id&gt;" without one.</summary>
        public static string LabelOf(StreamConfig s) =>
            string.IsNullOrEmpty(s.Label) ? "Ch " + s.PcmChannelId.ToString(CultureInfo.InvariantCulture) : s.Label;

        public static int PeriodMilliseconds(SamplePeriod period) => period switch
        {
            SamplePeriod.OneSecond => 1000,
            SamplePeriod.HundredMilliseconds => 100,
            SamplePeriod.TenMilliseconds => 10,
            _ => throw new ArgumentOutOfRangeException(nameof(period), period, null),
        };

        /// <summary>The line logged for a stream that completed, as the Qt main window writes it.</summary>
        public static string Completed(ProcessedStream s) =>
            $"  {s.Label}: {s.TimesSec.Length.ToString(CultureInfo.InvariantCulture)} samples, " +
            $"{s.Channels.Count.ToString(CultureInfo.InvariantCulture)} channel(s)";

        /// <summary>Gathers one run's reports; its log goes through a <see cref="RunLogOrder"/>.</summary>
        private sealed class Collector : IRunObserver
        {
            private readonly IProgress<int>? _progress;

            public Collector(IProgress<int>? progress, IRunLog? log)
            {
                _progress = progress;
                Log = new RunLogOrder(log);
            }

            public RunLogOrder Log { get; }

            public List<string> Errors { get; } = new();

            public List<ProcessedStream> Results { get; } = new();

            public void OnLog(string message) => Log.Line(message);

            public void OnError(string message)
            {
                lock (Errors) Errors.Add(message);
                Log.Error(message);
            }

            public void OnProgress(int percent) => _progress?.Report(percent);

            public void OnStreamCompleted(StreamResult result)
            {
                using (result)
                {
                    ProcessedStream p = result.ToProcessed();
                    lock (Results) Results.Add(p);
                    Log.Line(Completed(p));
                }
            }
        }
    }

    /// <summary>
    /// Passes a run's log on in the Qt application's order, one line at a time. While the run is being started the
    /// lines are held: those written on the starting thread are the job builder's, which the Qt application logged before
    /// its summary; anything the run's own threads say in that moment follows the summary.
    /// </summary>
    public sealed class RunLogOrder : IRunLog
    {
        private readonly IRunLog? _log;
        private readonly object _gate = new();
        private readonly List<(bool Error, string Text)> _jobLines = new();
        private List<(bool Error, string Text)>? _held;
        private int _startingThread;

        public RunLogOrder(IRunLog? log) => _log = log;

        /// <summary>Called on the thread about to start the run.</summary>
        public void Starting()
        {
            lock (_gate)
            {
                _startingThread = Environment.CurrentManagedThreadId;
                _held = new List<(bool, string)>();
            }
        }

        /// <summary>The run started: the job builder's lines, then <paramref name="between"/>, then whatever the run's
        /// threads have said meanwhile; from here on, every line as it comes.</summary>
        public void Started(IEnumerable<string> between)
        {
            lock (_gate)
            {
                foreach ((bool error, string text) in _jobLines) Emit(error, text);
                foreach (string line in between) Emit(false, line);
                foreach ((bool error, string text) in _held ?? new()) Emit(error, text);
                _jobLines.Clear();
                _held = null;
            }
        }

        /// <summary>The run did not start: the job builder's lines for the streams it built before the one refused.</summary>
        public void StartFailed() => Started(Array.Empty<string>());

        public void Line(string message) => Write(false, message);

        public void Error(string message) => Write(true, message);

        private void Write(bool error, string text)
        {
            lock (_gate)
            {
                if (_held is not null)
                {
                    (Environment.CurrentManagedThreadId == _startingThread ? _jobLines : _held).Add((error, text));
                    return;
                }
                Emit(error, text);
            }
        }

        private void Emit(bool error, string text)
        {
            if (_log is null) return;
            if (error) _log.Error(text);
            else _log.Line(text);
        }
    }
}
