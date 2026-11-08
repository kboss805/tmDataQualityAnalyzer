using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using TmDq.App.Models.Logging;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Templates;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;

namespace TmDq.App.ViewModels
{
    /// <summary>What a batch needs from the window it runs in: the status line, the file being run (named in the warning
    /// that a recording shares no time with the others), and a run with the progress dialog up.</summary>
    internal interface IRunHost
    {
        void SetStatus(string status);

        void SetRunningFile(string name);

        Task<RunOutcome> RunWithDialogAsync(RunRequest request);
    }

    /// <summary>What a batch did: the recordings it kept, and the summary for the status line.</summary>
    internal sealed record BatchResult(IReadOnlyList<ProcessedSource> Sources, string Summary);

    /// <summary>
    /// Apply Template's batch (US1.1), as v2.x's ran it: the template over several recordings, one after another, each
    /// kept in the plot as a recording of its own; then, when a folder is given, a CSV and plot images per file.
    /// </summary>
    internal sealed class BatchRunner
    {
        private readonly IRunHost _host;
        private readonly PlotViewModel _plot;
        private readonly SessionLog _log;
        private readonly SessionExport _export;
        private readonly string _appRoot;

        public BatchRunner(IRunHost host, PlotViewModel plot, SessionLog log, SessionExport export, string appRoot)
        {
            _host = host;
            _plot = plot;
            _log = log;
            _export = export;
            _appRoot = appRoot;
        }

        /// <summary>
        /// Runs <paramref name="template"/> over <paramref name="files"/> on a cleared plot: each file checked again and
        /// run with the template's rows, kept in the plot as a recording of its own, named for the Plot File selector,
        /// with the template's names and colours when asked; a file that is gone, no longer matches, or fails is skipped
        /// and said so. Then, when a folder is given, each file's CSV and images; and the plot left on the first file.
        /// </summary>
        /// <param name="outputFolder">Where each file's CSV and images go; null exports nothing.</param>
        public async Task<BatchResult> RunAsync(ProcessingTemplate template, IReadOnlyList<string> files, string? outputFolder,
                                                bool reuseAppearance)
        {
            int processed = 0, skipped = 0, nextSourceId = 0;
            var sources = new List<ProcessedSource>();
            _log.Success(string.Create(CultureInfo.InvariantCulture, $"Processing {files.Count} file(s)..."));
            foreach (string path in files)
            {
                if (!File.Exists(path))
                {
                    _log.Warning("Skipped missing batch file: " + path);
                    skipped++;
                    continue;
                }
                string name = Path.GetFileName(path), baseName = MainWindowViewModel.BaseName(name);
                int sourceId = nextSourceId++;
                try
                {
                    _host.SetStatus($"Opening {name} ...");
                    _log.Add("Opening: " + name);
                    Ch10File file = await StreamsRun.OpenAsync(path);
                    // Checked again at its turn: the file may have changed since it was chosen.
                    if (!TemplateBatch.Match(template, file.PcmChannels.Select(c => c.Id)).Ok)
                    {
                        _log.Warning("Skipped (channels no longer match template): " + name);
                        skipped++;
                        continue;
                    }
                    _host.SetStatus($"Processing {name} ...");
                    _host.SetRunningFile(name);
                    using RunOutcome outcome = await _host.RunWithDialogAsync(TemplateBatch.ToRunRequest(template, path, file, sourceId, _appRoot));
                    foreach (StreamResult result in outcome.Results) _plot.AddStream(result);
                    processed++;
                    sources.Add(new ProcessedSource(sourceId, path, template.TimeChannelIndex, template.SwapBytes,
                                                    template.Entries.Select(e => e.Config).ToList()));
                    _plot.SetSourceLabel(sourceId, baseName);
                    if (reuseAppearance) PlotTemplates.ApplyAppearance(template, sourceId, _plot);
                }
                catch (Exception e) when (e is OperationCanceledException or RecordingException)
                {
                    skipped++;
                    _log.Error("Batch file failed to process: " + baseName);
                }
            }

            if (outputFolder is not null) await ExportEachAsync(sources, outputFolder);

            // The plot opens on the first file; "All files (overlaid)" is in the Plot File selector.
            if (sources.Count > 1) _plot.SetVisibleSource(sources[0].SourceId);
            if (sources.Count > 0) _plot.Title = MainWindowViewModel.BaseName(Path.GetFileName(sources[0].FilePath));
            string summary = string.Create(CultureInfo.InvariantCulture, $"Batch complete: {processed} processed, {skipped} skipped.") +
                             (outputFolder is null ? "" : " Output written to " + outputFolder + ".");
            return new BatchResult(sources, summary);
        }

        /// <summary>
        /// Each recording on its own, as v2.x's batch wrote it: its CSV (every series of that recording), then with
        /// frame-sync data an image of the lock % and one of the missed frames, else one image; the view put back after.
        /// </summary>
        private async Task ExportEachAsync(IReadOnlyList<ProcessedSource> sources, string folder)
        {
            LockAxisView original = _plot.LockAxisView;
            bool frameSync = _plot.HasLockSeries || _plot.HasMissedFramesSeries;
            foreach (ProcessedSource source in sources)
            {
                string baseName = MainWindowViewModel.BaseName(Path.GetFileName(source.FilePath));
                _plot.SetVisibleSource(source.SourceId);
                _export.SourceCsv(Path.Combine(folder, baseName + ".csv"), source.SourceId);

                if (!_export.CanWriteImages) continue;
                if (frameSync)
                {
                    _plot.SetLockAxisView(LockAxisView.LockPercent);
                    await _export.ImageAsync(Path.Combine(folder, baseName + "_framesync_lock.png"));
                    _plot.SetLockAxisView(LockAxisView.MissedFrames);
                    await _export.ImageAsync(Path.Combine(folder, baseName + "_missed_frames.png"));
                }
                else
                {
                    await _export.ImageAsync(Path.Combine(folder, baseName + ".png"));
                }
            }
            _plot.SetLockAxisView(original);
        }
    }
}
