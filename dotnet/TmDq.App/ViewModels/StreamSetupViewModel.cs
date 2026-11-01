using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Services;
using TmDq.App.Streams;
using TmDq.Core;

namespace TmDq.App.ViewModels
{
    /// <summary>
    /// One stream's setup dialog (US1.0, US2.0, US2.1, US2.2): the Frame Sync Lock dialog, or the Receiver SNR one, which
    /// adds the receiver settings. It edits a copy of the row (<see cref="StreamRules.ForEditing"/>); OK is available only
    /// when <see cref="StreamRules.Problems"/> finds nothing, and the problems are shown as they arise, so a value the Qt
    /// dialog's validators and spin boxes would have refused, or a file would have slipped past them, is named instead.
    /// </summary>
    public sealed partial class StreamSetupViewModel : ObservableObject
    {
        public static readonly IReadOnlyList<string> Periods = new[] { "1 s", "100 ms", "10 ms" };
        public static readonly IReadOnlyList<string> Polarities = new[] { "Positive", "Negative" };
        public static readonly IReadOnlyList<string> Slopes = new[] { "±10 V", "±5 V", "0–10 V", "0–5 V" };

        private readonly StreamSetup _start;
        private readonly ISettingsFilePicker? _files;
        private readonly string? _appRoot;
        private string? _lastFolder;
        private string _receiverParamsToml;

        private string _syncPattern;
        private string _syncMask;
        private decimal? _bitsInMinorFrame;
        private decimal? _dataRateMbps;
        private int _periodIndex;
        private bool _randomized;
        private bool _inverted;
        private int _polarityIndex;
        private int _slopeIndex;
        private decimal? _scaleDdBPerV;
        private decimal? _numReceivers;
        private decimal? _receiverChannels;
        private bool _applyToAll;
        private string _fileMessage = string.Empty;
        private IReadOnlyList<string> _problems = Array.Empty<string>();

        private readonly int _timeChannelId;
        private readonly bool _swapBytes;
        private readonly IStreamDialogs? _dialogs;
        private readonly ICalibrationFilePicker? _calibrationFiles;
        private IReadOnlyList<CalibrationProfile> _calibration;
        private string _calCh10Path, _stepTomlPath;
        private double _clipStartSec, _clipEndSec;

        /// <param name="edited">What the dialog opens with: <see cref="StreamRules.ForEditing"/> of the row.</param>
        /// <param name="lastFolder">The folder a settings file was last loaded from or saved to in this session.</param>
        /// <param name="timeChannelId">The time channel chosen in Configure Streams, which a calibration is extracted with;
        /// -1 when the recording has none.</param>
        /// <param name="swapBytes">The recording's byte order (Legacy Chapter 10 off), which a calibration is read with.</param>
        /// <param name="dialogs">Shows Extract Calibration and its report; null leaves Apply Cal unavailable.</param>
        /// <param name="lastCalFolder">The folder a calibration recording was last chosen from in this session.</param>
        public StreamSetupViewModel(StreamSetup edited, ISettingsFilePicker? files, string? appRoot, string? lastFolder,
                                    int timeChannelId = -1, bool swapBytes = true, IStreamDialogs? dialogs = null,
                                    ICalibrationFilePicker? calibrationFiles = null, string? lastCalFolder = null)
        {
            _start = edited;
            _files = files;
            _appRoot = appRoot;
            _lastFolder = lastFolder;
            _timeChannelId = timeChannelId;
            _swapBytes = swapBytes;
            _dialogs = dialogs;
            _calibrationFiles = calibrationFiles;
            LastCalFolder = lastCalFolder;
            _calibration = edited.Calibration;
            _calCh10Path = edited.CalCh10Path;
            _stepTomlPath = edited.StepTomlPath;
            _clipStartSec = edited.ClipStartSec;
            _clipEndSec = edited.ClipEndSec;
            ApplyCalCommand = new AsyncRelayCommand(ApplyCalAsync, () => CanApplyCal);
            _receiverParamsToml = edited.ReceiverParamsToml;
            _syncPattern = edited.SyncPattern;
            _syncMask = edited.SyncMask;
            _bitsInMinorFrame = edited.BitsInMinorFrame;
            _dataRateMbps = (decimal)edited.DataRateMbps;
            _periodIndex = (int)edited.SamplePeriod;
            _randomized = edited.Randomized;
            _inverted = edited.Inverted;
            _polarityIndex = edited.PolarityIndex;
            _slopeIndex = edited.SlopeIndex;
            _scaleDdBPerV = (decimal)edited.ScaleDdBPerV;
            _numReceivers = edited.NumReceivers;
            _receiverChannels = edited.ReceiverChannels;

            LoadFrameSyncCommand = new AsyncRelayCommand(LoadFrameSyncAsync, () => _files is not null);
            SaveFrameSyncCommand = new AsyncRelayCommand(SaveFrameSyncAsync, () => _files is not null);
            LoadReceiverParamsCommand = new AsyncRelayCommand(LoadReceiverParamsAsync, () => _files is not null);
            SaveReceiverParamsCommand = new AsyncRelayCommand(SaveReceiverParamsAsync, () => _files is not null);
            OkCommand = new RelayCommand(Ok, () => CanAccept);
            CancelCommand = new RelayCommand(() => CloseRequested?.Invoke(this, EventArgs.Empty));
            Recheck();
        }

