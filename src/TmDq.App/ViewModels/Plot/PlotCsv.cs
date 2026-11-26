using System.Collections.Generic;
using System.Linq;
using TmDq.App.Models.Export;
using TmDq.Core;

namespace TmDq.App.ViewModels.Plot;

/// <summary>
/// The plot's series to and from the core's CSV (US6.1, US6.3). The core owns the format; this hands it each
/// series' identity and samples, and turns a parsed file back into series as a run's would be.
/// </summary>
internal static class PlotCsv
{
    /// <summary>Whether <paramref name="series"/> is written by Export Data: the antenna's own system writes that log, so
    /// nothing of it is handed back (US10.0).</summary>
    public static bool IsExported(PlotSeries series) => !series.IsPhasedArray;

    /// <summary>Every series' column, shown or not; a <paramref name="sourceId"/> of 0 or more keeps that source's
    /// only (per-file batch export).</summary>
    public static List<CsvColumn> Columns(IEnumerable<PlotSeries> series, int sourceId)
    {
        var columns = new List<CsvColumn>();
        foreach (PlotSeries s in series)
        {
            if (sourceId >= 0 && s.SourceId != sourceId) continue;
            if (!IsExported(s)) continue;
            columns.Add(new CsvColumn(s.Metric, s.Name, s.SourceId, s.X, s.Y));
        }
        return columns;
    }

    /// <summary>Writes <paramref name="columns"/> with the clock <paramref name="clock"/>: false when the file cannot
    /// be written, and when there is nothing to write (no file then).</summary>
    public static bool Write(string path, List<CsvColumn> columns, TimeBaseInfo clock)
    {
        try
        {
            CsvFile.Write(path, columns, clock.BaseDay, clock.BaseTimeOffset);
            return true;
        }
        catch (TmdqException)
        {
            return false;
        }
    }

    /// <summary>
    /// The file parsed, with what it turned out to be (US6.3, US10.0): read as this application's own export first, then as an
    /// antenna's log; null for a file that is neither (or cannot be read). No one is asked: each format's reader refuses the other's
    /// at its first line (an export's starts <c>Time (DOY:HH:MM:SS.mmm)</c>, a log's <c>Time, Name, Polarity</c>), so a log costs
    /// the export reader one line.
    /// </summary>
    public static (CsvData Data, CsvKind Kind)? Parse(string path)
    {
        if (TryParse(() => CsvFile.Parse(path)) is { } export) return (export, CsvKind.TelemetryReceivers);
        if (TryParse(() => CsvFile.ParsePhasedArrayLog(path)) is { } log) return (log, CsvKind.PhasedArrayAntenna);
        return null;
    }

    private static CsvData? TryParse(System.Func<CsvData> parse)
    {
        try
        {
            return parse();
        }
        catch (TmdqException)
        {
            return null;
        }
    }

    /// <summary>
    /// A parsed file's series, numbered from <paramref name="nextId"/>: each Frame Sync Lock stream's series given
    /// the core's stream order (<c>assignImportedStreamOrders</c>), so its two metrics share a colour and each stream
    /// has its own; shown as <paramref name="view"/> shows a run's. A file added to a plot that already shows data
    /// (<paramref name="sourceId"/> its own recording, <paramref name="shift"/> the seconds its first sample comes after the
    /// plot's elapsed zero) has its times moved there, once for each array the series share. <paramref name="sourceMap"/>, for a
    /// file that holds several recordings (an export of a batch, whose columns carry <c>S&lt;n&gt;| </c> qualifiers), gives each
    /// of the file's source numbers the number it has on the plot it is added to, so that none meets a recording already there.
    /// </summary>
    public static List<PlotSeries> Series(CsvData data, LockAxisView view, ref int nextId, CsvKind kind = CsvKind.TelemetryReceivers,
                                          int? sourceId = null, double shift = 0.0, IReadOnlyDictionary<int, int>? sourceMap = null)
    {
        bool phasedArray = kind == CsvKind.PhasedArrayAntenna;
        IReadOnlyList<int> orders = ImportedStreams.AssignOrders(data.Series);
        if (shift != 0.0)
        {
            foreach (double[] xs in data.Series.Select(s => s.X).Distinct())
                for (int i = 0; i < xs.Length; i++) xs[i] += shift;
        }
        var series = new List<PlotSeries>(data.Series.Count);
        for (int i = 0; i < data.Series.Count; i++)
        {
            CsvSeries c = data.Series[i];
            series.Add(new PlotSeries
            {
                Id = nextId++,
                Name = c.Name,
                StreamLabel = c.StreamLabel,
                Metric = c.Metric,
                ReceiverIndex = c.ReceiverIndex,
                ChannelIndex = c.ChannelIndex,
                StreamOrder = orders[i],   // an SNR series keeps its own; the core gives the others theirs
                SourceId = sourceId ?? (sourceMap is not null ? sourceMap[c.SourceId] : c.SourceId),
                IsPhasedArray = phasedArray,
                X = c.X,
                Y = c.Y,
                YMin = c.YMin,
                YMax = c.YMax,
                SortedX = ChartGeometry.IsSortedX(c.X),
                Visible = PlotViewModel.ShownInView(c.Metric, view),
            });
        }
        return series;
    }
}
