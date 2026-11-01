using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;

namespace TmDq.App.Services
{
    /// <summary>Asks the user for the Extract Calibration dialog's two files. An interface so the dialog's view model can be
    /// tested without a file dialog.</summary>
    public interface ICalibrationFilePicker
    {
        /// <summary>The step configuration chosen, or null if the user cancelled.</summary>
        /// <param name="folder">Where the dialog starts; null or a folder that does not exist leaves it to the platform.</param>
        Task<string?> OpenStepFileAsync(string? folder);

        /// <summary>The calibration recording chosen, or null if the user cancelled.</summary>
        Task<string?> OpenCalibrationRecordingAsync(string? folder);
    }

    /// <summary>The platform's open dialogs, owned by a window, with the Qt dialog's titles and filters.</summary>
    public sealed class StorageCalibrationFilePicker : ICalibrationFilePicker
    {
        private static readonly FilePickerFileType Toml = new("TOML Files") { Patterns = new[] { "*.toml" } };
        private static readonly FilePickerFileType Ch10 = new("Chapter 10 Files") { Patterns = new[] { "*.ch10", "*.c10" } };

        private readonly TopLevel _owner;

        public StorageCalibrationFilePicker(TopLevel owner) => _owner = owner;

        public Task<string?> OpenStepFileAsync(string? folder) =>
            OpenAsync("Select Step Configuration (TOML)", Toml, folder);

        public Task<string?> OpenCalibrationRecordingAsync(string? folder) =>
            OpenAsync("Select Calibration Chapter 10 File", Ch10, folder);

        private async Task<string?> OpenAsync(string title, FilePickerFileType type, string? folder)
        {
            var files = await _owner.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = title,
                AllowMultiple = false,
                FileTypeFilter = new[] { type, FilePickerFileTypes.All },
                SuggestedStartLocation = string.IsNullOrEmpty(folder) || !Directory.Exists(folder)
                    ? null
                    : await _owner.StorageProvider.TryGetFolderFromPathAsync(new Uri(Path.GetFullPath(folder))),
            });
            return files.FirstOrDefault()?.TryGetLocalPath();
        }
    }
}
