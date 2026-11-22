using System.Threading.Tasks;
using TmDq.App.ViewModels.Dialogs;

namespace TmDq.App.Services;

/// <summary>Shows the Export dialog modally; returns when it closes (<see cref="DialogViewModel.Accepted"/> says how).</summary>
public interface IExportDialog
{
    Task ShowAsync(ExportViewModel vm);
}