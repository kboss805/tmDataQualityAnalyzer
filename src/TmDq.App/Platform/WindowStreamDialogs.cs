using System.Threading.Tasks;
using Avalonia.Controls;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.Views;

namespace TmDq.App.Platform
{
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
            // The extraction's progress sits over this dialog, modal to it, as in v2.x.
            vm.ProgressDialog = new WindowProcessingDialog(window);
            await window.ShowDialog(Top);
        }

        public Task ShowReportAsync(string title, string text) => new ReportWindow(title, text).ShowDialog(Top);
    }
}
