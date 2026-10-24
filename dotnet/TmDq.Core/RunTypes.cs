using System;
using System.Collections.Generic;

namespace TmDq.Core;

/// <summary>How a stream is processed (tmdq_stream_mode).</summary>
public enum StreamMode
{
    /// <summary>Calibrated receiver-channel (SNR) values.</summary>
    ReceiverChannelInfo = 0,

    /// <summary>Frame-sync lock percentage only.</summary>
    FrameSyncLock = 1,
}

/// <summary>The sample periods the application offers, as the index the core takes.</summary>
public enum SamplePeriod
{
    OneSecond = 0,
    HundredMilliseconds = 1,
    TenMilliseconds = 2,
}

/// <summary>One measured calibration point: a raw count average and the true dB it stands for.</summary>
public sealed record CalibrationPoint(double RawAvg, double TrueDb);

/// <summary>A non-linear calibration profile for one receiver word.</summary>
/// <param name="WordIndex">Zero-based word within the minor frame.</param>
/// <param name="Valid">False means the profile is ignored and the word is calibrated linearly.</param>
/// <param name="Points">Sorted ascending by <see cref="CalibrationPoint.RawAvg"/>.</param>
public sealed record CalibrationProfile(int WordIndex, bool Valid, IReadOnlyList<CalibrationPoint> Points);

/// <summary>One stream's settings, as the user gives them. The defaults are the application's.</summary>
public sealed record StreamConfig
{
    /// <summary>The PCM channel to process.</summary>
    public required int PcmChannelId { get; init; }

    /// <summary>Display label; null or empty means "Ch &lt;id&gt;".</summary>
    public string? Label { get; init; }

    public StreamMode Mode { get; init; } = StreamMode.FrameSyncLock;

    /// <summary>The frame-sync pattern as hex text.</summary>
    public required string SyncPattern { get; init; }

    /// <summary>Hex text; null or empty means every bit of the pattern.</summary>
    public string? SyncMask { get; init; }

    public int BitsInMinorFrame { get; init; } = 32767;

    public bool Randomized { get; init; }

    public bool Inverted { get; init; }

    /// <summary>0 means use the rate TMATS declares.</summary>
    public double DataRateMbps { get; init; }

    public SamplePeriod SamplePeriod { get; init; } = SamplePeriod.OneSecond;

    /// <summary>Receiver SNR only.</summary>
    public int PolarityIndex { get; init; }

    /// <summary>Receiver SNR only.</summary>
    public int SlopeIndex { get; init; } = 3;

    /// <summary>Receiver SNR only: dB per volt.</summary>
    public double ScaleDdBPerV { get; init; } = 20.0;

    /// <summary>Receiver SNR only; used when no word-map file is found.</summary>
    public int NumReceivers { get; init; } = 16;

    /// <summary>Receiver SNR only; used when no word-map file is found.</summary>
    public int ReceiverChannels { get; init; } = 3;

    /// <summary>Receiver SNR only: the word-map file; null or empty means the shipped default.</summary>
    public string? ReceiverParamsToml { get; init; }

    /// <summary>Non-linear calibration profiles. If two name the same word, the first is used.</summary>
    public IReadOnlyList<CalibrationProfile> Calibration { get; init; } = Array.Empty<CalibrationProfile>();
}

/// <summary>One run: a recording and the streams to process over it, concurrently, with one read of the file.</summary>
public sealed record RunRequest(string FileName, int TimeChannelId, IReadOnlyList<StreamConfig> Streams)
{
    /// <summary>Id of this file within a multi-file session; carried into every result.</summary>
    public int SourceId { get; init; }

    /// <summary>File-scoped byte order; an ordinary Chapter 10 recording needs the swap.</summary>
    public bool SwapBytes { get; init; } = true;

    /// <summary>Where settings/receiver_params/default.toml is looked for; null means nowhere.</summary>
    public string? AppRoot { get; init; }
}

/// <summary>
/// Where a run reports to. Every method may be called from a native thread, never concurrently for the same
/// stream, and an exception thrown from one is caught and kept (see <see cref="ProcessingRun.ObserverException"/>).
/// A method must not call <see cref="ProcessingRun.Dispose"/> or <see cref="ProcessingRun.Wait"/> on its own run.
/// </summary>
public interface IRunObserver
{
    /// <summary>A status line. While the run is being started this is called on the starting thread.</summary>
    void OnLog(string message)
    {
    }

    /// <summary>A failure report.</summary>
    void OnError(string message)
    {
    }

    /// <summary>0 to 100, from the reader thread only.</summary>
    void OnProgress(int percent)
    {
    }

    /// <summary>
    /// A stream completed (and only a stream that completed successfully, in a run that was not cancelled).
    /// The observer owns <paramref name="result"/> and must dispose it.
    /// </summary>
    void OnStreamCompleted(StreamResult result)
    {
        result.Dispose();
    }

    /// <summary>Once, last, after every other call: at least one stream completed and there was no cancel.</summary>
    void OnFinished(bool success)
    {
    }
}
