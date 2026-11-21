using System;
using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// eng\check_release_core.ps1, which the package script runs on the core it builds and on the copy it stages: a Debug core
/// links the debug C runtime, a Release core never does, and the package must carry the Release one (a Debug core is about
/// three times slower and nothing else in the release build would say so). Each case runs the real script, as the package
/// script does; the import lists are the real ones, so a refusal that fires on the wrong names would show here.
/// </summary>
public sealed class ReleaseCoreGuardTests
{
    /// <summary>The imports of the real Release tmdq_core.dll (dumpbin /dependents).</summary>
    private const string ReleaseImports =
        "MSVCP140.dll,VCRUNTIME140.dll,VCRUNTIME140_1.dll,api-ms-win-crt-runtime-l1-1-0.dll,api-ms-win-crt-heap-l1-1-0.dll," +
        "api-ms-win-crt-string-l1-1-0.dll,api-ms-win-crt-math-l1-1-0.dll,api-ms-win-crt-locale-l1-1-0.dll," +
        "api-ms-win-crt-stdio-l1-1-0.dll,api-ms-win-crt-filesystem-l1-1-0.dll,api-ms-win-crt-time-l1-1-0.dll," +
        "api-ms-win-crt-convert-l1-1-0.dll,KERNEL32.dll";

    private static (int Exit, string Output) Run(params string[] arguments)
    {
        var start = new ProcessStartInfo("powershell")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        foreach (string a in new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", RepoPaths.File(Path.Combine("eng", "check_release_core.ps1")) })
            start.ArgumentList.Add(a);
        foreach (string a in arguments) start.ArgumentList.Add(a);
        using Process process = Process.Start(start)!;
        // Both streams are read before waiting, or a full pipe would stop the child.
        string output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
        Assert.True(process.WaitForExit(120_000), "the release-core check did not finish");
        return (process.ExitCode, output);
    }

    [Fact]
    public void AReleaseCoresImportsPass()
    {
        (int exit, string output) = Run("-Imports", ReleaseImports);
        Assert.True(exit == 0, "expected a pass:\n" + output);
        Assert.Contains("13 imports checked", output);
    }

    [Fact]
    public void TheRuntimesWithNumberedSuffixesAreNotMistakenForDebugOnes()
    {
        // _1 and _2 are the release runtime's own extra DLLs; only a trailing d marks the debug one.
        (int exit, string output) = Run("-Imports", "VCRUNTIME140_1.dll,MSVCP140_1.dll,MSVCP140_2.dll,CONCRT140.dll,VCOMP140.dll,ucrtbase.dll");
        Assert.True(exit == 0, "expected a pass:\n" + output);
    }

    [Theory]
    [InlineData("VCRUNTIME140D.dll")]
    [InlineData("VCRUNTIME140_1D.dll")]
    [InlineData("MSVCP140D.dll")]
    [InlineData("ucrtbased.dll")]
    [InlineData("CONCRT140D.dll")]
    [InlineData("VCOMP140D.dll")]
    [InlineData("vcruntime140_1d.dll")]
    [InlineData("UCRTBASED.DLL")]
    public void TheDebugRuntimeIsRefusedWhateverTheCaseAndNamesTheFile(string debugImport)
    {
        (int exit, string output) = Run("-Imports", ReleaseImports + "," + debugImport);
        Assert.True(exit == 1, "expected a refusal:\n" + output);
        Assert.Contains(debugImport, output);
        Assert.Contains("Release", output);
    }

    [Fact]
    public void ADebugImportAmongTheOthersIsFoundWhereverItIs()
    {
        // First, last and alone: the scan must read the whole list.
        foreach (string list in new[] { "ucrtbased.dll," + ReleaseImports, ReleaseImports + ",MSVCP140D.dll", "VCRUNTIME140D.dll" })
        {
            (int exit, string output) = Run("-Imports", list);
            Assert.True(exit == 1, $"expected a refusal for '{list}':\n{output}");
        }
    }

    [Fact]
    public void NothingToCheckIsNotAPass()
    {
        (int exit, string output) = Run("-Imports", "");
        Assert.True(exit == 1, "an empty import list must not read as clean:\n" + output);
        Assert.Contains("cannot verify", output);

        string missing = Path.Combine(Path.GetTempPath(), "tmdq-no-such-core-" + Guid.NewGuid().ToString("N") + ".dll");
        (int exitMissing, string outputMissing) = Run("-Dll", missing);
        Assert.True(exitMissing == 1, "a missing DLL must not read as clean:\n" + outputMissing);
        Assert.Contains("no such file", outputMissing);
    }

    [Fact]
    public void ABadInvocationIsAUsageErrorAndChecksNothing()
    {
        Assert.Equal(2, Run().Exit);
        Assert.Equal(2, Run("-Dll", "x.dll", "-Imports", ReleaseImports).Exit);
    }

    [Theory]
    [InlineData("Release", 0)]
    [InlineData("Debug", 1)]
    public void TheRealCoresAreToldApart(string configuration, int expectedExit)
    {
        // CI builds both before it runs the suites; elsewhere a configuration that was never built is a skip, not a pass.
        string dll = Path.Combine(RepoPaths.Root, "out", configuration, "tmdq_core.dll");
        Assert.SkipUnless(File.Exists(dll), $"out\\{configuration}\\tmdq_core.dll has not been built here");
        (int exit, string output) = Run("-Dll", dll);
        Assert.True(exit == expectedExit, $"the {configuration} core: expected exit {expectedExit}, got {exit}:\n{output}");
    }

    [Fact]
    public void ThePackageScriptChecksTheCoreItBuildsAndTheCopyItStages()
    {
        // Nothing else in the release would notice a Debug core, so the calls must be there, on those two files, in those
        // places: the first before the publish (a mistake costs seconds, not the packaging run), the second on what ships.
        string package = File.ReadAllText(RepoPaths.File(Path.Combine("deploy", "build_package.ps1")));
        const string call = @"eng\check_release_core.ps1') -Dll ";
        int built = package.IndexOf(call + "$CoreDll", StringComparison.Ordinal);
        int staged = package.IndexOf(call + "(Join-Path $AppDir 'tmdq_core.dll')", StringComparison.Ordinal);
        int publish = package.IndexOf("[2/8] Publishing", StringComparison.Ordinal);
        int staging = package.IndexOf("[4/8] Staging", StringComparison.Ordinal);
        int signing = package.IndexOf("[5/8] Signing", StringComparison.Ordinal);
        Assert.True(built > 0, "the package script does not check the core it builds");
        Assert.True(staged > 0, "the package script does not check the core it stages");
        Assert.True(built < publish, "the built core must be checked before the publish");
        Assert.InRange(staged, staging, signing);
        // A failed check must stop the packaging, not print and carry on.
        Assert.Equal(2, Regex.Count(package, @"check_release_core\.ps1[^\r\n]*\r?\n\s*if \(\$LASTEXITCODE -ne 0\) \{ throw "));
    }
}
