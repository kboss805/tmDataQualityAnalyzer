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
        // string decodes to a curly quote that ends it (audit B7: a script that could not even be parsed); the workflow's
        // temp scripts read the same way. The files are plain ASCII, and stay so.
        var offenders = new List<string>();
        foreach (string folder in new[] { "eng", "deploy", ".github" })
        {
            foreach (string file in Files(folder, "*.ps1", "*.psm1", "*.yml", "*.yaml", "*.bat", "*.cmd"))
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
        Assert.NotEmpty(Files("deploy", "*.wxs"));
    }

    [Fact]
    public void TheMsiSourceAndThePackageScriptsFindWhatTheyNameFromWhereTheyAre()
    {
        // The MSI source names files by $(RepoRoot)\... and nothing but building the package (a release-time step) shows
        // one that no longer resolves: moving deploy/avalonia/ up a level (audit F1) left the installer's release-notes path
        // pointing at a folder above the repository until a compile said so.
        string deploy = Path.Combine(RepoPaths.Root, "deploy");
        string wxs = File.ReadAllText(Path.Combine(deploy, "tmDataQualityAnalyzer.wxs"));
        MatchCollection named = Regex.Matches(wxs, @"\$\(RepoRoot\)\\([^""]+)""");
        Assert.True(named.Count >= 2, "the MSI source names no file under $(RepoRoot): has its layout changed?");
        foreach (Match m in named)
        {
            string path = Path.GetFullPath(Path.Combine(RepoPaths.Root, m.Groups[1].Value));
            Assert.True(File.Exists(path), $"$(RepoRoot)\\{m.Groups[1].Value} resolves to {path}, which does not exist");
        }

        // Every variable the source uses is one the script defines on the wix command line (-d "Name=..."): an unknown one
        // is an error only when the package is built.
        string msiScript = File.ReadAllText(Path.Combine(deploy, "build_msi.ps1"));
        var defined = Regex.Matches(msiScript, @"-d ""(\w+)=").Select(m => m.Groups[1].Value).ToHashSet();
        var used = Regex.Matches(wxs, @"\$\((\w+)\)").Select(m => m.Groups[1].Value).ToHashSet();
        Assert.True(used.Count >= 4, "the MSI source uses fewer variables than it did: has its layout changed?");
        Assert.True(used.IsSubsetOf(defined), "the MSI source uses $(" + string.Join("), $(", used.Except(defined)) + "), which build_msi.ps1 does not define");

        // The package scripts' root is the folder holding the solution, found from their own: each must walk up exactly as
        // far as it sits below it.
        foreach (string script in new[] { "build_package.ps1", "build_msi.ps1" })
        {
            string text = File.ReadAllText(Path.Combine(deploy, script));
            Match root = Regex.Match(text, @"\$Root = \(Resolve-Path \(Join-Path \$Here '([^']*)'\)\)\.Path");
            Assert.True(root.Success, $"{script} does not set $Root from $Here");
            Assert.True(File.Exists(Path.GetFullPath(Path.Combine(deploy, root.Groups[1].Value, "tmdq.sln"))),
                        $"{script}'s $Root ('{root.Groups[1].Value}' from deploy) is not the folder with tmdq.sln");
        }
    }

    [Fact]
    public void TheMsiOffersTheApplicationForCh10AndNeverWritesTheDefaultAnotherApplicationMayHold()
    {
        // Windows Installer removes every registry value a package wrote when it is uninstalled (and when the upgrade removes
        // the older product), without looking at whose the value has become. A ProgId with an Extension writes .ch10's DEFAULT
        // value, so an association another application took after ours was installed was deleted by our uninstall (the Sandbox
        // test, step 8b). The MSI lists its ProgID under .ch10\OpenWithProgids instead: a value named for our ProgID, which an
        // uninstall can only remove from us. A ProgId/Extension (or a write of .ch10's default) would bring that back.
        string wxs = File.ReadAllText(Path.Combine(RepoPaths.Root, "deploy", "tmDataQualityAnalyzer.wxs"));
        Assert.DoesNotContain("<Extension", wxs, StringComparison.Ordinal);
        Assert.DoesNotContain("<ProgId", wxs, StringComparison.Ordinal);
        Assert.False(Regex.IsMatch(wxs, @"Key=""Software\\Classes\\\.ch10""\s"), "the MSI writes a value of the .ch10 key itself (its default would be an association taken from another application)");
        Assert.Contains(@"Key=""Software\Classes\.ch10\OpenWithProgids"" Name=""tmDataQualityAnalyzer.ch10""", wxs, StringComparison.Ordinal);
        Assert.Contains(@"Key=""Software\Classes\tmDataQualityAnalyzer.ch10\shell\open\command""", wxs, StringComparison.Ordinal);
    }

    [Fact]
    public void TheMsiInstallsPerUserOrPerMachineAndEveryRegistryWriteFollowsTheInstall()
    {
        // US8.0: a user without admin rights installs, to a folder of their choosing. The package installs per-user or
        // per-machine (the wizard asks "for all users, or just for me"), and its registry writes are HKMU, which is HKLM for
        // an install for all users and HKCU for one user. A write to HKLM would make a per-user install by a standard user fail,
        // and the per-machine folder the dialog set builds from [ProgramFilesFolder] would be the 32-bit Program Files.
        string deploy = Path.Combine(RepoPaths.Root, "deploy");
        string wxs = File.ReadAllText(Path.Combine(deploy, "tmDataQualityAnalyzer.wxs"));
        Assert.Contains("Scope=\"perUserOrMachine\"", wxs, StringComparison.Ordinal);
        Assert.Contains("<ui:WixUI Id=\"WixUI_Advanced\" />", wxs, StringComparison.Ordinal);
        Assert.DoesNotMatch(@"<RegistryValue Root=""HK(LM|CU|CR)""", wxs);
        Assert.True(Regex.Count(wxs, @"<RegistryValue Root=""HKMU""") >= 5, "the MSI writes fewer HKMU values than it did: has its layout changed?");
        Assert.Contains("Value=\"[ProgramFiles64Folder][ApplicationFolderName]\"", wxs, StringComparison.Ordinal);
        Assert.Contains("Value=\"[LocalAppDataFolder]Programs\\[ApplicationFolderName]\"", wxs, StringComparison.Ordinal);

        // WiX writes MSIINSTALLPERUSER=1 for such a package, which makes a silent install from an elevated deployment tool
        // (Group Policy, SCCM, Intune: SYSTEM) per-user, into SYSTEM's profile. The build script takes it out, and checks it.
        string script = File.ReadAllText(Path.Combine(deploy, "build_msi.ps1"));
        Assert.Contains("DELETE FROM `Property` WHERE `Property`=''MSIINSTALLPERUSER''", script, StringComparison.Ordinal);
    }

    [Fact]
    public void TheWizardsLaunchStartsTheExeFromTheInstallFolderPropertyNotItsFileRow()
    {
        // The Finish page's "Launch" runs in the wizard, which resolves a file's path when it starts (the per-machine folder);
        // "only for me" then changes the APPLICATIONFOLDER property but not that resolved path. A launch by the exe's File row
        // (FileRef, or [#AppExe] in the wizard) after a per-user install looked for the exe in Program Files and failed with nothing
        // shown (return value 1631, found by the user in a v3.5.0 candidate). The path is built from the property at Finish instead.
        // (The shortcuts' [#AppExe] is resolved by the install itself, after the choice, and is right.)
        string wxs = File.ReadAllText(Path.Combine(RepoPaths.Root, "deploy", "tmDataQualityAnalyzer.wxs"));
        Assert.DoesNotMatch(@"<CustomAction Id=""LaunchApplication""[^>]*FileRef=", wxs);
        Assert.Contains(@"<CustomAction Id=""SetLaunchApplicationExe"" Property=""LAUNCHAPPLICATIONEXE"" Value=""[APPLICATIONFOLDER]\tmDataQualityAnalyzer.exe"" />", wxs, StringComparison.Ordinal);
        Assert.Contains(@"<CustomAction Id=""LaunchApplication"" Property=""LAUNCHAPPLICATIONEXE""", wxs, StringComparison.Ordinal);
        // The path is set before the launch reads it: both on the Finish button, in that order.
        Assert.Matches(@"Value=""SetLaunchApplicationExe""[^>]*Order=""1""", wxs);
        Assert.Matches(@"Value=""LaunchApplication""[^>]*Order=""2""", wxs);
    }

    [Theory]
    [InlineData("WixUIDialogBmp", "installer_dialog.bmp", 493, 312)]
    [InlineData("WixUIBannerBmp", "installer_banner.bmp", 493, 58)]
    public void TheWizardsPicturesAreOursAtTheSizesItDraws(string variable, string file, int width, int height)
    {
        // The MSI wizard's Welcome/Finish picture and its banner are drawn from the icon's artwork (eng\gen_installer_images.py).
        // Windows Installer stretches a bitmap of another size to the control, so a wrong size is a distorted picture, not an
        // error; and an image it cannot read is the stock WiX picture again. Both are 24-bit, bottom-up BMPs of the control's size.
        string wxs = File.ReadAllText(Path.Combine(RepoPaths.Root, "deploy", "tmDataQualityAnalyzer.wxs"));
        Assert.Contains($@"<WixVariable Id=""{variable}"" Value=""$(RepoRoot)\assets\{file}"" />", wxs, StringComparison.Ordinal);
        byte[] bmp = File.ReadAllBytes(Path.Combine(RepoPaths.Root, "assets", file));
        Assert.Equal((byte)'B', bmp[0]);
        Assert.Equal((byte)'M', bmp[1]);
        Assert.Equal(width, BitConverter.ToInt32(bmp, 18));
        Assert.Equal(height, BitConverter.ToInt32(bmp, 22));
        Assert.Equal(24, BitConverter.ToInt16(bmp, 28));
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

    /// <summary>
    /// <c>eng\ci_local.ps1</c> runs here what the workflow runs, so a push can be checked on this machine before the hosted run.
    /// It is only as good as its list: a step added to ci.yml and not to the script would pass here and fail there. The two
    /// lists, script by script and configuration by configuration, in order, are the same.
    /// </summary>
    [Fact]
    public void TheLocalCiRunsTheStepsTheWorkflowRunsInTheSameOrder()
    {
        string workflow = File.ReadAllText(RepoPaths.File(Path.Combine(".github", "workflows", "ci.yml")));
        string local = File.ReadAllText(RepoPaths.File(Path.Combine("eng", "ci_local.ps1")));

        // The workflow builds the core in a loop over its configurations; a step that says $cfg runs once for each.
        Match loop = Regex.Match(workflow, @"foreach \(\$cfg in ((?:'\w+'(?:, )?)+)\)");
        Assert.True(loop.Success, "ci.yml no longer loops over the core's configurations the way this test reads it");
        string[] configurations = Regex.Matches(loop.Groups[1].Value, @"'(\w+)'").Select(m => m.Groups[1].Value).ToArray();

        var inWorkflow = new List<string>();
        foreach (Match m in Regex.Matches(workflow, @"-File eng\\(\w+)\.ps1(?: -Configuration (\$cfg|\w+))?"))
        {
            string step = m.Groups[1].Value;
            string configuration = m.Groups[2].Value;
            if (configuration == "$cfg") inWorkflow.AddRange(configurations.Select(c => step + " -Configuration " + c));
            else inWorkflow.Add(configuration.Length == 0 ? step : step + " -Configuration " + configuration);
        }

        var inLocal = Regex.Matches(local, @"(?m)^Invoke-CiStep '[^']+' 'eng\\(\w+)\.ps1'(?: '-Configuration (\w+)')?")
                           .Select(m => m.Groups[2].Length == 0 ? m.Groups[1].Value : m.Groups[1].Value + " -Configuration " + m.Groups[2].Value)
                           .ToList();

        Assert.True(inWorkflow.Count >= 6, "read only " + inWorkflow.Count + " steps from ci.yml: has its command shape changed?");
        Assert.Equal(inWorkflow, inLocal);
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
    public void TheDocumentationLinksToFilesThatAreThereAndKeepsItsFencesAndHeadingsInOrder()
    {
        // A markdown lint of the rules that are objectively right (a link that goes nowhere, a fence left open so the rest
        // of the page is code, a tab, a heading that skips a level), held here so CI needs no second toolchain (audit T11).
        // The changelog and docs\records are kept as they were written, and their links and layout are of their day.
        var problems = new List<string>();
        string[] files = DocumentationFiles().ToArray();
        Assert.Contains(files, f => f.EndsWith("guide.md", StringComparison.Ordinal));
        Assert.Contains(files, f => f.EndsWith("README.md", StringComparison.Ordinal));
        foreach (string file in files)
        {
            string name = Path.GetRelativePath(RepoPaths.Root, file);
            string[] lines = File.ReadAllLines(file);
            bool inFence = false;
            int fences = 0, h1 = 0, previous = 0;
            for (int i = 0; i < lines.Length; i++)
            {
                string line = lines[i];
                if (Regex.IsMatch(line, @"^\s*(```|~~~)"))
                {
                    inFence = !inFence;
                    fences++;
                    continue;
                }
                if (inFence) continue;
                if (line.Contains('\t')) problems.Add($"{name}:{i + 1}: a tab");
                Match heading = Regex.Match(line, @"^(#{1,6})\s");
                if (heading.Success)
                {
                    int level = heading.Groups[1].Length;
                    if (level == 1) h1++;
                    if (previous > 0 && level > previous + 1) problems.Add($"{name}:{i + 1}: a heading jumps from level {previous} to {level}");
                    previous = level;
                }
                foreach (Match link in Regex.Matches(line, @"\]\(([^)\s]+)\)"))
                {
                    string target = link.Groups[1].Value;
                    if (Regex.IsMatch(target, @"^(https?:|mailto:|#)")) continue;
                    string path = Uri.UnescapeDataString(target.Split('#')[0]);
                    if (path.Length == 0) continue;
                    string full = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(file)!, path));
                    if (!File.Exists(full) && !Directory.Exists(full)) problems.Add($"{name}:{i + 1}: the link {target} goes to nothing");
                }
            }
            if (fences % 2 != 0) problems.Add($"{name}: a code fence is never closed");
            if (h1 > 1) problems.Add($"{name}: {h1} top-level headings");
        }
        Assert.True(problems.Count == 0, string.Join(Environment.NewLine, problems));
    }

    private static IEnumerable<string> DocumentationFiles()
    {
        yield return RepoPaths.File("README.md");
        yield return RepoPaths.File("SECURITY.md");
        foreach (string root in new[] { "docs", ".claude" })
        {
            foreach (string file in Directory.EnumerateFiles(Path.Combine(RepoPaths.Root, root), "*.md", SearchOption.AllDirectories))
            {
                string relative = Path.GetRelativePath(RepoPaths.Root, file).Replace('\\', '/');
                if (relative == "docs/changelog.md" || relative.StartsWith("docs/records/", StringComparison.Ordinal)) continue;
                if (relative.StartsWith(".claude/worktrees/", StringComparison.Ordinal)) continue;
                yield return file;
            }
        }
    }

    [Fact]
    public void NoScriptButCommonPs1TypesTheTargetFrameworkOrTheImportAllowList()
    {
        // The target framework names the folder a test exe is in, and the import allow-list is what makes a new dependency of
        // the DLL a decision: each lives in eng\common.ps1 (audit A5), where a change reaches every script that reads it.
        string[] literals = { "net10.0", "KERNEL32|WS2_32" };
        var offenders = new List<string>();
        foreach (string folder in new[] { "eng", "deploy" })
        {
            foreach (string file in Files(folder, "*.ps1"))
            {
                if (Path.GetFileName(file) == "common.ps1") continue;
                string text = File.ReadAllText(file);
                foreach (string literal in literals)
                {
                    if (text.Contains(literal, StringComparison.Ordinal)) offenders.Add($"{Path.GetRelativePath(RepoPaths.Root, file)} has {literal}");
                }
            }
        }
        Assert.True(offenders.Count == 0, string.Join(Environment.NewLine, offenders));
        string common = File.ReadAllText(RepoPaths.File(Path.Combine("eng", "common.ps1")));
        Assert.Contains("net10.0", common);
        Assert.Contains("KERNEL32|WS2_32", common);
    }

    [Fact]
    public void EveryKeptMutantStillNamesTextThatIsInItsFileExactlyOnce()
    {
        // A kept list (eng\mutants) is a regression check only while each mutant's "old" text is still in its file: after
        // a rename or a re-indent eng\mutate.ps1 answers NO-MATCH and checks nothing, and nobody is told. 28 of 155 had
        // gone that way (the follow-up audit of 2026-11-16), most of them in one re-indent. A change that moves the text
        // a mutant names now fails here, and the list is re-anchored in the same change.
        // eng\mutate.ps1 sets TMDQ_MUTATING while a mutant is applied: the mutant has taken its own text out, so this would
        // fail for every managed mutant and "catch" it whatever the real tests said.
        Assert.SkipWhen(Environment.GetEnvironmentVariable("TMDQ_MUTATING") == "1", @"a mutant is applied (eng\mutate.ps1)");
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
