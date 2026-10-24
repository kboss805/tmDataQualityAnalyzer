using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// The file formats through the managed library: the frame-sync pattern file, template matching, the CSV export
/// and import, and clock stamps. Every expected answer is what the core's own functions gave for the same input
/// (<see cref="FormatsDigest"/>), so the managed side is compared with the code behind the Qt application, over
/// a corpus that reaches the odd inputs: byte-order marks, UTF-16, legacy keys, padding, signs, overflow,
/// malformed rows and times.
/// </summary>
public class FormatsTests
{
    public FormatsTests()
    {
        Env.EnsureLoaded();
    }

    // --- reading the recorded answers

    private static string Unhex(string hex) => hex == "-" ? string.Empty : Encoding.UTF8.GetString(Convert.FromHexString(hex));

    private static double D(string text)
    {
        string t = text.Trim().ToLowerInvariant();
        if (t.StartsWith("nan") || t.StartsWith("-nan"))
        {
            // Positive, as the C library's strtod makes it (double.NaN has the sign bit set, which the writer prints differently).
            return BitConverter.Int64BitsToDouble(0x7FF8000000000000);
        }
        if (t == "inf")
        {
            return double.PositiveInfinity;
        }
        if (t == "-inf")
        {
            return double.NegativeInfinity;
        }
        return double.Parse(text, CultureInfo.InvariantCulture);
    }

    private static double[] Ds(string list) => list == "-" ? Array.Empty<double>() : list.Split(',').Select(D).ToArray();

    private static int[] Is(string list) => list == "-" ? Array.Empty<int>() : list.Split(',').Select(int.Parse).ToArray();

    private static bool Same(double a, double b) => double.IsNaN(a) ? double.IsNaN(b) : a.Equals(b);

    private static bool Same(IReadOnlyList<double> a, IReadOnlyList<double> b) => a.Count == b.Count && a.Zip(b, Same).All(x => x);

    private static string[] Answers(string kind) => FormatsDigest.Lines.Where(l => l.StartsWith(kind + "|")).ToArray();

    private static string TempFile(string name, byte[] bytes)
    {
        string path = Path.Combine(Path.GetTempPath(), "tmdq-formats-" + Guid.NewGuid().ToString("N") + "-" + name);
        File.WriteAllBytes(path, bytes);
        return path;
    }

    private sealed class Ops
    {
        public List<string[]> W = new(), M = new(), K = new();
        public List<(string[] Head, List<string[]> Cols)> X = new();

        public Ops()
        {
            string[] lines = FormatsDigest.Ops;
            for (int i = 0; i < lines.Length; i++)
            {
                string[] f = lines[i].Split(' ');
                switch (f[0])
                {
                    case "W": W.Add(f); break;
                    case "M": M.Add(f); break;
                    case "K": K.Add(f); break;
                    case "X":
                        var cols = new List<string[]>();
                        for (int c = 0; c < int.Parse(f[4]); c++)
                        {
                            cols.Add(lines[++i].Split(' '));
                        }
                        X.Add((f, cols));
                        break;
                    default: throw new InvalidOperationException("unknown op " + lines[i]);
                }
            }
        }
    }

    // --- the frame-sync pattern file

    [Fact]
    public void EveryPatternFileReadsAsTheCoreReadsIt()
    {
        string[] answers = Answers("P");
        Assert.Equal(FormatsDigest.PatternFiles.Count, answers.Length);

        var wrong = new List<string>();
        foreach (string line in answers)
        {
            string[] f = line.Split('|');
            string path = TempFile(f[1] + ".toml", Convert.FromBase64String(FormatsDigest.PatternFiles[f[1]]));
            try
            {
                FrameSyncFileData got = FrameSyncFile.Read(path);
                var want = new FrameSyncFileData(Unhex(f[2]), Unhex(f[3]), f[4] == "1" ? int.Parse(f[5]) : null, f[6] == "1", f[7] == "1", f[8] == "1" ? f[9] == "1" : null);
                if (got != want)
                {
                    wrong.Add($"{f[1]}: {got} instead of {want}");
                }
            }
            finally
            {
                File.Delete(path);
            }
        }
        Assert.True(wrong.Count == 0, string.Join("\n", wrong));
    }

