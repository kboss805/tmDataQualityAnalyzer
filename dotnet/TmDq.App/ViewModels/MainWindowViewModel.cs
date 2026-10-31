using System;
using System.IO;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Processing;
using TmDq.App.Services;

namespace TmDq.App.ViewModels
{
    /// <summary>The main window's state. The walking skeleton (plan step 3.4): open a recording, process its first PCM
    /// channel with the Frame Sync Lock defaults, show the lock percentage. Ugly on purpose; step 4 builds the real UI.</summary>
    public sealed partial class MainWindowViewModel : ObservableObject
    {
        private readonly IRecordingPicker? _picker;
        private string _status = "Open a Chapter 10 recording.";
        private int _progress;
        private bool _isBusy;
        private LockSeries? _series;

        public MainWindowViewModel(IRecordingPicker? picker = null)
        {
            _picker = picker;
            OpenCommand = new AsyncRelayCommand(OpenAsync, () => !IsBusy && _picker is not null);
        }

        public string Title => AppInfo.Title;

        public string VersionText => "Version " + AppInfo.Version;

        /// <summary>One line saying what happened last, an error included.</summary>
        public string Status
        {
            get => _status;
            private set => SetProperty(ref _status, value);
        }

        /// <summary>0 to 100 while a recording is processed.</summary>
        public int Progress
        {
            get => _progress;
            private set => SetProperty(ref _progress, value);
        }

        public bool IsBusy
        {
            get => _isBusy;
            private set
            {
                if (SetProperty(ref _isBusy, value)) OpenCommand.NotifyCanExecuteChanged();
            }
        }

        /// <summary>The processed stream, or null before the first recording and after a failure.</summary>
        public LockSeries? Series
        {
            get => _series;
            private set => SetProperty(ref _series, value);
        }

        public IAsyncRelayCommand OpenCommand { get; }

        private async Task OpenAsync()
        {
            string? path = await _picker!.PickRecordingAsync();
            if (path is not null) await LoadAsync(path);
        }

        /// <summary>Processes <paramref name="path"/> and shows the result or the reason it failed. Ignored while busy.</summary>
        public async Task LoadAsync(string path)
        {
            if (IsBusy) return;
            string name = Path.GetFileName(path);
            IsBusy = true;
            Series = null;
            Progress = 0;
            Status = $"Processing {name} ...";
            try
            {
                // Progress<T> made here posts each report back to this (the UI) thread.
                LockSeries series = await FirstChannelRun.RunAsync(path, new Progress<int>(p => Progress = p));
                Progress = 100;
                Series = series;
                Status = $"{name}: {series.Label} (channel {series.PcmChannelId}), {series.LockPercent.Length} samples, mean lock {series.MeanLock:F1} %";
            }
            catch (RecordingException e)
            {
                Status = e.Message;
            }
            catch (Exception e) when (e is FileNotFoundException or InvalidOperationException)
            {
                // The native core is missing or does not match this build.
                Status = "The processing core could not be loaded: " + e.Message;
            }
            finally
            {
                IsBusy = false;
            }
        }
    }
}
