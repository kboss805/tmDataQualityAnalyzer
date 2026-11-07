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
/// source in src/core/src is a core source, and these checks are the only ones that hold the projects to the disk.
/// </summary>
public class NativeProjectTests
{
    private static string CoreFolder => Path.Combine(Env.RepoRoot, "src", "core");

    private static string Sources => Path.Combine(CoreFolder, "src");

    private static string Abi => Path.Combine(CoreFolder, "abi");

    private static string TestsFolder => Path.Combine(Env.RepoRoot, "tests", "core");

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

    /// <summary>The files a folder holds, never none: an empty answer would make every comparison with a project's
    /// list pass whatever the list says, which is what a path to the wrong folder gives.</summary>
    private static string[] OnDisk(string folder, string pattern)
    {
        string[] files = Directory.GetFiles(folder, pattern).Select(p => Path.GetFileName(p).ToLowerInvariant()).Order(StringComparer.Ordinal).ToArray();
        Assert.True(files.Length > 0, "no " + pattern + " in " + folder);
        return files;
    }

    private static string[] Core => Compiled(Path.Combine(CoreFolder, "tmdq_core.vcxproj"));

    private static string[] Tests => Compiled(Path.Combine(TestsFolder, "tmdq_core_tests.vcxproj"));

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
        Assert.Equal(OnDisk(Sources, "*.cpp"), Under(Sources, Core));
        Assert.Equal(OnDisk(Abi, "*.cpp"), Under(Abi, Core));
    }

    [Fact]
    public void TheNativeTestsCompileEveryTestFileAndTheDllsOwnSources()
    {
        string tests = TestsFolder;
        Assert.Equal(OnDisk(tests, "*.cpp"), Under(tests, Tests));
        // The tests build the core and the ABI in, so they test the very sources the DLL is made of, and nothing else.
        Assert.Equal(Core.Order(StringComparer.OrdinalIgnoreCase), Tests.Where(p => !p.StartsWith(tests, StringComparison.OrdinalIgnoreCase))
                                                                         .Order(StringComparer.OrdinalIgnoreCase));
    }
}
