using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using TmDq.App.Models.Processing;
using TmDq.App.Services;
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
        Assert.Equal("The recording has no channel 5: channel 40 is selected. The recording has no time channel 9: time channel 3 is selected.", vm.ChannelNote);
        CalibrationRequest request = Assert.Single(runner.Requests);
        Assert.Equal((40, 3), (request.PcmChannelId, request.TimeChannelId));
    }

    [Fact]
    public async Task OnlyTheChannelTheRecordingLacksIsSaidSo()
    {
        var timeOnly = Dialog(StepCalBase(time: 9, pcm: 26), Channels(2, new[] { 2, 3 }, 26, 27), new RecordingRunner());
        await timeOnly.LoadCalibrationAsync("calibration.ch10");
        Assert.Equal("The recording has no time channel 9: time channel 2 is selected.", timeOnly.ChannelNote);

        var pcmOnly = Dialog(StepCalBase(time: 1, pcm: 5), Channels(1, new[] { 1 }, 40), new RecordingRunner());
        await pcmOnly.LoadCalibrationAsync("calibration.ch10");
        Assert.Equal("The recording has no channel 5: channel 40 is selected.", pcmOnly.ChannelNote);
        Assert.True(pcmOnly.HasChannelNote);

        var neither = Dialog(StepCalBase(time: 1, pcm: 26), Channels(1, new[] { 1 }, 26), new RecordingRunner());
        await neither.LoadCalibrationAsync("calibration.ch10");
        Assert.Equal(string.Empty, neither.ChannelNote);
        Assert.False(neither.HasChannelNote);
    }

    [Fact]
    public async Task ARecordingWhoseChannelsArriveAfterALaterChoiceIsDropped()
    {
        var runner = new RecordingRunner();
        using var release = new System.Threading.ManualResetEventSlim();
        RecordingChannels slow = Channels(1, new[] { 1 }, 26), fast = Channels(1, new[] { 1 }, 31, 32);
        var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null)
        {
            Runner = runner,
            ReadChannels = path =>
            {
                if (path == "slow.ch10") release.Wait();   // the first choice's channels come last
                return path == "slow.ch10" ? slow : fast;
            },
        };

        Task first = vm.LoadCalibrationAsync("slow.ch10");
        await vm.LoadCalibrationAsync("fast.ch10");
        release.Set();
        await first;

        Assert.Equal("fast.ch10", vm.CalPath);
        Assert.Equal(new[] { "31 - PCM 31", "32 - PCM 32" }, vm.CalPcmItems);   // the later choice's, not overwritten
        CalibrationRequest request = Assert.Single(runner.Requests);            // and the earlier one never extracted
        Assert.Equal("fast.ch10", request.FileName);
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

    // ---- one extraction at a time, the latest asked for -------------------------------------------------------------------

    /// <summary>A run that goes on until it is cancelled or let finish, then cannot hand its result back (so the status says
    /// which run's result was shown): it notes each start, each cancel and each result asked for, and how many ran at once.</summary>
    private sealed class HeldRunner : ICalibrationRunner
    {
        public readonly List<Held> Runs = new();
        public int Running, MostAtOnce;
        public TaskCompletionSource Started = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ICalibrationRunning Start(CalibrationRequest request, IRunObserver observer)
        {
            var run = new Held(this, Runs.Count, request);
            Runs.Add(run);
            MostAtOnce = Math.Max(MostAtOnce, Interlocked.Increment(ref Running));
            Started.TrySetResult();
            return run;
        }

        public sealed class Held(HeldRunner owner, int index, CalibrationRequest request) : ICalibrationRunning
        {
            private readonly ManualResetEventSlim _go = new();
            public CalibrationRequest Request { get; } = request;
            public bool Cancelled { get; private set; }
            public bool ResultAsked { get; private set; }
            public bool IsRunning => true;
            public void Finish() => _go.Set();
            public void Cancel()
            {
                Cancelled = true;
                _go.Set();
            }
            public void Wait() => _go.Wait();
            public Exception? ObserverException => null;
            public CalibrationResult GetResult()
            {
                ResultAsked = true;
                throw new TmdqException(Status.Internal, "the result of run " + index);
            }
            public void Dispose()
            {
                Interlocked.Decrement(ref owner.Running);
                _go.Dispose();
            }
        }
    }

    /// <summary>The progress dialog, counting how many are up at once.</summary>
    private sealed class CountingProgress : IProcessingDialog
    {
        public int Open, MostOpen, Shown;
        public void Show(ProcessingViewModel vm)
        {
            Shown++;
            MostOpen = Math.Max(MostOpen, ++Open);
        }
        public void Close() => Open--;
    }

    [AvaloniaFact]
    public async Task AnExtractionAskedForWhileOneRunsCancelsItDropsItsResultAndStartsOnceItHasStopped()
    {
        var runner = new HeldRunner();
        var progress = new CountingProgress();
        var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null)
        {
            Runner = runner, ProgressDialog = progress, ReadChannels = _ => Channels(1, new[] { 1, 2 }, 26, 27),
        };
        Task load = vm.LoadCalibrationAsync("calibration.ch10");
        await runner.Started.Task;
        Assert.Single(runner.Runs);
        runner.Started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        vm.CalPcmIndex = 1;    // another channel while the first run goes on,
        vm.CalTimeIndex = 1;   // and another time channel before that one has started
        Task latest = vm.Extraction!;

        Assert.True(runner.Runs[0].Cancelled);
        await load;
        await runner.Started.Task;   // the run next started, once the first had stopped: the latest asked for, not the one between
        Assert.Equal(2, runner.Runs.Count);
        Assert.Equal((27, 2), (runner.Runs[1].Request.PcmChannelId, runner.Runs[1].Request.TimeChannelId));
        runner.Runs[1].Finish();
        await latest;

        Assert.Equal(2, runner.Runs.Count);
        Assert.False(runner.Runs[0].ResultAsked);   // the cancelled run's result was never read, nor shown
        Assert.Equal((CalibrationStatusKind.Fail, "✗ the result of run 1"), (vm.CalKind, vm.CalStatus));
        Assert.Equal(1, runner.MostAtOnce);          // the second started once the first had stopped
        Assert.Equal((2, 1, 0), (progress.Shown, progress.MostOpen, progress.Open));   // one progress dialog at a time, each closed
    }

    [AvaloniaFact]
    public async Task AClipTypedDigitByDigitIsExtractedOnceWhenTheTypingStops()
    {
        var runner = new RecordingRunner();
        var waits = new List<TaskCompletionSource>();
        var vm = Dialog(StepCalBase(), Channels(1, new[] { 1 }, 26), runner);
        vm.ClipSettle = token =>
        {
            var wait = new TaskCompletionSource();
            token.Register(() => wait.TrySetCanceled(token));
            waits.Add(wait);
            return wait.Task;
        };
        await vm.LoadCalibrationAsync("calibration.ch10");
        Assert.Single(runner.Requests);   // the recording chosen: extracted at once

        foreach (string typed in new[] { "1", "12", "123" }) vm.ClipEndText = typed;
        Assert.Single(runner.Requests);   // nothing yet: the typing has not stopped
        Assert.Equal(3, waits.Count);
        Assert.True(waits[0].Task.IsCanceled && waits[1].Task.IsCanceled);   // each keystroke cut the last wait short
        Assert.False(vm.CanAccept);       // what was extracted is of another clip now
        Assert.Equal(ExtractCalibrationViewModel.ProcessingText, vm.CalStatus);

        waits[2].SetResult();
        await vm.Extraction!;

        Assert.Equal(2, runner.Requests.Count);
        Assert.Equal(123.0, runner.Requests[1].ClipEndSec);

        // Set rather than typed (a stream's saved clip, a test), a clip is extracted with at once.
        vm.ClipStart = 4m;
        await vm.Extraction!;
        Assert.Equal((3, 3), (runner.Requests.Count, waits.Count));
        Assert.Equal(4.0, runner.Requests[2].ClipStartSec);
    }

    [AvaloniaFact]
    public async Task ATypedClipWaitingWhenTheDialogClosesIsNeverExtracted()
    {
        var runner = new RecordingRunner();
        var wait = new TaskCompletionSource();
        var vm = Dialog(StepCalBase(), Channels(1, new[] { 1 }, 26), runner);
        vm.ClipSettle = token =>
        {
            token.Register(() => wait.TrySetCanceled(token));
            return wait.Task;
        };
        await vm.LoadCalibrationAsync("calibration.ch10");

        vm.ClipStartText = "5";
        vm.CancelExtraction();   // what the dialog's host does once its window has closed

        Assert.True(wait.Task.IsCanceled);
        await vm.Extraction!;
        Assert.Single(runner.Requests);
    }

    /// <summary>A runner that fails in a way no one planned for.</summary>
    private sealed class BrokenRunner : ICalibrationRunner
    {
        public ICalibrationRunning Start(CalibrationRequest request, IRunObserver observer) =>
            throw new InvalidOperationException("something unforeseen");
    }

    [Fact]
    public async Task AnExtractionThatFailsUnexpectedlyIsACrossNotAProcessingThatNeverEnds()
    {
        var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null)
        {
            Runner = new BrokenRunner(), ReadChannels = _ => Channels(1, new[] { 1 }, 26, 27),
        };

        await vm.LoadCalibrationAsync("calibration.ch10");   // nothing thrown out of it, nor out of an extraction no one awaits
        Assert.Equal((CalibrationStatusKind.Fail, "✗ The calibration could not be extracted: something unforeseen"), (vm.CalKind, vm.CalStatus));

        vm.CalPcmIndex = 1;
        await vm.Extraction!;
        Assert.True(vm.Extraction!.IsCompletedSuccessfully);
        Assert.Equal(CalibrationStatusKind.Fail, vm.CalKind);
        Assert.False(vm.CanAccept);
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
