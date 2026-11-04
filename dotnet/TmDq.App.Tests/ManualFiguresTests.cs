using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Styling;
using Avalonia.Threading;
using SkiaSharp;
using TmDq.App.Plot;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.App.Streams;
using TmDq.App.Templates;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The user manual's figures, rendered from the application itself on the headless platform (plan step 4.9): each
    /// test sets up one scene from the committed recordings and shipped settings, checks what the manual's caption says
    /// about it (so a caption cannot outlive the behaviour it describes), and renders it. With TMDQ_MANUAL_FIGURES set
    /// to a folder the figures are written there (scripts\build_manual.ps1 does this); otherwise to a temporary
    /// folder, deleted after, so the suite still proves every figure can be made. The names are the ones the Qt
    /// manual's hand captures had, which the manual's anchors place. The dark theme, which the application opens in, except
    /// where the light one is the subject.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class ManualFiguresTests : IDisposable
    {
        public const string OutputVariable = "TMDQ_MANUAL_FIGURES";

        private readonly string _out;
        private readonly bool _keep;
        private readonly string _scratch = Path.Combine(Path.GetTempPath(), "tmdq-figscratch-" + Guid.NewGuid().ToString("N"));

        public ManualFiguresTests()
        {
            string? folder = Environment.GetEnvironmentVariable(OutputVariable);
            _keep = !string.IsNullOrEmpty(folder);
            _out = _keep ? folder! : Path.Combine(Path.GetTempPath(), "tmdq-figures-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(_out);
            Directory.CreateDirectory(_scratch);
            NativeCore.EnsureLoaded();
        }

        public void Dispose()
        {
            if (!_keep) TempFiles.DeleteDirectory(_out, recursive: true);
            TempFiles.DeleteDirectory(_scratch, recursive: true);
        }

        private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

        private static string StepCal => Repo.Recording("step_cal, example.ch10");

        private static ProcessingTemplate PrnTemplate()
        {
            Assert.Equal(TemplateLoad.Loaded, TemplateFile.Read(Path.Combine(Repo.Root, "dotnet", "TmDq.App.Tests", "Templates", "prn-sample.json"),
                                                                 out ProcessingTemplate? t, out _));
            return t!;
        }

        // ---- scenes ------------------------------------------------------------------------------------------------

        private sealed class Settled : IStreamConfigurator
        {
            private readonly Func<string, Ch10File, StreamsChoice> _choose;
            public Settled(Func<string, Ch10File, StreamsChoice> choose) => _choose = choose;
            public Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file) => Task.FromResult<StreamsChoice?>(_choose(path, file));
        }

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
            return new MainWindowViewModel(s, configurator, progress, files: s, exportDialog: s, templateFiles: s, templateDialogs: s, appRoot: Repo.Root);
        }
        /// <summary>The progress dialog of a run, shown at the first report past 70 % to <paramref name="at"/>.</summary>
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

        private sealed class NoPrompts : IPlotPrompts
        {
            public Task<string?> AskTextAsync(string title, string label, string initial) => Task.FromResult<string?>(null);
            public Task<double?> AskNumberAsync(string title, string label, double value, double min, double max, int decimals) => Task.FromResult<double?>(null);
            public Task<(string Start, string Stop)?> AskTimeWindowAsync(string start, string stop) => Task.FromResult<(string, string)?>(null);
            public Task<Color?> PickColorAsync(Color initial) => Task.FromResult<Color?>(null);
            public Task<bool> CustomizeAsync(CustomizeViewModel customize) => Task.FromResult(false);
        }

        /// <summary>The PRN sample's two streams that carry data, as the template the Qt application saved has them
        /// (CH-01 on PRN 15, CH-03 on PRN 11), processed; and, with <paramref name="withBadStream"/>, CH-04 on PRN 15,
        /// which finds no sync.</summary>
        private static async Task<MainWindowViewModel> PrnProcessed(bool withBadStream = false, IProcessingDialog? progress = null,
                                                                    Action<MainWindowViewModel>? before = null)
        {
            ProcessingTemplate template = PrnTemplate();
            MainWindowViewModel vm = WithServices(new Settled((path, file) =>
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
            MainWindowViewModel vm = WithServices(new Settled((path, file) =>
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

        private static T Shown<T>(T window, ThemeVariant? theme = null) where T : Window
        {
            window.RequestedThemeVariant = theme ?? ThemeVariant.Dark;
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return window;
        }

        private static MainWindow MainWindowOf(MainWindowViewModel vm, ThemeVariant? theme = null) =>
            Shown(new MainWindow { DataContext = vm, Width = 1280, Height = 760 }, theme);

        private static SKBitmap Capture(Window window)
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
        /// (the headless platform does not draw popups into the window's frame).</summary>
        private static Window MenuOf(IEnumerable<object?> items)
        {
            var presenter = new MenuFlyoutPresenter { ItemsSource = items.ToList() };
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
            Assert.Equal(new[] { 13, 15 }, vm.AllSeries.Select(s => s.PcmChannelId));   // "these streams"
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

            // The application menu, open under its button.
            w.BuildMenu();
            Window app = MenuOf(Take(w.MainMenu));
            using (SKBitmap menu = Capture(app))
            {
                SKRectI button = Bounds(w.MenuButton, w);
                Save("walk-app-01-hamburger.png", window, new[] { (menu, new SKPointI(button.Left, button.Bottom + 2)) },
                     SKRectI.Create(0, 0, Math.Min(window.Width, button.Left + menu.Width + 220), Math.Min(window.Height, button.Bottom + menu.Height + 60)));
            }
            app.Close();

            // The plot's right-click menu, and its Y Axes submenu beside it.
            PlotView view = PlotViewOf(w);
            SKRectI chart = Bounds(view, w);
            List<object?> items = Take(view.BuildContextMenu()!);
            var yAxes = (MenuItem)items.OfType<MenuItem>().Single(i => (string)i.Header! == "Y Axes");
            Assert.Contains(items.OfType<MenuItem>(), i => (string)i.Header! == "Plot File" && !i.IsEnabled);   // "stay disabled until they apply"
            List<object?> sub = Take((MenuItem)view.BuildContextMenu()!.Items.OfType<MenuItem>().Single(i => (string)i.Header! == "Y Axes"));
            Window context = MenuOf(items);
            var at = new SKPointI(chart.Left + 260, chart.Top + 120);
            using (SKBitmap menu = Capture(context))
            {
                Save("Main Context Menu.png", window, new[] { (menu, at) }, chart);
                Window axes = MenuOf(sub);
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
            Assert.Equal(2, bad.AllSeries.Count);
            Assert.Contains(bad.Log.Entries, e => e.Level == Logging.LogLevel.Error);
            MainWindow w = MainWindowOf(bad);
            Save("walk-err-01-log-error.png", w);
            w.Close();

            // Processing under way, caught in a real run: the progress dialog over the window, the log filling in beside it.
            MainWindow? main = null;
            bool caught = false;
            var dialog = new CapturingDialog(p =>
            {
                using SKBitmap window = Capture(main!);
                Window progress = Shown(new ProcessingWindow { DataContext = p });
                using (SKBitmap box = Capture(progress))
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
            (string image, string data, string log) = ExportViewModel.DefaultPaths(Path.GetDirectoryName(Prn)!, vm.Plot.Title);
            Window export = Shown(new ExportWindow { DataContext = new ExportViewModel(image, data, log) });
            Save("Export Dialog.png", export);
            export.Close();
            main!.Close();
        }

        [AvaloniaFact]
        public async Task ConfigureStreamsAndTheSetupDialogs()
        {
            // Three streams selected, two configured; the fourth off. Each Ready mark in its state.
            Ch10File prn = StreamsRun.Open(Prn);
            var configure = new ConfigureStreamsViewModel(Prn, prn, new FakeStreamDialogs(), AppServices.Any, Repo.Root, AppServices.Any);
            IReadOnlyList<StreamSetup> template = PrnTemplate().Entries.Select(e => e.Config).ToList();
            configure.Rows[0].Setup = template[0] with { Process = true };
            configure.Rows[1].Process = true;
            configure.Rows[2].Setup = template[2] with { Process = true };
            Assert.Equal(new[] { Readiness.Ready, Readiness.NeedsConfiguration, Readiness.Ready, Readiness.Idle },
                         configure.Rows.Select(r => StreamRules.ReadinessOf(r.Setup)));
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
            // Linear: 48 channels, the legend listing them all.
            MainWindowViewModel linear = await StepCalProcessed();
            Assert.Equal(48, linear.Plot.Series.Count);
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
            Assert.Equal(("default.toml", 11, ""), (opening.StepName, opening.Steps.Count, opening.CalPath));
            Assert.Contains("loaded automatically", opening.StepStatus);
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
            var lockView = new CustomizeViewModel(lockVm.Plot, new NoPrompts());
            Window window = Shown(new CustomizeViewWindow { DataContext = lockView });
            Save("walk-cust-01-lock-tab.png", window);
            window.Close();

            // The Receiver SNR tab: one receiver expanded, two of its three channels shown, so its box is partly set.
            MainWindowViewModel snr = await StepCalProcessed();
            var snrView = new CustomizeViewModel(snr.Plot, new NoPrompts());
            ReceiverGroup first = snrView.SelectedSnrStream!.Receivers.First();
            first.IsExpanded = true;
            first.Channels[2].IsChecked = false;
            Assert.Null(first.IsChecked);
            window = new CustomizeViewWindow { DataContext = snrView };
            window.FindControl<TabControl>("Tabs")!.SelectedIndex = 1;
            Shown(window);
            Save("walk-cust-02-snr-tree.png", window);
            window.Close();

            window = Shown(new ColorPromptWindow(snr.Plot.Series[0].Color));
            Save("walk-cust-03-recolor.png", window);
            window.Close();
        }

        [AvaloniaFact]
        public async Task TheBatchConfirmationAndPlotFile()
        {
            ProcessingTemplate template = PrnTemplate();
            var copies = new[] { "flight 1.ch10", "flight 2.ch10", "flight 3.ch10" }.Select(n =>
            {
                string path = Path.Combine(_scratch, n);
                File.Copy(Prn, path);
                return path;
            }).ToList();
            IReadOnlyList<FileCheck> checks = TemplateBatch.Validate(template, copies.Append(StepCal).ToList());
            Assert.Equal(new[] { true, true, true, false }, checks.Select(c => c.Ok));   // "three files match and one is rejected"
            Window window = Shown(new BatchApplyWindow { DataContext = new BatchApplyViewModel(checks, _scratch, AppServices.Any) });
            Save("walk-batch-02-apply-dialog.png", window);
            window.Close();

            MainWindowViewModel vm = WithServices(new FirstChannelRun());
            await vm.RunBatchAsync(template, copies, null, reuseAppearance: true);
            Assert.Equal(3, vm.Plot.SourceList().Count);
            MainWindow w = MainWindowOf(vm);
            using SKBitmap shot = Capture(w);
            PlotView view = PlotViewOf(w);
            SKRectI chart = Bounds(view, w);
            List<object?> items = Take(view.BuildContextMenu()!);
            var plotFile = (MenuItem)items.OfType<MenuItem>().Single(i => (string)i.Header! == "Plot File");
            List<object?> files = Take((MenuItem)view.BuildContextMenu()!.Items.OfType<MenuItem>().Single(i => (string)i.Header! == "Plot File"));
            Assert.Equal(new[] { "All files (overlaid)", "flight 1", "flight 2", "flight 3" }, files.OfType<MenuItem>().Select(i => (string)i.Header!));
            Window context = MenuOf(items), sub = MenuOf(files);
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
}
