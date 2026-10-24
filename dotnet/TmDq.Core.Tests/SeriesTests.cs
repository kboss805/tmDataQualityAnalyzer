using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// Series construction through the managed library: the series a processed stream yields, the clock they share,
/// the identity of a stream and the order an imported CSV's streams are given. The expected answers are what the
/// core's own functions give (<see cref="SeriesDigest"/>), and the values the library hands out are also checked
/// against the stream they came from, which needs no recording of the core at all.
/// </summary>
public class SeriesTests
{
    private static readonly TimeSpan Timeout = TimeSpan.FromMinutes(3);

    public SeriesTests()
    {
        Env.EnsureLoaded();
    }

    // --- reading the recorded answers

    private static string Unhex(string hex) => hex == "-" ? string.Empty : Encoding.UTF8.GetString(Convert.FromHexString(hex));

    private static double D(string text) => double.Parse(text, CultureInfo.InvariantCulture);

    private static string[][] Answers(string kind) => SeriesDigest.Lines.Where(l => l.StartsWith(kind + "|")).Select(l => l.Split('|')).ToArray();

    private static double Sum(ReadOnlySpan<double> values)
    {
        double sum = 0;
        foreach (double v in values)
        {
            sum += v;
        }
        return sum;
    }

    // --- two real streams, kept

    private sealed class KeepAll : IRunObserver
    {
        public List<StreamResult> Results { get; } = new();

        public ManualResetEventSlim Done { get; } = new();

        public void OnStreamCompleted(StreamResult result)
        {
            lock (Results)
            {
                Results.Add(result);
            }
        }

        public void OnFinished(bool success) => Done.Set();
    }

    /// <summary>The step_cal recording as a lock stream (job 0) and a receiver stream (job 1) of source 3.</summary>
    private static StreamResult[] RunTwoStreams()
    {
        string pattern = "FE6B2840";
        var request = new RunRequest(Env.StepCal, 1, new[]
        {
            new StreamConfig { PcmChannelId = 26, Label = "Ch 26 é", SyncPattern = pattern, BitsInMinorFrame = 800 },
            new StreamConfig { PcmChannelId = 26, Label = "rx", Mode = StreamMode.ReceiverChannelInfo, SyncPattern = pattern, BitsInMinorFrame = 800 },
        })
        {
            SourceId = 3,
            AppRoot = Env.RepoRoot,
        };
        var keep = new KeepAll();
        using ProcessingRun run = ProcessingRun.Start(request, keep);
        Assert.True(keep.Done.Wait(Timeout, TestContext.Current.CancellationToken));
        run.Wait();
        return keep.Results.OrderBy(r => r.JobIndex).ToArray();
    }

    [Fact]
    public void TheKeyOfAStreamIsItsSourceItsLabelAndItsChannel()
    {
        StreamResult[] results = RunTwoStreams();
        try
        {
            foreach (string[] y in Answers("Y"))
            {
                StreamKey key = results[int.Parse(y[1])].Key;
                Assert.Equal(new StreamKey(int.Parse(y[2]), Unhex(y[3]), int.Parse(y[4])), key);
            }
            Assert.Equal(new StreamKey(3, "Ch 26 é", 26), results[0].Key);       // by hand, too
            Assert.Equal(new StreamKey(3, "rx", 26), results[1].Key);
            Assert.False(results[0].Key.IsSameStream(results[1].Key));                // the label differs
            Assert.True(results[0].Key.IsSameStream(results[0].Key));
        }
        finally
        {
            foreach (StreamResult r in results)
            {
                r.Dispose();
            }
        }
    }

