using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;

namespace TmDq.App.Models.Templates
{
    /// <summary>A recording processed this session, as a template is captured from it: the rows Configure Streams had (every
    /// one, processed or not), the time channel's place in its list, and the byte order.</summary>
    public sealed record ProcessedSource(int SourceId, string FilePath, int TimeChannelIndex, bool SwapBytes,
                                         IReadOnlyList<StreamSetup> Rows);

    /// <summary>A file checked against a template: whether its channels match, and why not.</summary>
    public sealed record FileCheck(string FilePath, bool Ok, string Reason);

    /// <summary>
    /// The batch's rules without a window (plan step 4.7, US1.1), as v2.x had them: a file matches a
    /// template when its PCM channel ids are exactly the template's (the core's <c>TemplateMatcher</c>), a template is
    /// captured from one processed recording and the plot's series, and a template's names and colours are put back on a
    /// recording's fresh series.
    /// </summary>
    public static class TemplateBatch
    {
        /// <summary>The template's channel ids against <paramref name="fileChannelIds"/>: the ids only, never the names.</summary>
        public static TemplateMatchResult Match(ProcessingTemplate template, IEnumerable<int> fileChannelIds) =>
            TemplateMatcher.Match(template.Entries.Select(e => e.Config.PcmChannelId).ToList(), fileChannelIds.ToList());

        /// <summary>A mismatch as the confirmation dialog states it: the channels the template expects and the file lacks,
        /// then those the file has spare.</summary>
        public static string Describe(TemplateMatchResult match)
        {
            var parts = new List<string>();
            if (match.Missing.Count > 0) parts.Add("missing channel(s): " + string.Join(", ", match.Missing));
            if (match.Extra.Count > 0) parts.Add("extra channel(s): " + string.Join(", ", match.Extra));
            return string.Join("; ", parts);
        }

        /// <summary>Each file's channels read and matched against <paramref name="template"/>, in the order given.</summary>
        public static IReadOnlyList<FileCheck> Validate(ProcessingTemplate template, IEnumerable<string> files)
        {
            var checks = new List<FileCheck>();
            foreach (string path in files)
            {
                Ch10File file;
                try
                {
                    file = Ch10File.Open(path);
                }
                catch (TmdqException)
                {
                    checks.Add(new FileCheck(path, false, "could not read channels"));
                    continue;
                }
                TemplateMatchResult match = Match(template, file.PcmChannels.Select(c => c.Id));
                checks.Add(new FileCheck(path, match.Ok, match.Ok ? "" : Describe(match)));
            }
            return checks;
        }

        /// <summary>
        /// A template captured from <paramref name="source"/>, as v2.x captured it: this
        /// application's version, the recording's name up to its first dot, the time channel's place and the byte order,
        /// every row, and for each row the name and colour of every series it gave (of that recording, on its channel).
        /// </summary>
        public static ProcessingTemplate Build(ProcessedSource source, PlotViewModel plot) => new(
            AppInfo.Version,
            BaseName(Path.GetFileName(source.FilePath)),
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

        /// <summary>A file name up to its first dot (all of it without one), as v2.x named a template.</summary>
        public static string BaseName(string fileName)
        {
            int dot = fileName.IndexOf('.', StringComparison.Ordinal);
            return dot < 0 ? fileName : fileName[..dot];
        }

        /// <summary>The run a template gives for a recording: its processed rows, over the time channel at the template's
        /// place in the recording's list (the default when the list is shorter), in the template's byte order.</summary>
        public static RunRequest ToRunRequest(ProcessingTemplate template, string path, Ch10File file, int sourceId, string? appRoot)
        {
            IReadOnlyList<ChannelInfo> times = file.TimeChannels;
            int timeChannelId = template.TimeChannelIndex >= 0 && template.TimeChannelIndex < times.Count
                ? times[template.TimeChannelIndex].Id
                : file.DefaultTimeChannel;
            return StreamRules.ToRunRequest(path, timeChannelId, template.Entries.Select(e => e.Config).ToList(), !template.SwapBytes, appRoot)
                with { SourceId = sourceId };
        }
    }
}
