using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Xml.Linq;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// The two native projects' source lists against the files on disk. MSBuild compiles only what a project lists, so a
/// core source left out of tmdq_core.vcxproj is a link error at best, and a test file left out of
/// tmdq_core_tests.vcxproj is a suite that silently never runs. Since the v2.x application went (plan step 6.1), every
/// source in src/model is a core source, and these checks are the only ones that hold the projects to the disk.
/// </summary>
public class NativeProjectTests
{
    private static string Native => Path.Combine(Env.RepoRoot, "native");

    private static string Model => Path.Combine(Env.RepoRoot, "src", "model");

    /// <summary>The full paths a project compiles, from its <c>ClCompile</c> items.</summary>
    private static string[] Compiled(string project)
    {
        XDocument doc = XDocument.Load(project);
        string folder = Path.GetDirectoryName(project)!;
        return doc.Descendants().Where(e => e.Name.LocalName == "ClCompile" && e.Attribute("Include") is not null)
                  .Select(e => Path.GetFullPath(Path.Combine(folder, e.Attribute("Include")!.Value)))
                  .ToArray();
    }

    private static string[] Under(string folder, string[] compiled) =>
        compiled.Where(p => string.Equals(Path.GetDirectoryName(p), Path.GetFullPath(folder), StringComparison.OrdinalIgnoreCase))
                .Select(p => Path.GetFileName(p).ToLowerInvariant()).Order(StringComparer.Ordinal).ToArray();

    private static string[] OnDisk(string folder, string pattern) =>
        Directory.GetFiles(folder, pattern).Select(p => Path.GetFileName(p).ToLowerInvariant()).Order(StringComparer.Ordinal).ToArray();

    private static string[] Core => Compiled(Path.Combine(Native, "tmdq_core", "tmdq_core.vcxproj"));

    private static string[] Tests => Compiled(Path.Combine(Native, "tmdq_core_tests", "tmdq_core_tests.vcxproj"));

    [Fact]
    public void EveryFileAProjectCompilesExistsAndIsListedOnce()
    {
        foreach (string[] list in new[] { Core, Tests })
        {
            Assert.NotEmpty(list);
            Assert.All(list, p => Assert.True(File.Exists(p), p + " is listed but does not exist"));
            Assert.Equal(list.Length, list.Distinct(StringComparer.OrdinalIgnoreCase).Count());
        }
    }

    [Fact]
    public void TheDllCompilesEveryCoreAndAbiSource()
    {
        Assert.Equal(OnDisk(Model, "*.cpp"), Under(Model, Core));
        Assert.Equal(OnDisk(Path.Combine(Native, "tmdq_core"), "*.cpp"), Under(Path.Combine(Native, "tmdq_core"), Core));
    }

    [Fact]
    public void TheNativeTestsCompileEveryTestFileAndTheDllsOwnSources()
    {
        string tests = Path.Combine(Native, "tmdq_core_tests");
        Assert.Equal(OnDisk(tests, "*.cpp"), Under(tests, Tests));
        // The tests build the core and the ABI in, so they test the very sources the DLL is made of, and nothing else.
        Assert.Equal(Core.Order(StringComparer.OrdinalIgnoreCase), Tests.Where(p => !p.StartsWith(tests, StringComparison.OrdinalIgnoreCase))
                                                                         .Order(StringComparer.OrdinalIgnoreCase));
    }
}
