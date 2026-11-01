using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Threading;
using TmDq.App.Plot;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.App.Templates;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// Save as Template and Apply Template to Files in the main window (plan step 4.7, US1.1): the Qt main view's and
    /// <c>BatchController</c>'s flow and messages, a batch over copies of the PRN sample, its per-file export held to the
    /// Qt application's CSV, and the windows.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class TemplateBatchTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-batch-" + Guid.NewGuid().ToString("N"));

        public TemplateBatchTests()
        {
            NativeCore.EnsureLoaded();
            Directory.CreateDirectory(_dir);
        }

        public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);

        private static string Fixture(string name) => Path.Combine(Repo.Root, "dotnet", "TmDq.App.Tests", "Templates", name);

        private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

        private static string StepCal => Repo.Recording("step_cal, example.ch10");

        private static ProcessingTemplate PrnTemplate()
        {
            Assert.Equal(TemplateLoad.Loaded, TemplateFile.Read(Fixture("prn-sample.json"), out ProcessingTemplate? t, out _));
            return t!;
        }

        /// <summary>A copy of the PRN sample under <paramref name="name"/>.</summary>
        private string Copy(string name)
        {
            string path = Path.Combine(_dir, name);
            File.Copy(Prn, path);
            return path;
        }

        private sealed class FakeTemplateFiles : ITemplateFilePicker
        {
            public string? Template, SavePath, Folder;
            public IReadOnlyList<string> Recordings = Array.Empty<string>();
            public readonly List<(string What, string? Folder)> Asked = new();

            public Task<string?> OpenTemplateAsync(string? folder) { Asked.Add(("template", folder)); return Task.FromResult(Template); }
            public Task<string?> SaveTemplateAsync(string? folder) { Asked.Add(("save", folder)); return Task.FromResult(SavePath); }
            public Task<IReadOnlyList<string>> OpenRecordingsAsync(string? folder) { Asked.Add(("recordings", folder)); return Task.FromResult(Recordings); }
            public Task<string?> PickFolderAsync(string? folder) { Asked.Add(("folder", folder)); return Task.FromResult(Folder); }
        }

        private sealed class FakeTemplateDialogs : ITemplateDialogs
        {
            public BatchApplyViewModel? Shown;
            public Action<BatchApplyViewModel>? OnConfirm;
            public IReadOnlyList<string>? Choices;
            public int? Choice;

            public Task ConfirmBatchAsync(BatchApplyViewModel vm)
            {
                Shown = vm;
                OnConfirm?.Invoke(vm);
                return Task.CompletedTask;
            }

            public Task<int?> ChooseSourceAsync(IReadOnlyList<string> choices)
            {
                Choices = choices;
                return Task.FromResult(Choice);
            }
        }

        private sealed class FakeImages : IPlotImageExporter
        {
            public readonly List<(string Path, LockAxisView View, int Source)> Asked = new();
            public PlotViewModel? Plot;

            public Task<(string Path, ImageFormat Format)?> ExportAsync(string path)
            {
                Asked.Add((path, Plot!.LockAxisView, Plot.VisibleSource));
                return Task.FromResult<(string, ImageFormat)?>(ImageFiles.Resolve(path));
            }
        }

        private sealed class CancelFirst : IProcessingDialog
        {
            public int Shown;

            public void Show(ProcessingViewModel vm)
            {
                if (Shown++ == 0) vm.CancelCommand.Execute(null);
            }

            public void Close()
            {
            }
        }

        private static string[] Lines(MainWindowViewModel vm, int skip) => vm.Log.Entries.Skip(skip).Select(e => e.Level + " " + e.Text).ToArray();

        // ---- the batch ------------------------------------------------------------------------------------------------------

        [Fact]
        public async Task ABatchRunsEachFileInTurnKeepsEachAndExportsEachAsTheQtApplicationDoes()
        {
            string one = Copy("flight 1.ch10"), two = Copy("flight 2.ch10"), missing = Path.Combine(_dir, "gone.ch10"), output = Path.Combine(_dir, "out");
            Directory.CreateDirectory(output);
            var images = new FakeImages();
            var vm = new MainWindowViewModel(imageExporter: images, appRoot: Repo.Root);
            images.Plot = vm.Plot;
            vm.Plot.SetLockAxisView(LockAxisView.MissedFrames);   // a view to be put back
            int before = vm.Log.Entries.Count;
            await vm.RunBatchAsync(PrnTemplate(), new[] { one, missing, two, StepCal }, output, reuseAppearance: false);

            string[] lines = Lines(vm, before);
            string[] expected =
            {
                "Success Processing 4 file(s)...",
                "Warning Skipped missing batch file: " + missing,
                "Warning Skipped (channels no longer match template): step_cal, example.ch10",
                "Success Exported: " + Path.Combine(output, "flight 1.csv"),
                "Success Plot exported as PNG to " + Path.Combine(output, "flight 1_framesync_lock.png"),
                "Success Plot exported as PNG to " + Path.Combine(output, "flight 1_missed_frames.png"),
                "Success Exported: " + Path.Combine(output, "flight 2.csv"),
                "Success Plot exported as PNG to " + Path.Combine(output, "flight 2_framesync_lock.png"),
                "Success Plot exported as PNG to " + Path.Combine(output, "flight 2_missed_frames.png"),
                "Success Batch complete: 2 processed, 2 skipped. Output written to " + output + ".",
            };
            Assert.Equal(expected, lines.Where(l => expected.Contains(l)));   // in this order, among the runs' own lines
            Assert.Equal("Batch complete: 2 processed, 2 skipped. Output written to " + output + ".", vm.Status);

            // Each image is its own recording, in each metric; the view is put back after.
            Assert.Equal(new[] { (0, LockAxisView.LockPercent), (0, LockAxisView.MissedFrames), (1, LockAxisView.LockPercent), (1, LockAxisView.MissedFrames) },
                         images.Asked.Select(a => (a.Source, a.View)));
            Assert.Equal(LockAxisView.LockPercent, vm.Plot.LockAxisView);   // a batch starts a fresh plot, and puts its view back

            // Both kept, named for Plot File, the plot on the first.
            Assert.Equal(new[] { (0, "flight 1"), (1, "flight 2") }, vm.Plot.SourceList());
            Assert.Equal((0, "flight 1"), (vm.Plot.VisibleSource, vm.Plot.Title));
            Assert.Equal(new[] { (0, one), (1, two) }, vm.Sources.Select(s => (s.SourceId, s.FilePath)));
            Assert.False(vm.IsBusy);

            // The first file's CSV is the Qt application's export of these settings, byte for byte (4.5's oracle); the
            // second's columns are qualified by its recording.
            byte[] csv = File.ReadAllBytes(Path.Combine(output, "flight 1.csv"));
            Assert.Equal((9479, "1be395481250be54acbd94c95a093be75b166e7e5616abf94895b622a91e305a"),
                         (csv.Length, Convert.ToHexString(SHA256.HashData(csv)).ToLowerInvariant()));
            Assert.StartsWith("Time (DOY:HH:MM:SS.mmm),S1| CH-01 PRN 15 5M Lock (%)", File.ReadAllLines(Path.Combine(output, "flight 2.csv"))[0]);
        }

        [Fact]
        public async Task ATemplatesNamesAndColoursGoOnEveryFileWhenAskedAndAFailedFileIsSaidSo()
        {
            string one = Copy("flight 1.ch10"), two = Copy("flight 2.ch10");
            var vm = new MainWindowViewModel(processingDialog: new CancelFirst(), appRoot: Repo.Root);
            int before = vm.Log.Entries.Count;
            await vm.RunBatchAsync(PrnTemplate(), new[] { one, two }, null, reuseAppearance: true);
            string[] lines = Lines(vm, before);
            Assert.Contains("Error Batch file failed to process: flight 1", lines);
            Assert.Equal("Success Batch complete: 1 processed, 1 skipped.", lines[^1]);
            Assert.Equal(new[] { (1, "flight 2") }, vm.Plot.SourceList());
            Assert.Equal("flight 2", vm.Plot.Title);
            Assert.Equal(-1, vm.Plot.VisibleSource);   // one file: nothing to choose
            Assert.Equal(new[] { "Fifteen", "Fifteen", "Eleven", "Eleven" }, vm.Plot.Series.Select(s => s.Name));
            Assert.Equal(new[] { Color.Parse("#ff6a0dad"), Color.Parse("#ff6a0dad"), Color.Parse("#ff1f77b4"), Color.Parse("#ff1f77b4") },
                         vm.Plot.Series.Select(s => s.Color));

            // Without reuse, the series keep their own names; no folder, no export.
            var plain = new MainWindowViewModel(appRoot: Repo.Root);
            await plain.RunBatchAsync(PrnTemplate(), new[] { one }, null, reuseAppearance: false);
            Assert.Equal(new[] { "CH-01 PRN 15 5M", "CH-01 PRN 15 5M", "CH-03 PRN 11 1M", "CH-03 PRN 11 1M" }, plain.Plot.Series.Select(s => s.Name));
            Assert.DoesNotContain(plain.Log.Entries, e => e.Text.StartsWith("Exported", StringComparison.Ordinal));
            Assert.Empty(Directory.GetFiles(_dir, "*.csv"));
            await plain.RunBatchAsync(PrnTemplate(), Array.Empty<string>(), null, false);   // nothing to run: nothing happens
        }

        // ---- Apply Template to Files --------------------------------------------------------------------------------------

        [Fact]
        public void ATemplateThatCannotBeUsedIsAnErrorInTheQtWords()
        {
            var vm = new MainWindowViewModel();
            string Write(string name, string text)
            {
                string path = Path.Combine(_dir, name);
                File.WriteAllText(path, text);
                return path;
            }
            string missing = Path.Combine(_dir, "nope.json");
            Assert.Null(vm.ReadTemplate(missing));
            Assert.Equal("Could not open template file: " + missing, vm.Log.Entries[^1].Text);
            Assert.Null(vm.ReadTemplate(Write("bad.json", "{ not json")));
            Assert.StartsWith("Template file is not valid JSON: ", vm.Log.Entries[^1].Text);
            Assert.Null(vm.ReadTemplate(Write("new.json", "{\"schemaVersion\": 9, \"entries\": []}")));
            Assert.Equal("Template file uses an unsupported schema version.", vm.Log.Entries[^1].Text);
            Assert.Null(vm.ReadTemplate(Write("array.json", "[]")));
            Assert.Equal("Template file is not a valid template (unrecognized format).", vm.Log.Entries[^1].Text);
            Assert.Null(vm.ReadTemplate(Fixture("empty.json")));
            Assert.Equal("Template has no stream entries.", vm.Log.Entries[^1].Text);
            Assert.All(vm.Log.Entries.TakeLast(5), e => Assert.Equal(Logging.LogLevel.Error, e.Level));
            Assert.NotNull(vm.ReadTemplate(Fixture("prn-sample.json")));
        }

        [Fact]
        public async Task ApplyTemplateReadsTheTemplateChecksTheFilesAndRunsTheOnesThatMatchOnceConfirmed()
        {
            string one = Copy("flight 1.ch10");
            var files = new FakeTemplateFiles { Template = Fixture("prn-sample.json"), Recordings = new[] { one, StepCal } };
            var dialogs = new FakeTemplateDialogs();
            var vm = new MainWindowViewModel(templateFiles: files, templateDialogs: dialogs, appRoot: Repo.Root);
            Assert.True(vm.ApplyTemplateCommand.CanExecute(null));
            Assert.False(new MainWindowViewModel(templateFiles: files).ApplyTemplateCommand.CanExecute(null));
            Assert.False(new MainWindowViewModel(templateDialogs: dialogs).ApplyTemplateCommand.CanExecute(null));

            // Cancelled at the confirmation: nothing runs.
            int before = vm.Log.Entries.Count;
            await vm.ApplyTemplateCommand.ExecuteAsync(null);
            BatchApplyViewModel shown = dialogs.Shown!;
            Assert.Equal("1 of 2 selected file(s) match the template and will be processed.", shown.Summary);
            Assert.Equal(new[] { "flight 1.ch10", "step_cal, example.ch10  —  missing channel(s): 13, 14, 15, 16; extra channel(s): 26" },
                         shown.Rows.Select(r => r.Text));
            Assert.Equal(Path.GetDirectoryName(Fixture("prn-sample.json")), shown.OutputFolder);   // where the template came from
            Assert.Equal(new[] { ("template", (string?)null), ("recordings", Path.GetDirectoryName(Fixture("prn-sample.json"))) }, files.Asked);
            Assert.Equal(before, vm.Log.Entries.Count);

            // Confirmed: only the files that match run.
            dialogs.OnConfirm = c => c.ProcessCommand.Execute(null);
            await vm.ApplyTemplateCommand.ExecuteAsync(null);
            Assert.Contains("Success Processing 1 file(s)...", Lines(vm, before));
            Assert.Equal(new[] { (0, "flight 1") }, vm.Plot.SourceList());

            // A template that cannot be used stops there; no files are asked for.
            files.Asked.Clear();
            files.Template = Fixture("empty.json");
            await vm.ApplyTemplateCommand.ExecuteAsync(null);
            Assert.Equal(new[] { "template" }, files.Asked.Select(a => a.What));
            files.Template = null;   // cancelled
            await vm.ApplyTemplateCommand.ExecuteAsync(null);
            files.Template = Fixture("prn-sample.json");
            files.Recordings = Array.Empty<string>();   // no files chosen
            dialogs.Shown = null;
            await vm.ApplyTemplateCommand.ExecuteAsync(null);
            Assert.Null(dialogs.Shown);
        }

        // ---- Save as Template -----------------------------------------------------------------------------------------------

        [Fact]
        public async Task SaveAsTemplateCapturesTheRecordingJustProcessed()
        {
            var files = new FakeTemplateFiles { SavePath = Path.Combine(_dir, "mine.json") };
            var vm = new MainWindowViewModel(templateFiles: files, appRoot: Repo.Root);
            Assert.False(vm.SaveTemplateCommand.CanExecute(null));   // nothing processed yet
            await vm.SaveTemplateCommand.ExecuteAsync(null);         // run anyway: it says so
            Assert.Equal((Logging.LogLevel.Warning, "Nothing to save as a template -- no sources have finished processing yet."),
                         (vm.Log.Entries[^1].Level, vm.Log.Entries[^1].Text));

            int told = 0;
            vm.SaveTemplateCommand.CanExecuteChanged += (_, _) => told++;
            await vm.LoadAsync(Prn);   // the first channel with the defaults, no dialog
            Assert.True(vm.SaveTemplateCommand.CanExecute(null));
            Assert.True(told > 0);
            ProcessedSource source = Assert.Single(vm.Sources);
            Assert.Equal((0, Prn, 0, true), (source.SourceId, source.FilePath, source.TimeChannelIndex, source.SwapBytes));
            Assert.Equal(new[] { 13, 14, 15, 16 }, source.Rows.Select(r => r.PcmChannelId));   // every row, processed or not
            Assert.Equal(new[] { (0, "prn_testfile, sample") }, vm.Plot.SourceList());

            vm.Plot.RenameSeries(vm.Plot.Series[0].Id, "Renamed");
            await vm.SaveTemplateCommand.ExecuteAsync(null);
            Assert.Equal("Template saved: mine.json", vm.Log.Entries[^1].Text);
            Assert.Equal(_dir, vm.LastFolder);
            Assert.Equal(TemplateLoad.Loaded, TemplateFile.Read(files.SavePath, out ProcessingTemplate? t, out _));
            Assert.Equal(("prn_testfile, sample", 4), (t!.Name, t.Entries.Count));
            Assert.Equal(new[] { "Renamed", "Renamed" }, t.Entries[0].Appearance.Select(a => a.Name));
            Assert.Empty(t.Entries[1].Appearance);

            // Cancelled: nothing written; unwritable: said so.
            File.Delete(files.SavePath);
            files.SavePath = null;
            await vm.SaveTemplateCommand.ExecuteAsync(null);
            Assert.False(File.Exists(Path.Combine(_dir, "mine.json")));
            string nowhere = Path.Combine(_dir, "missing", "t.json");
            Assert.False(vm.SaveTemplate(vm.Sources[0], nowhere));
            Assert.Equal((Logging.LogLevel.Error, "Could not write template file: " + nowhere), (vm.Log.Entries[^1].Level, vm.Log.Entries[^1].Text));
        }

        [Fact]
        public async Task WithSeveralRecordingsSaveAsTemplateAsksWhichAndAnOpenForgetsThem()
        {
            string one = Copy("flight 1.ch10"), two = Copy("flight 2.ch10");
            var files = new FakeTemplateFiles { SavePath = Path.Combine(_dir, "chosen.json") };
            var dialogs = new FakeTemplateDialogs { Choice = 1 };
            var vm = new MainWindowViewModel(templateFiles: files, templateDialogs: dialogs, appRoot: Repo.Root);
            await vm.RunBatchAsync(PrnTemplate(), new[] { one, two }, null, false);
            await vm.SaveTemplateCommand.ExecuteAsync(null);
            Assert.Equal(new[] { "flight 1.ch10 (source 0)", "flight 2.ch10 (source 1)" }, dialogs.Choices);
            Assert.Equal(TemplateLoad.Loaded, TemplateFile.Read(files.SavePath, out ProcessingTemplate? t, out _));
            Assert.Equal("flight 2", t!.Name);

            dialogs.Choice = null;   // cancelled
            files.Asked.Clear();
            await vm.SaveTemplateCommand.ExecuteAsync(null);
            Assert.Empty(files.Asked);

            await vm.LoadAsync(Prn);   // a new recording: the batch's are gone
            Assert.Single(vm.Sources);
        }

        // ---- the confirmation's state ----------------------------------------------------------------------------------------

        [Fact]
        public async Task TheConfirmationSaysHowManyMatchAndNeedsAFolderOnlyToExport()
        {
            var checks = new[] { new FileCheck(@"C:\a\one.ch10", true, ""), new FileCheck(@"C:\a\two.ch10", false, "extra channel(s): 9") };
            var files = new FakeTemplateFiles { Folder = @"D:\out" };
            var vm = new BatchApplyViewModel(checks, @"C:\a", files);
            Assert.Equal("Batch Process Files", BatchApplyViewModel.Title);
            Assert.Equal(("1 of 2 selected file(s) match the template and will be processed.", 1), (vm.Summary, vm.MatchCount));
            Assert.Equal(new[] { @"C:\a\one.ch10" }, vm.Matched);
            Assert.Equal(("✓", "one.ch10", @"C:\a\one.ch10"), (vm.Rows[0].Glyph, vm.Rows[0].Text, vm.Rows[0].ToolTip));
            Assert.Equal(("✗", "two.ch10  —  extra channel(s): 9"), (vm.Rows[1].Glyph, vm.Rows[1].Text));
            Assert.Equal((Colors.Green, Color.Parse("#CC0000")), (((ISolidColorBrush)vm.Rows[0].GlyphBrush).Color, ((ISolidColorBrush)vm.Rows[1].GlyphBrush).Color));
            Assert.Equal((false, true, @"C:\a"), (vm.ExportPerFile, vm.ReuseAppearance, vm.OutputFolder));
            Assert.True(vm.CanProcess);
            Assert.False(vm.BrowseCommand.CanExecute(null));

            int told = 0;
            vm.ProcessCommand.CanExecuteChanged += (_, _) => told++;
            vm.ExportPerFile = true;
            Assert.True(vm.BrowseCommand.CanExecute(null));
            vm.OutputFolder = "  ";
            Assert.False(vm.CanProcess);
            Assert.False(vm.ProcessCommand.CanExecute(null));
            Assert.True(told > 0);
            await vm.BrowseCommand.ExecuteAsync(null);
            Assert.Equal(("folder", (string?)"  "), files.Asked[^1]);
            Assert.Equal(@"D:\out", vm.OutputFolder);
            Assert.True(vm.CanProcess);
            files.Folder = null;   // cancelled
            await vm.BrowseCommand.ExecuteAsync(null);
            Assert.Equal(@"D:\out", vm.OutputFolder);

            // No file matches: nothing to process.
            Assert.False(new BatchApplyViewModel(new[] { checks[1] }, "", null).CanProcess);
            int closed = 0;
            vm.CloseRequested += (_, _) => closed++;
            vm.CancelCommand.Execute(null);
            Assert.Equal((1, false), (closed, vm.Accepted));
            vm.ProcessCommand.Execute(null);
            Assert.Equal((2, true), (closed, vm.Accepted));
        }

        // ---- the windows ----------------------------------------------------------------------------------------------------

        [AvaloniaFact]
        public void TheConfirmationWindowIsLaidOutAsQtsAndClosesOnProcessOrCancel()
        {
            var owner = new Window { Width = 400, Height = 300 };
            owner.Show();
            var vm = new BatchApplyViewModel(new[] { new FileCheck(@"C:\a\one.ch10", true, "") }, @"C:\a", new FakeTemplateFiles());
            Task shown = new WindowTemplateDialogs(owner).ConfirmBatchAsync(vm);
            Dispatcher.UIThread.RunJobs();
            var window = Assert.IsType<BatchApplyWindow>(Assert.Single(owner.OwnedWindows));
            Assert.True(window.IsDialog);
            Assert.Equal("Batch Process Files", window.Title);
            Assert.Equal(vm.Summary, window.FindControl<TextBlock>("Summary")!.Text);
            Assert.Equal("Also export a CSV + plot images (Frame Sync Lock and Missed Frames) per file", window.FindControl<CheckBox>("ExportBox")!.Content);
            Assert.Equal("Reuse the template's saved series names and colors", window.FindControl<CheckBox>("ReuseBox")!.Content);
            Assert.Equal(true, window.FindControl<CheckBox>("ReuseBox")!.IsChecked);
            TextBox folder = window.FindControl<TextBox>("FolderBox")!;
            Assert.Equal((@"C:\a", false), (folder.Text, folder.IsEnabled));
            Assert.False(window.FindControl<Button>("BrowseButton")!.IsEffectivelyEnabled);
            window.FindControl<CheckBox>("ExportBox")!.IsChecked = true;
            Dispatcher.UIThread.RunJobs();
            Assert.True(vm.ExportPerFile);
            Assert.True(folder.IsEnabled);
            Assert.True(window.FindControl<Button>("BrowseButton")!.IsEffectivelyEnabled);
            Button process = window.FindControl<Button>("ProcessButton")!;
            Assert.True(process.IsDefault);
            Assert.True(window.FindControl<Button>("CancelButton")!.IsCancel);
            Assert.True(process.IsEffectivelyEnabled);
            vm.ProcessCommand.Execute(null);
            Dispatcher.UIThread.RunJobs();
            Assert.True(shown.IsCompleted, "the confirmation is still open.");

            var cancelled = new BatchApplyViewModel(Array.Empty<FileCheck>(), "", null);
            shown = new WindowTemplateDialogs(owner).ConfirmBatchAsync(cancelled);
            Dispatcher.UIThread.RunJobs();
            Assert.False(Assert.IsType<BatchApplyWindow>(Assert.Single(owner.OwnedWindows)).FindControl<Button>("ProcessButton")!.IsEffectivelyEnabled);
            cancelled.CancelCommand.Execute(null);
            Dispatcher.UIThread.RunJobs();
            Assert.True(shown.IsCompleted, "the confirmation is still open after Cancel.");
            owner.Close();
        }

        [AvaloniaFact]
        public async Task TheChoiceOfRecordingGivesItsIndexOrNothing()
        {
            var owner = new Window { Width = 400, Height = 300 };
            owner.Show();
            var dialogs = new WindowTemplateDialogs(owner);
            Task<int?> chosen = dialogs.ChooseSourceAsync(new[] { "a (source 0)", "b (source 1)" });
            Dispatcher.UIThread.RunJobs();
            var window = Assert.IsType<ChoiceWindow>(Assert.Single(owner.OwnedWindows));
            Assert.Equal("Save as Template", window.Title);
            Assert.Contains(window.GetVisualDescendantsOf<TextBlock>(), t => t.Text == "Capture the per-stream settings from which source?");
            Assert.Equal(0, window.Choices.SelectedIndex);
            window.Choices.SelectedIndex = 1;
            window.OkButton.RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Assert.True(chosen.IsCompleted);
            Assert.Equal(1, await chosen);

            chosen = dialogs.ChooseSourceAsync(new[] { "a" });
            Dispatcher.UIThread.RunJobs();
            ((ChoiceWindow)owner.OwnedWindows.Single()).CancelButton.RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Assert.Null(await chosen);
            owner.Close();
        }

        [AvaloniaFact]
        public void TheTopBarHasSaveAsTemplateAndApplyTemplate()
        {
            var vm = new MainWindowViewModel(templateFiles: new FakeTemplateFiles(), templateDialogs: new FakeTemplateDialogs());
            var window = new MainWindow { DataContext = vm };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Button open = window.FindControl<Button>("OpenButton")!, save = window.FindControl<Button>("SaveTemplateButton")!,
                   apply = window.FindControl<Button>("ApplyTemplateButton")!;
            Assert.Same(open.Parent, save.Parent);
            Assert.Same(open.Parent, apply.Parent);
            Assert.Equal(("Save as Template...", "Apply Template..."), ((string)save.Content!, (string)apply.Content!));
            Assert.Same(vm.SaveTemplateCommand, save.Command);
            Assert.Same(vm.ApplyTemplateCommand, apply.Command);
            Assert.NotNull(ToolTip.GetTip(save));
            Assert.NotNull(ToolTip.GetTip(apply));
            Assert.False(save.IsEffectivelyEnabled);   // nothing processed yet
            Assert.True(apply.IsEffectivelyEnabled);
            window.Close();
        }
    }

    internal static class VisualSearch
    {
        public static IEnumerable<T> GetVisualDescendantsOf<T>(this Avalonia.Visual v) =>
            Avalonia.VisualTree.VisualExtensions.GetVisualDescendants(v).OfType<T>();
    }
}
