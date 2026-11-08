using System.Threading.Tasks;

namespace TmDq.App.Services
{
    /// <summary>Asks the user for a settings file to load or a name to save one under (the setup dialogs' Load and Save
    /// buttons). An interface so the dialogs' view models can be tested without a file dialog.</summary>
    public interface ISettingsFilePicker
    {
        /// <summary>The chosen file's local path, or null if the user cancelled.</summary>
        /// <param name="folder">Where the dialog starts; null or a folder that does not exist leaves it to the platform.</param>
        Task<string?> OpenAsync(string title, string? folder);

        /// <summary>The path the user chose to save to, or null if they cancelled.</summary>
        Task<string?> SaveAsync(string title, string? folder);
    }
}
