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
using TmDq.App.Settings;
using TmDq.App.Templates;
using TmDq.Core;

namespace TmDq.App.ViewModels
{
    /// <summary>The main window's state: open a recording, ask what to process (Configure Streams, plan step 4.1; with no
    /// one to ask, the first PCM channel with the Frame Sync Lock defaults, as the walking skeleton did), process it and
    /// plot it (plan step 4.3b: <see cref="Plot"/>), with the progress dialog up for the run and every step in the log
    /// (plan step 4.2).
    /// <para>Three jobs are done by types of their own, reached through this one's members: what the shell remembers
    /// (<see cref="ShellPreferences"/>), Export (<see cref="SessionExport"/>) and Apply Template's batch
    /// (<see cref="BatchRunner"/>). What it reaches outside itself is handed in as one <see cref="MainWindowServices"/>.</para>
    /// </summary>
    public sealed partial class MainWindowViewModel : ObservableObject, IRunHost
    {
        private readonly IRecordingPicker? _picker;
        private readonly IStreamConfigurator _configurator;
        private readonly IProcessingDialog? _processingDialog;
        private readonly IDataFilePicker? _files;
        private readonly IExportDialog? _exportDialog;
        private readonly ITemplateFilePicker? _templateFiles;
        private readonly ITemplateDialogs? _templateDialogs;
        private readonly ShellPreferences _shell;
        private readonly SessionExport _export;
        private readonly BatchRunner _batch;
        private readonly List<ProcessedSource> _sources = new();
        private string _runningFile = string.Empty;
        private ProcessingViewModel? _processing;
        private string _status = "Open a Chapter 10 recording.";
        private int _progress;
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
            _exportDialog = services.ExportDialog;
            _templateFiles = services.TemplateFiles;
            _templateDialogs = services.TemplateDialogs;
            Log = new SessionLog(services.Clock);
            _export = new SessionExport(Plot, Log, services.ImageExporter);
            _batch = new BatchRunner(this, Plot, Log, _export, services.AppRoot ?? AppContext.BaseDirectory);
            OpenCommand = new AsyncRelayCommand(OpenAsync, () => !IsBusy && _picker is not null);
            ImportCsvCommand = new AsyncRelayCommand(PickAndImportAsync, () => !IsBusy && _files is not null);
            ExportCommand = new AsyncRelayCommand(ExportAsync, () => !IsBusy && Plot.HasData && _exportDialog is not null);
            SaveTemplateCommand = new AsyncRelayCommand(SaveTemplateAsync, () => !IsBusy && _sources.Count > 0 && _templateFiles is not null);
            ApplyTemplateCommand = new AsyncRelayCommand(ApplyTemplateAsync, () => !IsBusy && _templateFiles is not null && _templateDialogs is not null);
            OpenRecentCommand = new AsyncRelayCommand<string>(p => OpenPathAsync(p!), p => !IsBusy && p is not null);
            _shell.PropertyChanged += (_, e) => OnPropertyChanged(e.PropertyName);
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

        /// <summary>What is remembered between sessions.</summary>
        public Preferences Preferences => _shell.Current;

        /// <summary>The recordings and CSVs opened last, the newest first (the menu's Recent Files).</summary>
        public IReadOnlyList<string> RecentFiles => _shell.RecentFiles;

        /// <summary>Opens a recent file, or one dropped on the window. Unavailable while busy.</summary>
        public IAsyncRelayCommand<string> OpenRecentCommand { get; }

        public IRelayCommand ClearRecentFilesCommand => _shell.ClearRecentFilesCommand;

        /// <summary>
        /// Opens <paramref name="path"/> by its suffix, as v2.x did for Recent Files and a drop: an
        /// exported CSV is imported, anything else opened as a recording. Ignored while busy, as both are.
        /// </summary>
        public Task OpenPathAsync(string path) => Settings.RecentFiles.IsCsv(path) ? ImportCsvAsync(path) : LoadAsync(path);

        /// <summary>The dark theme (v2.x's default too) or the light one (US9.0); the choice is remembered.</summary>
        public bool IsDarkTheme
        {
            get => _shell.IsDarkTheme;
            set => _shell.IsDarkTheme = value;
        }

        /// <summary>The menu item that switches the theme, named for where it goes.</summary>
        public string ThemeMenuText => _shell.ThemeMenuText;

        public IRelayCommand ToggleThemeCommand => _shell.ToggleThemeCommand;

        /// <summary>The log sidebar shown (the title bar's toggle, Ctrl+B); the choice is remembered.</summary>
        public bool SidebarVisible
        {
            get => _shell.SidebarVisible;
            set => _shell.SidebarVisible = value;
        }

