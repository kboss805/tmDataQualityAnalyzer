using System;
using System.Collections.Generic;

namespace TmDq.Core;

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