    [Fact]
    public void TheSeriesOfRealStreamsAreWhatTheCoreMakes()
    {
        StreamResult[] results = RunTwoStreams();
        try
        {
            string[][] elapsedLines = Answers("E");
            string[][] seriesLines = Answers("p");
            Assert.Equal(6, elapsedLines.Length);       // two streams at three bases
            var wrong = new List<string>();

            foreach (string[] e in elapsedLines)
            {
                int job = int.Parse(e[1]);
                double baseAbs = D(e[2]);
                using SeriesSet set = results[job].BuildSeries(baseAbs);
                string at = $"job {job} base {baseAbs:R}";

                ReadOnlySpan<double> elapsed = set.ElapsedSeconds;
                if (elapsed.Length != int.Parse(e[3]) || Sum(elapsed) != D(e[4]) || elapsed[0] != D(e[5]) || elapsed[^1] != D(e[6]) || set.Count != int.Parse(e[7]))
                {
                    wrong.Add($"{at}: elapsed or count");
                    continue;
                }

                // What needs no recording: the elapsed time is the sample's time less the base.
                ReadOnlySpan<double> times = results[job].TimesSec;
                for (int i = 0; i < times.Length; i++)
                {
                    if (elapsed[i] != times[i] - baseAbs)
                    {
                        wrong.Add($"{at}: elapsed[{i}] is not the sample time less the base");
                        break;
                    }
                }

                string[][] want = seriesLines.Where(p => p[1] == e[1] && p[2] == e[2]).ToArray();
                if (want.Length != set.Count)
                {
                    wrong.Add($"{at}: {set.Count} series instead of {want.Length}");
                    continue;
                }
                for (int i = 0; i < want.Length; i++)
                {
                    string[] w = want[i];
                    SeriesSpec g = set.Specs[i];
                    ReadOnlySpan<double> values = set.Values(i);
                    bool ok = (int)g.Metric == int.Parse(w[4]) && g.Name == Unhex(w[5]) && g.StreamLabel == Unhex(w[6]) &&
                              g.ReceiverIndex == int.Parse(w[7]) && g.ChannelIndex == int.Parse(w[8]) && g.StreamOrder == int.Parse(w[9]) &&
                              g.StreamSequence == int.Parse(w[10]) && g.SourceId == int.Parse(w[11]) && g.YMin == D(w[12]) && g.YMax == D(w[13]) &&
                              g.ValueCount == int.Parse(w[14]) && values.Length == g.ValueCount && Sum(values) == D(w[15]) &&
                              values[0] == D(w[16]) && values[^1] == D(w[17]);
                    if (!ok)
                    {
                        wrong.Add($"{at}: series {i} ({g.Name}) differs");
                    }
                }
            }
            Assert.True(wrong.Count == 0, string.Join("\n", wrong));
        }
        finally
        {
            foreach (StreamResult r in results)
            {
                r.Dispose();
            }
        }
    }

    [Fact]
    public void TheValuesOfASeriesAreTheStreamsOwnData()
    {
        StreamResult[] results = RunTwoStreams();
        try
        {
            using SeriesSet lockSet = results[0].BuildSeries(0.0);
            Assert.Equal(new[] { MetricType.FrameSyncLock, MetricType.AccumulatedMissedFrames }, lockSet.Specs.Select(s => s.Metric));
            Assert.Equal(results[0].LockPercent.ToArray(), lockSet.Values(0).ToArray());
            Assert.Equal(results[0].MissedFrames.ToArray(), lockSet.Values(1).ToArray());
            Assert.All(lockSet.Specs, s => Assert.Equal(("Ch 26 é", "Ch 26 é", 26, 0, 3), (s.Name, s.StreamLabel, s.StreamOrder, s.StreamSequence, s.SourceId)));

            using SeriesSet rxSet = results[1].BuildSeries(0.0);
            Assert.Equal(results[1].ChannelCount, rxSet.Count);                     // a series per receiver channel, no lock series
            Assert.All(rxSet.Specs, s => Assert.Equal(MetricType.Snr, s.Metric));
            for (int i = 0; i < rxSet.Count; i++)
            {
                Assert.Equal("26 - rx " + results[1].ChannelName(i), rxSet.Specs[i].Name);       // "<channel> - <label> <name>", as the CSV spells it
                Assert.Equal(results[1].ChannelValues(i).ToArray(), rxSet.Values(i).ToArray());
                Assert.Equal(1, rxSet.Specs[i].StreamSequence);
            }
            Assert.Equal((1, 0), (rxSet.Specs[0].ReceiverIndex, rxSet.Specs[0].ChannelIndex));          // L_RCVR1
            Assert.Equal((1, 1), (rxSet.Specs[1].ReceiverIndex, rxSet.Specs[1].ChannelIndex));          // R_RCVR1
            Assert.Equal((2, 0), (rxSet.Specs[3].ReceiverIndex, rxSet.Specs[3].ChannelIndex));          // L_RCVR2
        }
        finally
        {
            foreach (StreamResult r in results)
            {
                r.Dispose();
            }
        }
    }

