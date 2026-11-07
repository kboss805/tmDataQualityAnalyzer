using System;
using System.Collections.Generic;

namespace TmDq.Core;

/// <summary>Which kind of measurement a series carries (tmdq_metric_type).</summary>
public enum MetricType
{
    /// <summary>A receiver channel's calibrated SNR, in dB.</summary>
    Snr = 0,

    /// <summary>Frame-sync lock percentage.</summary>
    FrameSyncLock = 1,

    /// <summary>Cumulative missed frames.</summary>
    AccumulatedMissedFrames = 2,
}

/// <summary>What a frame-sync pattern file says; anything it does not usefully carry is empty or null.</summary>
/// <param name="Pattern">`FrameSync`, as written (not case-folded); empty if absent.</param>
/// <param name="Mask">`FrameSyncMask`, as written; empty if absent.</param>
/// <param name="BitsPerFrame">The frame length, if the file gives a usable one.</param>
/// <param name="BitsFromLegacyKey">The length came from the older `WordsInMinorFrame` key.</param>
/// <param name="Inverted">`Inverted`; false when the key is absent.</param>
/// <param name="Randomized">`Randomized`, or null when the file has no such key (older files, the shipped patterns among them: leave the operator's choice alone).</param>
public sealed record FrameSyncFileData(
    string Pattern,
    string Mask,
    int? BitsPerFrame,
    bool BitsFromLegacyKey,
    bool Inverted,
    bool? Randomized);

/// <summary>The frame-sync pattern file the stream dialogs load and save: pattern, mask, frame length and the two code-format flags.</summary>
public static unsafe class FrameSyncFile
{
    /// <summary>
    /// Reads a pattern file. A file that is missing, empty or unreadable reads as a file that says nothing
    /// (every field empty or null), as in the application.
    /// </summary>
    public static FrameSyncFileData Read(string path)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);

        using var arena = new Arena();
        Library.Check(Native.tmdq_frame_sync_file_read(arena.Utf8(path), out IntPtr file));
        try
        {
            FrameSyncFileInfoNative info = default;
            info.StructSize = (uint)sizeof(FrameSyncFileInfoNative);
            Library.Check(Native.tmdq_frame_sync_file_info_get(file, &info));
            return new FrameSyncFileData(
                Text.Read(info.PatternUtf8),
                Text.Read(info.MaskUtf8),
                info.HasBitsPerFrame != 0 ? info.BitsPerFrame : null,
                info.BitsFromLegacyKey != 0,
                info.Inverted != 0,
                info.HasRandomized != 0 ? info.Randomized != 0 : null);
        }
        finally
        {
            Native.tmdq_frame_sync_file_free(file);
        }
    }

    /// <summary>
    /// Writes a pattern file: the pattern and mask as given, the frame length and the two flags. Data rate and
    /// sample rate are deliberately not written. A null pattern or mask is written as empty.
    /// </summary>
    /// <exception cref="TmdqException"><see cref="Status.Io"/> when the file cannot be created or fully written.</exception>
    public static void Write(string path, string? pattern, string? mask, int bitsPerFrame, bool inverted, bool randomized)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);

        using var arena = new Arena();
        Library.Check(Native.tmdq_frame_sync_file_write(
            arena.Utf8(path), arena.Utf8(pattern), arena.Utf8(mask), bitsPerFrame, inverted ? 1 : 0, randomized ? 1 : 0));
    }
}

/// <summary>The scalar settings of a receiver-parameters file (US5.1). Its word map is not here: the core reads it
/// when a stream is processed, and <see cref="ReceiverParamsFile.Save"/> writes it.</summary>
/// <param name="PolarityIndex">0 positive, 1 negative.</param>
/// <param name="SlopeIndex">Index into the voltage ranges: 0 ±10 V, 1 ±5 V, 2 0–10 V, 3 0–5 V.</param>
/// <param name="ScaleDdBPerV">Calibration scale, dB per volt.</param>
/// <param name="NumReceivers">Receivers contributing channels.</param>
/// <param name="ReceiverChannels">Channels per receiver.</param>
public sealed record ReceiverParams(int PolarityIndex, int SlopeIndex, double ScaleDdBPerV, int NumReceivers, int ReceiverChannels);