        /// <summary>Raised when OK or Cancel closes the dialog; <see cref="Accepted"/> says which.</summary>
        public event EventHandler? CloseRequested;

        public bool IsReceiverSnr => _start.Mode == StreamMode.ReceiverChannelInfo;

        public string ModeName => IsReceiverSnr ? "Receiver SNR" : "Frame Sync Lock";

        public string Title => $"Configure {ModeName} — {_start.Label}";

        public string ApplyToAllText => $"Apply to all {ModeName} streams";

        public string ApplyToAllTip => $"Copy these settings to every other selected stream currently set to {ModeName} mode.";

        public string SyncPlaceholder => IsReceiverSnr ? "e.g. FE6B2840" : "e.g. A345CA5C";

        /// <summary>What a data rate of 0 means for this stream: the rate TMATS declares, or "TMATS" when it declares none.</summary>
        public string DataRateHint => _start.TmatsDataRateMbps > 0
            ? "0 = TMATS (" + _start.TmatsDataRateMbps.ToString("F3", CultureInfo.InvariantCulture) + " Mbps)"
            : "0 = TMATS";

        public string SyncPattern { get => _syncPattern; set => Set(ref _syncPattern, value); }

        public string SyncMask { get => _syncMask; set => Set(ref _syncMask, value); }

        public decimal? BitsInMinorFrame { get => _bitsInMinorFrame; set => Set(ref _bitsInMinorFrame, value); }

        public decimal? DataRateMbps { get => _dataRateMbps; set => Set(ref _dataRateMbps, value); }

        public int PeriodIndex { get => _periodIndex; set => Set(ref _periodIndex, value); }

        public bool Randomized { get => _randomized; set => Set(ref _randomized, value); }

        public bool Inverted { get => _inverted; set => Set(ref _inverted, value); }

        public int PolarityIndex { get => _polarityIndex; set => Set(ref _polarityIndex, value); }

        public int SlopeIndex { get => _slopeIndex; set => Set(ref _slopeIndex, value); }

        public decimal? ScaleDdBPerV { get => _scaleDdBPerV; set => Set(ref _scaleDdBPerV, value); }

        public decimal? NumReceivers { get => _numReceivers; set => Set(ref _numReceivers, value); }

        public decimal? ReceiverChannels { get => _receiverChannels; set => Set(ref _receiverChannels, value); }

        public bool ApplyToAll { get => _applyToAll; set => SetProperty(ref _applyToAll, value); }

        /// <summary>The receiver-parameters file the word map comes from, as a file name; empty for the shipped default.</summary>
        public string ReceiverParamsName => _receiverParamsToml.Length == 0 ? "(default word map)" : Path.GetFileName(_receiverParamsToml);

        /// <summary>What the last Load or Save did, or why it failed; empty before either.</summary>
        public string FileMessage { get => _fileMessage; private set => SetProperty(ref _fileMessage, value); }

        /// <summary>What stops OK, in the order <see cref="StreamRules.Problems"/> gives it.</summary>
        public IReadOnlyList<string> Problems { get => _problems; private set => SetProperty(ref _problems, value); }

        public string ProblemText => string.Join(Environment.NewLine, Problems);

        public bool CanAccept => Problems.Count == 0;

        public bool Accepted { get; private set; }

        /// <summary>The folder a settings file was last loaded from or saved to, for the next dialog.</summary>
        public string? LastFolder => _lastFolder;

        public IAsyncRelayCommand LoadFrameSyncCommand { get; }

        public IAsyncRelayCommand SaveFrameSyncCommand { get; }

        public IAsyncRelayCommand LoadReceiverParamsCommand { get; }

        public IAsyncRelayCommand SaveReceiverParamsCommand { get; }

        public IRelayCommand OkCommand { get; }

        public IRelayCommand CancelCommand { get; }

