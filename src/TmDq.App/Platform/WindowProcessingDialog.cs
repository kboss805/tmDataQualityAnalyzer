using Avalonia.Controls;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.Views;

namespace TmDq.App.Platform
{
    /// <summary>The progress dialog as a window, modal to the main window.</summary>
    public sealed class WindowProcessingDialog : IProcessingDialog
    {
        private readonly Window _owner;
        private ProcessingWindow? _window;

        public WindowProcessingDialog(Window owner) => _owner = owner;

        public void Show(ProcessingViewModel vm)
        {
            _window = new ProcessingWindow { DataContext = vm };
            // Not awaited: the dialog stays up while the run goes on, and Close ends it.
            _ = _window.ShowDialog(_owner);
        }

        public void Close()
        {
            _window?.CloseWhenDone();
            _window = null;
        }
    }
}
