using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// The receiver-parameters file through the library (plan step 4.1a): the scalars a Receiver SNR stream starts with
/// and loads, and the complete file it saves, word map included. The native tests hold the ABI to the core's own
/// functions byte for byte; these hold the binding to the ABI, with values chosen so that a field passed in the wrong
/// slot reads back as a different number.
/// </summary>
public class ReceiverParamsTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-rxparams-" + Guid.NewGuid().ToString("N"));

    public ReceiverParamsTests()
    {
        Env.EnsureLoaded();
        Directory.CreateDirectory(_dir);
    }

    public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);

    private string Write(string name, string text)
    {
        string path = Path.Combine(_dir, name);
        File.WriteAllText(path, text);
        return path;
    }

    private static string Shipped(string name) => Path.Combine(Env.RepoRoot, "settings", "receiver_params", name);

    /// <summary>Distinct in every field, and different from every file below.</summary>
    private static readonly ReceiverParams Shown = new(1, 2, 41.5, 7, 5);

    [Fact]
    public void TheDefaultsAreTheApplications()
    {
        Assert.Equal(new ReceiverParams(0, 3, 20.0, 16, 3), ReceiverParamsFile.Defaults());
    }

    [Fact]
    public void TheShippedFilesReadAsTheyAreWritten()
    {
        Assert.Equal(new ReceiverParams(0, 3, 20.0, 16, 3), ReceiverParamsFile.Read(Shipped("default.toml")));
        ReceiverParams rasa = ReceiverParamsFile.Read(Shipped("RASA.toml"));
        Assert.Equal(12, rasa.NumReceivers);
        Assert.Equal(3, rasa.ReceiverChannels);
    }

    [Fact]
    public void EveryScalarIsReadIntoItsOwnField()
    {
        string path = Write("all.toml", "[Parameters]\r\nPolarity=1\r\nSlope=0\r\nScale=12.25\r\n[Receivers]\r\nCount=9\r\nChannelsPerReceiver=4\r\n");
        Assert.Equal(new ReceiverParams(1, 0, 12.25, 9, 4), ReceiverParamsFile.Read(path, Shown));
    }

    [Fact]
    public void AKeyTheFileDoesNotCarryKeepsWhatIsShown()
    {
        string path = Write("scale.toml", "[Parameters]\r\nScale=33\r\n");
        Assert.Equal(Shown with { ScaleDdBPerV = 33 }, ReceiverParamsFile.Read(path, Shown));
        Assert.Equal(ReceiverParamsFile.Defaults() with { ScaleDdBPerV = 33 }, ReceiverParamsFile.Read(path));
    }

    [Fact]
    public void AMissingFileChangesNothing()
    {
        Assert.Equal(Shown, ReceiverParamsFile.Read(Path.Combine(_dir, "nowhere.toml"), Shown));
    }

    [Fact]
    public void OlderFilesCountsAreReadAndNonPositiveValuesIgnored()
    {
        Assert.Equal(Shown with { NumReceivers = 8, ReceiverChannels = 2 },
                     ReceiverParamsFile.Read(Write("legacy.toml", "[Parameters]\r\nNumReceivers=8\r\nReceiverChannels=2\r\n"), Shown));
        Assert.Equal(Shown, ReceiverParamsFile.Read(Write("zero.toml", "[Parameters]\r\nScale=0\r\n[Receivers]\r\nCount=-1\r\nChannelsPerReceiver=0\r\n"), Shown));
    }

    [Fact]
    public void ASavedFileReadsBackAsWhatWasSaved()
    {
        var saved = new ReceiverParams(1, 2, 17.75, 6, 2);
        string path = Path.Combine(_dir, "saved.toml");
        ReceiverParamsFile.Save(path, saved, null, 800);
        Assert.Equal(saved, ReceiverParamsFile.Read(path));
    }

    [Fact]
    public void WithoutASourceTheDefaultWordMapForTheCountsIsWritten()
    {
        string path = Path.Combine(_dir, "default-map.toml");
        ReceiverParamsFile.Save(path, new ReceiverParams(0, 3, 20, 2, 3), null, 800);
        string[] sections = Sections(path);
        Assert.Equal(new[] { "L_RCVR1", "R_RCVR1", "C_RCVR1", "L_RCVR2", "R_RCVR2", "C_RCVR2" }, sections);
    }

    [Fact]
    public void ASourceThatLoadsLendsItsWordMap()
    {
        // RASA's map, whatever counts are on screen: the map the stream would actually process with.
        string path = Path.Combine(_dir, "rasa-map.toml");
        ReceiverParamsFile.Save(path, new ReceiverParams(0, 3, 20, 2, 3), Shipped("RASA.toml"), 800);
        Assert.Equal(Sections(Shipped("RASA.toml")), Sections(path));
        Assert.True(Sections(path).Length > 6);
    }

    [Fact]
    public void TheFrameLengthBoundsTheMap()
    {
        // 16 x 3 channels need 48 words beyond the sync; 160 bits is 10 words.
        string path = Path.Combine(_dir, "too-big.toml");
        TmdqException e = Assert.Throws<TmdqException>(() => ReceiverParamsFile.Save(path, ReceiverParamsFile.Defaults(), null, 160));
        Assert.Equal(Status.Io, e.Status);
        Assert.False(string.IsNullOrWhiteSpace(e.Message));
        Assert.False(File.Exists(path));
        ReceiverParamsFile.Save(path, ReceiverParamsFile.Defaults(), null, 800);
        Assert.True(File.Exists(path));
    }

    [Fact]
    public void AFileThatCannotBeWrittenIsAnIoErrorInTheCoresWords()
    {
        string blocked = Path.Combine(_dir, "blocked.toml");
        Directory.CreateDirectory(blocked);
        TmdqException e = Assert.Throws<TmdqException>(() => ReceiverParamsFile.Save(blocked, ReceiverParamsFile.Defaults(), null, 800));
        Assert.Equal(Status.Io, e.Status);
        Assert.Contains("Could not write 'blocked.toml'.", e.Message);
    }

    [Fact]
    public void TheWordsInAMinorFrameAreRoundedUp()
    {
        Assert.Equal(50, FrameGeometry.WordsInMinorFrame(800));
        Assert.Equal(51, FrameGeometry.WordsInMinorFrame(801));
        Assert.Equal(2048, FrameGeometry.WordsInMinorFrame(32767));
        Assert.Equal(0, FrameGeometry.WordsInMinorFrame(0));
    }

    [Fact]
    public void NullArgumentsAreRefusedBeforeTheDllIsAsked()
    {
        Assert.Throws<ArgumentNullException>(() => ReceiverParamsFile.Read(null!));
        Assert.Throws<ArgumentNullException>(() => ReceiverParamsFile.Save(null!, Shown, null, 800));
        Assert.Throws<ArgumentNullException>(() => ReceiverParamsFile.Save(Path.Combine(_dir, "x.toml"), null!, null, 800));
    }

    /// <summary>The word-map section names, in file order: every section but the seven the core skips as metadata or a
    /// placeholder (FrameSetup's reserved names; RASA's map has an [EMPTY] for each unused fourth channel).</summary>
    private static string[] Sections(string path) =>
        Regex.Matches(File.ReadAllText(path), @"(?m)^\[([^\]]+)\]")
             .Select(m => m.Groups[1].Value)
             .Where(n => n is not ("Defaults" or "Frame" or "Parameters" or "Time" or "Receivers" or "Bounds" or "EMPTY"))
             .ToArray();
}
