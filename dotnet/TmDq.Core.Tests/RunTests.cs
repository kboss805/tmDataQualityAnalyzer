using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// Runs of the committed recordings through the managed library. The numbers are the ones the Qt application's
/// own pipeline produces: the PRN figures are what the interop spike recorded before the de-Qt sweep (116
/// samples and 1,100 missed frames at one second, 11,554 and 838 at ten milliseconds), and the rest were taken
/// from the core run directly (PipelineRunner, the code behind both applications) with a digest that is
/// recomputed here in the same order, so equal inputs give equal floating-point results.
/// </summary>
public class RunTests
{
    private static readonly TimeSpan Timeout = TimeSpan.FromMinutes(3);

    public RunTests()
    {
        Env.EnsureLoaded();
    }

    // --- the streams under test

    private static StreamConfig Prn(int channel = 13, SamplePeriod period = SamplePeriod.OneSecond, string pattern = "334AABBF") => new()
    {
        PcmChannelId = channel, Label = "Ch " + channel, SyncPattern = pattern, BitsInMinorFrame = 32767, SamplePeriod = period,
    };

    private static StreamConfig StepCal(StreamMode mode = StreamMode.FrameSyncLock, string label = "step") => new()
    {
        PcmChannelId = 26, Label = label, Mode = mode, SyncPattern = "FE6B2840", BitsInMinorFrame = 800,
    };

    private static StreamConfig Trc(StreamMode mode = StreamMode.FrameSyncLock) => new()
    {
        PcmChannelId = 13, Label = "trc", Mode = mode, SyncPattern = "FE6B2840", BitsInMinorFrame = 8000, Randomized = true,
    };

    private static RunRequest Request(string file, params StreamConfig[] streams) => new(file, 1, streams)
    {
        AppRoot = Env.RepoRoot,
    };

    private static Collector RunToTheEnd(RunRequest request)
    {
        var collector = new Collector();
        using ProcessingRun run = ProcessingRun.Start(request, collector);
        Assert.True(collector.WaitFinished(Timeout), "the run did not finish: " + string.Join(" | ", collector.Events));
        run.Wait();
        Assert.Null(run.ObserverException);
        return collector;
    }

    private sealed record Digest(int N, double LockMean, double LockMin, double LockMax, double LastMissed, double Span,
                                 int Channels, string First, string Last, double Sum);

    private static Digest Of(ProcessedStream s)
    {
        double lockSum = 0, lockMin = double.MaxValue, lockMax = double.MinValue;
        foreach (double v in s.LockPercent)
        {
            lockSum += v;
            lockMin = Math.Min(lockMin, v);
            lockMax = Math.Max(lockMax, v);
        }
        double sum = 0;
        foreach (ChannelData ch in s.Channels)
        {
            foreach (double v in ch.Values)
            {
                sum += v;
            }
        }
        return new Digest(
            s.TimesSec.Length,
            s.LockPercent.Length == 0 ? 0 : lockSum / s.LockPercent.Length,
            s.LockPercent.Length == 0 ? 0 : lockMin,
            s.LockPercent.Length == 0 ? 0 : lockMax,
            s.MissedFrames.Length == 0 ? 0 : s.MissedFrames[^1],
            s.TimesSec.Length == 0 ? 0 : s.TimesSec[^1] - s.TimesSec[0],
            s.Channels.Count,
            s.Channels.Count == 0 ? "-" : s.Channels[0].Name,
            s.Channels.Count == 0 ? "-" : s.Channels[^1].Name,
            sum);
    }

    private static void AssertLock(ProcessedStream s, int n, double mean, double min, double max, double missed, double span)
    {
        Digest d = Of(s);
        Assert.Equal(n, d.N);
        Assert.Equal(mean, d.LockMean, 6);
        Assert.Equal(min, d.LockMin, 6);
        Assert.Equal(max, d.LockMax, 6);
        Assert.Equal(missed, d.LastMissed);
        Assert.Equal(span, d.Span, 3);
        Assert.Equal(0, d.Channels);
        Assert.Equal(n, s.TimesSec.Length);
        Assert.Equal(n, s.MissedFrames.Length);
    }

