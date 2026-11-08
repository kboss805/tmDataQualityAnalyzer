using System.IO;
using System.Linq;
using TmDq.App.Models.Templates;

namespace TmDq.App.ViewModels.Plot
{
    /// <summary>
    /// Templates and the plot (US1.1): a template captured from a processed recording's series, and a template's names
    /// and colours put back on a recording's series. Here, beside the plot, because both read or change it; the rest of
    /// a template's work, which knows no plot, is <see cref="TemplateBatch"/>'s.
    /// </summary>
    public static class PlotTemplates
    {
        /// <summary>
        /// A template captured from <paramref name="source"/>, as v2.x captured it: this
        /// application's version, the recording's name up to its first dot, the time channel's place and the byte order,
        /// every row, and for each row the name and colour of every series it gave (of that recording, on its channel).
        /// </summary>
        public static ProcessingTemplate Build(ProcessedSource source, PlotViewModel plot) => new(
            AppInfo.Version,
            TemplateBatch.BaseName(Path.GetFileName(source.FilePath)),
            source.TimeChannelIndex,
            source.SwapBytes,
            source.Rows.Select(row => new TemplateEntry(
                row,
                plot.Series.Where(s => s.SourceId == source.SourceId && s.StreamOrder == row.PcmChannelId)
                    .Select(s => new SeriesAppearance(s.Metric, s.ReceiverIndex, s.ChannelIndex, s.Name, s.Color))
                    .ToList())).ToList());

        /// <summary>
        /// Puts <paramref name="template"/>'s names and colours back on <paramref name="sourceId"/>'s series, as v2.x
        /// put them back: each look goes to the first series of that recording with its channel, metric,
        /// receiver and channel index; then one refresh.
        /// </summary>
        public static void ApplyAppearance(ProcessingTemplate template, int sourceId, PlotViewModel plot)
        {
            foreach (TemplateEntry entry in template.Entries)
                foreach (SeriesAppearance look in entry.Appearance)
                {
                    PlotSeries? s = plot.Series.FirstOrDefault(x => x.SourceId == sourceId && x.StreamOrder == entry.Config.PcmChannelId &&
                                                                    x.Metric == look.Metric && x.ReceiverIndex == look.ReceiverIndex &&
                                                                    x.ChannelIndex == look.ChannelIndex);
                    if (s is null) continue;
                    plot.RenameSeries(s.Id, look.Name);
                    plot.RecolorSeries(s.Id, look.Color);
                }
            plot.CommitAppearance();
        }
    }
}
