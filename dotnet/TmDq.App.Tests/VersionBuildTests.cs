using System;
using System.IO;
using System.Reflection;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// dotnet/build/AppVersion.props against headers other than the real one: a throwaway project imports it and is
    /// built with the header path given on the command line (a global property overrides the one the props set).
    /// The real header can only show that the numbers agree; these show they are read from the file, each from its
    /// own definition, and that a header the pattern cannot read stops the build instead of shipping 1.0.0.
    /// </summary>
    public sealed class VersionBuildTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-version-" + Guid.NewGuid().ToString("N"));

        public VersionBuildTests() => Directory.CreateDirectory(_dir);

        public void Dispose()
        {
            try { Directory.Delete(_dir, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }

        private (int Exit, string Output, string Dll) Build(string header)
        {
            string headerPath = Path.Combine(_dir, "constants.h");
            File.WriteAllText(headerPath, header);
            string project = Path.Combine(_dir, "versioned", "Versioned.csproj");
            Directory.CreateDirectory(Path.GetDirectoryName(project)!);
            string props = Repo.File(Path.Combine("dotnet", "build", "AppVersion.props"));
            File.WriteAllText(project,
                "<Project Sdk=\"Microsoft.NET.Sdk\">\n" +
                $"  <Import Project=\"{props}\" />\n" +
                "  <PropertyGroup><TargetFramework>net10.0</TargetFramework></PropertyGroup>\n" +
                "</Project>\n");
            File.WriteAllText(Path.Combine(_dir, "versioned", "Empty.cs"), "namespace Versioned { internal static class Empty { } }\n");
            string outDir = Path.Combine(_dir, "out") + Path.DirectorySeparatorChar;
            var (exit, output) = BuildTool.Run(_dir, project, "-p:TmDqVersionSource=" + headerPath, "-p:OutDir=" + outDir);
            return (exit, output, Path.Combine(outDir, "Versioned.dll"));
        }

        private static string HeaderWith(string major, string minor, string patch) =>
            "// it's a header with an apostrophe, \"quotes\" and a 'char' literal\n" +
            "struct AppVersion {\n" +
            $"    static constexpr int kMajor = {major};\n" +
            $"    static constexpr int kMinor = {minor};\n" +
            $"    static constexpr int kPatch = {patch};\n" +
            "    static std::string toString() { return std::to_string(kMajor) + \".\" + std::to_string(kMinor); }\n" +
            "};\n" +
            "inline constexpr double kMajorEdgeFractionOfStep = 0.25;\n";

        [Fact]
        public void EachNumberIsReadFromItsOwnDefinition()
        {
            // Three different numbers, so none can stand in for another; a later name that starts with kMajor and
            // a toString() line that mentions all three must not be read as definitions.
            var r = Build(HeaderWith("7", "81", "902"));
            Assert.True(r.Exit == 0, r.Output);
            AssemblyName name = AssemblyName.GetAssemblyName(r.Dll);
            Assert.Equal(new Version(7, 81, 902, 0), name.Version);
        }

        [Theory]
        [InlineData("kPatch")]
        [InlineData("kMinor")]
        [InlineData("kMajor")]
        public void AHeaderMissingANumberStopsTheBuild(string missing)
        {
            string header = HeaderWith("7", "81", "902").Replace($"static constexpr int {missing} =", $"static constexpr int {missing}Gone =");
            var r = Build(header);
            Assert.True(r.Exit != 0, "the build succeeded without " + missing + ":\n" + r.Output);
            Assert.Contains("Could not read AppVersion (kMajor/kMinor/kPatch)", r.Output);
        }

        [Fact]
        public void AMissingHeaderStopsTheBuild()
        {
            string project = Path.Combine(_dir, "versioned", "Versioned.csproj");
            Build(HeaderWith("1", "2", "3"));   // writes the project
            var (exit, output) = BuildTool.Run(_dir, project, "-p:TmDqVersionSource=" + Path.Combine(_dir, "nowhere.h"));
            Assert.True(exit != 0, output);
            Assert.Contains("AppVersion source not found", output);
        }
    }
}
