using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
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
    /// The Configure Streams dialog (US1.0): a row per PCM channel, the time channel and Legacy Chapter 10 for the whole
    /// recording, and Process. Every rule is <see cref="StreamRules"/>'s; this holds the rows and opens a row's setup
    /// dialog through <see cref="IStreamDialogs"/>.
    /// </summary>
    public sealed partial class ConfigureStreamsViewModel : ObservableObject
    {
        private readonly IStreamDialogs? _dialogs;
        private readonly ISettingsFilePicker? _files;
        private readonly string? _appRoot;
        private readonly IReadOnlyList<ChannelInfo> _timeChannels;
        private int _timeChannelIndex;
        private bool _legacyChapter10;
        private bool _all;
        private string? _lastFolder, _lastCalFolder;
        private readonly ICalibrationFilePicker? _calibrationFiles;

        /// <param name="calibrationFiles">Extract Calibration's file dialogs (plan step 4.6).</param>
        public ConfigureStreamsViewModel(string recording, Ch10File file, IStreamDialogs? dialogs, ISettingsFilePicker? files, string? appRoot,
                                         ICalibrationFilePicker? calibrationFiles = null)
        {
            Recording = recording;
            _dialogs = dialogs;
            _files = files;
            _appRoot = appRoot;
            _calibrationFiles = calibrationFiles;
            _timeChannels = file.TimeChannels;
            TimeChannelItems = _timeChannels.Select(StreamRules.TimeChannelText).ToList();
            _timeChannelIndex = Math.Max(0, _timeChannels.ToList().FindIndex(c => c.Id == file.DefaultTimeChannel));
            Rows = new ObservableCollection<StreamRowViewModel>(file.PcmChannels.Select(c => new StreamRowViewModel(this, StreamDefaults.NewStream(c))));
            ProcessCommand = new RelayCommand(Process, () => CanProcess);
            CancelCommand = new RelayCommand(() => CloseRequested?.Invoke(this, EventArgs.Empty));
        }

        /// <summary>Raised when Process or Cancel closes the dialog; <see cref="Accepted"/> says which.</summary>
        public event EventHandler? CloseRequested;

        public string Recording { get; }

        public string Title => "Configure Streams";

        public ObservableCollection<StreamRowViewModel> Rows { get; }

        /// <summary>"&lt;id&gt; - &lt;name&gt;" for each time channel, as the Qt dialog lists them.</summary>
        public IReadOnlyList<string> TimeChannelItems { get; }

        public bool HasTimeChannels => _timeChannels.Count > 0;

        public int TimeChannelIndex { get => _timeChannelIndex; set => SetProperty(ref _timeChannelIndex, value); }

        /// <summary>The selected time channel's id, or -1 when the recording has none.</summary>
        public int TimeChannelId => HasTimeChannels && TimeChannelIndex >= 0 && TimeChannelIndex < _timeChannels.Count
            ? _timeChannels[TimeChannelIndex].Id
            : -1;

        /// <summary>The recording's words are not byte-swapped (older equipment). File-wide, like the time channel.</summary>
        public bool LegacyChapter10 { get => _legacyChapter10; set => SetProperty(ref _legacyChapter10, value); }

        /// <summary>The "All" box: setting it sets every row's Process. Rows changed one at a time do not change it, as in
        /// the Qt dialog.</summary>
        public bool All
        {
            get => _all;
            set
            {
                if (!SetProperty(ref _all, value)) return;
                IReadOnlyList<StreamSetup> set = StreamRules.SetAll(Setups, value);
                for (int i = 0; i < Rows.Count; i++) Rows[i].Setup = set[i];
            }
        }

        public bool CanProcess => StreamRules.CanProcess(Setups);

        /// <summary>Why Process is not available, as the dialog says it; empty when it is.</summary>
        public string WhyNotProcess => StreamRules.WhyNotProcess(Setups) ?? string.Empty;

        public bool Accepted { get; private set; }

        public IRelayCommand ProcessCommand { get; }

        public IRelayCommand CancelCommand { get; }

        public IReadOnlyList<StreamSetup> Setups => Rows.Select(r => r.Setup).ToList();

        /// <summary>The run Process starts.</summary>
        public RunRequest ToRunRequest() => StreamRules.ToRunRequest(Recording, TimeChannelId, Setups, LegacyChapter10, _appRoot);

        /// <summary>Opens <paramref name="row"/>'s setup dialog in its current mode, and keeps what OK gives it (and, with
        /// Apply to all, copies it to every other checked row in the same mode).</summary>
        public async Task ConfigureAsync(StreamRowViewModel row)
        {
            int index = Rows.IndexOf(row);
            if (index < 0 || !row.Setup.Process || _dialogs is null) return;
            // A calibration is extracted with the time channel selected here and the recording's byte order, as the run
            // will read the data it calibrates (the Qt dialog used the recording's first time channel whatever was chosen).
            var setup = new StreamSetupViewModel(StreamRules.ForEditing(row.Setup, _appRoot), _files, _appRoot, _lastFolder,
                                                 TimeChannelId, !LegacyChapter10, _dialogs, _calibrationFiles, _lastCalFolder);
            await _dialogs.EditStreamAsync(setup);
            _lastCalFolder = setup.LastCalFolder;
            if (!setup.Accepted) return;
            _lastFolder = setup.LastFolder;
            row.Setup = StreamRules.Accept(row.Setup, setup.ToSetup());
            if (setup.ApplyToAll)
            {
                IReadOnlyList<StreamSetup> applied = StreamRules.ApplyToAll(Setups, index);
                for (int i = 0; i < Rows.Count; i++) Rows[i].Setup = applied[i];
            }
        }

        internal void RowChanged()
        {
            OnPropertyChanged(nameof(CanProcess));
            OnPropertyChanged(nameof(WhyNotProcess));
            ProcessCommand.NotifyCanExecuteChanged();
        }

        private void Process()
        {
            if (!CanProcess) return;
            Accepted = true;
            CloseRequested?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>One row of Configure Streams.</summary>
    public sealed partial class StreamRowViewModel : ObservableObject
    {
        public static readonly IReadOnlyList<string> Modes = new[] { "Receiver SNR", "Frame Sync Lock" };

        private readonly ConfigureStreamsViewModel _owner;
        private StreamSetup _setup;

        public StreamRowViewModel(ConfigureStreamsViewModel owner, StreamSetup setup)
        {
            _owner = owner;
            _setup = setup;
            ConfigureCommand = new AsyncRelayCommand(() => _owner.ConfigureAsync(this), () => CanConfigure);
        }

        public StreamSetup Setup
        {
            get => _setup;
            set
            {
                if (ReferenceEquals(_setup, value)) return;
                _setup = value;
                OnPropertyChanged(string.Empty);   // every derived property
                ConfigureCommand.NotifyCanExecuteChanged();
                _owner.RowChanged();
            }
        }

        public string Label => Setup.Label;

        public bool Process { get => Setup.Process; set => Setup = Setup with { Process = value }; }

        /// <summary>The Mode list's index, in the Qt dialog's order: 0 Receiver SNR, 1 Frame Sync Lock.</summary>
        public int ModeIndex
        {
            get => Setup.Mode == StreamMode.FrameSyncLock ? 1 : 0;
            set => Setup = StreamRules.WithMode(Setup, value == 1 ? StreamMode.FrameSyncLock : StreamMode.ReceiverChannelInfo);
        }

        /// <summary>The gear is available only on a checked row, as in the Qt dialog.</summary>
        public bool CanConfigure => Setup.Process;

        public Readiness Readiness => StreamRules.ReadinessOf(Setup);

        public bool IsReady => Readiness == Readiness.Ready;

        /// <summary>A checked row that cannot be processed yet (a red cross; an unchecked row's cross is gray).</summary>
        public bool IsBlocking => Readiness is Readiness.NeedsConfiguration or Readiness.MissingPattern;

        public string ReadyGlyph => IsReady ? "✓" : "✗";

        public string ReadyTip => StreamRules.ReadinessTip(Readiness);

        public IAsyncRelayCommand ConfigureCommand { get; }
    }
}
