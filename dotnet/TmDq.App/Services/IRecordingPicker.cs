using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;

namespace TmDq.App.Services
{
    /// <summary>Asks the user for a recording. An interface so the view model can be tested without a file dialog.</summary>
    public interface IRecordingPicker
    {
        /// <summary>The chosen file's local path, or null if the user cancelled.</summary>
        Task<string?> PickRecordingAsync();
    }

    /// <summary>The platform's open-file dialog, owned by a window.</summary>
    public sealed class StorageRecordingPicker : IRecordingPicker
    {
        private static readonly FilePickerFileType Ch10 = new("Chapter 10 recordings") { Patterns = new[] { "*.ch10" } };

        private readonly TopLevel _owner;

        public StorageRecordingPicker(TopLevel owner) => _owner = owner;

        public async Task<string?> PickRecordingAsync()
        {
            var files = await _owner.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Open a Chapter 10 recording",
                AllowMultiple = false,
                FileTypeFilter = new[] { Ch10, FilePickerFileTypes.All },
            });
            return files.FirstOrDefault()?.TryGetLocalPath();
        }
    }
}
