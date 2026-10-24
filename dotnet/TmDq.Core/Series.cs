using System;
using System.Collections.Generic;
using System.Threading;

namespace TmDq.Core;

/// <summary>One series a processed stream yields, before the host gives it a colour, visibility and an id.</summary>
/// <param name="Name">The legend and CSV name: the stream label for the frame-sync metrics, the channel's own name for SNR.</param>
/// <param name="ReceiverIndex">From the parameter name's last "_RCVR&lt;N&gt;"; 0 for the frame-sync metrics.</param>
/// <param name="ChannelIndex">0-based count within the receiver among this stream's SNR series.</param>
/// <param name="StreamOrder">The stream's PCM channel id.</param>
/// <param name="StreamSequence">The stream's job index, for ordering streams in the legend.</param>
/// <param name="YMin">Smallest value as Qt's qMin sees a NaN; <see cref="double.MaxValue"/> if there is none.</param>
/// <param name="YMax">Largest value; <see cref="double.MinValue"/> if there is none.</param>
/// <param name="ValueCount">Number of values, parallel to the set's elapsed times.</param>
public sealed record SeriesSpec(
    MetricType Metric,
    string Name,
    string StreamLabel,
    int ReceiverIndex,
    int ChannelIndex,
    int StreamOrder,
    int StreamSequence,
    int SourceId,
    double YMin,
    double YMax,
    int ValueCount);

/// <summary>
/// A processed stream as series on the shared elapsed axis: lock %, then accumulated missed frames, then one per
/// receiver channel (a metric the stream has no data for is left out). The values are views of the stream's data
/// (no copy), valid until <see cref="Dispose"/>; the set keeps that data alive, so the <see cref="StreamResult"/>
/// it was built from may be disposed first. The samples stay where the host keeps them: this describes and
/// decides, it does not store.
/// </summary>
public sealed unsafe class SeriesSet : IDisposable
{
    private IntPtr _handle;
    private readonly double* _elapsed;
    private readonly int _elapsedCount;
    private readonly SeriesSpec[] _specs;
    private readonly double*[] _values;

