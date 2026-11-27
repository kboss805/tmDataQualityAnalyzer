<#
.SYNOPSIS
    Names the kept mutant lists a change touches, so the mutation check runs those and not all 61.

.DESCRIPTION
    A kept list guards the code in the files its mutants change. A change that touches none of those files cannot
    move that list's verdicts through the code (it can through a test: see below), so a change runs the lists its
    own files reach, and a release runs every list. 708 mutants cost most of a day; the lists a typical change
    reaches cost minutes.

    The files are the change's: what differs between -Base (default main) and the working tree, committed or not,
    plus untracked files. A list is affected when
      * one of its mutants' files changed, or
      * the list itself changed, or
      * a test file changed that the list's suite holds (tests\core for native, tests\TmDq.Core.Tests for managed,
        tests\TmDq.App.Tests for app) and the list names that file's class in its arguments: a changed test can
        stop catching a mutant, so the lists that run its class are affected too.
    A change to a shared file the lists cannot see (a .props, a script the suites run) is not found: when in doubt,
    run every list (-All).

    Prints one line a list: its path, its suite and its mutant count. With -Run it then runs each list with
    eng\mutate.ps1 in turn, in this worktree (-Parts and -Part pass through, to share a run between worktrees), and
    exits with the first non-zero code.

    Works under Windows PowerShell 5.1 and PowerShell 7.

    Usage, from a worktree:
        powershell -ExecutionPolicy Bypass -File eng\mutants_affected.ps1 [-Base main] [-All] [-Run]
            [-Parts <n> -Part <k>]
#>
param(
    [string]$Base = 'main',
    [switch]$All,
    [switch]$Run,
    [int]$Parts = 1,
    [int]$Part = 0
)
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$listDir = Join-Path $root 'eng\mutants'

# ---- what changed ------------------------------------------------------------------------------------------
$changed = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
if (-not $All) {
    $committed = & git -C $root diff --name-only $Base 2>$null
    if ($LASTEXITCODE -ne 0) { throw "git diff against '$Base' failed: is it a branch or commit here?" }
    $untracked = & git -C $root ls-files --others --exclude-standard
    foreach ($f in @($committed) + @($untracked)) {
        if ($f) { [void]$changed.Add(($f -replace '\\', '/')) }
    }
}

$testRoots = @{
    native  = 'tests/core/'
    managed = 'tests/TmDq.Core.Tests/'
    app     = 'tests/TmDq.App.Tests/'
}

# ---- the lists ---------------------------------------------------------------------------------------------
$hits = @()
foreach ($file in Get-ChildItem $listDir -Filter '*.json' | Sort-Object Name) {
    $spec = Get-Content $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $spec.mutants) { continue }   # example.json and any other list without mutants
    $suite = [string]$spec.suite
    $rel = "eng/mutants/$($file.Name)"
    $why = $null

    if ($All) { $why = 'all' }
    elseif ($changed.Contains($rel)) { $why = 'the list itself' }
    else {
        foreach ($m in $spec.mutants) {
            if ($changed.Contains(([string]$m.file -replace '\\', '/'))) { $why = [string]$m.file; break }
        }
    }
    if (-not $why -and $testRoots.ContainsKey($suite)) {
        # The classes the list runs, from every "-class X" in its arguments (the list's and each mutant's).
        $classes = @()
        $argSets = @(, $spec.args) + @($spec.mutants | ForEach-Object { , $_.args })
        foreach ($a in $argSets) {
            $items = @($a)
            for ($i = 0; $i -lt $items.Count - 1; $i++) {
                if ($items[$i] -eq '-class') { $classes += ([string]$items[$i + 1] -split '\.')[-1] }
            }
        }
        $classes = @($classes | Select-Object -Unique)
        foreach ($f in $changed) {
            if ($f.StartsWith($testRoots[$suite], [StringComparison]::OrdinalIgnoreCase) -and $f.EndsWith('.cs')) {
                $stem = [IO.Path]::GetFileNameWithoutExtension($f)
                if ($classes -contains $stem) { $why = $f; break }
            }
        }
    }
    if ($why) { $hits += [pscustomobject]@{ Path = $file.FullName; Rel = $rel; Suite = $suite; Count = @($spec.mutants).Count; Why = $why } }
}

if ($hits.Count -eq 0) {
    Write-Host "No kept list is affected by the change against $Base."
    exit 0
}

$total = ($hits | Measure-Object -Property Count -Sum).Sum
Write-Host "$($hits.Count) kept list(s), $total mutant(s), reached by the change against $Base ($(@($changed).Count) file(s)):"
foreach ($h in $hits) { Write-Host ('  {0,-48} {1,-8} {2,4}  {3}' -f $h.Rel, $h.Suite, $h.Count, $h.Why) }

if (-not $Run) {
    Write-Host 'Run them with -Run, or one at a time with eng\mutate.ps1 -List <list>.'
    exit 0
}

# ---- run them ----------------------------------------------------------------------------------------------
$worst = 0
foreach ($h in $hits) {
    Write-Host ''
    Write-Host "=== $($h.Rel) ===" -ForegroundColor Cyan
    $mutateArgs = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'mutate.ps1'), '-List', $h.Path, '-Parts', $Parts, '-Part', $Part)
    & powershell @mutateArgs
    if ($LASTEXITCODE -ne 0 -and $worst -eq 0) { $worst = $LASTEXITCODE }
}
exit $worst
