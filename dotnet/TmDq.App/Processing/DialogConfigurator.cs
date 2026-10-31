using System.Threading.Tasks;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.Core;

namespace TmDq.App.Processing
{
    /// <summary>Asks the user, through Configure Streams, what to process (US1.0).</summary>
    public sealed class DialogConfigurator : IStreamConfigurator
    {
        private readonly IStreamDialogs _dialogs;
        private readonly ISettingsFilePicker? _files;
        private readonly string? _appRoot;

        /// <param name="appRoot">Where settings/ is: the defaults files, and the shipped receiver word map.</param>
        public DialogConfigurator(IStreamDialogs dialogs, ISettingsFilePicker? files, string? appRoot)
        {
            _dialogs = dialogs;
            _files = files;
            _appRoot = appRoot;
        }

        public async Task<RunRequest?> ConfigureAsync(string path, Ch10File file)
        {
            var vm = new ConfigureStreamsViewModel(path, file, _dialogs, _files, _appRoot);
            await _dialogs.ConfigureStreamsAsync(vm);
            return vm.Accepted ? vm.ToRunRequest() : null;
        }
    }
}
