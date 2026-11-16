using System.Threading.Tasks;
using Avalonia.Controls;
using TmDq.App.Services;
using TmDq.App.ViewModels.Dialogs;
using TmDq.App.Views.Dialogs;

namespace TmDq.App.Platform
{
    /// <summary>The Export dialog as a window, modal to its owner.</summary>
    public sealed class WindowExportDialog : IExportDialog
    {
        private readonly Window _owner;

        public WindowExportDialog(Window owner) => _owner = owner;

        public async Task ShowAsync(ExportViewModel vm)
        {
            await Modal.ShowAsync(new ExportWindow(), vm, _owner);
        }
    }
}