    internal SeriesSet(IntPtr handle)
    {
        _handle = handle;
        try
        {
            SeriesSetInfoNative info = default;
            info.StructSize = (uint)sizeof(SeriesSetInfoNative);
            Library.Check(Native.tmdq_series_set_info_get(handle, &info));
            _elapsed = info.Elapsed;
            _elapsedCount = info.ElapsedCount;

            _specs = new SeriesSpec[info.SeriesCount];
            _values = new double*[info.SeriesCount];
            for (int i = 0; i < _specs.Length; i++)
            {
                SeriesSpecNative s = default;
                s.StructSize = (uint)sizeof(SeriesSpecNative);
                Library.Check(Native.tmdq_series_set_spec(handle, i, &s));
                _values[i] = s.YValues;
                _specs[i] = new SeriesSpec(
                    (MetricType)s.MetricType,
                    Text.Read(s.NameUtf8),
                    Text.Read(s.StreamLabelUtf8),
                    s.ReceiverIndex,
                    s.ChannelIndex,
                    s.StreamOrder,
                    s.StreamSequence,
                    s.SourceId,
                    s.YMin,
                    s.YMax,
                    s.YCount);
            }
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    /// <summary>Number of series.</summary>
    public int Count => _specs.Length;

    /// <summary>The series, in order. A copy of the metadata; the values are read with <see cref="Values"/>.</summary>
    public IReadOnlyList<SeriesSpec> Specs => _specs;

    /// <summary>Seconds from the shared base to each sample; shared by every series.</summary>
    public ReadOnlySpan<double> ElapsedSeconds
    {
        get
        {
            ThrowIfDisposed();
            return new ReadOnlySpan<double>(_elapsed, _elapsedCount);
        }
    }

    /// <summary>The values of the <paramref name="index"/>'th series, parallel to <see cref="ElapsedSeconds"/>.</summary>
    public ReadOnlySpan<double> Values(int index)
    {
        ThrowIfDisposed();
        if ((uint)index >= (uint)_specs.Length)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        return new ReadOnlySpan<double>(_values[index], _specs[index].ValueCount);
    }

    private void ThrowIfDisposed()
    {
        if (_handle == IntPtr.Zero)
        {
            throw new ObjectDisposedException(nameof(SeriesSet));
        }
    }

    /// <summary>Releases the set. Safe to call twice.</summary>
    public void Dispose()
    {
        IntPtr handle = Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle != IntPtr.Zero)
        {
            Native.tmdq_series_set_free(handle);
        }
        GC.SuppressFinalize(this);
    }

    ~SeriesSet() => Dispose();
}

/// <summary>
/// Which stream a series belongs to: the same stream run again replaces its earlier series. Two streams are the
/// same only if the source, the label and the channel all agree.
/// </summary>
/// <param name="SourceId">The request's source id.</param>
/// <param name="Label">The stream's display label.</param>
/// <param name="Channel">The PCM channel id.</param>
public readonly record struct StreamKey(int SourceId, string Label, int Channel)
{
    /// <summary>
    /// Whether the two keys name the same stream, decided by the core (the rule is written once). It always agrees
    /// with <c>==</c>, which compares the three fields; a test holds the two together.
    /// </summary>
    public unsafe bool IsSameStream(StreamKey other)
    {
        Library.RequireLoaded();
        using var arena = new Arena();
        StreamKeyNative a = ToNative(this, arena);
        StreamKeyNative b = ToNative(other, arena);
        int same = Native.tmdq_stream_key_equal(&a, &b);
        if (same < 0)
        {
            throw new TmdqException(Status.BadArgument, Library.LastError());
        }
        return same != 0;
    }

    private static unsafe StreamKeyNative ToNative(StreamKey key, Arena arena) => new()
    {
        StructSize = (uint)sizeof(StreamKeyNative),
        SourceId = key.SourceId,
        LabelUtf8 = arena.Utf8(key.Label),
        Channel = key.Channel,
    };
}

/// <summary>The clock the shared elapsed axis hangs from.</summary>
/// <param name="BaseDay">Day of year of the base, 1-based; 0 when there is none.</param>
/// <param name="BaseAbsSeconds">Absolute IRIG seconds of elapsed zero.</param>
/// <param name="BaseTimeOffset">Seconds after midnight of the base.</param>
public sealed record TimeBaseInfo(int BaseDay, double BaseAbsSeconds, double BaseTimeOffset);

/// <summary>
/// The clock all loaded series share: elapsed zero is the earliest absolute sample of any source. It says how far
/// every existing series must shift when a stream arrives or a source goes; applying the shift to the stored
/// samples is the host's. Starts with no time (day 0, time 0, absolute 0).
/// </summary>
public sealed unsafe class TimeBase : IDisposable
{
    private IntPtr _handle;

    public TimeBase()
    {
        Library.RequireLoaded();
        Library.Check(Native.tmdq_time_base_create(out _handle));
    }

    /// <summary>The base as it stands.</summary>
    public TimeBaseInfo Info
    {
        get
        {
            TimeBaseInfoNative info = default;
            info.StructSize = (uint)sizeof(TimeBaseInfoNative);
            Library.Check(Native.tmdq_time_base_info_get(Handle, &info));
            return new TimeBaseInfo(info.BaseDay, info.BaseAbsSeconds, info.BaseTimeOffset);
        }
    }

    /// <summary>Back to no time.</summary>
    public void Reset() => Library.Check(Native.tmdq_time_base_reset(Handle));

    /// <summary>Sets the day and time of day directly, as an imported CSV states them; the absolute base is not touched.</summary>
    public void SetClock(int day, double offset) => Library.Check(Native.tmdq_time_base_set_clock(Handle, day, offset));

    /// <summary>
    /// Whether a source spanning [<paramref name="newMinAbs"/>, <paramref name="newMaxAbs"/>] absolute seconds touches
    /// the data already loaded, which spans from the base for <paramref name="existingMaxElapsed"/> seconds (touching
    /// at one instant counts).
    /// </summary>
    public bool Overlaps(double newMinAbs, double newMaxAbs, double existingMaxElapsed)
    {
        Library.Check(Native.tmdq_time_base_overlaps(Handle, newMinAbs, newMaxAbs, existingMaxElapsed, out int overlaps));
        return overlaps != 0;
    }

