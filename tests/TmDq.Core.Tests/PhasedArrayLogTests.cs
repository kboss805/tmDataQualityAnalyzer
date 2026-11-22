using System;
using System.IO;
using System.Linq;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// A phased array antenna's SNR log through the managed library (US10.0, ABI revision 4). The core's own tests hold the
/// format; these hold that the binding hands every field through, as <see cref="CsvFile.Parse"/> does for the application's CSV.
/// </summary>
public class PhasedArrayLogTests
{
    public PhasedArrayLogTests()
    {
        Env.EnsureLoaded();
    }

    [Fact]
    public void TheCommittedSampleIsThirtyStreamsOfThreeSeriesEach()
    {
        CsvData log = CsvFile.ParsePhasedArrayLog(Env.Recording("av_snr_log.csv"));

        Assert.Equal(90, log.Series.Count);
        Assert.Equal(0, log.SkippedRows);
        Assert.Equal(317, log.BaseDay);   // 2026-11-13
        Assert.Equal(19 * 3600 + 15 * 60 + 35.513, log.BaseTimeOffset, 6);
        Assert.Equal(56 * 60 + 53.474 - (15 * 60 + 35.513), log.XMax, 6);

        CsvSeries first = log.Series[0];
        Assert.Equal("Stream 1 H", first.Name);
        Assert.Equal(MetricType.Snr, first.Metric);
        Assert.Equal("SCU1", first.StreamLabel);
        Assert.Equal((1, 0, 1, 0), (first.ReceiverIndex, first.ChannelIndex, first.StreamOrder, first.SourceId));
        Assert.Equal(2479, first.X.Length);
        Assert.Equal(first.X.Length, first.Y.Length);
        Assert.Equal(0.0, first.X[0]);

        CsvSeries last = log.Series[^1];
        Assert.Equal("Stream 30 C", last.Name);
        Assert.Equal((30, 2), (last.ReceiverIndex, last.ChannelIndex));

        // Each stream is H, V then C, in that order.
        Assert.Equal(
            Enumerable.Range(0, 90).Select(i => (i / 3 + 1, i % 3)),
            log.Series.Select(s => (s.ReceiverIndex, s.ChannelIndex)));
        Assert.Equal(2.4, log.Series[6].Y[0], 6);    // Stream 3 H at the first sample
        Assert.Equal(-0.8, log.Series[8].Y[0], 6);   // Stream 3 C
    }

    [Fact]
    public void AFileThatIsNotTheLogIsAPrepareFailureNamingWhatWasWrong()
    {
        string ours = TempFiles.NewPath("array", ".csv");
        string missing = TempFiles.NewPath("array-none", ".csv");
        File.WriteAllText(ours, "Time (DOY:HH:MM:SS.mmm),A Lock (%)\r\n120:00:00:00.000,5\r\n");
        try
        {
            TmdqException e = Assert.Throws<TmdqException>(() => CsvFile.ParsePhasedArrayLog(ours));
            Assert.Equal(Status.Prepare, e.Status);
            Assert.Contains("phased array log", e.Message, StringComparison.Ordinal);
            Assert.Throws<TmdqException>(() => CsvFile.ParsePhasedArrayLog(missing));
            Assert.Throws<ArgumentNullException>(() => CsvFile.ParsePhasedArrayLog(null!));

            // And the application's own parser refuses the log, as the story's two kinds of file are told apart by their header.
            Assert.Throws<TmdqException>(() => CsvFile.Parse(Env.Recording("av_snr_log.csv")));
        }
        finally
        {
            TempFiles.Delete(ours);
        }
    }
}
