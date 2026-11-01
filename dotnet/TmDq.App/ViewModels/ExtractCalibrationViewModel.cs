using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Services;
using TmDq.App.Streams;
using TmDq.Core;

namespace TmDq.App.ViewModels
{
    /// <summary>What a calibration is extracted with: the Receiver SNR dialog's settings as they stand, the time channel
    /// chosen in Configure Streams and the recording's byte order, so the extraction reads the calibration recording
    /// exactly as the main run will read the data it calibrates.</summary>
    public sealed record CalibrationBase(
        int TimeChannelId,
        int PcmChannelId,
        string SyncPattern,
        string SyncMask,
        int BitsInMinorFrame,
        bool Randomized,
        bool Inverted,
        double DataRateMbps,
        bool SwapBytes,
        string ReceiverParamsToml,
        int NumReceivers,
        int ReceiverChannels);

    /// <summary>How a line of the dialog reads: as the Qt dialog marks and colours it.</summary>
    public enum CalibrationStatusKind
    {
        Pending,
        Ok,
        Warn,
        Fail,
    }

    /// <summary>
    /// The Extract Calibration dialog's state (plan step 4.6, US5.3), the Qt <c>CalibrationSetupDialog</c>: a step file
    /// (the stream's last, else the shipped default, loaded as the dialog opens), a calibration recording, and a clip at
    /// each end. Once both files are in hand, and again after any change to them or to a clip, the calibration is
    /// extracted, with the progress dialog up and Cancel stopping it. OK is available once the step file parsed and at
    /// least one channel calibrated; it gives the profiles, the two files, the clips and the report.
    /// </summary>
    public sealed partial class ExtractCalibrationViewModel : ObservableObject
    {
        public const string Title = "Extract Calibration";
        public const string ProcessingText = "Processing calibration file…";
        public const decimal ClipMaximum = 100000m;

        private readonly CalibrationBase _base;
        private readonly string? _appRoot;
        private readonly ICalibrationFilePicker? _files;
        private IReadOnlyList<double> _steps = Array.Empty<double>();
        private bool _stepOk, _calOk;
        private string _stepPath = string.Empty, _calPath = string.Empty;
        private string _stepStatus = string.Empty, _calStatus = string.Empty;
        private CalibrationStatusKind _stepKind, _calKind;
        private decimal? _clipStart = 0m, _clipEnd = 0m;

        /// <param name="initialStepPath">The step file the stream last used; it is loaded if it exists, else the shipped
        /// default.</param>
        /// <param name="stepFolder">Where the step file's Browse starts once one was chosen this session.</param>
        /// <param name="calFolder">Where the calibration recording's Browse starts once one was chosen this session.</param>
        public ExtractCalibrationViewModel(CalibrationBase calibrationBase, string? appRoot, string? initialStepPath,
                                           ICalibrationFilePicker? files = null, string? stepFolder = null, string? calFolder = null)
        {
            _base = calibrationBase;
            _appRoot = appRoot;
            _files = files;
            StepFolder = stepFolder;
            CalFolder = calFolder;
            BrowseStepCommand = new AsyncRelayCommand(BrowseStepAsync, () => _files is not null);
            BrowseCalCommand = new AsyncRelayCommand(BrowseCalAsync, () => _files is not null);
            OkCommand = new RelayCommand(Ok, () => CanAccept);
            CancelCommand = new RelayCommand(() => CloseRequested?.Invoke(this, EventArgs.Empty));
            SetStep(CalibrationStatusKind.Pending, "No file selected.");
            SetCal(CalibrationStatusKind.Pending, "No file selected.");

            // Almost every calibration uses the same step file, so the dialog starts with one loaded, and says it was
            // loaded automatically: a step file that does not match the recording still calibrates, just wrongly.
            string initial = StepFile.InitialPath(appRoot, initialStepPath);
            if (initial.Length > 0) LoadStepFile(initial, automatic: true);
        }

