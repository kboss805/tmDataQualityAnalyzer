using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// scripts\gen_third_party_notices.ps1, which writes THIRD-PARTY-NOTICES.txt for the packages (plan step 3.3) from
    /// the publish's deps file. Each case writes a deps file and a packages folder of its own and runs the real script.
    /// The licences it reproduces are obligations (MIT and BSD both require the copyright and licence text to travel
    /// with the binaries), so what it leaves out or summarises matters as much as what it writes.
    /// </summary>
    public sealed class ThirdPartyNoticesTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-notices-" + Guid.NewGuid().ToString("N"));
        private readonly Dictionary<string, object> _target = new();
        private readonly Dictionary<string, object> _libraries = new();

        public ThirdPartyNoticesTests() => Directory.CreateDirectory(Path.Combine(_dir, "packages"));

        public void Dispose()
        {
            try { Directory.Delete(_dir, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }

        /// <summary>A package in the packages folder, listed in the deps file with the given runtime files.</summary>
        private string AddPackage(string id, string version, string licenceXml, string copyright = "", string url = "",
                                  string[]? files = null, string type = "package")
        {
            string folder = Path.Combine(_dir, "packages", id.ToLowerInvariant(), version.ToLowerInvariant());
            Directory.CreateDirectory(folder);
            File.WriteAllText(Path.Combine(folder, id.ToLowerInvariant() + ".nuspec"),
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<package><metadata>" +
                $"<id>{id}</id><version>{version}</version>{licenceXml}" +
                (copyright.Length > 0 ? $"<copyright>{copyright}</copyright>" : "") +
                (url.Length > 0 ? $"<projectUrl>{url}</projectUrl>" : "") +
                "</metadata></package>\n");
            string key = (type == "runtimepack" ? "runtimepack." : "") + id + "/" + version;
            var runtime = (files ?? new[] { $"lib/net10.0/{id}.dll" }).ToDictionary(f => f, f => (object)new { });
            _target[key] = new Dictionary<string, object> { ["runtime"] = runtime };
            _libraries[key] = new Dictionary<string, object> { ["type"] = type, ["path"] = (id + "/" + version).ToLowerInvariant() };
            return folder;
        }

        private static string Mit => "<license type=\"expression\">MIT</license>";

        private (int Exit, string Output, string Notices) Run(bool nativeCore = false)
        {
            // The application's own project is in every deps file and is never a third party.
            _target["tmDataQualityAnalyzer/2.14.2"] = new Dictionary<string, object>
            {
                ["runtime"] = new Dictionary<string, object> { ["tmDataQualityAnalyzer.dll"] = new { } },
            };
            _libraries["tmDataQualityAnalyzer/2.14.2"] = new Dictionary<string, object> { ["type"] = "project" };
            const string rid = ".NETCoreApp,Version=v10.0/win-x64";
            string deps = Path.Combine(_dir, "app.deps.json");
            File.WriteAllText(deps, JsonSerializer.Serialize(new Dictionary<string, object>
            {
                ["runtimeTarget"] = new Dictionary<string, object> { ["name"] = rid },
                ["targets"] = new Dictionary<string, object> { [rid] = _target },
                ["libraries"] = _libraries,
            }));
            string output = Path.Combine(_dir, "out", "THIRD-PARTY-NOTICES.txt");
            var args = new List<string> { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                Repo.File(Path.Combine("scripts", "gen_third_party_notices.ps1")),
                "-DepsFile", deps, "-Out", output, "-PackagesFolder", Path.Combine(_dir, "packages") };
            if (!nativeCore) args.Add("-NoNativeCore");
            var start = new ProcessStartInfo("powershell")
            {
                RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true,
            };
            foreach (string a in args) start.ArgumentList.Add(a);
            using Process p = Process.Start(start)!;
            string text = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
            Assert.True(p.WaitForExit(60_000), "the generator did not finish");
            return (p.ExitCode, text, File.Exists(output) ? File.ReadAllText(output) : "");
        }

        private static int Count(string text, string what) => Regex.Matches(text, Regex.Escape(what)).Count;

        [Fact]
        public void AnMitPackageWithoutALicenceFileGetsTheMitTextWithItsOwnCopyright()
        {
            AddPackage("Some.Lib", "1.2.3", Mit, copyright: "Copyright 2020 © Some Authors");
            var r = Run();
            Assert.True(r.Exit == 0, r.Output);
            Assert.Contains("Some.Lib 1.2.3", r.Notices);
            Assert.Contains("Copyright 2020 © Some Authors", r.Notices);
            Assert.Contains("Permission is hereby granted, free of charge", r.Notices);
            Assert.Contains("THE SOFTWARE IS PROVIDED \"AS IS\"", r.Notices);
        }

        [Fact]
        public void ACopyrightWithoutTheWordGetsItPrefixed()
        {
            AddPackage("Tmds.Like", "1.0.0", Mit, copyright: "Tom Example");
            var r = Run();
            Assert.True(r.Exit == 0, r.Output);
            Assert.Contains("Copyright (c) Tom Example", r.Notices);
        }

        [Fact]
        public void APackagesOwnLicenceFileIsQuotedInsteadOfATemplate()
        {
            string folder = AddPackage("Native.Lib", "2.0.0", "<license type=\"file\">LICENSE</license>");
            File.WriteAllText(Path.Combine(folder, "LICENSE"), "Copyright 2018 The Native Authors.\nBSD licence text, verbatim.");
            var r = Run();
            Assert.True(r.Exit == 0, r.Output);
            Assert.Contains("BSD licence text, verbatim.", r.Notices);
            Assert.DoesNotContain("Permission is hereby granted", r.Notices);
        }

        [Fact]
        public void PackagesWithTheSameLicenceTextShareOneCopyAndAreAllNamed()
        {
            AddPackage("Family.A", "12.1.4", Mit, copyright: "Copyright The Family");
            AddPackage("Family.B", "12.1.4", Mit, copyright: "Copyright The Family");
            var r = Run();
            Assert.True(r.Exit == 0, r.Output);
            Assert.Equal(1, Count(r.Notices, "Permission is hereby granted"));
            Assert.Contains("Family.A 12.1.4", r.Notices);
            Assert.Contains("Family.B 12.1.4", r.Notices);
        }

        [Fact]
        public void ANoticesFileCarriedByTwoPackagesIsIncludedOnceNamingBoth()
        {
            // SkiaSharp's and HarfBuzzSharp's native packages carry the same 140 KB file.
            foreach (string id in new[] { "Skia.Natives", "HarfBuzz.Natives" })
            {
                string folder = AddPackage(id, "1.0.0", Mit, copyright: "Copyright Microsoft", files: new[] { $"runtimes/win-x64/native/lib{id}.dll" });
                File.WriteAllText(Path.Combine(folder, "THIRD-PARTY-NOTICES.txt"), "Skia: Copyright Google. BSD-3-Clause text.");
            }
            var r = Run();
            Assert.True(r.Exit == 0, r.Output);
            Assert.Equal(1, Count(r.Notices, "Skia: Copyright Google. BSD-3-Clause text."));
            Assert.Matches("Third-party notices carried by .*HarfBuzz\\.Natives 1\\.0\\.0.*Skia\\.Natives 1\\.0\\.0", r.Notices);
        }

        [Fact]
        public void TheRuntimePackIsIncludedUnderItsPackageName()
        {
            string folder = AddPackage("Microsoft.NETCore.App.Runtime.win-x64", "10.0.14", Mit, files: new[] { "runtimes/win-x64/lib/net10.0/System.Private.CoreLib.dll" }, type: "runtimepack");
            File.WriteAllText(Path.Combine(folder, "LICENSE.TXT"), "The MIT License (MIT)\nCopyright (c) .NET Foundation and Contributors");
            File.WriteAllText(Path.Combine(folder, "THIRD-PARTY-NOTICES.TXT"), ".NET Runtime uses third-party libraries.");
            var r = Run();
            Assert.True(r.Exit == 0, r.Output);
            Assert.Contains("Microsoft.NETCore.App.Runtime.win-x64 10.0.14", r.Notices);
            Assert.DoesNotContain("runtimepack.", r.Notices);
            Assert.Contains(".NET Runtime uses third-party libraries.", r.Notices);
        }

        [Fact]
        public void TheAppsOwnProjectsAndPackagesThatShipNoBinaryAreLeftOut()
        {
            AddPackage("Ships", "1.0.0", Mit, copyright: "Copyright S");
            AddPackage("BuildOnly", "1.0.0", Mit, copyright: "Copyright B", files: new[] { "lib/net10.0/_._" });
            var r = Run();
            Assert.True(r.Exit == 0, r.Output);
            Assert.Contains("Ships 1.0.0", r.Notices);
            Assert.DoesNotContain("BuildOnly", r.Notices);
            Assert.DoesNotContain("tmDataQualityAnalyzer/", r.Notices);
            Assert.Contains("1 packages", r.Output);
            // The summary at the top lists each shipped package with its version and licence.
            Assert.Matches(@"(?m)^\s+Ships\s+1\.0\.0\s+MIT\s*$", r.Notices);
        }

        [Fact]
        public void ALicenceWithNoFileAndNoTemplateIsAnErrorNotASummary()
        {
            AddPackage("Odd.Licence", "1.0.0", "<license type=\"expression\">Apache-2.0</license>", copyright: "Copyright O");
            var r = Run();
            Assert.True(r.Exit != 0, "an Apache-2.0 package without its licence file was summarised:\n" + r.Notices);
            Assert.Contains("Odd.Licence 1.0.0", r.Output);
        }

        [Fact]
        public void AnMitPackageWithNoCopyrightLineIsAnError()
        {
            AddPackage("No.Copyright", "1.0.0", Mit);
            var r = Run();
            Assert.True(r.Exit != 0, r.Notices);
            Assert.Contains("No.Copyright 1.0.0", r.Output);
        }

        [Fact]
        public void APackageMissingFromThePackagesFolderIsAnError()
        {
            string folder = AddPackage("Gone", "1.0.0", Mit, copyright: "Copyright G");
            Directory.Delete(folder, recursive: true);
            var r = Run();
            Assert.True(r.Exit != 0, r.Notices);
            Assert.Contains("Gone 1.0.0", r.Output);
        }

        [Fact]
        public void TheNativeCoresVendoredLibraryIsQuotedFromItsHeader()
        {
            // tmdq_core.dll compiles in irig106, under a BSD 3-clause licence carried in its source headers.
            AddPackage("Some.Lib", "1.0.0", Mit, copyright: "Copyright S");
            var r = Run(nativeCore: true);
            Assert.True(r.Exit == 0, r.Output);
            Assert.Contains("irig106 library, compiled into tmdq_core.dll", r.Notices);
            Assert.Contains("Copyright (c) 2006 Irig106.org", r.Notices);
            Assert.Contains("Neither the name Irig106.org nor the names of its contributors", r.Notices);
            Assert.DoesNotContain("#ifndef", r.Notices);
            Assert.DoesNotContain("Define features", r.Notices);   // the licence, not the file's own description
            Assert.Matches(@"(?m)^\s+irig106 library \(in tmdq_core\.dll\)\s+BSD-3-Clause\s*$", r.Notices);
        }

        [Fact]
        public void ProjectUrlsLoseTheirTrackingQueryStrings()
        {
            AddPackage("Tracked", "1.0.0", Mit, copyright: "Copyright T", url: "https://example.org/?utm_source=nuget&amp;utm_medium=referral");
            var r = Run();
            Assert.True(r.Exit == 0, r.Output);
            Assert.Contains("(https://example.org/)", r.Notices);
            Assert.DoesNotContain("utm_", r.Notices);
        }

        [Fact]
        public void TheFileIsUtf8WithAByteOrderMarkSoNotepadShowsTheCopyrightSigns()
        {
            AddPackage("Sign.Lib", "1.0.0", Mit, copyright: "Copyright © Sign");
            Run();
            byte[] bytes = File.ReadAllBytes(Path.Combine(_dir, "out", "THIRD-PARTY-NOTICES.txt"));
            Assert.Equal(new byte[] { 0xEF, 0xBB, 0xBF }, bytes.Take(3).ToArray());
            Assert.Contains("Copyright © Sign", Encoding.UTF8.GetString(bytes));
        }
    }
}
