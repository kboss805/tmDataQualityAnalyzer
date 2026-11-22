using System;
using System.IO;
using System.Threading.Tasks;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.App.ViewModels.Dialogs;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// A stream's setup dialog without a window (plan step 4.1b): what it opens with in each mode, the problems that
/// stop OK as they arise (US7.0), OK and Cancel, and its Load and Save buttons through the core, with the folders
/// the file dialogs start in.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class StreamSetupViewModelTests : IDisposable
{
    private readonly string _dir = TempFiles.NewPath("setupvm");

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
        Assert.Equal(("A345CA5C", "FFFF0000", 1600, 2.5, 2, true, true),
                     (vm.SyncPattern, vm.SyncMask, vm.BitsInMinorFrame, vm.DataRateMbps, vm.PeriodIndex, vm.Randomized, vm.Inverted));
        Assert.Equal("Configure Frame Sync Lock \u2014 CH-01 PRN 15 5M", vm.Title);
        Assert.Equal("Apply to all Frame Sync Lock streams", vm.ApplyToAllText);
        Assert.False(vm.ApplyToAll);   // off until asked for, as in v2.14.2's dialog
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
        Assert.Equal((1, 2, 41.5, 7, 5), (snr.PolarityIndex, snr.SlopeIndex, snr.ScaleDdBPerV, snr.NumReceivers, snr.ReceiverChannels));
        Assert.Equal(("41.500", "7", "5"), (snr.ScaleText, snr.NumReceiversText, snr.ReceiverChannelsText));
        Assert.Equal("(default word map)", snr.ReceiverParamsName);
    }

    [Fact]
    public void TheChoicesAreUs21sAndUs30sInTheirOrder()
    {
        // US2.1's voltage ranges and polarity, and US3.0's and US3.2's sample windows: the lists themselves, as
        // v2.14.2's dialog offered them. What each index means to the core is pinned natively
        // ("Constants: uiSamplePeriods", "Constants: uiSlopeVoltageBounds", "Constants: uiPolarityConstants").
        Assert.Equal(new[] { "1 s", "100 ms", "10 ms" }, StreamSetupViewModel.Periods);
        Assert.Equal(new[] { "Positive", "Negative" }, StreamSetupViewModel.Polarities);
        Assert.Equal(new[] { "±10 V", "±5 V", "0–10 V", "0–5 V" }, StreamSetupViewModel.Slopes);
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

        vm.BitsPerFrameText = "";
        vm.DataRateText = "  ";
        Assert.Equal(new[] { "Bits Per Frame is empty.", "Data Rate is empty." }, vm.Problems);
        vm.BitsPerFrameText = "800";
        vm.DataRateText = "0";
        Assert.Empty(vm.Problems);
    }

    [Fact]
    public void TheReceiverFieldsAreCheckedOnlyForReceiverSnr()
    {
        StreamSetupViewModel lockDialog = Dialog(Row());
        lockDialog.ScaleText = "";
        lockDialog.NumReceiversText = "0";
        Assert.Empty(lockDialog.Problems);

        StreamSetupViewModel snr = Dialog(Row(StreamMode.ReceiverChannelInfo));
        snr.ScaleText = "";
        snr.NumReceiversText = "";
        snr.ReceiverChannelsText = "";
        Assert.Equal(new[] { "Scale (dB/V) is empty.", "Num Rcvrs is empty.", "Num Channels is empty." }, snr.Problems);
        snr.ScaleText = "20";
        snr.NumReceiversText = "101";
        snr.ReceiverChannelsText = "3";
        Assert.Equal(new[] { "Num Rcvrs must be from 1 to 100." }, snr.Problems);
    }

    [Theory]
    [InlineData("800", true)]
    [InlineData(" 800 ", true)]
    [InlineData("+800", true)]
    [InlineData("1e3", false)]
    [InlineData("800.5", false)]
    [InlineData("800.0", false)]
    [InlineData("8 00", false)]
    [InlineData("8,000", false)]
    [InlineData("abc", false)]
    [InlineData("0x40", false)]
    [InlineData("99999999999999999999999", false)]
    public void BitsPerFrameMustBeAnInteger(string text, bool whole)
    {
        StreamSetupViewModel vm = Dialog(Row());
        vm.BitsPerFrameText = text;
        if (whole)
            Assert.DoesNotContain("Bits Per Frame must be a whole number.", vm.Problems);
        else
        {
            Assert.Equal(new[] { "Bits Per Frame must be a whole number." }, vm.Problems);
            Assert.Null(vm.BitsInMinorFrame);
            Assert.False(vm.CanAccept);
            Assert.False(vm.OkCommand.CanExecute(null));
            Assert.Equal(Row().BitsInMinorFrame, vm.ToSetup().BitsInMinorFrame);   // the row keeps what it opened with
        }
    }

    [Theory]
    [InlineData("0", true)]
    [InlineData("5", true)]
    [InlineData("5.", true)]
    [InlineData(".5", true)]
    [InlineData("2.500", true)]
    [InlineData("1e1", true)]
    [InlineData("-0", true)]
    [InlineData("5,0", false)]
    [InlineData("5 Mbps", false)]
    [InlineData("NaN", false)]
    [InlineData("Infinity", false)]
    [InlineData("-", false)]
    [InlineData("1.2.3", false)]
    public void DataRateMustBeANumber(string text, bool number)
    {
        StreamSetupViewModel vm = Dialog(Row());
        vm.DataRateText = text;
        if (number)
        {
            Assert.DoesNotContain("Data Rate must be a number.", vm.Problems);
            Assert.NotNull(vm.DataRateMbps);
        }
        else
        {
            Assert.Equal(new[] { "Data Rate must be a number." }, vm.Problems);
            Assert.Null(vm.DataRateMbps);
            Assert.False(vm.CanAccept);
        }
    }

    [Fact]
    public void ADataRateOfZeroStillMeansTmatsAndALimitIsStillALimit()
    {
        StreamSetupViewModel vm = Dialog(Row());
        vm.DataRateText = "0";
        Assert.True(vm.CanAccept);
        Assert.Equal(0.0, vm.ToSetup().DataRateMbps);
        vm.DataRateText = "1000.5";
        Assert.Equal(new[] { "Data Rate must be from 0 (the TMATS rate) to 1000 Mbps." }, vm.Problems);
        vm.DataRateText = "-1";
        Assert.Equal(new[] { "Data Rate must be from 0 (the TMATS rate) to 1000 Mbps." }, vm.Problems);
        vm.BitsPerFrameText = "63";
        Assert.Contains("Bits Per Frame must be from 64 to 65536.", vm.Problems);
        vm.BitsPerFrameText = "99999999999";   // past an int: the nearest one, which is out of range
        Assert.Contains("Bits Per Frame must be from 64 to 65536.", vm.Problems);
        vm.DataRateText = "0";
        vm.BitsPerFrameText = "4294968096";   // 2^32 + 800: an int that wrapped would be a valid 800
        Assert.Equal(new[] { "Bits Per Frame must be from 64 to 65536." }, vm.Problems);
        vm.BitsPerFrameText = null!;   // a box that reports no text is an empty one
        Assert.Equal(new[] { "Bits Per Frame is empty." }, vm.Problems);
    }

    [Fact]
    public void TheReceiverFieldsAreRefusedAsTheyAreTyped()
    {
        StreamSetupViewModel snr = Dialog(Row(StreamMode.ReceiverChannelInfo));
        snr.ScaleText = "twenty";
        snr.NumReceiversText = "2.5";
        snr.ReceiverChannelsText = "3x";
        Assert.Equal(new[] { "Scale (dB/V) must be a number.", "Num Rcvrs must be a whole number.", "Num Channels must be a whole number." }, snr.Problems);
        Assert.False(snr.CanAccept);
        snr.ScaleText = "12.5";
        snr.NumReceiversText = "4";
        snr.ReceiverChannelsText = "3";
        Assert.Empty(snr.Problems);
        snr.ScaleText = "0";
        Assert.Equal(new[] { "Scale (dB/V) must be from 0.001 to 999.999." }, snr.Problems);

        // The lock dialog does not check what it does not show.
        StreamSetupViewModel lockDialog = Dialog(Row());
        lockDialog.ScaleText = "twenty";
        lockDialog.NumReceiversText = "2.5";
        Assert.Empty(lockDialog.Problems);
    }

    [Fact]
    public void ANumberIsShownWithoutLosingAnyOfIt()
    {
        // Three decimals as the spin boxes showed them, unless that would change the value now that the text is what is held.
        StreamSetupViewModel vm = Dialog(Row() with { DataRateMbps = 0.0005 });
        Assert.Equal("0.0005", vm.DataRateText);
        Assert.Equal(0.0005, vm.ToSetup().DataRateMbps);
        Assert.Equal("5.000", Dialog(Row() with { DataRateMbps = 5 }).DataRateText);
        Assert.Equal("32767", Dialog(Row() with { BitsInMinorFrame = 32767 }).BitsPerFrameText);
    }

    [Fact]
    public void TypingANumberTellsEveryoneWhoReadsTheProblems()
    {
        StreamSetupViewModel vm = Dialog(Row());
        var said = new System.Collections.Generic.List<string?>();
        vm.PropertyChanged += (_, e) => said.Add(e.PropertyName);
        vm.BitsPerFrameText = "x";
        Assert.Contains(nameof(StreamSetupViewModel.BitsPerFrameText), said);
        Assert.Contains(nameof(StreamSetupViewModel.ProblemText), said);
        Assert.Contains(nameof(StreamSetupViewModel.CanAccept), said);
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
        vm.BitsPerFrameText = "800";
        vm.DataRateText = "0.8";
        vm.PeriodIndex = 1;
        vm.Randomized = true;
        vm.Inverted = true;
        vm.PolarityIndex = 1;
        vm.SlopeIndex = 0;
        vm.ScaleText = "12.5";
        vm.NumReceiversText = "12";
        vm.ReceiverChannelsText = "4";
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
        Assert.Equal((file.Pattern.ToUpperInvariant(), (int?)file.BitsPerFrame), (vm.SyncPattern, vm.BitsInMinorFrame));
        Assert.Equal(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"{file.BitsPerFrame}"), vm.BitsPerFrameText);
        Assert.Equal(3.0, vm.DataRateMbps);   // a per-session field: not in the file, left alone
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
        vm.BitsPerFrameText = "800";
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
        // Every field shown starts away from what RASA says, so each one the load does not reach stays visibly wrong.
        StreamSetup shown = Row(StreamMode.ReceiverChannelInfo) with { BitsInMinorFrame = 800, PolarityIndex = 0, SlopeIndex = 0, ScaleDdBPerV = 1, NumReceivers = 1, ReceiverChannels = 5 };
        StreamSetupViewModel vm = Dialog(shown, new FakeFilePicker(rasa));
        await vm.LoadReceiverParamsCommand.ExecuteAsync(null);
        ReceiverParams expected = ReceiverParamsFile.Read(rasa, StreamFiles.ReceiverParamsOf(shown));
        Assert.NotEqual(StreamFiles.ReceiverParamsOf(shown), expected);
        Assert.Equal((expected.PolarityIndex, expected.SlopeIndex, expected.ScaleDdBPerV, expected.NumReceivers, expected.ReceiverChannels),
                     (vm.PolarityIndex, vm.SlopeIndex, vm.ScaleDdBPerV, vm.NumReceivers, vm.ReceiverChannels));
        Assert.Equal(3, vm.ReceiverChannels);
        Assert.Equal("3", vm.ReceiverChannelsText);
        Assert.Equal("RASA.toml", vm.ReceiverParamsName);
        Assert.Equal(rasa, vm.ToSetup().ReceiverParamsToml);
        Assert.Equal("Loaded RASA.toml.", vm.FileMessage);
    }

    [Fact]
    public async Task SavingReceiverParametersWritesWhatIsShown()
    {
        string target = Path.Combine(_dir, "rx.toml");
        StreamSetupViewModel vm = Dialog(Row(StreamMode.ReceiverChannelInfo) with { BitsInMinorFrame = 800 }, new FakeFilePicker(target));
        vm.ScaleText = "12.5";
        vm.NumReceiversText = "4";
        await vm.SaveReceiverParamsCommand.ExecuteAsync(null);
        Assert.Equal(new ReceiverParams(0, 3, 12.5, 4, 3), ReceiverParamsFile.Read(target));
        Assert.Equal("Saved rx.toml.", vm.FileMessage);
        Assert.Equal(_dir, vm.LastFolder);
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