        /// <summary>Raised when OK or Cancel closes the dialog; <see cref="Accepted"/> says which.</summary>
        public event EventHandler? CloseRequested;

        /// <summary>The progress dialog an extraction shows; the window sets it, so it sits over the dialog.</summary>
        public IProcessingDialog? ProgressDialog { get; set; }

        public CalibrationBase Base => _base;

        public string StepPath => _stepPath;

        public string StepName => Path.GetFileName(_stepPath);

        public string CalPath => _calPath;

        public string CalName => Path.GetFileName(_calPath);

        public string StepStatus => _stepStatus;

        public string CalStatus => _calStatus;

        public CalibrationStatusKind StepKind => _stepKind;

        public CalibrationStatusKind CalKind => _calKind;

        public IBrush StepBrush => BrushOf(_stepKind);

        public IBrush CalBrush => BrushOf(_calKind);

        /// <summary>Seconds left off the start of the calibration recording before steps are detected (0 to 100,000, one
        /// decimal); a change extracts again.</summary>
        public decimal? ClipStart
        {
            get => _clipStart;
            set
            {
                if (SetProperty(ref _clipStart, value)) _ = ExtractAsync();
            }
        }

        /// <summary>Seconds left off the end, likewise.</summary>
        public decimal? ClipEnd
        {
            get => _clipEnd;
            set
            {
                if (SetProperty(ref _clipEnd, value)) _ = ExtractAsync();
            }
        }

        public double ClipStartSec => (double)(_clipStart ?? 0m);

        public double ClipEndSec => (double)(_clipEnd ?? 0m);

        /// <summary>The step levels parsed from the step file, in dB.</summary>
        public IReadOnlyList<double> Steps => _steps;

        /// <summary>The usable profiles of the last extraction; empty until one calibrated a channel.</summary>
        public IReadOnlyList<CalibrationProfile> Profiles { get; private set; } = Array.Empty<CalibrationProfile>();

        /// <summary>The last extraction's report (the core's summary of every receiver), with the clip warning first when
        /// the clips were ignored.</summary>
        public string Report { get; private set; } = string.Empty;

        public bool CanAccept => _stepOk && _calOk;

        public bool Accepted { get; private set; }

        /// <summary>The folders the two Browse buttons last used, for the next dialog.</summary>
        public string? StepFolder { get; private set; }

        public string? CalFolder { get; private set; }

        /// <summary>The last extraction started (a change of clip starts one without waiting for it); null before any.</summary>
        public Task? Extraction { get; private set; }

        public IAsyncRelayCommand BrowseStepCommand { get; }

        public IAsyncRelayCommand BrowseCalCommand { get; }

        public IRelayCommand OkCommand { get; }

        public IRelayCommand CancelCommand { get; }

        /// <summary>Loads <paramref name="path"/> as the step file and extracts again (what Browse does once a file is chosen).</summary>
        public Task LoadStepFileAsync(string path)
        {
            StepFolder = Path.GetDirectoryName(path);
            LoadStepFile(path, automatic: false);
            return Extraction ?? Task.CompletedTask;
        }

        /// <summary>Takes <paramref name="path"/> as the calibration recording and extracts (what Browse does once a file is
        /// chosen). Refused, with the reason in its status, while the stream has no frame sync pattern.</summary>
        public Task LoadCalibrationAsync(string path)
        {
            if (_base.SyncPattern.Trim().Length == 0)
            {
                SetCal(CalibrationStatusKind.Fail, "Set a frame sync pattern before loading the calibration file.");
                return Task.CompletedTask;
            }
            CalFolder = Path.GetDirectoryName(path);
            _calPath = path;
            OnPropertyChanged(nameof(CalPath));
            OnPropertyChanged(nameof(CalName));
            return ExtractAsync();
        }

