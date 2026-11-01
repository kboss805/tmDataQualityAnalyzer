using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Logging;
using TmDq.App.Plot;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.Core;

namespace TmDq.App.ViewModels
{
    /// <summary>The main window's state: open a recording, ask what to process (Configure Streams, plan step 4.1; with no
    /// one to ask, the first PCM channel with the Frame Sync Lock defaults, as the walking skeleton did), process it and
    /// plot it (plan step 4.3b: <see cref="Plot"/>), with the progress dialog up for the run and every step in the log
    /// (plan step 4.2).</summary>
    public sealed partial class MainWindowViewModel : ObservableObject
    {
        private readonly IRecordingPicker? _picker;
        private readonly IStreamConfigurator _configurator;
        private readonly IProcessingDialog? _processingDialog;
        private ProcessingViewModel? _processing;
        private string _status = "Open a Chapter 10 recording.";
        private int _progress;
        private bool _isBusy;
        private LockSeries? _series;
        private IReadOnlyList<LockSeries> _allSeries = Array.Empty<LockSeries>();

        /// <param name="configurator">What a recording is processed with; null means the first PCM channel with the
        /// Frame Sync Lock defaults (<see cref="FirstChannelRun"/>).</param>
        /// <param name="processingDialog">The progress dialog shown while a run goes on; null shows none.</param>
        /// <param name="clock">The log's clock; null is the system's.</param>
        public MainWindowViewModel(IRecordingPicker? picker = null, IStreamConfigurator? configurator = null,
                                   IProcessingDialog? processingDialog = null, TimeProvider? clock = null)
        {
            _picker = picker;
            _configurator = configurator ?? new FirstChannelRun();
            _processingDialog = processingDialog;
            Log = new SessionLog(clock);
            OpenCommand = new AsyncRelayCommand(OpenAsync, () => !IsBusy && _picker is not null);
            Log.Add("tmDataQualityAnalyzer v" + AppInfo.Version);
            Log.Add("Load a Chapter 10 file to configure per-stream processing.");
            // A time window entered outside the recording is clamped, and says so in the log, in goldenrod.
            Plot.Warning += (_, message) => Log.Warning(message);
        }

        public string Title => AppInfo.Title;

        /// <summary>The log sidebar's contents.</summary>
        public SessionLog Log { get; }

        /// <summary>What the chart draws: every stream processed, on one time axis.</summary>
        public PlotViewModel Plot { get; } = new();

        /// <summary>The progress dialog's state while a recording is processed; null otherwise.</summary>
        public ProcessingViewModel? Processing
        {
            get => _processing;
            private set => SetProperty(ref _processing, value);
        }

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
        /// failed, logging each step as the Qt application does. The progress dialog is up for the run, and its Cancel
        /// stops every stream. Cancelling Configure Streams leaves the last result on screen. Ignored while busy.</summary>
        public async Task LoadAsync(string path)
        {
            if (IsBusy) return;
            string name = Path.GetFileName(path);
            IsBusy = true;
            Progress = 0;
            try
            {
                Status = $"Opening {name} ...";
                Log.Add("Opening: " + name);
                Ch10File file = await StreamsRun.OpenAsync(path);
                Log.Add("  File size: " + SizeText(new FileInfo(path).Length));
                Log.Add("  Time channels: " + CountText(file.TimeChannels.Count));
                Log.Add("  PCM channels: " + CountText(file.PcmChannels.Count));
                RunRequest? request = await _configurator.ConfigureAsync(path, file);
                if (request is null)
                {
                    Status = $"{name}: not processed (Configure Streams was cancelled).";
                    return;
                }
                Show(Array.Empty<LockSeries>());
                Status = $"Processing {name} ...";
                using RunOutcome outcome = await RunWithDialogAsync(request);
                Progress = 100;
                Show(outcome.Streams.Where(s => s.Mode == StreamMode.FrameSyncLock).Select(LockSeries.Of).ToList());
                // The core builds each stream's series, in table order, on one shared time axis.
                foreach (StreamResult result in outcome.Results) Plot.AddStream(result);
                Plot.Title = BaseName(name);
                Status = Describe(name, outcome);
                Log.Success("Processing complete — results plotted from memory.");
            }
            catch (OperationCanceledException)
            {
                Show(Array.Empty<LockSeries>());
                Status = $"{name}: processing was cancelled.";
            }
            catch (RecordingException e)
            {
                Show(Array.Empty<LockSeries>());
                Status = e.Message;
                // A run with nothing processed has logged the core's own reasons already.
                if (e is not NothingProcessedException) Log.Error(e.Message);
            }
            catch (Exception e) when (e is FileNotFoundException or InvalidOperationException)
            {
                // The native core is missing or does not match this build.
                Show(Array.Empty<LockSeries>());
                Status = "The processing core could not be loaded: " + e.Message;
                Log.Error(Status);
            }
            finally
            {
                IsBusy = false;
            }
        }

        /// <summary>Runs <paramref name="request"/> with the progress dialog up until it has stopped.</summary>
        private async Task<RunOutcome> RunWithDialogAsync(RunRequest request)
        {
            var processing = new ProcessingViewModel();
            Processing = processing;
            _processingDialog?.Show(processing);
            try
            {
                // Made here, both post each report back to this (the UI) thread, in order.
                var progress = new Progress<int>(p => Progress = processing.Progress = p);
                return await StreamsRun.RunAsync(request, progress, new UiRunLog(Log), processing.Token);
            }
            finally
            {
                _processingDialog?.Close();
                Processing = null;
            }
        }

        /// <summary>A file size as the Qt log gives it: megabytes from 1 MiB, kilobytes below, one decimal.</summary>
        public static string SizeText(long bytes) => bytes >= 1048576
            ? (bytes / 1048576.0).ToString("F1", CultureInfo.InvariantCulture) + " MB"
            : (bytes / 1024.0).ToString("F1", CultureInfo.InvariantCulture) + " KB";

        /// <summary>A channel count as the Qt log gives it: "none" for zero.</summary>
        public static string CountText(int count) => count == 0 ? "none" : count.ToString(CultureInfo.InvariantCulture);

        /// <summary>Passes a run's log lines, which come on the core's threads, to the session log on the thread that made
        /// it, in order.</summary>
        private sealed class UiRunLog : IRunLog
        {
            private readonly SessionLog _log;
            private readonly SynchronizationContext? _ui = SynchronizationContext.Current;

            public UiRunLog(SessionLog log) => _log = log;

            public void Line(string message) => OnUi(() => _log.Add(message));

            public void Error(string message) => OnUi(() => _log.Error(message));

            private void OnUi(Action add)
            {
                if (_ui is null) lock (_log) add();
                else _ui.Post(_ => add(), null);
            }
        }

        /// <summary>A file name as the plot's title takes it, the Qt <c>QFileInfo::baseName</c>: up to its first dot.</summary>
        public static string BaseName(string fileName)
        {
            int dot = fileName.IndexOf('.');
            return dot < 0 ? fileName : fileName[..dot];
        }

        private void Show(IReadOnlyList<LockSeries> series)
        {
            // An empty run summary is a new run, or one that failed: the plot is cleared with it.
            if (series.Count == 0) Plot.Clear();
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
