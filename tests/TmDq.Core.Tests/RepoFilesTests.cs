using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.RegularExpressions;
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

    [Fact]
    public void NoFileButTheProjectsOwnIsNamedClaudeMdWhereClaudeCodeWouldLoadItAsInstructions()
    {
        // Claude Code loads a file named CLAUDE.md as instructions the first time anything under its folder is read, and
        // Windows' case-insensitive names make "claude.md" one too: docs/claude.md put its 81 KB into every session (and every
        // subagent) that opened a file in docs (audit T4). The developer guide is docs/guide.md; the instructions are
        // .claude/CLAUDE.md, and nothing else may carry the name.
        var offenders = Files(".", "*.md")
            .Where(f => string.Equals(Path.GetFileName(f), "claude.md", StringComparison.OrdinalIgnoreCase))
            .Select(f => Path.GetRelativePath(RepoPaths.Root, f))
            .ToList();
        Assert.True(offenders.Count == 0, "named like Claude Code's instructions file: " + string.Join(", ", offenders));
        Assert.True(File.Exists(RepoPaths.File(Path.Combine("docs", "guide.md"))), "the developer guide is not where the docs say");
    }

    [Fact]
    public void EveryKeptMutantStillNamesTextThatIsInItsFileExactlyOnce()
    {
        // A kept list (eng\mutants) is a regression check only while each mutant's "old" text is still in its file: after
        // a rename or a re-indent eng\mutate.ps1 answers NO-MATCH and checks nothing, and nobody is told. 28 of 155 had
        // gone that way (the follow-up audit of 2026-11-16), most of them in one re-indent. A change that moves the text
        // a mutant names now fails here, and the list is re-anchored in the same change.
        string[] lists = Directory.GetFiles(Path.Combine(RepoPaths.Root, "eng", "mutants"), "*.json");
        Assert.True(lists.Length >= 16, "the mutant lists were not found");
        var problems = new List<string>();
        int mutants = 0;
        foreach (string list in lists)
        {
            string name = Path.GetFileName(list);
            // Strict JSON: a raw line break inside a string (one list had one) is refused here as a stricter reader would.
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(list));
            var ids = new HashSet<string>(StringComparer.Ordinal);
            foreach (JsonElement mutant in document.RootElement.GetProperty("mutants").EnumerateArray())
            {
                mutants++;
                string id = mutant.GetProperty("id").GetString()!;
                string file = mutant.GetProperty("file").GetString()!;
                string old = mutant.GetProperty("old").GetString()!.ReplaceLineEndings("\n");
                string replacement = mutant.GetProperty("new").GetString()!.ReplaceLineEndings("\n");
                if (!ids.Add(id)) problems.Add($"{name} {id}: the id is used twice");
                if (old.Length == 0 || old == replacement) problems.Add($"{name} {id}: the mutant changes nothing");
                string path = Path.Combine(RepoPaths.Root, file.Replace('/', Path.DirectorySeparatorChar));
                if (!File.Exists(path))
                {
                    problems.Add($"{name} {id}: {file} does not exist");
                    continue;
                }
                int found = Occurrences(File.ReadAllText(path).ReplaceLineEndings("\n"), old);
                if (found != 1) problems.Add($"{name} {id}: its old text is in {file} {found} times");
            }
        }
        Assert.True(mutants >= 170, $"only {mutants} mutants were read");
        Assert.True(problems.Count == 0, string.Join(Environment.NewLine, problems));
    }

    [Fact]
    public void EveryNativeTestCaseIsNamedForItsSuiteAndSaysWhatItHoldsInWords()
    {
        // The native cases are named "<Suite>: <what it holds, as words>" (the write-test skill): the name is what a failure
        // and a mutation check's filter report. A third of them were identifiers (Abi: theStructLayoutsArePinned,
        // Constants: uiSamplePeriods) or had no suite (the follow-up audit's C7).
        List<string> names = NativeTestCaseNames();
        var problems = names.Where(n => !Regex.IsMatch(n, @"^[A-Za-z][A-Za-z0-9 ]*: \S+ \S+")).ToList();
        Assert.True(names.Count >= 500, $"only {names.Count} test cases were read");
        Assert.True(problems.Count == 0, "not '<Suite>: <words>': " + string.Join(" | ", problems));
    }

    [Fact]
    public void EveryTestCaseFilterOfAKeptNativeMutantListStillMatchesACase()
    {
        // A filter that matches nothing runs no test, so the mutants of its list all SURVIVE or all say nothing ran. Renaming
        // a case (the audit's C7 did, 90 of them) must re-anchor the filters that name it, in the same change.
        List<string> names = NativeTestCaseNames();
        var problems = new List<string>();
        int filters = 0;
        foreach (string list in Directory.GetFiles(Path.Combine(RepoPaths.Root, "eng", "mutants"), "*.json"))
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(list));
            JsonElement root = document.RootElement;
            if (root.GetProperty("suite").GetString() != "native")
            {
                continue;
            }

            var argSets = new List<JsonElement>();
            if (root.TryGetProperty("args", out JsonElement top)) argSets.Add(top);
            foreach (JsonElement mutant in root.GetProperty("mutants").EnumerateArray())
            {
                if (mutant.TryGetProperty("args", out JsonElement own)) argSets.Add(own);
            }

            foreach (string arg in argSets.SelectMany(a => a.EnumerateArray()).Select(a => a.GetString()!).Where(a => a.StartsWith("-tc=", StringComparison.Ordinal)).Distinct())
            {
                foreach (string pattern in arg[4..].Split(','))
                {
                    filters++;
                    // doctest's wildcard: '*' and '?', and the case of the letters does not count.
                    var wildcard = new Regex("^" + Regex.Escape(pattern).Replace(@"\*", ".*").Replace(@"\?", ".") + "$", RegexOptions.IgnoreCase);
                    if (!names.Any(wildcard.IsMatch)) problems.Add($"{Path.GetFileName(list)}: -tc={pattern}");
                }
            }
        }

        Assert.True(filters >= 10, $"only {filters} filters were read");
        Assert.True(problems.Count == 0, "matches no test case: " + string.Join(" | ", problems));
    }

    private static List<string> NativeTestCaseNames()
    {
        var names = new List<string>();
        foreach (string file in Directory.GetFiles(Path.Combine(RepoPaths.Root, "tests", "core"), "*.cpp"))
        {
            foreach (string line in File.ReadAllLines(file))
            {
                Match m = Regex.Match(line, @"^\s*TEST_CASE(?:_TEMPLATE)?\(""([^""]*)""");
                if (m.Success) names.Add(m.Groups[1].Value);
            }
        }

        return names;
    }

    [Theory]
    [InlineData("abcabc", "bc", 2)]
    [InlineData("aaaa", "aa", 3)]          // overlapping too: "exactly once" must not miss a second place
    [InlineData("abc", "x", 0)]
    public void OccurrencesAreCountedWhereverTheyStart(string text, string part, int expected) =>
        Assert.Equal(expected, Occurrences(text, part));

    private static int Occurrences(string text, string part)
    {
        int count = 0;
        for (int at = text.IndexOf(part, StringComparison.Ordinal); at >= 0; at = text.IndexOf(part, at + 1, StringComparison.Ordinal))
        {
            count++;
        }
        return count;
    }
}
