using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Styling;
using Avalonia.Threading;
using SkiaSharp;
using TmDq.App.Models.Export;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.App.Models.Templates;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Dialogs;
using TmDq.App.ViewModels.Plot;
using TmDq.App.Views;
using TmDq.App.Views.Dialogs;
using TmDq.App.Views.Plot;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// The user manual's figures, rendered from the application itself on the headless platform (plan step 4.9): each
/// test sets up one scene from the committed recordings and shipped settings, checks what the manual's caption says
/// about it (so a caption cannot outlive the behaviour it describes), and renders it. With TMDQ_MANUAL_FIGURES set
/// to a folder the figures are written there (eng\build_manual.ps1 does this); otherwise to a temporary
/// folder, deleted after, so the suite still proves every figure can be made. The names are the ones the v2.x
/// manual's hand captures had, which the manual's anchors place. The dark theme, which the application opens in, except
/// where the light one is the subject.
/// <para>A figure is the same in every capture, so a re-capture changes only the figures whose subject changed: the
/// log's clock is fixed (<see cref="FigureClock"/>), a dialog shows <see cref="ShownFolder"/> where the files really
/// are, and every animation runs out before a capture. A real run's log, behind the main window's figures, is written
/// by threads that race, and says how long it took: <see cref="Steady"/> sets its lines in one order, and its
/// percentages and seconds in one wording, before each capture.</para>
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class ManualFiguresTests : IDisposable
{
    public const string OutputVariable = "TMDQ_MANUAL_FIGURES";

    /// <summary>Written beside the figures when they are kept: the version of the application that rendered them.</summary>
    public const string RenderedVersionFile = "rendered-version.txt";

    private readonly string _out;
    private readonly bool _keep;
    private readonly string _scratch = TempFiles.NewPath("figscratch");

    public ManualFiguresTests()
    {
        string? folder = Environment.GetEnvironmentVariable(OutputVariable);
        _keep = !string.IsNullOrEmpty(folder);
        _out = _keep ? folder! : TempFiles.NewPath("figures");
        Directory.CreateDirectory(_out);
        // Four figures show the log, whose first line names the version: the figures say which version rendered them, and
        // eng\check_release_consistency.ps1 refuses a release whose figures were rendered by another one.
        if (_keep) File.WriteAllText(Path.Combine(_out, RenderedVersionFile), AppInfo.Version + "\n");
        Directory.CreateDirectory(_scratch);
        NativeCore.EnsureLoaded();
    }

    public void Dispose()
    {
        // This fixture renders figures and asserts nothing about a handle being released, so a scratch folder that will
        // not go is no failure: on CI the three copies of the 81 MB recording have been held past the retry window
        // (three failures, the last two on the same file; never traced to a holder; the core closes a recording before it reports a run finished). TemplateBatchTests
        // runs the same copy and batch under the strict delete, so a leak in that path still fails there.
        if (!_keep) TempFiles.TryDeleteDirectory(_out);
        TempFiles.TryDeleteDirectory(_scratch);
    }

    /// <summary>The calibration walkthrough shows the step file as text, not as a picture of an editor: what it shows must
    /// be lines of the file the application ships (<c>settings\rcvr_cals\TRC.toml</c>), in its order, so the manual cannot
    /// describe a file that is not there.</summary>
    [Fact]
    public void TheStepFileTheWalkthroughShowsIsLinesOfTheOneTheApplicationShips()
    {
        string html = File.ReadAllText(Repo.File(@"docs\manual\walkthroughs.html"));
        const string open = "<pre id=\"step-toml\">";
        int start = html.IndexOf(open, StringComparison.Ordinal);
        Assert.True(start >= 0, "the walkthrough has lost its step file excerpt");
        int end = html.IndexOf("</pre>", start, StringComparison.Ordinal);
        string shown = System.Net.WebUtility.HtmlDecode(html[(start + open.Length)..end]);

        string[] shipped = File.ReadAllLines(Repo.File(@"settings\rcvr_cals\TRC.toml"));
        int from = 0, checkedLines = 0;
        foreach (string line in shown.Split('\n').Select(l => l.TrimEnd('\r')))
        {
            if (line == "…" || line.Length == 0) continue;   // the cut, and the blank lines between
            int at = Array.IndexOf(shipped, line, from);
            Assert.True(at >= 0, $"the manual shows '{line}', which the shipped TRC.toml does not have there");
            from = at + 1;
            checkedLines++;
        }
        Assert.True(checkedLines >= 8, $"only {checkedLines} lines were compared");
        Assert.Contains("db = 60.0", shown, StringComparison.Ordinal);   // the file's last level: an excerpt that stops short says "eleven steps" of a file it cuts
    }

    /// <summary>The folder a dialog shows where the scene's files really are (the checkout, or a scratch folder named
    /// for the run), which would change the figure from one checkout or run to the next and put the user's name in
    /// the manual.</summary>
    private const string ShownFolder = @"D:\Flight Tests";

    private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

    private static string StepCal => Repo.Recording("step_cal, example.ch10");

    private static ProcessingTemplate PrnTemplate()
    {
        Assert.Equal(TemplateLoad.Loaded, TemplateFile.Read(Path.Combine(Repo.Root, "tests", "TmDq.App.Tests", "Templates", "prn-sample.json"),
                                                             out ProcessingTemplate? t, out _));
        return t!;
    }

    // ---- scenes ------------------------------------------------------------------------------------------------

    /// <summary>Every service a command needs to be available, as the application has them; none is asked anything.</summary>
    private sealed class AppServices : IRecordingPicker, IDataFilePicker, IExportDialog, ITemplateFilePicker, ITemplateDialogs,
                                       ISettingsFilePicker, ICalibrationFilePicker
    {
        public static readonly AppServices Any = new();
        public Task<string?> OpenAsync(string title, string? folder) => Task.FromResult<string?>(null);
        public Task<string?> SaveAsync(string title, string? folder) => Task.FromResult<string?>(null);
        public Task<string?> OpenStepFileAsync(string? folder) => Task.FromResult<string?>(null);
        public Task<string?> OpenCalibrationRecordingAsync(string? folder) => Task.FromResult<string?>(null);
        public Task<string?> PickRecordingAsync() => Task.FromResult<string?>(null);
        public Task<string?> OpenCsvAsync(string? folder) => Task.FromResult<string?>(null);
        public Task<string?> SaveExportAsync(ExportKind kind, string current) => Task.FromResult<string?>(null);
        public Task ShowAsync(ExportViewModel vm) => Task.CompletedTask;
        public Task<string?> OpenTemplateAsync(string? folder) => Task.FromResult<string?>(null);
        public Task<string?> SaveTemplateAsync(string? folder) => Task.FromResult<string?>(null);
        public Task<IReadOnlyList<string>> OpenRecordingsAsync(string? folder) => Task.FromResult<IReadOnlyList<string>>(Array.Empty<string>());
        public Task<string?> PickFolderAsync(string? folder) => Task.FromResult<string?>(null);
        public Task ConfirmBatchAsync(BatchApplyViewModel vm) => Task.CompletedTask;
        public Task<int?> ChooseSourceAsync(IReadOnlyList<string> choices) => Task.FromResult<int?>(null);
    }

    private static MainWindowViewModel WithServices(IStreamConfigurator configurator, IProcessingDialog? progress = null)
    {
        AppServices s = AppServices.Any;
        return new MainWindowViewModel(new MainWindowServices
        {
            Picker = s, Configurator = configurator, ProcessingDialog = progress, Files = s, ExportDialog = s, TemplateFiles = s,
            TemplateDialogs = s, AppRoot = Repo.Root, Clock = new FigureClock(),
        });
    }

    /// <summary>The log's clock in a figure: stopped, so every line has the same time in every capture. (A clock that
    /// moved on at each line would not do: how many progress lines a run logs before a capture is the run's own.)</summary>
    private sealed class FigureClock : TimeProvider
    {
        public override DateTimeOffset GetUtcNow() => new(2026, 11, 3, 9, 41, 7, TimeSpan.Zero);

        public override TimeZoneInfo LocalTimeZone => TimeZoneInfo.Utc;
    }
    /// <summary>The progress dialog of a run, shown at the first report past 70 % to the callback it is made with.</summary>
    private sealed class CapturingDialog : IProcessingDialog
    {
        private readonly Action<ProcessingViewModel> _at;
        private bool _done;
        public CapturingDialog(Action<ProcessingViewModel> at) => _at = at;
        public void Show(ProcessingViewModel vm) => vm.PropertyChanged += (_, e) =>
        {
            if (_done || e.PropertyName != nameof(ProcessingViewModel.Progress) || vm.Progress < 70) return;
            _done = true;
            _at(vm);
        };
        public void Close() { }
    }

    /// <summary>The PRN sample's two streams that carry data, as the template v2.14.2 saved has them
    /// (CH-01 on PRN 15, CH-03 on PRN 11), processed; and, with <paramref name="withBadStream"/>, CH-04 on PRN 15,
    /// which finds no sync.</summary>
    private static async Task<MainWindowViewModel> PrnProcessed(bool withBadStream = false, IProcessingDialog? progress = null,
                                                                Action<MainWindowViewModel>? before = null)
    {
        ProcessingTemplate template = PrnTemplate();
        MainWindowViewModel vm = WithServices(FakeConfigurator.Choosing((path, file) =>
        {
            List<StreamSetup> rows = template.Entries.Select(e => e.Config).ToList();
            if (withBadStream)
                rows[3] = StreamDefaults.NewStream(file.PcmChannels.Single(c => c.Id == 16)) with { Process = true, Configured = true };
            RunRequest request = StreamRules.ToRunRequest(path, file.DefaultTimeChannel, rows, legacyChapter10: false, Repo.Root);
            return new StreamsChoice(request, rows, 0);
        }), progress);
        before?.Invoke(vm);
        await vm.LoadAsync(Prn);
        return vm;
    }

    private static StreamConfig StepCalSnr(IReadOnlyList<CalibrationProfile>? profiles = null) => new()
    {
        PcmChannelId = 26, Label = "CH-16 AGC STEP CALS 800K", Mode = StreamMode.ReceiverChannelInfo,
        SyncPattern = ReceiverSnrDefaults.Pattern, SyncMask = "FFFFFFFF", BitsInMinorFrame = ReceiverSnrDefaults.BitsInMinorFrame,
        Calibration = profiles ?? Array.Empty<CalibrationProfile>(),
    };

    /// <summary>step_cal's receiver stream processed, linearly or with <paramref name="profiles"/>.</summary>
    private static async Task<MainWindowViewModel> StepCalProcessed(IReadOnlyList<CalibrationProfile>? profiles = null)
    {
        MainWindowViewModel vm = WithServices(FakeConfigurator.Choosing((path, file) =>
            new StreamsChoice(new RunRequest(path, file.DefaultTimeChannel, new[] { StepCalSnr(profiles) }) { AppRoot = Repo.Root },
                              Array.Empty<StreamSetup>(), 0)));
        await vm.LoadAsync(StepCal);
        return vm;
    }

    /// <summary>step_cal's stream as recorded, for Extract Calibration: channel 26, the receiver frame sync, 800-bit
    /// frames at 0.8 Mbps.</summary>
    private static CalibrationBase StepCalBase() => new(1, 26, "FE6B2840", "", 800, false, false, 0.8, true, "", 16, 3);

    private static async Task<ExtractCalibrationViewModel> Extracted(decimal clipStart = 0m, decimal clipEnd = 0m)
    {
        var e = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null, AppServices.Any) { ClipStart = clipStart, ClipEnd = clipEnd };
        await e.LoadCalibrationAsync(StepCal);
        if (e.Extraction is { } running) await running;
        return e;
    }

    // ---- rendering ---------------------------------------------------------------------------------------------

    // Between two drawings of a window that is settling; the theme's animations run for a tenth of a second or so.
    private const int SettleMs = 150, SettleFrames = 5, SettleRounds = 20;

    // A text box's caret blinks on the clock, so it would be drawn in one capture and not the next: a figure has none.
    // (One for each window: a style has one parent.)
    private static Style NoCaret() => new(x => x.OfType<TextBox>())
    {
        Setters = { new Setter(TextBox.CaretBrushProperty, Brushes.Transparent) },
    };

    private static T Shown<T>(T window, ThemeVariant? theme = null) where T : Window
    {
        window.RequestedThemeVariant = theme ?? ThemeVariant.Dark;
        window.Styles.Add(NoCaret());
        window.Show();
        Dispatcher.UIThread.RunJobs();
        return window;
    }

    /// <summary>An expander's chevron is created turned over (180 degrees) and turns to its rest at the first render ticks, over
    /// a few tens of milliseconds of the clock: a window drawn before, or between the ticks that carry it there, shows a chevron
    /// half turned, which is how the receiver tree's figure came out differently from one capture to the next. Not run for
    /// every window: the progress dialog's capture is taken inside a run's report, where the jobs it runs would be the run's.</summary>
    private static T AtRest<T>(T window) where T : Window
    {
        for (int tick = 0; tick < 3; tick++)
        {
            System.Threading.Thread.Sleep(100);
            AvaloniaHeadlessPlatform.ForceRenderTimerTick(2);
            Dispatcher.UIThread.RunJobs();
        }
        return window;
    }

    private static MainWindow MainWindowOf(MainWindowViewModel vm, ThemeVariant? theme = null) =>
        Shown(new MainWindow { DataContext = vm, Width = 1280, Height = 760 }, theme);

    /// <summary>What <paramref name="window"/> draws once every animation in it has run to its end: a chevron caught
    /// half turned, or a check box between two colours, would make the figure differ from one capture to the next.
    /// It is drawn again, time and frames apart (so it holds however the headless platform's animation clock counts),
    /// until two drawings in a row are the same. Not when <paramref name="settle"/> is false: the processing figure is
    /// captured from inside a run's progress report, where a wait would let the run finish (and the test go on)
    /// underneath the capture; nothing in it is animated.</summary>
    private static SKBitmap Capture(Window window, bool settle = true)
    {
        if (window.DataContext is MainWindowViewModel vm) SteadyLog(vm);
        SKBitmap shot = Draw(window);
        for (int round = 0; settle && round < SettleRounds; round++)
        {
            System.Threading.Thread.Sleep(SettleMs);
            AvaloniaHeadlessPlatform.ForceRenderTimerTick(SettleFrames);
            SKBitmap next = Draw(window);
            bool still = shot.Bytes.AsSpan().SequenceEqual(next.Bytes);
            shot.Dispose();
            shot = next;
            if (still && round > 0) return shot;
        }
        Assert.False(settle, "the window was still moving after " + SettleRounds + " drawings");
        return shot;
    }

    /// <summary>The two wordings in a run's log that differ from one run to the next, as the core words them: a "NN% read..."
    /// line is logged at the first progress check (time-gated) past each tenth of the file, so NN is 42 in one run and
    /// 41 in the next; and a stream's "elapsed N.Ns" is wall-clock time. A figure shows the canonical wording (the tenth,
    /// and two seconds: "41% read...", "elapsed 2.0s").</summary>
    internal static string SteadyText(string text)
    {
        text = System.Text.RegularExpressions.Regex.Replace(text, @"^(\d)\d% read\.\.\.$", "${1}1% read...");
        return System.Text.RegularExpressions.Regex.Replace(text, @"elapsed \d+\.\d+s\.", "elapsed 2.0s.");
    }

    /// <summary>The log's lines as a figure shows them: <see cref="SteadyText"/> on each, and the lines the reader's thread and
    /// the streams' threads log together, which race, set in one order: the reader's "Reading...", the streams' start lines,
    /// the reader's percentages, then each stream's results; a stream's own lines keep their order and the streams are
    /// taken in the order of their channels. Any other line ends such a run of lines.</summary>
    internal static List<Models.Logging.LogEntry> Steady(IReadOnlyList<Models.Logging.LogEntry> entries)
    {
        List<Models.Logging.LogEntry> steady = entries.Select(e => e with { Text = SteadyText(e.Text) }).ToList();
        var byTag = new Dictionary<string, int>();   // "CH-03 PRN 11 1M" -> 15, from the lines that say "[CH 15 CH-03 PRN 11 1M] ..."
        foreach (Models.Logging.LogEntry e in steady)
            if (System.Text.RegularExpressions.Regex.Match(e.Text, @"^\[CH (\d+) (.+?)\] ") is { Success: true } m)
                byTag[m.Groups[2].Value] = int.Parse(m.Groups[1].Value);

        // The channel of the stream a line belongs to: the one it names, or the one whose sample count it gives.
        int? Stream(Models.Logging.LogEntry e)
        {
            System.Text.RegularExpressions.Match m = System.Text.RegularExpressions.Regex.Match(e.Text, @"^\[CH (\d+) ");
            if (m.Success) return int.Parse(m.Groups[1].Value);
            System.Text.RegularExpressions.Match s = System.Text.RegularExpressions.Regex.Match(e.Text, @"^\s*(.+?): \d+ samples, ");
            return s.Success && byTag.TryGetValue(s.Groups[1].Value, out int channel) ? channel : null;
        }

        (int Rank, int Channel)? Place(Models.Logging.LogEntry e)
        {
            if (e.Text == "Reading Chapter 10 file...") return (0, 0);
            if (System.Text.RegularExpressions.Regex.IsMatch(e.Text, @"^\d+% read\.\.\.$")) return (2, 0);
            if (Stream(e) is not { } channel) return null;
            return (e.Text.EndsWith("] processing...", StringComparison.Ordinal) ? 1 : 3, channel);
        }

        for (int i = 0; i < steady.Count; i++)
        {
            if (Place(steady[i]) is null) continue;
            int j = i;
            while (j < steady.Count && Place(steady[j]) is not null) j++;
            List<Models.Logging.LogEntry> run = steady.GetRange(i, j - i).OrderBy(e => Place(e)!.Value.Rank).ThenBy(e => Place(e)!.Value.Channel).ToList();   // stable
            steady.RemoveRange(i, j - i);
            steady.InsertRange(i, run);
            i = j - 1;
        }
        return steady;
    }

    private static void SteadyLog(MainWindowViewModel vm)
    {
        List<Models.Logging.LogEntry> steady = Steady(vm.Log.Entries);
        for (int i = 0; i < steady.Count; i++)
            if (steady[i] != vm.Log.Entries[i]) vm.Log.Entries[i] = steady[i];
    }

    [Theory]
    [InlineData("42% read...", "41% read...")]
    [InlineData("11% read...", "11% read...")]
    [InlineData("93% read...", "91% read...")]
    [InlineData("[CH 13 CH-01 PRN 15 5M] complete — 116 samples extracted, elapsed 1.9s.", "[CH 13 CH-01 PRN 15 5M] complete — 116 samples extracted, elapsed 2.0s.")]
    [InlineData("elapsed 12.34s.", "elapsed 2.0s.")]
    [InlineData("Processing complete — results plotted from memory.", "Processing complete — results plotted from memory.")]
    [InlineData("File size: 77.3 MB", "File size: 77.3 MB")]
    public void TheRunsTwoUnsteadyWordingsAreSetToTheirCanonicalForm(string logged, string shown) =>
        Assert.Equal(shown, SteadyText(logged));

    [Fact]
    public void TheLinesOfTheThreadsThatRaceAreSetInOneOrderEachStreamKeepingItsOwn()
    {
        static Models.Logging.LogEntry L(string text) => new("09:41:07", text, Models.Logging.LogLevel.Info);
        var logged = new[]
        {
            L("--- Processing 3 stream(s) concurrently ---"),
            L("[CH 15 CH-03 PRN 11 1M] processing..."), L("Reading Chapter 10 file..."), L("[CH 16 CH-04 PRN 11 5M] processing..."),
            L("11% read..."), L("[CH 13 CH-01 PRN 15 5M] processing..."), L("42% read..."),
            L("[CH 16 CH-04 PRN 11 5M] 0 bytes processed."), L("[CH 15 CH-03 PRN 11 1M] 5 bytes processed."),
            L("[CH 15 CH-03 PRN 11 1M] complete — 1 samples extracted, elapsed 1.9s."), L("  CH-03 PRN 11 1M: 109 samples, 0 channel(s)"),
            L("Processing complete — results plotted from memory."),
        };
        Assert.Equal(new[]
        {
            "--- Processing 3 stream(s) concurrently ---",
            "Reading Chapter 10 file...", "[CH 13 CH-01 PRN 15 5M] processing...", "[CH 15 CH-03 PRN 11 1M] processing...",
            "[CH 16 CH-04 PRN 11 5M] processing...", "11% read...", "41% read...",
            "[CH 15 CH-03 PRN 11 1M] 5 bytes processed.", "[CH 15 CH-03 PRN 11 1M] complete — 1 samples extracted, elapsed 2.0s.",
            "  CH-03 PRN 11 1M: 109 samples, 0 channel(s)", "[CH 16 CH-04 PRN 11 5M] 0 bytes processed.",
            "Processing complete — results plotted from memory.",
        }, Steady(logged).Select(e => e.Text));
    }

    private static SKBitmap Draw(Window window)
    {
        using WriteableBitmap frame = Frames.Capture(window);
        using ILockedFramebuffer fb = frame.Lock();
        var bitmap = new SKBitmap(new SKImageInfo(fb.Size.Width, fb.Size.Height,
                                                  fb.Format == PixelFormat.Rgba8888 ? SKColorType.Rgba8888 : SKColorType.Bgra8888, SKAlphaType.Premul));
        var row = new byte[fb.Size.Width * 4];
        for (int y = 0; y < fb.Size.Height; y++)
        {
            Marshal.Copy(fb.Address + y * fb.RowBytes, row, 0, row.Length);
            Marshal.Copy(row, 0, bitmap.GetPixels() + y * bitmap.RowBytes, row.Length);
        }
        return bitmap;
    }

    /// <summary>A menu's items, as the open menu shows them: a popup of its own, so drawn in a window of its own
    /// (the headless platform does not draw popups into the window's frame).
    /// <para>Taken out of their menu, the items are given the data context the open menu would have given them, and a
    /// list's entries the menu item theme they would have been made with.</para></summary>
    private static Window MenuOf(IEnumerable<object?> items, object? dataContext = null, ControlTheme? theme = null)
    {
        var presenter = new MenuFlyoutPresenter { ItemsSource = items.ToList(), DataContext = dataContext, ItemContainerTheme = theme };
        return Shown(new Window { Content = presenter, SizeToContent = SizeToContent.WidthAndHeight });
    }

    private static List<object?> Take(ItemsControl menu)
    {
        var items = menu.Items.ToList();
        menu.Items.Clear();
        return items;
    }

    private static List<object?> Take(MenuFlyout menu)
    {
        var items = menu.Items.ToList();
        menu.Items.Clear();
        return items;
    }

    /// <summary>Writes <paramref name="name"/>: <paramref name="base"/> with each layer drawn over it where given,
    /// with a soft shadow as a popup has, then cut to <paramref name="crop"/> when given.</summary>
    private void Save(string name, SKBitmap @base, IEnumerable<(SKBitmap Layer, SKPointI At)>? layers = null, SKRectI? crop = null)
    {
        using var surface = SKSurface.Create(new SKImageInfo(@base.Width, @base.Height, SKColorType.Bgra8888, SKAlphaType.Premul));
        SKCanvas canvas = surface.Canvas;
        canvas.DrawBitmap(@base, 0, 0);
        using var shadow = new SKPaint { ImageFilter = SKImageFilter.CreateDropShadow(0, 4, 6, 6, new SKColor(0, 0, 0, 110)) };
        foreach ((SKBitmap layer, SKPointI at) in layers ?? Array.Empty<(SKBitmap, SKPointI)>())
            canvas.DrawBitmap(layer, at.X, at.Y, shadow);
        using SKImage image = crop is { } c ? surface.Snapshot(c) : surface.Snapshot();
        using SKData png = image.Encode(SKEncodedImageFormat.Png, 100);
        File.WriteAllBytes(Path.Combine(_out, name), png.ToArray());
    }

    private void Save(string name, Window window, SKRectI? crop = null)
    {
        using SKBitmap shot = Capture(window);
        Save(name, shot, crop: crop);
    }

    /// <summary>A control's rectangle in its window's pixels.</summary>
    private static SKRectI Bounds(Control c, Visual window)
    {
        Point p = c.TranslatePoint(new Point(0, 0), window)!.Value;
        return SKRectI.Create((int)p.X, (int)p.Y, (int)c.Bounds.Width, (int)c.Bounds.Height);
    }

    private static PlotView PlotViewOf(MainWindow w) => w.FindControl<PlotView>("PlotView")!;

    // ---- the figures -------------------------------------------------------------------------------------------

    [AvaloniaFact]
    public async Task TheLockPlotsWithAndWithoutTheLegendAndInAccumulation()
    {
        MainWindowViewModel vm = await PrnProcessed();
        Assert.Equal(new[] { 13, 15 }, PlotFixtures.Locks(vm).Select(s => s.StreamOrder));   // "these streams"
        Assert.False(vm.Plot.RightAxisVisible);                                     // "no SNR scale on the right"
        MainWindow w = MainWindowOf(vm);
        SKRectI chart = Bounds(PlotViewOf(w), w);
        Save("FrameSync Perctentage Plot with Legend.png", w, chart);
        vm.Plot.LegendVisible = false;
        Dispatcher.UIThread.RunJobs();
        Save("FrameSync Perctentage Plot without Legend.png", w, chart);
        vm.Plot.LegendVisible = true;
        vm.Plot.SetLockAxisView(LockAxisView.MissedFrames);
        Dispatcher.UIThread.RunJobs();
        Save("Frame Error Accumulation Plot with Legend.png", w, chart);

        // The light theme, the whole window: the choice applies to every window, dialog and plot.
        vm.Plot.SetLockAxisView(LockAxisView.LockPercent);
        w.RequestedThemeVariant = ThemeVariant.Light;
        Dispatcher.UIThread.RunJobs();
        Save("walk-theme-01-light-plot.png", w);
        w.Close();
    }

    [AvaloniaFact]
    public async Task TheMenusOverTheWindowAndTheChart()
    {
        MainWindowViewModel vm = await PrnProcessed();
        MainWindow w = MainWindowOf(vm);
        using SKBitmap window = Capture(w);

        // The application menu, open under its button, and its File submenu beside it.
        List<object?> menuItems = Take(w.MainMenu);
        var fileMenu = menuItems.OfType<MenuItem>().Single(i => (string)i.Header! == "File");
        Window app = MenuOf(menuItems, vm);
        using (SKBitmap menu = Capture(app))
        {
            SKRectI button = Bounds(w.MenuButton, w);
            // Its entries, once the menu is drawn with them (an item with none draws no arrow).
            Window file = MenuOf(Take(fileMenu), vm);
            Point entry = fileMenu.TranslatePoint(new Point(fileMenu.Bounds.Width, 0), app)!.Value;
            using SKBitmap subMenu = Capture(file);
            var menuAt = new SKPointI(button.Left, button.Bottom + 2);
            Save("walk-app-01-hamburger.png", window,
                 new[] { (menu, menuAt), (subMenu, new SKPointI(menuAt.X + (int)entry.X - 4, menuAt.Y + (int)entry.Y - 4)) },
                 SKRectI.Create(0, 0, Math.Min(window.Width, button.Left + menu.Width + subMenu.Width + 40), Math.Min(window.Height, button.Bottom + menu.Height + 60)));
            file.Close();
        }
        app.Close();

        // The plot's right-click menu, and its Y Axes submenu beside it.
        PlotView view = PlotViewOf(w);
        SKRectI chart = Bounds(view, w);
        List<object?> items = Take(view.ChartMenu);
        var yAxes = (MenuItem)items.OfType<MenuItem>().Single(i => (string)i.Header! == "Y Axes");
        Window context = MenuOf(items, view.Commands);
        Assert.Contains(items.OfType<MenuItem>(), i => (string)i.Header! == "Plot File" && !i.IsEnabled);   // "stay disabled until they apply"
        var at = new SKPointI(chart.Left + 260, chart.Top + 120);
        using (SKBitmap menu = Capture(context))
        {
            Save("Main Context Menu.png", window, new[] { (menu, at) }, chart);
            // Its submenu's items, once the menu is drawn with them (an item with none draws no arrow).
            Window axes = MenuOf(Take(yAxes), view.Commands);
            Point item = yAxes.TranslatePoint(new Point(yAxes.Bounds.Width, 0), context)!.Value;
            using (SKBitmap subMenu = Capture(axes))
                Save("Y Axes Submenu.png", window, new[] { (menu, at), (subMenu, new SKPointI(at.X + (int)item.X - 4, at.Y + (int)item.Y - 4)) }, chart);
            axes.Close();
        }
        context.Close();
        w.Close();
    }

    [AvaloniaFact]
    public async Task ProcessingTheLogAndAStreamThatFindsNoSync()
    {
        // A stream configured with the wrong pattern: an error in the log, and one curve fewer on the plot.
        MainWindowViewModel bad = await PrnProcessed(withBadStream: true);
        Assert.Equal(2, PlotFixtures.Locks(bad).Count);
        Assert.Contains(bad.Log.Entries, e => e.Level == Models.Logging.LogLevel.Error);
        MainWindow w = MainWindowOf(bad);
        Save("walk-err-01-log-error.png", w);
        Assert.All(bad.Log.Entries.Where(e => e.Text.Contains("% read")), e => Assert.Matches(@"^\d1% read\.\.\.$", e.Text));   // captured steady
        w.Close();

        // Processing under way, caught in a real run: the progress dialog over the window, the log filling in beside it.
        MainWindow? main = null;
        bool caught = false;
        var dialog = new CapturingDialog(p =>
        {
            p.Progress = 70;   // the first report past 70 % says 70, 71 or 73, by how long the read took: the bar is drawn at 70
            using SKBitmap window = Capture(main!, settle: false);
            Window progress = Shown(new ProcessingWindow { DataContext = p });
            using (SKBitmap box = Capture(progress, settle: false))
                Save("walk-fs-04-processing.png", window, new[] { (box, new SKPointI((window.Width - box.Width) / 2 + 200, (window.Height - box.Height) / 2)) });
            progress.Close();
            caught = true;
        });
        MainWindowViewModel vm = await PrnProcessed(progress: dialog, before: v => main = MainWindowOf(v));
        Assert.True(caught, "the run never reported 70 %");

        // Set Time Window, opened on the window shown.
        Window time = Shown(new TimeWindowPromptWindow(vm.Plot.FormatTime(vm.Plot.XViewMin), vm.Plot.FormatTime(vm.Plot.XViewMax)));
        Save("walk-app-03-time-window.png", time);
        time.Close();

        // The Export dialog, on its defaults.
        (string image, string data, string log) = ExportViewModel.DefaultPaths(ShownFolder, vm.Plot.Title);
        Window export = Shown(new ExportWindow { DataContext = new ExportViewModel(image, data, log) });
        Save("Export Dialog.png", export);
        export.Close();
        main!.Close();
    }

    [AvaloniaFact]
    public async Task ConfigureStreamsAndTheSetupDialogs()
    {
        // The walkthrough's own choice, the one every later figure of the PRN recording shows: CH-01 and CH-03, the two
        // streams that carry data, are on and ready; CH-02 and CH-04 are off. Process is free to run.
        Ch10File prn = StreamsRun.Open(Prn);
        var configure = new ConfigureStreamsViewModel(Prn, prn, new FakeStreamDialogs(), AppServices.Any, Repo.Root, AppServices.Any);
        List<StreamSetup> template = PrnTemplate().Entries.Select(e => e.Config).ToList();
        configure.Rows[0].Setup = template[0] with { Process = true };
        configure.Rows[2].Setup = template[2] with { Process = true };
        Assert.Equal(new[] { Readiness.Ready, Readiness.Idle, Readiness.Ready, Readiness.Idle },
                     configure.Rows.Select(r => StreamRules.ReadinessOf(r.Setup)));
        Assert.True(configure.CanProcess);
        // And it is the run the plot figures come from: the streams ticked here are the curves drawn there.
        Assert.Equal(configure.Rows.Where(r => r.Process).Select(r => r.Label),
                     PlotFixtures.Locks(await PrnProcessed()).Select(s => s.Name));
        Window window = Shown(new ConfigureStreamsWindow { DataContext = configure });
        Save("Config Streams Dialg.png", window);
        Save("walk-fs-02-configure-streams.png", window);
        window.Close();

        // A step calibration recording's stream switched to Receiver SNR, ready.
        Ch10File step = StreamsRun.Open(StepCal);
        var snr = new ConfigureStreamsViewModel(StepCal, step, new FakeStreamDialogs(), AppServices.Any, Repo.Root, AppServices.Any);
        snr.Rows[0].Process = true;
        snr.Rows[0].ModeIndex = 0;
        snr.Rows[0].Setup = StreamRules.ForEditing(snr.Rows[0].Setup, Repo.Root) with { Configured = true };
        Assert.Equal(Readiness.Ready, StreamRules.ReadinessOf(snr.Rows[0].Setup));
        window = Shown(new ConfigureStreamsWindow { DataContext = snr });
        Save("walk-snr-01-mode-select.png", window);
        window.Close();

        // The frame sync setup dialog on a new stream: PRN 15, which is what a stream starts on.
        StreamSetup lockRow = StreamRules.ForEditing(StreamDefaults.NewStream(prn.PcmChannels[0]) with { Process = true }, Repo.Root);
        Assert.Equal(FrameSyncDefaults.Pattern, lockRow.SyncPattern);
        window = Shown(new StreamSetupWindow { DataContext = new StreamSetupViewModel(lockRow, AppServices.Any, Repo.Root, null, prn.DefaultTimeChannel, true, new FakeStreamDialogs(), AppServices.Any) });
        Save("walk-fs-03-framesync-setup.png", window);
        window.Close();

        // The receiver setup dialog: the frame sync half above, the receiver half below.
        StreamSetup snrRow = StreamRules.ForEditing(StreamRules.WithMode(StreamDefaults.NewStream(step.PcmChannels[0]), StreamMode.ReceiverChannelInfo)
                                                    with { Process = true }, Repo.Root);
        window = Shown(new StreamSetupWindow { DataContext = new StreamSetupViewModel(snrRow, AppServices.Any, Repo.Root, null, step.DefaultTimeChannel, true, new FakeStreamDialogs(), AppServices.Any) });
        Save("walk-snr-02-receiver-setup.png", window);
        window.Close();
    }

    [AvaloniaFact]
    public async Task TheReceiverPlotsAndStepCalibration()
    {
        // Linear: 48 channels of sixteen receivers, and a plot opens with each receiver's combined channel only, so the legend lists sixteen.
        MainWindowViewModel linear = await StepCalProcessed();
        Assert.Equal(48, linear.Plot.Series.Count);
        Assert.Equal(16, linear.Plot.LegendEntries().Count);
        Assert.All(linear.Plot.LegendEntries(), e => Assert.StartsWith("CH26 C_RCVR", e.Label, StringComparison.Ordinal));
        Assert.False(linear.Plot.LeftAxisVisible);   // "no left-hand axis"
        MainWindow w = MainWindowOf(linear);
        SKRectI chart = Bounds(PlotViewOf(w), w);
        Save("Uncalibrated SNR Plot with Legend.png", w, chart);
        linear.Plot.LegendVisible = false;
        Dispatcher.UIThread.RunJobs();
        Save("Uncalibrated SNR Plot without Legend.png", w, chart);
        w.Close();

        // Extract Calibration as it opens: the shipped step file loaded and confirmed, the recording still to choose.
        var opening = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null, AppServices.Any);
        Assert.Equal(("default.toml", ""), (opening.StepName, opening.CalPath));
        Assert.Equal("✓ 11 steps parsed (loaded automatically).", opening.StepStatus);
        Window window = Shown(new ExtractCalibrationWindow { DataContext = opening });
        Save("walk-cal-01-extract-button.png", window);
        window.Close();

        // Both files, the ends trimmed: eleven steps, twelve of forty-eight channels.
        ExtractCalibrationViewModel clipped = await Extracted(6m, 2m);
        Assert.Equal("✓ 11 steps parsed (loaded automatically).", clipped.StepStatus);
        Assert.Equal("✓ Calibrated 12 of 48 channel(s).", clipped.CalStatus);
        window = Shown(new ExtractCalibrationWindow { DataContext = clipped });
        Save("walk-cal-03-clip-controls.png", window);
        window.Close();

        // The summary: receivers 1, 3, 5 and 6 calibrated, receiver 1 held at its top measured step.
        ExtractCalibrationViewModel full = await Extracted();
        Assert.Contains("receivers 1, 3, 5, 6", full.Report);
        Assert.Contains("48", full.Report);
        window = Shown(new ReportWindow("Calibration Extracted", full.Report));
        Save("walk-cal-04-summary.png", window);
        window.Close();

        // Calibrated: the steps evenly spaced, topping out at 60 dB.
        MainWindowViewModel calibrated = await StepCalProcessed(full.Profiles);
        Assert.Equal(60, calibrated.Plot.Series.Where(s => s.YMax <= 60.5).Max(s => Math.Round(s.YMax)));
        w = MainWindowOf(calibrated);
        calibrated.Plot.LegendVisible = false;
        Dispatcher.UIThread.RunJobs();
        Save("Calibrated SNR Plot without Legend.png", w, Bounds(PlotViewOf(w), w));
        w.Close();
    }

    [AvaloniaFact]
    public async Task CustomizeViewItsReceiverTreeAndTheColourDialog()
    {
        MainWindowViewModel lockVm = await PrnProcessed();
        var lockView = new CustomizeViewModel(lockVm.Plot, new NoPlotPrompts());
        Window window = Shown(new CustomizeViewWindow { DataContext = lockView });
        Save("walk-cust-01-lock-tab.png", window);
        window.Close();

        // The Receiver SNR tab: one receiver expanded, only its combined channel shown (how a plot opens), so its box is partly set.
        MainWindowViewModel snr = await StepCalProcessed();
        var snrView = new CustomizeViewModel(snr.Plot, new NoPlotPrompts());
        ReceiverGroup first = snrView.SelectedSnrStream!.Receivers.First();
        first.IsExpanded = true;
        Assert.Equal(new[] { false, false, true }, first.Channels.Select(c => c.IsChecked));
        Assert.Null(first.IsChecked);
        window = new CustomizeViewWindow { DataContext = snrView };
        window.FindControl<TabControl>("Tabs")!.SelectedIndex = 1;
        AtRest(Shown(window));
        Save("walk-cust-02-snr-tree.png", window);
        window.Close();

        window = Shown(new ColorPromptWindow(snr.Plot.Series[0].Color));
        Save("walk-cust-03-recolor.png", window);
        window.Close();
    }

    private sealed class FixedKind : ICsvKindDialog
    {
        public Task<CsvKind?> ChooseAsync(string fileName) => Task.FromResult<CsvKind?>(CsvKind.PhasedArrayAntenna);
    }

    [AvaloniaFact]
    public async Task AnAntennasLogImportedAfterTheQuestionEveryImportAsks()
    {
        string sample = Repo.Recording("av_snr_log.csv");

        // The question every import asks first, as the dialog words it for this file.
        var question = new ChoiceWindow("Import CSV", $"What kind of file is '{Path.GetFileName(sample)}'?", CsvKinds.All.Select(CsvKinds.Title).ToList());
        question.Choices.SelectedIndex = CsvKinds.All.ToList().IndexOf(CsvKind.PhasedArrayAntenna);   // the answer for this file
        Window ask = Shown(question);
        Save("walk-csv-01-kind.png", ask);
        ask.Close();

        // The log on an empty plot: 30 streams of H, V and C is 90 series, and only the 30 combined ones are shown.
        AppServices s = AppServices.Any;
        var vm = new MainWindowViewModel(new MainWindowServices
        {
            Picker = s, Files = s, ExportDialog = s, TemplateFiles = s, TemplateDialogs = s, AppRoot = Repo.Root, Clock = new FigureClock(),
            CsvKinds = new FixedKind(),
        });
        await vm.ImportCsvAsync(sample);
        Assert.Equal(90, vm.Plot.Series.Count);
        Assert.Equal(30, vm.Plot.Series.Count(x => x.Visible));
        Assert.Equal("Stream 1 C", vm.Plot.LegendEntries()[0].Label);
        vm.Plot.LegendVisible = false;   // thirty rows would cover the Receiver SNR scale this figure is about
        MainWindow w = MainWindowOf(vm);
        Save("walk-csv-02-antenna-plot.png", w);
        w.Close();

        // Customize View on it: the SCU's group, a stream expanded to its three polarizations, only C shown.
        var view = new CustomizeViewModel(vm.Plot, new NoPlotPrompts());
        ReceiverGroup stream = view.SelectedSnrStream!.Receivers.First();
        stream.IsExpanded = true;
        Assert.Equal(new[] { "H", "V", "C" }, stream.Channels.Select(c => c.Label));
        Window window = new CustomizeViewWindow { DataContext = view };
        window.FindControl<TabControl>("Tabs")!.SelectedIndex = 1;
        AtRest(Shown(window));
        Save("walk-csv-03-customize.png", window);
        window.Close();
    }

    [AvaloniaFact]
    public async Task TheBatchConfirmationAndPlotFile()
    {
        // The template a user saves names each series for its stream (which is what they saw when they saved it), not "Fifteen" and
        // "Eleven" as the fixture does for the soak's sake. The files are named as files, since the selector lists recordings.
        ProcessingTemplate fixture = PrnTemplate();
        ProcessingTemplate template = fixture with
        {
            Entries = fixture.Entries.Select(e => e with
            {
                Appearance = e.Appearance.Select(a => a with { Name = e.Config.Label }).ToList(),
            }).ToList(),
        };
        var copies = new[] { "prn_testfile 1.ch10", "prn_testfile 2.ch10", "prn_testfile 3.ch10" }.Select(n =>
        {
            string path = Path.Combine(_scratch, n);
            File.Copy(Prn, path);
            return path;
        }).ToList();
        IReadOnlyList<FileCheck> checks = TemplateBatch.Validate(template, copies.Append(StepCal).ToList());
        Assert.Equal(new[] { true, true, true, false }, checks.Select(c => c.Ok));   // "three files match and one is rejected"
        Window window = Shown(new BatchApplyWindow { DataContext = new BatchApplyViewModel(checks, Path.Combine(ShownFolder, "Exports"), AppServices.Any) });
        Save("walk-batch-02-apply-dialog.png", window);
        window.Close();

        MainWindowViewModel vm = WithServices(new FirstChannelRun());
        await vm.RunBatchAsync(template, copies, null, reuseAppearance: true);
        Assert.Equal(3, vm.Plot.SourceList().Count);
        MainWindow w = MainWindowOf(vm);
        using SKBitmap shot = Capture(w);
        PlotView view = PlotViewOf(w);
        SKRectI chart = Bounds(view, w);
        PlotCommands commands = view.Commands!;
        List<object?> items = Take(view.ChartMenu);
        var plotFile = (MenuItem)items.OfType<MenuItem>().Single(i => (string)i.Header! == "Plot File");
        Assert.Equal(new[] { "All files (overlaid)", "prn_testfile 1", "prn_testfile 2", "prn_testfile 3" },
                     commands.PlotFiles.Where(c => !c.IsSeparator).Select(c => c.Header));
        // The legend names streams as the recording does (CH-01 PRN 15 5M), not by names that mean nothing to a reader.
        Assert.All(vm.Plot.Series, s => Assert.StartsWith("CH-", s.Name, StringComparison.Ordinal));
        Window context = MenuOf(items, commands), sub = MenuOf(commands.PlotFiles, theme: plotFile.ItemContainerTheme);
        var at = new SKPointI(chart.Left + 260, chart.Top + 80);
        Point item = plotFile.TranslatePoint(new Point(plotFile.Bounds.Width, 0), context)!.Value;
        using (SKBitmap menu = Capture(context))
        using (SKBitmap subMenu = Capture(sub))
            Save("walk-batch-03-plot-file-menu.png", shot, new[] { (menu, at), (subMenu, new SKPointI(at.X + (int)item.X - 4, at.Y + (int)item.Y - 4)) }, chart);
        context.Close();
        sub.Close();
        w.Close();
    }
}
