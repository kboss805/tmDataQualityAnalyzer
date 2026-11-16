using System.Threading.Tasks;

namespace TmDq.App.Services
{
    /// <summary>Help &gt; User Manual and Help &gt; About. An interface so the menu's two Help entries are view-model
    /// commands like the rest, and a test can see them asked without a browser or a window.</summary>
    public interface IHelpActions
    {
        /// <summary>Opens the user manual; says so over the main window when it cannot.</summary>
        Task OpenManualAsync();

        /// <summary>Shows About over the main window.</summary>
        Task ShowAboutAsync();
    }
}
