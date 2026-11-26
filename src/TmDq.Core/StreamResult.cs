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
/// One stream's finished result, held in native memory. The spans (<see cref="TimesSec"/>, <see cref="LockPercent"/>,
/// <see cref="MissedFrames"/>, <see cref="ChannelValues"/>) are views into it, no copy, and are valid only until
/// <see cref="Dispose"/>: a span still in use when another thread disposes the result reads freed memory, so dispose it
/// only once no span is in use. What copies (<see cref="ToProcessed"/>, <see cref="ChannelName"/>, <see cref="Key"/>)
/// holds the result open until its copy is done, so a Dispose from another thread meanwhile is carried out after the
/// copy; any call after the Dispose throws <see cref="ObjectDisposedException"/>, except the numbers and the
/// <see cref="Label"/>, copied when the result was made. Safe to dispose from any thread, at any time.
/// </summary>
public sealed unsafe class StreamResult : IDisposable
{
    private readonly StreamResultHandle _handle;
    private StreamInfoNative _info;
    private readonly string _label;

    /// <summary>Takes ownership of <paramref name="handle"/>: it is released if this cannot be made.</summary>
    internal StreamResult(IntPtr handle)
    {
        _handle = new StreamResultHandle(handle);
        try
        {
            StreamInfoNative info = default;
            info.StructSize = (uint)sizeof(StreamInfoNative);
            Library.Check(Native.tmdq_stream_result_info(_handle, &info));
            _info = info;
            _label = Text.Read(info.LabelUtf8);
        }
        catch
        {
            _handle.Dispose();
            throw;
        }
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
    public string ChannelName(int index)
    {
        using HandleLease lease = Lease();   // the name is the result's: read before it can be freed
        return Text.Read(Fetch(index).NameUtf8);
    }

    /// <summary>The zero-based word within the minor frame the <paramref name="index"/>'th series is read from.</summary>
    public int ChannelWord(int index) => Fetch(index).WordIndex;

    /// <summary>The calibrated values of the <paramref name="index"/>'th series, parallel to <see cref="TimesSec"/>.</summary>
    public ReadOnlySpan<double> ChannelValues(int index) => new(Fetch(index).Values, _info.SampleCount);

    /// <summary>Copies the whole result into managed arrays, which outlive this object.</summary>
    public ProcessedStream ToProcessed()
    {
        using HandleLease lease = Lease();   // every span below is copied before the result can be freed
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
        Library.Check(Native.tmdq_stream_result_series(Handle, baseAbsSeconds, out SeriesSetHandle set));
        return new SeriesSet(set);
    }

    /// <summary>Which stream this is: the same stream run again is the same only if source, label and channel all agree.</summary>
    public StreamKey Key
    {
        get
        {
            using HandleLease lease = Lease();   // the label is the result's: read before it can be freed
            StreamKeyNative key = default;
            key.StructSize = (uint)sizeof(StreamKeyNative);
            Library.Check(Native.tmdq_stream_result_key(_handle, &key));
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
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
    }

    /// <summary>Holds the result open for a call and the copy of what it returns (see <see cref="NativeHandle.Lease"/>).</summary>
    private HandleLease Lease()
    {
        ThrowIfDisposed();   // names this object, where the handle's own refusal would name the handle
        return _handle.Lease();
    }

    /// <summary>Releases the native result. Safe to call twice.</summary>
    public void Dispose() => _handle.Dispose();

    /// <summary>The native handle, for the calls that build on a result (series construction).</summary>
    internal StreamResultHandle Handle
    {
        get
        {
            ThrowIfDisposed();
            return _handle;
        }
    }
}