    /// <summary>
    /// A stream whose first absolute sample is <paramref name="firstAbsSeconds"/> is being added;
    /// <paramref name="hasSeries"/> is whether any series remain once the stream it replaces is gone. With none the
    /// base becomes this sample (so it can move later); otherwise it moves only earlier.
    /// </summary>
    /// <returns>The seconds to ADD to every existing series' elapsed values (0 when the base did not move).</returns>
    public double Adopt(double firstAbsSeconds, bool hasSeries)
    {
        Library.Check(Native.tmdq_time_base_adopt(Handle, firstAbsSeconds, hasSeries ? 1 : 0, out double shift));
        return shift;
    }

    /// <summary>
    /// A source was removed. <paramref name="earliestElapsed"/> is the smallest first elapsed value among the
    /// series that remain, against the current base, or null if none has a sample. If it is later than the base, the
    /// base moves up to it.
    /// </summary>
    /// <returns>The seconds to SUBTRACT from every remaining series' elapsed values.</returns>
    public double Advance(double? earliestElapsed)
    {
        Library.Check(Native.tmdq_time_base_advance(Handle, earliestElapsed.HasValue ? 1 : 0, earliestElapsed ?? 0.0, out double shift));
        return shift;
    }

    private IntPtr Handle
    {
        get
        {
            IntPtr handle = _handle;
            if (handle == IntPtr.Zero)
            {
                throw new ObjectDisposedException(nameof(TimeBase));
            }
            return handle;
        }
    }

    /// <summary>Releases the base. Safe to call twice.</summary>
    public void Dispose()
    {
        IntPtr handle = Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle != IntPtr.Zero)
        {
            Native.tmdq_time_base_free(handle);
        }
        GC.SuppressFinalize(this);
    }

    ~TimeBase() => Dispose();
}

/// <summary>One series of an imported CSV, as far as giving its stream an order is concerned.</summary>
/// <param name="StreamOrder">What the file gave (the channel id, for an SNR series).</param>
public sealed record ImportedSeriesInfo(MetricType Metric, string StreamLabel, int SourceId, int StreamOrder);

/// <summary>The order an imported CSV's streams are given.</summary>
public static unsafe class ImportedStreams
{
    /// <summary>
    /// Gives each frame-sync series the order of its stream (that of the SNR series of the same label in the same
    /// source, else a fresh order above every one in the file); SNR series keep what the file gave.
    /// </summary>
    /// <returns>The order of each series, in the same order as <paramref name="series"/>.</returns>
    public static IReadOnlyList<int> AssignOrders(IReadOnlyList<ImportedSeriesInfo> series)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(series);

        using var arena = new Arena();
        ImportedSeriesNative** wire = (ImportedSeriesNative**)arena.Alloc((nuint)(series.Count * sizeof(void*)));
        var natives = new ImportedSeriesNative*[series.Count];
        for (int i = 0; i < series.Count; i++)
        {
            ImportedSeriesNative* one = arena.Alloc<ImportedSeriesNative>(1);
            *one = new ImportedSeriesNative
            {
                StructSize = (uint)sizeof(ImportedSeriesNative),
                MetricType = (int)series[i].Metric,
                StreamLabelUtf8 = arena.Utf8(series[i].StreamLabel),
                SourceId = series[i].SourceId,
                StreamOrder = series[i].StreamOrder,
            };
            wire[i] = one;
            natives[i] = one;
        }

        Library.Check(Native.tmdq_assign_imported_stream_orders(wire, series.Count));

        var orders = new int[series.Count];
        for (int i = 0; i < orders.Length; i++)
        {
            orders[i] = natives[i]->StreamOrder;
        }
        return orders;
    }

    /// <summary>The same for the series of a parsed CSV.</summary>
    public static IReadOnlyList<int> AssignOrders(IReadOnlyList<CsvSeries> series)
    {
        ArgumentNullException.ThrowIfNull(series);
        var infos = new ImportedSeriesInfo[series.Count];
        for (int i = 0; i < infos.Length; i++)
        {
            infos[i] = new ImportedSeriesInfo(series[i].Metric, series[i].StreamLabel, series[i].SourceId, series[i].StreamOrder);
        }
        return AssignOrders(infos);
    }
}
