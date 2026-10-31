using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// scripts\check_package_licences.ps1, which every .NET project here runs after restore (plan decision D5: no
    /// package outside the open-source allow-list, so no Avalonia Accelerate component can arrive). Each case writes
    /// a packages folder and an assets file of its own and runs the real script, as the build does.
    /// </summary>
    public sealed class PackageLicenceTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-licences-" + Guid.NewGuid().ToString("N"));
        private readonly List<(string Id, string Version)> _packages = new();

        public PackageLicenceTests() => Directory.CreateDirectory(Path.Combine(_dir, "packages"));

        public void Dispose()
        {
            try { Directory.Delete(_dir, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }

        /// <summary>Puts a package in the packages folder with the given licence metadata (the inner XML of
        /// &lt;metadata&gt; after the id and version), and optionally a file inside it.</summary>
        private void AddPackage(string id, string version, string licenceXml, (string Name, string Text)? file = null)
        {
            string folder = Path.Combine(_dir, "packages", id.ToLowerInvariant(), version.ToLowerInvariant());
            Directory.CreateDirectory(folder);
            File.WriteAllText(Path.Combine(folder, id.ToLowerInvariant() + ".nuspec"),
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" +
                "<package xmlns=\"http://schemas.microsoft.com/packaging/2013/05/nuspec.xsd\">\n" +
                $"  <metadata>\n    <id>{id}</id>\n    <version>{version}</version>\n    {licenceXml}\n  </metadata>\n</package>\n");
            if (file is { } f) File.WriteAllText(Path.Combine(folder, f.Name), f.Text);
            _packages.Add((id, version));
        }

        private void AddListedOnly(string id, string version) => _packages.Add((id, version));

        private string WriteAssets(bool withFolder = true)
        {
            var libraries = _packages.ToDictionary(
                p => p.Id + "/" + p.Version,
                p => (object)new Dictionary<string, object>
                {
                    ["type"] = "package",
                    ["path"] = (p.Id + "/" + p.Version).ToLowerInvariant(),
                });
            // A project reference is listed too, and must be ignored: it is not a package.
            libraries["TmDq.Core/1.0.0"] = new Dictionary<string, object> { ["type"] = "project", ["path"] = "../TmDq.Core/TmDq.Core.csproj" };
            var assets = new Dictionary<string, object>
            {
                ["version"] = 3,
                ["libraries"] = libraries,
                ["packageFolders"] = withFolder
                    ? new Dictionary<string, object> { [Path.Combine(_dir, "packages") + Path.DirectorySeparatorChar] = new { } }
                    : new Dictionary<string, object>(),
            };
            string path = Path.Combine(_dir, "project.assets.json");
            File.WriteAllText(path, JsonSerializer.Serialize(assets));
            return path;
        }

        private static (int Exit, string Output) Run(string assets)
        {
            var start = new ProcessStartInfo("powershell")
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            foreach (string a in new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                         Repo.File(Path.Combine("scripts", "check_package_licences.ps1")), "-AssetsFile", assets })
                start.ArgumentList.Add(a);
            using Process process = Process.Start(start)!;
            string output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
            Assert.True(process.WaitForExit(60_000), "the licence check did not finish");
            return (process.ExitCode, output);
        }

        private static void Passes((int Exit, string Output) r, int packages)
        {
            Assert.True(r.Exit == 0, "expected a pass:\n" + r.Output);
            Assert.Contains($"{packages} package(s) checked, all on the allow-list", r.Output);
            Assert.DoesNotContain("error TMDQLIC", r.Output);
        }

        private static void Fails((int Exit, string Output) r, string package, string why)
        {
            Assert.True(r.Exit != 0, "expected a failure:\n" + r.Output);
            // In MSBuild's canonical error form, naming the package and the reason.
            Assert.Contains($"error TMDQLIC: {package}", r.Output);
            Assert.Contains(why, r.Output);
        }

        private static string Expression(string spdx) => $"<license type=\"expression\">{spdx}</license>";

        [Fact]
        public void PackagesUnderAllowedLicencesPass()
        {
            AddPackage("Avalonia", "12.1.4", Expression("MIT"));
            AddPackage("xunit.v3", "3.2.2", Expression("Apache-2.0"));
            AddPackage("Some.Bsd", "1.0.0", Expression("BSD-3-Clause"));
            AddPackage("Other.Bsd", "1.0.0", Expression("BSD-2-Clause"));
            Passes(Run(WriteAssets()), 4);
        }

        [Fact]
        public void AProprietaryPackageFailsTheCheck()
        {
            // What an Accelerate component looks like to restore: a licence that is not an open-source one.
            AddPackage("Avalonia", "12.1.4", Expression("MIT"));
            AddPackage("Avalonia.Controls.TreeDataGrid.Pro", "12.0.0", Expression("LicenseRef-Avalonia-Commercial"));
            Fails(Run(WriteAssets()), "Avalonia.Controls.TreeDataGrid.Pro 12.0.0", "is not on the allow-list");
        }

        [Fact]
        public void ACopyleftLicenceOutsideTheListFails()
        {
            // Open source, but not on the list: widening the list is a decision, not a default.
            AddPackage("Some.Lib", "1.0.0", Expression("LGPL-2.1-only"));
            Fails(Run(WriteAssets()), "Some.Lib 1.0.0", "is not on the allow-list");
        }

        [Fact]
        public void AnExpressionPassesOnlyIfOneAlternativeIsWhollyAllowed()
        {
            AddPackage("Either", "1.0.0", Expression("LicenseRef-Proprietary OR MIT"));
            AddPackage("Grouped", "1.0.0", Expression("(MIT AND Apache-2.0)"));
            Passes(Run(WriteAssets()), 2);
        }

        [Fact]
        public void AnExpressionWithAnyDisallowedTermInEveryAlternativeFails()
        {
            AddPackage("Both", "1.0.0", Expression("MIT AND LicenseRef-Proprietary"));
            Fails(Run(WriteAssets()), "Both 1.0.0", "is not on the allow-list");
        }

        [Fact]
        public void AnUnreviewedLicenceFileFails()
        {
            AddPackage("Some.Natives", "1.0.0", "<license type=\"file\">LICENSE</license>", ("LICENSE", "All rights reserved."));
            Fails(Run(WriteAssets()), "Some.Natives 1.0.0", "read it and add the package to $Reviewed");
        }

        [Fact]
        public void TheReviewedLicenceFilePassesOnlyWhileItIsTheOneReviewed()
        {
            // The real reviewed entry: ANGLE's BSD 3-clause text, pinned by hash. Reproduce the package with the
            // shipped file's exact bytes, then with one byte added.
            AddPackage("Avalonia.Angle.Windows.Natives", "2.1.27548.20260419", "<license type=\"file\">LICENSE</license>");
            string copy = Path.Combine(_dir, "packages", "avalonia.angle.windows.natives", "2.1.27548.20260419", "LICENSE");
            File.Copy(LicenceOfRestoredAngle(), copy);
            Assert.Equal("54AFF7276217DF9F6B5181613999D208C9E40D2B1D51BF55217837E6871A4A63",
                         Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(copy))));
            Passes(Run(WriteAssets()), 1);

            File.AppendAllText(copy, " ");
            Fails(Run(WriteAssets()), "Avalonia.Angle.Windows.Natives", "is not the one reviewed");
        }

        [Fact]
        public void ALicenceFileMissingFromThePackageFails()
        {
            AddPackage("Some.Natives", "1.0.0", "<license type=\"file\">LICENSE.txt</license>");
            Fails(Run(WriteAssets()), "Some.Natives 1.0.0", "is not in the package");
        }

        [Fact]
        public void ALicenceUrlIsAcceptedOnlyFromTheNuGetLicenceService()
        {
            AddPackage("Old.Mit", "1.0.0", "<licenseUrl>https://licenses.nuget.org/MIT</licenseUrl>");
            Passes(Run(WriteAssets()), 1);
        }

        [Fact]
        public void AnyOtherLicenceUrlFails()
        {
            AddPackage("Old.Vendor", "1.0.0", "<licenseUrl>https://example.com/eula.html</licenseUrl>");
            Fails(Run(WriteAssets()), "Old.Vendor 1.0.0", "has only a licence URL");
        }

        [Fact]
        public void ANuGetLicenceUrlNamingADisallowedLicenceFails()
        {
            AddPackage("Old.Gpl", "1.0.0", "<licenseUrl>https://licenses.nuget.org/GPL-2.0-only</licenseUrl>");
            Fails(Run(WriteAssets()), "Old.Gpl 1.0.0", "names a licence not on the allow-list");
        }

        [Fact]
        public void ALicenceOfAnUnknownTypeFails()
        {
            AddPackage("Odd", "1.0.0", "<license type=\"other\">MIT</license>");
            Fails(Run(WriteAssets()), "Odd 1.0.0", "unknown type 'other'");
        }

        [Fact]
        public void APackageWithNoLicenceFails()
        {
            AddPackage("Bare", "1.0.0", "<description>no licence at all</description>");
            Fails(Run(WriteAssets()), "Bare 1.0.0", "declares no licence");
        }

        [Fact]
        public void APackageWhoseNuspecCannotBeFoundFails()
        {
            AddPackage("Avalonia", "12.1.4", Expression("MIT"));
            AddListedOnly("Ghost", "1.0.0");
            Fails(Run(WriteAssets()), "Ghost 1.0.0", "no .nuspec found");
        }

        [Fact]
        public void EveryOffenderIsNamedNotJustTheFirst()
        {
            AddPackage("First.Bad", "1.0.0", Expression("LicenseRef-A"));
            AddPackage("Fine", "1.0.0", Expression("MIT"));
            AddPackage("Second.Bad", "1.0.0", Expression("LicenseRef-B"));
            var r = Run(WriteAssets());
            Fails(r, "First.Bad 1.0.0", "is not on the allow-list");
            Fails(r, "Second.Bad 1.0.0", "is not on the allow-list");
            Assert.Contains("2 of 3 package(s)", r.Output);
        }

        [Fact]
        public void AMissingAssetsFileOrPackagesFolderFails()
        {
            var missing = Run(Path.Combine(_dir, "nowhere", "project.assets.json"));
            Assert.True(missing.Exit != 0);
            Assert.Contains("error TMDQLIC: no assets file", missing.Output);

            AddPackage("Avalonia", "12.1.4", Expression("MIT"));
            var noFolder = Run(WriteAssets(withFolder: false));
            Assert.True(noFolder.Exit != 0);
            Assert.Contains("names no packages folder", noFolder.Output);
        }

        [Fact]
        public void EveryProjectInTheSolutionPassesAndImportsTheCheck()
        {
            // The real thing: each project's own restore output, transitive packages included.
            foreach (string project in new[] { "TmDq.Core", "TmDq.Core.Tests", "TmDq.App", "TmDq.App.Tests" })
            {
                string csproj = File.ReadAllText(Repo.File(Path.Combine("dotnet", project, project + ".csproj")));
                Assert.True(csproj.Contains(@"<Import Project=""..\build\PackageLicences.targets"" />"), project + " does not import the licence check");
                string assets = Path.Combine(Repo.Root, "dotnet", project, "obj", "project.assets.json");
                if (project == "TmDq.Core" && !File.Exists(assets)) continue;   // no packages, nothing restored
                var r = Run(assets);
                Assert.True(r.Exit == 0, project + ":\n" + r.Output);
            }
        }

        /// <summary>ANGLE's licence as restore put it on disk for the application (Avalonia.Desktop brings it).</summary>
        private static string LicenceOfRestoredAngle()
        {
            string assets = Path.Combine(Repo.Root, "dotnet", "TmDq.App", "obj", "project.assets.json");
            using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(assets));
            foreach (JsonProperty folder in doc.RootElement.GetProperty("packageFolders").EnumerateObject())
            {
                string licence = Path.Combine(folder.Name, "avalonia.angle.windows.natives", "2.1.27548.20260419", "LICENSE");
                if (File.Exists(licence)) return licence;
            }
            throw new FileNotFoundException("the application's restore did not bring Avalonia.Angle.Windows.Natives 2.1.27548.20260419");
        }
    }
}
