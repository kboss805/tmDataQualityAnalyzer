using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.LogicalTree;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Threading;
using TmDq.App.Logging;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// Processing (plan step 4.2): the progress dialog and its Cancel (US1.0, US3.0, US3.2), and the log sidebar with its
    /// colours and multi-line messages. The log says what the Qt application's log says, in its order: the lines are the
    /// Qt view model's and coordinator's, and the core's own, which the core writes the same for both.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class ProcessingTests
    {
        private static string Prn => Repo.Recording("prn_testfile, sample.ch10");
        private static string StepCal => Repo.Recording("step_cal, example.ch10");
        private const string Clock = "13:05:09";

        /// <summary>A clock stopped at 13:05:09, local time being UTC.</summary>
        private sealed class StoppedClock : TimeProvider
        {
            public override DateTimeOffset GetUtcNow() => new(2026, 10, 31, 13, 5, 9, TimeSpan.Zero);

            public override TimeZoneInfo LocalTimeZone => TimeZoneInfo.Utc;
        }

        /// <summary>Every line a run logged, with whether it was an error, in the order it came.</summary>
        private sealed class RecordingLog : IRunLog
        {
            public List<(bool Error, string Text)> Lines { get; } = new();

            public void Line(string message) { lock (Lines) Lines.Add((false, message)); }

            public void Error(string message) { lock (Lines) Lines.Add((true, message)); }

            public List<string> Texts { get { lock (Lines) return Lines.Select(l => l.Text).ToList(); } }
        }

        /// <summary>The progress dialog as a test sees it: what it was shown, what it was told, and how often.</summary>
        private sealed class FakeProcessingDialog : IProcessingDialog
        {
            public Action<ProcessingViewModel>? OnShow { get; set; }

            public List<ProcessingViewModel> Shown { get; } = new();

            public List<int> Progress { get; } = new();

            public int Closed { get; private set; }

            public void Show(ProcessingViewModel vm)
            {
                Shown.Add(vm);
                vm.PropertyChanged += (_, e) =>
                {
                    if (e.PropertyName == nameof(ProcessingViewModel.Progress)) Progress.Add(vm.Progress);
                };
                OnShow?.Invoke(vm);
            }

            public void Close() => Closed++;
        }

        /// <summary>Configure Streams cancelled.</summary>
        private sealed class Cancelled : IStreamConfigurator
        {
            public Task<RunRequest?> ConfigureAsync(string path, Ch10File file) => Task.FromResult<RunRequest?>(null);
        }

        private static StreamConfig StepCalSnr(string? label = "RCVR") => new()
        {
            PcmChannelId = 26, Label = label, Mode = StreamMode.ReceiverChannelInfo,
            SyncPattern = ReceiverSnrDefaults.Pattern, SyncMask = "FFFFFFFF", BitsInMinorFrame = ReceiverSnrDefaults.BitsInMinorFrame,
        };

        private static RunRequest StepCalRequest(params StreamConfig[] streams)
        {
            NativeCore.EnsureLoaded();
            return new RunRequest(StepCal, Ch10File.Open(StepCal).DefaultTimeChannel, streams) { AppRoot = Repo.Root };
        }

        private static List<string> Texts(MainWindowViewModel vm) => vm.Log.Entries.Select(e => e.Text).ToList();

        // ---- The log's rules -------------------------------------------------------------------------------------------

        [Fact]
        public void ALinesLevelFollowsTheQtRules()
        {
            // The Qt suite's cases (TestMainViewModelHelpers::classifyLogMessageSeverity), and the edges of each rule.
            Assert.Equal(LogLevel.Error, SessionLog.Classify("File read ERROR; aborting."));
            Assert.Equal(LogLevel.Warning, SessionLog.Classify("WARNING: 2 time gap(s) detected."));
            Assert.Equal(LogLevel.Success, SessionLog.Classify("Pre-scan result: 3 PCM channels."));
            Assert.Equal(LogLevel.Success, SessionLog.Classify("Processing complete."));
            Assert.Equal(LogLevel.Info, SessionLog.Classify("Opening: test.ch10"));
            Assert.Equal(LogLevel.Error, SessionLog.Classify("ERROR with a WARNING too"));
            Assert.Equal(LogLevel.Warning, SessionLog.Classify("Processing complete, but a WARNING"));
            Assert.Equal(LogLevel.Info, SessionLog.Classify("an error, a warning: case matters"));
            Assert.Equal(LogLevel.Info, SessionLog.Classify("  Processing complete"));       // only at the start
            Assert.Equal(LogLevel.Info, SessionLog.Classify("The Pre-scan result: none"));
            Assert.Equal(LogLevel.Info, SessionLog.Classify("Processing"));
        }

        [Fact]
        public void EachLineIsTimedByTheClockAndKeepsItsOwnLineBreaks()
        {
            var log = new SessionLog(new StoppedClock());
            log.Add("Opening: a.ch10");
            log.Add("[CH 13] WARNING: two");
            log.Error("first line\n  second line\n  third line");
            log.Warning("a warning");
            log.Success("done");
            log.Add("ERROR in the text, logged as information", LogLevel.Info);

            Assert.Equal(new[] { LogLevel.Info, LogLevel.Warning, LogLevel.Error, LogLevel.Warning, LogLevel.Success, LogLevel.Info },
                         log.Entries.Select(e => e.Level));
            Assert.All(log.Entries, e => Assert.Equal(Clock, e.Time));
            Assert.Equal("13:05:09  Opening: a.ch10", log.Entries[0].Line);
            Assert.Equal("13:05:09  Opening: a.ch10\n13:05:09  [CH 13] WARNING: two\n13:05:09  first line\n  second line\n  third line\n" +
                         "13:05:09  a warning\n13:05:09  done\n13:05:09  ERROR in the text, logged as information", log.PlainText);
        }

        [Fact]
        public void TheClockIsTheLocalTime()
        {
            var shifted = new ShiftedClock();
            var log = new SessionLog(shifted);
            log.Add("x");
            Assert.Equal("15:05:09", log.Entries[0].Time);
        }

        private sealed class ShiftedClock : TimeProvider
        {
            public override DateTimeOffset GetUtcNow() => new(2026, 10, 31, 13, 5, 9, TimeSpan.Zero);

            public override TimeZoneInfo LocalTimeZone { get; } = TimeZoneInfo.CreateCustomTimeZone("plus two", TimeSpan.FromHours(2), "plus two", "plus two");
        }

        [Fact]
        public void TheSummaryIsTheQtApplications()
        {
            var request = new RunRequest(@"C:\rec\flight 7.ch10", 1, new[]
            {
                new StreamConfig { PcmChannelId = 13, Label = "CH-01 PRN 15 5M", SyncPattern = "334AABBF" },
                new StreamConfig { PcmChannelId = 7, Label = null, SyncPattern = "334AABBF", SamplePeriod = SamplePeriod.HundredMilliseconds },
                new StreamConfig { PcmChannelId = 8, Label = "", SyncPattern = "334AABBF", SamplePeriod = SamplePeriod.TenMilliseconds },
            });
            Assert.Equal(new[]
            {
                "--- Processing Summary ---",
                "  Input: flight 7.ch10",
                "  Streams: 3",
                "  Time range: all",
                "  CH-01 PRN 15 5M: 1000 ms period",
                "  Ch 7: 100 ms period",
                "  Ch 8: 10 ms period",
                "--- Processing 3 stream(s) concurrently ---",
            }, StreamsRun.Summary(request));
        }

        [Theory]
        [InlineData(0L, "0.0 KB")]
        [InlineData(1023L, "1.0 KB")]
        [InlineData(1048575L, "1024.0 KB")]
        [InlineData(1048576L, "1.0 MB")]
        [InlineData(81013684L, "77.3 MB")]
        public void AFileSizeIsGivenAsTheQtLogGivesIt(long bytes, string expected)
        {
            Assert.Equal(expected, MainWindowViewModel.SizeText(bytes));
        }

        [Fact]
        public void TheJobLinesComeFirstAndWhatTheThreadsSayFollowsTheSummary()
        {
            // The order the Qt application logged in: the job builder's lines (written on the starting thread while the
            // run is started), the summary, then the run's threads, which may already be talking by then.
            var log = new RecordingLog();
            var order = new RunLogOrder(log);
            order.Line("before");
            order.Starting();
            order.Line("job 1");
            var reader = new Thread(() =>
            {
                order.Line("reader");
                order.Error("reader error");
            });
            reader.Start();
            reader.Join();
            order.Line("job 2");
            order.Started(new[] { "summary 1", "summary 2" });
            order.Line("after");
            new Thread(() => order.Error("late error")).Start();
            SpinWait.SpinUntil(() => log.Lines.Count == 9, 5000);

            Assert.Equal(new[]
            {
                (false, "before"), (false, "job 1"), (false, "job 2"), (false, "summary 1"), (false, "summary 2"),
                (false, "reader"), (true, "reader error"), (false, "after"), (true, "late error"),
            }, log.Lines);
        }

        [Fact]
        public void ARunThatDoesNotStartLogsTheJobLinesItHadAndNoSummary()
        {
            var log = new RecordingLog();
            var order = new RunLogOrder(log);
            order.Starting();
            order.Line("job 1");
            order.StartFailed();
            order.Line("after");
            Assert.Equal(new[] { (false, "job 1"), (false, "after") }, log.Lines);
        }

        // ---- The run's log, through the core ----------------------------------------------------------------------------

        [Fact]
        public async Task TheRunLogsTheJobLinesThenTheSummaryThenWhatTheRunSays()
        {
            // A Receiver SNR stream: the core's job builder writes a line for it while the run is started.
            RunRequest request = StepCalRequest(StepCalSnr());
            var log = new RecordingLog();
            RunOutcome outcome = await StreamsRun.RunAsync(request, null, log, TestContext.Current.CancellationToken);
            ProcessedStream s = Assert.Single(outcome.Streams);
            List<string> lines = log.Texts;

            Assert.Equal("  RCVR: no non-linear calibration profiles configured — using linear calibration.", lines[0]);
            Assert.Equal(StreamsRun.Summary(request), lines.Skip(1).Take(6));
            Assert.Equal("Reading Chapter 10 file...", lines[7]);
            Assert.Contains($"  RCVR: {s.TimesSec.Length} samples, {s.Channels.Count} channel(s)", lines);
            Assert.True(s.TimesSec.Length > 0 && s.Channels.Count > 0);
            Assert.DoesNotContain(log.Lines, l => l.Error);
        }

        [Fact]
        public async Task TheCoresErrorsAreLoggedAsErrorsAndARunWithNothingProcessedSaysWhy()
        {
            // The Frame Sync Lock defaults on step_cal find no sync: the core reports an error and nothing completes.
            NativeCore.EnsureLoaded();
            Ch10File file = Ch10File.Open(StepCal);
            RunRequest request = FirstChannelRun.Request(StepCal, file);
            var log = new RecordingLog();
            NothingProcessedException e = await Assert.ThrowsAsync<NothingProcessedException>(() => StreamsRun.RunAsync(request, null, log, TestContext.Current.CancellationToken));
            (bool Error, string Text) error = Assert.Single(log.Lines, l => l.Error);
            Assert.Contains("no frame sync found", error.Text);
            Assert.Equal($"{file.PcmChannels[0].Name} (channel 26): {error.Text}", e.Message);
            Assert.DoesNotContain(log.Lines, l => l.Text.Contains("samples,", StringComparison.Ordinal));
        }

        [Fact]
        public async Task ARunTheCoreRefusesLogsTheStreamsBuiltBeforeAndSaysWhy()
        {
            RunRequest request = StepCalRequest(StepCalSnr(), StepCalSnr("bad") with { SyncPattern = "XYZ" });
            var log = new RecordingLog();
            RecordingException e = await Assert.ThrowsAsync<RecordingException>(() => StreamsRun.RunAsync(request, null, log, TestContext.Current.CancellationToken));
            Assert.IsNotType<NothingProcessedException>(e);
            Assert.Contains("bad", e.Message);
            Assert.Equal(new[] { "  RCVR: no non-linear calibration profiles configured — using linear calibration." }, log.Texts);
        }

        [Fact]
        public async Task ACancelBeforeTheRunStartsStartsNothing()
        {
            NativeCore.EnsureLoaded();
            RunRequest request = FirstChannelRun.Request(Prn, Ch10File.Open(Prn));
            var log = new RecordingLog();
            using var cancel = new CancellationTokenSource();
            cancel.Cancel();
            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => StreamsRun.RunAsync(request, null, log, cancel.Token));
            Assert.Empty(log.Lines);
        }

        [Fact]
        public void ACancelWhileTheFileIsReadStopsTheReaderAndEveryStream()
        {
            // The reader is parked in its first progress report (it makes them only while it reads), the cancel is made
            // there, and the reader released: it must stop reading, and no stream may complete.
            NativeCore.EnsureLoaded();
            Ch10File file = Ch10File.Open(Prn);
            var request = new RunRequest(Prn, file.DefaultTimeChannel,
                file.PcmChannels.Take(2).Select(FirstChannelRun.DefaultStream).ToArray());
            var log = new RecordingLog();
            using var cancel = new CancellationTokenSource();
            using var parked = new ManualResetEventSlim();
            using var release = new ManualResetEventSlim();
            int firstReport = -1;
            var progress = new ParkingProgress(p =>
            {
                if (Interlocked.CompareExchange(ref firstReport, p, -1) != -1) return;
                parked.Set();
                release.Wait();
            });
            Exception? thrown = null;
            var runner = new Thread(() =>
            {
                try { StreamsRun.Run(request, progress, log, cancel.Token); }
                catch (Exception e) { thrown = e; }
            });
            runner.Start();
            Assert.True(parked.Wait(TimeSpan.FromSeconds(60), TestContext.Current.CancellationToken), "the reader never reported progress");
            Assert.InRange(firstReport, 0, 99);   // still reading
            cancel.Cancel();
            release.Set();
            Assert.True(runner.Join(TimeSpan.FromSeconds(60)), "the run did not stop");

            Assert.IsAssignableFrom<OperationCanceledException>(thrown);
            Assert.Contains("Reading cancelled by user.", log.Texts);
            Assert.DoesNotContain(log.Lines, l => l.Text.Contains("samples,", StringComparison.Ordinal));
        }

        private sealed class ParkingProgress : IProgress<int>
        {
            private readonly Action<int> _report;
            public ParkingProgress(Action<int> report) => _report = report;
            public void Report(int value) => _report(value);
        }

        // ---- The main window's log and progress dialog ------------------------------------------------------------------

        [AvaloniaFact]
        public async Task TheLogTellsTheStoryOfARunAsTheQtApplicationDoes()
        {
            var vm = new MainWindowViewModel(clock: new StoppedClock());
            Assert.Equal(new[] { "tmDataQualityAnalyzer v" + AppInfo.Version, "Load a Chapter 10 file to configure per-stream processing." }, Texts(vm));
            await vm.LoadAsync(Prn);
            List<string> lines = Texts(vm);

            NativeCore.EnsureLoaded();
            RunRequest request = FirstChannelRun.Request(Prn, Ch10File.Open(Prn));
            string[] opening = { "Opening: prn_testfile, sample.ch10", "  File size: 77.3 MB", "  Time channels: 1", "  PCM channels: 4" };
            Assert.Equal(opening.Concat(StreamsRun.Summary(request)), lines.Skip(2).Take(10));
            Assert.Equal("Reading Chapter 10 file...", lines[12]);
            int completed = lines.IndexOf("  CH-01 PRN 15 5M: 116 samples, 0 channel(s)");
            Assert.True(completed > 12, "the stream's line is missing or out of place");
            Assert.Equal("Processing complete — results plotted from memory.", lines[^1]);
            Assert.Equal(LogLevel.Success, vm.Log.Entries[^1].Level);
            Assert.All(vm.Log.Entries.Take(12), e => Assert.Equal(LogLevel.Info, e.Level));
            Assert.All(vm.Log.Entries, e => Assert.Equal(Clock, e.Time));
        }

        [AvaloniaFact]
        public async Task TheCoresErrorIsLoggedOnceInRedAndNothingIsCalledComplete()
        {
            var vm = new MainWindowViewModel(clock: new StoppedClock());
            await vm.LoadAsync(StepCal);
            LogEntry error = Assert.Single(vm.Log.Entries, e => e.Level == LogLevel.Error);
            Assert.Contains("no frame sync found", error.Text);
            Assert.EndsWith(error.Text, vm.Status);
            Assert.DoesNotContain(Texts(vm), t => t.StartsWith("Processing complete", StringComparison.Ordinal));
        }

        [AvaloniaFact]
        public async Task AFileThatCannotBeOpenedIsLoggedInRed()
        {
            string junk = Path.Combine(Path.GetTempPath(), "tmdq-app-junk-" + Guid.NewGuid().ToString("N") + ".ch10");
            File.WriteAllBytes(junk, new byte[4096]);
            try
            {
                var dialog = new FakeProcessingDialog();
                var vm = new MainWindowViewModel(processingDialog: dialog);
                await vm.LoadAsync(junk);
                Assert.Equal("Opening: " + Path.GetFileName(junk), vm.Log.Entries[^2].Text);
                Assert.Equal((vm.Status, LogLevel.Error), (vm.Log.Entries[^1].Text, vm.Log.Entries[^1].Level));
                Assert.Empty(dialog.Shown);
            }
            finally
            {
                File.Delete(junk);
            }
        }

        [AvaloniaFact]
        public async Task TheProgressDialogIsUpForTheRunAndShowsTheCoresProgress()
        {
            var dialog = new FakeProcessingDialog();
            var vm = new MainWindowViewModel(processingDialog: dialog);
            bool upDuringTheRun = false;
            dialog.OnShow = p => upDuringTheRun = ReferenceEquals(vm.Processing, p) && vm.IsBusy && dialog.Closed == 0;
            await vm.LoadAsync(Prn);

            ProcessingViewModel shown = Assert.Single(dialog.Shown);
            Assert.True(upDuringTheRun);
            Assert.Equal(1, dialog.Closed);
            Assert.Null(vm.Processing);
            Assert.Equal("Processing...", shown.Text);
            // The reader reports 100 at the end of every run; the dialog has only the core's reports, so this shows
            // they reach it.
            Assert.NotEmpty(dialog.Progress);
            Assert.Equal(100, dialog.Progress[^1]);
            Assert.True(dialog.Progress.Zip(dialog.Progress.Skip(1)).All(p => p.First <= p.Second), "progress went backwards");
            Assert.All(dialog.Progress, p => Assert.InRange(p, 0, 100));
            Assert.Equal(116, vm.Series?.LockPercent.Length);
        }

        [AvaloniaFact]
        public async Task NoProgressDialogWhenNothingIsProcessed()
        {
            var dialog = new FakeProcessingDialog();
            var vm = new MainWindowViewModel(configurator: new Cancelled(), processingDialog: dialog);
            await vm.LoadAsync(Prn);
            Assert.Empty(dialog.Shown);
            Assert.Equal(0, dialog.Closed);
            Assert.Null(vm.Processing);
        }

        [AvaloniaFact]
        public async Task CancelInTheProgressDialogStopsTheRunAndShowsNothing()
        {
            var dialog = new FakeProcessingDialog();
            var vm = new MainWindowViewModel(processingDialog: dialog, clock: new StoppedClock());
            await vm.LoadAsync(Prn);
            Assert.NotNull(vm.Series);

            dialog.OnShow = p => p.CancelCommand.Execute(null);
            await vm.LoadAsync(Prn);
            ProcessingViewModel cancelled = dialog.Shown[^1];
            Assert.Equal("Cancelling...", cancelled.Text);
            Assert.True(cancelled.IsCancelling);
            Assert.False(cancelled.CancelCommand.CanExecute(null));
            Assert.Equal(2, dialog.Closed);
            Assert.Null(vm.Series);
            Assert.Empty(vm.AllSeries);
            Assert.False(vm.IsBusy);
            Assert.Equal("prn_testfile, sample.ch10: processing was cancelled.", vm.Status);
            List<string> lines = Texts(vm);
            int second = lines.LastIndexOf("Opening: prn_testfile, sample.ch10");
            Assert.DoesNotContain(lines.Skip(second), t => t.StartsWith("Processing complete", StringComparison.Ordinal));
            Assert.DoesNotContain(vm.Log.Entries.Skip(second), e => e.Level == LogLevel.Error);
        }

        // ---- The windows ------------------------------------------------------------------------------------------------

        private static T Shown<T>(T window) where T : Window
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return window;
        }

        [AvaloniaFact]
        public void TheProgressDialogShowsTheRunAndCancelOnlyAsks()
        {
            var vm = new ProcessingViewModel();
            ProcessingWindow w = Shown(new ProcessingWindow { DataContext = vm });
            Assert.Equal("Processing", w.Title);
            Assert.True(w.Bounds.Width >= 360);
            Assert.Equal("Processing...", w.FindControl<TextBlock>("StatusText")!.Text);
            ProgressBar bar = w.FindControl<ProgressBar>("Progress")!;
            Assert.Equal((0, 100, 0), (bar.Minimum, bar.Maximum, bar.Value));
            vm.Progress = 42;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(42, bar.Value);

            Button cancel = w.FindControl<Button>("CancelButton")!;
            Assert.True(cancel.IsEffectivelyEnabled);
            cancel.Command!.Execute(null);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("Cancelling...", w.FindControl<TextBlock>("StatusText")!.Text);
            Assert.False(cancel.IsEffectivelyEnabled);
            Assert.True(vm.Token.IsCancellationRequested);
            Assert.True(w.IsVisible);   // it stays until the run has stopped
            w.CloseWhenDone();
        }

        [AvaloniaFact]
        public void TheProgressDialogCannotBeClosedOrEscapedWhileTheRunGoesOn()
        {
            var vm = new ProcessingViewModel();
            ProcessingWindow w = Shown(new ProcessingWindow { DataContext = vm });
            w.Close();
            Dispatcher.UIThread.RunJobs();
            Assert.True(w.IsVisible);
            Button cancel = w.FindControl<Button>("CancelButton")!;
            Assert.False(cancel.IsCancel);
            cancel.Focus();
            w.KeyPress(Key.Escape, RawInputModifiers.None, PhysicalKey.Escape, null);
            Dispatcher.UIThread.RunJobs();
            Assert.True(w.IsVisible);
            Assert.False(vm.IsCancelling);

            w.CloseWhenDone();
            Dispatcher.UIThread.RunJobs();
            Assert.False(w.IsVisible);
        }

        [AvaloniaFact]
        public void TheProgressDialogIsShownOverTheMainWindowAndClosedWhenTold()
        {
            Window owner = Shown(new Window());
            var dialog = new WindowProcessingDialog(owner);
            var vm = new ProcessingViewModel();
            dialog.Show(vm);
            Dispatcher.UIThread.RunJobs();
            ProcessingWindow w = Assert.IsType<ProcessingWindow>(owner.OwnedWindows.Single());
            Assert.Same(vm, w.DataContext);
            Assert.True(w.IsVisible);
            dialog.Close();
            Dispatcher.UIThread.RunJobs();
            Assert.Empty(owner.OwnedWindows);
            Assert.False(w.IsVisible);
            owner.Close();
        }

        private static (LogView View, SelectableTextBlock Text, ScrollViewer Scroll, Window Window) ShowLog(IList<LogEntry> entries, double height = 300)
        {
            var view = new LogView { Entries = entries, Width = 400, Height = height };
            var window = new Window { Content = view, SizeToContent = SizeToContent.WidthAndHeight, Background = Brushes.White };
            // Greyscale text: subpixel antialiasing gives black glyphs coloured fringes, which a colour count would misread.
            TextOptions.SetTextRenderingMode(window, TextRenderingMode.Antialias);
            Shown(window);
            Dispatcher.UIThread.RunJobs();
            return (view, view.GetLogicalDescendants().OfType<SelectableTextBlock>().Single(),
                    view.GetLogicalDescendants().OfType<ScrollViewer>().Single(), window);
        }

        private static string Shown(SelectableTextBlock text) =>
            string.Concat(text.Inlines!.Select(i => i switch { Run r => r.Text, LineBreak => "\n", _ => "?" }));

        [AvaloniaFact]
        public void TheLogShowsEveryLineInItsLevelsColour()
        {
            var log = new SessionLog(new StoppedClock());
            log.Add("an ordinary line of information");
            log.Error("an error line, drawn red");
            log.Warning("a warning line, in goldenrod");
            log.Success("a success line, drawn green");
            var (_, text, _, window) = ShowLog(log.Entries);

            Assert.Equal(log.PlainText, Shown(text));
            Run[] runs = text.Inlines!.OfType<Run>().ToArray();
            // Information has no colour of its own: it inherits the text colour, which a theme sets.
            Assert.Equal(new IBrush?[] { text.Foreground, LogView.ErrorBrush, LogView.WarningBrush, LogView.SuccessBrush }, runs.Select(r => r.Foreground));
            Assert.False(runs[0].IsSet(TextElement.ForegroundProperty));

            // Drawn: each line's band holds its colour and none of the others.
            Rect block = Frames.InWindow(text, window);
            var lines = text.TextLayout.TextLines;
            Assert.Equal(4, lines.Count);
            Color red = Colors.Red, gold = Color.FromRgb(0xDA, 0xA5, 0x20), green = Color.FromRgb(0, 0x80, 0), black = Colors.Black;
            using ILockedFramebuffer fb = Frames.Capture(window).Lock();
            double top = block.Top;
            var seen = new List<(int Black, int Red, int Gold, int Green)>();
            foreach (var line in lines)
            {
                // The middle half of the line: a descender of the line above reaches into the top of its band.
                var band = new Rect(block.Left, top + line.Height / 4, block.Width, line.Height / 2);
                seen.Add((Frames.Near(fb, band, black), Frames.Near(fb, band, red), Frames.Near(fb, band, gold), Frames.Near(fb, band, green)));
                top += line.Height;
            }
            string all = string.Join("; ", seen);
            Assert.True(seen[0].Black > 30 && seen[0].Red + seen[0].Gold + seen[0].Green == 0, "information: " + all);
            Assert.True(seen[1].Red > 30 && seen[1].Black + seen[1].Gold + seen[1].Green == 0, "error: " + all);
            Assert.True(seen[2].Gold > 30 && seen[2].Black + seen[2].Red + seen[2].Green == 0, "warning: " + all);
            Assert.True(seen[3].Green > 30 && seen[3].Black + seen[3].Red + seen[3].Gold == 0, "success: " + all);
            window.Close();
        }

        [AvaloniaFact]
        public void AMultiLineMessageKeepsItsLinesAndTheirIndent()
        {
            // The Qt log once ran a multi-line report together into one paragraph (HTML collapses a newline and a run of
            // spaces); here each line of the message must be a line of the text, its indent kept.
            var log = new SessionLog(new StoppedClock());
            log.Error("first line\n  second line\n  third line");
            log.Add("next entry");
            var (_, text, _, window) = ShowLog(log.Entries);
            Assert.Equal("13:05:09  first line\n  second line\n  third line\n13:05:09  next entry", Shown(text));
            var lines = text.TextLayout.TextLines;
            Assert.Equal(4, lines.Count);
            window.Close();
        }

        [AvaloniaFact]
        public void TheLogFollowsTheNewestLine()
        {
            var log = new SessionLog(new StoppedClock());
            for (int i = 0; i < 60; i++) log.Add("line " + i);
            var (_, _, scroll, window) = ShowLog(log.Entries, height: 200);
            Assert.True(scroll.Extent.Height > scroll.Viewport.Height * 2, "the log does not overflow its box");
            Assert.Equal(scroll.Extent.Height - scroll.Viewport.Height, scroll.Offset.Y, 1);

            for (int i = 0; i < 20; i++) log.Add("more " + i);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(scroll.Extent.Height - scroll.Viewport.Height, scroll.Offset.Y, 1);
            window.Close();
        }

        [AvaloniaFact]
        public void TheMainWindowShowsTheLogBesideTheChart()
        {
            var vm = new MainWindowViewModel(clock: new StoppedClock());
            MainWindow window = Shown(new MainWindow { DataContext = vm });
            Control panel = window.FindControl<Control>("LogPanel")!;
            Control chart = window.FindControl<Control>("Chart")!;
            Rect p = Frames.InWindow(panel, window), c = Frames.InWindow(chart, window);
            Assert.True(p.Right <= c.Left, $"the log ({p}) is not left of the chart ({c})");
            Assert.InRange(p.Width, 400, 410);
            Assert.True(p.Height > window.ClientSize.Height * 0.8);

            SelectableTextBlock text = window.FindControl<LogView>("Log")!.GetLogicalDescendants().OfType<SelectableTextBlock>().Single();
            Assert.Equal(vm.Log.PlainText, Shown(text));
            vm.Log.Warning("added later");
            Dispatcher.UIThread.RunJobs();
            Assert.EndsWith("13:05:09  added later", Shown(text));
            window.Close();
        }
    }
}
