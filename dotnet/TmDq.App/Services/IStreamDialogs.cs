using System.Threading.Tasks;
using Avalonia.Controls;
using TmDq.App.ViewModels;
using TmDq.App.Views;

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

    /// <summary>The dialogs as windows, each modal to the one that opened it.</summary>
    public sealed class WindowStreamDialogs : IStreamDialogs
    {
        private readonly Window _owner;
        private Window? _configure, _setup;

        private Window Top => _setup ?? _configure ?? _owner;

        public WindowStreamDialogs(Window owner) => _owner = owner;

        public async Task ConfigureStreamsAsync(ConfigureStreamsViewModel vm)
        {
            var window = new ConfigureStreamsWindow { DataContext = vm };
            vm.CloseRequested += (_, _) => window.Close();
            _configure = window;
            try
            {
                await window.ShowDialog(_owner);
            }
            finally
            {
                _configure = null;
            }
        }

        public async Task EditStreamAsync(StreamSetupViewModel vm)
        {
            var window = new StreamSetupWindow { DataContext = vm };
            vm.CloseRequested += (_, _) => window.Close();
            Window owner = Top;
            _setup = window;
            try
            {
                await window.ShowDialog(owner);
            }
            finally
            {
                _setup = null;
            }
        }

        public async Task ExtractCalibrationAsync(ExtractCalibrationViewModel vm)
        {
            var window = new ExtractCalibrationWindow { DataContext = vm };
            vm.CloseRequested += (_, _) => window.Close();
            // The extraction's progress sits over this dialog, as the Qt one is window-modal to it.
            vm.ProgressDialog = new WindowProcessingDialog(window);
            await window.ShowDialog(Top);
        }

        public Task ShowReportAsync(string title, string text) => new ReportWindow(title, text).ShowDialog(Top);
    }
}