        // ---- non-linear step calibration (plan step 4.6, US5.3) ---------------------------------------------------------

        public const string ApplyCalTipText =
            "Build a non-linear calibration profile from a calibration Chapter 10 file and a step-config TOML. Uses the word " +
            "map, frame sync, and polarity above plus the time channel from the main dialog. Runtime-only; the profile " +
            "itself is not saved to disk.";

        /// <summary>Apply Cal opens Extract Calibration: for a Receiver SNR stream whose settings have no problem, in a
        /// recording with a time channel.</summary>
        public bool CanApplyCal => _dialogs is not null && IsReceiverSnr && _timeChannelId >= 0 && CanAccept;

        /// <summary>What Apply Cal does, or why it is not available.</summary>
        public string ApplyCalTip =>
            !IsReceiverSnr || _dialogs is null ? ApplyCalTipText
            : _timeChannelId < 0 ? "Select a Time Channel in the Configure Streams dialog before extracting calibration."
            : !CanAccept ? "Correct the settings above before extracting calibration."
            : ApplyCalTipText;

        public IAsyncRelayCommand ApplyCalCommand { get; }

        /// <summary>The profiles Apply Cal gave, carried to the row on OK.</summary>
        public IReadOnlyList<CalibrationProfile> Calibration => _calibration;

        public int CalibratedChannels => _calibration.Count(p => p.Valid);

        /// <summary>Beside Apply Cal, as the Qt dialog says it.</summary>
        public string CalibrationText => CalibratedChannels == 0
            ? "(none — using linear calibration)"
            : string.Create(CultureInfo.InvariantCulture, $"{CalibratedChannels} channel(s) calibrated (non-linear)");

        public bool IsCalibrated => CalibratedChannels > 0;

        /// <summary>The folder a calibration recording was last chosen from, for the next dialog.</summary>
        public string? LastCalFolder { get; private set; }

        /// <summary>Extract Calibration with the settings as they stand, the time channel and the byte order; on OK, its
        /// profiles and references are the dialog's, and its report is shown, as the Qt dialog shows it.</summary>
        private async Task ApplyCalAsync()
        {
            if (!CanApplyCal) return;
            StreamSetup s = ToSetup();
            var calibrationBase = new CalibrationBase(_timeChannelId, s.PcmChannelId, s.SyncPattern.Trim().ToUpperInvariant(),
                                                      s.SyncMask.Trim().ToUpperInvariant(), s.BitsInMinorFrame, s.Randomized,
                                                      s.Inverted, s.DataRateMbps, _swapBytes, s.ReceiverParamsToml,
                                                      s.NumReceivers, s.ReceiverChannels);
            var extract = new ExtractCalibrationViewModel(calibrationBase, _appRoot, _stepTomlPath, _calibrationFiles,
                                                          _lastFolder, LastCalFolder);
            await _dialogs!.ExtractCalibrationAsync(extract);
            _lastFolder = extract.StepFolder ?? _lastFolder;
            LastCalFolder = extract.CalFolder ?? LastCalFolder;
            if (!extract.Accepted) return;

            _calibration = extract.Profiles;
            _calCh10Path = extract.CalPath;
            _stepTomlPath = extract.StepPath;
            _clipStartSec = extract.ClipStartSec;
            _clipEndSec = extract.ClipEndSec;
            OnPropertyChanged(nameof(Calibration));
            OnPropertyChanged(nameof(CalibratedChannels));
            OnPropertyChanged(nameof(CalibrationText));
            OnPropertyChanged(nameof(IsCalibrated));
            await _dialogs.ShowReportAsync("Calibration Extracted", extract.Report.Length > 0
                ? extract.Report
                : string.Create(CultureInfo.InvariantCulture, $"Applied non-linear calibration to {_calibration.Count} channel(s)."));
        }

        /// <summary>The row as the dialog holds it. A field left empty (a spin box cleared) keeps the value it opened with;
        /// <see cref="Problems"/> has already said so.</summary>
        public StreamSetup ToSetup() => _start with
        {
            SyncPattern = SyncPattern ?? string.Empty,
            SyncMask = SyncMask ?? string.Empty,
            BitsInMinorFrame = BitsInMinorFrame is decimal b ? (int)b : _start.BitsInMinorFrame,
            DataRateMbps = DataRateMbps is decimal r ? (double)r : _start.DataRateMbps,
            SamplePeriod = (SamplePeriod)PeriodIndex,
            Randomized = Randomized,
            Inverted = Inverted,
            PolarityIndex = PolarityIndex,
            SlopeIndex = SlopeIndex,
            ScaleDdBPerV = ScaleDdBPerV is decimal s ? (double)s : _start.ScaleDdBPerV,
            NumReceivers = NumReceivers is decimal n ? (int)n : _start.NumReceivers,
            ReceiverChannels = ReceiverChannels is decimal c ? (int)c : _start.ReceiverChannels,
            ReceiverParamsToml = _receiverParamsToml,
            Calibration = _calibration,
            CalCh10Path = _calCh10Path,
            StepTomlPath = _stepTomlPath,
            ClipStartSec = _clipStartSec,
            ClipEndSec = _clipEndSec,
        };