/// <summary>The receiver-parameters file the Receiver SNR dialog loads and saves, through the core, so both
/// applications read and write the same bytes.</summary>
public static unsafe class ReceiverParamsFile
{
    /// <summary>The application's defaults: what a stream starts with, and what a missing key falls back to.</summary>
    public static ReceiverParams Defaults()
    {
        Library.RequireLoaded();
        ReceiverParamsNative result = default;
        result.StructSize = (uint)sizeof(ReceiverParamsNative);
        using var arena = new Arena();
        // An empty path is a file that says nothing, read from the core's own defaults.
        Library.Check(Native.tmdq_receiver_params_read(arena.Utf8(string.Empty), null, &result));
        return FromNative(result);
    }

    /// <summary>
    /// Reads a file's scalar settings. A key the file does not carry keeps its value from <paramref name="current"/>
    /// (pass what is shown; null means <see cref="Defaults"/>), so a partial file changes only what it says; a
    /// missing or unreadable file changes nothing. Counts are read from [Receivers], else the older [Parameters] keys;
    /// a scale or count that is not positive is ignored.
    /// </summary>
    public static ReceiverParams Read(string path, ReceiverParams? current = null)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);
        ReceiverParamsNative result = default;
        result.StructSize = (uint)sizeof(ReceiverParamsNative);
        using var arena = new Arena();
        if (current is null)
        {
            Library.Check(Native.tmdq_receiver_params_read(arena.Utf8(path), null, &result));
        }
        else
        {
            ReceiverParamsNative start = ToNative(current);
            Library.Check(Native.tmdq_receiver_params_read(arena.Utf8(path), &start, &result));
        }
        return FromNative(result);
    }

    /// <summary>
    /// Writes a complete file: the scalars, then the word map of <paramref name="wordMapSource"/> when that names a
    /// file that loads in a frame of <paramref name="bitsPerFrame"/> bits, otherwise the default map for the receiver
    /// counts (null or empty means the default map). The file is replaced, never merged.
    /// </summary>
    /// <exception cref="TmdqException"><see cref="Status.Io"/> with the core's reason: no usable word map (more channels
    /// than the frame has words), or the file could not be written.</exception>
    public static void Save(string path, ReceiverParams parameters, string? wordMapSource, int bitsPerFrame)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(parameters);
        ReceiverParamsNative native = ToNative(parameters);
        using var arena = new Arena();
        Library.Check(Native.tmdq_receiver_params_save(arena.Utf8(path), &native, arena.Utf8(wordMapSource),
                                                       FrameGeometry.WordsInMinorFrame(bitsPerFrame)));
    }

    private static ReceiverParamsNative ToNative(ReceiverParams p) => new()
    {
        StructSize = (uint)sizeof(ReceiverParamsNative),
        PolarityIndex = p.PolarityIndex,
        SlopeIndex = p.SlopeIndex,
        ScaleDbPerV = p.ScaleDdBPerV,
        NumReceivers = p.NumReceivers,
        ReceiverChannels = p.ReceiverChannels,
    };

    private static ReceiverParams FromNative(ReceiverParamsNative n) =>
        new(n.PolarityIndex, n.SlopeIndex, n.ScaleDbPerV, n.NumReceivers, n.ReceiverChannels);
}

/// <summary>The minor frame's arithmetic, as the core does it.</summary>
public static class FrameGeometry
{
    /// <summary>Words in a minor frame of <paramref name="bitsInMinorFrame"/> bits, rounded up: a partly filled last
    /// word still takes a slot.</summary>
    public static int WordsInMinorFrame(int bitsInMinorFrame)
    {
        Library.RequireLoaded();
        return Native.tmdq_words_in_minor_frame(bitsInMinorFrame);
    }
}

/// <summary>The outcome of matching a template's PCM channel ids against a file's.</summary>
/// <param name="Ok">The two sets are identical.</param>
/// <param name="Missing">Ids the template needs and the file lacks (sorted, each once).</param>
/// <param name="Extra">Ids the file has and the template does not (sorted, each once).</param>
public sealed record TemplateMatchResult(bool Ok, IReadOnlyList<int> Missing, IReadOnlyList<int> Extra);