    // --- the recorded numbers

    [Fact]
    public void PrnChannel13AtOneSecondMatchesTheInteropSpikesRecordedNumbers()
    {
        Collector c = RunToTheEnd(Request(Env.Prn, Prn()));
        Assert.True(c.Success);
        ProcessedStream s = Assert.Single(c.Results);
        AssertLock(s, 116, 93.531893, 0.0, 100.0, 1100.0, 115.0);
        Assert.Equal(13, s.PcmChannelId);
        Assert.Equal("Ch 13", s.Label);
        Assert.Equal(StreamMode.FrameSyncLock, s.Mode);
    }

    [Fact]
    public void PrnChannel13AtTenMillisecondsMatchesTheInteropSpikesRecordedNumbers()
    {
        Collector c = RunToTheEnd(Request(Env.Prn, Prn(period: SamplePeriod.TenMilliseconds)));
        AssertLock(Assert.Single(c.Results), 11554, 92.643059, 0.0, 100.0, 838.0, 115.530);
    }

    [Fact]
    public void StepCalFrameSyncLockMatchesTheCore()
    {
        Collector c = RunToTheEnd(Request(Env.StepCal, StepCal()));
        AssertLock(Assert.Single(c.Results), 66, 99.999303, 99.953998, 100.0, 0.0, 65.0);
    }

    [Fact]
    public void StepCalReceiverChannelsMatchTheCore()
    {
        Collector c = RunToTheEnd(Request(Env.StepCal, StepCal(StreamMode.ReceiverChannelInfo)));
        ProcessedStream s = Assert.Single(c.Results);
        Digest d = Of(s);
        Assert.Equal(66, d.N);
        Assert.Equal(48, d.Channels);
        Assert.Equal("L_RCVR1", d.First);
        Assert.Equal("C_RCVR16", d.Last);
        Assert.Equal(32628.981247, d.Sum, 5);
        Assert.Empty(s.LockPercent);        // a receiver stream has no lock series
        Assert.Empty(s.MissedFrames);
        Assert.Equal(StreamMode.ReceiverChannelInfo, s.Mode);
        Assert.All(s.Channels, ch => Assert.Equal(66, ch.Values.Length));
        Assert.Equal(Enumerable.Range(0, 48), s.Channels.Select(ch => ch.WordIndex));      // zero-based words
    }

    [Fact]
    public void TheRandomizedRecordingMatchesTheCoreInBothModes()
    {
        Collector lockRun = RunToTheEnd(Request(Env.Trc, Trc()));
        AssertLock(Assert.Single(lockRun.Results), 199, 99.999516, 99.903621, 100.0, 0.0, 198.0);

        Collector snrRun = RunToTheEnd(Request(Env.Trc, Trc(StreamMode.ReceiverChannelInfo)));
        Digest d = Of(Assert.Single(snrRun.Results));
        Assert.Equal(199, d.N);
        Assert.Equal(48, d.Channels);
        Assert.Equal(15336.616407, d.Sum, 5);
    }

    [Fact]
    public void StreamsOfBothModesInOneRunEachGiveTheirOwnResult()
    {
        var request = Request(Env.StepCal, StepCal(StreamMode.FrameSyncLock, "lock stream"), StepCal(StreamMode.ReceiverChannelInfo, "receiver stream")) with { SourceId = 7 };
        Collector c = RunToTheEnd(request);
        Assert.True(c.Success);
        Assert.Equal(2, c.Results.Count);
        ProcessedStream lockStream = c.Results[0], receiver = c.Results[1];
        Assert.Equal((0, 7, "lock stream", StreamMode.FrameSyncLock), (lockStream.JobIndex, lockStream.SourceId, lockStream.Label, lockStream.Mode));
        Assert.Equal((1, 7, "receiver stream", StreamMode.ReceiverChannelInfo), (receiver.JobIndex, receiver.SourceId, receiver.Label, receiver.Mode));
        AssertLock(lockStream, 66, 99.999303, 99.953998, 100.0, 0.0, 65.0);
        Assert.Equal(32628.981247, Of(receiver).Sum, 5);
    }

    // --- failures say why