        private void Set<T>(ref T field, T value, [System.Runtime.CompilerServices.CallerMemberName] string? name = null)
        {
            if (SetProperty(ref field, value, name)) Recheck();
        }

        private void Recheck()
        {
            var problems = new List<string>();
            if (BitsInMinorFrame is null) problems.Add("Bits Per Frame is empty.");
            if (DataRateMbps is null) problems.Add("Data Rate is empty.");
            if (IsReceiverSnr)
            {
                if (ScaleDdBPerV is null) problems.Add("Scale (dB/V) is empty.");
                if (NumReceivers is null) problems.Add("Num Rcvrs is empty.");
                if (ReceiverChannels is null) problems.Add("Num Channels is empty.");
            }
            problems.AddRange(StreamRules.Problems(ToSetup()));
            Problems = problems;
            OnPropertyChanged(nameof(ProblemText));
            OnPropertyChanged(nameof(CanAccept));
            OkCommand?.NotifyCanExecuteChanged();
            OnPropertyChanged(nameof(CanApplyCal));
            OnPropertyChanged(nameof(ApplyCalTip));
            ApplyCalCommand?.NotifyCanExecuteChanged();
        }

        private void Ok()
        {
            if (!CanAccept) return;
            Accepted = true;
            CloseRequested?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>Puts a row's fields back on the dialog (after a Load).</summary>
        private void Show(StreamSetup s)
        {
            SyncPattern = s.SyncPattern;
            SyncMask = s.SyncMask;
            BitsInMinorFrame = s.BitsInMinorFrame;
            Randomized = s.Randomized;
            Inverted = s.Inverted;
            PolarityIndex = s.PolarityIndex;
            SlopeIndex = s.SlopeIndex;
            ScaleDdBPerV = (decimal)s.ScaleDdBPerV;
            NumReceivers = s.NumReceivers;
            ReceiverChannels = s.ReceiverChannels;
            _receiverParamsToml = s.ReceiverParamsToml;
            OnPropertyChanged(nameof(ReceiverParamsName));
            Recheck();
        }

        private async Task LoadFrameSyncAsync()
        {
            string? path = await _files!.OpenAsync("Load Frame Sync Parameters",
                                                   StreamFiles.LoadFolder(_appRoot, StreamFiles.FrameSyncFolder, _lastFolder));
            if (path is null) return;
            _lastFolder = Path.GetDirectoryName(path);
            Show(StreamFiles.LoadFrameSync(ToSetup(), path));
            FileMessage = "Loaded " + Path.GetFileName(path) + ".";
        }

        private async Task SaveFrameSyncAsync()
        {
            string? path = await _files!.SaveAsync("Save Frame Sync Parameters", _lastFolder);
            if (path is null) return;
            try
            {
                string written = StreamFiles.SaveFrameSync(ToSetup(), path);
                _lastFolder = Path.GetDirectoryName(written);
                FileMessage = "Saved " + Path.GetFileName(written) + ".";
            }
            catch (TmdqException e)
            {
                FileMessage = "Could not save: " + e.Message;
            }
        }

        private async Task LoadReceiverParamsAsync()
        {
            string? path = await _files!.OpenAsync("Load Receiver Parameters",
                                                   StreamFiles.LoadFolder(_appRoot, StreamFiles.ReceiverParamsFolder, _lastFolder));
            if (path is null) return;
            _lastFolder = Path.GetDirectoryName(path);
            Show(StreamFiles.LoadReceiverParams(ToSetup(), path));
            FileMessage = "Loaded " + Path.GetFileName(path) + ".";
        }

        private async Task SaveReceiverParamsAsync()
        {
            string? path = await _files!.SaveAsync("Save Receiver Parameters", _lastFolder);
            if (path is null) return;
            try
            {
                string written = StreamFiles.SaveReceiverParams(ToSetup(), path);
                _lastFolder = Path.GetDirectoryName(written);
                FileMessage = "Saved " + Path.GetFileName(written) + ".";
            }
            catch (TmdqException e)
            {
                FileMessage = "Could not save: " + e.Message;
            }
        }
    }
}
