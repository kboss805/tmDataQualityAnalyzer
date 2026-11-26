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
using Avalonia.VisualTree;
using TmDq.App.Models.Export;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.App.Models.Templates;
using TmDq.App.Platform;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Dialogs;
using TmDq.App.ViewModels.Plot;
using TmDq.App.Views.Converters;
using TmDq.App.Views.Dialogs;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// Save as Template and Apply Template to Files in the main window (plan step 4.7, US1.1): v2.14.2's main window's
/// and batch's flow and messages, a batch over copies of the PRN sample, its per-file export held to the
/// CSV v2.14.2 wrote, and the windows.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class TemplateBatchTests : IDisposable, IClassFixture<ShortRecordings>
{
    private readonly string _dir = TempFiles.NewPath("batch");
    private readonly ShortRecordings _short;

    public TemplateBatchTests(ShortRecordings recordings)
    {
        NativeCore.EnsureLoaded();
        Directory.CreateDirectory(_dir);
        _short = recordings;
    }

    public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);

    private static string Fixture(string name) => Path.Combine(Repo.Root, "tests", "TmDq.App.Tests", "Templates", name);

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

    /// <summary>A copy of the short PRN recording (<see cref="ShortRecordings"/>) under <paramref name="name"/>: it processes as the sample does, in a fraction of its time.</summary>
    private string CopyShort(string name)
    {
        string path = Path.Combine(_dir, name);
        File.Copy(_short.First, path);
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

    /// <summary>The progress dialog, noting the main window's status line each time it is shown.</summary>
    private sealed class StatusAtShow : IProcessingDialog
    {
        public MainWindowViewModel? Main;
        public readonly List<string> Seen = new();
        public readonly List<Models.Logging.LogLevel> Levels = new();

        public void Show(ProcessingViewModel vm)
        {
            Seen.Add(Main!.Status);
            Levels.Add(Main.StatusLevel);
        }

        public void Close()
        {
        }
    }

    private static string[] Lines(MainWindowViewModel vm, int skip) => vm.Log.Entries.Skip(skip).Select(e => e.Level + " " + e.Text).ToArray();

    // ---- the batch ------------------------------------------------------------------------------------------------------

    [Fact]
    public async Task ABatchRunsEachFileInTurnKeepsEachAndExportsEachAsV214Did()
    {
        string one = Copy("flight 1.ch10"), two = Copy("flight 2.ch10"), missing = Path.Combine(_dir, "gone.ch10"), output = Path.Combine(_dir, "out");
        Directory.CreateDirectory(output);
        var images = new FakeImages();
        var vm = new MainWindowViewModel(new MainWindowServices { ImageExporter = images, AppRoot = Repo.Root });
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
        Assert.Equal("Batch complete: 2 processed, 2 skipped.", vm.Status);
        Assert.Equal(Models.Logging.LogLevel.Warning, vm.StatusLevel);
        Assert.Equal("Batch complete: 2 processed, 2 skipped. Output written to " + output + ".", vm.Log.Entries[^1].Text);

        // Each image is its own recording, in each metric; the view is put back after.
        Assert.Equal(new[] { (0, LockAxisView.LockPercent), (0, LockAxisView.MissedFrames), (1, LockAxisView.LockPercent), (1, LockAxisView.MissedFrames) },
                     images.Asked.Select(a => (a.Source, a.View)));
        Assert.Equal(LockAxisView.LockPercent, vm.Plot.LockAxisView);   // a batch starts a fresh plot, and puts its view back

        // Both kept, named for Plot File, the plot on the first.
        Assert.Equal(new[] { (0, "flight 1"), (1, "flight 2") }, vm.Plot.SourceList());
        Assert.Equal((0, "flight 1"), (vm.Plot.VisibleSource, vm.Plot.Title));
        Assert.Equal(new[] { (0, one), (1, two) }, vm.Sources.Select(s => (s.SourceId, s.FilePath)));
        Assert.False(vm.IsBusy);

        // The first file's CSV is the export of these settings (4.5's oracle, re-recorded for the sample grid: see
        // ExportImportTests.ExportsAsV214WroteThem); the second's columns are qualified by its recording.
        byte[] csv = File.ReadAllBytes(Path.Combine(output, "flight 1.csv"));
        Assert.Equal((7081, "85efad9242e0bd40a2b7388e8a43ae6de8bc42de7f7af421baf5d15a539615ba"),
                     (csv.Length, Convert.ToHexString(SHA256.HashData(csv)).ToLowerInvariant()));
        Assert.StartsWith("Time (DOY:HH:MM:SS.mmm),S1| CH-01 PRN 15 5M Lock (%)", File.ReadAllLines(Path.Combine(output, "flight 2.csv"))[0]);
    }

    [Fact]
    public async Task WithNoImageWriterABatchExportsEachCsvAloneAndItsStatusNamesTheFileRunning()
    {
        string one = Copy("flight 1.ch10"), two = Copy("flight 2.ch10"), output = Path.Combine(_dir, "out");
        Directory.CreateDirectory(output);
        var dialog = new StatusAtShow();
        var vm = new MainWindowViewModel(new MainWindowServices { ProcessingDialog = dialog, AppRoot = Repo.Root });
        dialog.Main = vm;
        int before = vm.Log.Entries.Count;
        await vm.RunBatchAsync(PrnTemplate(), new[] { one, two }, output, reuseAppearance: false);

        // Each file's run is shown under the status naming it.
        Assert.Equal(new[] { "Processing flight 1.ch10...", "Processing flight 2.ch10..." }, dialog.Seen);
        Assert.All(dialog.Levels, l => Assert.Equal(Models.Logging.LogLevel.Info, l));   // under way: no mark

        // Each CSV, and no image tried: no line about a plot image, and no image on disk.
        string[] lines = Lines(vm, before);
        Assert.Contains("Success Exported: " + Path.Combine(output, "flight 1.csv"), lines);
        Assert.Contains("Success Exported: " + Path.Combine(output, "flight 2.csv"), lines);
        Assert.DoesNotContain(lines, l => l.Contains("export plot", StringComparison.Ordinal) || l.Contains("Plot exported", StringComparison.Ordinal));
        Assert.Equal(new[] { "flight 1.csv", "flight 2.csv" }, Directory.GetFiles(output).Select(f => Path.GetFileName(f)).Order());
        Assert.Equal("Batch complete: 2 processed, 0 skipped.", vm.Status);
        Assert.Equal(Models.Logging.LogLevel.Success, vm.StatusLevel);
        Assert.Equal("Batch complete: 2 processed, 0 skipped. Output written to " + output + ".", vm.Log.Entries[^1].Text);
    }

    [Fact]
    public async Task ATemplatesNamesAndColoursGoOnEveryFileWhenAskedAndAFailedFileIsSaidSo()
    {
        string one = Copy("flight 1.ch10"), two = Copy("flight 2.ch10");
        var vm = new MainWindowViewModel(new MainWindowServices { ProcessingDialog = new CancelFirst(), AppRoot = Repo.Root });
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
        var plain = new MainWindowViewModel(new MainWindowServices { AppRoot = Repo.Root });
        await plain.RunBatchAsync(PrnTemplate(), new[] { one }, null, reuseAppearance: false);
        Assert.Equal(new[] { "CH-01 PRN 15 5M", "CH-01 PRN 15 5M", "CH-03 PRN 11 1M", "CH-03 PRN 11 1M" }, plain.Plot.Series.Select(s => s.Name));
        Assert.DoesNotContain(plain.Log.Entries, e => e.Text.StartsWith("Exported", StringComparison.Ordinal));
        Assert.Empty(Directory.GetFiles(_dir, "*.csv"));
        // Nothing to run: nothing happens, not even the plot cleared (as v2.14.2's batch started with no files).
        int logged = plain.Log.Entries.Count;
        string status = plain.Status;
        IReadOnlyList<PlotSeries> shown = plain.Plot.Series.ToArray();
        await plain.RunBatchAsync(PrnTemplate(), Array.Empty<string>(), null, false);
        Assert.Equal(logged, plain.Log.Entries.Count);
        Assert.Equal(status, plain.Status);
        Assert.Equal(shown, plain.Plot.Series);
        Assert.False(plain.IsBusy);

        // A CSV that cannot be written is said so.
        string nowhere = Path.Combine(_dir, "nowhere");
        await plain.RunBatchAsync(PrnTemplate(), new[] { one }, nowhere, false);
        Assert.Contains((Models.Logging.LogLevel.Error, "Failed to export CSV: " + Path.Combine(nowhere, "flight 1.csv")),
                        plain.Log.Entries.Select(e => (e.Level, e.Text)));
    }

    /// <summary>
    /// A copy of <paramref name="source"/> recorded <paramref name="days"/> later, so that it shares no time with it: each
    /// time packet's day that many more. A time packet (data type 0x11) holds, after its channel-specific word, the BCD time in 16-bit
    /// words; the third has the day's units and tens in its low byte, and its high byte the hundreds of a day of the
    /// year (two low bits) or, when the channel-specific word's bit 9 says the date is a day and month, the month.
    /// </summary>
    private string ADayLater(string source, string name, int days = 1)
    {
        byte[] b = File.ReadAllBytes(source);
        int edited = 0;
        for (int p = 0; p + 24 <= b.Length; p += BitConverter.ToInt32(b, p + 4))
        {
            Assert.True(b[p] == 0x25 && b[p + 1] == 0xEB, "lost the packets' sync at " + p);
            if (b[p + 15] != 0x11) continue;
            int data = p + 24 + ((b[p + 14] & 0x80) != 0 ? 12 : 0);   // past a secondary header
            bool dayOfYear = ((b[data + 1] >> 1) & 1) == 0;
            int at = data + 4 + 4;
            int day = (b[at] & 0xF) + 10 * (b[at] >> 4) + (dayOfYear ? 100 * (b[at + 1] & 0x3) : 0) + days;
            Assert.InRange(day, 1 + days, dayOfYear ? 365 : 28);   // no carry into the month or the year
            b[at] = (byte)(day % 10 | day / 10 % 10 << 4);
            if (dayOfYear) b[at + 1] = (byte)(b[at + 1] & ~0x3 | day / 100);
            edited++;
        }
        Assert.True(edited > 0, "no time packet");
        string path = Path.Combine(_dir, name);
        File.WriteAllBytes(path, b);
        return path;
    }

    [Fact]
    public async Task ARecordingThatSharesNoTimeWithTheOthersIsNamedInAWarning()
    {
        // step_cal's channel in Receiver SNR with the defaults, over the time channel at place 1, swapped.
        Ch10File file = StreamsRun.Open(StepCal);
        StreamSetup row = StreamRules.ForEditing(StreamRules.WithMode(StreamDefaults.NewStream(file.PcmChannels[0]), StreamMode.ReceiverChannelInfo), Repo.Root)
            with { Process = true, Configured = true };
        var template = new ProcessingTemplate("v", "step", 1, true, new[] { new TemplateEntry(row, Array.Empty<SeriesAppearance>()) });
        string later = ADayLater(StepCal, "a day later.ch10"), missing = Path.Combine(_dir, "gone.ch10"), output = Path.Combine(_dir, "out");
        Directory.CreateDirectory(output);
        var images = new FakeImages();
        var vm = new MainWindowViewModel(new MainWindowServices { TemplateFiles = new FakeTemplateFiles(), TemplateDialogs = new FakeTemplateDialogs(), ImageExporter = images, AppRoot = Repo.Root });
        images.Plot = vm.Plot;
        var available = new List<bool>();
        vm.ApplyTemplateCommand.CanExecuteChanged += (_, _) => available.Add(vm.ApplyTemplateCommand.CanExecute(null));
        int before = vm.Log.Entries.Count;
        await vm.RunBatchAsync(template, new[] { StepCal, later, missing }, output, false);

        Assert.Equal(new[] { false, true }, available);   // Apply Template waits while the batch runs
        Assert.Contains("Warning The most recently added source's recording time ('a day later.ch10') does not overlap the data already loaded. " +
                        "They will share one time axis with a large gap between them.", Lines(vm, before));
        Assert.Equal("Batch complete: 2 processed, 1 skipped.", vm.Status);
        Assert.Equal(Models.Logging.LogLevel.Warning, vm.StatusLevel);
        Assert.Equal("Batch complete: 2 processed, 1 skipped. Output written to " + output + ".", vm.Log.Entries[^1].Text);
        // Without frame-sync data, one image per file, named for it.
        Assert.Equal(new[] { Path.Combine(output, "step_cal, example.png"), Path.Combine(output, "a day later.png") }, images.Asked.Select(a => a.Path));
        // Each recording kept as the template ran it, for Save as Template: its time channel's place and byte order.
        Assert.Equal(new[] { (0, 1, true), (1, 1, true) }, vm.Sources.Select(s => (s.SourceId, s.TimeChannelIndex, s.SwapBytes)));
    }

    [Fact]
    public async Task EachRecordingEarlierThanTheLastMovesTheTimeBaseAgain()
    {
        // TestPlotViewModel's addStreamDataRebasesTwiceForSuccessivelyEarlierSources, through the batch: step_cal two days
        // later, a day later, then as recorded. Each file starts a day before everything already shown, so the shared
        // base moves twice and the files already there shift right twice, landing a day apart.
        Ch10File file = StreamsRun.Open(StepCal);
        StreamSetup row = StreamRules.ForEditing(StreamRules.WithMode(StreamDefaults.NewStream(file.PcmChannels[0]), StreamMode.ReceiverChannelInfo), Repo.Root)
            with { Process = true, Configured = true };
        var template = new ProcessingTemplate("v", "step", 1, true, new[] { new TemplateEntry(row, Array.Empty<SeriesAppearance>()) });
        string twoDays = ADayLater(StepCal, "two days later.ch10", days: 2), oneDay = ADayLater(StepCal, "a day later.ch10");
        var vm = new MainWindowViewModel(new MainWindowServices { AppRoot = Repo.Root });
        await vm.RunBatchAsync(template, new[] { twoDays, oneDay, StepCal }, null, false);

        Assert.Equal(new[] { 0, 1, 2 }, vm.Plot.SourceList().Select(s => s.Id));
        double StartOf(int source) => vm.Plot.Series.First(s => s.SourceId == source).X[0];
        Assert.Equal(0.0, StartOf(2), 9);
        Assert.Equal(86400.0, StartOf(1), 6);
        Assert.Equal(2 * 86400.0, StartOf(0), 6);
        Assert.Equal(StartOf(0) + vm.Plot.Series.First(s => s.SourceId == 2).X[^1], vm.Plot.XMax, 6);
    }

    [Fact]
    public async Task RemovingTheEarliestRecordingStartsTheAxisAtTheNextAndKeepsEverySamplesClockTime()
    {
        // step_cal as recorded, then a day later: elapsed zero is the first's start, and the second's samples a day on.
        Ch10File file = StreamsRun.Open(StepCal);
        StreamSetup row = StreamRules.ForEditing(StreamRules.WithMode(StreamDefaults.NewStream(file.PcmChannels[0]), StreamMode.ReceiverChannelInfo), Repo.Root)
            with { Process = true, Configured = true };
        var template = new ProcessingTemplate("v", "step", 1, true, new[] { new TemplateEntry(row, Array.Empty<SeriesAppearance>()) });
        string oneDay = ADayLater(StepCal, "a day later.ch10");
        var vm = new MainWindowViewModel(new MainWindowServices { AppRoot = Repo.Root });
        await vm.RunBatchAsync(template, new[] { StepCal, oneDay }, null, false);
        PlotSeries later = vm.Plot.Series.First(s => s.SourceId == 1);
        Assert.Equal(86400.0, later.X[0], 6);
        string[] clock = later.X.Select(vm.Plot.FormatTime).ToArray();
        int version = later.Version;
        double[] held = later.X;   // as an export being written holds it
        string before = Path.Combine(_dir, "before.csv"), after = Path.Combine(_dir, "after.csv");
        Assert.True(vm.Plot.ExportCsv(before, sourceId: 1));

        Assert.True(vm.Plot.RemoveSource(0));

        Assert.Equal(0.0, later.X[0], 9);                                    // the axis starts at the earliest sample left
        Assert.Equal(clock, later.X.Select(vm.Plot.FormatTime));             // each sample at the clock time it was taken
        Assert.True(later.Version > version);                                // moved, so the chart draws it again
        Assert.NotSame(held, later.X);                                       // in new arrays: what an export holds is not moved under it
        Assert.Equal(86400.0, held[0], 6);
        Assert.Equal((0.0, later.X[^1]), (vm.Plot.XViewMin, vm.Plot.XViewMax));
        Assert.Equal(later.X[^1], vm.Plot.XMax, 6);
        Assert.True(vm.Plot.ExportCsv(after));
        Assert.Equal(TempFiles.ReadAllBytes(before), TempFiles.ReadAllBytes(after));   // to the millisecond, as the CSV gives it
    }

    [Fact]
    public async Task ReusingTheTemplatesLooksKeepsEveryFilesNamesAndColoursNotOnlyTheLasts()
    {
        // Each file's series were recoloured to the template's once it ran; the next file's run used to colour every series again.
        string one = CopyShort("flight 1.ch10"), two = CopyShort("flight 2.ch10");
        var vm = new MainWindowViewModel(new MainWindowServices { AppRoot = Repo.Root });

        await vm.RunBatchAsync(PrnTemplate(), new[] { one, two }, null, reuseAppearance: true);

        Assert.Equal(new[] { 0, 1 }, vm.Plot.SourceList().Select(s => s.Id));
        foreach (int source in new[] { 0, 1 })
        {
            PlotSeries[] series = vm.Plot.Series.Where(s => s.SourceId == source).ToArray();
            Assert.Equal(new[] { "Fifteen", "Fifteen", "Eleven", "Eleven" }, series.Select(s => s.Name));
            Assert.Equal(new[] { Color.Parse("#ff6a0dad"), Color.Parse("#ff6a0dad"), Color.Parse("#ff1f77b4"), Color.Parse("#ff1f77b4") },
                         series.Select(s => s.Color));
        }
    }

    [Fact]
    public async Task ABatchColoursItsStreamsAfreshNotAfterThoseOfThePlotItReplaced()
    {
        var vm = new MainWindowViewModel(new MainWindowServices { AppRoot = Repo.Root });
        await vm.LoadAsync(CopyShort("before.ch10"));   // CH-01, in the first Frame Sync Lock colour
        ProcessingTemplate t = PrnTemplate();
        ProcessingTemplate thirdOnly = t with
        {
            Entries = t.Entries.Select(e => e.Config.PcmChannelId == 13 ? e with { Config = e.Config with { Process = false } } : e).ToList(),
        };

        await vm.RunBatchAsync(thirdOnly, new[] { CopyShort("batch.ch10") }, null, reuseAppearance: false);

        PlotSeries lockSeries = Assert.Single(vm.Plot.Series, s => s.Metric == MetricType.FrameSyncLock);
        Assert.NotEqual(13, lockSeries.StreamOrder);
        Assert.Equal(PlotColors.FrameSyncLock[0], lockSeries.Color);   // the plot started again: its first stream, its first colour
    }

    [Fact]
    public async Task FilesOfOneBaseNameAreNamedApartAndNoneOfTheirExportsOverwritesAnother()
    {
        // Named up to the first dot, as v2.x named them, both of these were "2024": the second's CSV and images replaced the first's.
        string one = CopyShort("2024.11.05_run1.ch10"), two = CopyShort("2024.11.05_run2.ch10"), output = Path.Combine(_dir, "out");
        Directory.CreateDirectory(output);
        var images = new FakeImages();
        var vm = new MainWindowViewModel(new MainWindowServices { ImageExporter = images, AppRoot = Repo.Root });
        images.Plot = vm.Plot;

        await vm.RunBatchAsync(PrnTemplate(), new[] { one, two }, output, reuseAppearance: false);

        Assert.Equal(new[] { (0, "2024"), (1, "2024 (2)") }, vm.Plot.SourceList());
        Assert.Equal("2024", vm.Plot.Title);
        Assert.Equal(new[] { "2024 (2).csv", "2024.csv" }, Directory.GetFiles(output).Select(f => Path.GetFileName(f)).Order(StringComparer.Ordinal));
        Assert.Equal(new[] { "2024_framesync_lock.png", "2024_missed_frames.png", "2024 (2)_framesync_lock.png", "2024 (2)_missed_frames.png" },
                     images.Asked.Select(a => Path.GetFileName(a.Path)));
        Assert.StartsWith("Time (DOY:HH:MM:SS.mmm),S1| ", File.ReadAllLines(Path.Combine(output, "2024 (2).csv"))[0], StringComparison.Ordinal);   // the second's own

        // Windows does not tell "Flight" from "flight" in a file name, so neither does the batch.
        string upper = CopyShort("Flight.a.ch10"), lower = CopyShort("flight.b.ch10"), output2 = Path.Combine(_dir, "out2");
        Directory.CreateDirectory(output2);
        var vm2 = new MainWindowViewModel(new MainWindowServices { AppRoot = Repo.Root });
        await vm2.RunBatchAsync(PrnTemplate(), new[] { upper, lower }, output2, reuseAppearance: false);
        Assert.Equal(new[] { "Flight", "flight (2)" }, vm2.Plot.SourceList().Select(s => s.Label));
        Assert.Equal(new[] { "Flight.csv", "flight (2).csv" }, Directory.GetFiles(output2).Select(f => Path.GetFileName(f)).Order(StringComparer.Ordinal));
    }

    [Fact]
    public async Task AnOpenKeepsTheByteOrderAndTimeChannelConfigureStreamsSettled()
    {
        // Safran's channel 13 is PRN-15 recorded in the other byte order (Legacy Chapter 10 on), as 4.5's oracle runs it.
        string safran = Repo.Recording("safran_testfile, sample.ch10");
        Ch10File file = StreamsRun.Open(safran);
        var vm = new MainWindowViewModel(new MainWindowServices { Configurator = FakeConfigurator.Settled(StreamsChoices.Safran(safran, file, timeChannelIndex: 3)), AppRoot = Repo.Root });
        await vm.LoadAsync(safran);
        ProcessedSource source = Assert.Single(vm.Sources);
        Assert.Equal((3, false), (source.TimeChannelIndex, source.SwapBytes));
    }

    // ---- Apply Template to Files --------------------------------------------------------------------------------------

    [Fact]
    public void ATemplateThatCannotBeUsedIsAnErrorInV214sWords()
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
        TemplateFile.Parse("{ not json", out _, out string reason);   // the parser's own words follow
        Assert.Equal("Template file is not valid JSON: " + reason, vm.Log.Entries[^1].Text);
        Assert.Null(vm.ReadTemplate(Write("new.json", "{\"schemaVersion\": 9, \"entries\": []}")));
        Assert.Equal("Template file uses an unsupported schema version.", vm.Log.Entries[^1].Text);
        Assert.Null(vm.ReadTemplate(Write("array.json", "[]")));
        Assert.Equal("Template file is not a valid template (unrecognized format).", vm.Log.Entries[^1].Text);
        Assert.Null(vm.ReadTemplate(Fixture("empty.json")));
        Assert.Equal("Template has no stream entries.", vm.Log.Entries[^1].Text);
        Assert.All(vm.Log.Entries.TakeLast(5), e => Assert.Equal(Models.Logging.LogLevel.Error, e.Level));
        // The bar has one sentence for every way it can fail; the log has the reason.
        Assert.Equal(("Could not read the template. See the log.", Models.Logging.LogLevel.Error), (vm.Status, vm.StatusLevel));
        Assert.NotNull(vm.ReadTemplate(Fixture("prn-sample.json")));
    }

    [Fact]
    public async Task ApplyTemplateIsBusyWhileItChecksTheFilesAndNotWhileItsDialogIsUp()
    {
        // The check runs off the UI thread with no dialog up: were the window not busy, Open or a drop could start another
        // load meanwhile, and the batch the user then confirmed would refuse itself and say nothing.
        string one = Copy("flight 1.ch10");
        var files = new FakeTemplateFiles { Template = Fixture("prn-sample.json"), Recordings = new[] { one, StepCal } };
        var dialogs = new FakeTemplateDialogs();
        var vm = new MainWindowViewModel(new MainWindowServices { TemplateFiles = files, TemplateDialogs = dialogs, AppRoot = Repo.Root });
        var busy = new List<bool>();
        vm.PropertyChanged += (_, e) => { if (e.PropertyName == nameof(MainWindowViewModel.IsBusy)) busy.Add(vm.IsBusy); };
        bool busyAtTheDialog = true;
        dialogs.OnConfirm = _ => busyAtTheDialog = vm.IsBusy;

        // Cancelled at the confirmation: busy for the check, and no longer when the dialog is up.
        await vm.ApplyTemplateCommand.ExecuteAsync(null);
        Assert.Equal(new[] { true, false }, busy);
        Assert.False(busyAtTheDialog);
        Assert.True(vm.ApplyTemplateCommand.CanExecute(null));

        // Confirmed: the batch is then busy on its own account, and the window is free when it ends.
        busy.Clear();
        dialogs.OnConfirm = c => { busyAtTheDialog = vm.IsBusy; c.ProcessCommand.Execute(null); };
        await vm.ApplyTemplateCommand.ExecuteAsync(null);
        Assert.Equal(new[] { true, false, true, false }, busy);
        Assert.False(busyAtTheDialog);
        Assert.Equal("Batch complete: 1 processed, 0 skipped.", vm.Status);
        Assert.False(vm.IsBusy);
    }

    [Fact]
    public async Task ApplyTemplateWithoutTheNativeCoreSaysSoAndStops()
    {
        // As Open, Import CSV and the batch say it: in the status and the log, not thrown out of the command. The
        // check of the files is the first thing that needs the core, after the template and the recordings are chosen.
        string? before = Environment.GetEnvironmentVariable("TMDQ_CORE_DLL");
        Environment.SetEnvironmentVariable("TMDQ_CORE_DLL", Path.Combine(_dir, "no-such-core", "tmdq_core.dll"));
        try
        {
            var files = new FakeTemplateFiles { Template = Fixture("prn-sample.json"), Recordings = new[] { Prn } };
            var dialogs = new FakeTemplateDialogs();
            var vm = new MainWindowViewModel(new MainWindowServices { TemplateFiles = files, TemplateDialogs = dialogs, AppRoot = Repo.Root });
            await vm.ApplyTemplateCommand.ExecuteAsync(null);
            Assert.StartsWith(MainWindowViewModel.CoreNotLoaded, vm.Status);
            Assert.Equal((vm.Status, Models.Logging.LogLevel.Error), (vm.Log.Entries[^1].Text, vm.Log.Entries[^1].Level));
            Assert.Null(dialogs.Shown);   // nothing was asked
            Assert.False(vm.IsBusy);
        }
        finally
        {
            Environment.SetEnvironmentVariable("TMDQ_CORE_DLL", before);
        }
    }

    [Fact]
    public async Task ApplyTemplateReadsTheTemplateChecksTheFilesAndRunsTheOnesThatMatchOnceConfirmed()
    {
        string one = Copy("flight 1.ch10");
        var files = new FakeTemplateFiles { Template = Fixture("prn-sample.json"), Recordings = new[] { one, StepCal } };
        var dialogs = new FakeTemplateDialogs();
        var vm = new MainWindowViewModel(new MainWindowServices { TemplateFiles = files, TemplateDialogs = dialogs, AppRoot = Repo.Root });
        Assert.True(vm.ApplyTemplateCommand.CanExecute(null));
        Assert.False(new MainWindowViewModel(new MainWindowServices { TemplateFiles = files }).ApplyTemplateCommand.CanExecute(null));
        Assert.False(new MainWindowViewModel(new MainWindowServices { TemplateDialogs = dialogs }).ApplyTemplateCommand.CanExecute(null));

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
        // As the confirmation was left: the template's names and colours, and no export.
        Assert.Equal("Batch complete: 1 processed, 0 skipped.", vm.Status);
        Assert.Equal(Models.Logging.LogLevel.Success, vm.StatusLevel);
        Assert.Equal("Fifteen", vm.Plot.Series[0].Name);

        // Its own names kept, and each file exported to the folder given (spaces trimmed).
        string output = Path.Combine(_dir, "out");
        Directory.CreateDirectory(output);
        dialogs.OnConfirm = c =>
        {
            c.ReuseAppearance = false;
            c.ExportPerFile = true;
            c.OutputFolder = "  " + output + "  ";
            c.ProcessCommand.Execute(null);
        };
        await vm.ApplyTemplateCommand.ExecuteAsync(null);
        Assert.Equal("Batch complete: 1 processed, 0 skipped.", vm.Status);
        Assert.Equal(Models.Logging.LogLevel.Success, vm.StatusLevel);
        Assert.Equal("Batch complete: 1 processed, 0 skipped. Output written to " + output + ".", vm.Log.Entries[^1].Text);
        Assert.True(File.Exists(Path.Combine(output, "flight 1.csv")));
        Assert.Equal("CH-01 PRN 15 5M", vm.Plot.Series[0].Name);

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
        var vm = new MainWindowViewModel(new MainWindowServices { TemplateFiles = files, AppRoot = Repo.Root });
        Assert.False(vm.SaveTemplateCommand.CanExecute(null));   // nothing processed yet
        await vm.SaveTemplateCommand.ExecuteAsync(null);         // run anyway: it says so
        Assert.Equal((Models.Logging.LogLevel.Warning, "Nothing to save as a template -- no sources have finished processing yet."),
                     (vm.Log.Entries[^1].Level, vm.Log.Entries[^1].Text));
        Assert.Equal(("Nothing to save as a template yet.", Models.Logging.LogLevel.Warning), (vm.Status, vm.StatusLevel));

        int told = 0;
        vm.SaveTemplateCommand.CanExecuteChanged += (_, _) => told++;
        await vm.LoadAsync(Prn);   // the first channel with the defaults, no dialog
        Assert.True(vm.SaveTemplateCommand.CanExecute(null));
        Assert.True(told > 0);
        ProcessedSource source = Assert.Single(vm.Sources);
        Assert.Equal((0, Prn, 0, true), (source.SourceId, source.FilePath, source.TimeChannelIndex, source.SwapBytes));
        Assert.Equal(new[] { 13, 14, 15, 16 }, source.Rows.Select(r => r.PcmChannelId));   // every row, processed or not
        Assert.Equal(new[] { true, false, false, false }, source.Rows.Select(r => r.Process));   // the first, as it was run
        Assert.Equal(new[] { (0, "prn_testfile, sample") }, vm.Plot.SourceList());

        vm.Plot.RenameSeries(vm.Plot.Series[0].Id, "Renamed");
        await vm.SaveTemplateCommand.ExecuteAsync(null);
        Assert.Equal("Template saved: mine.json", vm.Log.Entries[^1].Text);
        Assert.Equal(("Saved template mine.json.", Models.Logging.LogLevel.Success), (vm.Status, vm.StatusLevel));
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
        Assert.Equal((Models.Logging.LogLevel.Error, "Could not write template file: " + nowhere), (vm.Log.Entries[^1].Level, vm.Log.Entries[^1].Text));
        Assert.Equal(("Could not save the template. See the log.", Models.Logging.LogLevel.Error), (vm.Status, vm.StatusLevel));
    }

    [Fact]
    public async Task WithSeveralRecordingsSaveAsTemplateAsksWhichAndAnOpenForgetsThem()
    {
        string one = Copy("flight 1.ch10"), two = Copy("flight 2.ch10");
        var files = new FakeTemplateFiles { SavePath = Path.Combine(_dir, "chosen.json") };
        var dialogs = new FakeTemplateDialogs { Choice = 1 };
        var vm = new MainWindowViewModel(new MainWindowServices { TemplateFiles = files, TemplateDialogs = dialogs, AppRoot = Repo.Root });
        await vm.RunBatchAsync(PrnTemplate(), new[] { one, two }, null, false);
        await vm.SaveTemplateCommand.ExecuteAsync(null);
        Assert.Equal(new[] { "flight 1.ch10 (source 0)", "flight 2.ch10 (source 1)" }, dialogs.Choices);
        Assert.Equal(TemplateLoad.Loaded, TemplateFile.Read(files.SavePath, out ProcessingTemplate? t, out _));
        Assert.Equal("flight 2", t!.Name);

        dialogs.Choice = null;   // cancelled
        files.Asked.Clear();
        await vm.SaveTemplateCommand.ExecuteAsync(null);
        Assert.Empty(files.Asked);

        // A recording that cannot be read forgets the batch's: there is nothing left to capture.
        string notRecording = Path.Combine(_dir, "not.ch10");
        File.WriteAllText(notRecording, "no");
        await vm.LoadAsync(notRecording);
        Assert.Empty(vm.Sources);
        await vm.RunBatchAsync(PrnTemplate(), new[] { one, two }, null, false);
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
        Assert.Equal("Process Multiple Files", BatchApplyViewModel.Title);
        Assert.Equal(("1 of 2 selected file(s) match the template and will be processed.", 1), (vm.Summary, vm.MatchCount));
        Assert.Equal(new[] { @"C:\a\one.ch10" }, vm.Matched);
        Assert.Equal(("✓", "one.ch10", @"C:\a\one.ch10"), (vm.Rows[0].Glyph, vm.Rows[0].Text, vm.Rows[0].ToolTip));
        Assert.Equal(("✗", "two.ch10  —  extra channel(s): 9"), (vm.Rows[1].Glyph, vm.Rows[1].Text));
        Assert.Equal((true, false), (vm.Rows[0].Check.Ok, vm.Rows[1].Check.Ok));
        Assert.Equal((StatusLevel.Success, StatusLevel.Critical), (StatusPaint.OfMatch(true), StatusPaint.OfMatch(false)));
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

        // No file matches: nothing to process, even run regardless.
        var none = new BatchApplyViewModel(new[] { checks[1] }, "", null);
        Assert.False(none.CanProcess);
        none.ProcessCommand.Execute(null);
        Assert.False(none.Accepted);
        int closed = 0;
        vm.CloseRequested += (_, _) => closed++;
        vm.CancelCommand.Execute(null);
        Assert.Equal((1, false), (closed, vm.Accepted));
        vm.ProcessCommand.Execute(null);
        Assert.Equal((2, true), (closed, vm.Accepted));
    }

    [Fact]
    public void ProcessIsAskedAgainWhenExportingPerFileOrTheFolderChanges()
    {
        var vm = new BatchApplyViewModel(new[] { new FileCheck(@"C:\a\one.ch10", true, "") }, "", new FakeTemplateFiles());
        var said = new List<string?>();
        vm.PropertyChanged += (_, e) => said.Add(e.PropertyName);
        int asked = 0;
        vm.ProcessCommand.CanExecuteChanged += (_, _) => asked++;

        vm.ExportPerFile = true;   // with no folder, Process now needs one
        Assert.Contains(nameof(BatchApplyViewModel.CanProcess), said);
        Assert.True(asked > 0);
        Assert.False(vm.ProcessCommand.CanExecute(null));

        said.Clear();
        asked = 0;
        vm.OutputFolder = @"D:\out";
        Assert.Contains(nameof(BatchApplyViewModel.CanProcess), said);
        Assert.True(asked > 0);
        Assert.True(vm.ProcessCommand.CanExecute(null));
    }

    // ---- the windows ----------------------------------------------------------------------------------------------------

    [AvaloniaFact]
    public void TheConfirmationWindowListsTheFilesAndClosesOnProcessOrCancel()
    {
        var owner = new Window { Width = 400, Height = 300 };
        owner.Show();
        var vm = new BatchApplyViewModel(new[] { new FileCheck(@"C:\a\one.ch10", true, "") }, @"C:\a", new FakeTemplateFiles());
        Task shown = new WindowTemplateDialogs(owner).ConfirmBatchAsync(vm);
        Dispatcher.UIThread.RunJobs();
        var window = Assert.IsType<BatchApplyWindow>(Assert.Single(owner.OwnedWindows));
        Assert.True(window.IsDialog);
        Assert.Equal("Process Multiple Files", window.Title);
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
    public void TheConfirmationWindowColoursEachFilesGlyphByWhetherItMatches()
    {
        var checks = new[] { new FileCheck(@"C:\a\one.ch10", true, ""), new FileCheck(@"C:\a\two.ch10", false, "extra channel(s): 9") };
        var window = new BatchApplyWindow { DataContext = new BatchApplyViewModel(checks, @"C:\a", new FakeTemplateFiles()) };
        window.Show();
        Dispatcher.UIThread.RunJobs();
        TextBlock[] glyphs = window.GetVisualDescendants().OfType<TextBlock>().Where(t => t.Text is "✓" or "✗").ToArray();
        // Fluent's success and critical, in the theme's shade (light here: #0F7B0F and #C42B1C).
        Assert.Equal(new[] { ("✓", Color.Parse("#0F7B0F")), ("✗", Color.Parse("#C42B1C")) },
                     glyphs.Select(t => (t.Text ?? "", ((ISolidColorBrush)t.Foreground!).Color)));
        window.Close();
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
}

internal static class VisualSearch
{
    public static IEnumerable<T> GetVisualDescendantsOf<T>(this Avalonia.Visual v) =>
        Avalonia.VisualTree.VisualExtensions.GetVisualDescendants(v).OfType<T>();
}
