using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using TmDq.App.Services;
using TmDq.App.ViewModels;

namespace TmDq.App.Platform
{
    /// <summary>The platform's file dialogs, owned by a window, with v2.x's titles and filters.</summary>
    public sealed class StorageDataFilePicker : IDataFilePicker
    {
        private static readonly FilePickerFileType ExportedCsv = new("Exported CSV") { Patterns = new[] { "*.csv" } };
        private static readonly FilePickerFileType Csv = new("CSV Files") { Patterns = new[] { "*.csv" } };
        private static readonly FilePickerFileType Png = new("PNG Files") { Patterns = new[] { "*.png" } };
        private static readonly FilePickerFileType Pdf = new("PDF Files") { Patterns = new[] { "*.pdf" } };
        private static readonly FilePickerFileType Svg = new("SVG Files") { Patterns = new[] { "*.svg" } };
        private static readonly FilePickerFileType Text = new("Text Files") { Patterns = new[] { "*.txt" } };

        private readonly TopLevel _owner;

        public StorageDataFilePicker(TopLevel owner) => _owner = owner;

        /// <summary>What Import CSV offers: exported CSVs only (US6.3), starting at <paramref name="start"/>.</summary>
        public static FilePickerOpenOptions CsvOptions(IStorageFolder? start) => new()
        {
            Title = "Import CSV File",
            AllowMultiple = false,
            FileTypeFilter = new[] { ExportedCsv },
            SuggestedStartLocation = start,
        };

        public async Task<string?> OpenCsvAsync(string? folder)
        {
            var files = await _owner.StorageProvider.OpenFilePickerAsync(CsvOptions(await FolderAsync(folder)));
            return files.FirstOrDefault()?.TryGetLocalPath();
        }

        public async Task<string?> SaveExportAsync(ExportKind kind, string current)
        {
            (string title, FilePickerFileType[] types) = kind switch
            {
                ExportKind.Image => ("Export Plot Image", new[] { Png, Pdf, Svg, FilePickerFileTypes.All }),
                ExportKind.Data => ("Export CSV Data", new[] { Csv, FilePickerFileTypes.All }),
                _ => ("Export Log", new[] { Text, FilePickerFileTypes.All }),
            };
            IStorageFile? file = await _owner.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
            {
                Title = title,
                FileTypeChoices = types,
                SuggestedFileName = Path.GetFileName(current),
                SuggestedStartLocation = await FolderAsync(Path.GetDirectoryName(current)),
            });
            return file?.TryGetLocalPath();
        }

        private async Task<IStorageFolder?> FolderAsync(string? folder) =>
            string.IsNullOrEmpty(folder) || !Directory.Exists(folder)
                ? null
                : await _owner.StorageProvider.TryGetFolderFromPathAsync(new Uri(Path.GetFullPath(folder)));
    }
}