        public IRelayCommand ToggleSidebarCommand => _shell.ToggleSidebarCommand;

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
                if (!SetProperty(ref _isBusy, value)) return;
                OpenCommand.NotifyCanExecuteChanged();
                ImportCsvCommand.NotifyCanExecuteChanged();
                ExportCommand.NotifyCanExecuteChanged();
                SaveTemplateCommand.NotifyCanExecuteChanged();
                ApplyTemplateCommand.NotifyCanExecuteChanged();
                OpenRecentCommand.NotifyCanExecuteChanged();
            }
        }

        /// <summary>The recordings processed this session, as a template is captured from them; cleared by a new run.</summary>
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
        /// <c>LastCh10Directory</c>, which the first run imports).</summary>
        public string? LastFolder
        {
            get => _shell.LastFolder;
            private set => _shell.LastFolder = value;
        }

        public IAsyncRelayCommand OpenCommand { get; }

        /// <summary>Import CSV: an exported CSV shown without processing anything (US6.3).</summary>
        public IAsyncRelayCommand ImportCsvCommand { get; }

        /// <summary>Export: the plot as an image, its data as CSV and the log as text, any of them in one action (US6.0 to
        /// US6.2). Available once there is data.</summary>
        public IAsyncRelayCommand ExportCommand { get; }

        public const string NotAnExportedCsv =
            "ERROR: '{0}' is not a recognized exported CSV (expected a 'Time (DOY:HH:MM:SS.mmm)' column with data rows). Nothing was imported.";

        private async Task OpenAsync()
        {
            string? path = await _picker!.PickRecordingAsync();
            if (path is not null) await LoadAsync(path);
        }

        private async Task PickAndImportAsync()
        {
            string? path = await _files!.OpenCsvAsync(LastFolder);
            if (path is not null) await ImportCsvAsync(path);
        }

        /// <summary>
        /// Shows an exported CSV in place of whatever was shown (US6.3): the plot cleared, the file
        /// read off the UI thread with "Loading..." over the chart, then titled with its name. A file that is not an
        /// exported CSV is an error, and nothing is shown. Ignored while busy.
        /// </summary>
        public async Task ImportCsvAsync(string path)
        {
            if (IsBusy) return;
            string name = Path.GetFileName(path);
            LastFolder = Path.GetDirectoryName(Path.GetFullPath(path));
            IsBusy = true;
            try
            {
                Log.Add("Importing CSV: " + name);
                ClearRun();
                NativeCore.EnsureLoaded();
                if (await Plot.LoadCsvAsync(path) == CsvLoad.Loaded)
                {
                    _shell.AddRecentFile(path);
                    Plot.Title = BaseName(name);
                    Status = "Imported CSV: " + name;
                    Log.Success(Status);
                }
                else
                {
                    Status = string.Format(CultureInfo.InvariantCulture, NotAnExportedCsv, name);
                    Log.Error(Status);
                }
            }
            catch (Exception e) when (e is FileNotFoundException or InvalidOperationException)
            {
                Status = "The processing core could not be loaded: " + e.Message;
                Log.Error(Status);
            }
            finally
            {
                IsBusy = false;
            }
        }

        private async Task ExportAsync()
        {
            (string image, string data, string log) = ExportViewModel.DefaultPaths(LastFolder ?? AppContext.BaseDirectory, Plot.Title);
            var choice = new ExportViewModel(image, data, log, _files);
            await _exportDialog!.ShowAsync(choice);
            if (choice.Accepted) await ExportAsync(choice);
        }

        /// <summary>
        /// Writes what <paramref name="choice"/> asks for, in v2.x's order (the data, the image, then the log, which so
        /// carries the lines about the other two), each saying in the log where it went or that it failed; a failure does
        /// not stop the rest. Nothing without data.
        /// </summary>
        public Task ExportAsync(ExportViewModel choice) => _export.WriteAsync(choice);

        // ---- templates and batch (plan step 4.7, US1.1) ------------------------------------------------------------------

        /// <summary>
        /// Save as Template (US1.1): with several recordings processed, which to
        /// capture from; then where; then the template, with the series' names and colours as they are now.
        /// </summary>
        private async Task SaveTemplateAsync()
        {
            if (_sources.Count == 0)
            {
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
                TemplateFile.Write(path, TemplateBatch.Build(source, Plot));
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
            {
                Log.Error("Could not write template file: " + path);
                return false;
            }
            LastFolder = Path.GetDirectoryName(Path.GetFullPath(path));
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
            NativeCore.EnsureLoaded();
            IReadOnlyList<FileCheck> checks = await Task.Run(() => TemplateBatch.Validate(template, picked));
            var confirm = new BatchApplyViewModel(checks, LastFolder ?? AppContext.BaseDirectory, _templateFiles);
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
                Log.Error("Could not open template file: " + path);
                return null;
            }
            switch (load)
            {
                case TemplateLoad.InvalidJson:
                    Log.Error("Template file is not valid JSON: " + error);
                    return null;
                case TemplateLoad.UnsupportedSchemaVersion:
                    Log.Error("Template file uses an unsupported schema version.");
                    return null;
                case TemplateLoad.InvalidFormat:
                    Log.Error("Template file is not a valid template (unrecognized format).");
                    return null;
            }
            if (template!.Entries.Count == 0)
            {
                Log.Error("Template has no stream entries.");
                return null;
            }
            return template;
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
                Status = result.Summary;
                Log.Success(Status);
            }
            catch (Exception e) when (e is FileNotFoundException or InvalidOperationException)
            {
                Status = "The processing core could not be loaded: " + e.Message;
                Log.Error(Status);
            }
            finally
            {
                Progress = 0;
                IsBusy = false;
            }
        }

        /// <summary>Opens <paramref name="path"/>, asks what to process, processes it and shows the result or the reason it
        /// failed, logging each step as v2.x did. The progress dialog is up for the run, and its Cancel
        /// stops every stream. Cancelling Configure Streams leaves the last result on screen. Ignored while busy.</summary>
        public async Task LoadAsync(string path)
        {
            if (IsBusy) return;
            string name = Path.GetFileName(path);
            LastFolder = Path.GetDirectoryName(Path.GetFullPath(path));
            IsBusy = true;
            Progress = 0;
            try
            {
                Status = $"Opening {name} ...";
                Log.Add("Opening: " + name);
                Ch10File file = await StreamsRun.OpenAsync(path);
                // A recording joins Recent Files once its channels are read, as in v2.x.
                _shell.AddRecentFile(path);
                Log.Add("  File size: " + SizeText(new FileInfo(path).Length));
                Log.Add("  Time channels: " + CountText(file.TimeChannels.Count));
                Log.Add("  PCM channels: " + CountText(file.PcmChannels.Count));
                StreamsChoice? choice = await _configurator.ConfigureAsync(path, file);
                if (choice is null)
                {
                    Status = $"{name}: not processed (Configure Streams was cancelled).";
                    return;
                }
                ClearRun();
                Status = $"Processing {name} ...";
                using RunOutcome outcome = await RunWithDialogAsync(choice.Request);
                Progress = 100;
                // The core builds each stream's series, in table order, on one shared time axis.
                foreach (StreamResult result in outcome.Results) Plot.AddStream(result);
                Plot.Title = BaseName(name);
                // The recording, as a template is captured from it, named in the Plot File selector.
                SetSources(new[] { new ProcessedSource(choice.Request.SourceId, path, choice.TimeChannelIndex, choice.Request.SwapBytes, choice.Rows) });
                Plot.SetSourceLabel(choice.Request.SourceId, BaseName(name));
                Status = Describe(name, outcome);
                Log.Success("Processing complete — results plotted from memory.");
            }
            catch (OperationCanceledException)
            {
                ClearRun();
                Status = $"{name}: processing was cancelled.";
            }
            catch (RecordingException e)
            {
                ClearRun();
                Status = e.Message;
                // A run with nothing processed has logged the core's own reasons already.
                if (e is not NothingProcessedException) Log.Error(e.Message);
            }
            catch (Exception e) when (e is FileNotFoundException or InvalidOperationException)
            {
                // The native core is missing or does not match this build.
                ClearRun();
                Status = "The processing core could not be loaded: " + e.Message;
                Log.Error(Status);
            }
            finally
            {
                IsBusy = false;
            }
        }

        void IRunHost.SetStatus(string status) => Status = status;

        void IRunHost.SetRunningFile(string name) => _runningFile = name;

        Task<RunOutcome> IRunHost.RunWithDialogAsync(RunRequest request) => RunWithDialogAsync(request);

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
            int dot = fileName.IndexOf('.');
            return dot < 0 ? fileName : fileName[..dot];
        }

        /// <summary>A new run, or one that failed: the plot is cleared, and the recordings it held.</summary>
        private void ClearRun()
        {
            Plot.Clear();
            SetSources(Array.Empty<ProcessedSource>());
        }

        /// <summary>The status line after a run: one Frame Sync Lock stream as the walking skeleton described it, else how
        /// many of each mode were processed; then any error the core reported for a stream that was not.</summary>
        public static string Describe(string name, RunOutcome outcome)
        {
            IReadOnlyList<ProcessedStream> streams = outcome.Streams;
            string text;
            if (streams.Count == 1 && streams[0].Mode == StreamMode.FrameSyncLock)
            {
                ProcessedStream s = streams[0];
                double mean = s.LockPercent.Length == 0 ? 0 : s.LockPercent.Average();
                text = $"{name}: {s.Label} (channel {s.PcmChannelId}), {s.LockPercent.Length} samples, mean lock {mean:F1} %";
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
