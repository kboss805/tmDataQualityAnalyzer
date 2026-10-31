using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using TmDq.App.Processing;
using TmDq.App.Streams;
using TmDq.App.ViewModels;
using TmDq.Core;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// Configure Streams without a window (plan step 4.1b), on the committed PRN sample (PCM channels 13 to 16, data on
    /// 13, PRN15, and 15, PRN11), and the whole path from Open through it to the chart: the rows, the recording's own
    /// settings, the gear and its dialog, Apply to all, Process, Cancel, and what a run of several streams shows.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class ConfigureStreamsTests
    {
        private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

        private static string Prn11 => Path.Combine(Repo.Root, "settings", "framesync_patterns", "framesync_PRN11.toml");

        public ConfigureStreamsTests() => NativeCore.EnsureLoaded();

        private static ConfigureStreamsViewModel Dialog(FakeStreamDialogs? dialogs = null, FakeFilePicker? files = null) =>
            new(Prn, StreamsRun.Open(Prn), dialogs ?? new FakeStreamDialogs(), files ?? new FakeFilePicker(), Repo.Root);

        [Fact]
        public void EveryPcmChannelIsARowAndNothingIsCheckedYet()
        {
            ConfigureStreamsViewModel vm = Dialog();
            Assert.Equal(new[] { "CH-01 PRN 15 5M", "CH-02 PRN 15 20M", "CH-03 PRN 11 1M", "CH-04 PRN 11 5M" }, vm.Rows.Select(r => r.Label));
            Assert.All(vm.Rows, r =>
            {
                Assert.False(r.Process);
                Assert.Equal(1, r.ModeIndex);   // Frame Sync Lock
                Assert.False(r.CanConfigure);
                Assert.Equal(Readiness.Idle, r.Readiness);
                Assert.Equal("✗", r.ReadyGlyph);
                Assert.False(r.IsBlocking);
            });
            Assert.Equal(new[] { 5.0, 20.0, 1.0, 5.0 }, vm.Rows.Select(r => r.Setup.TmatsDataRateMbps));
            Assert.Equal(new[] { "1 - IRIG-B" }, vm.TimeChannelItems);
            Assert.Equal((0, 1, true), (vm.TimeChannelIndex, vm.TimeChannelId, vm.HasTimeChannels));
            Assert.False(vm.LegacyChapter10);
            Assert.False(vm.CanProcess);
            Assert.False(vm.ProcessCommand.CanExecute(null));
            Assert.Equal("Check at least one stream to process.", vm.WhyNotProcess);
            Assert.Equal("Configure Streams", vm.Title);
        }

        [Fact]
        public void ACheckedRowWaitsForItsGear()
        {
            ConfigureStreamsViewModel vm = Dialog();
            StreamRowViewModel row = vm.Rows[0];
            row.Process = true;
            Assert.True(row.CanConfigure);
            Assert.True(row.ConfigureCommand.CanExecute(null));
            Assert.Equal(Readiness.NeedsConfiguration, row.Readiness);
            Assert.True(row.IsBlocking);
            Assert.Equal("Click the gear icon to configure this stream.", row.ReadyTip);
            Assert.False(vm.CanProcess);
            Assert.Equal("CH-01 PRN 15 5M: click the gear icon to configure this stream.", vm.WhyNotProcess);
        }

        [Fact]
        public async Task TheGearOpensTheRowsDialogAndOkMakesItReady()
        {
            var dialogs = new FakeStreamDialogs { OnEdit = FakeStreamDialogs.Ok(d => d.DataRateMbps = 4) };
            ConfigureStreamsViewModel vm = Dialog(dialogs);
            StreamRowViewModel row = vm.Rows[0];
            row.Process = true;
            await row.ConfigureCommand.ExecuteAsync(null);

            StreamSetupViewModel opened = Assert.Single(dialogs.Edited);
            Assert.Equal("Configure Frame Sync Lock — CH-01 PRN 15 5M", opened.Title);
            Assert.Equal(Readiness.Ready, row.Readiness);
            Assert.Equal("✓", row.ReadyGlyph);
            Assert.True(row.IsReady);
            Assert.Equal(4.0, row.Setup.DataRateMbps);
            Assert.True(vm.CanProcess);
            Assert.True(vm.ProcessCommand.CanExecute(null));
            Assert.Equal("", vm.WhyNotProcess);
        }

        [Fact]
        public async Task CancellingTheDialogChangesNothing()
        {
            var dialogs = new FakeStreamDialogs { OnEdit = d => { d.DataRateMbps = 4; d.CancelCommand.Execute(null); return Task.CompletedTask; } };
            ConfigureStreamsViewModel vm = Dialog(dialogs);
            vm.Rows[0].Process = true;
            StreamSetup before = vm.Rows[0].Setup;
            await vm.Rows[0].ConfigureCommand.ExecuteAsync(null);
            Assert.Same(before, vm.Rows[0].Setup);
        }

        [Fact]
        public async Task AnUncheckedRowsGearDoesNothing()
        {
            var dialogs = new FakeStreamDialogs { OnEdit = FakeStreamDialogs.Ok() };
            ConfigureStreamsViewModel vm = Dialog(dialogs);
            await vm.ConfigureAsync(vm.Rows[0]);
            Assert.Empty(dialogs.Edited);
            Assert.False(vm.Rows[0].Setup.Configured);
        }

        [Fact]
        public async Task TheDialogOpensOnTheOtherModesDefaultsAfterAModeChange()
        {
            var dialogs = new FakeStreamDialogs { OnEdit = FakeStreamDialogs.Ok() };
            ConfigureStreamsViewModel vm = Dialog(dialogs);
            StreamRowViewModel row = vm.Rows[0];
            row.Process = true;
            await row.ConfigureCommand.ExecuteAsync(null);
            Assert.True(row.IsReady);

            row.ModeIndex = 0;   // Receiver SNR
            Assert.Equal(Readiness.NeedsConfiguration, row.Readiness);
            await row.ConfigureCommand.ExecuteAsync(null);
            StreamSetupViewModel snr = dialogs.Edited[1];
            Assert.True(snr.IsReceiverSnr);
            // The shipped Receiver SNR defaults file, read from the application root.
            Assert.Equal(("FE6B2840", "FFFFFFFF", 800m), (snr.SyncPattern, snr.SyncMask, snr.BitsInMinorFrame));
            Assert.True(row.IsReady);
            Assert.Equal(StreamMode.ReceiverChannelInfo, row.Setup.SettingsMode);
        }

        [Fact]
        public async Task ApplyToAllCopiesToTheOtherCheckedRowsOfTheMode()
        {
            var dialogs = new FakeStreamDialogs { OnEdit = FakeStreamDialogs.Ok(d => { d.SyncPattern = "A345CA5C"; d.ApplyToAll = true; }) };
            ConfigureStreamsViewModel vm = Dialog(dialogs);
            vm.Rows[0].Process = true;
            vm.Rows[1].Process = true;
            vm.Rows[2].Process = true;
            vm.Rows[2].ModeIndex = 0;   // Receiver SNR: another mode
            await vm.Rows[0].ConfigureCommand.ExecuteAsync(null);
            Assert.Equal(new[] { "A345CA5C", "A345CA5C", "334AABBF", "334AABBF" }, vm.Rows.Select(r => r.Setup.SyncPattern));
            Assert.Equal(new[] { true, true, false, false }, vm.Rows.Select(r => r.IsReady));
        }

        [Fact]
        public void TheAllBoxSetsEveryRow()
        {
            ConfigureStreamsViewModel vm = Dialog();
            vm.All = true;
            Assert.All(vm.Rows, r => Assert.True(r.Process));
            vm.Rows[1].Process = false;
            Assert.True(vm.All);   // one-way, as in the Qt dialog
            vm.All = false;
            Assert.All(vm.Rows, r => Assert.False(r.Process));
        }

        [Fact]
        public async Task TheRunIsTheCheckedRowsWithTheRecordingsSettings()
        {
            var dialogs = new FakeStreamDialogs { OnEdit = FakeStreamDialogs.Ok() };
            ConfigureStreamsViewModel vm = Dialog(dialogs);
            vm.Rows[2].Process = true;
            await vm.Rows[2].ConfigureCommand.ExecuteAsync(null);
            vm.LegacyChapter10 = true;
            RunRequest run = vm.ToRunRequest();
            Assert.Equal((Prn, 1, false, Repo.Root), (run.FileName, run.TimeChannelId, run.SwapBytes, run.AppRoot));
            Assert.Equal(new[] { 15 }, run.Streams.Select(s => s.PcmChannelId));
        }

        [Fact]
        public void ProcessAndCancelCloseTheDialog()
        {
            ConfigureStreamsViewModel vm = Dialog();
            int closed = 0;
            vm.CloseRequested += (_, _) => closed++;
            vm.ProcessCommand.Execute(null);   // nothing checked: refused
            Assert.Equal((0, false), (closed, vm.Accepted));
            vm.CancelCommand.Execute(null);
            Assert.Equal((1, false), (closed, vm.Accepted));
        }

        [Fact]
        public async Task TheLastFolderCarriesFromOneDialogToTheNext()
        {
            var files = new FakeFilePicker(Prn11, null);
            int edits = 0;
            var dialogs = new FakeStreamDialogs
            {
                OnEdit = async d =>
                {
                    if (edits++ == 0) await d.LoadFrameSyncCommand.ExecuteAsync(null);
                    else await d.SaveFrameSyncCommand.ExecuteAsync(null);
                    d.OkCommand.Execute(null);
                },
            };
            ConfigureStreamsViewModel vm = Dialog(dialogs, files);
            vm.Rows[0].Process = true;
            await vm.Rows[0].ConfigureCommand.ExecuteAsync(null);
            await vm.Rows[0].ConfigureCommand.ExecuteAsync(null);
            Assert.Equal(Path.GetDirectoryName(Prn11), files.Asked[1].Folder);
        }

        // --- the whole path ---

        [Fact]
        public async Task TwoStreamsConfiguredAndProcessedAreBothDrawn()
        {
            var files = new FakeFilePicker(Prn11);
            var dialogs = new FakeStreamDialogs();
            dialogs.OnConfigure = async c =>
            {
                c.Rows[0].Process = true;   // CH-01, PRN15: the defaults
                dialogs.OnEdit = FakeStreamDialogs.Ok();
                await c.Rows[0].ConfigureCommand.ExecuteAsync(null);
                c.Rows[2].Process = true;   // CH-03, PRN11: loaded from the shipped pattern
                dialogs.OnEdit = async d => { await d.LoadFrameSyncCommand.ExecuteAsync(null); d.OkCommand.Execute(null); };
                await c.Rows[2].ConfigureCommand.ExecuteAsync(null);
                c.ProcessCommand.Execute(null);
            };
            var vm = new MainWindowViewModel(configurator: new DialogConfigurator(dialogs, files, Repo.Root));
            await vm.LoadAsync(Prn);

            Assert.Equal(new[] { 13, 15 }, vm.AllSeries.Select(s => s.PcmChannelId));
            Assert.Same(vm.AllSeries[0], vm.Series);
            Assert.Equal(116, vm.Series!.LockPercent.Length);   // channel 13 as the walking skeleton ran it
            Assert.True(vm.AllSeries[1].MeanLock > 50, $"PRN11 should lock with its own pattern (mean {vm.AllSeries[1].MeanLock})");
            Assert.Equal("prn_testfile, sample.ch10: 2 streams processed (2 Frame Sync Lock, 0 Receiver SNR)", vm.Status);
            Assert.False(vm.IsBusy);
        }

        [Fact]
        public async Task CancellingConfigureStreamsKeepsTheLastResult()
        {
            var dialogs = new FakeStreamDialogs();
            var vm = new MainWindowViewModel(configurator: new DialogConfigurator(dialogs, null, Repo.Root));
            dialogs.OnConfigure = async c =>
            {
                c.Rows[0].Process = true;
                dialogs.OnEdit = FakeStreamDialogs.Ok();
                await c.Rows[0].ConfigureCommand.ExecuteAsync(null);
                c.ProcessCommand.Execute(null);
            };
            await vm.LoadAsync(Prn);
            LockSeries? shown = vm.Series;
            Assert.NotNull(shown);

            dialogs.OnConfigure = c => { c.CancelCommand.Execute(null); return Task.CompletedTask; };
            await vm.LoadAsync(Prn);
            Assert.Same(shown, vm.Series);
            Assert.Equal("prn_testfile, sample.ch10: not processed (Configure Streams was cancelled).", vm.Status);
            Assert.False(vm.IsBusy);
            Assert.Equal(2, dialogs.ConfigureCalls);
        }

        [Fact]
        public async Task AReceiverSnrStreamIsProcessedAndCounted()
        {
            var dialogs = new FakeStreamDialogs();
            dialogs.OnConfigure = async c =>
            {
                c.Rows[0].Process = true;
                c.Rows[0].ModeIndex = 0;
                dialogs.OnEdit = FakeStreamDialogs.Ok();
                await c.Rows[0].ConfigureCommand.ExecuteAsync(null);
                c.ProcessCommand.Execute(null);
            };
            var vm = new MainWindowViewModel(configurator: new DialogConfigurator(dialogs, null, Repo.Root));
            await vm.LoadAsync(Repo.Recording("step_cal, example.ch10"));
            Assert.Null(vm.Series);
            Assert.Empty(vm.AllSeries);
            Assert.Equal("step_cal, example.ch10: 1 stream processed (0 Frame Sync Lock, 1 Receiver SNR)", vm.Status);
        }

        [Fact]
        public void TheStatusNamesWhatWasNotProcessed()
        {
            var lockStream = new ProcessedStream(13, "A", 0, 0, StreamMode.FrameSyncLock, new[] { 0.0 }, new[] { 50.0 }, new[] { 0.0 }, Array.Empty<ChannelData>());
            var snr = lockStream with { Mode = StreamMode.ReceiverChannelInfo, PcmChannelId = 14 };
            Assert.Equal("f.ch10: A (channel 13), 1 samples, mean lock 50.0 %",
                         MainWindowViewModel.Describe("f.ch10", new RunOutcome(new[] { lockStream }, Array.Empty<string>())));
            Assert.Equal("f.ch10: 2 streams processed (1 Frame Sync Lock, 1 Receiver SNR). Not processed: B: no sync.",
                         MainWindowViewModel.Describe("f.ch10", new RunOutcome(new[] { lockStream, snr }, new[] { "B: no sync." })));
        }
    }
}
