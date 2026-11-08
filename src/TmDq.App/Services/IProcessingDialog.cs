using TmDq.App.ViewModels;

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
}
