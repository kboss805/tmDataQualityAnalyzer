using System.Threading.Tasks;
using Avalonia.Controls;
using TmDq.App.ViewModels.Dialogs;

namespace TmDq.App.Platform
{
    /// <summary>What every dialog host does with a dialog window: gives it its view model, closes it when the view model
    /// asks, and shows it modal to its owner.</summary>
    internal static class Modal
    {
        public static Task ShowAsync(Window window, DialogViewModel vm, Window owner)
        {
            window.DataContext = vm;
            vm.CloseRequested += (_, _) => window.Close();
            return window.ShowDialog(owner);
        }
    }
}
