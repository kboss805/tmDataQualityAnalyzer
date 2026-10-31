using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// What <see cref="Library.Load"/> checks, and what it does when called again. The checks are separate functions
/// of plain values, because the real DLL always passes them and a process can load only one module: here they are
/// fed a DLL that reports the wrong thing. The failures of a first load are in <see cref="IsolatedLoadTests"/>,
/// which needs a process nothing has been loaded into.
/// </summary>
public class LibraryTests
{
    public LibraryTests()
    {
        Env.EnsureLoaded();
    }

    [Fact]
    public void TheVersionMustBeExactlyTheOneThisBindingWasWrittenFor()
    {
        Library.CheckVersion("x.dll", Library.ExpectedVersion, Library.MinimumRevision);

        Assert.Throws<InvalidOperationException>(() => Library.CheckVersion("x.dll", Library.ExpectedVersion - 1, Library.MinimumRevision));
        InvalidOperationException newer = Assert.Throws<InvalidOperationException>(
            () => Library.CheckVersion("x.dll", Library.ExpectedVersion + 1, Library.MinimumRevision));
        Assert.Contains("x.dll", newer.Message);
        Assert.Contains($"version {Library.ExpectedVersion + 1}.", newer.Message);
        Assert.Contains($"needs version {Library.ExpectedVersion}", newer.Message);
    }

    [Fact]
    public void ALaterRevisionOfTheVersionIsFineAndAnEarlierOneIsNot()
    {
        Library.CheckVersion("x.dll", Library.ExpectedVersion, Library.MinimumRevision + 1);
        Library.CheckVersion("x.dll", Library.ExpectedVersion, Library.MinimumRevision + 40);
        Assert.Throws<InvalidOperationException>(() => Library.CheckVersion("x.dll", Library.ExpectedVersion, Library.MinimumRevision - 1));
    }

    [Fact]
    public void TheLoadedDllPassesItsOwnChecks()
    {
        Assert.Equal(Library.ExpectedVersion, Library.AbiVersion);
        Assert.True(Library.AbiRevision >= Library.MinimumRevision);
        Library.CheckVersion("loaded", Library.AbiVersion, Library.AbiRevision);
        Library.CheckLayouts(Library.Mirrored, Native.tmdq_sizeof);
    }

    [Fact]
    public void EveryMirroredStructIsComparedAndEachOneWrongSizeIsRefusedByName()
    {
        // The table covers every struct id the binding declares, each paired with a type of that name...
        Assert.Equal(
            Enum.GetValues<StructId>().OrderBy(i => (int)i),
            Library.Mirrored.Select(m => m.Id).OrderBy(i => (int)i));
        Assert.All(Library.Mirrored, m => Assert.Equal(m.Id.ToString() + "Native", m.Type.Name));

        // ...and a DLL that disagrees about any one of them is refused, naming it and both sizes.
        foreach ((StructId id, Type type) in Library.Mirrored)
        {
            int managed = Marshal.SizeOf(type);
            foreach (int wrong in new[] { managed - 8, managed + 8 })
            {
                InvalidOperationException e = Assert.Throws<InvalidOperationException>(
                    () => Library.CheckLayouts(Library.Mirrored, which => which == (int)id ? wrong : Native.tmdq_sizeof(which)));
                Assert.Contains($"for {id}:", e.Message);
                Assert.Contains($"managed {managed} bytes, native {wrong} bytes", e.Message);
            }
        }
    }

    [Fact]
    public void AskingTheNativeSideForSizesUsesTheIdsTheEnumNames()
    {
        // A swapped pair would still pass the size check on a DLL whose two structs happen to be the same size,
        // so the ids are pinned to the numbers the C header assigns.
        Assert.Equal(
            new[] { ("ChannelInfo", 1), ("CalPoint", 2), ("CalProfile", 3), ("StreamConfig", 4), ("RunRequest", 5), ("Callbacks", 6), ("StreamInfo", 7), ("ChannelSeries", 8), ("CalibrationRequest", 9), ("CalibrationInfo", 10), ("CalibrationChannel", 11), ("FrameSyncFileInfo", 12), ("MatchInfo", 13), ("CsvInfo", 14), ("CsvSeriesInfo", 15), ("CsvColumn", 16), ("SeriesSetInfo", 17), ("SeriesSpec", 18), ("StreamKey", 19), ("TimeBaseInfo", 20), ("ImportedSeries", 21) },
            Enum.GetValues<StructId>().Select(i => (i.ToString(), (int)i)));
    }

    [Fact]
    public void LoadingTheSameDllAgainIsHarmlessHoweverThePathIsSpelled()
    {
        string path = Env.DllPath();
        string loaded = Library.LoadedPath!;
        Assert.Equal(Path.GetFullPath(path), loaded);

        Library.Load(path);
        Library.Load(path.ToUpperInvariant());                                   // Windows paths do not care about case
        Library.Load(Path.GetRelativePath(Environment.CurrentDirectory, path));  // nor about being relative
        Assert.Equal(loaded, Library.LoadedPath);
    }

    [Fact]
    public void AnotherDllCannotBeLoadedBesideTheFirst()
    {
        string copy = Path.Combine(Path.GetTempPath(), "tmdq-copy-" + Guid.NewGuid().ToString("N") + ".dll");
        File.Copy(Env.DllPath(), copy);
        try
        {
            InvalidOperationException e = Assert.Throws<InvalidOperationException>(() => Library.Load(copy));
            Assert.Contains("already loaded", e.Message);
            Assert.Contains(Library.LoadedPath!, e.Message);
            Assert.Contains(copy, e.Message);
        }
        finally
        {
            TempFiles.Delete(copy);
        }
    }

    [Fact]
    public void ABadPathIsRefusedWithoutDisturbingTheLoadedDll()
    {
        ArgumentException empty = Assert.Throws<ArgumentException>(() => Library.Load(""));
        Assert.Equal("dllPath", empty.ParamName);
        ArgumentException none = Assert.Throws<ArgumentNullException>(() => Library.Load(null!));
        Assert.Equal("dllPath", none.ParamName);

        string missing = Path.Combine("no", "such", "tmdq_core-" + Guid.NewGuid().ToString("N") + ".dll");
        FileNotFoundException e = Assert.Throws<FileNotFoundException>(() => Library.Load(missing));
        Assert.Equal(Path.GetFullPath(missing), e.FileName);

        Assert.Equal(Path.GetFullPath(Env.DllPath()), Library.LoadedPath);
    }

    [Fact]
    public void ALastErrorOnAThreadThatHasHadNoneIsEmpty()
    {
        string seen = "unset";
        var thread = new System.Threading.Thread(() => seen = Library.LastError());
        thread.Start();
        thread.Join();
        Assert.Equal(string.Empty, seen);
    }
}
