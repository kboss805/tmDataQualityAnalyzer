using System;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace TmDq.Core.Tests;

/// <summary>
/// Deletes the files and folders a test made. Another process can hold a file it did not make for a moment after
/// it is written (on the CI runner, most likely the virus scanner reading a new file in the temp folder), and a plain
/// File.Delete in a finally block then fails a test whose every check passed. This retries for a short window while
/// the file is in use, and still throws once the window is over: a handle the library leaked is never released, so
/// a test that deletes a file to prove the library let go of it still fails when it did not.
/// </summary>
internal static class TempFiles
{
    /// <summary>How long a file may stay in use before the delete gives up and throws.</summary>
    public static readonly TimeSpan Window = TimeSpan.FromSeconds(3);

    private static readonly TimeSpan Pause = TimeSpan.FromMilliseconds(50);

    /// <summary>Deletes a file, as File.Delete does (a file that does not exist is not an error).</summary>
    public static void Delete(string path) => Delete(path, Window);

    /// <summary>Deletes a folder, as Directory.Delete does (a folder that does not exist is an error).</summary>
    public static void DeleteDirectory(string path, bool recursive = false) => DeleteDirectory(path, recursive, Window);

    internal static void Delete(string path, TimeSpan window) => Retry(() => File.Delete(path), window);

    internal static void DeleteDirectory(string path, bool recursive, TimeSpan window) =>
        Retry(() => Directory.Delete(path, recursive), window);

    private static void Retry(Action delete, TimeSpan window)
    {
        Stopwatch clock = Stopwatch.StartNew();
        while (true)
        {
            try
            {
                delete();
                return;
            }
            catch (Exception e) when (InUse(e) && clock.Elapsed < window)
            {
                Thread.Sleep(Pause);
            }
        }
    }

    /// <summary>A sharing violation, or the access denial Windows gives for a file another process holds or has
    /// marked for deletion. A missing file or folder is not one of these and is never retried.</summary>
    private static bool InUse(Exception e) =>
        e is UnauthorizedAccessException
        || (e is IOException && e is not FileNotFoundException && e is not DirectoryNotFoundException
            && e is not PathTooLongException);
}
