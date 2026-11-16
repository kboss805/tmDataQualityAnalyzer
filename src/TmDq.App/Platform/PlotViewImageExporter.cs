using System.Threading.Tasks;
using TmDq.App.Models.Export;
using TmDq.App.Services;
using TmDq.App.Views.Plot;

namespace TmDq.App.Platform;

/// <summary>Exports the plot a <see cref="PlotView"/> shows.</summary>
public sealed class PlotViewImageExporter : IPlotImageExporter
{
    private readonly PlotView _view;

    public PlotViewImageExporter(PlotView view) => _view = view;

    public Task<(string Path, ImageFormat Format)?> ExportAsync(string path) => PlotImageExport.ExportAsync(_view, path);
}
