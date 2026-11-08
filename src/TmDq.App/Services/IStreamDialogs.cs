using System.Threading.Tasks;
using TmDq.App.ViewModels;

namespace TmDq.App.Services
{
    /// <summary>Shows the stream dialogs modally. An interface so the view models can be tested without windows: a test's
    /// fake answers as a user would, through the view model it is handed.</summary>
    public interface IStreamDialogs
    {
        /// <summary>Shows Configure Streams; returns when it closes (<see cref="ConfigureStreamsViewModel.Accepted"/> says how).</summary>
        Task ConfigureStreamsAsync(ConfigureStreamsViewModel vm);

        /// <summary>Shows a stream's setup dialog; returns when it closes (<see cref="StreamSetupViewModel.Accepted"/> says how).</summary>
        Task EditStreamAsync(StreamSetupViewModel vm);

        /// <summary>Shows Extract Calibration over the setup dialog; returns when it closes
        /// (<see cref="ExtractCalibrationViewModel.Accepted"/> says how).</summary>
        Task ExtractCalibrationAsync(ExtractCalibrationViewModel vm);

        /// <summary>Shows a report (a calibration's) over the setup dialog, with OK; returns when it is closed.</summary>
        Task ShowReportAsync(string title, string text);
    }
}
