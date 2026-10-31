using Avalonia.Controls;
using TmDq.App.ViewModels;
using TmDq.App.Views;

namespace TmDq.App.Services
{
    /// <summary>Shows the progress dialog for the length of a run. An interface so the view model can be tested without a
    /// window: a test's fake can press Cancel through the view model it is handed.</summary>
    public interface IProcessingDialog
    {
        /// <summary>Shows the dialog, modal to the main window; returns at once.</summary>
        void Show(ProcessingViewModel vm);

        /// <summary>Closes it, once the run has stopped.</summary>
        void Close();
    }

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
