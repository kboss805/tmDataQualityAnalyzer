using System;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace TmDq.Core.Tests;

/// <summary>
/// Reads back and deletes the files and folders a test made. Another process can hold a file it did not make for a
/// moment after it is written (on the CI runner, most likely the virus scanner reading a new file in the temp folder),
/// and a plain File.ReadAllBytes just after the library wrote the file, or a File.Delete in a finally block, then fails
/// a test whose every check would pass. This retries for a short window while the file is in use, and still throws
/// once the window is over: a handle the library leaked is never released, so a test that reads or deletes a file to
/// prove the library let go of it still fails when it did not.
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

    /// <summary>Removes a scratch folder a test made and does not care whether it goes: for the folders of the tests that
    /// run a build tool (dotnet, msbuild) or a script, whose child process can still hold a file in it as the test ends,
    /// and a folder left in %TEMP% is no failure of the product. Waits out the window like <see cref="DeleteDirectory"/>,
    /// then gives up quietly. Never for a file the library under test opened: that one must fail the test when it is not
    /// released, which is what <see cref="Delete"/> and <see cref="DeleteDirectory"/> are for.</summary>
    /// <returns>Whether the folder is gone.</returns>
    public static bool TryDeleteDirectory(string path) => TryDeleteDirectory(path, Window);

    /// <summary>Reads a file, as File.ReadAllBytes does (a file that does not exist is an error at once).</summary>
    public static byte[] ReadAllBytes(string path) => ReadAllBytes(path, Window);

    /// <summary>Reads a file as text, as File.ReadAllText does (a file that does not exist is an error at once).</summary>
    public static string ReadAllText(string path) => Retry(() => File.ReadAllText(path), Window);

    internal static byte[] ReadAllBytes(string path, TimeSpan window) => Retry(() => File.ReadAllBytes(path), window);

    internal static void Delete(string path, TimeSpan window) => Retry(() => File.Delete(path), window);

    internal static void DeleteDirectory(string path, bool recursive, TimeSpan window) =>
        Retry(() => Directory.Delete(path, recursive), window);

    internal static bool TryDeleteDirectory(string path, TimeSpan window)
    {
        try
        {
            DeleteDirectory(path, recursive: true, window);
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return !Directory.Exists(path);   // a folder that was not there is gone, as far as the test cares
        }
    }

    private static void Retry(Action act, TimeSpan window) => Retry(() => { act(); return true; }, window);

    private static T Retry<T>(Func<T> act, TimeSpan window)
    {
        Stopwatch clock = Stopwatch.StartNew();
        while (true)
        {
            try
            {
                return act();
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
