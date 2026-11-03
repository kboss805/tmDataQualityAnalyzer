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
/// tmdq_core_tests.vcxproj is a suite that silently never runs. The Qt suite's TestCoreQtFree checks the core's list
/// against the .pro's and goes with it (plan step 6.1); these checks do not depend on Qt, so they stay.
/// </summary>
public class NativeProjectTests
{
    private static string Native => Path.Combine(Env.RepoRoot, "native");

    private static string Model => Path.Combine(Env.RepoRoot, "src", "model");

    /// <summary>
    /// The Qt application's model sources: they live beside the core's but are not core, and go with the Qt application
    /// (plan step 6.1). Once they are deleted this list must be emptied, or the test fails.
    /// </summary>
    private static readonly string[] QtModelSources = { "processingtemplateschema.cpp", "streamconfigschema.cpp" };

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
        Assert.All(QtModelSources, f => Assert.True(File.Exists(Path.Combine(Model, f)),
            f + " is gone: take it off QtModelSources, so that every source in src/model is held to the core's list"));
        Assert.Equal(OnDisk(Model, "*.cpp").Except(QtModelSources), Under(Model, Core));
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
