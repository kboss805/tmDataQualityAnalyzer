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
    /// One class on purpose: a test here points TMDQ_CORE_DLL elsewhere, and xunit runs a class's tests one at a time
    /// (and the classes of <see cref="NativeCoreCollection"/> one at a time with it).
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
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
        public async Task DefaultsThatFindNoSyncSayWhichStreamAndWhyAndShowNothing()
        {
            // step_cal's PCM channel is a Receiver SNR stream (FE6B2840, 800-bit frames): the PRN15 defaults find nothing.
            string stepCal = Repo.Recording("step_cal, example.ch10");
            var vm = new MainWindowViewModel();
            await vm.LoadAsync(stepCal);
            Assert.Null(vm.Series);
            Assert.False(vm.IsBusy);

            // The reason is the core's own, as the core gives it for the same run.
            NativeCore.EnsureLoaded();
            Ch10File file = Ch10File.Open(stepCal);
            var errors = new ErrorCollector();
            using (ProcessingRun run = ProcessingRun.Start(new RunRequest(stepCal, file.DefaultTimeChannel,
                       new[] { FirstChannelRun.DefaultStream(file.PcmChannels[0]) }), errors))
                run.Wait();
            Assert.NotEmpty(errors.Errors);
            Assert.Equal($"{file.PcmChannels[0].Name} (channel 26): {string.Join(" ", errors.Errors)}", vm.Status);
        }

        private sealed class ErrorCollector : IRunObserver
        {
            public List<string> Errors { get; } = new();
            public void OnError(string message) { lock (Errors) Errors.Add(message); }
        }

        [AvaloniaFact]
        public async Task ARecordingWithNoPcmChannelSaysSo()
        {
            // step_cal with its one PCM channel disabled in TMATS (R-1\CHE-27:T -> F), a same-length edit, so no packet
            // length or checksum moves (the native reader's tests make the same edit). The file still opens: it simply
            // offers nothing to process, which must be said, not crash the app on an empty channel list.
            byte[] bytes = File.ReadAllBytes(Repo.Recording("step_cal, example.ch10"));
            byte[] from = System.Text.Encoding.ASCII.GetBytes(@"R-1\CHE-27:T;"), to = System.Text.Encoding.ASCII.GetBytes(@"R-1\CHE-27:F;");
            int at = IndexOf(bytes, from);
            Assert.True(at >= 0 && IndexOf(bytes, from, at + 1) < 0, "the channel-enabled key is not in the recording exactly once");
            to.CopyTo(bytes, at);
            string copy = Path.Combine(Path.GetTempPath(), "tmdq-app-nopcm-" + Guid.NewGuid().ToString("N") + ".ch10");
            File.WriteAllBytes(copy, bytes);
            try
            {
                NativeCore.EnsureLoaded();
                Assert.Empty(Ch10File.Open(copy).PcmChannels);
                var vm = new MainWindowViewModel();
                await vm.LoadAsync(copy);
                Assert.Null(vm.Series);
                Assert.False(vm.IsBusy);
                Assert.Equal(Path.GetFileName(copy) + " has no PCM channel to process.", vm.Status);
            }
            finally
            {
                File.Delete(copy);
            }
        }

        private static int IndexOf(byte[] haystack, byte[] needle, int start = 0)
        {
            for (int i = start; i <= haystack.Length - needle.Length; i++)
            {
                int j = 0;
                while (j < needle.Length && haystack[i + j] == needle[j]) j++;
                if (j == needle.Length) return i;
            }
            return -1;
        }

        [AvaloniaFact]
        public async Task AFailedLoadClearsThePreviousRecordingsSeries()
        {
            // Nothing of the last recording may stay on screen under a status that says this one failed.
            var vm = new MainWindowViewModel();
            await vm.LoadAsync(Prn);
            Assert.NotNull(vm.Series);
            await vm.LoadAsync(Repo.Recording("step_cal, example.ch10"));
            Assert.Null(vm.Series);
        }

        [Fact]
        public async Task TheRunReportsTheCoresProgressToTheCaller()
        {
            // A recorder that keeps every report as it arrives (Progress<T> would post them to a context instead).
            var reports = new List<int>();
            var recorder = new SyncProgress(p => { lock (reports) reports.Add(p); });
            LockSeries s = await FirstChannelRun.RunAsync(Prn, recorder);
            Assert.Equal(116, s.LockPercent.Length);
            // The core's reader reports 100 once at the end of every run; the view model sets 100 itself as well, so
            // only this shows the core's reports reach the caller.
            Assert.Contains(100, reports);
            Assert.All(reports, p => Assert.InRange(p, 0, 100));
        }

        private sealed class SyncProgress : IProgress<int>
        {
            private readonly Action<int> _report;
            public SyncProgress(Action<int> report) => _report = report;
            public void Report(int value) => _report(value);
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
                Assert.Equal((vm.Status, Logging.LogLevel.Error), (vm.Log.Entries[^1].Text, vm.Log.Entries[^1].Level));
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
            PlotChart chart = window.FindControl<PlotChart>("Chart")!;
            Assert.Same(vm.Plot, chart.Plot);
            Rect area = Frames.InWindow(chart, window);
            Rect plot = new Rect(area.X + chart.PlotArea.X + 2, area.Y + chart.PlotArea.Y + 2, chart.PlotArea.Width - 4, chart.PlotArea.Height - 4);

            int before;
            uint background;
            using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
            {
                background = Frames.Pixel(fb, (int)plot.Center.X, (int)plot.Center.Y);
                before = Frames.CountDifferent(fb, plot, background);
            }

            await vm.LoadAsync(Prn);
            Dispatcher.UIThread.RunJobs();
            Assert.True(vm.Plot.HasData);
            Assert.Equal("prn_testfile, sample", vm.Plot.Title);
            int after;
            using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                after = Frames.CountDifferent(fb, plot, background);

            // An empty chart is a frame with nothing inside; 116 points across the plot are a line hundreds of pixels long.
            Assert.Equal(0, before);
            Assert.True(after > 500, $"only {after} pixels of the plot were drawn");
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
