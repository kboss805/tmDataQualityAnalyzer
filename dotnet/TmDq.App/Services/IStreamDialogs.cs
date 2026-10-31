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
    }

    /// <summary>The dialogs as windows, each modal to the one that opened it.</summary>
    public sealed class WindowStreamDialogs : IStreamDialogs
    {
        private readonly Window _owner;
        private Window? _configure;

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
            await window.ShowDialog(_configure ?? _owner);
        }
    }
}
