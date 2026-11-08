using System.Threading.Tasks;

namespace TmDq.App.Services
{
    /// <summary>Asks the user for a recording. An interface so the view model can be tested without a file dialog.</summary>
    public interface IRecordingPicker
    {
        /// <summary>The chosen file's local path, or null if the user cancelled.</summary>
        Task<string?> PickRecordingAsync();
    }
}