    [Fact]
    public void ThePatternCorpusReachesEveryKindOfAnswer()
    {
        // A corpus that never produced a legacy length, an absent Randomized key or a non-empty mask would prove little.
        string[][] rows = Answers("P").Select(l => l.Split('|')).ToArray();
        Assert.Contains(rows, f => f[4] == "1" && f[6] == "1");        // a length from the older key
        Assert.Contains(rows, f => f[4] == "1" && f[6] == "0");        // a length from BitsPerFrame
        Assert.Contains(rows, f => f[4] == "0");                       // no usable length
        Assert.Contains(rows, f => f[7] == "1");                       // inverted
        Assert.Contains(rows, f => f[8] == "0");                       // no Randomized key
        Assert.Contains(rows, f => f[8] == "1" && f[9] == "1");        // randomized
        Assert.Contains(rows, f => f[8] == "1" && f[9] == "0");        // not randomized, said so
        Assert.Contains(rows, f => f[3] != "-");                       // a mask
        Assert.Contains(rows, f => f[2] == "-");                       // no pattern
    }

    [Fact]
    public void AFileThatIsNotThereReadsAsAFileThatSaysNothing()
    {
        string missing = Path.Combine(Path.GetTempPath(), "tmdq-no-pattern-" + Guid.NewGuid().ToString("N") + ".toml");
        Assert.Equal(new FrameSyncFileData(string.Empty, string.Empty, null, false, false, null), FrameSyncFile.Read(missing));
        Assert.Throws<ArgumentNullException>(() => FrameSyncFile.Read(null!));
    }