    [Fact]
    public void ASetKeepsItsDataAliveAfterItsResultIsDisposedAndRefusesAfterItIs()
    {
        StreamResult[] results = RunTwoStreams();
        double[] expected = results[0].LockPercent.ToArray();
        SeriesSet set = results[0].BuildSeries(100.0);
        foreach (StreamResult r in results)
        {
            r.Dispose();                    // the result goes first
        }

        Assert.Equal(expected, set.Values(0).ToArray());          // and the set still reads
        Assert.Equal(66, set.ElapsedSeconds.Length);

        Assert.Throws<ArgumentOutOfRangeException>(() => set.Values(2));
        Assert.Throws<ArgumentOutOfRangeException>(() => set.Values(-1));
        set.Dispose();
        set.Dispose();                      // twice is harmless
        Assert.Throws<ObjectDisposedException>(() => set.ElapsedSeconds.Length);
        Assert.Throws<ObjectDisposedException>(() => set.Values(0).Length);
        Assert.Equal(2, set.Specs.Count);   // the metadata is a copy and survives
        Assert.Throws<ObjectDisposedException>(() => results[0].BuildSeries(0));
        Assert.Throws<ObjectDisposedException>(() => results[0].Key);
    }

    // --- the shared time base

    [Fact]
    public void ANewTimeBaseHasNoTime()
    {
        using var tb = new TimeBase();
        Assert.Equal(new TimeBaseInfo(0, 0.0, 0.0), tb.Info);
    }

    [Fact]
    public void ALongSequenceOfTimeBaseOperationsAgreesWithTheCoreStepByStep()
    {
        string[][] steps = Answers("T");
        string[] ops = SeriesDigest.Ops.Where(o => o.StartsWith("t ")).ToArray();
        Assert.Equal(ops.Length, steps.Length);

        using var tb = new TimeBase();
        var wrong = new List<string>();
        for (int i = 0; i < ops.Length; i++)
        {
            string[] op = ops[i].Split(' ');
            string[] want = steps[i];
            string result = "-";
            switch (op[1])
            {
                case "reset": tb.Reset(); break;
                case "clock": tb.SetClock(int.Parse(op[2]), D(op[3])); break;
                case "overlaps": result = tb.Overlaps(D(op[2]), D(op[3]), D(op[4])) ? "1" : "0"; break;
                case "adopt": result = tb.Adopt(D(op[2]), op[3] == "1").ToString("R", CultureInfo.InvariantCulture); break;
                case "advance": result = tb.Advance(op[2] == "1" ? D(op[3]) : null).ToString("R", CultureInfo.InvariantCulture); break;
                default: throw new InvalidOperationException(ops[i]);
            }

            TimeBaseInfo info = tb.Info;
            bool sameResult = result == "-" ? want[3] == "-" : want[3] != "-" && D(result) == D(want[3]);
            bool same = want[2] == op[1] && sameResult &&
                        info.BaseDay == int.Parse(want[4]) && info.BaseAbsSeconds == D(want[5]) && info.BaseTimeOffset == D(want[6]);
            if (!same)
            {
                wrong.Add($"step {i} ({ops[i]}): result {result}, base {info}; the core: {string.Join("|", want.Skip(3))}");
            }
        }
        Assert.True(wrong.Count == 0, string.Join("\n", wrong));
    }

