using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// The channel lists of the five committed recordings. The expected ids, names and TMATS rates are the ones
/// TestChapter10Reader pins on the Qt side (recorded from the application), so the managed reading is compared
/// with the application's, not with itself.
/// </summary>
public class FileTests
{
    public FileTests()
    {
        Env.EnsureLoaded();
    }

    public static IEnumerable<object[]> Recordings() => new[]
    {
        new object[] { "agc_nzl_rasa_testfile.ch10", new[] { "1:TimeChannel-1" },
            new[] { "19:2240.5A:2500000", "20:2257.5A:2100000", "21:2257.5B:2100000", "22:2257.5C:2100000",
                    "23:2240.5B:2500000", "24:2240.5C:2500000", "25:2240.5D:2500000", "26:CH-01:800000" } },
        new object[] { "agc_rnrz-l_trc_testfile.ch10", new[] { "1:IRIG-B" },
            new[] { "13:CH-01 2250.5MHZ AGC 800Kbps RNRZ-L:800000" } },
        new object[] { "prn_testfile, sample.ch10", new[] { "1:IRIG-B" },
            new[] { "13:CH-01 PRN 15 5M:5000000", "14:CH-02 PRN 15 20M:20000000", "15:CH-03 PRN 11 1M:1000000",
                    "16:CH-04 PRN 11 5M:5000000" } },
        new object[] { "safran_testfile, sample.ch10", new[] { "1:Time_Pck" },
            new[] { "11:PCM01:700000", "12:PCM02:1000000", "13:PCM03:1000000", "14:PCM04:700000" } },
        new object[] { "step_cal, example.ch10", new[] { "1:IRIG-B" },
            new[] { "26:CH-16 AGC STEP CALS 800K:800000" } },
    };

    private static string Describe(ChannelInfo c) => $"{c.Id}:{c.Name}:{c.TmatsDataRateBps:0}";

    [Theory]
    [MemberData(nameof(Recordings))]
    public void EachRecordingListsItsChannelsExactly(string file, string[] time, string[] pcm)
    {
        Ch10File opened = Ch10File.Open(Env.Recording(file));

        Assert.Equal(time, opened.TimeChannels.Select(c => $"{c.Id}:{c.Name}"));
        Assert.Equal(pcm, opened.PcmChannels.Select(Describe));
        Assert.All(opened.TimeChannels, c => Assert.Equal(0.0, c.TmatsDataRateBps));
        Assert.Equal(1, opened.DefaultTimeChannel);
    }

    [Fact]
    public void ARecordingThatIsNotThereIsAPrepareError()
    {
        string missing = Path.Combine(Path.GetTempPath(), "tmdq-no-such-" + Guid.NewGuid().ToString("N") + ".ch10");
        TmdqException e = Assert.Throws<TmdqException>(() => Ch10File.Open(missing));
        Assert.Equal(Status.Prepare, e.Status);
        Assert.NotEmpty(e.Message);
    }

    [Fact]
    public void AFileThatIsNotARecordingIsAPrepareError()
    {
        string junk = Path.Combine(Path.GetTempPath(), "tmdq-junk-" + Guid.NewGuid().ToString("N") + ".ch10");
        File.WriteAllBytes(junk, new byte[4096]);
        try
        {
            TmdqException e = Assert.Throws<TmdqException>(() => Ch10File.Open(junk));
            Assert.Equal(Status.Prepare, e.Status);
        }
        finally
        {
            File.Delete(junk);
        }
    }

    [Fact]
    public void OpeningDoesNotKeepTheRecordingOpen()
    {
        // The native handle is closed before Open returns: a file still held open could not be deleted.
        string dir = Path.Combine(Path.GetTempPath(), "tmdq-held-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        string copy = Path.Combine(dir, "recording.ch10");
        File.Copy(Env.StepCal, copy);
        try
        {
            Ch10File.Open(copy);
            File.Delete(copy);
            Assert.False(File.Exists(copy));
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    [Fact]
    public void ANullPathIsRefusedBeforeTheDllIsAsked()
    {
        Assert.Throws<ArgumentNullException>(() => Ch10File.Open(null!));
    }

    [Fact]
    public void ANonAsciiFolderOpensWhenTheHostRunsUnderTheUtf8CodePage()
    {
        string dir = Path.Combine(Path.GetTempPath(), "tmdq-café-Ω-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        string copy = Path.Combine(dir, "recording.ch10");
        File.Copy(Env.StepCal, copy);
        try
        {
            Ch10File opened = Ch10File.Open(copy);
            Assert.Equal(26, Assert.Single(opened.PcmChannels).Id);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    [Fact]
    public void ThisHostCarriesTheUtf8CodePageManifest()
    {
        // The reason the test above can be unconditional: if the manifest is missing, this fails and says so,
        // rather than the non-ASCII check quietly going unasserted.
        Assert.Equal(65001u, Env.ActiveCodePage);
    }

    [Fact]
    public void OpeningFromSeveralThreadsAtOnceGivesTheSameAnswer()
    {
        var results = new Ch10File[8];
        Parallel.For(0, results.Length, i => results[i] = Ch10File.Open(Env.Prn));
        Assert.All(results, r => Assert.Equal(new[] { 13, 14, 15, 16 }, r.PcmChannels.Select(c => c.Id)));
    }
}
