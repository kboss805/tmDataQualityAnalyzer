using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// TempFiles, which every test here cleans up through: it must wait out another process holding a file for a moment
/// (what failed CI on PR #218: a passing test's cleanup met the runner's virus scanner), and must still fail when a
/// file stays held, because some tests delete a file to prove the library released it.
/// </summary>
public class TempFilesTests
{
    private static string NewPath(string suffix = ".tmp") =>
        Path.Combine(Path.GetTempPath(), "tmdq-tempfiles-" + Guid.NewGuid().ToString("N") + suffix);

    /// <summary>Holds a file open with no sharing at all, as a scanner can, and lets go after the delay.</summary>
    private static Task HoldFor(string path, TimeSpan delay)
    {
        var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None);
        return Task.Run(async () =>
        {
            try { await Task.Delay(delay, TestContext.Current.CancellationToken); }
            finally { stream.Dispose(); }
        });
    }

    [Fact]
    public async Task AFileHeldForAMomentIsDeletedOnceItIsReleased()
    {
        // Through the public call and its own window: a brief hold must not fail a cleanup.
        string path = NewPath();
        File.WriteAllText(path, "x");
        Task release = HoldFor(path, TimeSpan.FromMilliseconds(400));
        Stopwatch clock = Stopwatch.StartNew();
        TempFiles.Delete(path);
        Assert.False(File.Exists(path));
        Assert.True(clock.Elapsed >= TimeSpan.FromMilliseconds(300), $"it did not wait for the release ({clock.ElapsedMilliseconds} ms)");
        await release;
    }

    [Fact]
    public void AFileStillHeldWhenTheWindowEndsIsAnError()
    {
        // A leaked handle never lets go: the delete must give up and say so, after trying for the whole window.
        string path = NewPath();
        File.WriteAllText(path, "x");
        using (new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None))
        {
            Stopwatch clock = Stopwatch.StartNew();
            Assert.Throws<IOException>(() => TempFiles.Delete(path, TimeSpan.FromMilliseconds(300)));
            Assert.True(clock.Elapsed >= TimeSpan.FromMilliseconds(300), $"it gave up early ({clock.ElapsedMilliseconds} ms)");
            Assert.True(File.Exists(path));
        }
        TempFiles.Delete(path);
        Assert.False(File.Exists(path));
    }

    [Fact]
    public void TheDefaultWindowIsLongEnoughForAScanAndShortEnoughToFailALeak()
    {
        Assert.InRange(TempFiles.Window, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(10));
    }

    [Fact]
    public void AMissingFileIsNotAnErrorAndIsNotWaitedFor()
    {
        // As File.Delete: nothing there, nothing to do.
        Stopwatch clock = Stopwatch.StartNew();
        TempFiles.Delete(NewPath());
        Assert.True(clock.Elapsed < TimeSpan.FromSeconds(1));
    }

    [Fact]
    public void AMissingFolderIsAnErrorAtOnce()
    {
        // As Directory.Delete, and not retried: a folder that is not there will not appear.
        Stopwatch clock = Stopwatch.StartNew();
        Assert.Throws<DirectoryNotFoundException>(() => TempFiles.DeleteDirectory(NewPath(""), recursive: true));
        Assert.True(clock.Elapsed < TimeSpan.FromSeconds(1), $"a missing folder was retried ({clock.ElapsedMilliseconds} ms)");
    }

    [Fact]
    public async Task AFolderWithAFileHeldForAMomentIsDeletedOnceItIsReleased()
    {
        string dir = NewPath("");
        Directory.CreateDirectory(Path.Combine(dir, "inner"));
        string file = Path.Combine(dir, "inner", "held.bin");
        File.WriteAllText(file, "x");
        Task release = HoldFor(file, TimeSpan.FromMilliseconds(400));
        TempFiles.DeleteDirectory(dir, recursive: true);
        Assert.False(Directory.Exists(dir));
        await release;
    }

    [Fact]
    public void AnEmptyFolderIsDeletedWithoutRecursion()
    {
        string dir = NewPath("");
        Directory.CreateDirectory(dir);
        TempFiles.DeleteDirectory(dir);
        Assert.False(Directory.Exists(dir));
    }

    [Fact]
    public void ANonEmptyFolderWithoutRecursionIsStillAnError()
    {
        // Some cleanups delete without recursion on purpose, so a file the test forgot to remove is noticed.
        string dir = NewPath("");
        Directory.CreateDirectory(dir);
        File.WriteAllText(Path.Combine(dir, "left-behind.txt"), "x");
        try
        {
            Assert.Throws<IOException>(() => TempFiles.DeleteDirectory(dir, recursive: false, TimeSpan.FromMilliseconds(200)));
            Assert.True(Directory.Exists(dir));
        }
        finally
        {
            TempFiles.DeleteDirectory(dir, recursive: true);
        }
    }

    [Fact]
    public void AnAccessDenialThatDoesNotClearIsAnError()
    {
        // A read-only file is refused with UnauthorizedAccessException, which is retried (Windows also uses it
        // for a file another process has marked for deletion) and then reported, not swallowed.
        string path = NewPath();
        File.WriteAllText(path, "x");
        File.SetAttributes(path, FileAttributes.ReadOnly);
        try
        {
            Stopwatch clock = Stopwatch.StartNew();
            Assert.Throws<UnauthorizedAccessException>(() => TempFiles.Delete(path, TimeSpan.FromMilliseconds(200)));
            Assert.True(clock.Elapsed >= TimeSpan.FromMilliseconds(200), $"an access denial was not retried ({clock.ElapsedMilliseconds} ms)");
            Assert.True(File.Exists(path));
        }
        finally
        {
            File.SetAttributes(path, FileAttributes.Normal);
            TempFiles.Delete(path);
        }
    }
}