    [Fact]
    public void TheTimeBaseSequenceReachesEveryKindOfAnswer()
    {
        string[][] steps = Answers("T");
        Assert.Contains(steps, s => s[2] == "adopt" && D(s[3]) != 0);                      // a base that moved earlier
        Assert.Contains(steps, s => s[2] == "adopt" && D(s[3]) == 0);                      // one that did not
        Assert.Contains(steps, s => s[2] == "advance" && D(s[3]) != 0);
        Assert.Contains(steps, s => s[2] == "advance" && D(s[3]) == 0);
        Assert.Contains(steps, s => s[2] == "overlaps" && s[3] == "1");
        Assert.Contains(steps, s => s[2] == "overlaps" && s[3] == "0");
        Assert.Contains(steps, s => s[2] == "clock");
        Assert.Contains(steps, s => s[2] == "reset");
        Assert.Contains(steps, s => s[4] != "0");                                          // a base with a day
    }

    [Fact]
    public void ATimeBaseWorksByHandForAFirstAnEarlierAndALaterStream()
    {
        using var tb = new TimeBase();

        // The first stream: the base becomes its first sample, nothing shifts.
        Assert.Equal(0.0, tb.Adopt(1000.0, hasSeries: false));
        Assert.Equal(1000.0, tb.Info.BaseAbsSeconds);

        // One that starts earlier moves the base back, and every existing series shifts right by the difference.
        Assert.Equal(250.0, tb.Adopt(750.0, hasSeries: true));
        Assert.Equal(750.0, tb.Info.BaseAbsSeconds);

        // One that starts later leaves the base where it is.
        Assert.Equal(0.0, tb.Adopt(900.0, hasSeries: true));
        Assert.Equal(750.0, tb.Info.BaseAbsSeconds);

        // A source went and the earliest remaining sample is 40 s after the base: the base moves up and series shift left.
        Assert.Equal(40.0, tb.Advance(40.0));
        Assert.Equal(790.0, tb.Info.BaseAbsSeconds);
        Assert.Equal(0.0, tb.Advance(null));                         // nothing left: nothing to move

        // Touching at one instant counts as overlapping.
        Assert.True(tb.Overlaps(790.0 + 10.0, 900.0, 10.0));
        Assert.False(tb.Overlaps(790.0 + 10.5, 900.0, 10.0));
        Assert.True(tb.Overlaps(0.0, 790.0, 5.0));

        tb.SetClock(120, 77125.5);
        Assert.Equal(new TimeBaseInfo(120, 790.0, 77125.5), tb.Info);        // the absolute base is not touched
        tb.Reset();
        Assert.Equal(new TimeBaseInfo(0, 0.0, 0.0), tb.Info);
    }

    [Fact]
    public void ADisposedTimeBaseRefusesEveryCall()
    {
        var tb = new TimeBase();
        tb.Dispose();
        tb.Dispose();
        Assert.Throws<ObjectDisposedException>(() => tb.Info);
        Assert.Throws<ObjectDisposedException>(() => tb.Reset());
        Assert.Throws<ObjectDisposedException>(() => tb.SetClock(1, 0));
        Assert.Throws<ObjectDisposedException>(() => tb.Overlaps(0, 1, 1));
        Assert.Throws<ObjectDisposedException>(() => tb.Adopt(0, false));
        Assert.Throws<ObjectDisposedException>(() => tb.Advance(null));
    }

    // --- stream identity

