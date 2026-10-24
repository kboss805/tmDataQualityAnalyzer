using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// Step files and calibration extraction through the managed library, on the committed step_cal recording. The
/// expected numbers (<see cref="CalibrationDigest"/>) are what the core's CalibrationExtractor makes when run
/// directly, the code behind the Qt application's Extract Calibration dialog.
/// </summary>
public class CalibrationTests
{
    private static readonly TimeSpan Timeout = TimeSpan.FromMinutes(3);

    public CalibrationTests()
    {
        Env.EnsureLoaded();
    }

    private static string ShippedSteps => Path.Combine(Env.RepoRoot, "settings", "rcvr_cals", "default.toml");

    private static double Parse(string text) => double.Parse(text, CultureInfo.InvariantCulture);

    private static CalibrationRequest Request(Func<CalibrationRequest, CalibrationRequest>? change = null)
    {
        var request = new CalibrationRequest
        {
            FileName = Env.StepCal,
            TimeChannelId = 1,
            PcmChannelId = 26,
            SyncPattern = "FE6B2840",
            BitsInMinorFrame = 800,
            DataRateMbps = 0.8,
            Steps = StepFile.Parse(ShippedSteps),
            AppRoot = Env.RepoRoot,
        };
        return change is null ? request : change(request);
    }

    private static (Collector Events, CalibrationResult Result) RunToTheEnd(CalibrationRequest request)
    {
        var collector = new Collector();
        using CalibrationRun run = CalibrationRun.Start(request, collector);
        Assert.True(collector.WaitFinished(Timeout), "the calibration did not finish: " + string.Join(" | ", collector.Events));
        run.Wait();
        Assert.Null(run.ObserverException);
        Assert.False(run.IsRunning);
        return (collector, run.GetResult());
    }

    // --- step files

    [Fact]
    public void TheShippedStepFileHasElevenStepsFromZeroToSixtyDb()
    {
        IReadOnlyList<double> steps = StepFile.Parse(ShippedSteps);
        Assert.Equal(Enumerable.Range(0, 11).Select(i => i * 6.0), steps);
    }

    [Fact]
    public void AStepFileThatCannotBeReadSaysWhatTheParserSays()
    {
        string missing = Path.Combine(Path.GetTempPath(), "tmdq-no-steps-" + Guid.NewGuid().ToString("N") + ".toml");
        TmdqException e = Assert.Throws<TmdqException>(() => StepFile.Parse(missing));
        Assert.Equal(Status.Prepare, e.Status);
        Assert.Equal($"Could not open step-config file '{Path.GetFileName(missing)}'.", e.Message);       // the parser quotes the file name

        string empty = Path.Combine(Path.GetTempPath(), "tmdq-empty-steps-" + Guid.NewGuid().ToString("N") + ".toml");
        File.WriteAllText(empty, "# no steps here\n");
        try
        {
            TmdqException none = Assert.Throws<TmdqException>(() => StepFile.Parse(empty));
            Assert.Equal(Status.Prepare, none.Status);
            Assert.Equal($"No [[Step]] entries found in '{Path.GetFileName(empty)}'.", none.Message);
        }
        finally
        {
            File.Delete(empty);
        }

        Assert.Throws<ArgumentNullException>(() => StepFile.Parse(null!));
    }

