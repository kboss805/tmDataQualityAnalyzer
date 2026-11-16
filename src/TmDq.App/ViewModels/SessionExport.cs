using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using TmDq.App.Models.Export;
using TmDq.App.Models.Logging;
using TmDq.App.Services;
using TmDq.App.ViewModels.Dialogs;
using TmDq.App.ViewModels.Plot;

namespace TmDq.App.ViewModels
{
    /// <summary>
    /// The session written out (US6.0 to US6.2): the plot's data as CSV, the plot as an image and the log as text, each
    /// saying in the log where it went or that it failed, in v2.14.2's words.
    /// </summary>
    internal sealed class SessionExport
    {
        private readonly PlotViewModel _plot;
        private readonly SessionLog _log;
        private readonly IPlotImageExporter? _images;
        private readonly Action<string, LogLevel> _report;

        /// <param name="report">The status line: what the Export dialog did, in a sentence.</param>
        public SessionExport(PlotViewModel plot, SessionLog log, IPlotImageExporter? images, Action<string, LogLevel> report)
        {
            _plot = plot;
            _log = log;
            _images = images;
            _report = report;
        }

        /// <summary>Whether there is anything to write an image with.</summary>
        public bool CanWriteImages => _images is not null;

        /// <summary>
        /// Writes what <paramref name="choice"/> asks for, in v2.x's order (the data, the image, then the log, which so
        /// carries the lines about the other two); a failure does not stop the rest. Nothing without data.
        /// </summary>
        public async Task WriteAsync(ExportViewModel choice)
        {
            if (!_plot.HasData) return;
            var done = new List<string>();
            var failed = new List<string>();
            void Note(bool ok, string what) => (ok ? done : failed).Add(what);
            if (choice.ExportData) Note(Csv(choice.DataPath, -1, "Data exported to ", "Error: Failed to export data to "), "data");
            if (choice.ExportImage) Note(await ImageAsync(choice.ImagePath), "plot image");
            if (choice.ExportLog)
            {
                bool ok = WriteLog(choice.LogPath);
                if (ok) _log.Success("Log exported to " + choice.LogPath);
                else _log.Error("Error: Failed to export log to " + choice.LogPath);
                Note(ok, "log file");
            }
            // One sentence for the status bar (the log has each path): what was written, and what was not.
            if (failed.Count == 0) _report("Exported " + Sentence(done) + ".", LogLevel.Success);
            else if (done.Count == 0) _report("Could not export " + Sentence(failed) + ". See the log.", LogLevel.Error);
            else _report("Exported " + Sentence(done) + ". Could not export " + Sentence(failed) + ". See the log.", LogLevel.Warning);
        }

        /// <summary>"data", "data and log file", "data, plot image and log file".</summary>
        private static string Sentence(List<string> items) =>
            items.Count <= 1 ? string.Concat(items) : string.Join(", ", items.Take(items.Count - 1)) + " and " + items[^1];

        /// <summary>One recording's series as CSV, as a batch writes each file's: "Exported:" or "Failed to export CSV:".</summary>
        public void SourceCsv(string path, int sourceId) => Csv(path, sourceId, "Exported: ", "Failed to export CSV: ");

        /// <summary>The plot as an image at <paramref name="path"/> (its format by its suffix), and the log told which.</summary>
        public async Task<bool> ImageAsync(string path)
        {
            (string Path, ImageFormat Format)? written = _images is null ? null : await _images.ExportAsync(path);
            if (written is { } w) _log.Success($"Plot exported as {w.Format.ToString().ToUpperInvariant()} to {w.Path}");
            else _log.Error("Error: Failed to export plot to " + ImageFiles.Resolve(path).Path);
            return written is not null;
        }

        private bool Csv(string path, int sourceId, string done, string failed)
        {
            bool ok = _plot.ExportCsv(path, sourceId);
            if (ok) _log.Success(done + path);
            else _log.Error(failed + path);
            return ok;
        }

        /// <summary>The log as v2.14.2's export wrote it (US6.2): each line as the sidebar shows it, CR LF line ends,
        /// UTF-8 without a byte-order mark, ending with a line end.</summary>
        private bool WriteLog(string path)
        {
            string text = _log.PlainText.Replace("\n", "\r\n", StringComparison.Ordinal);
            if (!text.EndsWith("\r\n", StringComparison.Ordinal)) text += "\r\n";
            try
            {
                File.WriteAllText(path, text, new UTF8Encoding(false));
                return true;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
            {
                return false;
            }
        }
    }
}
