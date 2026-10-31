using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Platform;
using Avalonia.Threading;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The walking skeleton (plan step 3.4): open a recording, process its first PCM channel with the Frame Sync Lock
    /// defaults through the native core, and show the lock percentage, end to end. The committed PRN sample's first PCM
    /// channel is 13, "CH-01 PRN 15 5M", whose numbers with these settings the managed tests already pin (116 one-second
    /// windows, mean 93.531893 %), so the app is held to a known answer, and to the core's own result for the same run.
    /// One class on purpose: a test here points TMDQ_CORE_DLL elsewhere, and xunit runs a class's tests one at a time.
    /// </summary>
    public sealed class WalkingSkeletonTests
    {
        private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

        private sealed class FakePicker : IRecordingPicker
        {
            private readonly string? _answer;
            public FakePicker(string? answer) => _answer = answer;
            public int Calls { get; private set; }
            public Task<string?> PickRecordingAsync() { Calls++; return Task.FromResult(_answer); }
        }

        [AvaloniaFact]
        public async Task OpeningTheSampleShowsItsFirstChannelsLockPercentage()
        {
            var vm = new MainWindowViewModel();
            await vm.LoadAsync(Prn);

            LockSeries s = Assert.IsType<LockSeries>(vm.Series);
            Assert.Equal("CH-01 PRN 15 5M", s.Label);
            Assert.Equal(13, s.PcmChannelId);
            Assert.Equal(116, s.LockPercent.Length);
            Assert.Equal(116, s.TimesSec.Length);
            Assert.Equal(93.531893, s.MeanLock, 6);
            Assert.Equal(0.0, s.LockPercent.Min());
            Assert.Equal(100.0, s.LockPercent.Max());
            Assert.Equal(100, vm.Progress);
            Assert.False(vm.IsBusy);
            string mean = s.MeanLock.ToString("F1", CultureInfo.CurrentCulture);
            Assert.Equal($"prn_testfile, sample.ch10: CH-01 PRN 15 5M (channel 13), 116 samples, mean lock {mean} %", vm.Status);
        }

        [AvaloniaFact]
        public async Task TheSeriesIsExactlyWhatTheCoreMakesForTheSameRun()
        {
            var vm = new MainWindowViewModel();
            await vm.LoadAsync(Prn);
            LockSeries s = vm.Series!;

            // The same run straight through TmDq.Core, with the settings written out here rather than taken from the app.
            NativeCore.EnsureLoaded();
            Ch10File file = Ch10File.Open(Prn);
            var config = new StreamConfig
            {
                PcmChannelId = 13, Label = "CH-01 PRN 15 5M", Mode = StreamMode.FrameSyncLock, SyncPattern = "334AABBF",
                SyncMask = "FFFFFFFF", BitsInMinorFrame = 32767, SamplePeriod = SamplePeriod.OneSecond,
            };
            var collector = new Collector();
            using (ProcessingRun run = ProcessingRun.Start(new RunRequest(Prn, file.DefaultTimeChannel, new[] { config }), collector))
                run.Wait();
            ProcessedStream direct = Assert.Single(collector.Results);
            Assert.Equal(direct.TimesSec, s.TimesSec);
            Assert.Equal(direct.LockPercent, s.LockPercent);
        }

        private sealed class Collector : IRunObserver
        {
            public List<ProcessedStream> Results { get; } = new();
            public void OnStreamCompleted(StreamResult result) { using (result) lock (Results) Results.Add(result.ToProcessed()); }
        }

        [AvaloniaFact]
        public async Task ProgressRisesToOneHundredAndBusyIsSetForTheRunOnly()
        {
            var vm = new MainWindowViewModel();
            var progress = new List<int>();
            var busy = new List<bool>();
            vm.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(MainWindowViewModel.Progress)) progress.Add(vm.Progress);
                if (e.PropertyName == nameof(MainWindowViewModel.IsBusy)) busy.Add(vm.IsBusy);
            };
            await vm.LoadAsync(Prn);

            Assert.Equal(new[] { true, false }, busy);
            Assert.NotEmpty(progress);
            Assert.Equal(100, progress[^1]);
            Assert.True(progress.Zip(progress.Skip(1)).All(p => p.First <= p.Second), "progress went backwards: " + string.Join(",", progress));
            Assert.All(progress, p => Assert.InRange(p, 0, 100));
        }

        [AvaloniaFact]
        public async Task AFileThatIsNotARecordingSaysSoAndShowsNothing()
        {
            string junk = Path.Combine(Path.GetTempPath(), "tmdq-app-junk-" + Guid.NewGuid().ToString("N") + ".ch10");
            File.WriteAllBytes(junk, new byte[4096]);
            try
            {
                var vm = new MainWindowViewModel();
                await vm.LoadAsync(junk);
                Assert.Null(vm.Series);
                Assert.False(vm.IsBusy);
                Assert.StartsWith(Path.GetFileName(junk) + " could not be opened: ", vm.Status);
            }
            finally
            {
                File.Delete(junk);
            }
        }

        [AvaloniaFact]
        public async Task DefaultsThatFindNoSyncSayWhichStreamAndShowNothing()
        {
            // step_cal's PCM channel is a Receiver SNR stream (FE6B2840, 800-bit frames): the PRN15 defaults find nothing.
            var vm = new MainWindowViewModel();
            await vm.LoadAsync(Repo.Recording("step_cal, example.ch10"));
            Assert.Null(vm.Series);
            Assert.False(vm.IsBusy);
            Assert.Matches(@"\(channel 26\): \S", vm.Status);
        }

        [AvaloniaFact]
        public async Task AMissingNativeCoreIsReportedNotThrown()
        {
            string? before = Environment.GetEnvironmentVariable("TMDQ_CORE_DLL");
            Environment.SetEnvironmentVariable("TMDQ_CORE_DLL", Path.Combine(Path.GetTempPath(), "tmdq-no-such-core", "tmdq_core.dll"));
            try
            {
                var vm = new MainWindowViewModel();
                await vm.LoadAsync(Prn);
                Assert.Null(vm.Series);
                Assert.False(vm.IsBusy);
                Assert.StartsWith("The processing core could not be loaded: ", vm.Status);
            }
            finally
            {
                Environment.SetEnvironmentVariable("TMDQ_CORE_DLL", before);
            }
        }

        [AvaloniaFact]
        public async Task OpenAsksThePickerAndACancelChangesNothing()
        {
            var picker = new FakePicker(null);
            var vm = new MainWindowViewModel(picker);
            string status = vm.Status;
            await vm.OpenCommand.ExecuteAsync(null);
            Assert.Equal(1, picker.Calls);
            Assert.Null(vm.Series);
            Assert.Equal(status, vm.Status);
        }

        [AvaloniaFact]
        public async Task OpenProcessesTheChosenRecording()
        {
            var vm = new MainWindowViewModel(new FakePicker(Prn));
            await vm.OpenCommand.ExecuteAsync(null);
            Assert.Equal(116, vm.Series?.LockPercent.Length);
        }

        [AvaloniaFact]
        public async Task OpenIsDisabledWhileARecordingIsProcessedAndASecondLoadIsIgnored()
        {
            var vm = new MainWindowViewModel(new FakePicker(Prn));
            Assert.True(vm.OpenCommand.CanExecute(null));
            Task first = vm.LoadAsync(Prn);
            Assert.True(vm.IsBusy);
            Assert.False(vm.OpenCommand.CanExecute(null));
            await vm.LoadAsync(Repo.Recording("step_cal, example.ch10"));   // ignored: returns at once
            Assert.True(vm.IsBusy);
            await first;
            Assert.True(vm.OpenCommand.CanExecute(null));
            Assert.Equal(13, vm.Series?.PcmChannelId);
        }

        [AvaloniaFact]
        public void WithoutAPickerOpenIsUnavailable()
        {
            Assert.False(new MainWindowViewModel().OpenCommand.CanExecute(null));
        }

        [AvaloniaFact]
        public async Task TheChartDrawsTheSeriesOnceItIsProcessed()
        {
            var vm = new MainWindowViewModel();
            var window = new MainWindow { DataContext = vm };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            LockChart chart = window.FindControl<LockChart>("Chart")!;
            Rect area = Frames.InWindow(chart, window);
            Rect plot = area.Deflate(LockChart.PlotInset + 2);

            int before;
            uint background;
            using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
            {
                background = Frames.Pixel(fb, (int)area.Center.X, (int)area.Center.Y);
                before = Frames.CountDifferent(fb, plot, background);
            }

            await vm.LoadAsync(Prn);
            Dispatcher.UIThread.RunJobs();
            Assert.Same(vm.Series, chart.Series);
            int after;
            using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                after = Frames.CountDifferent(fb, plot, background);

            Assert.Equal(0, before);
            // 116 points across the plot: a line hundreds of pixels long.
            Assert.True(after > 500, $"only {after} pixels of the plot were drawn");
            window.Close();
        }

        [AvaloniaFact]
        public void TheChartDrawsNothingWithoutASeriesOrWithOnePoint()
        {
            var chart = new LockChart { Width = 400, Height = 300 };
            var window = new Window { Content = chart, SizeToContent = SizeToContent.WidthAndHeight };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Rect area = Frames.InWindow(chart, window);
            uint background;
            using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
            {
                background = Frames.Pixel(fb, 1, 1);
                Assert.Equal(0, Frames.CountDifferent(fb, area, background));
            }
            chart.Series = new LockSeries("one", 1, new[] { 5.0 }, new[] { 50.0 });
            Dispatcher.UIThread.RunJobs();
            using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                Assert.Equal(0, Frames.CountDifferent(fb, area, background));
            window.Close();
        }

        [Fact]
        public void TheDefaultsAreTheQtApplicationsFromTheHeader()
        {
            string header = File.ReadAllText(Repo.File(Path.Combine("include", "constants.h")));
            string Text(string name) => Regex.Match(header, name + @"\s*=\s*""([0-9A-Fa-f]+)""").Groups[1].Value;
            int Number(string name) => int.Parse(Regex.Match(header, @"int\s+" + name + @"\s*=\s*(\d+)\s*;").Groups[1].Value, CultureInfo.InvariantCulture);
            Assert.Equal(FrameSyncDefaults.Pattern, Text("kDefaultFrameSyncLockPattern"));
            Assert.Equal(FrameSyncDefaults.Mask, Text("kDefaultFrameSyncMask"));
            Assert.Equal(FrameSyncDefaults.BitsInMinorFrame, Number("kDefaultFrameSyncLockBits"));
        }

        [Fact]
        public void TheDefaultStreamCarriesTheDefaultsAndTheChannel()
        {
            StreamConfig c = FirstChannelRun.DefaultStream(new ChannelInfo(13, "CH-01 PRN 15 5M", 5_000_000));
            Assert.Equal(13, c.PcmChannelId);
            Assert.Equal("CH-01 PRN 15 5M", c.Label);
            Assert.Equal(StreamMode.FrameSyncLock, c.Mode);
            Assert.Equal("334AABBF", c.SyncPattern);
            Assert.Equal("FFFFFFFF", c.SyncMask);
            Assert.Equal(32767, c.BitsInMinorFrame);
            Assert.Equal(SamplePeriod.OneSecond, c.SamplePeriod);
            Assert.False(c.Randomized);
            Assert.False(c.Inverted);
            Assert.Equal(0.0, c.DataRateMbps);   // the rate TMATS declares
        }

        [Theory]
        [InlineData(new string[0], null)]
        [InlineData(new[] { @"C:\rec\a.ch10" }, @"C:\rec\a.ch10")]
        [InlineData(new[] { "--verbose", @"C:\rec\a.ch10", "b.ch10" }, @"C:\rec\a.ch10")]
        [InlineData(new[] { "", "  ", "-x" }, null)]
        public void TheCommandLinesFirstNonOptionIsTheRecording(string[] args, string? expected)
        {
            Assert.Equal(expected, Startup.RecordingFromArguments(args));
        }

        [Fact]
        public void TheSmokeReportSaysWhatHappenedInOneLine()
        {
            var vm = new MainWindowViewModel();
            Assert.Equal("failed | Open a Chapter 10 recording.", Startup.SmokeReport(vm));
        }

        [AvaloniaFact]
        public async Task TheSmokeReportOfAProcessedRecordingCarriesItsNumbers()
        {
            var vm = new MainWindowViewModel();
            await vm.LoadAsync(Prn);
            string report = Startup.SmokeReport(vm);
            Assert.StartsWith("ok samples=116 mean=93.532 channel=13 | prn_testfile, sample.ch10: ", report);
            string file = Path.Combine(Path.GetTempPath(), "tmdq-smoke-" + Guid.NewGuid().ToString("N") + ".txt");
            try
            {
                Startup.WriteSmokeReport(file, vm);
                Assert.Equal(report + Environment.NewLine, File.ReadAllText(file));
            }
            finally
            {
                File.Delete(file);
            }
        }
    }
}
