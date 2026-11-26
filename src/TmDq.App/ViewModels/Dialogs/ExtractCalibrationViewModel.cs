using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.App.Services;
using TmDq.Core;

namespace TmDq.App.ViewModels.Dialogs;

/// <summary>How a line of the dialog reads, which decides its mark and its colour.</summary>
public enum CalibrationStatusKind
{
    Pending,
    Ok,
    Warn,
    Fail,
}

/// <summary>
/// The Extract Calibration dialog's state (plan step 4.6, US5.3): a step file
/// (the stream's last, else the shipped default, loaded as the dialog opens), a calibration recording, and a clip at
/// each end. Once both files are in hand, and again after any change to them or to a clip, the calibration is
/// extracted, with the progress dialog up and Cancel stopping it. OK is available once the step file parsed and at
/// least one channel calibrated; it gives the profiles, the two files, the clips and the report.
/// </summary>
public sealed class ExtractCalibrationViewModel : DialogViewModel
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
    private decimal _clipStart, _clipEnd;
    private string _clipStartText = NumberText.FormatDecimal(0m), _clipEndText = NumberText.FormatDecimal(0m);
    private string? _clipStartProblem, _clipEndProblem;
    private IReadOnlyList<ChannelInfo> _calPcm = Array.Empty<ChannelInfo>(), _calTime = Array.Empty<ChannelInfo>();
    private int _calPcmIndex = -1, _calTimeIndex = -1;
    private int _loadVersion;   // the latest recording chosen: an earlier one whose channels arrive late is dropped

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
        SetStep(CalibrationStatusKind.Pending, "No file selected.");
        SetCal(CalibrationStatusKind.Pending, "No file selected.");

        // Almost every calibration uses the same step file, so the dialog starts with one loaded, and says it was
        // loaded automatically: a step file that does not match the recording still calibrates, just wrongly.
        string initial = StepFile.InitialPath(appRoot, initialStepPath);
        if (initial.Length > 0) LoadStepFile(initial, automatic: true);
    }

    /// <summary>The progress dialog an extraction shows; the window sets it, so it sits over the dialog.</summary>
    public IProcessingDialog? ProgressDialog { get; set; }

    /// <summary>What starts an extraction: the core, unless a test stands in.</summary>
    public ICalibrationRunner Runner { get; set; } = CoreCalibrationRunner.Instance;

    /// <summary>What the calibration run is given besides the files and clips. No view reads it: it is kept public as the
    /// tests' only window onto the request the stream's settings make, as audit D8 decided.</summary>
    public CalibrationBase Base => _base;

    /// <summary>What lists the calibration recording's channels once it is chosen: the core, unless a test stands in.</summary>
    public Func<string, RecordingChannels> ReadChannels { get; set; } = RecordingChannels.Read;

    // The calibration recording is often not the recording the stream was set up on, and its step cals can be on another
    // channel of it. Once it is chosen the dialog lists its PCM and time channels, starts on the stream's own (or, where
    // the recording has none by that id, the first PCM channel and the recording's default time channel) and extracts
    // again when either is changed.

    /// <summary>"&lt;id&gt; - &lt;name&gt;" for each PCM channel of the calibration recording; empty until one is chosen.</summary>
    public IReadOnlyList<string> CalPcmItems { get; private set; } = Array.Empty<string>();

    /// <summary>The same for its time channels; empty when it has none.</summary>
    public IReadOnlyList<string> CalTimeItems { get; private set; } = Array.Empty<string>();

    /// <summary>True once the calibration recording's channels are listed, which is when the dialog shows them.</summary>
    public bool HasCalChannels => _calPcm.Count > 0;

    /// <summary>Says when the stream's PCM channel is not in the calibration recording and another was selected.</summary>
    public string ChannelNote { get; private set; } = string.Empty;

    public bool HasChannelNote => ChannelNote.Length > 0;

    /// <summary>The selected PCM channel; setting it extracts again.</summary>
    public int CalPcmIndex
    {
        get => _calPcmIndex;
        set
        {
            if (value < 0 || value >= _calPcm.Count || !SetProperty(ref _calPcmIndex, value)) return;
            _ = ExtractAsync();
        }
    }

    /// <summary>The selected time channel; setting it extracts again.</summary>
    public int CalTimeIndex
    {
        get => _calTimeIndex;
        set
        {
            if (value < 0 || value >= _calTime.Count || !SetProperty(ref _calTimeIndex, value)) return;
            _ = ExtractAsync();
        }
    }

    /// <summary>The PCM channel the extraction reads: the selected one, else the stream's own.</summary>
    public int PcmChannelId => _calPcmIndex >= 0 && _calPcmIndex < _calPcm.Count ? _calPcm[_calPcmIndex].Id : _base.PcmChannelId;

    /// <summary>The time channel the extraction reads: the selected one, else the one chosen in Configure Streams.</summary>
    public int TimeChannelId => _calTimeIndex >= 0 && _calTimeIndex < _calTime.Count ? _calTime[_calTimeIndex].Id : _base.TimeChannelId;

    public string StepPath => _stepPath;

    public string StepName => Path.GetFileName(_stepPath);

    public string CalPath => _calPath;

    public string CalName => Path.GetFileName(_calPath);

    public string StepStatus => _stepStatus;

    public string CalStatus => _calStatus;

    public CalibrationStatusKind StepKind => _stepKind;

    public CalibrationStatusKind CalKind => _calKind;

    // The two clips are plain text boxes (no up/down arrows). What is typed is held in the *Text strings; text that is a
    // number from 0 to ClipMaximum becomes the clip and extracts again, and anything else is a problem that holds OK and
    // leaves the clip, and the calibration, as they were.

    /// <summary>Seconds left off the start of the calibration recording before steps are detected (0 to 100,000); a
    /// change extracts again. Setting it types it into <see cref="ClipStartText"/>.</summary>
    public decimal ClipStart
    {
        get => _clipStart;
        set => ClipStartText = NumberText.FormatDecimal(value);
    }

    /// <summary>Seconds left off the end, likewise.</summary>
    public decimal ClipEnd
    {
        get => _clipEnd;
        set => ClipEndText = NumberText.FormatDecimal(value);
    }

    public string ClipStartText
    {
        get => _clipStartText;
        set
        {
            if (!SetProperty(ref _clipStartText, value ?? string.Empty)) return;
            if (TakeClip(_clipStartText, "Clip Start", ref _clipStartProblem) is decimal clip && SetProperty(ref _clipStart, clip, nameof(ClipStart))) _ = ExtractAsync();
            ClipProblemsChanged();
        }
    }

    public string ClipEndText
    {
        get => _clipEndText;
        set
        {
            if (!SetProperty(ref _clipEndText, value ?? string.Empty)) return;
            if (TakeClip(_clipEndText, "Clip End", ref _clipEndProblem) is decimal clip && SetProperty(ref _clipEnd, clip, nameof(ClipEnd))) _ = ExtractAsync();
            ClipProblemsChanged();
        }
    }

    public double ClipStartSec => (double)_clipStart;

    public double ClipEndSec => (double)_clipEnd;

    /// <summary>What is wrong with the clips as typed, one line each, or empty.</summary>
    public string ClipProblemText => string.Join(Environment.NewLine, new[] { _clipStartProblem, _clipEndProblem }.OfType<string>());

    public bool HasClipProblem => _clipStartProblem is not null || _clipEndProblem is not null;

    /// <summary>The clip the text is, or null with the reason in <paramref name="problem"/> (null when it is one).</summary>
    private static decimal? TakeClip(string text, string name, ref string? problem)
    {
        decimal? clip = NumberText.ParseDecimal(text);
        problem = text.Trim().Length == 0 ? name + " is empty."
            : clip is null ? name + " must be a number."
            : clip is < 0m or > ClipMaximum ? string.Create(CultureInfo.InvariantCulture, $"{name} must be from 0 to {ClipMaximum:0} s.")
            : null;
        return problem is null ? clip : null;
    }

    private void ClipProblemsChanged()
    {
        OnPropertyChanged(nameof(ClipProblemText));
        OnPropertyChanged(nameof(HasClipProblem));
        UpdateOk();
    }


    /// <summary>The usable profiles of the last extraction; empty until one calibrated a channel.</summary>
    public IReadOnlyList<CalibrationProfile> Profiles { get; private set; } = Array.Empty<CalibrationProfile>();

    /// <summary>The last extraction's report (the core's summary of every receiver), with the clip warning first when
    /// the clips were ignored.</summary>
    public string Report { get; private set; } = string.Empty;

    public bool CanAccept => _stepOk && _calOk && !HasClipProblem;

    /// <summary>The folders the two Browse buttons last used, for the next dialog.</summary>
    public string? StepFolder { get; private set; }

    public string? CalFolder { get; private set; }

    /// <summary>The last extraction started (a change of clip starts one without waiting for it); null before any.</summary>
    public Task? Extraction { get; private set; }

    public IAsyncRelayCommand BrowseStepCommand { get; }

    public IAsyncRelayCommand BrowseCalCommand { get; }

    public IRelayCommand OkCommand { get; }

    /// <summary>Loads <paramref name="path"/> as the step file and extracts again (what Browse does once a file is chosen).</summary>
    public Task LoadStepFileAsync(string path)
    {
        StepFolder = Path.GetDirectoryName(path);
        LoadStepFile(path, automatic: false);
        return Extraction ?? Task.CompletedTask;
    }

    /// <summary>Takes <paramref name="path"/> as the calibration recording and extracts (what Browse does once a file is
    /// chosen). Refused, with the reason in its status, while the stream has no frame sync pattern.</summary>
    public async Task LoadCalibrationAsync(string path)
    {
        if (_base.SyncPattern.Trim().Length == 0)
        {
            SetCal(CalibrationStatusKind.Fail, "Set a frame sync pattern before loading the calibration file.");
            return;
        }
        CalFolder = Path.GetDirectoryName(path);
        _calPath = path;
        OnPropertyChanged(nameof(CalPath));
        OnPropertyChanged(nameof(CalName));

        int version = ++_loadVersion;
        // The recording's channels first (its TMATS only, off the UI thread): the extraction below reads the ones chosen.
        RecordingChannels channels;
        try
        {
            channels = await Task.Run(() => ReadChannels(path));
        }
        catch (RecordingException) when (version != _loadVersion)
        {
            return;   // another recording was chosen meanwhile: this one's failure is of no interest
        }
        catch (RecordingException e)
        {
            SetChannels(null);
            SetCalibrated(false, Array.Empty<CalibrationProfile>(), string.Empty);
            SetCal(CalibrationStatusKind.Fail, e.Message);
            return;
        }
        if (version != _loadVersion) return;   // another recording was chosen while this one's channels were read
        SetChannels(channels);
        await ExtractAsync();
    }

    /// <summary>Lists <paramref name="channels"/> and selects what the extraction starts on: the stream's own channels
    /// where the recording has them, else its first PCM channel and its default time channel. Null clears the lists.</summary>
    private void SetChannels(RecordingChannels? channels)
    {
        _calPcm = channels?.Pcm ?? Array.Empty<ChannelInfo>();
        _calTime = channels?.Time ?? Array.Empty<ChannelInfo>();
        CalPcmItems = _calPcm.Select(ChannelText).ToList();
        CalTimeItems = _calTime.Select(ChannelText).ToList();
        int pcm = _calPcm.ToList().FindIndex(c => c.Id == _base.PcmChannelId);
        int time = _calTime.ToList().FindIndex(c => c.Id == _base.TimeChannelId);
        bool timeLacking = time < 0 && _calTime.Count > 0;
        if (time < 0 && channels is not null) time = _calTime.ToList().FindIndex(c => c.Id == channels.DefaultTimeChannel);
        _calPcmIndex = _calPcm.Count == 0 ? -1 : Math.Max(0, pcm);
        _calTimeIndex = _calTime.Count == 0 ? -1 : Math.Max(0, time);
        // Each channel the stream names that the recording does not have is said, with the one selected in its place.
        var notes = new List<string>();
        if (_calPcm.Count > 0 && pcm < 0)
            notes.Add(string.Create(CultureInfo.InvariantCulture, $"The recording has no channel {_base.PcmChannelId}: channel {_calPcm[_calPcmIndex].Id} is selected."));
        if (timeLacking)
            notes.Add(string.Create(CultureInfo.InvariantCulture, $"The recording has no time channel {_base.TimeChannelId}: time channel {_calTime[_calTimeIndex].Id} is selected."));
        ChannelNote = string.Join(" ", notes);
        OnPropertyChanged(nameof(CalPcmItems));
        OnPropertyChanged(nameof(CalTimeItems));
        OnPropertyChanged(nameof(CalPcmIndex));
        OnPropertyChanged(nameof(CalTimeIndex));
        OnPropertyChanged(nameof(HasCalChannels));
        OnPropertyChanged(nameof(ChannelNote));
        OnPropertyChanged(nameof(HasChannelNote));
    }

    private static string ChannelText(ChannelInfo channel) =>
        channel.Id.ToString(CultureInfo.InvariantCulture) + " - " + channel.Name;

    /// <summary>What the extraction is asked for: the dialog's base, its step file and its clips.</summary>
    public CalibrationRequest ToRequest() => new()
    {
        FileName = _calPath,
        TimeChannelId = TimeChannelId,
        PcmChannelId = PcmChannelId,
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
        UpdateOk();
        _ = ExtractAsync();
    }

    /// <summary>
    /// Extracts with what the dialog holds: nothing until a calibration
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

        using var progress = new ProcessingViewModel(ProcessingText);
        var observer = new Observer(progress);
        ICalibrationRunning run;
        try
        {
            run = Runner.Start(ToRequest(), observer);
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
            // A report the core made that the observer could not handle was caught at the boundary and kept: the
            // success flag it records cannot be trusted, so say so rather than show a result (audit B4).
            if (run.ObserverException is { } failure)
            {
                SetCal(CalibrationStatusKind.Fail, "A report from the core could not be handled: " + failure.Message);
                return;
            }
            try
            {
                result = run.GetResult();
            }
            catch (TmdqException e)
            {
                // The core could not hand the result back (out of memory, an internal error): said, not left at
                // "Processing..." with the progress dialog gone (audit B4).
                SetCal(CalibrationStatusKind.Fail, e.Message);
                return;
            }
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
        OkCommand.NotifyCanExecuteChanged();
    }

    private void SetStep(CalibrationStatusKind kind, string text)
    {
        _stepKind = kind;
        _stepStatus = Marked(kind, text);
        OnPropertyChanged(nameof(StepKind));
        OnPropertyChanged(nameof(StepStatus));
    }

    private void SetCal(CalibrationStatusKind kind, string text)
    {
        _calKind = kind;
        _calStatus = Marked(kind, text);
        OnPropertyChanged(nameof(CalKind));
        OnPropertyChanged(nameof(CalStatus));
    }

    /// <summary>A status line as v2.x wrote it: a tick, a warning sign or a cross before the text.</summary>
    public static string Marked(CalibrationStatusKind kind, string text) => kind switch
    {
        CalibrationStatusKind.Ok => Glyphs.Ok + " " + text,
        CalibrationStatusKind.Warn => Glyphs.Warn + " " + text,
        CalibrationStatusKind.Fail => Glyphs.Fail + " " + text,
        _ => text,
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
        Accept();
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
