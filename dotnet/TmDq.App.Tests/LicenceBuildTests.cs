using System;
using System.Collections;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// dotnet/build/PackageLicences.targets end to end: a throwaway project that imports it restores a package from
    /// a local feed and is built with the real SDK. The script's own tests (PackageLicenceTests) cannot see the
    /// target being wired wrongly (the wrong hook, a condition that is never true), and only a real build can.
    /// Everything is isolated: the feed, the packages folder and the NuGet configuration are the test's own.
    /// </summary>
    public sealed class LicenceBuildTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-licbuild-" + Guid.NewGuid().ToString("N"));

        public LicenceBuildTests()
        {
            Directory.CreateDirectory(Path.Combine(_dir, "feed"));
            File.WriteAllText(Path.Combine(_dir, "nuget.config"),
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<configuration>\n" +
                "  <config><add key=\"globalPackagesFolder\" value=\"packages\" /></config>\n" +
                "  <packageSources><clear /><add key=\"local\" value=\"feed\" /></packageSources>\n" +
                "</configuration>\n");
        }

        public void Dispose()
        {
            try { Directory.Delete(_dir, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }

        /// <summary>A package with no assemblies and the given licence metadata, written straight into the feed.</summary>
        private void Pack(string id, string licenceXml, (string Name, string Text)? file = null)
        {
            string nupkg = Path.Combine(_dir, "feed", $"{id}.1.0.0.nupkg");
            using ZipArchive zip = ZipFile.Open(nupkg, ZipArchiveMode.Create);
            using (var w = new StreamWriter(zip.CreateEntry(id + ".nuspec").Open()))
                w.Write("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" +
                        "<package xmlns=\"http://schemas.microsoft.com/packaging/2013/05/nuspec.xsd\">\n" +
                        $"  <metadata>\n    <id>{id}</id>\n    <version>1.0.0</version>\n    <authors>test</authors>\n" +
                        $"    <description>test</description>\n    {licenceXml}\n  </metadata>\n</package>\n");
            if (file is { } f)
                using (var w = new StreamWriter(zip.CreateEntry(f.Name).Open()))
                    w.Write(f.Text);
        }

        private (int Exit, string Output) Build(params string[] packageIds)
        {
            string project = Path.Combine(_dir, "consumer", "Consumer.csproj");
            Directory.CreateDirectory(Path.GetDirectoryName(project)!);
            string targets = Repo.File(Path.Combine("dotnet", "build", "PackageLicences.targets"));
            string references = string.Concat(Array.ConvertAll(packageIds, id => $"<PackageReference Include=\"{id}\" Version=\"1.0.0\" />"));
            File.WriteAllText(project,
                "<Project Sdk=\"Microsoft.NET.Sdk\">\n" +
                "  <PropertyGroup><TargetFramework>net10.0</TargetFramework></PropertyGroup>\n" +
                $"  <ItemGroup>{references}</ItemGroup>\n" +
                $"  <Import Project=\"{targets}\" />\n" +
                "</Project>\n");
            return BuildTool.Run(_dir, project);
        }

        [Fact]
        public void ABuildThatResolvesAProprietaryPackageFails()
        {
            // How a commercial component ships: its licence is a EULA file, which nobody has reviewed.
            Pack("Tmdq.Fake.Commercial", "<license type=\"file\">EULA.txt</license>", ("EULA.txt", "Commercial licence. All rights reserved."));
            var r = Build("Tmdq.Fake.Commercial");
            Assert.True(r.Exit != 0, "the build succeeded with a proprietary package:\n" + r.Output);
            Assert.Contains("error TMDQLIC: Tmdq.Fake.Commercial 1.0.0", r.Output);
        }

        [Fact]
        public void APackageAddedToAProjectThatAlreadyBuiltIsCheckedToo()
        {
            // The way a proprietary package really arrives: a project that built clean gains a reference later. The
            // check's stamp must not let the second build skip it.
            Pack("Tmdq.Fake.Mit", "<license type=\"expression\">MIT</license>");
            Pack("Tmdq.Fake.Commercial", "<license type=\"file\">EULA.txt</license>", ("EULA.txt", "Commercial licence. All rights reserved."));
            var first = Build("Tmdq.Fake.Mit");
            Assert.True(first.Exit == 0, "the clean build failed:\n" + first.Output);
            var second = Build("Tmdq.Fake.Mit", "Tmdq.Fake.Commercial");
            Assert.True(second.Exit != 0, "the second build succeeded with a proprietary package:\n" + second.Output);
            Assert.Contains("error TMDQLIC: Tmdq.Fake.Commercial 1.0.0", second.Output);
        }

        [Fact]
        public void ABuildWithOnlyAllowedPackagesSucceeds()
        {
            // The control: the same project and feed with an MIT package builds, and the check ran.
            Pack("Tmdq.Fake.Mit", "<license type=\"expression\">MIT</license>");
            var r = Build("Tmdq.Fake.Mit");
            Assert.True(r.Exit == 0, "the build failed:\n" + r.Output);
            Assert.Contains("1 package(s) checked, all on the allow-list", r.Output);
            // The check's stamp is build output: under obj\, never beside the project file where git would see it.
            string consumer = Path.Combine(_dir, "consumer");
            Assert.False(File.Exists(Path.Combine(consumer, "package-licences.ok")), "the stamp landed in the project folder");
            Assert.True(File.Exists(Path.Combine(consumer, "obj", "Debug", "net10.0", "package-licences.ok")), "no stamp under obj\\");
        }
    }

    /// <summary>Runs `dotnet build` on a throwaway project, isolated from the calling process's MSBuild state.</summary>
    internal static class BuildTool
    {
        public static (int Exit, string Output) Run(string workingDirectory, string project, params string[] extra)
        {
            var start = new ProcessStartInfo("dotnet")
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = workingDirectory,
            };
            foreach (string a in new[] { "build", project, "-nologo", "-v", "minimal", "-p:NuGetAudit=false" })
                start.ArgumentList.Add(a);
            foreach (string a in extra)
                start.ArgumentList.Add(a);
            // A build started from inside another .NET process must not inherit that process's MSBuild settings, nor
            // the Platform=x64 that vcvars64 (scripts\env.ps1) puts in the environment: MSBuild reads every variable
            // as a property, and that one moves the output to bin\x64\.
            foreach (DictionaryEntry e in Environment.GetEnvironmentVariables())
            {
                string key = (string)e.Key;
                if (key.StartsWith("MSBuild", StringComparison.OrdinalIgnoreCase)
                    || key.Equals("Platform", StringComparison.OrdinalIgnoreCase)
                    || key.Equals("Configuration", StringComparison.OrdinalIgnoreCase))
                    start.Environment.Remove(key);
            }
            start.Environment["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1";
            start.Environment["DOTNET_NOLOGO"] = "1";

            using Process process = Process.Start(start)!;
            string output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
            Assert.True(process.WaitForExit(180_000), "the build did not finish");
            return (process.ExitCode, output);
        }
    }
}
