using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using TmDq.App.Plot;
using TmDq.App.ViewModels;
using TmDq.App.Views;

namespace TmDq.App.Services
{
    /// <summary>Asks the user for a CSV to import or a file to export to. An interface so the view models can be tested
    /// without a file dialog.</summary>
    public interface IDataFilePicker
    {
        /// <summary>The CSV chosen to import, or null if the user cancelled.</summary>
        /// <param name="folder">Where the dialog starts; null or a folder that does not exist leaves it to the platform.</param>
        Task<string?> OpenCsvAsync(string? folder);

        /// <summary>The path chosen for an export row's file, or null if the user cancelled.</summary>
        /// <param name="current">The row's path, where the dialog starts.</param>
        Task<string?> SaveExportAsync(ExportKind kind, string current);
    }

    /// <summary>Shows the Export dialog modally; returns when it closes (<see cref="ExportViewModel.Accepted"/> says how).</summary>
    public interface IExportDialog
    {
        Task ShowAsync(ExportViewModel vm);
    }

    /// <summary>Writes the plot as an image file.</summary>
    public interface IPlotImageExporter
    {
        /// <summary>The file written and its format, or null when it could not be written.</summary>
        Task<(string Path, ImageFormat Format)?> ExportAsync(string path);
    }

    /// <summary>The platform's file dialogs, owned by a window, with the Qt dialogs' titles and filters.</summary>
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

        public async Task<string?> OpenCsvAsync(string? folder)
        {
            var files = await _owner.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Import CSV File",
                AllowMultiple = false,
                FileTypeFilter = new[] { ExportedCsv },
                SuggestedStartLocation = await FolderAsync(folder),
            });
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

    /// <summary>The Export dialog as a window, modal to its owner.</summary>
    public sealed class WindowExportDialog : IExportDialog
    {
        private readonly Window _owner;

        public WindowExportDialog(Window owner) => _owner = owner;

        public async Task ShowAsync(ExportViewModel vm)
        {
            var window = new ExportWindow { DataContext = vm };
            vm.CloseRequested += (_, _) => window.Close();
            await window.ShowDialog(_owner);
        }
    }

    /// <summary>Exports the plot a <see cref="PlotView"/> shows.</summary>
    public sealed class PlotViewImageExporter : IPlotImageExporter
    {
        private readonly PlotView _view;

        public PlotViewImageExporter(PlotView view) => _view = view;

        public Task<(string Path, ImageFormat Format)?> ExportAsync(string path) => PlotImageExport.ExportAsync(_view, path);
    }
}
