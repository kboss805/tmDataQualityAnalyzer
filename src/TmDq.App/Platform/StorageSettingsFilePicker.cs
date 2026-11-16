using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using TmDq.App.Services;

namespace TmDq.App.Platform
{
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
                SuggestedStartLocation = await StorageDialogs.StartFolderAsync(_owner.StorageProvider, folder),
            });
            return StorageDialogs.FirstPath(files);
        }

        public async Task<string?> SaveAsync(string title, string? folder)
        {
            IStorageFile? file = await _owner.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
            {
                Title = title,
                FileTypeChoices = new[] { Toml, FilePickerFileTypes.All },
                SuggestedStartLocation = await StorageDialogs.StartFolderAsync(_owner.StorageProvider, folder),
            });
            return file?.TryGetLocalPath();
        }
    }
}
