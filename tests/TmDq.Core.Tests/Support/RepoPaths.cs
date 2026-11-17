using System;
using System.IO;

namespace TmDq.Core.Tests;

/// <summary>Where the repository and its committed test files are, for both managed test projects (the app's tests link
/// this file, as they do <c>TempFiles.cs</c>): the folder holding tmdq.sln, found by walking up from the test
/// executable, and the files under it. A file that is missing fails the test that wanted it, never skips it: a missing
/// committed file is a broken checkout.</summary>
internal static class RepoPaths
{
    public static string Root { get; } = FindRoot();

    /// <summary>A committed file under the repository root.</summary>
    public static string File(string relative)
    {
        string path = Path.Combine(Root, relative);
        if (!System.IO.File.Exists(path))
        {
            throw new FileNotFoundException("a repository file the tests need is missing", path);
        }
        return path;
    }

    /// <summary>A committed recording under tests/data/test files.</summary>
    public static string Recording(string name) => File(Path.Combine("tests", "data", "test files", name));

    private static string FindRoot()
    {
        for (DirectoryInfo? d = new(AppContext.BaseDirectory); d is not null; d = d.Parent)
        {
            if (System.IO.File.Exists(Path.Combine(d.FullName, "tmdq.sln")))
            {
                return d.FullName;
            }
        }
        throw new DirectoryNotFoundException("the repository root (the folder holding tmdq.sln) was not found above " + AppContext.BaseDirectory);
    }
}
