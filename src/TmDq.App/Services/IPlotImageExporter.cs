using System.Threading.Tasks;
using TmDq.App.Models.Export;

namespace TmDq.App.Services;

/// <summary>Writes the plot as an image file.</summary>
public interface IPlotImageExporter
{
    /// <summary>The file written and its format, or null when it could not be written.</summary>
    Task<(string Path, ImageFormat Format)?> ExportAsync(string path);
}