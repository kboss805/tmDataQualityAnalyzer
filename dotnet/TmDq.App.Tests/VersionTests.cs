using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text.RegularExpressions;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The version is written once, in AppVersion (include/constants.h), and dotnet/build/AppVersion.props reads it
    /// into every assembly. These tests read the header themselves, with their own pattern, and require every
    /// place the version reaches to agree with it, so a project that forgets the import (and ships 1.0.0) fails.
    /// </summary>
    public sealed class VersionTests
    {
        /// <summary>"major.minor.patch" from AppVersion's three definitions.</summary>
        public static string HeaderVersion()
        {
            string text = File.ReadAllText(Repo.File(Path.Combine("include", "constants.h")));
            string Number(string name)
            {
                MatchCollection found = Regex.Matches(text, @"static\s+constexpr\s+int\s+" + name + @"\s*=\s*(\d+)\s*;");
                Assert.True(found.Count == 1, $"AppVersion::{name} is defined {found.Count} times in constants.h");
                return found[0].Groups[1].Value;
            }
            return Number("kMajor") + "." + Number("kMinor") + "." + Number("kPatch");
        }

        [Fact]
        public void TheApplicationAssemblyCarriesTheHeaderVersion()
        {
            string version = HeaderVersion();
            Assembly app = typeof(AppInfo).Assembly;
            Assert.Equal(version, AppInfo.Version);
            Assert.Equal(new Version(version + ".0"), app.GetName().Version);
            Assert.Equal(version + ".0", app.GetCustomAttribute<AssemblyFileVersionAttribute>()?.Version);
        }

        [Fact]
        public void TheCoreLibraryCarriesTheHeaderVersion()
        {
            Assert.Equal(new Version(HeaderVersion() + ".0"), typeof(TmDq.Core.Library).Assembly.GetName().Version);
        }

        [Fact]
        public void TheExecutableCarriesTheHeaderVersionInItsVersionResource()
        {
            // What Explorer, the installer and a crash report read.
            string exe = Path.Combine(Repo.AppOutput, "tmDataQualityAnalyzer.exe");
            Assert.True(File.Exists(exe), "the application has not been built: " + exe);
            FileVersionInfo info = FileVersionInfo.GetVersionInfo(exe);
            Assert.Equal(HeaderVersion() + ".0", info.FileVersion);
            Assert.Equal(HeaderVersion(), info.ProductVersion);
            Assert.Equal("TM Data Quality Analyzer", info.ProductName);
        }
    }
}