    [Fact]
    public void APatternFileInANonAsciiFolderIsRead()
    {
        string folder = Path.Combine(Path.GetTempPath(), "tmdq-café-Ω-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder);
        string path = Path.Combine(folder, "pattern-é.toml");
        File.WriteAllText(path, "[Frame]\nFrameSync = \"FE6B2840\"\nBitsPerFrame = 800\n");
        try
        {
            Assert.Equal(new FrameSyncFileData("FE6B2840", string.Empty, 800, false, false, null), FrameSyncFile.Read(path));
            FrameSyncFile.Write(path, "AB", null, 64, false, false);
            Assert.Equal("AB", FrameSyncFile.Read(path).Pattern);
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    [Fact]
    public void EveryPatternWritesTheBytesTheCoreWrites()
    {
        var ops = new Ops();
        string[] answers = Answers("W");
        Assert.Equal(ops.W.Count, answers.Length);

        foreach (string[] w in ops.W)
        {
            string[] want = answers.Single(a => a.StartsWith("W|" + w[1] + "|")).Split('|');
            string path = Path.Combine(Path.GetTempPath(), "tmdq-write-" + Guid.NewGuid().ToString("N") + ".toml");
            try
            {
                FrameSyncFile.Write(path, Unhex(w[2]), Unhex(w[3]), int.Parse(w[4]), w[5] == "1", w[6] == "1");
                Assert.True(want[2] == "1", w[1]);
                Assert.True(want[3] == Convert.ToHexString(File.ReadAllBytes(path)).ToLowerInvariant(), $"{w[1]}: the file differs from the core's");
            }
            finally
            {
                File.Delete(path);
            }
        }
    }

    [Fact]
    public void WhatIsWrittenIsReadBack()
    {
        string path = Path.Combine(Path.GetTempPath(), "tmdq-roundtrip-" + Guid.NewGuid().ToString("N") + ".toml");
        try
        {
            FrameSyncFile.Write(path, "334AABBF", "FFFFFFFF", 32767, true, false);
            Assert.Equal(new FrameSyncFileData("334AABBF", "FFFFFFFF", 32767, false, true, false), FrameSyncFile.Read(path));
            FrameSyncFile.Write(path, "FE6B2840", null, 800, false, true);
            Assert.Equal(new FrameSyncFileData("FE6B2840", string.Empty, 800, false, false, true), FrameSyncFile.Read(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void APatternThatCannotBeWrittenIsAnIoError()
    {
        string folder = Path.Combine(Path.GetTempPath(), "tmdq-a-folder-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder);
        try
        {
            TmdqException e = Assert.Throws<TmdqException>(() => FrameSyncFile.Write(folder, "AB", "", 64, false, false));    // a folder is not a file
            Assert.Equal(Status.Io, e.Status);
            Assert.Throws<ArgumentNullException>(() => FrameSyncFile.Write(null!, "AB", "", 64, false, false));
        }
        finally
        {
            Directory.Delete(folder);
        }
    }

    // --- template matching

    [Fact]
    public void EveryTemplateMatchesAsTheCoreMatches()
    {
        var ops = new Ops();
        string[] answers = Answers("M");
        Assert.Equal(ops.M.Count, answers.Length);

        foreach (string[] m in ops.M)
        {
            string[] want = answers.Single(a => a.StartsWith("M|" + m[1] + "|")).Split('|');
            TemplateMatchResult got = TemplateMatcher.Match(Is(m[2]), Is(m[3]));
            Assert.True(want[2] == "1" == got.Ok, $"{m[1]}: ok was {got.Ok}");
            Assert.True(Is(want[3]).SequenceEqual(got.Missing), $"{m[1]}: missing was {string.Join(",", got.Missing)}");
            Assert.True(Is(want[4]).SequenceEqual(got.Extra), $"{m[1]}: extra was {string.Join(",", got.Extra)}");
        }
    }

    [Fact]
    public void TheMatchOfNothingAgainstNothingIsOkAndNullListsAreRefused()
    {
        TemplateMatchResult none = TemplateMatcher.Match(Array.Empty<int>(), Array.Empty<int>());
        Assert.True(none.Ok);
        Assert.Empty(none.Missing);
        Assert.Empty(none.Extra);
        Assert.Throws<ArgumentNullException>(() => TemplateMatcher.Match(null!, Array.Empty<int>()));
        Assert.Throws<ArgumentNullException>(() => TemplateMatcher.Match(Array.Empty<int>(), null!));
    }

    // --- clock stamps

    [Fact]
    public void EveryClockStampIsTheOneTheCoreFormats()
    {
        var ops = new Ops();
        string[] answers = Answers("K");
        Assert.Equal(ops.K.Count, answers.Length);

        foreach (string[] k in ops.K)
        {
            string want = Unhex(answers.Single(a => a.StartsWith("K|" + k[1] + "|")).Split('|')[2]);
            string got = ClockTime.Format(int.Parse(k[2]), D(k[3]), D(k[4]), k[5] == "1");
            Assert.True(want == got, $"{k[1]}: {got} instead of {want}");
        }
    }

    [Fact]
    public void ClockStampsReadAsDayHourMinuteSecond()
    {
        Assert.Equal("001:00:00:00", ClockTime.Format(1, 0, 0));
        Assert.Equal("001:00:00:00.000", ClockTime.Format(1, 0, 0, includeMilliseconds: true));
        Assert.Equal("120:21:25:25.500", ClockTime.Format(120, 77125.5, 0, true));
        Assert.Equal("002:00:00:00.500", ClockTime.Format(1, 86399.5, 1, true));       // across midnight
    }

    // --- CSV import

    [Fact]
    public void EveryCsvIsParsedAsTheCoreParsesIt()
    {
        string[][] all = FormatsDigest.Lines.Where(l => l.StartsWith("C|") || l.StartsWith("s|")).Select(l => l.Split('|')).ToArray();
        Assert.Equal(FormatsDigest.CsvFiles.Count, all.Count(f => f[0] == "C"));

        var wrong = new List<string>();
        for (int i = 0; i < all.Length; i++)
        {
            string[] f = all[i];
            if (f[0] != "C")
            {
                continue;
            }

            string name = f[1];
            string[][] series = all.Skip(i + 1).TakeWhile(x => x[0] == "s").ToArray();
            string path = TempFile(name + ".csv", Convert.FromBase64String(FormatsDigest.CsvFiles[name]));
            try
            {
                if (f[2] == "0")
                {
                    TmdqException e = Assert.Throws<TmdqException>(() => CsvFile.Parse(path));
                    if (e.Status != Status.Prepare || e.Message.Length == 0)
                    {
                        wrong.Add($"{name}: refused with {e.Status} '{e.Message}'");
                    }
                    continue;
                }

                CsvData got = CsvFile.Parse(path);
                if (got.BaseDay != int.Parse(f[3]) || !Same(got.BaseTimeOffset, D(f[4])) || !Same(got.XMax, D(f[5])) ||
                    got.SkippedRows != int.Parse(f[6]) || got.Series.Count != int.Parse(f[7]) || got.Series.Count != series.Length)
                {
                    wrong.Add($"{name}: info day {got.BaseDay} offset {got.BaseTimeOffset:R} xmax {got.XMax:R} skipped {got.SkippedRows} n {got.Series.Count}");
                    continue;
                }

                for (int s = 0; s < series.Length; s++)
                {
                    string[] w = series[s];
                    CsvSeries g = got.Series[s];
                    bool ok = (int)g.Metric == int.Parse(w[2]) && g.Name == Unhex(w[3]) && g.StreamLabel == Unhex(w[4]) &&
                              g.ReceiverIndex == int.Parse(w[5]) && g.ChannelIndex == int.Parse(w[6]) && g.StreamOrder == int.Parse(w[7]) &&
                              g.SourceId == int.Parse(w[8]) && Same(g.YMin, D(w[9])) && Same(g.YMax, D(w[10])) &&
                              Same(g.X, Ds(w[11])) && Same(g.Y, Ds(w[12]));
                    if (!ok)
                    {
                        wrong.Add($"{name}: series {s} ({g.Name}) differs");
                    }
                }
            }
            finally
            {
                File.Delete(path);
            }
        }
        Assert.True(wrong.Count == 0, string.Join("\n", wrong));
    }

    [Fact]
    public void TheCsvCorpusReachesEveryKindOfAnswer()
    {
        string[][] rows = FormatsDigest.Lines.Where(l => l.StartsWith("C|") || l.StartsWith("s|")).Select(l => l.Split('|')).ToArray();
        string[][] files = rows.Where(f => f[0] == "C").ToArray();
        Assert.Contains(files, f => f[2] == "0");                      // refused
        Assert.Contains(files, f => f[2] == "1" && f[6] != "0");       // rows skipped
        Assert.Contains(files, f => f[2] == "1" && f[3] != "1");       // a base day other than 1
        Assert.Contains(files, f => f[2] == "1" && f[4] != "0");       // a base time of day
        string[][] series = rows.Where(f => f[0] == "s").ToArray();
        Assert.Contains(series, f => f[2] == "0");                     // SNR
        Assert.Contains(series, f => f[2] == "1");                     // lock
        Assert.Contains(series, f => f[2] == "2");                     // missed frames
        Assert.Contains(series, f => f[8] != "0");                     // a source qualifier
        Assert.Contains(series, f => f[6] != "0");                     // a channel index above 0
        Assert.Contains(series, f => f[5] != "0");                     // a receiver
        Assert.Contains(series, f => f[12].Contains("nan"));           // a value that is not a number
    }

    [Fact]
    public void ParsedCsvsGiveTheirNativeMemoryBack()
    {
        // 100,000 rows of four series is about 6 MB of native arrays per parse: thirty parses never freed would be
        // some 190 MB more than thirty that were.
        string path = Path.Combine(Path.GetTempPath(), "tmdq-bigcsv-" + Guid.NewGuid().ToString("N") + ".csv");
        try
        {
            double[] x = Enumerable.Range(0, 100_000).Select(i => i * 0.01).ToArray();
            double[] y = Enumerable.Range(0, 100_000).Select(i => (double)(i % 100)).ToArray();
            CsvFile.Write(path, Enumerable.Range(0, 4).Select(i => new CsvColumn(MetricType.FrameSyncLock, "S" + i, 0, x, y)).ToList(), 1, 0);
            Assert.Equal(4, CsvFile.Parse(path).Series.Count);

            GC.Collect();
            GC.WaitForPendingFinalizers();
            long before = System.Diagnostics.Process.GetCurrentProcess().PrivateMemorySize64;
            for (int i = 0; i < 30; i++)
            {
                Assert.Equal(100_000, CsvFile.Parse(path).Series[0].X.Length);
            }
            GC.Collect();
            GC.WaitForPendingFinalizers();
            long grown = System.Diagnostics.Process.GetCurrentProcess().PrivateMemorySize64 - before;
            Assert.True(grown < 100L * 1024 * 1024, $"private memory grew by {grown / (1024 * 1024)} MiB over thirty parses");
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void ACsvThatIsNotThereIsAPrepareErrorAndNullIsRefused()
    {
        string missing = Path.Combine(Path.GetTempPath(), "tmdq-no-csv-" + Guid.NewGuid().ToString("N") + ".csv");
        TmdqException e = Assert.Throws<TmdqException>(() => CsvFile.Parse(missing));
        Assert.Equal(Status.Prepare, e.Status);
        Assert.Throws<ArgumentNullException>(() => CsvFile.Parse(null!));
    }

    // --- CSV export

    private static List<CsvColumn> Columns(List<string[]> cols) =>
        cols.Select(c => new CsvColumn((MetricType)int.Parse(c[1]), Unhex(c[2]), int.Parse(c[3]), Ds(c[4]), Ds(c[5]))).ToList();

    [Fact]
    public void EveryCsvIsWrittenWithTheBytesTheCoreWrites()
    {
        var ops = new Ops();
        string[] answers = Answers("X");
        Assert.Equal(ops.X.Count, answers.Length);

        foreach ((string[] head, List<string[]> cols) in ops.X)
        {
            string[] want = answers.Single(a => a.StartsWith("X|" + head[1] + "|")).Split('|');
            string path = Path.Combine(Path.GetTempPath(), "tmdq-csvwrite-" + Guid.NewGuid().ToString("N") + ".csv");
            try
            {
                CsvFile.Write(path, Columns(cols), int.Parse(head[2]), D(head[3]));
                Assert.True(want[2] == "1", head[1]);
                Assert.True(want[3] == Convert.ToHexString(File.ReadAllBytes(path)).ToLowerInvariant(), $"{head[1]}: the file differs from the core's");
            }
            finally
            {
                File.Delete(path);
            }
        }
    }

    [Fact]
    public void AWrittenCsvIsAnExactFileAndIsReadBackWithTheSameIdentities()
    {
        string path = Path.Combine(Path.GetTempPath(), "tmdq-csvroundtrip-" + Guid.NewGuid().ToString("N") + ".csv");
        try
        {
            var columns = new List<CsvColumn>
            {
                new(MetricType.FrameSyncLock, "Ch A", 0, new[] { 0.0, 1.0 }, new[] { 99.0, 98.0 }),
                new(MetricType.AccumulatedMissedFrames, "Ch A", 0, new[] { 0.0, 1.0 }, new[] { 0.0, 2.0 }),
                new(MetricType.Snr, "40 - Ch A L_RCVR2", 1, new[] { 0.0, 1.0 }, new[] { -80.5, -79.25 }),
            };
            CsvFile.Write(path, columns, 120, 77125.5);

            // The header, hand-written: CR LF, the clock column, the metric suffixes and the source qualifier.
            string text = File.ReadAllText(path);
            Assert.StartsWith("Time (DOY:HH:MM:SS.mmm),Ch A Lock (%),Ch A Accumulated Missed Frames,S1| 40 - Ch A L_RCVR2\r\n", text);
            Assert.Contains("120:21:25:25.500,99.000000,0.000000,-80.500000\r\n", text);
            Assert.Contains("120:21:25:26.500,98.000000,2.000000,-79.250000\r\n", text);
            Assert.NotEqual(0xEF, File.ReadAllBytes(path)[0]);       // no byte-order mark

            CsvData back = CsvFile.Parse(path);
            Assert.Equal(120, back.BaseDay);
            Assert.Equal(77125.5, back.BaseTimeOffset);
            Assert.Equal(1.0, back.XMax);
            Assert.Equal(new[] { MetricType.FrameSyncLock, MetricType.AccumulatedMissedFrames, MetricType.Snr }, back.Series.Select(s => s.Metric));
            Assert.Equal(new[] { "Ch A", "Ch A", "40 - Ch A L_RCVR2" }, back.Series.Select(s => s.Name));
            Assert.Equal(new[] { 0, 0, 1 }, back.Series.Select(s => s.SourceId));
            Assert.Equal(new[] { 99.0, 98.0 }, back.Series[0].Y);
            Assert.Equal(new[] { -80.5, -79.25 }, back.Series[2].Y);
            Assert.Equal(40, back.Series[2].StreamOrder);
            Assert.Equal(2, back.Series[2].ReceiverIndex);
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void ACsvThatCannotBeWrittenIsRefusedForTheRightReason()
    {
        string path = Path.Combine(Path.GetTempPath(), "tmdq-csvbad-" + Guid.NewGuid().ToString("N") + ".csv");
        var one = new CsvColumn(MetricType.FrameSyncLock, "A", 0, new[] { 0.0 }, new[] { 1.0 });

        TmdqException none = Assert.Throws<TmdqException>(() => CsvFile.Write(path, Array.Empty<CsvColumn>(), 1, 0));
        Assert.Equal(Status.BadArgument, none.Status);
        TmdqException metric = Assert.Throws<TmdqException>(() => CsvFile.Write(path, new[] { one with { Metric = (MetricType)99 } }, 1, 0));
        Assert.Equal(Status.BadArgument, metric.Status);
        Assert.False(File.Exists(path));

        string folder = Path.Combine(Path.GetTempPath(), "tmdq-csv-folder-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder);
        try
        {
            TmdqException io = Assert.Throws<TmdqException>(() => CsvFile.Write(folder, new[] { one }, 1, 0));
            Assert.Equal(Status.Io, io.Status);
        }
        finally
        {
            Directory.Delete(folder);
        }

        Assert.Throws<ArgumentNullException>(() => CsvFile.Write(null!, new[] { one }, 1, 0));
        Assert.Throws<ArgumentNullException>(() => CsvFile.Write(path, null!, 1, 0));
    }

    [Fact]
    public void TheWriterReachesEveryKindOfInput()
    {
        // The corpus must have exercised ties, sources, empty and non-ASCII names, NaN, and an empty column.
        var ops = new Ops();
        Assert.Contains(ops.X, x => x.Cols.Any(c => c[3] != "0"));                           // a source other than 0
        Assert.Contains(ops.X, x => x.Cols.Any(c => c[2] == "-"));                           // an empty name
        Assert.Contains(ops.X, x => x.Cols.Any(c => c[5].Contains("nan")));                  // not a number
        Assert.Contains(ops.X, x => x.Cols.Any(c => c[4] == "-"));                           // an empty column
        Assert.Contains(ops.X, x => x.Cols.Any(c => c[4].Split(',').Length != c[5].Split(',').Length));    // x and y of different lengths
        Assert.Equal(3, ops.X.SelectMany(x => x.Cols).Select(c => c[1]).Distinct().Count());   // all three metrics, across the corpus
    }
}
