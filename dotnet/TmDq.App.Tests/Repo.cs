using System;
using System.IO;

namespace TmDq.App.Tests
{
    /// <summary>Paths in the repository the tests read: the folder holding tmdq.sln, found by walking up from the
    /// test executable, and the build output of the application next to this project's.</summary>
    internal static class Repo
    {
        public static string Root { get; } = FindRoot();

        public static string File(string relative)
        {
            string path = Path.Combine(Root, relative);
            if (!System.IO.File.Exists(path)) throw new FileNotFoundException("a repository file the tests need is missing", path);
            return path;
        }

        /// <summary>A committed recording under tests/data/test files (a missing one fails, never skips).</summary>
        public static string Recording(string name) => File(Path.Combine("tests", "data", "test files", name));

        /// <summary>The application's output folder for the configuration these tests were built in:
        /// dotnet\TmDq.App.Tests\bin\Release\net10.0 becomes dotnet\TmDq.App\bin\Release\net10.0.</summary>
        public static string AppOutput
        {
            get
            {
                var here = new DirectoryInfo(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar));
                string framework = here.Name, configuration = here.Parent!.Name;
                return Path.Combine(Root, "dotnet", "TmDq.App", "bin", configuration, framework);
            }
        }

        private static string FindRoot()
        {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
                if (System.IO.File.Exists(Path.Combine(dir.FullName, "tmdq.sln"))) return dir.FullName;
            throw new DirectoryNotFoundException("no folder above " + AppContext.BaseDirectory + " holds tmdq.sln");
        }
    }
}
