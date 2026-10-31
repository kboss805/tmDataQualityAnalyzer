using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.Core;

namespace TmDq.App.ViewModels
{
    /// <summary>The main window's state: open a recording, ask what to process (Configure Streams, plan step 4.1; with no
    /// one to ask, the first PCM channel with the Frame Sync Lock defaults, as the walking skeleton did), process it and
    /// show the lock percentage. The chart is still the skeleton's; plan step 4.3 builds the real one.</summary>
    public sealed partial class MainWindowViewModel : ObservableObject
    {
        private readonly IRecordingPicker? _picker;
        private readonly IStreamConfigurator _configurator;
        private string _status = "Open a Chapter 10 recording.";
        private int _progress;
        private bool _isBusy;
        private LockSeries? _series;
        private IReadOnlyList<LockSeries> _allSeries = Array.Empty<LockSeries>();

        /// <param name="configurator">What a recording is processed with; null means the first PCM channel with the
        /// Frame Sync Lock defaults (<see cref="FirstChannelRun"/>).</param>
        public MainWindowViewModel(IRecordingPicker? picker = null, IStreamConfigurator? configurator = null)
        {
            _picker = picker;
            _configurator = configurator ?? new FirstChannelRun();
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

        /// <summary>The first Frame Sync Lock stream processed, or null before the first recording, after a failure, and
        /// when no such stream was processed.</summary>
        public LockSeries? Series
        {
            get => _series;
            private set => SetProperty(ref _series, value);
        }

        /// <summary>Every Frame Sync Lock stream processed, in table order; empty when none.</summary>
        public IReadOnlyList<LockSeries> AllSeries
        {
            get => _allSeries;
            private set => SetProperty(ref _allSeries, value);
        }

        public IAsyncRelayCommand OpenCommand { get; }

        private async Task OpenAsync()
        {
            string? path = await _picker!.PickRecordingAsync();
            if (path is not null) await LoadAsync(path);
        }

        /// <summary>Opens <paramref name="path"/>, asks what to process, processes it and shows the result or the reason it
        /// failed. Cancelling Configure Streams leaves the last result on screen. Ignored while busy.</summary>
        public async Task LoadAsync(string path)
        {
            if (IsBusy) return;
            string name = Path.GetFileName(path);
            IsBusy = true;
            Progress = 0;
            try
            {
                Status = $"Opening {name} ...";
                Ch10File file = await StreamsRun.OpenAsync(path);
                RunRequest? request = await _configurator.ConfigureAsync(path, file);
                if (request is null)
                {
                    Status = $"{name}: not processed (Configure Streams was cancelled).";
                    return;
                }
                Show(Array.Empty<LockSeries>());
                Status = $"Processing {name} ...";
                // Progress<T> made here posts each report back to this (the UI) thread.
                RunOutcome outcome = await StreamsRun.RunAsync(request, new Progress<int>(p => Progress = p));
                Progress = 100;
                Show(outcome.Streams.Where(s => s.Mode == StreamMode.FrameSyncLock).Select(LockSeries.Of).ToList());
                Status = Describe(name, outcome);
            }
            catch (RecordingException e)
            {
                Show(Array.Empty<LockSeries>());
                Status = e.Message;
            }
            catch (Exception e) when (e is FileNotFoundException or InvalidOperationException)
            {
                // The native core is missing or does not match this build.
                Show(Array.Empty<LockSeries>());
                Status = "The processing core could not be loaded: " + e.Message;
            }
            finally
            {
                IsBusy = false;
            }
        }

        private void Show(IReadOnlyList<LockSeries> series)
        {
            AllSeries = series;
            Series = series.Count > 0 ? series[0] : null;
        }

        /// <summary>The status line after a run: one Frame Sync Lock stream as the walking skeleton described it, else how
        /// many of each mode were processed; then any error the core reported for a stream that was not.</summary>
        public static string Describe(string name, RunOutcome outcome)
        {
            IReadOnlyList<ProcessedStream> streams = outcome.Streams;
            string text;
            if (streams.Count == 1 && streams[0].Mode == StreamMode.FrameSyncLock)
            {
                LockSeries s = LockSeries.Of(streams[0]);
                text = $"{name}: {s.Label} (channel {s.PcmChannelId}), {s.LockPercent.Length} samples, mean lock {s.MeanLock:F1} %";
            }
            else
            {
                int locks = streams.Count(s => s.Mode == StreamMode.FrameSyncLock);
                text = $"{name}: {streams.Count} {(streams.Count == 1 ? "stream" : "streams")} processed " +
                       $"({locks} Frame Sync Lock, {streams.Count - locks} Receiver SNR)";
            }
            return outcome.Errors.Count == 0 ? text : text + ". Not processed: " + string.Join(" ", outcome.Errors);
        }
    }
}
