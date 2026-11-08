using System.Threading.Tasks;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Plot;

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
}
