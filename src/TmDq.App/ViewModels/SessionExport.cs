using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using TmDq.App.Logging;
using TmDq.App.Plot;
using TmDq.App.Services;

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

        public SessionExport(PlotViewModel plot, SessionLog log, IPlotImageExporter? images)
        {
            _plot = plot;
            _log = log;
            _images = images;
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
            if (choice.ExportData) Csv(choice.DataPath, -1, "Data exported to ", "Error: Failed to export data to ");
            if (choice.ExportImage) await ImageAsync(choice.ImagePath);
            if (choice.ExportLog)
            {
                if (WriteLog(choice.LogPath)) _log.Success("Log exported to " + choice.LogPath);
                else _log.Error("Error: Failed to export log to " + choice.LogPath);
            }
        }

        /// <summary>One recording's series as CSV, as a batch writes each file's: "Exported:" or "Failed to export CSV:".</summary>
        public void SourceCsv(string path, int sourceId) => Csv(path, sourceId, "Exported: ", "Failed to export CSV: ");

        /// <summary>The plot as an image at <paramref name="path"/> (its format by its suffix), and the log told which.</summary>
        public async Task ImageAsync(string path)
        {
            (string Path, ImageFormat Format)? written = _images is null ? null : await _images.ExportAsync(path);
            if (written is { } w) _log.Success($"Plot exported as {w.Format.ToString().ToUpperInvariant()} to {w.Path}");
            else _log.Error("Error: Failed to export plot to " + ImageFiles.Resolve(path).Path);
        }

        private void Csv(string path, int sourceId, string done, string failed)
        {
            if (_plot.ExportCsv(path, sourceId)) _log.Success(done + path);
            else _log.Error(failed + path);
        }

        /// <summary>The log as v2.14.2's export wrote it (US6.2): each line as the sidebar shows it, CR LF line ends,
        /// UTF-8 without a byte-order mark, ending with a line end.</summary>
        private bool WriteLog(string path)
        {
            string text = _log.PlainText.Replace("\n", "\r\n");
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
