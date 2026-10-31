using System;
using System.IO;
using System.Threading.Tasks;
using TmDq.App.Processing;
using TmDq.App.Streams;
using TmDq.App.ViewModels;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// A stream's setup dialog without a window (plan step 4.1b): what it opens with in each mode, the problems that
    /// stop OK as they arise (US7.0), OK and Cancel, and its Load and Save buttons through the core, with the folders
    /// the file dialogs start in.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class StreamSetupViewModelTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-setupvm-" + Guid.NewGuid().ToString("N"));

        public StreamSetupViewModelTests()
        {
            NativeCore.EnsureLoaded();
            Directory.CreateDirectory(_dir);
        }

        public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);

        private static StreamSetup Row(StreamMode mode = StreamMode.FrameSyncLock) =>
            new() { PcmChannelId = 13, Label = "CH-01 PRN 15 5M", Mode = mode, SettingsMode = mode, Process = true, TmatsDataRateMbps = 5 };

        private static StreamSetupViewModel Dialog(StreamSetup s, FakeFilePicker? files = null, string? appRoot = null, string? lastFolder = null) =>
            new(s, files ?? new FakeFilePicker(), appRoot, lastFolder);

        [Fact]
        public void TheDialogOpensWithTheRowAndIsNamedForItsMode()
        {
            StreamSetup s = Row() with { SyncPattern = "A345CA5C", SyncMask = "FFFF0000", BitsInMinorFrame = 1600, DataRateMbps = 2.5,
                                         SamplePeriod = SamplePeriod.TenMilliseconds, Randomized = true, Inverted = true };
            StreamSetupViewModel vm = Dialog(s);
            Assert.Equal(("A345CA5C", "FFFF0000", 1600m, 2.5m, 2, true, true),
                         (vm.SyncPattern, vm.SyncMask, vm.BitsInMinorFrame, vm.DataRateMbps, vm.PeriodIndex, vm.Randomized, vm.Inverted));
            Assert.Equal("Configure Frame Sync Lock \u2014 CH-01 PRN 15 5M", vm.Title);
            Assert.Equal("Apply to all Frame Sync Lock streams", vm.ApplyToAllText);
            Assert.False(vm.IsReceiverSnr);
            Assert.Equal("e.g. A345CA5C", vm.SyncPlaceholder);
            Assert.True(vm.CanAccept);
            Assert.Equal(s, vm.ToSetup());

            StreamSetupViewModel snr = Dialog(Row(StreamMode.ReceiverChannelInfo) with { PolarityIndex = 1, SlopeIndex = 2, ScaleDdBPerV = 41.5, NumReceivers = 7, ReceiverChannels = 5 });
            Assert.Equal("Configure Receiver SNR \u2014 CH-01 PRN 15 5M", snr.Title);
            Assert.Equal("Apply to all Receiver SNR streams", snr.ApplyToAllText);
            Assert.Equal("Copy these settings to every other selected stream currently set to Receiver SNR mode.", snr.ApplyToAllTip);
            Assert.True(snr.IsReceiverSnr);
            Assert.Equal("e.g. FE6B2840", snr.SyncPlaceholder);
            Assert.Equal((1, 2, 41.5m, 7m, 5m), (snr.PolarityIndex, snr.SlopeIndex, snr.ScaleDdBPerV, snr.NumReceivers, snr.ReceiverChannels));
            Assert.Equal("(default word map)", snr.ReceiverParamsName);
        }

        [Fact]
        public void ADataRateOfZeroSaysWhatItMeans()
        {
            Assert.Equal("0 = TMATS (5.000 Mbps)", Dialog(Row()).DataRateHint);
            Assert.Equal("0 = TMATS", Dialog(Row() with { TmatsDataRateMbps = 0 }).DataRateHint);
        }

        [Fact]
        public void TheProblemsFollowTheFieldsAndHoldOk()
        {
            StreamSetupViewModel vm = Dialog(Row());
            vm.SyncPattern = "";
            Assert.Equal(new[] { StreamRules.PatternRequired }, vm.Problems);
            Assert.False(vm.CanAccept);
            Assert.False(vm.OkCommand.CanExecute(null));
            Assert.Equal(StreamRules.PatternRequired, vm.ProblemText);

            vm.SyncPattern = "fe6b28g0";
            Assert.Equal(new[] { "The frame sync pattern must be 1 to 16 hexadecimal digits." }, vm.Problems);
            vm.SyncPattern = "fe6b2840";
            Assert.True(vm.CanAccept);
            Assert.True(vm.OkCommand.CanExecute(null));

            vm.BitsInMinorFrame = null;
            vm.DataRateMbps = null;
            Assert.Equal(new[] { "Bits Per Frame is empty.", "Data Rate is empty." }, vm.Problems);
            vm.BitsInMinorFrame = 800;
            vm.DataRateMbps = 0;
            Assert.Empty(vm.Problems);
        }

        [Fact]
        public void TheReceiverFieldsAreCheckedOnlyForReceiverSnr()
        {
            StreamSetupViewModel lockDialog = Dialog(Row());
            lockDialog.ScaleDdBPerV = null;
            lockDialog.NumReceivers = 0;
            Assert.Empty(lockDialog.Problems);

            StreamSetupViewModel snr = Dialog(Row(StreamMode.ReceiverChannelInfo));
            snr.ScaleDdBPerV = null;
            snr.NumReceivers = null;
            snr.ReceiverChannels = null;
            Assert.Equal(new[] { "Scale (dB/V) is empty.", "Num Rcvrs is empty.", "Num Channels is empty." }, snr.Problems);
            snr.ScaleDdBPerV = 20;
            snr.NumReceivers = 101;
            snr.ReceiverChannels = 3;
            Assert.Equal(new[] { "Num Rcvrs must be from 1 to 100." }, snr.Problems);
        }

        [Fact]
        public void OkAcceptsAndCancelDoesNot()
        {
            StreamSetupViewModel vm = Dialog(Row());
            int closed = 0;
            vm.CloseRequested += (_, _) => closed++;
            vm.SyncPattern = "";
            vm.OkCommand.Execute(null);
            Assert.Equal((0, false), (closed, vm.Accepted));
            vm.CancelCommand.Execute(null);
            Assert.Equal((1, false), (closed, vm.Accepted));

            StreamSetupViewModel ok = Dialog(Row());
            ok.CloseRequested += (_, _) => closed++;
            ok.OkCommand.Execute(null);
            Assert.Equal((2, true), (closed, ok.Accepted));
        }

        [Fact]
        public void WhatTheDialogHoldsIsTheSetupItGivesBack()
        {
            StreamSetupViewModel vm = Dialog(Row(StreamMode.ReceiverChannelInfo));
            vm.SyncPattern = "FE6B2840";
            vm.SyncMask = "FFFF0000";
            vm.BitsInMinorFrame = 800;
            vm.DataRateMbps = 0.8m;
            vm.PeriodIndex = 1;
            vm.Randomized = true;
            vm.Inverted = true;
            vm.PolarityIndex = 1;
            vm.SlopeIndex = 0;
            vm.ScaleDdBPerV = 12.5m;
            vm.NumReceivers = 12;
            vm.ReceiverChannels = 4;
            Assert.Equal(Row(StreamMode.ReceiverChannelInfo) with
            {
                SyncPattern = "FE6B2840", SyncMask = "FFFF0000", BitsInMinorFrame = 800, DataRateMbps = 0.8,
                SamplePeriod = SamplePeriod.HundredMilliseconds, Randomized = true, Inverted = true,
                PolarityIndex = 1, SlopeIndex = 0, ScaleDdBPerV = 12.5, NumReceivers = 12, ReceiverChannels = 4,
            }, vm.ToSetup());
        }

        // --- Load and Save ---

        private string AppRootWithSettings(params string[] subfolders)
        {
            string root = Path.Combine(_dir, "app");
            foreach (string sub in subfolders) Directory.CreateDirectory(Path.Combine(root, "settings", sub));
            return root;
        }

        [Fact]
        public async Task LoadStartsInTheShippedFolderAndSaveInTheLastOne()
        {
            string root = AppRootWithSettings(StreamFiles.FrameSyncFolder, StreamFiles.ReceiverParamsFolder);
            var files = new FakeFilePicker(null, null, null, null);
            StreamSetupViewModel vm = Dialog(Row(StreamMode.ReceiverChannelInfo), files, root, lastFolder: @"C:\last");
            await vm.LoadFrameSyncCommand.ExecuteAsync(null);
            await vm.SaveFrameSyncCommand.ExecuteAsync(null);
            await vm.LoadReceiverParamsCommand.ExecuteAsync(null);
            await vm.SaveReceiverParamsCommand.ExecuteAsync(null);
            Assert.Equal(new[]
            {
                ("open", "Load Frame Sync Parameters", (string?)Path.Combine(root, "settings", "framesync_patterns")),
                ("save", "Save Frame Sync Parameters", (string?)@"C:\last"),
                ("open", "Load Receiver Parameters", (string?)Path.Combine(root, "settings", "receiver_params")),
                ("save", "Save Receiver Parameters", (string?)@"C:\last"),
            }, files.Asked);
            Assert.Equal("", vm.FileMessage);   // every dialog was cancelled: nothing happened
            Assert.Equal(@"C:\last", vm.LastFolder);
        }

        [Fact]
        public async Task WithoutTheShippedFolderLoadStartsInTheLastOne()
        {
            var files = new FakeFilePicker(new string?[] { null });
            StreamSetupViewModel vm = Dialog(Row(), files, AppRootWithSettings(), lastFolder: @"C:\last");
            await vm.LoadFrameSyncCommand.ExecuteAsync(null);
            Assert.Equal(@"C:\last", files.Asked[0].Folder);
            Assert.Equal(@"C:\last", StreamFiles.LoadFolder(null, StreamFiles.FrameSyncFolder, @"C:\last"));
        }

        [Fact]
        public async Task LoadingAPatternFileShowsItAndRemembersTheFolder()
        {
            string prn11 = Path.Combine(Repo.Root, "settings", "framesync_patterns", "framesync_PRN11.toml");
            FrameSyncFileData file = FrameSyncFile.Read(prn11);
            StreamSetupViewModel vm = Dialog(Row() with { DataRateMbps = 3 }, new FakeFilePicker(prn11));
            await vm.LoadFrameSyncCommand.ExecuteAsync(null);
            Assert.Equal((file.Pattern.ToUpperInvariant(), (decimal?)file.BitsPerFrame), (vm.SyncPattern, vm.BitsInMinorFrame));
            Assert.Equal(3m, vm.DataRateMbps);   // a per-session field: not in the file, left alone
            Assert.Equal("Loaded framesync_PRN11.toml.", vm.FileMessage);
            Assert.Equal(Path.GetDirectoryName(prn11), vm.LastFolder);
            Assert.True(vm.CanAccept);
        }

        [Fact]
        public async Task SavingAPatternFileWritesWhatIsShown()
        {
            string target = Path.Combine(_dir, "mine");
            StreamSetupViewModel vm = Dialog(Row(), new FakeFilePicker(target));
            vm.SyncPattern = "fe6b2840";
            vm.BitsInMinorFrame = 800;
            vm.Inverted = true;
            await vm.SaveFrameSyncCommand.ExecuteAsync(null);
            FrameSyncFileData written = FrameSyncFile.Read(target + ".toml");
            Assert.Equal(("FE6B2840", (int?)800, true), (written.Pattern, written.BitsPerFrame, written.Inverted));
            Assert.Equal("Saved mine.toml.", vm.FileMessage);
            Assert.Equal(_dir, vm.LastFolder);
        }

        [Fact]
        public async Task ASaveThatFailsSaysSo()
        {
            string blocked = Path.Combine(_dir, "no-such-folder", "x.toml");
            StreamSetupViewModel vm = Dialog(Row(StreamMode.ReceiverChannelInfo), new FakeFilePicker(blocked, blocked), lastFolder: @"C:\last");
            await vm.SaveFrameSyncCommand.ExecuteAsync(null);
            Assert.StartsWith("Could not save: ", vm.FileMessage);
            vm = Dialog(Row(StreamMode.ReceiverChannelInfo), new FakeFilePicker(blocked), lastFolder: @"C:\last");
            await vm.SaveReceiverParamsCommand.ExecuteAsync(null);
            Assert.StartsWith("Could not save: ", vm.FileMessage);
            Assert.Contains("x.toml", vm.FileMessage, StringComparison.Ordinal);
            Assert.Equal(@"C:\last", vm.LastFolder);
        }

        [Fact]
        public async Task LoadingReceiverParametersShowsThemAndTheWordMap()
        {
            string rasa = Path.Combine(Repo.Root, "settings", "receiver_params", "RASA.toml");
            StreamSetupViewModel vm = Dialog(Row(StreamMode.ReceiverChannelInfo) with { BitsInMinorFrame = 800 }, new FakeFilePicker(rasa));
            await vm.LoadReceiverParamsCommand.ExecuteAsync(null);
            ReceiverParams expected = ReceiverParamsFile.Read(rasa, StreamFiles.ReceiverParamsOf(Row(StreamMode.ReceiverChannelInfo)));
            Assert.Equal((expected.PolarityIndex, expected.SlopeIndex, (decimal)expected.ScaleDdBPerV, (decimal)expected.NumReceivers),
                         (vm.PolarityIndex, vm.SlopeIndex, vm.ScaleDdBPerV, vm.NumReceivers));
            Assert.Equal("RASA.toml", vm.ReceiverParamsName);
            Assert.Equal(rasa, vm.ToSetup().ReceiverParamsToml);
            Assert.Equal("Loaded RASA.toml.", vm.FileMessage);
        }

        [Fact]
        public async Task SavingReceiverParametersWritesWhatIsShown()
        {
            string target = Path.Combine(_dir, "rx.toml");
            StreamSetupViewModel vm = Dialog(Row(StreamMode.ReceiverChannelInfo) with { BitsInMinorFrame = 800 }, new FakeFilePicker(target));
            vm.ScaleDdBPerV = 12.5m;
            vm.NumReceivers = 4;
            await vm.SaveReceiverParamsCommand.ExecuteAsync(null);
            Assert.Equal(new ReceiverParams(0, 3, 12.5, 4, 3), ReceiverParamsFile.Read(target));
            Assert.Equal("Saved rx.toml.", vm.FileMessage);
            Assert.Equal("", vm.ToSetup().ReceiverParamsToml);   // saving does not change the row's word map
        }

        [Fact]
        public void WithoutAFilePickerLoadAndSaveAreUnavailable()
        {
            var vm = new StreamSetupViewModel(Row(), null, null, null);
            Assert.False(vm.LoadFrameSyncCommand.CanExecute(null));
            Assert.False(vm.SaveReceiverParamsCommand.CanExecute(null));
        }
    }
}
