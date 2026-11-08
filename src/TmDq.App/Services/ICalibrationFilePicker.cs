using System.Threading.Tasks;

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
}
