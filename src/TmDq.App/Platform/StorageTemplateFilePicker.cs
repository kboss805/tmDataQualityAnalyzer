using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using TmDq.App.Services;

namespace TmDq.App.Platform
{
    /// <summary>The platform's file dialogs, owned by a window, with v2.x's titles and filters.</summary>
    public sealed class StorageTemplateFilePicker : ITemplateFilePicker
    {
        private static readonly FilePickerFileType Templates = new("Template Files") { Patterns = new[] { "*.json" } };
        private static readonly FilePickerFileType Ch10 = new("Chapter 10 Files") { Patterns = new[] { "*.ch10" } };

        private readonly TopLevel _owner;

        public StorageTemplateFilePicker(TopLevel owner) => _owner = owner;

        public async Task<string?> OpenTemplateAsync(string? folder)
        {
            var files = await _owner.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Apply Template to Files",
                AllowMultiple = false,
                FileTypeFilter = new[] { Templates },
                SuggestedStartLocation = await FolderAsync(folder),
            });
            return files.FirstOrDefault()?.TryGetLocalPath();
        }

        public async Task<string?> SaveTemplateAsync(string? folder)
        {
            IStorageFile? file = await _owner.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
            {
                Title = "Save as Template",
                FileTypeChoices = new[] { Templates },
                DefaultExtension = "json",
                SuggestedStartLocation = await FolderAsync(folder),
            });
            return file?.TryGetLocalPath();
        }

        public async Task<IReadOnlyList<string>> OpenRecordingsAsync(string? folder)
        {
            var files = await _owner.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Select Chapter 10 Files",
                AllowMultiple = true,
                FileTypeFilter = new[] { Ch10 },
                SuggestedStartLocation = await FolderAsync(folder),
            });
            return files.Select(f => f.TryGetLocalPath()).OfType<string>().ToList();
        }

        public async Task<string?> PickFolderAsync(string? folder)
        {
            var folders = await _owner.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
            {
                Title = "Select Output Folder",
                AllowMultiple = false,
                SuggestedStartLocation = await FolderAsync(folder),
            });
            return folders.FirstOrDefault()?.TryGetLocalPath();
        }

        private async Task<IStorageFolder?> FolderAsync(string? folder) =>
            string.IsNullOrEmpty(folder) || !Directory.Exists(folder)
                ? null
                : await _owner.StorageProvider.TryGetFolderFromPathAsync(new Uri(Path.GetFullPath(folder)));
    }
}