    [Fact]
    public void AStepFileWithItsOwnValuesIsReadInFileOrder()
    {
        string path = Path.Combine(Path.GetTempPath(), "tmdq-steps-" + Guid.NewGuid().ToString("N") + ".toml");
        File.WriteAllText(path, "[[Step]]\ndb = 30.5\n[[Step]]\ndb = -3\n[[Step]]\ndb = 12.25\n");
        try
        {
            Assert.Equal(new[] { 30.5, -3.0, 12.25 }, StepFile.Parse(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void TheInitialStepFileIsTheOneThePreferredNamesElseTheShippedDefault()
    {
        string root = Env.RepoRoot.Replace('\\', '/');
        string shipped = root + "/settings/rcvr_cals/default.toml";

        Assert.Equal(shipped, StepFile.InitialPath(root, null));
        Assert.Equal(shipped, StepFile.InitialPath(root, ""));

        string trc = root + "/settings/rcvr_cals/TRC.toml";
        Assert.Equal(trc, StepFile.InitialPath(root, trc));                              // an existing file wins
        Assert.Equal(shipped, StepFile.InitialPath(root, root + "/no/such.toml"));       // a stale one is passed over
        Assert.Equal(string.Empty, StepFile.InitialPath(null, null));                     // nothing to go on
        Assert.Equal(trc, StepFile.InitialPath(null, trc));                               // ...unless a file is named

        // And with non-ASCII text, which crosses the boundary as UTF-8 and comes back whole.
        string folder = Path.Combine(Path.GetTempPath(), "tmdq-café-Ω-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder);
        string named = Path.Combine(folder, "steps-é.toml").Replace('\\', '/');
        File.WriteAllText(named, "[[Step]]\ndb = 1\n");
        try
        {
            Assert.Equal(named, StepFile.InitialPath(root, named));
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    // --- a calibration, compared with the core's own run

    private static void AssertChannel(string expected, CalibrationChannel c, int index)
    {
        string[] f = expected.Split('|');
        Assert.Equal(index, int.Parse(f[0]));
        string at = $"channel {index} ({f[1]})";
        Assert.True(f[1] == c.Name, $"{at}: name was {c.Name}");
        Assert.True(int.Parse(f[2]) == c.WordIndex, $"{at}: word");
        Assert.True((f[3] == "1") == c.Valid, $"{at}: valid");
        Assert.True((f[4] == "1") == c.HadData, $"{at}: had data");
        Assert.True(int.Parse(f[5]) == (int)c.Outcome, $"{at}: outcome was {c.Outcome}");
        Assert.True(int.Parse(f[6]) == c.DetectedPlateaus, $"{at}: detected plateaus");
        Assert.True((f[7] == "1") == c.ExtraPlateaus, $"{at}: extra plateaus");
        Assert.True((f[8] == "1") == c.ThresholdRelaxed, $"{at}: threshold relaxed");
        Assert.True((f[9] == "1") == c.Saturated, $"{at}: saturated");
        Assert.True(int.Parse(f[10]) == c.SweepLevels, $"{at}: sweep levels");
        Assert.True(Parse(f[11]) == c.EdgeThreshold, $"{at}: edge threshold {c.EdgeThreshold:R}");
        Assert.True(int.Parse(f[12]) == c.Points.Count, $"{at}: point count");
        double raw = 0, db = 0;
        foreach (CalibrationPoint p in c.Points)
        {
            raw += p.RawAvg;
            db += p.TrueDb;
        }
        Assert.True(Parse(f[13]) == raw, $"{at}: sum of raw averages {raw:R}");
        Assert.True(Parse(f[14]) == db, $"{at}: sum of dB {db:R}");
        Assert.True(int.Parse(f[15]) == c.PlateauLevels.Count, $"{at}: plateau level count");
        double levels = 0;
        foreach (double v in c.PlateauLevels)
        {
            levels += v;
        }
        Assert.True(Parse(f[16]) == levels, $"{at}: sum of plateau levels {levels:R}");
        Assert.True(int.Parse(f[17]) == c.UnresolvedDb.Count, $"{at}: unresolved count");
        double unresolved = 0;
        foreach (double v in c.UnresolvedDb)
        {
            unresolved += v;
        }
        Assert.True(Parse(f[18]) == unresolved, $"{at}: sum of unresolved dB");
    }

    [Fact]
    public void ACalibrationOfTheStepCalRecordingMatchesTheCoresOwn()
    {
        (Collector events, CalibrationResult result) = RunToTheEnd(Request());

        Assert.True(events.Success);
        Assert.Equal(CalibrationDigest.Summary, result.Summary);
        Assert.Equal(string.Empty, result.Error);
        Assert.Equal(CalibrationDigest.Report, result.Report);
        Assert.Equal(Parse(CalibrationDigest.Seconds), result.RecordingSeconds);
        Assert.False(result.ClipIgnored);

        Assert.Equal(CalibrationDigest.Channels.Length, result.Channels.Count);
        for (int i = 0; i < result.Channels.Count; i++)
        {
            AssertChannel(CalibrationDigest.Channels[i], result.Channels[i], i);
        }

        // Profiles are for the channels that calibrated, and carry their word and points as they are.
        IReadOnlyList<CalibrationProfile> profiles = result.Profiles();
        Assert.Equal(new[] { 0, 1, 2, 6, 7, 8, 12, 13, 14, 15, 16, 17 }, profiles.Select(p => p.WordIndex));
        Assert.All(profiles, p => Assert.True(p.Valid));
        CalibrationChannel first = result.Channels[0];
        Assert.Equal(first.Points, profiles[0].Points);
        Assert.Equal(first.ToProfile(), profiles[0]);
        Assert.False(result.Channels[3].Valid);
        Assert.False(result.Channels[3].ToProfile().Valid);
        Assert.Empty(result.Channels[3].Points);
    }

    [Fact]
    public void TheDocumentedOrderOfWhatAnObserverIsToldHolds()
    {
        (Collector c, _) = RunToTheEnd(Request());
        IReadOnlyList<string> events = c.Events;

        Assert.Equal("finished:1", events[^1]);                  // LAST
        Assert.Equal(1, c.FinishedCount);                        // and once
        Assert.DoesNotContain(events, e => e.StartsWith("stream:"));
        Assert.DoesNotContain(events, e => e.StartsWith("error:"));
        Assert.Contains(events, e => e.StartsWith("log:"));
        Assert.Equal(1, c.DistinctProgressThreads);
        Assert.Equal(c.Progress.OrderBy(p => p), c.Progress);
        Assert.Equal(100, c.Progress[^1]);
    }

    [Fact]
    public void ThePointsATrueCalibrationFoundFeedARunAndGiveTheCoresNumbers()
    {
        (_, CalibrationResult result) = RunToTheEnd(Request());

        var receiver = new StreamConfig
        {
            PcmChannelId = 26, Label = "step", Mode = StreamMode.ReceiverChannelInfo, SyncPattern = "FE6B2840", BitsInMinorFrame = 800,
            Calibration = result.Profiles(),
        };
        var collector = new Collector();
        using ProcessingRun run = ProcessingRun.Start(new RunRequest(Env.StepCal, 1, new[] { receiver }) { AppRoot = Env.RepoRoot }, collector);
        Assert.True(collector.WaitFinished(Timeout));
        run.Wait();

        ProcessedStream s = Assert.Single(collector.Results);
        double sum = 0;
        foreach (ChannelData ch in s.Channels)
        {
            foreach (double v in ch.Values)
            {
                sum += v;
            }
        }
        Assert.Equal(CalibrationDigest.RunSamples, s.TimesSec.Length);
        Assert.Equal(CalibrationDigest.RunChannels, s.Channels.Count);
        Assert.Equal(Parse(CalibrationDigest.RunSum), sum);
    }

    // --- every setting reaches the extractor

    private static (bool Ok, int Channels, int Calibrated, int Plateaus, double Seconds, double Raw, double Db) Totals(CalibrationResult r, bool ok)
    {
        double raw = 0, db = 0;
        foreach (CalibrationChannel c in r.Channels)
        {
            foreach (CalibrationPoint p in c.Points)
            {
                raw += p.RawAvg;
                db += p.TrueDb;
            }
        }
        return (ok, r.Channels.Count, r.Channels.Count(c => c.Valid), r.Channels.Sum(c => c.DetectedPlateaus), r.RecordingSeconds, raw, db);
    }

    private static void AssertCase(string name, CalibrationRequest request)
    {
        string line = CalibrationDigest.Cases.Single(c => c.StartsWith(name + "|"));
        string[] f = line.Split('|');
        (Collector events, CalibrationResult result) = RunToTheEnd(request);
        var got = Totals(result, events.Success);

        Assert.True((f[1] == "1") == got.Ok, $"{name}: success was {got.Ok}: {result.Summary}");
        Assert.True(int.Parse(f[2]) == got.Channels, $"{name}: channels {got.Channels}");
        Assert.True(int.Parse(f[3]) == got.Calibrated, $"{name}: calibrated {got.Calibrated}");
        Assert.True(int.Parse(f[4]) == got.Plateaus, $"{name}: plateaus {got.Plateaus}");
        Assert.True(Parse(f[5]) == got.Seconds, $"{name}: seconds {got.Seconds:R}");
        Assert.True(Parse(f[6]) == got.Raw, $"{name}: raw sum {got.Raw:R}");
        Assert.True(Parse(f[7]) == got.Db, $"{name}: dB sum {got.Db:R}");
        if (!got.Ok)
        {
            Assert.NotEmpty(result.Error);           // a failure says why
            Assert.Contains(events.Events, e => e.StartsWith("error:"));
        }
    }

    [Fact]
    public void TheUnchangedCalibrationIsTheBaseOfTheVariations()
    {
        AssertCase("base", Request());
    }

    [Fact]
    public void EverySettingOfACalibrationReachesTheExtractor()
    {
        // Each of these changes one thing, and the core's result for it differs from the base's (so a setting the
        // library forgot to carry would give the base's numbers). Several make the extraction fail outright:
        // the wrong code format or a mask that matches nothing finds no sync, which is a result too.
        AssertCase("clips", Request(r => r with { ClipStartSec = 2.0, ClipEndSec = 5.0 }));     // asymmetric: a swap would show
        AssertCase("random", Request(r => r with { Randomized = true }));
        AssertCase("invert", Request(r => r with { Inverted = true }));
        AssertCase("rate", Request(r => r with { DataRateMbps = 0.4 }));
        AssertCase("mask", Request(r => r with { SyncMask = "FFFF0000" }));
        AssertCase("bits", Request(r => r with { BitsInMinorFrame = 1600 }));
        AssertCase("steps-short", Request(r => r with { Steps = r.Steps.Take(r.Steps.Count - 1).ToArray() }));
    }

    [Fact]
    public void TheByteOrderOfTheFileAndThePcmChannelReachTheExtractor()
    {
        // Without the swap the pattern is destroyed and nothing syncs; a channel that is not in the file cannot be opened.
        (Collector plain, CalibrationResult noSwap) = RunToTheEnd(Request(r => r with { SwapBytes = false }));
        Assert.False(plain.Success);
        Assert.Empty(noSwap.Channels);
        Assert.NotEmpty(noSwap.Error);

        (Collector ghost, CalibrationResult none) = RunToTheEnd(Request(r => r with { PcmChannelId = 1026 }));
        Assert.False(ghost.Success);
        Assert.Empty(none.Channels);
        Assert.NotEmpty(none.Error);
    }

    [Fact]
    public void TheReceiverGridIsUsedOnlyWhenNoWordMapIsFound()
    {
        string noRoot = Path.Combine(Path.GetTempPath(), "tmdq-no-root-" + Guid.NewGuid().ToString("N"));

        // Two receivers of one channel are L_RCVR1 and L_RCVR2; one receiver of two channels is L_RCVR1 and R_RCVR1.
        (_, CalibrationResult wide) = RunToTheEnd(Request(r => r with { AppRoot = noRoot, NumReceivers = 2, ReceiverChannels = 1 }));
        Assert.Equal(new[] { "L_RCVR1", "L_RCVR2" }, wide.Channels.Select(c => c.Name));
        AssertCase("grid-2x1", Request(r => r with { AppRoot = noRoot, NumReceivers = 2, ReceiverChannels = 1 }));

        (_, CalibrationResult tall) = RunToTheEnd(Request(r => r with { AppRoot = noRoot, NumReceivers = 1, ReceiverChannels = 2 }));
        Assert.Equal(new[] { "L_RCVR1", "R_RCVR1" }, tall.Channels.Select(c => c.Name));
        AssertCase("grid-1x2", Request(r => r with { AppRoot = noRoot, NumReceivers = 1, ReceiverChannels = 2 }));
    }

    [Fact]
    public void AWordMapFileAndTheApplicationRootsDefaultAreUsed()
    {
        string root = Path.Combine(Path.GetTempPath(), "tmdq-cal-root-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "settings", "receiver_params"));
        File.WriteAllText(Path.Combine(root, "settings", "receiver_params", "default.toml"), "[C_RCVR1]\nWord = 3\n[R_RCVR1]\nWord = 4\n");
        string named = Path.Combine(root, "one.toml");
        File.WriteAllText(named, "[L_RCVR1]\nWord = 2\n");
        try
        {
            (_, CalibrationResult own) = RunToTheEnd(Request(r => r with { AppRoot = root }));
            Assert.Equal(new[] { "C_RCVR1", "R_RCVR1" }, own.Channels.Select(c => c.Name));
            Assert.Equal(new[] { 2, 3 }, own.Channels.Select(c => c.WordIndex));       // "Word = 3" is the third word, index 2

            (_, CalibrationResult file) = RunToTheEnd(Request(r => r with { AppRoot = root, ReceiverParamsToml = named }));
            CalibrationChannel only = Assert.Single(file.Channels);
            Assert.Equal("L_RCVR1", only.Name);
            Assert.Equal(1, only.WordIndex);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    // --- clips that leave nothing

    [Fact]
    public void ClipsThatWouldLeaveNothingAreIgnoredAndSaid()
    {
        (_, CalibrationResult result) = RunToTheEnd(Request(r => r with { ClipStartSec = 1.0e6, ClipEndSec = 1.0e6 }));
        Assert.True(result.ClipIgnored);
        Assert.Equal(Parse(CalibrationDigest.Seconds), result.RecordingSeconds);
        Assert.Equal(48, result.Channels.Count);
        Assert.Equal(12, result.Channels.Count(c => c.Valid));
    }

    // --- failures

    [Fact]
    public void ACalibrationSettingTheExtractorCannotUseIsAJobErrorWithTheCoresMessage()
    {
        var spy = new Collector();
        TmdqException pattern = Assert.Throws<TmdqException>(() => CalibrationRun.Start(Request(r => r with { SyncPattern = "zz" }), spy));
        Assert.Equal(Status.Job, pattern.Status);
        Assert.Equal("Invalid frame sync pattern 'zz'.", pattern.Message);

        TmdqException empty = Assert.Throws<TmdqException>(() => CalibrationRun.Start(Request(r => r with { SyncPattern = "" }), spy));
        Assert.Equal("Invalid frame sync pattern ''.", empty.Message);

        TmdqException mask = Assert.Throws<TmdqException>(() => CalibrationRun.Start(Request(r => r with { SyncMask = "xyz" }), spy));
        Assert.Equal(Status.Job, mask.Status);
        Assert.Equal("Invalid frame sync mask 'xyz'.", mask.Message);

        Assert.Empty(spy.Events);        // nothing was started, so nothing was reported
    }

    [Fact]
    public void AnIncompleteRequestIsABadArgument()
    {
        TmdqException none = Assert.Throws<TmdqException>(() => CalibrationRun.Start(Request(r => r with { FileName = "" })));
        Assert.Equal(Status.BadArgument, none.Status);
        Assert.Throws<ArgumentNullException>(() => CalibrationRun.Start(null!));
    }

    [Fact]
    public void AFailureTheExtractorSeesBeforeItStartsIsReportedBeforeStartReturns()
    {
        string missing = Path.Combine(Path.GetTempPath(), "tmdq-no-cal-" + Guid.NewGuid().ToString("N") + ".ch10");
        var collector = new Collector();
        using CalibrationRun run = CalibrationRun.Start(Request(r => r with { FileName = missing }), collector);

        // No waiting: the error and the end were delivered during Start, and the run is already finished.
        Assert.Equal(1, collector.FinishedCount);
        Assert.False(collector.Success);
        Assert.Contains(collector.Events, e => e.StartsWith("error:"));
        Assert.Equal("finished:0", collector.Events[^1]);
        Assert.False(run.IsRunning);

        CalibrationResult result = run.GetResult();
        Assert.Empty(result.Channels);
        Assert.NotEmpty(result.Error);
        Assert.Equal(result.Error, collector.Events.First(e => e.StartsWith("error:")).Substring("error:".Length));

        // No steps at all is refused the same way, in the extractor's own words.
        var spy = new Collector();
        using CalibrationRun noSteps = CalibrationRun.Start(Request(r => r with { Steps = Array.Empty<double>() }), spy);
        Assert.Equal(1, spy.FinishedCount);
        Assert.False(spy.Success);
        CalibrationResult why = noSteps.GetResult();
        Assert.Equal("No calibration steps were provided.", why.Error);
        Assert.Equal("No calibration steps were provided.", why.Summary);
    }

    // --- state, cancel, dispose

    [Fact]
    public void AResultIsRefusedWhileTheExtractionRunsAndCancelEndsIt()
    {
        var collector = new Collector();
        using CalibrationRun run = CalibrationRun.Start(Request(), collector);
        Assert.True(run.IsRunning);

        TmdqException e = Assert.Throws<TmdqException>(() => run.GetResult());
        Assert.Equal(Status.State, e.Status);

        run.Cancel();
        run.Cancel();       // twice is as good as once
        Assert.True(collector.WaitFinished(Timeout));
        run.Wait();

        Assert.False(collector.Success);
        Assert.Equal("finished:0", collector.Events[^1]);
        Assert.False(run.IsRunning);
        CalibrationResult result = run.GetResult();
        Assert.Equal("Calibration extraction cancelled.", result.Summary);
        Assert.Equal(string.Empty, result.Error);
    }

    [Fact]
    public void DisposingAnExtractionInFlightCancelsAndWaitsAndAResultOutlivesTheRun()
    {
        var collector = new Collector();
        CalibrationRun run = CalibrationRun.Start(Request(), collector);
        run.Dispose();       // no cancel, no wait: dispose does both

        Assert.Equal(1, collector.FinishedCount);
        Assert.False(collector.Success);
        Assert.Equal("finished:0", collector.Events[^1]);
        run.Dispose();       // twice is harmless
        run.Cancel();
        run.Wait();
        Assert.False(run.IsRunning);
        Assert.Throws<ObjectDisposedException>(() => run.GetResult());

        // A result taken before disposal is a copy and survives it.
        CalibrationResult kept;
        using (CalibrationRun done = CalibrationRun.Start(Request(), null))
        {
            done.Wait();
            kept = done.GetResult();
        }
        Assert.Equal(CalibrationDigest.Summary, kept.Summary);
        Assert.Equal(48, kept.Channels.Count);
        Assert.Equal(11, kept.Channels[6].Points.Count);
    }

    [Fact]
    public void AnObserverThatThrowsDoesNotBreakTheExtraction()
    {
        var c = new Collector { Throwing = true };
        using CalibrationRun run = CalibrationRun.Start(Request(), c);
        Assert.True(c.WaitFinished(Timeout));
        run.Wait();

        Assert.NotNull(run.ObserverException);
        Assert.Equal(1, c.FinishedCount);
        Assert.Equal(12, run.GetResult().Channels.Count(ch => ch.Valid));
    }

    [Fact]
    public void ARunWithNoObserverJustRunsAndTheDefaultsAreTheApplicationsDefaults()
    {
        using CalibrationRun run = CalibrationRun.Start(Request());
        run.Wait();
        Assert.Equal(12, run.GetResult().Channels.Count(c => c.Valid));

        var d = new CalibrationRequest { FileName = "f", TimeChannelId = 1, PcmChannelId = 2, SyncPattern = "AB", BitsInMinorFrame = 800, Steps = Array.Empty<double>() };
        Assert.True(d.SwapBytes);
        Assert.False(d.Randomized);
        Assert.False(d.Inverted);
        Assert.Equal(16, d.NumReceivers);
        Assert.Equal(3, d.ReceiverChannels);
        Assert.Equal(0.0, d.DataRateMbps);
        Assert.Equal(0.0, d.ClipStartSec);
        Assert.Equal(0.0, d.ClipEndSec);
        Assert.Null(d.SyncMask);
        Assert.Null(d.ReceiverParamsToml);
        Assert.Null(d.AppRoot);
    }
}
