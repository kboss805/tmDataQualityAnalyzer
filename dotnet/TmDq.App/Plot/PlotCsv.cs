using System.Collections.Generic;
using TmDq.Core;

namespace TmDq.App.Plot
{
    /// <summary>
    /// The plot's series to and from the core's CSV (US6.1, US6.3). The core owns the format; this hands it each
    /// series' identity and samples, and turns a parsed file back into series as a run's would be.
    /// </summary>
    internal static class PlotCsv
    {
        /// <summary>Every series' column, shown or not; a <paramref name="sourceId"/> of 0 or more keeps that source's
        /// only (per-file batch export).</summary>
        public static List<CsvColumn> Columns(IEnumerable<PlotSeries> series, int sourceId)
        {
            var columns = new List<CsvColumn>();
            foreach (PlotSeries s in series)
            {
                if (sourceId >= 0 && s.SourceId != sourceId) continue;
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

        /// <summary>The file parsed, or null for one that is not an exported CSV (or cannot be read).</summary>
        public static CsvData? Parse(string path)
        {
            try
            {
                return CsvFile.Parse(path);
            }
            catch (TmdqException)
            {
                return null;
            }
        }

        /// <summary>
        /// A parsed file's series, numbered from <paramref name="nextId"/>: each Frame Sync Lock stream's series given
        /// the core's stream order (<c>assignImportedStreamOrders</c>), so its two metrics share a colour and each stream
        /// has its own; shown as <paramref name="view"/> shows a run's.
        /// </summary>
        public static List<PlotSeries> Series(CsvData data, LockAxisView view, ref int nextId)
        {
            IReadOnlyList<int> orders = ImportedStreams.AssignOrders(data.Series);
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
                    SourceId = c.SourceId,
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
}
