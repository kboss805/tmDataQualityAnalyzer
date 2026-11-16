using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// Rules about the repository's own files that nothing else would notice until a build, a runner or a release broke on
/// them: the scripts' encoding, the projects' output folders, the test hosts' manifest, the workflow's limits.
/// </summary>
public class RepoFilesTests
{
    private static readonly string[] SkippedFolders = { "bin", "obj", "out", "staging", "third_party", "node_modules", ".git", ".claude" };

    private static IEnumerable<string> Files(string folder, params string[] patterns)
    {
        string start = Path.Combine(RepoPaths.Root, folder);
        var pending = new Stack<string>();
        pending.Push(start);
        while (pending.Count > 0)
        {
            string dir = pending.Pop();
            foreach (string pattern in patterns)
            {
                foreach (string file in Directory.EnumerateFiles(dir, pattern))
                {
                    yield return file;
                }
            }
            foreach (string sub in Directory.EnumerateDirectories(dir))
            {
                if (!SkippedFolders.Contains(Path.GetFileName(sub), StringComparer.OrdinalIgnoreCase))
                {
                    pending.Push(sub);
                }
            }
        }
    }

    [Fact]
    public void TheScriptsTheInstallerAndTheWorkflowAreAsciiOnly()
    {
        // Windows PowerShell 5.1 reads a UTF-8 file with no byte-order mark as Windows-1252, so one em dash inside a
        // string decodes to a curly quote that ends it (audit B7: a script that could not even be parsed); Inno Setup
        // and the workflow's temp scripts read the same way. The files are plain ASCII, and stay so.
        var offenders = new List<string>();
        foreach (string folder in new[] { "eng", "deploy", ".github" })
        {
            foreach (string file in Files(folder, "*.ps1", "*.psm1", "*.iss", "*.yml", "*.yaml", "*.bat", "*.cmd"))
            {
                byte[] bytes = File.ReadAllBytes(file);
                int at = Array.FindIndex(bytes, b => b > 127);
                if (at >= 0)
                {
                    offenders.Add($"{Path.GetRelativePath(RepoPaths.Root, file)} (byte {at})");
                }
            }
        }
        Assert.True(offenders.Count == 0, "not ASCII: " + string.Join(", ", offenders));
    }

    [Fact]
    public void TheCheckSeesTheFilesItIsMeantToGuard()
    {
        // A walk that found nothing would pass the test above for ever.
        string[] names = Files("eng", "*.ps1").Select(Path.GetFileName).ToArray()!;
        Assert.Contains("env.ps1", names);
        Assert.Contains("mutate.ps1", names);
        Assert.NotEmpty(Files(".github", "*.yml"));
        Assert.NotEmpty(Files("deploy", "*.iss"));
    }

    [Fact]
    public void EveryDotnetProjectKeepsItsOutputWhereItAlwaysIs()
    {
        // vcvars leaves Platform=x64 in the shell; with AppendPlatformToOutputPath=false a dotnet build puts its output
        // in bin\<Configuration>\ all the same, which is why no script has to clear the variable.
        var projects = Files("src", "*.csproj").Concat(Files("tests", "*.csproj")).ToList();
        Assert.True(projects.Count >= 4, "the projects were not found");
        foreach (string project in projects)
        {
            Assert.Contains("<AppendPlatformToOutputPath>false</AppendPlatformToOutputPath>", File.ReadAllText(project));
        }
    }

    [Fact]
    public void TheTwoDotnetTestHostsMergeTheSharedUtf8ManifestAndNeitherHasACopy()
    {
        foreach (string project in new[] { "TmDq.Core.Tests", "TmDq.App.Tests" })
        {
            string csproj = File.ReadAllText(RepoPaths.File(Path.Combine("tests", project, project + ".csproj")));
            Assert.Contains("utf8codepage.manifest", csproj);
            Assert.False(File.Exists(Path.Combine(RepoPaths.Root, "tests", project, "app.manifest")), project + " has a manifest of its own");
        }
        Assert.True(File.Exists(RepoPaths.File(Path.Combine("assets", "utf8codepage.manifest"))));
    }

    [Fact]
    public void TheWorkflowLimitsItsRunTimeAndItsToken()
    {
        string ci = File.ReadAllText(RepoPaths.File(Path.Combine(".github", "workflows", "ci.yml")));
        Assert.Matches(@"(?m)^\s+timeout-minutes:\s*\d+\s*$", ci);
        Assert.Matches(@"(?ms)^permissions:\s*\r?\n\s+contents:\s*read\s*$", ci);
    }
}