/// <summary>Whether a recording has exactly the PCM channels a processing template was made for.</summary>
public static unsafe class TemplateMatcher
{
    /// <summary>Compares two channel-id sets exactly: order and repeats do not matter, and two empty sets match.</summary>
    public static TemplateMatchResult Match(IReadOnlyList<int> templateIds, IReadOnlyList<int> fileIds)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(templateIds);
        ArgumentNullException.ThrowIfNull(fileIds);

        using var arena = new Arena();
        int* template = Copy(arena, templateIds);
        int* file = Copy(arena, fileIds);
        Library.Check(Native.tmdq_template_match(template, templateIds.Count, file, fileIds.Count, out IntPtr match));
        try
        {
            MatchInfoNative info = default;
            info.StructSize = (uint)sizeof(MatchInfoNative);
            Library.Check(Native.tmdq_match_info_get(match, &info));
            return new TemplateMatchResult(
                info.Ok != 0,
                new ReadOnlySpan<int>(info.Missing, info.MissingCount).ToArray(),
                new ReadOnlySpan<int>(info.Extra, info.ExtraCount).ToArray());
        }
        finally
        {
            Native.tmdq_match_free(match);
        }
    }

    private static int* Copy(Arena arena, IReadOnlyList<int> ids)
    {
        if (ids.Count == 0)
        {
            return null;
        }

        int* copy = arena.Alloc<int>(ids.Count);
        for (int i = 0; i < ids.Count; i++)
        {
            copy[i] = ids[i];
        }
        return copy;
    }
}

/// <summary>One series of a parsed CSV, as a managed copy.</summary>
/// <param name="Name">The column header, with any source qualifier removed.</param>
/// <param name="StreamLabel">The stream the header names; empty if it names none.</param>
/// <param name="ReceiverIndex">1-based, from "_RCVR&lt;N&gt;"; 0 for the frame-sync metrics.</param>
/// <param name="ChannelIndex">0-based count within (source, stream, receiver); SNR series only.</param>
/// <param name="StreamOrder">The PCM channel id for an SNR series; 0 for the others until the plot gives them one.</param>
/// <param name="SourceId">From the "S&lt;n&gt;| " qualifier; 0 when there is none.</param>
/// <param name="YMin">Smallest value; <see cref="double.MaxValue"/> if there is none.</param>
/// <param name="YMax">Largest value; <see cref="double.MinValue"/> if there is none.</param>
/// <param name="X">Elapsed seconds from the first row.</param>
/// <param name="Y">One value per x.</param>
public sealed record CsvSeries(
    MetricType Metric,
    string Name,
    string StreamLabel,
    int ReceiverIndex,
    int ChannelIndex,
    int StreamOrder,
    int SourceId,
    double YMin,
    double YMax,
    double[] X,
    double[] Y);

/// <summary>A parsed CSV, as a managed copy.</summary>
/// <param name="BaseDay">Day of the first parsed row.</param>
/// <param name="BaseTimeOffset">Seconds after midnight of the first parsed row.</param>
/// <param name="SkippedRows">Malformed data rows skipped.</param>
/// <param name="XMax">The largest elapsed time.</param>
public sealed record CsvData(int BaseDay, double BaseTimeOffset, int SkippedRows, double XMax, IReadOnlyList<CsvSeries> Series);

/// <summary>One column to write to a CSV: the series' identity (which decides its header) and its samples.</summary>
/// <param name="Name">The series' name as the plot shows it.</param>
/// <param name="SourceId">0 is written unqualified.</param>
/// <param name="X">Elapsed seconds from the recording's first sample.</param>
/// <param name="Y">One value per x (the shorter of the two is written).</param>
public sealed record CsvColumn(MetricType Metric, string Name, int SourceId, IReadOnlyList<double> X, IReadOnlyList<double> Y);