    [Fact]
    public void TwoStreamsAreTheSameOnlyIfSourceLabelAndChannelAllAgree()
    {
        string[][] q = Answers("Q");
        string[] ops = SeriesDigest.Ops.Where(o => o.StartsWith("Q ")).ToArray();
        Assert.Equal(ops.Length, q.Length);

        for (int i = 0; i < ops.Length; i++)
        {
            string[] f = ops[i].Split(' ');
            var a = new StreamKey(int.Parse(f[2]), Unhex(f[3]), int.Parse(f[4]));
            var b = new StreamKey(int.Parse(f[5]), Unhex(f[6]), int.Parse(f[7]));
            bool core = q[i][2] == "1";

            Assert.True(a.IsSameStream(b) == core, $"{f[1]}: the core says {core}");
            Assert.True(b.IsSameStream(a) == core, $"{f[1]}: not symmetric");
            Assert.True((a == b) == core, $"{f[1]}: == disagrees with the core");        // the managed equality is the same rule
        }
        Assert.Contains(q, x => x[2] == "1");
        Assert.Contains(q, x => x[2] == "0");
    }

    // --- the order an imported CSV's streams are given

    [Fact]
    public void ImportedStreamsAreOrderedAsTheCoreOrdersThem()
    {
        string[] ops = SeriesDigest.Ops;
        string[][] answers = Answers("I");
        int seen = 0;
        for (int i = 0; i < ops.Length; i++)
        {
            if (!ops[i].StartsWith("I "))
            {
                continue;
            }

            string[] head = ops[i].Split(' ');
            var series = new List<ImportedSeriesInfo>();
            for (int k = 0; k < int.Parse(head[2]); k++)
            {
                string[] s = ops[++i].Split(' ');
                series.Add(new ImportedSeriesInfo((MetricType)int.Parse(s[1]), Unhex(s[2]), int.Parse(s[3]), int.Parse(s[4])));
            }

            string[] want = answers.Single(a => a[1] == head[1]);
            string orders = series.Count == 0 ? "-" : string.Join(",", ImportedStreams.AssignOrders(series));
            Assert.True(want[2] == orders, $"{head[1]}: {orders} instead of {want[2]}");
            seen++;
        }
        Assert.Equal(answers.Length, seen);
    }

    [Fact]
    public void AParsedCsvsStreamsAreOrderedLikeTheQtTestsExpect()
    {
        // The expected orders are the ones TestCsvSeries pins for these two files, read through the plot.
        (string File, int[] Orders)[] cases =
        {
            ("lock-orders-after-snr", new[] { 41, 40, 40, 41 }),        // Zed Lock, 40 - A L_RCVR1, A Lock, Zed Missed
            ("two-sources-one-label", new[] { 1, 2, 2 }),                // A Lock; S2| A Lock; S2| A Missed
            ("all-three-kinds", new[] { 41, 41, 40 }),                   // A Lock, A Missed, 40 - Ch A L_RCVR2
        };
        foreach ((string file, int[] orders) in cases)
        {
            string path = Path.Combine(Path.GetTempPath(), "tmdq-orders-" + Guid.NewGuid().ToString("N") + ".csv");
            File.WriteAllBytes(path, Convert.FromBase64String(FormatsDigest.CsvFiles[file]));
            try
            {
                CsvData csv = CsvFile.Parse(path);
                Assert.Equal(orders, ImportedStreams.AssignOrders(csv.Series));
            }
            finally
            {
                File.Delete(path);
            }
        }
    }

    [Fact]
    public void AnUnknownMetricIsRefused()
    {
        TmdqException e = Assert.Throws<TmdqException>(
            () => ImportedStreams.AssignOrders(new[] { new ImportedSeriesInfo((MetricType)99, "A", 0, 1) }));
        Assert.Equal(Status.BadArgument, e.Status);
        Assert.NotEmpty(e.Message);
    }

    [Fact]
    public void NullListsAreRefusedAndNothingIsAnEmptyList()
    {
        Assert.Throws<ArgumentNullException>(() => ImportedStreams.AssignOrders((IReadOnlyList<ImportedSeriesInfo>)null!));
        Assert.Throws<ArgumentNullException>(() => ImportedStreams.AssignOrders((IReadOnlyList<CsvSeries>)null!));
        Assert.Empty(ImportedStreams.AssignOrders(Array.Empty<ImportedSeriesInfo>()));
    }
}
