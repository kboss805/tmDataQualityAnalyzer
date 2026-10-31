using System;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;

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

    /// <summary>The platform's file dialogs, owned by a window, filtered to TOML files.</summary>
    public sealed class StorageSettingsFilePicker : ISettingsFilePicker
    {
        private static readonly FilePickerFileType Toml = new("TOML files") { Patterns = new[] { "*.toml" } };

        private readonly TopLevel _owner;

        public StorageSettingsFilePicker(TopLevel owner) => _owner = owner;

        public async Task<string?> OpenAsync(string title, string? folder)
        {
            var files = await _owner.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = title,
                AllowMultiple = false,
                FileTypeFilter = new[] { Toml, FilePickerFileTypes.All },
                SuggestedStartLocation = await StartAt(folder),
            });
            return files.FirstOrDefault()?.TryGetLocalPath();
        }

        public async Task<string?> SaveAsync(string title, string? folder)
        {
            IStorageFile? file = await _owner.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
            {
                Title = title,
                FileTypeChoices = new[] { Toml, FilePickerFileTypes.All },
                SuggestedStartLocation = await StartAt(folder),
            });
            return file?.TryGetLocalPath();
        }

        private async Task<IStorageFolder?> StartAt(string? folder) =>
            string.IsNullOrEmpty(folder) || !System.IO.Directory.Exists(folder)
                ? null
                : await _owner.StorageProvider.TryGetFolderFromPathAsync(new Uri(System.IO.Path.GetFullPath(folder)));
    }
}