/// <summary>The application's CSV export, read and written.</summary>
public static unsafe class CsvFile
{
    /// <summary>Parses a CSV the application exported.</summary>
    /// <exception cref="TmdqException"><see cref="Status.Prepare"/> when the file is unreadable, has the wrong header or holds no parseable sample.</exception>
    public static CsvData Parse(string path)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);

        using var arena = new Arena();
        Library.Check(Native.tmdq_csv_parse(arena.Utf8(path), out IntPtr csv));
        try
        {
            CsvInfoNative info = default;
            info.StructSize = (uint)sizeof(CsvInfoNative);
            Library.Check(Native.tmdq_csv_info_get(csv, &info));

            var series = new CsvSeries[info.SeriesCount];
            for (int i = 0; i < series.Length; i++)
            {
                CsvSeriesInfoNative s = default;
                s.StructSize = (uint)sizeof(CsvSeriesInfoNative);
                Library.Check(Native.tmdq_csv_series_get(csv, i, &s));
                series[i] = new CsvSeries(
                    (MetricType)s.MetricType,
                    Text.Read(s.NameUtf8),
                    Text.Read(s.StreamLabelUtf8),
                    s.ReceiverIndex,
                    s.ChannelIndex,
                    s.StreamOrder,
                    s.SourceId,
                    s.YMin,
                    s.YMax,
                    new ReadOnlySpan<double>(s.XValues, s.PointCount).ToArray(),
                    new ReadOnlySpan<double>(s.YValues, s.PointCount).ToArray());
            }
            return new CsvData(info.BaseDay, info.BaseTimeOffset, info.SkippedRows, info.XMax, series);
        }
        finally
        {
            Native.tmdq_csv_free(csv);
        }
    }

    /// <summary>
    /// Writes columns as the CSV <see cref="Parse"/> reads: CR LF, UTF-8 without a byte-order mark, six decimals, one
    /// row per distinct millisecond. The time column reads as a day and time of day from
    /// <paramref name="baseDay"/> and <paramref name="baseTimeOffset"/>.
    /// </summary>
    /// <exception cref="TmdqException"><see cref="Status.BadArgument"/> for no columns or a bad metric; <see cref="Status.Io"/> when the file cannot be written.</exception>
    public static void Write(string path, IReadOnlyList<CsvColumn> columns, int baseDay, double baseTimeOffset)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(columns);

        using var arena = new Arena();
        CsvColumnNative** wire = (CsvColumnNative**)arena.Alloc((nuint)(columns.Count * sizeof(void*)));
        for (int i = 0; i < columns.Count; i++)
        {
            CsvColumn c = columns[i];
            CsvColumnNative* one = arena.Alloc<CsvColumnNative>(1);
            *one = new CsvColumnNative
            {
                StructSize = (uint)sizeof(CsvColumnNative),
                MetricType = (int)c.Metric,
                NameUtf8 = arena.Utf8(c.Name),
                XValues = Copy(arena, c.X),
                YValues = Copy(arena, c.Y),
                SourceId = c.SourceId,
                XCount = c.X.Count,
                YCount = c.Y.Count,
            };
            wire[i] = one;
        }
        Library.Check(Native.tmdq_csv_write(arena.Utf8(path), wire, columns.Count, baseDay, baseTimeOffset));
    }

    private static double* Copy(Arena arena, IReadOnlyList<double> values)
    {
        if (values.Count == 0)
        {
            return null;
        }

        double* copy = arena.Alloc<double>(values.Count);
        for (int i = 0; i < values.Count; i++)
        {
            copy[i] = values[i];
        }
        return copy;
    }
}

/// <summary>The "DDD:HH:MM:SS" clock stamps the plot's axis and readouts show.</summary>
public static unsafe class ClockTime
{
    /// <summary>
    /// <paramref name="elapsed"/> seconds after the first sample as "DDD:HH:MM:SS" (with ".mmm" if
    /// <paramref name="includeMilliseconds"/>), the first sample being at <paramref name="baseDay"/>,
    /// <paramref name="baseTimeOffset"/> seconds after midnight.
    /// </summary>
    public static string Format(int baseDay, double baseTimeOffset, double elapsed, bool includeMilliseconds = false)
    {
        Library.RequireLoaded();
        return Text.Snprintf((buffer, capacity) =>
            Native.tmdq_clock_time_format(baseDay, baseTimeOffset, elapsed, includeMilliseconds ? 1 : 0, (byte*)buffer, capacity));
    }
}