        /// <summary>What the extraction is asked for: the dialog's base, its step file and its clips.</summary>
        public CalibrationRequest ToRequest() => new()
        {
            FileName = _calPath,
            TimeChannelId = _base.TimeChannelId,
            PcmChannelId = _base.PcmChannelId,
            SyncPattern = _base.SyncPattern,
            SyncMask = _base.SyncMask.Length == 0 ? null : _base.SyncMask,
            BitsInMinorFrame = _base.BitsInMinorFrame,
            Randomized = _base.Randomized,
            Inverted = _base.Inverted,
            DataRateMbps = _base.DataRateMbps,
            SwapBytes = _base.SwapBytes,
            ReceiverParamsToml = _base.ReceiverParamsToml.Length == 0 ? null : _base.ReceiverParamsToml,
            AppRoot = _appRoot,
            NumReceivers = _base.NumReceivers,
            ReceiverChannels = _base.ReceiverChannels,
            Steps = _steps,
            ClipStartSec = ClipStartSec,
            ClipEndSec = ClipEndSec,
        };

        private void LoadStepFile(string path, bool automatic)
        {
            _stepPath = path;
            OnPropertyChanged(nameof(StepPath));
            OnPropertyChanged(nameof(StepName));
            try
            {
                _steps = StepFile.Parse(path);
                _stepOk = true;
                SetStep(CalibrationStatusKind.Ok, string.Create(CultureInfo.InvariantCulture,
                    $"{_steps.Count} steps parsed{(automatic ? " (loaded automatically)" : "")}."));
            }
            catch (TmdqException e)
            {
                _steps = Array.Empty<double>();
                _stepOk = false;
                SetStep(CalibrationStatusKind.Fail, e.Message);
            }
            OnPropertyChanged(nameof(Steps));
            UpdateOk();
            _ = ExtractAsync();
        }

        /// <summary>
        /// Extracts with what the dialog holds, as the Qt <c>maybeRunExtraction</c> does: nothing until a calibration
        /// recording is chosen; a word to choose a valid step file first; otherwise the extraction, its outcome in the
        /// recording's status line and OK available once a channel calibrated.
        /// </summary>
        private Task ExtractAsync()
        {
            if (_calPath.Length == 0) return Task.CompletedTask;
            if (!_stepOk)
            {
                SetCalibrated(false, Array.Empty<CalibrationProfile>(), string.Empty);
                SetCal(CalibrationStatusKind.Pending, "Select a valid step cal file to process this recording.");
                return Task.CompletedTask;
            }
            Task run = RunAsync();
            Extraction = run;
            return run;
        }

        private async Task RunAsync()
        {
            SetCalibrated(false, Array.Empty<CalibrationProfile>(), string.Empty);
            SetCal(CalibrationStatusKind.Pending, ProcessingText);

            var progress = new ProcessingViewModel(ProcessingText);
            var observer = new Observer(progress);
            CalibrationRun run;
            try
            {
                run = CalibrationRun.Start(ToRequest(), observer);
            }
            catch (TmdqException e)
            {
                // A setting that cannot be read is shown here, before anything starts.
                SetCal(CalibrationStatusKind.Fail, e.Message);
                return;
            }

            CalibrationResult result;
            using (run)
            {
                if (run.IsRunning) ProgressDialog?.Show(progress);
                try
                {
                    using (progress.Token.Register(run.Cancel))
                        await Task.Run(run.Wait);
                }
                finally
                {
                    ProgressDialog?.Close();
                }
                result = run.GetResult();
            }

            if (!observer.Success)
            {
                SetCal(CalibrationStatusKind.Fail, result.Summary);
                return;
            }
            IReadOnlyList<CalibrationProfile> profiles = result.Profiles();
            string report = result.Report;
            if (result.ClipIgnored)
            {
                // The clips are dropped when they would leave nothing to measure; without this the operator would see a
                // result that ignored the values they typed.
                string warning = string.Create(CultureInfo.InvariantCulture,
                    $"Clip Start + Clip End ({ClipStartSec + ClipEndSec:F1} s) exceed the recording ({result.RecordingSeconds:F1} s) - clips ignored.");
                SetCalibrated(profiles.Count > 0, profiles, warning + "\n\n" + report);
                SetCal(CalibrationStatusKind.Warn, warning + " " + result.Summary);
            }
            else
            {
                SetCalibrated(profiles.Count > 0, profiles, report);
                SetCal(profiles.Count > 0 ? CalibrationStatusKind.Ok : CalibrationStatusKind.Fail, result.Summary);
            }
        }