    [Fact]
    public void AWrongSyncPatternFindsNothingAndSaysSo()
    {
        Collector c = RunToTheEnd(Request(Env.Prn, Prn(pattern: "12345678")));
        Assert.False(c.Success);
        Assert.Empty(c.Results);
        Assert.Contains(c.Events, e => e.StartsWith("error:"));
        Assert.Equal("finished:0", c.Events[^1]);
    }

    [Fact]
    public void AStreamTheCoreRejectsIsAJobErrorWithTheCoresMessage()
    {
        StreamConfig bad = Prn() with { SyncPattern = "not hex" };
        TmdqException e = Assert.Throws<TmdqException>(() => ProcessingRun.Start(Request(Env.Prn, Prn(), bad)));
        Assert.Equal(Status.Job, e.Status);
        Assert.Contains("Ch 13", e.Message);
        Assert.Contains("sync", e.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void ARecordingThatCannotBeUsedIsAPrepareError()
    {
        string missing = Path.Combine(Path.GetTempPath(), "tmdq-no-such-" + Guid.NewGuid().ToString("N") + ".ch10");
        TmdqException e = Assert.Throws<TmdqException>(() => ProcessingRun.Start(Request(missing, Prn())));
        Assert.Equal(Status.Prepare, e.Status);

        TmdqException ghost = Assert.Throws<TmdqException>(() => ProcessingRun.Start(Request(Env.Prn, Prn(channel: 99))));
        Assert.Equal(Status.Prepare, ghost.Status);
    }

    [Fact]
    public void AnIncompleteRequestIsABadArgument()
    {
        TmdqException none = Assert.Throws<TmdqException>(() => ProcessingRun.Start(new RunRequest(Env.Prn, 1, Array.Empty<StreamConfig>())));
        Assert.Equal(Status.BadArgument, none.Status);
        Assert.Equal("No streams selected for processing. Configure streams first.", none.Message);

        TmdqException time = Assert.Throws<TmdqException>(() => ProcessingRun.Start(new RunRequest(Env.Prn, -1, new[] { Prn() })));
        Assert.Equal(Status.BadArgument, time.Status);
        Assert.Equal("Invalid time channel.", time.Message);

        Assert.Throws<ArgumentNullException>(() => ProcessingRun.Start(null!));
    }

    // --- what an observer is told, and when

    [Fact]
    public void AnObserverIsToldInTheDocumentedOrder()
    {
        Collector c = RunToTheEnd(Request(Env.StepCal, StepCal(StreamMode.FrameSyncLock, "a"), StepCal(StreamMode.ReceiverChannelInfo, "b")));
        IReadOnlyList<string> events = c.Events;

        Assert.Equal("finished:1", events[^1]);                      // LAST
        Assert.Equal(1, c.FinishedCount);                            // and once
        Assert.Equal(1, events.Count(e => e == "stream:0"));
        Assert.Equal(1, events.Count(e => e == "stream:1"));
        Assert.DoesNotContain(events, e => e.StartsWith("error:"));
        Assert.Contains(events, e => e.StartsWith("log:"));

        // Progress comes from one thread (the reader's), never goes backwards, and ends at 100.
        Assert.Equal(1, c.DistinctProgressThreads);
        Assert.Equal(c.Progress.OrderBy(p => p), c.Progress);
        Assert.Equal(100, c.Progress[^1]);
    }

    [Fact]
    public void AnObserverThatThrowsDoesNotBreakTheRun()
    {
        var c = new Collector { Throwing = true };
        using ProcessingRun run = ProcessingRun.Start(Request(Env.StepCal, StepCal(), StepCal(StreamMode.ReceiverChannelInfo)), c);
        Assert.True(c.WaitFinished(Timeout));
        run.Wait();

        Assert.NotNull(run.ObserverException);     // kept, not allowed to unwind a native thread
        Assert.Equal(1, c.FinishedCount);
        Assert.True(c.Success);
        Assert.Equal(2, c.Results.Count);
    }

    [Fact]
    public void ARunWithNoObserverJustRuns()
    {
        using ProcessingRun run = ProcessingRun.Start(Request(Env.StepCal, StepCal()));
        run.Wait();
        Assert.False(run.IsRunning);
    }

    // --- cancel, dispose, and results that outlive their run

    [Fact]
    public void CancellingEndsARunUnsuccessfullyAndDeliversNothing()
    {
        var c = new Collector();
        using ProcessingRun run = ProcessingRun.Start(Request(Env.Prn, Prn()), c);
        Assert.True(run.IsRunning);
        run.Cancel();
        run.Cancel();       // twice is as good as once
        Assert.True(c.WaitFinished(Timeout));
        run.Wait();

        Assert.False(c.Success);
        Assert.Empty(c.Results);
        Assert.Equal("finished:0", c.Events[^1]);
        Assert.False(run.IsRunning);
    }

    [Fact]
    public void DisposingARunInFlightCancelsAndWaits()
    {
        var c = new Collector();
        ProcessingRun run = ProcessingRun.Start(Request(Env.Prn, Prn()), c);
        run.Dispose();       // no cancel, no wait: dispose does both

        Assert.Equal(1, c.FinishedCount);
        Assert.False(c.Success);
        Assert.Equal("finished:0", c.Events[^1]);
        run.Dispose();       // twice is harmless
        run.Cancel();
        run.Wait();
        Assert.False(run.IsRunning);
    }

    [Fact]
    public void AResultCanBeReadInPlaceAndOutlivesItsRunUntilDisposed()
    {
        StreamResult? kept = null;
        var observer = new KeepFirst(r => kept = r);
        using (ProcessingRun run = ProcessingRun.Start(Request(Env.StepCal, StepCal(StreamMode.ReceiverChannelInfo)), observer))
        {
            Assert.True(observer.Done.Wait(Timeout, TestContext.Current.CancellationToken));
            run.Wait();
        }

        Assert.NotNull(kept);        // the run is gone; the result is not
        Assert.Equal(66, kept!.SampleCount);
        Assert.Equal(48, kept.ChannelCount);
        Assert.Equal(66, kept.TimesSec.Length);
        Assert.Equal(66, kept.ChannelValues(47).Length);
        Assert.Equal("C_RCVR16", kept.ChannelName(47));
        Assert.Equal(47, kept.ChannelWord(47));
        Assert.True(kept.LockPercent.IsEmpty);
        ProcessedStream copy = kept.ToProcessed();

        kept.Dispose();
        kept.Dispose();             // twice is harmless
        Assert.Throws<ObjectDisposedException>(() => kept.TimesSec.Length);
        Assert.Throws<ObjectDisposedException>(() => kept.ChannelName(0));
        Assert.Equal(66, copy.TimesSec.Length);     // the copy does not depend on the native result
        Assert.Equal(48, copy.Channels.Count);
        TmdqException range = Assert.Throws<TmdqException>(() => { using var again = KeepOne(); again.ChannelName(48); });
        Assert.Equal(Status.BadArgument, range.Status);
    }

    private static StreamResult KeepOne()
    {
        StreamResult? one = null;
        var observer = new KeepFirst(r => one = r);
        using ProcessingRun run = ProcessingRun.Start(Request(Env.StepCal, StepCal(StreamMode.ReceiverChannelInfo)), observer);
        Assert.True(observer.Done.Wait(Timeout, TestContext.Current.CancellationToken));
        run.Wait();
        return one!;
    }

    private sealed class KeepFirst : IRunObserver
    {
        private readonly Action<StreamResult> _keep;

        public KeepFirst(Action<StreamResult> keep)
        {
            _keep = keep;
        }

        public ManualResetEventSlim Done { get; } = new();

        public void OnStreamCompleted(StreamResult result) => _keep(result);

        public void OnFinished(bool success) => Done.Set();
    }

    // --- settings reach the core, and runs do not disturb each other

    [Fact]
    public void ANonLinearProfileReachesTheJobAndAnInvalidOneDoesNot()
    {
        double SumOf(StreamConfig config) => Of(Assert.Single(RunToTheEnd(Request(Env.StepCal, config)).Results)).Sum;

        StreamConfig plain = StepCal(StreamMode.ReceiverChannelInfo);
        var points = new[] { new CalibrationPoint(0, -10), new CalibrationPoint(1000, 0), new CalibrationPoint(30000, 40), new CalibrationPoint(65535, 60) };
        StreamConfig profiled = plain with { Calibration = new[] { new CalibrationProfile(1, true, points) } };
        StreamConfig ignored = plain with { Calibration = new[] { new CalibrationProfile(1, false, points) } };

        double baseline = SumOf(plain);
        Assert.Equal(32628.981247, baseline, 5);
        Assert.NotEqual(baseline, SumOf(profiled));       // a valid profile changes the word it names
        Assert.Equal(baseline, SumOf(ignored));           // an invalid one leaves it linear
    }

    [Fact]
    public void ADifferentSamplePeriodReachesTheJob()
    {
        Collector c = RunToTheEnd(Request(Env.StepCal, StepCal() with { SamplePeriod = SamplePeriod.HundredMilliseconds }));
        ProcessedStream s = Assert.Single(c.Results);
        Assert.True(s.TimesSec.Length > 600, $"100 ms over 65 s should give about 650 samples, not {s.TimesSec.Length}");
    }

    [Fact]
    public async Task TwoRunsAtOnceAgreeWithOneAlone()
    {
        Digest alone = Of(Assert.Single(RunToTheEnd(Request(Env.Prn, Prn())).Results));
        CancellationToken token = TestContext.Current.CancellationToken;
        Digest[] both = await Task.WhenAll(
            Task.Run(() => Of(Assert.Single(RunToTheEnd(Request(Env.Prn, Prn())).Results)), token),
            Task.Run(() => Of(Assert.Single(RunToTheEnd(Request(Env.Prn, Prn())).Results)), token));
        Assert.All(both, d => Assert.Equal(alone, d));
    }

    [Fact]
    public void ANonAsciiFolderRunsUnderTheUtf8CodePage()
    {
        string dir = Path.Combine(Path.GetTempPath(), "tmdq-café-Ω-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        string copy = Path.Combine(dir, "recording.ch10");
        File.Copy(Env.StepCal, copy);
        try
        {
            Collector c = RunToTheEnd(Request(copy, StepCal()));
            AssertLock(Assert.Single(c.Results), 66, 99.999303, 99.953998, 100.0, 0.0, 65.0);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    // --- every setting reaches the core, one at a time

    private static string Fingerprint(RunRequest request)
    {
        try
        {
            Collector c = RunToTheEnd(request);
            if (!c.Success || c.Results.Count == 0)
            {
                return "no result";
            }
            ProcessedStream s = c.Results[0];
            Digest d = Of(s);
            return $"{d.N}|{d.LockMean:R}|{d.LastMissed:R}|{d.Channels}|{d.First}|{d.Last}|{d.Sum:R}|{s.Label}|{s.Mode}";
        }
        catch (TmdqException e)
        {
            return "refused: " + e.Message;
        }
    }

    [Fact]
    public void EverySettingOfAStreamReachesTheJob()
    {
        // The step_cal recording, one stream, changed in one setting at a time. If the library forgot to carry a
        // setting, the changed stream would give the unchanged stream's result. The receiver grid (count and
        // channels per receiver) only matters when no word-map file is found, so those two run with no app root.
        var cases = new (string Name, StreamConfig Base, Func<StreamConfig, StreamConfig> Change, bool NoAppRoot)[]
        {
            ("sync mask", StepCal(), c => c with { SyncMask = "FFFF0000" }, false),
            ("derandomize", StepCal(), c => c with { Randomized = true }, false),
            ("invert", StepCal(), c => c with { Inverted = true }, false),
            ("data rate", StepCal(), c => c with { DataRateMbps = 0.4 }, false),
            ("sample period", StepCal(), c => c with { SamplePeriod = SamplePeriod.HundredMilliseconds }, false),
            ("bits in the minor frame", StepCal(), c => c with { BitsInMinorFrame = 700 }, false),
            ("label", StepCal(), c => c with { Label = "another" }, false),
            ("scale", StepCal(StreamMode.ReceiverChannelInfo), c => c with { ScaleDdBPerV = c.ScaleDdBPerV * 2 }, false),
            ("polarity", StepCal(StreamMode.ReceiverChannelInfo), c => c with { PolarityIndex = 1 }, false),
            ("slope", StepCal(StreamMode.ReceiverChannelInfo), c => c with { SlopeIndex = 2 }, false),
            ("receiver count", StepCal(StreamMode.ReceiverChannelInfo), c => c with { NumReceivers = 2 }, true),
            ("channels per receiver", StepCal(StreamMode.ReceiverChannelInfo), c => c with { ReceiverChannels = 1 }, true),
        };

        foreach (var (name, original, change, noAppRoot) in cases)
        {
            RunRequest baseRequest = Request(Env.StepCal, original);
            RunRequest changedRequest = Request(Env.StepCal, change(original));
            if (noAppRoot)
            {
                baseRequest = baseRequest with { AppRoot = null };
                changedRequest = changedRequest with { AppRoot = null };
            }

            string before = Fingerprint(baseRequest);
            string after = Fingerprint(changedRequest);
            Assert.True(before != "no result" && !before.StartsWith("refused"), $"{name}: the unchanged stream must run: {before}");
            Assert.True(before != after, $"{name}: changing it should change the result, so the library is not carrying it");
        }
    }

    [Fact]
    public void TheByteOrderOfTheFileReachesTheJob()
    {
        RunRequest swapped = Request(Env.StepCal, StepCal());
        string with = Fingerprint(swapped);
        string without = Fingerprint(swapped with { SwapBytes = false });
        Assert.NotEqual("no result", with);
        Assert.NotEqual(with, without);          // an ordinary recording decodes to something else without the swap
    }

    [Fact]
    public void TheApplicationRootsOwnWordMapIsUsedWhenNoFileIsNamed()
    {
        string root = Path.Combine(Path.GetTempPath(), "tmdq-root-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "settings", "receiver_params"));
        File.WriteAllText(Path.Combine(root, "settings", "receiver_params", "default.toml"), "[C_RCVR1]\nWord = 7\n");
        try
        {
            StreamConfig receiver = StepCal(StreamMode.ReceiverChannelInfo);

            ProcessedStream own = Assert.Single(RunToTheEnd(Request(Env.StepCal, receiver) with { AppRoot = root }).Results);
            ChannelData only = Assert.Single(own.Channels);
            Assert.Equal("C_RCVR1", only.Name);
            Assert.Equal(6, only.WordIndex);       // "Word = 7" in the file is the seventh word, index 6

            // No root: the built-in 16 receivers of 3 channels.
            ProcessedStream none = Assert.Single(RunToTheEnd(Request(Env.StepCal, receiver) with { AppRoot = null }).Results);
            Assert.Equal(48, none.Channels.Count);

            // And the shipped map, from the repository root.
            ProcessedStream shipped = Assert.Single(RunToTheEnd(Request(Env.StepCal, receiver)).Results);
            Assert.Equal(48, shipped.Channels.Count);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void CalibrationProfilesAreCarriedWordForWordAndPointForPoint()
    {
        ProcessedStream Run(params CalibrationProfile[] profiles) =>
            Assert.Single(RunToTheEnd(Request(Env.StepCal, StepCal(StreamMode.ReceiverChannelInfo) with { Calibration = profiles })).Results);
        double[] Word(ProcessedStream s, int word) => s.Channels.Single(c => c.WordIndex == word).Values;
        double Sum(ProcessedStream s) => s.Channels.Sum(c => c.Values.Sum());

        var low = new[] { new CalibrationPoint(0, -10), new CalibrationPoint(1000, 0), new CalibrationPoint(30000, 40), new CalibrationPoint(65535, 60) };
        var high = new[] { new CalibrationPoint(0, 5), new CalibrationPoint(20000, 15), new CalibrationPoint(65535, 25) };

        ProcessedStream plain = Run();
        ProcessedStream one = Run(new CalibrationProfile(1, true, low));
        ProcessedStream six = Run(new CalibrationProfile(6, true, high));
        ProcessedStream both = Run(new CalibrationProfile(1, true, low), new CalibrationProfile(6, true, high));

        // Calibration is per word and the sum is over words, so two profiles do what each does alone.
        Assert.Equal(Sum(plain) + (Sum(one) - Sum(plain)) + (Sum(six) - Sum(plain)), Sum(both), 6);
        Assert.NotEqual(Sum(plain), Sum(one));
        Assert.NotEqual(Sum(plain), Sum(six));
        Assert.Equal(Word(plain, 0), Word(both, 0));         // a word nobody profiled is untouched

        // A profile maps the raw count onto its own dB range and never leaves it (out of range clamps), and it is
        // the same monotonic map for every sample: where the linear values order two samples, the profiled ones
        // order them the same way (or the opposite way for every pair, if the receiver is inverted).
        foreach ((int word, double lo, double hi, ProcessedStream profiled) in new[] { (1, -10.0, 60.0, one), (6, 5.0, 25.0, six) })
        {
            double[] linear = Word(plain, word), values = Word(profiled, word);
            Assert.All(values, v => Assert.InRange(v, lo, hi));
            Assert.True(values.Distinct().Count() > 2, $"word {word}: a profile of several points should give several values");
            int direction = 0;
            for (int i = 0; i < linear.Length; i++)
            {
                for (int j = i + 1; j < linear.Length; j++)
                {
                    int agree = Math.Sign(linear[i] - linear[j]) * Math.Sign(values[i] - values[j]);
                    if (agree != 0)
                    {
                        direction = direction == 0 ? agree : direction;
                        Assert.Equal(direction, agree);
                    }
                }
            }
        }
    }

    [Fact]
    public void TheFirstExceptionAnObserverThrewIsTheOneKept()
    {
        var observer = new ThrowsNumbered();
        using ProcessingRun run = ProcessingRun.Start(Request(Env.StepCal, StepCal(), StepCal(StreamMode.ReceiverChannelInfo)), observer);
        Assert.True(observer.Done.Wait(Timeout, TestContext.Current.CancellationToken));
        run.Wait();

        Assert.True(observer.Thrown > 1, "the run should have given the observer several things to throw at");
        Assert.Equal("call 1", run.ObserverException!.Message);
    }

    private sealed class ThrowsNumbered : IRunObserver
    {
        private int _count;

        public ManualResetEventSlim Done { get; } = new();

        public int Thrown => Volatile.Read(ref _count);

        private void Throw() => throw new InvalidOperationException("call " + Interlocked.Increment(ref _count));

        public void OnLog(string message) => Throw();

        public void OnError(string message) => Throw();

        public void OnProgress(int percent) => Throw();

        public void OnStreamCompleted(StreamResult result)
        {
            result.Dispose();
            Throw();
        }

        public void OnFinished(bool success)
        {
            Done.Set();
            Throw();
        }
    }

    [Fact]
    public void ProgressIsTheReadersOwnPercentageNotAConstant()
    {
        Collector c = RunToTheEnd(Request(Env.Prn, Prn()));
        Assert.Equal(100, c.Progress[^1]);
        Assert.Contains(c.Progress, p => p < 100);
    }

    [Fact]
    public void EveryAccessorOfADisposedResultRefusesAndAHandleToNothingIsRefused()
    {
        StreamResult result = KeepOneOf(StepCal());
        Assert.Equal(66, result.LockPercent.Length);       // alive: all three series are there
        Assert.Equal(66, result.MissedFrames.Length);
        result.Dispose();

        Assert.Throws<ObjectDisposedException>(() => result.TimesSec.Length);
        Assert.Throws<ObjectDisposedException>(() => result.LockPercent.Length);
        Assert.Throws<ObjectDisposedException>(() => result.MissedFrames.Length);
        Assert.Throws<ObjectDisposedException>(() => result.ChannelValues(0).Length);
        Assert.Throws<ObjectDisposedException>(() => result.ChannelWord(0));
        Assert.Throws<ObjectDisposedException>(() => result.ToProcessed());
        Assert.Throws<ObjectDisposedException>(() => result.Handle);

        // A result built over nothing says so at once, instead of reading garbage later.
        TmdqException e = Assert.Throws<TmdqException>(() => new StreamResult(IntPtr.Zero));
        Assert.Equal(Status.BadArgument, e.Status);
    }

    private static StreamResult KeepOneOf(StreamConfig config)
    {
        StreamResult? one = null;
        var observer = new KeepFirst(r => one = r);
        using ProcessingRun run = ProcessingRun.Start(Request(Env.StepCal, config), observer);
        Assert.True(observer.Done.Wait(Timeout, TestContext.Current.CancellationToken));
        run.Wait();
        return one!;
    }

    [Fact]
    public void DisposedResultsGiveTheirNativeMemoryBack()
    {
        // A receiver stream at 10 ms is 48 series of about 6,500 doubles: some 2.5 MB of native memory per result.
        // Thirty results left undisposed would be 75 MB more than thirty that were freed.
        StreamConfig big = StepCal(StreamMode.ReceiverChannelInfo) with { SamplePeriod = SamplePeriod.TenMilliseconds };
        using (StreamResult warm = KeepOneOf(big))
        {
            Assert.True(warm.SampleCount > 6000);
        }

        long before = System.Diagnostics.Process.GetCurrentProcess().PrivateMemorySize64;
        for (int i = 0; i < 30; i++)
        {
            using StreamResult result = KeepOneOf(big);
            Assert.True(result.TimesSec.Length > 6000);
        }
        long grown = System.Diagnostics.Process.GetCurrentProcess().PrivateMemorySize64 - before;
        Assert.True(grown < 40L * 1024 * 1024, $"private memory grew by {grown / (1024 * 1024)} MiB over thirty disposed results");
    }

    [Fact]
    public void AWordMapFileNamedOnTheStreamIsTheOneUsed()
    {
        string path = Path.Combine(Path.GetTempPath(), "tmdq-wordmap-" + Guid.NewGuid().ToString("N") + ".toml");
        File.WriteAllText(path, "[L_RCVR1]\nWord = 2\n[R_RCVR1]\nWord = 5\n");
        try
        {
            StreamConfig named = StepCal(StreamMode.ReceiverChannelInfo) with { ReceiverParamsToml = path };
            ProcessedStream s = Assert.Single(RunToTheEnd(Request(Env.StepCal, named)).Results);

            // Not the shipped map's 48 channels: the two this file names, at the words it names (one-based in the file).
            Assert.Equal(new[] { "L_RCVR1", "R_RCVR1" }, s.Channels.Select(c => c.Name));
            Assert.Equal(new[] { 1, 4 }, s.Channels.Select(c => c.WordIndex));
        }
        finally
        {
            File.Delete(path);
        }
    }

    private sealed class OnlyTheDefaults : IRunObserver
    {
    }

    [Fact]
    public void AnObserverThatDoesNotOverrideCompletionStillReleasesTheResult()
    {
        StreamResult result = KeepOneOf(StepCal());
        Assert.Equal(66, result.TimesSec.Length);

        ((IRunObserver)new OnlyTheDefaults()).OnStreamCompleted(result);      // the interface's own default

        Assert.Throws<ObjectDisposedException>(() => result.TimesSec.Length);
    }

    [Fact]
    public void TheDefaultsAreTheApplicationsDefaults()
    {
        var d = new StreamConfig { PcmChannelId = 1, SyncPattern = "AB" };
        Assert.Equal(StreamMode.FrameSyncLock, d.Mode);
        Assert.Equal(32767, d.BitsInMinorFrame);
        Assert.Equal(SamplePeriod.OneSecond, d.SamplePeriod);
        Assert.Equal(0, d.PolarityIndex);
        Assert.Equal(3, d.SlopeIndex);
        Assert.Equal(20.0, d.ScaleDdBPerV);
        Assert.Equal(16, d.NumReceivers);
        Assert.Equal(3, d.ReceiverChannels);
        Assert.False(d.Randomized);
        Assert.False(d.Inverted);
        Assert.Equal(0.0, d.DataRateMbps);
        Assert.Empty(d.Calibration);

        var r = new RunRequest("f", 1, Array.Empty<StreamConfig>());
        Assert.True(r.SwapBytes);
        Assert.Equal(0, r.SourceId);
        Assert.Null(r.AppRoot);
    }
}
