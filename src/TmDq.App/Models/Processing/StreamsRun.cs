using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using TmDq.Core;

namespace TmDq.App.Models.Processing;

/// <summary>What the log and the status line need of a stream that completed: which it is and how many samples and
/// channels it has. A few numbers, not the arrays: those stay in the core's result (<see cref="RunOutcome.Results"/>),
/// which the plot builds its series from and a reader of the samples reads in place.</summary>
public sealed record StreamSummary(int JobIndex, string Label, int SampleCount, int ChannelCount);

/// <summary>What a run produced: a summary of every stream that completed, in table order, the core's own result of each
/// (the same order), and every error reported. Disposing it releases the core's results.</summary>
public sealed record RunOutcome(IReadOnlyList<StreamSummary> Streams, IReadOnlyList<StreamResult> Results,
                                IReadOnlyList<string> Errors) : IDisposable
{
    public void Dispose()
    {
        foreach (StreamResult r in Results) r.Dispose();
    }
}

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

    /// <summary>The size of the recording at <paramref name="path"/>, in bytes, for the log.</summary>
    /// <exception cref="RecordingException">The file cannot be read now (gone, or locked, since it was opened): the recording is
    /// what failed, not the processing core.</exception>
    public static long FileSize(string path)
    {
        try
        {
            return new FileInfo(path).Length;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            throw new RecordingException($"{Path.GetFileName(path)} could not be read: {e.Message}", e);
        }
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
    /// Runs <paramref name="request"/> and waits for it, logging as v2.x did: the lines the core
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
            // A report the core made that the observer could not handle was caught at the boundary and kept; said
            // here, or the stream it concerned would be missing with no reason.
            if (run.ObserverException is { } failure)
                observer.OnError("A report from the core could not be handled: " + failure.Message);
        }
        var completed = observer.Results.OrderBy(r => r.Summary.JobIndex).ToList();
        List<StreamSummary> streams = completed.Select(r => r.Summary).ToList();
        if (streams.Count == 0)
        {
            cancel.ThrowIfCancellationRequested();
            string why = observer.Errors.Count > 0 ? string.Join(" ", observer.Errors) : "the stream did not complete";
            string what = request.Streams.Count == 1
                ? $"{request.Streams[0].Label} (channel {request.Streams[0].PcmChannelId})"
                : Path.GetFileName(request.FileName);
            throw new NothingProcessedException($"{what}: {why}");
        }
        return new RunOutcome(streams, completed.Select(r => r.Result).ToList(), observer.Errors.ToList());
    }

    /// <summary>The lines logged when a run starts, in v2.14.2's words: the file, the streams with their sample
    /// periods, and how many run at once.</summary>
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
    private static string LabelOf(StreamConfig s) =>
        string.IsNullOrEmpty(s.Label) ? "Ch " + s.PcmChannelId.ToString(CultureInfo.InvariantCulture) : s.Label;

    private static int PeriodMilliseconds(SamplePeriod period) => period switch
    {
        SamplePeriod.OneSecond => 1000,
        SamplePeriod.HundredMilliseconds => 100,
        SamplePeriod.TenMilliseconds => 10,
        _ => throw new ArgumentOutOfRangeException(nameof(period), period, null),
    };

    /// <summary>The line logged for a stream that completed, in v2.14.2's words.</summary>
    private static string Completed(StreamSummary s) =>
        $"  {s.Label}: {s.SampleCount.ToString(CultureInfo.InvariantCulture)} samples, " +
        $"{s.ChannelCount.ToString(CultureInfo.InvariantCulture)} channel(s)";

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

        /// <summary>Each completed stream: its summary, and the core's result, kept for the plot.</summary>
        public List<(StreamSummary Summary, StreamResult Result)> Results { get; } = new();

        public void OnLog(string message) => Log.Line(message);

        public void OnError(string message)
        {
            lock (Errors) Errors.Add(message);
            Log.Error(message);
        }

        public void OnProgress(int percent) => _progress?.Report(percent);

        public void OnStreamCompleted(StreamResult result)
        {
            // Nothing of the arrays is copied: the result keeps them in native memory for the plot, and what is said of the
            // stream here is a line and a count.
            var summary = new StreamSummary(result.JobIndex, result.Label, result.SampleCount, result.ChannelCount);
            lock (Results) Results.Add((summary, result));
            Log.Line(Completed(summary));
        }
    }
}
