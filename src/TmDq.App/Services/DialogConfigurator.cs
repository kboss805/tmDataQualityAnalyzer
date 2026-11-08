using System.Threading.Tasks;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Settings;
using TmDq.App.ViewModels;
using TmDq.Core;

namespace TmDq.App.Services
{
    /// <summary>Asks the user, through Configure Streams, what to process (US1.0).</summary>
    public sealed class DialogConfigurator : IStreamConfigurator
    {
        private readonly IStreamDialogs _dialogs;
        private readonly ISettingsFilePicker? _files;
        private readonly string? _appRoot;
        private readonly ICalibrationFilePicker? _calibrationFiles;
        private readonly PreferencesStore? _preferences;

        /// <param name="appRoot">Where settings/ is: the defaults files, and the shipped receiver word map.</param>
        /// <param name="calibrationFiles">Extract Calibration's file dialogs.</param>
        /// <param name="preferences">Where the last calibration recording's folder is kept between sessions.</param>
        public DialogConfigurator(IStreamDialogs dialogs, ISettingsFilePicker? files, string? appRoot, ICalibrationFilePicker? calibrationFiles = null,
                                  PreferencesStore? preferences = null)
        {
            _dialogs = dialogs;
            _files = files;
            _appRoot = appRoot;
            _calibrationFiles = calibrationFiles;
            _preferences = preferences;
        }

        public async Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file)
        {
            var vm = new ConfigureStreamsViewModel(path, file, _dialogs, _files, _appRoot, _calibrationFiles,
                                                   _preferences?.Current.LastCalibrationFolder);
            await _dialogs.ConfigureStreamsAsync(vm);
            _preferences?.Update(p => p with { LastCalibrationFolder = vm.LastCalFolder });
            return vm.Accepted ? new StreamsChoice(vm.ToRunRequest(), vm.Setups, vm.TimeChannelIndex) : null;
        }
    }
}
