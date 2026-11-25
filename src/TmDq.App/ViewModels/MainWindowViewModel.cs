using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Models.Export;
using TmDq.App.Models.Logging;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Settings;
using TmDq.App.Models.Templates;
using TmDq.App.Services;
using TmDq.App.ViewModels.Dialogs;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;

namespace TmDq.App.ViewModels;

/// <summary>The main window's state: open a recording, ask what to process (Configure Streams, plan step 4.1; with no
/// one to ask, the first PCM channel with the Frame Sync Lock defaults, as <c>FirstChannelRun</c> does), process it and
/// plot it (plan step 4.3b: <see cref="Plot"/>), with the progress dialog up for the run and every step in the log
/// (plan step 4.2).
/// <para>Three jobs are done by types of their own, reached through this one's members: what the shell remembers
/// (<see cref="ShellPreferences"/>), Export (<see cref="SessionExport"/>) and Apply Template's batch
/// (<see cref="BatchRunner"/>). What it reaches outside itself is handed in as one <see cref="MainWindowServices"/>.</para>
/// </summary>
public sealed class MainWindowViewModel : ObservableObject, IRunHost
{
    /// <summary>Recent Files with nothing in it.</summary>
    public const string NoRecentFiles = "(No recent files)";

    /// <summary>What the status line and the log begin with when tmdq_core.dll is missing or is not the one this build was made for.</summary>
    public const string CoreNotLoaded = "The processing core could not be loaded: ";

    private readonly IRecordingPicker? _picker;
    private readonly IStreamConfigurator _configurator;
    private readonly IProcessingDialog? _processingDialog;
    private readonly IDataFilePicker? _files;
    private readonly ICsvKindDialog? _csvKinds;
    private readonly IExportDialog? _exportDialog;
    private readonly ITemplateFilePicker? _templateFiles;
    private readonly ITemplateDialogs? _templateDialogs;
    private readonly IHelpActions? _help;
    private readonly ShellPreferences _shell;
    private readonly SessionExport _export;
    private readonly BatchRunner _batch;
    private readonly string _appRoot;
    private readonly List<ProcessedSource> _sources = new();
    private List<MenuChoice>? _recentMenu;
    private string _runningFile = string.Empty;
    private string _status = "Open a Chapter 10 recording.";
    private LogLevel _statusLevel = LogLevel.Info;
    private bool _isBusy;

