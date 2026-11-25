using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels.Dialogs;
using TmDq.App.Views.Dialogs;
using TmDq.Core;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// Extract Calibration lets the operator choose the channels of the calibration recording: its step cals can be recorded
/// on another channel than the stream's (a calibration file is usually a different recording), and its time channel can
/// differ too. The dialog lists the recording's channels once it is chosen, starts on the stream's own where the
/// recording has them, and extracts again on a change. The lists come from a stand-in for the core and the extraction
/// is a runner that only records what it was asked, except where the real core is wanted.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class CalibrationChannelTests
{
    // The dialog reads its step file through the core, so every case needs it loaded, whichever test ran before.
    public CalibrationChannelTests() => NativeCore.EnsureLoaded();

    private static string StepCal => Repo.Recording("step_cal, example.ch10");

    /// <summary>step_cal's stream as recorded: time channel 1, PCM channel 26.</summary>
    private static CalibrationBase StepCalBase(int time = 1, int pcm = 26) =>
        new(time, pcm, "FE6B2840", "", 800, false, false, 0.8, true, "", 16, 3);

    private static RecordingChannels Channels(int defaultTime, int[] time, params int[] pcm) =>
        new(time.Select(id => new ChannelInfo(id, "TIME " + id, 0)).ToList(),
            pcm.Select(id => new ChannelInfo(id, "PCM " + id, 800000)).ToList(),
            defaultTime);

    /// <summary>A runner that notes the request and refuses it, so nothing is extracted but every request is seen.</summary>
    private sealed class RecordingRunner : ICalibrationRunner
    {
        public readonly List<CalibrationRequest> Requests = new();

        public ICalibrationRunning Start(CalibrationRequest request, IRunObserver observer)
        {
            Requests.Add(request);
            throw new TmdqException(Status.BadArgument, "stand-in runner");
        }
    }

    private static ExtractCalibrationViewModel Dialog(CalibrationBase calibrationBase, RecordingChannels channels, RecordingRunner runner) =>
        new(calibrationBase, Repo.Root, null) { Runner = runner, ReadChannels = _ => channels };

    [Fact]
    public async Task TheRecordingsChannelsAreListedAndTheStreamsOwnAreSelected()
    {
        var runner = new RecordingRunner();
        var vm = Dialog(StepCalBase(time: 2, pcm: 27), Channels(1, new[] { 1, 2 }, 26, 27, 28), runner);
        Assert.False(vm.HasCalChannels);   // nothing to choose from until a recording is
        Assert.Empty(vm.CalPcmItems);

        await vm.LoadCalibrationAsync("calibration.ch10");

        Assert.True(vm.HasCalChannels);
        Assert.Equal(new[] { "26 - PCM 26", "27 - PCM 27", "28 - PCM 28" }, vm.CalPcmItems);
        Assert.Equal(new[] { "1 - TIME 1", "2 - TIME 2" }, vm.CalTimeItems);
        Assert.Equal((1, 1), (vm.CalPcmIndex, vm.CalTimeIndex));
        Assert.Equal((27, 2), (vm.PcmChannelId, vm.TimeChannelId));
        Assert.Equal(string.Empty, vm.ChannelNote);
        CalibrationRequest request = Assert.Single(runner.Requests);
        Assert.Equal((27, 2), (request.PcmChannelId, request.TimeChannelId));
    }

    [Fact]
    public async Task AStreamsChannelTheRecordingLacksFallsBackToItsFirstPcmAndItsDefaultTimeChannelAndSaysSo()
    {
        var runner = new RecordingRunner();
        var vm = Dialog(StepCalBase(time: 9, pcm: 5), Channels(3, new[] { 2, 3 }, 40, 41), runner);

        await vm.LoadCalibrationAsync("calibration.ch10");

        Assert.Equal((0, 1), (vm.CalPcmIndex, vm.CalTimeIndex));   // 40 is the first PCM; 3 is the recording's default time
        Assert.Equal("The recording has no channel 5: channel 40 is selected.", vm.ChannelNote);
        CalibrationRequest request = Assert.Single(runner.Requests);
        Assert.Equal((40, 3), (request.PcmChannelId, request.TimeChannelId));
    }

    [Fact]
    public async Task ChoosingAnotherChannelExtractsAgainOnThatChannelOnly()
    {
        var runner = new RecordingRunner();
        var vm = Dialog(StepCalBase(), Channels(1, new[] { 1, 2 }, 26, 27), runner);
        await vm.LoadCalibrationAsync("calibration.ch10");
        Assert.Single(runner.Requests);

        vm.CalPcmIndex = 1;
        await (vm.Extraction ?? Task.CompletedTask);
        Assert.Equal(2, runner.Requests.Count);
        Assert.Equal((27, 1), (runner.Requests[1].PcmChannelId, runner.Requests[1].TimeChannelId));

        vm.CalTimeIndex = 1;
        await (vm.Extraction ?? Task.CompletedTask);
        Assert.Equal(3, runner.Requests.Count);
        Assert.Equal((27, 2), (runner.Requests[2].PcmChannelId, runner.Requests[2].TimeChannelId));

        // The same index again, or one the lists do not have, asks for nothing.
        vm.CalPcmIndex = 1;
        vm.CalPcmIndex = 7;
        vm.CalTimeIndex = -1;
        Assert.Equal(3, runner.Requests.Count);
        Assert.Equal((1, 1), (vm.CalPcmIndex, vm.CalTimeIndex));
    }

    [Fact]
    public async Task AnotherRecordingStartsOverOnTheStreamsChannelsNotTheLastOnesChoice()
    {
        var runner = new RecordingRunner();
        var vm = Dialog(StepCalBase(), Channels(1, new[] { 1 }, 26, 27), runner);
        await vm.LoadCalibrationAsync("first.ch10");
        vm.CalPcmIndex = 1;

        await vm.LoadCalibrationAsync("second.ch10");

        Assert.Equal(0, vm.CalPcmIndex);
        Assert.Equal(26, runner.Requests[^1].PcmChannelId);
    }

    [Fact]
    public async Task ARecordingThatCannotBeOpenedIsACrossWithNoChannelsAndNothingExtracted()
    {
        var runner = new RecordingRunner();
        RecordingChannels good = Channels(1, new[] { 1 }, 26, 27);
        var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null)
        {
            Runner = runner,
            ReadChannels = path => path == "empty.ch10"
                ? throw new RecordingException("empty.ch10 has no PCM channel to process.")
                : good,
        };
        await vm.LoadCalibrationAsync("good.ch10");
        Assert.True(vm.HasCalChannels);
        Assert.Single(runner.Requests);

        await vm.LoadCalibrationAsync("empty.ch10");

        Assert.Equal((CalibrationStatusKind.Fail, "✗ empty.ch10 has no PCM channel to process."), (vm.CalKind, vm.CalStatus));
        Assert.False(vm.HasCalChannels);   // the last recording's lists do not stay for a file that gave none
        Assert.Empty(vm.CalPcmItems);
        Assert.False(vm.CanAccept);
        Assert.Single(runner.Requests);   // nothing more was extracted
    }

    [Fact]
    public async Task TheRealCoreCalibratesOnTheRecordingsOwnChannelWhenTheStreamsIsNotThere()
    {
        // step_cal is recorded on PCM channel 26; the stream was set up on channel 99 of another recording.
        var vm = new ExtractCalibrationViewModel(StepCalBase(time: 77, pcm: 99), Repo.Root, null);

        await vm.LoadCalibrationAsync(StepCal);
        await vm.Extraction!;

        Assert.Equal(("26 - ", 1), (vm.CalPcmItems.Single()[..5], vm.TimeChannelId));
        Assert.Equal(26, vm.PcmChannelId);
        Assert.Equal(CalibrationStatusKind.Ok, vm.CalKind);
        Assert.NotEmpty(vm.Profiles);
        Assert.True(vm.CanAccept);
    }

    [AvaloniaFact]
    public async Task TheWindowShowsTheChannelListsOnlyOnceARecordingIsChosen()
    {
        var vm = Dialog(StepCalBase(), Channels(1, new[] { 1, 2 }, 26, 27), new RecordingRunner());
        var window = new ExtractCalibrationWindow { DataContext = vm };
        window.Show();
        Dispatcher.UIThread.RunJobs();
        ComboBox pcm = window.FindControl<ComboBox>("CalPcmChannel")!, time = window.FindControl<ComboBox>("CalTimeChannel")!;
        Assert.False(pcm.IsEffectivelyVisible);   // its row is hidden with the recording unchosen
        Assert.False(time.IsEffectivelyVisible);
        Assert.Equal(string.Empty, window.FindControl<TextBlock>("ChannelNote")!.Text ?? string.Empty);

        await vm.LoadCalibrationAsync("calibration.ch10");
        Dispatcher.UIThread.RunJobs();

        Assert.True(pcm.IsEffectivelyVisible);
        Assert.True(time.IsEffectivelyVisible);
        Assert.Equal(new[] { "26 - PCM 26", "27 - PCM 27" }, pcm.Items.Cast<string>());
        Assert.Equal(0, pcm.SelectedIndex);
        pcm.SelectedIndex = 1;   // the operator's choice reaches the dialog
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(1, vm.CalPcmIndex);
        Assert.Equal(27, vm.PcmChannelId);
        window.Close();
    }
}