        private void SetCalibrated(bool ok, IReadOnlyList<CalibrationProfile> profiles, string report)
        {
            _calOk = ok;
            Profiles = profiles;
            Report = report;
            OnPropertyChanged(nameof(Profiles));
            OnPropertyChanged(nameof(Report));
            UpdateOk();
        }

        private void UpdateOk()
        {
            OnPropertyChanged(nameof(CanAccept));
            OkCommand?.NotifyCanExecuteChanged();
        }

        private void SetStep(CalibrationStatusKind kind, string text)
        {
            _stepKind = kind;
            _stepStatus = Marked(kind, text);
            OnPropertyChanged(nameof(StepKind));
            OnPropertyChanged(nameof(StepStatus));
            OnPropertyChanged(nameof(StepBrush));
        }

        private void SetCal(CalibrationStatusKind kind, string text)
        {
            _calKind = kind;
            _calStatus = Marked(kind, text);
            OnPropertyChanged(nameof(CalKind));
            OnPropertyChanged(nameof(CalStatus));
            OnPropertyChanged(nameof(CalBrush));
        }

        /// <summary>A status line as the Qt dialog writes it: a tick, a warning sign or a cross before the text.</summary>
        public static string Marked(CalibrationStatusKind kind, string text) => kind switch
        {
            CalibrationStatusKind.Ok => "✓ " + text,
            CalibrationStatusKind.Warn => "⚠ " + text,
            CalibrationStatusKind.Fail => "✗ " + text,
            _ => text,
        };

        /// <summary>The Qt dialog's colours: green, the log's goldenrod, red, and gray while pending.</summary>
        public static IBrush BrushOf(CalibrationStatusKind kind) => kind switch
        {
            CalibrationStatusKind.Ok => Brushes.Green,
            CalibrationStatusKind.Warn => new SolidColorBrush(Color.FromRgb(0xDA, 0xA5, 0x20)),
            CalibrationStatusKind.Fail => new SolidColorBrush(Color.FromRgb(0xCC, 0x00, 0x00)),
            _ => Brushes.Gray,
        };

        private async Task BrowseStepAsync()
        {
            string? path = await _files!.OpenStepFileAsync(StreamFiles.LoadFolder(_appRoot, StreamFiles.StepCalFolder, StepFolder));
            if (path is not null) await LoadStepFileAsync(path);
        }

        private async Task BrowseCalAsync()
        {
            if (_base.SyncPattern.Trim().Length == 0)
            {
                SetCal(CalibrationStatusKind.Fail, "Set a frame sync pattern before loading the calibration file.");
                return;
            }
            string? path = await _files!.OpenCalibrationRecordingAsync(CalFolder ?? StepFolder);
            if (path is not null) await LoadCalibrationAsync(path);
        }

        private void Ok()
        {
            if (!CanAccept) return;
            Accepted = true;
            CloseRequested?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>The extraction's reports, the progress on the dialog's thread.</summary>
        private sealed class Observer : IRunObserver
        {
            private readonly ProcessingViewModel _progress;
            private readonly SynchronizationContext? _ui = SynchronizationContext.Current;

            public Observer(ProcessingViewModel progress) => _progress = progress;

            public bool Success { get; private set; }

            public void OnProgress(int percent)
            {
                if (_ui is null) _progress.Progress = percent;
                else _ui.Post(_ => _progress.Progress = percent, null);
            }

            public void OnFinished(bool success) => Success = success;
        }
    }
}