    /// <param name="services">What it reaches outside itself; null, or any one left out, as
    /// <see cref="MainWindowServices"/> says.</param>
    public MainWindowViewModel(MainWindowServices? services = null)
    {
        services ??= new MainWindowServices();
        _shell = new ShellPreferences(services.Preferences ?? new PreferencesStore(new Preferences()));
        _picker = services.Picker;
        _configurator = services.Configurator ?? new FirstChannelRun();
        _processingDialog = services.ProcessingDialog;
        _files = services.Files;
        _csvKinds = services.CsvKinds;
        _exportDialog = services.ExportDialog;
        _templateFiles = services.TemplateFiles;
        _templateDialogs = services.TemplateDialogs;
        _help = services.Help;
        Log = new SessionLog(services.Clock);
        _export = new SessionExport(Plot, Log, services.ImageExporter, Report);
        _appRoot = services.AppRoot ?? AppContext.BaseDirectory;
        _batch = new BatchRunner(this, Plot, Log, _export, _appRoot);
        OpenCommand = new AsyncRelayCommand(OpenAsync, () => !IsBusy && _picker is not null);
        AddRecordingCommand = new AsyncRelayCommand(AddRecordingAsync, () => !IsBusy && _picker is not null);
        ImportCsvCommand = new AsyncRelayCommand(PickAndImportAsync, () => !IsBusy && _files is not null);
        ExportCommand = new AsyncRelayCommand(ExportAsync, () => !IsBusy && Plot.HasData && _exportDialog is not null);
        SaveTemplateCommand = new AsyncRelayCommand(SaveTemplateAsync, () => !IsBusy && _sources.Count > 0 && _templateFiles is not null);
        ApplyTemplateCommand = new AsyncRelayCommand(ApplyTemplateAsync, () => !IsBusy && _templateFiles is not null && _templateDialogs is not null);
        OpenRecentCommand = new AsyncRelayCommand<string>(p => OpenPathAsync(p!), p => !IsBusy && p is not null);
        // Help is there while a run goes on, as v2.x's was: it touches nothing a run uses.
        OpenManualCommand = new AsyncRelayCommand(() => _help!.OpenManualAsync(), () => _help is not null);
        ShowAboutCommand = new AsyncRelayCommand(() => _help!.ShowAboutAsync(), () => _help is not null);
        _shell.PropertyChanged += (_, e) =>
        {
            OnPropertyChanged(e.PropertyName);
            if (e.PropertyName == nameof(RecentFiles))
            {
                _recentMenu = null;
                OnPropertyChanged(nameof(RecentMenu));
            }
        };
        // The legend's visibility is remembered between sessions (US4.0, US4.1), as v2.x remembered it.
        Plot.LegendVisible = _shell.LegendVisible;
        Log.Add("tmDataQualityAnalyzer v" + AppInfo.Version);
        Log.Add("Load a Chapter 10 file to configure per-stream processing.");
        // A time window entered outside the recording is clamped, and says so in the log, in goldenrod; so are the rows
        // an imported CSV skipped.
        Plot.Warning += (_, message) => Log.Warning(message);
        // A batch's recordings share one time axis; one that does not overlap the rest is said so, in v2.14.2's
        // words.
        Plot.NonOverlappingSource += (_, _) => Log.Warning(
            "The most recently added source's recording time ('" + _runningFile + "') does not overlap the data already loaded. " +
            "They will share one time axis with a large gap between them.");
        Plot.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(PlotViewModel.HasData)) ExportCommand.NotifyCanExecuteChanged();
            if (e.PropertyName == nameof(PlotViewModel.LegendVisible)) _shell.LegendVisible = Plot.LegendVisible;
        };
    }

    // ---- the shell (plan step 4.8): what is remembered, recent files, the theme and the sidebar --------------------

    /// <summary>The recordings and CSVs opened last, the newest first (the menu's Recent Files).</summary>
    public IReadOnlyList<string> RecentFiles => _shell.RecentFiles;

    /// <summary>Opens a recent file, or one dropped on the window. Unavailable while busy.</summary>
    public IAsyncRelayCommand<string> OpenRecentCommand { get; }

    public IRelayCommand ClearRecentFilesCommand => _shell.ClearRecentFilesCommand;

    /// <summary>The menu's Recent Files: each by its name, its path as the tooltip, newest first, then Clear Recent
    /// Files; or <see cref="NoRecentFiles"/>, disabled.</summary>
    public IReadOnlyList<MenuChoice> RecentMenu => _recentMenu ??= MakeRecentMenu();

    private List<MenuChoice> MakeRecentMenu()
    {
        if (RecentFiles.Count == 0) return new List<MenuChoice> { new(NoRecentFiles) { IsEnabled = false } };
        var menu = RecentFiles.Select(path => new MenuChoice(Path.GetFileName(path), OpenRecentCommand, path) { Tip = path }).ToList();
        menu.Add(MenuChoice.Separator());
        menu.Add(new MenuChoice("Clear Recent Files", ClearRecentFilesCommand));
        return menu;
    }

    /// <summary>
    /// Opens <paramref name="path"/> by its suffix, as v2.x did for Recent Files and a drop: an
    /// exported CSV is imported, anything else opened as a recording. Ignored while busy, as both are.
    /// </summary>
    public Task OpenPathAsync(string path) => Models.Settings.RecentFiles.IsCsv(path) ? ImportCsvAsync(path) : LoadAsync(path);

    /// <summary>The dark theme (v2.x's default too) or the light one (US9.0); the choice is remembered.</summary>
    public bool IsDarkTheme
    {
        get => _shell.IsDarkTheme;
        set => _shell.IsDarkTheme = value;
    }

    /// <summary>The light theme, as the Settings menu's Light Theme entry shows it: checked when it is the one in use.</summary>
    public bool IsLightTheme => _shell.IsLightTheme;

    public IRelayCommand UseLightThemeCommand => _shell.UseLightThemeCommand;

    public IRelayCommand UseDarkThemeCommand => _shell.UseDarkThemeCommand;

    /// <summary>The log sidebar shown (the title bar's toggle, Ctrl+B); the choice is remembered.</summary>
    public bool SidebarVisible
    {
        get => _shell.SidebarVisible;
        set => _shell.SidebarVisible = value;
    }

    public IRelayCommand ToggleSidebarCommand => _shell.ToggleSidebarCommand;

    [SuppressMessage("Performance", "CA1822:Mark members as static", Justification = "Bound from the window's XAML, which binds to an instance.")]
    public string Title => AppInfo.Title;

    /// <summary>The log sidebar's contents.</summary>
    public SessionLog Log { get; }

    /// <summary>What the chart draws: every stream processed, on one time axis.</summary>
    public PlotViewModel Plot { get; } = new();

    /// <summary>One short line saying what happened last, in plain words, for the status bar under the log and the chart:
    /// what is being done ("Processing x.ch10..."), what finished ("Finished processing x.ch10: 3 streams."), or what
    /// went wrong. The log keeps the detail (every path, every stream); this is the sentence that is still there with the
    /// log hidden.</summary>
    public string Status
    {
        get => _status;
        private set => SetProperty(ref _status, value);
    }

    /// <summary>How <see cref="Status"/> went: a success, a warning (done, with something left out) or an error draw a mark
    /// and a colour in the status bar; information (what is under way, the first prompt) does not.</summary>
    public LogLevel StatusLevel
    {
        get => _statusLevel;
        private set
        {
            if (SetProperty(ref _statusLevel, value)) OnPropertyChanged(nameof(HasStatusMark));
        }
    }

    /// <summary>Whether the status bar draws a mark (a tick, a warning sign, a cross) before the status.</summary>
    public bool HasStatusMark => _statusLevel != LogLevel.Info;

    /// <summary>Sets the status line and how it went.</summary>
    private void Report(string text, LogLevel level = LogLevel.Info)
    {
        Status = text;
        StatusLevel = level;
    }

    public bool IsBusy
    {
        get => _isBusy;
        private set
        {
            if (!SetProperty(ref _isBusy, value)) return;
            OpenCommand.NotifyCanExecuteChanged();
            AddRecordingCommand.NotifyCanExecuteChanged();
            ImportCsvCommand.NotifyCanExecuteChanged();
            ExportCommand.NotifyCanExecuteChanged();
            SaveTemplateCommand.NotifyCanExecuteChanged();
            ApplyTemplateCommand.NotifyCanExecuteChanged();
            OpenRecentCommand.NotifyCanExecuteChanged();
        }
    }

    /// <summary>The recordings processed this session and still on the plot, as a template is captured from them; Open and a batch replace them, Add Recording appends. No view
    /// reads it: it is kept public as the tests' only window onto what Save Template would capture (a source's id and path
    /// are not written into the template), as audit D8 decided.</summary>
    public IReadOnlyList<ProcessedSource> Sources => _sources;

    /// <summary>Save as Template: one processed recording's rows and its series' looks, to a file (US1.1).</summary>
    public IAsyncRelayCommand SaveTemplateCommand { get; }

    /// <summary>Apply Template to Files: a template run over recordings chosen together, one after another (US1.1).</summary>
    public IAsyncRelayCommand ApplyTemplateCommand { get; }

    // Only ever while busy: Save as Template is told when the work ends.
    private void SetSources(IEnumerable<ProcessedSource> sources)
    {
        _sources.Clear();
        _sources.AddRange(sources);
    }

    /// <summary>The folder of the last recording opened, CSV imported or template used: where Import CSV and the
    /// template dialogs start and Export proposes to write, remembered between sessions (v2.x's
    /// <c>LastCh10Directory</c>, which v3.1.0 no longer reads).</summary>
    public string? LastFolder
    {
        get => _shell.LastFolder;
        private set => _shell.LastFolder = value;
    }

    public IAsyncRelayCommand OpenCommand { get; }

    /// <summary>Add Recording: configures and processes another recording as Open does, but keeps everything already on the
    /// plot (the recordings processed before and what was imported), each recording its own entry in Plot File and Remove File.
    /// With an empty plot it is Open.</summary>
    public IAsyncRelayCommand AddRecordingCommand { get; }

    /// <summary>Help &gt; User Manual: the full manual beside the application, else the base one.</summary>
    public IAsyncRelayCommand OpenManualCommand { get; }

    /// <summary>Help &gt; About: the version and what the application does.</summary>
    public IAsyncRelayCommand ShowAboutCommand { get; }

    /// <summary>Import CSV: an exported CSV shown without processing anything (US6.3).</summary>
    public IAsyncRelayCommand ImportCsvCommand { get; }

    /// <summary>Export: the plot as an image, its data as CSV and the log as text, any of them in one action (US6.0 to
    /// US6.2). Available once there is data.</summary>
    public IAsyncRelayCommand ExportCommand { get; }

    private static readonly CompositeFormat NotAnExportedCsv = CompositeFormat.Parse(
        "ERROR: '{0}' is not a recognized exported CSV (expected a 'Time (DOY:HH:MM:SS.mmm)' column with data rows). Nothing was imported.");

    private static readonly CompositeFormat NotAPhasedArrayLog = CompositeFormat.Parse(
        "ERROR: '{0}' is not a recognized AV Phased Array Antenna log (expected the header 'Time, Name, Polarity, Stream 1, ...' with data rows). Nothing was imported.");

    private async Task OpenAsync()
    {
        string? path = await _picker!.PickRecordingAsync();
        if (path is not null) await LoadAsync(path);
    }

    private async Task AddRecordingAsync()
    {
        string? path = await _picker!.PickRecordingAsync();
        if (path is not null) await LoadAsync(path, add: true);
    }

    private async Task PickAndImportAsync()
    {
        string? path = await _files!.OpenCsvAsync(LastFolder);
        if (path is not null) await ImportCsvAsync(path);
    }

    /// <summary>
    /// Shows an exported CSV in place of whatever was shown (US6.3): the plot cleared, the file
    /// read off the UI thread with "Loading..." over the chart, then titled with its name. A file that is not an
    /// exported CSV is an error, and nothing is shown. An antenna's log (US10.0) is added to the plot instead, as a recording of
    /// its own named for the file, the title and the rest left as they were. Ignored while busy.
    /// </summary>
    public async Task ImportCsvAsync(string path, CsvKind? kind = null)
    {
        if (IsBusy) return;
        string name = Path.GetFileName(path);
        // Every way in asks what the file is (US10.0), each time: the menu, a drop, Recent Files. Cancel imports nothing. A host with no
        // dialog reads it as the application's own export, which is all a CSV was before.
        if (kind is null && _csvKinds is not null)
        {
            kind = await _csvKinds.ChooseAsync(name);
            if (kind is null || IsBusy) return;
        }
        CsvKind chosen = kind ?? CsvKind.TelemetryReceivers;
        LastFolder = Path.GetDirectoryName(Path.GetFullPath(path));
        IsBusy = true;
        try
        {
            Log.Add("Importing CSV: " + name);
            // An antenna's log joins what the plot shows, so that the two can be compared (US10.0); any other CSV replaces it.
            bool joins = chosen == CsvKind.PhasedArrayAntenna && Plot.HasData;
            if (!joins) ClearRun();
            NativeCore.EnsureLoaded();
            if (await Plot.LoadCsvAsync(path, chosen, BaseName(name)) == CsvLoad.Loaded)
            {
                _shell.AddRecentFile(path);
                if (!joins) Plot.Title = BaseName(name);
                Report("Imported " + name + ".", LogLevel.Success);
                Log.Success("Imported CSV: " + name);
            }
            else
            {
                string error = string.Format(CultureInfo.InvariantCulture,
                    chosen == CsvKind.PhasedArrayAntenna ? NotAPhasedArrayLog : NotAnExportedCsv, name);
                Report(error, LogLevel.Error);
                Log.Error(error);
            }
        }
        catch (Exception e) when (e is FileNotFoundException or InvalidOperationException)
        {
            string error = CoreNotLoaded + e.Message;
            Report(error, LogLevel.Error);
            Log.Error(error);
        }
        finally
        {
            IsBusy = false;
        }
    }

    private async Task ExportAsync()
    {
        (string image, string data, string log) = ExportViewModel.DefaultPaths(LastFolder ?? _appRoot, Plot.Title);
        var choice = new ExportViewModel(image, data, log, _files, Plot.HasExportableData);
        await _exportDialog!.ShowAsync(choice);
        if (choice.Accepted) await ExportAsync(choice);
    }

    /// <summary>
    /// Writes what <paramref name="choice"/> asks for, in v2.x's order (the data, the image, then the log, which so
    /// carries the lines about the other two), each saying in the log where it went or that it failed; a failure does
    /// not stop the rest. Nothing without data. The data is written off the UI thread, with the window busy
    /// meanwhile (nothing else may open, import or export); ignored while busy.
    /// </summary>
    public async Task ExportAsync(ExportViewModel choice)
    {
        if (IsBusy) return;
        IsBusy = true;
        try
        {
            await _export.WriteAsync(choice);
        }
        finally
        {
            IsBusy = false;
        }
    }

    // ---- templates and batch (plan step 4.7, US1.1) ------------------------------------------------------------------

    /// <summary>
    /// Save as Template (US1.1): with several recordings processed, which to
    /// capture from; then where; then the template, with the series' names and colours as they are now.
    /// </summary>
    private async Task SaveTemplateAsync()
    {
        if (_sources.Count == 0)
        {
            Report("Nothing to save as a template yet.", LogLevel.Warning);
            Log.Warning("Nothing to save as a template -- no sources have finished processing yet.");
            return;
        }
        int index = 0;
        if (_sources.Count > 1)
        {
            if (_templateDialogs is null) return;
            var choices = _sources.Select(s => Path.GetFileName(s.FilePath) +
                                               string.Create(CultureInfo.InvariantCulture, $" (source {s.SourceId})")).ToList();
            if (await _templateDialogs.ChooseSourceAsync(choices) is not int chosen) return;
            index = chosen;
        }
        string? path = await _templateFiles!.SaveTemplateAsync(LastFolder);
        if (string.IsNullOrEmpty(path)) return;
        SaveTemplate(_sources[index], path);
    }

    /// <summary>Writes the template captured from <paramref name="source"/> to <paramref name="path"/>, and says so.</summary>
    public bool SaveTemplate(ProcessedSource source, string path)
    {
        try
        {
            TemplateFile.Write(path, PlotTemplates.Build(source, Plot));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            Report("Could not save the template. See the log.", LogLevel.Error);
            Log.Error("Could not write template file: " + path);
            return false;
        }
        LastFolder = Path.GetDirectoryName(Path.GetFullPath(path));
        Report("Saved template " + Path.GetFileName(path) + ".", LogLevel.Success);
        Log.Success("Template saved: " + Path.GetFileName(path));
        return true;
    }

    /// <summary>
    /// Apply Template to Files (US1.1): a template, read with its errors in
    /// the log; then the recordings; each checked against the template; the confirmation; then the batch.
    /// </summary>
    private async Task ApplyTemplateAsync()
    {
        if (IsBusy) return;
        string? templatePath = await _templateFiles!.OpenTemplateAsync(LastFolder);
        if (string.IsNullOrEmpty(templatePath)) return;
        if (ReadTemplate(templatePath) is not { } template) return;
        LastFolder = Path.GetDirectoryName(Path.GetFullPath(templatePath));

        IReadOnlyList<string> picked = await _templateFiles.OpenRecordingsAsync(LastFolder);
        if (picked.Count == 0) return;
        IReadOnlyList<FileCheck> checks;
        // The check runs off the UI thread with no dialog up, so Open, a drop and the rest would still be there to start
        // another load; one that did would leave the batch, once confirmed, to refuse itself (RunBatchAsync) and say nothing.
        // Busy for the check; not for the confirmation, whose dialog is modal, nor for the batch, which is busy itself.
        IsBusy = true;
        try
        {
            NativeCore.EnsureLoaded();
            checks = await Task.Run(() => TemplateBatch.Validate(template, picked));
        }
        catch (Exception e) when (e is FileNotFoundException or InvalidOperationException)
        {
            // As Open, Import CSV and the batch say it: reported, not thrown out of the command.
            string error = CoreNotLoaded + e.Message;
            Report(error, LogLevel.Error);
            Log.Error(error);
            return;
        }
        finally
        {
            IsBusy = false;
        }
        var confirm = new BatchApplyViewModel(checks, LastFolder ?? _appRoot, _templateFiles);
        await _templateDialogs!.ConfirmBatchAsync(confirm);
        if (!confirm.Accepted || confirm.Matched.Count == 0) return;
        await RunBatchAsync(template, confirm.Matched, confirm.ExportPerFile ? confirm.OutputFolder.Trim() : null, confirm.ReuseAppearance);
    }

    /// <summary>The template at <paramref name="path"/>, or null with the reason in the log, in v2.14.2's
    /// words.</summary>
    public ProcessingTemplate? ReadTemplate(string path)
    {
        TemplateLoad load;
        ProcessingTemplate? template;
        string error;
        try
        {
            load = TemplateFile.Read(path, out template, out error);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return CouldNotRead("Could not open template file: " + path);
        }
        switch (load)
        {
            case TemplateLoad.InvalidJson:
                return CouldNotRead("Template file is not valid JSON: " + error);
            case TemplateLoad.UnsupportedSchemaVersion:
                return CouldNotRead("Template file uses an unsupported schema version.");
            case TemplateLoad.InvalidFormat:
                return CouldNotRead("Template file is not a valid template (unrecognized format).");
        }
        if (template!.Entries.Count == 0) return CouldNotRead("Template has no stream entries.");
        return template;
    }

    /// <summary>The log gets the reason, in v2.14.2's words; the status bar says only that the template could not be read.</summary>
    private ProcessingTemplate? CouldNotRead(string reason)
    {
        Report("Could not read the template. See the log.", LogLevel.Error);
        Log.Error(reason);
        return null;
    }

    /// <summary>
    /// Runs <paramref name="template"/> over <paramref name="files"/>, one after another, as v2.x's
    /// batch did: a fresh plot; each file checked again and run with the template's rows, kept in the
    /// plot as a recording of its own, named for the Plot File selector, with the template's names and colours when
    /// asked; a file that is gone, no longer matches, or fails is skipped and said so. Then, when a folder is given, a
    /// CSV and plot images per file; the plot left on the first file; and the summary.
    /// </summary>
    /// <param name="outputFolder">Where each file's CSV and images go; null exports nothing.</param>
    public async Task RunBatchAsync(ProcessingTemplate template, IReadOnlyList<string> files, string? outputFolder, bool reuseAppearance)
    {
        if (files.Count == 0 || IsBusy) return;
        IsBusy = true;
        try
        {
            ClearRun();
            BatchResult result = await _batch.RunAsync(template, files, outputFolder, reuseAppearance);
            SetSources(result.Sources);
            // The bar says how many; the log also says where the output went.
            Report(result.Headline, result.Skipped > 0 ? LogLevel.Warning : LogLevel.Success);
            Log.Success(result.Summary);
        }
        catch (Exception e) when (e is FileNotFoundException or InvalidOperationException)
        {
            string error = CoreNotLoaded + e.Message;
            Report(error, LogLevel.Error);
            Log.Error(error);
        }
        finally
        {
            IsBusy = false;
        }
    }

    /// <summary>Opens <paramref name="path"/>, asks what to process, processes it and shows the result or the reason it
    /// failed, logging each step as v2.x did. The progress dialog is up for the run, and its Cancel
    /// stops every stream. Cancelling Configure Streams leaves the last result on screen. Ignored while busy. With
    /// <paramref name="add"/> (Add Recording) and something on the plot, nothing on the plot is removed and the recording is
    /// added beside it, named in Plot File and Remove File, the plot's title unchanged.</summary>
    public async Task LoadAsync(string path, bool add = false)
    {
        if (IsBusy) return;
        string name = Path.GetFileName(path);
        // Add Recording keeps everything on the plot; with nothing on it, there is nothing to keep and it is Open.
        bool keep = add && Plot.HasData;
        string verb = keep ? "Adding" : "Opening";
        LastFolder = Path.GetDirectoryName(Path.GetFullPath(path));
        IsBusy = true;
        try
        {
            Report($"{verb} {name}...");
            Log.Add($"{verb}: {name}");
            Ch10File file = await StreamsRun.OpenAsync(path);
            // A recording joins Recent Files once its channels are read, as in v2.x.
            _shell.AddRecentFile(path);
            Log.Add("  File size: " + SizeText(new FileInfo(path).Length));
            Log.Add("  Time channels: " + CountText(file.TimeChannels.Count));
            Log.Add("  PCM channels: " + CountText(file.PcmChannels.Count));
            StreamsChoice? choice = await _configurator.ConfigureAsync(path, file);
            if (choice is null)
            {
                Report($"Cancelled. {name} was not processed.");
                return;
            }
            if (!keep) ClearProcessedRun();
            // The run is numbered after what the plot keeps, so that a CSV imported before it never shares its source id.
            RunRequest request = choice.Request with { SourceId = Plot.NextSourceId };
            Report($"Processing {name}...");
            using RunOutcome outcome = await RunWithDialogAsync(request);
            // The core builds each stream's series, in table order, on one shared time axis.
            foreach (StreamResult result in outcome.Results) Plot.AddStream(result);
            if (!keep) Plot.Title = BaseName(name);   // an added recording leaves the plot's title as it is
            // The recording, as a template is captured from it, named in the Plot File selector.
            var processed = new ProcessedSource(request.SourceId, path, choice.TimeChannelIndex, request.SwapBytes, choice.Rows);
            SetSources(keep ? _sources.Append(processed).ToList() : new[] { processed });
            Plot.SetSourceLabel(request.SourceId, keep ? UniqueSourceLabel(BaseName(name)) : BaseName(name));
            Report(Describe(name, outcome), outcome.Errors.Count == 0 ? LogLevel.Success : LogLevel.Warning);
            Log.Success("Processing complete — results plotted from memory.");
        }
        catch (OperationCanceledException)
        {
            if (!keep) ClearProcessedRun();
            Report($"Cancelled. {name} was not processed.", LogLevel.Warning);
        }
        catch (RecordingException e)
        {
            if (!keep) ClearProcessedRun();
            Report(e.Message, LogLevel.Error);
            // A run with nothing processed has logged the core's own reasons already.
            if (e is not NothingProcessedException) Log.Error(e.Message);
        }
        catch (Exception e) when (e is FileNotFoundException or InvalidOperationException)
        {
            // The native core is missing or does not match this build.
            if (!keep) ClearProcessedRun();
            string error = CoreNotLoaded + e.Message;
            Report(error, LogLevel.Error);
            Log.Error(error);
        }
        finally
        {
            IsBusy = false;
        }
    }

    void IRunHost.SetStatus(string status) => Report(status);

    void IRunHost.SetRunningFile(string name) => _runningFile = name;

    Task<RunOutcome> IRunHost.RunWithDialogAsync(RunRequest request) => RunWithDialogAsync(request);

    /// <summary>Runs <paramref name="request"/> with the progress dialog up until it has stopped.</summary>
    private async Task<RunOutcome> RunWithDialogAsync(RunRequest request)
    {
        using var processing = new ProcessingViewModel();
        _processingDialog?.Show(processing);
        try
        {
            // Made here, both post each report back to this (the UI) thread, in order.
            var progress = new Progress<int>(p => processing.Progress = p);
            return await StreamsRun.RunAsync(request, progress, new UiRunLog(Log), processing.Token);
        }
        finally
        {
            _processingDialog?.Close();
        }
    }

    /// <summary>A file size as v2.14.2's log gave it: megabytes from 1 MiB, kilobytes below, one decimal.</summary>
    public static string SizeText(long bytes) => bytes >= 1048576
        ? (bytes / 1048576.0).ToString("F1", CultureInfo.InvariantCulture) + " MB"
        : (bytes / 1024.0).ToString("F1", CultureInfo.InvariantCulture) + " KB";

    /// <summary>A channel count as v2.14.2's log gave it: "none" for zero.</summary>
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

    /// <summary>A file name as the plot's title takes it: up to its first dot (all of it without one).</summary>
    public static string BaseName(string fileName)
    {
        int dot = fileName.IndexOf('.', StringComparison.Ordinal);
        return dot < 0 ? fileName : fileName[..dot];
    }

    /// <summary>A new run, or one that failed: the plot is cleared, and the recordings it held.</summary>
    private void ClearRun()
    {
        Plot.Clear();
        SetSources(Array.Empty<ProcessedSource>());
    }

    /// <summary>
    /// A new recording to process, or one that failed: the recordings processed before are removed from the plot and from
    /// <see cref="Sources"/>, and what was imported from CSV files stays, so a recording can be processed beside data
    /// imported earlier (an antenna's log, an export) and compared with it. With nothing imported this is
    /// <see cref="ClearRun"/>: the plot starts again, its title and view with it.
    /// </summary>
    private void ClearProcessedRun()
    {
        var processed = new HashSet<int>(_sources.Select(s => s.SourceId));
        if (Plot.SourceList().All(s => processed.Contains(s.Id)))
        {
            ClearRun();
            return;
        }
        foreach (int id in processed) Plot.RemoveSource(id);
        SetSources(Array.Empty<ProcessedSource>());
    }

    /// <summary>A recording's name for Plot File and Remove File, made different from every one the plot already holds, for a
    /// recording added a second time (or two of one name from different folders): "name (2)", "name (3)".</summary>
    private string UniqueSourceLabel(string label)
    {
        HashSet<string> taken = Plot.SourceList().Select(s => s.Label).ToHashSet();
        if (!taken.Contains(label)) return label;
        for (int i = 2; ; i++)
        {
            string candidate = string.Create(CultureInfo.InvariantCulture, $"{label} ({i})");
            if (!taken.Contains(candidate)) return candidate;
        }
    }

    /// <summary>The status line after a run: that it finished and how many streams it processed, and how many it could not
    /// (the log says which, and why). The numbers of each stream are in the log and on the plot, not here.</summary>
    public static string Describe(string name, RunOutcome outcome)
    {
        int done = outcome.Streams.Count, failed = outcome.Errors.Count;
        string text = string.Create(CultureInfo.InvariantCulture,
            $"Finished processing {name}: {done} {(done == 1 ? "stream" : "streams")}");
        return failed == 0 ? text + "." : string.Create(CultureInfo.InvariantCulture, $"{text}, {failed} not processed. See the log.");
    }
}
