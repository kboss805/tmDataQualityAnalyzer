using System;
using System.IO;
using TmDq.Core.Tests;

namespace TmDq.App.Tests;

/// <summary>Paths in the repository the tests read: the folder holding tmdq.sln, found by walking up from the
/// test executable, and the build output of the application next to this project's.</summary>
internal static class Repo
{
    public static string Root => RepoPaths.Root;

    public static string File(string relative) => RepoPaths.File(relative);

    /// <summary>A .NET project's folder: a test project's under tests\, any other under src\.</summary>
    public static string Project(string name) =>
        Path.Combine(Root, name.EndsWith(".Tests", StringComparison.Ordinal) ? "tests" : "src", name);

    /// <summary>A committed recording under tests/data/test files (a missing one fails, never skips).</summary>
    public static string Recording(string name) => RepoPaths.Recording(name);

    /// <summary>The application's output folder for the configuration these tests were built in:
    /// tests\TmDq.App.Tests\bin\Release\net10.0 becomes src\TmDq.App\bin\Release\net10.0.</summary>
    public static string AppOutput
    {
        get
        {
            var here = new DirectoryInfo(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar));
            string framework = here.Name, configuration = here.Parent!.Name;
            return Path.Combine(Root, "src", "TmDq.App", "bin", configuration, framework);
        }
    }
}
