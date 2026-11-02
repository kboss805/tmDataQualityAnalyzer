using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using TmDq.App.ViewModels;
using TmDq.App.Views;

namespace TmDq.App.Services
{
    /// <summary>Asks the user for the files the template commands need. An interface so the view models can be tested
    /// without a file dialog.</summary>
    public interface ITemplateFilePicker
    {
        /// <summary>The template to apply, or null if the user cancelled.</summary>
        Task<string?> OpenTemplateAsync(string? folder);

        /// <summary>Where to save a template, or null if the user cancelled.</summary>
        Task<string?> SaveTemplateAsync(string? folder);

        /// <summary>The recordings to run a template over (several at once), or an empty list if the user cancelled.</summary>
        Task<IReadOnlyList<string>> OpenRecordingsAsync(string? folder);

        /// <summary>The folder chosen for a batch's per-file export, or null if the user cancelled.</summary>
        Task<string?> PickFolderAsync(string? folder);
    }

    /// <summary>Shows the template commands' dialogs modally. An interface so the view model can be tested without windows.</summary>
    public interface ITemplateDialogs
    {
        /// <summary>Shows the batch confirmation; returns when it closes (<see cref="BatchApplyViewModel.Accepted"/> says how).</summary>
        Task ConfirmBatchAsync(BatchApplyViewModel vm);

        /// <summary>Which of <paramref name="choices"/> to capture a template from; null if the user cancelled.</summary>
        Task<int?> ChooseSourceAsync(IReadOnlyList<string> choices);
    }

    /// <summary>The platform's file dialogs, owned by a window, with the Qt dialogs' titles and filters.</summary>
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

    /// <summary>The template commands' dialogs as windows, modal to the main window.</summary>
    public sealed class WindowTemplateDialogs : ITemplateDialogs
    {
        private readonly Window _owner;

        public WindowTemplateDialogs(Window owner) => _owner = owner;

        public async Task ConfirmBatchAsync(BatchApplyViewModel vm)
        {
            var window = new BatchApplyWindow { DataContext = vm };
            vm.CloseRequested += (_, _) => window.Close();
            await window.ShowDialog(_owner);
        }

        public Task<int?> ChooseSourceAsync(IReadOnlyList<string> choices) =>
            new ChoiceWindow("Save as Template", "Capture the per-stream settings from which source?", choices).ShowDialog<int?>(_owner);
    }
}
