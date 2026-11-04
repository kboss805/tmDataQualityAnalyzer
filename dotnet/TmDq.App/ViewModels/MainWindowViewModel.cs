using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
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
    /// (plan step 4.2).</summary>
    public sealed partial class MainWindowViewModel : ObservableObject
    {
        private readonly IRecordingPicker? _picker;
        private readonly IStreamConfigurator _configurator;
        private readonly IProcessingDialog? _processingDialog;
        private readonly IDataFilePicker? _files;
        private readonly IExportDialog? _exportDialog;
        private readonly IPlotImageExporter? _imageExporter;
        private readonly ITemplateFilePicker? _templateFiles;
        private readonly ITemplateDialogs? _templateDialogs;
        private readonly string _appRoot;
        private readonly PreferencesStore _preferences;
        private readonly List<ProcessedSource> _sources = new();
        private string _runningFile = string.Empty;
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
        /// <param name="files">The CSV to import and Export's Browse dialogs; null leaves Import CSV unavailable.</param>
        /// <param name="exportDialog">The Export dialog; null leaves Export unavailable.</param>
        /// <param name="imageExporter">What writes the plot as an image; null makes an image export fail.</param>
        /// <param name="templateFiles">Save as Template's and Apply Template's file dialogs; null leaves both unavailable.</param>
        /// <param name="templateDialogs">The batch confirmation and the choice of recording; null leaves Apply Template unavailable.</param>
        /// <param name="appRoot">Where settings/ is, for a batch's runs; null is the application's folder.</param>
        /// <param name="preferences">What is remembered between sessions (plan step 4.8); null keeps the defaults, in memory.</param>
        public MainWindowViewModel(IRecordingPicker? picker = null, IStreamConfigurator? configurator = null,
                                   IProcessingDialog? processingDialog = null, TimeProvider? clock = null,
                                   IDataFilePicker? files = null, IExportDialog? exportDialog = null,
                                   IPlotImageExporter? imageExporter = null, ITemplateFilePicker? templateFiles = null,
                                   ITemplateDialogs? templateDialogs = null, string? appRoot = null,
                                   PreferencesStore? preferences = null)
        {
            _preferences = preferences ?? new PreferencesStore(new Preferences());
            _picker = picker;
            _configurator = configurator ?? new FirstChannelRun();
            _processingDialog = processingDialog;
            _files = files;
            _exportDialog = exportDialog;
            _imageExporter = imageExporter;
            _templateFiles = templateFiles;
            _templateDialogs = templateDialogs;
            _appRoot = appRoot ?? AppContext.BaseDirectory;
            Log = new SessionLog(clock);
            OpenCommand = new AsyncRelayCommand(OpenAsync, () => !IsBusy && _picker is not null);
            ImportCsvCommand = new AsyncRelayCommand(PickAndImportAsync, () => !IsBusy && _files is not null);
            ExportCommand = new AsyncRelayCommand(ExportAsync, () => !IsBusy && Plot.HasData && _exportDialog is not null);
            SaveTemplateCommand = new AsyncRelayCommand(SaveTemplateAsync, () => !IsBusy && _sources.Count > 0 && _templateFiles is not null);
            ApplyTemplateCommand = new AsyncRelayCommand(ApplyTemplateAsync, () => !IsBusy && _templateFiles is not null && _templateDialogs is not null);
            OpenRecentCommand = new AsyncRelayCommand<string>(p => OpenPathAsync(p!), p => !IsBusy && p is not null);
            ClearRecentFilesCommand = new RelayCommand(() => SetRecentFiles(Array.Empty<string>()));
            ToggleThemeCommand = new RelayCommand(() => IsDarkTheme = !IsDarkTheme);
            ToggleSidebarCommand = new RelayCommand(() => SidebarVisible = !SidebarVisible);
            // The legend's visibility is remembered between sessions (US4.0, US4.1), as v2.x remembered it.
            Plot.LegendVisible = _preferences.Current.LegendVisible;
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
                if (e.PropertyName == nameof(PlotViewModel.LegendVisible))
                    _preferences.Update(p => p with { LegendVisible = Plot.LegendVisible });
            };
        }

        // ---- the shell (plan step 4.8): what is remembered, recent files, the theme and the sidebar --------------------

        /// <summary>What is remembered between sessions.</summary>
        public Preferences Preferences => _preferences.Current;

        /// <summary>The recordings and CSVs opened last, the newest first (the menu's Recent Files).</summary>
        public IReadOnlyList<string> RecentFiles => _preferences.Current.RecentFiles;

        /// <summary>Opens a recent file, or one dropped on the window. Unavailable while busy.</summary>
        public IAsyncRelayCommand<string> OpenRecentCommand { get; }

        public IRelayCommand ClearRecentFilesCommand { get; }

        private void SetRecentFiles(IReadOnlyList<string> files)
        {
            _preferences.Update(p => p with { RecentFiles = files });
            OnPropertyChanged(nameof(RecentFiles));
        }

        private void AddRecentFile(string path) => SetRecentFiles(Settings.RecentFiles.Add(RecentFiles, Path.GetFullPath(path)));

        /// <summary>
        /// Opens <paramref name="path"/> by its suffix, as v2.x did for Recent Files and a drop: an
        /// exported CSV is imported, anything else opened as a recording. Ignored while busy, as both are.
        /// </summary>
        public Task OpenPathAsync(string path) => Settings.RecentFiles.IsCsv(path) ? ImportCsvAsync(path) : LoadAsync(path);

        /// <summary>The dark theme (v2.x's default too) or the light one (US9.0); the choice is remembered.</summary>
        public bool IsDarkTheme
        {
            get => _preferences.Current.IsDark;
            set
            {
                if (value == IsDarkTheme) return;
                _preferences.Update(p => p with { Theme = value ? Preferences.DarkTheme : Preferences.LightTheme });
                OnPropertyChanged();
                OnPropertyChanged(nameof(ThemeMenuText));
            }
        }

        /// <summary>The menu item that switches the theme, named for where it goes.</summary>
        public string ThemeMenuText => IsDarkTheme ? "Switch to Light Theme" : "Switch to Dark Theme";

        public IRelayCommand ToggleThemeCommand { get; }

        /// <summary>The log sidebar shown (the title bar's toggle, Ctrl+B); the choice is remembered.</summary>
        public bool SidebarVisible
        {
            get => _preferences.Current.SidebarVisible;
            set
            {
                if (value == SidebarVisible) return;
                _preferences.Update(p => p with { SidebarVisible = value });
                OnPropertyChanged();
            }
        }

        public IRelayCommand ToggleSidebarCommand { get; }

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
            get => _preferences.Current.LastFolder;
            private set => _preferences.Update(p => p with { LastFolder = value });
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
                Show(Array.Empty<LockSeries>());
                NativeCore.EnsureLoaded();
                if (await Plot.LoadCsvAsync(path) == CsvLoad.Loaded)
                {
                    AddRecentFile(path);
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
        public async Task ExportAsync(ExportViewModel choice)
        {
            if (!Plot.HasData) return;
            if (choice.ExportData)
            {
                if (Plot.ExportCsv(choice.DataPath)) Log.Success("Data exported to " + choice.DataPath);
                else Log.Error("Error: Failed to export data to " + choice.DataPath);
            }
            if (choice.ExportImage)
            {
                (string Path, ImageFormat Format)? written = _imageExporter is null ? null : await _imageExporter.ExportAsync(choice.ImagePath);
                if (written is { } w) Log.Success($"Plot exported as {w.Format.ToString().ToUpperInvariant()} to {w.Path}");
                else Log.Error("Error: Failed to export plot to " + ImageFiles.Resolve(choice.ImagePath).Path);
            }
            if (choice.ExportLog)
            {
                if (WriteLog(choice.LogPath)) Log.Success("Log exported to " + choice.LogPath);
                else Log.Error("Error: Failed to export log to " + choice.LogPath);
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
            int processed = 0, skipped = 0, nextSourceId = 0;
            var sources = new List<ProcessedSource>();
            try
            {
                Show(Array.Empty<LockSeries>());
                Log.Success(string.Create(CultureInfo.InvariantCulture, $"Processing {files.Count} file(s)..."));
                foreach (string path in files)
                {
                    if (!File.Exists(path))
                    {
                        Log.Warning("Skipped missing batch file: " + path);
                        skipped++;
                        continue;
                    }
                    string name = Path.GetFileName(path), baseName = BaseName(name);
                    int sourceId = nextSourceId++;
                    try
                    {
                        Status = $"Opening {name} ...";
                        Log.Add("Opening: " + name);
                        Ch10File file = await StreamsRun.OpenAsync(path);
                        // Checked again at its turn: the file may have changed since it was chosen.
                        if (!TemplateBatch.Match(template, file.PcmChannels.Select(c => c.Id)).Ok)
                        {
                            Log.Warning("Skipped (channels no longer match template): " + name);
                            skipped++;
                            continue;
                        }
                        Status = $"Processing {name} ...";
                        _runningFile = name;
                        using RunOutcome outcome = await RunWithDialogAsync(TemplateBatch.ToRunRequest(template, path, file, sourceId, _appRoot));
                        foreach (StreamResult result in outcome.Results) Plot.AddStream(result);
                        processed++;
                        sources.Add(new ProcessedSource(sourceId, path, template.TimeChannelIndex, template.SwapBytes,
                                                        template.Entries.Select(e => e.Config).ToList()));
                        Plot.SetSourceLabel(sourceId, baseName);
                        if (reuseAppearance) TemplateBatch.ApplyAppearance(template, sourceId, Plot);
                    }
                    catch (Exception e) when (e is OperationCanceledException or RecordingException)
                    {
                        skipped++;
                        Log.Error("Batch file failed to process: " + baseName);
                    }
                }
                SetSources(sources);

                if (outputFolder is not null) await ExportEachSourceAsync(outputFolder);

                // The plot opens on the first file; "All files (overlaid)" is in the Plot File selector.
                if (sources.Count > 1) Plot.SetVisibleSource(sources[0].SourceId);
                if (sources.Count > 0) Plot.Title = BaseName(Path.GetFileName(sources[0].FilePath));
                Status = string.Create(CultureInfo.InvariantCulture, $"Batch complete: {processed} processed, {skipped} skipped.") +
                         (outputFolder is null ? "" : " Output written to " + outputFolder + ".");
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

        /// <summary>
        /// Each recording on its own, as v2.x's batch wrote it: its CSV (every series of that recording), then
        /// with frame-sync data an image of the lock % and one of the missed frames, else one image; the view put back after.
        /// </summary>
        private async Task ExportEachSourceAsync(string folder)
        {
            LockAxisView original = Plot.LockAxisView;
            bool frameSync = Plot.HasLockSeries || Plot.HasMissedFramesSeries;
            foreach (ProcessedSource source in _sources)
            {
                string baseName = BaseName(Path.GetFileName(source.FilePath));
                Plot.SetVisibleSource(source.SourceId);
                string csv = Path.Combine(folder, baseName + ".csv");
                if (Plot.ExportCsv(csv, source.SourceId)) Log.Success("Exported: " + csv);
                else Log.Error("Failed to export CSV: " + csv);

                if (_imageExporter is null) continue;
                if (frameSync)
                {
                    Plot.SetLockAxisView(LockAxisView.LockPercent);
                    await ExportImageAsync(Path.Combine(folder, baseName + "_framesync_lock.png"));
                    Plot.SetLockAxisView(LockAxisView.MissedFrames);
                    await ExportImageAsync(Path.Combine(folder, baseName + "_missed_frames.png"));
                }
                else
                {
                    await ExportImageAsync(Path.Combine(folder, baseName + ".png"));
                }
            }
            Plot.SetLockAxisView(original);
        }

        private async Task ExportImageAsync(string path)
        {
            (string Path, ImageFormat Format)? written = await _imageExporter!.ExportAsync(path);
            if (written is { } w) Log.Success($"Plot exported as {w.Format.ToString().ToUpperInvariant()} to {w.Path}");
            else Log.Error("Error: Failed to export plot to " + ImageFiles.Resolve(path).Path);
        }

        /// <summary>The log as v2.14.2's export wrote it (US6.2): each line as the sidebar shows it, CR LF line ends,
        /// UTF-8 without a byte-order mark, ending with a line end.</summary>
        private bool WriteLog(string path)
        {
            string text = Log.PlainText.Replace("\n", "\r\n");
            if (!text.EndsWith("\r\n", StringComparison.Ordinal)) text += "\r\n";
            try
            {
                File.WriteAllText(path, text, new UTF8Encoding(false));
                return true;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
            {
                return false;
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
                AddRecentFile(path);
                Log.Add("  File size: " + SizeText(new FileInfo(path).Length));
                Log.Add("  Time channels: " + CountText(file.TimeChannels.Count));
                Log.Add("  PCM channels: " + CountText(file.PcmChannels.Count));
                StreamsChoice? choice = await _configurator.ConfigureAsync(path, file);
                if (choice is null)
                {
                    Status = $"{name}: not processed (Configure Streams was cancelled).";
                    return;
                }
                Show(Array.Empty<LockSeries>());
                Status = $"Processing {name} ...";
                using RunOutcome outcome = await RunWithDialogAsync(choice.Request);
                Progress = 100;
                Show(outcome.Streams.Where(s => s.Mode == StreamMode.FrameSyncLock).Select(LockSeries.Of).ToList());
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

        private void Show(IReadOnlyList<LockSeries> series)
        {
            // An empty run summary is a new run, or one that failed: the plot is cleared with it, and the recordings it held.
            if (series.Count == 0)
            {
                Plot.Clear();
                SetSources(Array.Empty<ProcessedSource>());
            }
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
