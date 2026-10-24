using System;
using System.Collections.Generic;

namespace TmDq.Core;

/// <summary>A copy of one receiver-channel series, parallel to the stream's times.</summary>
public sealed record ChannelData(string Name, int WordIndex, double[] Values);

/// <summary>A managed copy of a finished stream, independent of the native result it came from.</summary>
public sealed record ProcessedStream(
    int PcmChannelId,
    string Label,
    int JobIndex,
    int SourceId,
    StreamMode Mode,
    double[] TimesSec,
    double[] LockPercent,
    double[] MissedFrames,
    IReadOnlyList<ChannelData> Channels);

/// <summary>
/// One stream's finished result, held in native memory. The arrays are views into it (no copy) and are valid
/// until <see cref="Dispose"/>; <see cref="ToProcessed"/> takes a copy that outlives it. Thread-safe to dispose
/// from any thread, at any time.
/// </summary>
public sealed unsafe class StreamResult : IDisposable
{
    private IntPtr _handle;
    private StreamInfoNative _info;
    private readonly string _label;

    internal StreamResult(IntPtr handle)
    {
        _handle = handle;
        StreamInfoNative info = default;
        info.StructSize = (uint)sizeof(StreamInfoNative);
        Library.Check(Native.tmdq_stream_result_info(handle, &info));
        _info = info;
        _label = Text.Read(info.LabelUtf8);
    }

    /// <summary>The PCM channel this stream was processed from.</summary>
    public int PcmChannelId => _info.PcmChannelId;

    /// <summary>The stream's display label.</summary>
    public string Label => _label;

    /// <summary>Position of this stream in the request.</summary>
    public int JobIndex => _info.JobIndex;

    /// <summary>The request's source id.</summary>
    public int SourceId => _info.SourceId;

    public StreamMode Mode => (StreamMode)_info.Mode;

    /// <summary>Number of output samples.</summary>
    public int SampleCount => _info.SampleCount;

    /// <summary>Number of receiver series (0 for a lock-only stream).</summary>
    public int ChannelCount => _info.ChannelCount;

    /// <summary>Absolute IRIG seconds per sample.</summary>
    public ReadOnlySpan<double> TimesSec
    {
        get
        {
            ThrowIfDisposed();
            return new ReadOnlySpan<double>(_info.TimesSec, _info.SampleCount);
        }
    }

    /// <summary>Frame-sync lock percentage per sample, 0 to 100; empty for a receiver stream.</summary>
    public ReadOnlySpan<double> LockPercent
    {
        get
        {
            ThrowIfDisposed();
            return _info.LockPercent is null ? default : new ReadOnlySpan<double>(_info.LockPercent, _info.SampleCount);
        }
    }

    /// <summary>Cumulative missed frames per sample; empty for a receiver stream.</summary>
    public ReadOnlySpan<double> MissedFrames
    {
        get
        {
            ThrowIfDisposed();
            return _info.MissedFrames is null ? default : new ReadOnlySpan<double>(_info.MissedFrames, _info.SampleCount);
        }
    }

    /// <summary>The name of the <paramref name="index"/>'th receiver series, e.g. "L_RCVR1".</summary>
    public string ChannelName(int index) => Text.Read(Fetch(index).NameUtf8);

    /// <summary>The zero-based word within the minor frame the <paramref name="index"/>'th series is read from.</summary>
    public int ChannelWord(int index) => Fetch(index).WordIndex;

    /// <summary>The calibrated values of the <paramref name="index"/>'th series, parallel to <see cref="TimesSec"/>.</summary>
    public ReadOnlySpan<double> ChannelValues(int index) => new(Fetch(index).Values, _info.SampleCount);

    /// <summary>Copies the whole result into managed arrays, which outlive this object.</summary>
    public ProcessedStream ToProcessed()
    {
        var channels = new ChannelData[ChannelCount];
        for (int i = 0; i < channels.Length; i++)
        {
            channels[i] = new ChannelData(ChannelName(i), ChannelWord(i), ChannelValues(i).ToArray());
        }
        return new ProcessedStream(PcmChannelId, Label, JobIndex, SourceId, Mode,
            TimesSec.ToArray(), LockPercent.ToArray(), MissedFrames.ToArray(), channels);
    }

    /// <summary>
    /// The series this stream yields against a shared time base at <paramref name="baseAbsSeconds"/> absolute IRIG
    /// seconds. The set keeps this stream's data alive, so this result may be disposed first.
    /// </summary>
    public SeriesSet BuildSeries(double baseAbsSeconds)
    {
        Library.Check(Native.tmdq_stream_result_series(Handle, baseAbsSeconds, out IntPtr set));
        return new SeriesSet(set);
    }

    /// <summary>Which stream this is: the same stream run again is the same only if source, label and channel all agree.</summary>
    public StreamKey Key
    {
        get
        {
            StreamKeyNative key = default;
            key.StructSize = (uint)sizeof(StreamKeyNative);
            Library.Check(Native.tmdq_stream_result_key(Handle, &key));
            return new StreamKey(key.SourceId, Text.Read(key.LabelUtf8), key.Channel);
        }
    }

    private ChannelSeriesNative Fetch(int index)
    {
        ThrowIfDisposed();
        ChannelSeriesNative series = default;
        series.StructSize = (uint)sizeof(ChannelSeriesNative);
        Library.Check(Native.tmdq_stream_result_channel(_handle, index, &series));
        return series;
    }

    private void ThrowIfDisposed()
    {
        if (_handle == IntPtr.Zero)
        {
            throw new ObjectDisposedException(nameof(StreamResult));
        }
    }

    /// <summary>Releases the native result. Safe to call twice.</summary>
    public void Dispose()
    {
        IntPtr handle = System.Threading.Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle != IntPtr.Zero)
        {
            Native.tmdq_stream_result_free(handle);
        }
    }

    /// <summary>The native handle, for the calls that build on a result (series construction).</summary>
    internal IntPtr Handle
    {
        get
        {
            ThrowIfDisposed();
            return _handle;
        }
    }

    ~StreamResult() => Dispose();
}
