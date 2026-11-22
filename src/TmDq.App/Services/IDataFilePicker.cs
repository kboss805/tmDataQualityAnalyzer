using System.Threading.Tasks;
using TmDq.App.Models.Export;

namespace TmDq.App.Services;

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