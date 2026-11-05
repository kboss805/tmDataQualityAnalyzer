<#
.SYNOPSIS
    Runs a list of single-defect mutants against one test suite, in a git worktree, and reports which ones
    the tests catch.

.DESCRIPTION
    Each mutant replaces one exact piece of text in one file. For each mutant the script applies the
    replacement, builds, runs the suite, and puts the file back byte for byte. A test that cannot fail
    proves nothing; this is how every step of the move to Avalonia from plan step 2.4 on checked its
    tests, and how every behaviour change is checked now.

    The verdicts:
        CAUGHT     a test failed, the run crashed, or it hung past the time limit
        SURVIVED   the suite passed with the defect in place: either a gap in the tests or an equivalent
                   mutant, and you decide which
        COMPILE    the compiler refused the mutant. That is not a survivor. Rewrite it so that it compiles,
                   unless the compiler itself is what holds the rule (nullable analysis, for example)
        NO-MATCH   the old text is not in the file exactly once, so nothing ran
        NO-TESTS   the suite passed, but its arguments matched no test, so nothing was checked
        STALE      the build left a binary older than the mutated file, so nothing ran
        LOCKED     a file lock, or a build tool failing (mt.exe: MSB3073), outlasted the retries, so
                   nothing ran

    What makes a result worth trusting, each one a trap that has bitten a past run:
      * It runs only in a linked git worktree (git worktree add C:\tmp\<name> <branch>). In the main
        checkout an IDE's design-time build shares obj and bin with the loop and breaks half of it.
      * Before the first mutant the unmutated tree must build, with zero compiler warnings for the
        native suite, and pass in full.
      * Every file in the list is kept as bytes before anything changes. After each mutant the file is
        written back and compared, and the run stops if it cannot be restored.
      * The test executable is deleted before each build, and whatever the mutated file compiles into
        (the test exe, tmdq_core.dll, or the project's assembly) must be newer than the mutated file. A
        relink that did not happen once showed two mutants with the previous mutant's failures.
      * An old text that matches nothing would run unmutated and "survive", so it must match exactly
        once.
      * Old and new text take the file's own line endings, so a list does not depend on how git checked
        the file out.

    The list is JSON:
        {
          "suite": "native" | "managed" | "app",
          "args": [ "-tc=TomlDocument*" ],        optional: the test exe's arguments
          "env": { "NAME": "value" },             optional: variables for the test exe
          "timeoutSeconds": 420,                  optional, default 420: a run past it is a hang
          "mutants": [
            { "id": "T1",
              "file": "src/model/tomldocument.cpp",   relative to the worktree's root
              "old": "exact text, once in the file",
              "new": "its replacement",
              "args": [ ... ],                    optional: replaces the list's args for this mutant
              "env": { ... },                     optional: added to the list's env for this mutant
              "expect": "caught",                 optional: caught, survived or compile
              "note": "why" }                     optional, for the reader
          ]
        }

    The suites:
        native    native\tmdq_core_tests (doctest). Run with a filter while iterating: "args": ["-tc=Abi*"]
        managed   dotnet\TmDq.Core.Tests (xunit v3), against the worktree's tmdq_core.dll
        app       dotnet\TmDq.App.Tests (xunit v3), against the worktree's tmdq_core.dll.
                  "args": ["-stopOnFail"] stops at the first failure, which is all a verdict needs.
                  Run the soak's explicit cases with "args": ["-explicit", "only", "-class",
                  "TmDq.App.Tests.SoakTests"] and each mutant's runs in "env": {"TMDQ_SOAK_ONLY": "..."}.
    For managed and app the DLL is rebuilt first, so a mutant in the native core reaches them too.

    A mutant's "expect" records a decision. "survived" marks an equivalent mutant once it has been
    reasoned through, and "compile" marks a rule that the compiler holds. A rerun then reports any
    mutant whose verdict no longer matches, which makes a kept list a regression check on the tests.

    A long list can run in two worktrees at once: -Parts 2 -Part 0 in one, -Parts 2 -Part 1 in the other.
    After you close a gap, rerun only what survived with -Only S3,S7. With -NoBaseline the green
    baseline is skipped. Use it only right after a full run in the same worktree with no change since.

    Exit code: 0 when every mutant has a verdict and every expectation is met, 1 when some mutant has no
    verdict, 2 for a bad list or a bad invocation, 3 when the baseline is not green, 4 when a file
    could not be restored, 5 when an expectation was not met.

    Works under Windows PowerShell 5.1 and PowerShell 7.

    Usage, from the worktree:
        powershell -ExecutionPolicy Bypass -File scripts\mutate.ps1 -List <mutants.json>
            [-Only <id>,...] [-Parts <n> -Part <k>] [-Out <results.txt>] [-NoBaseline]
    scripts\mutants\example.json is a small native list that reaches each verdict the harness gives
    when a mutant runs.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$List,
    [string[]]$Only = @(),
    [int]$Parts = 1,
    [int]$Part = 0,
    [string]$Out = '',
    [switch]$NoBaseline
)
$ErrorActionPreference = 'Continue'   # 5.1 turns a native tool's stderr into a terminating error under Stop

function Fail([int]$code, [string]$message) { Write-Host $message -ForegroundColor Red; exit $code }

# ---- where we are -------------------------------------------------------------------------------------------
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$gitDir = (& git -C $root rev-parse --absolute-git-dir 2>$null)
$commonDir = (& git -C $root rev-parse --git-common-dir 2>$null)
if (-not $gitDir -or -not $commonDir) { Fail 2 "$root is not inside a git repository" }
if (-not [IO.Path]::IsPathRooted($commonDir)) { $commonDir = Join-Path $root $commonDir }
$gitDir = [IO.Path]::GetFullPath($gitDir).TrimEnd('\')
$commonDir = [IO.Path]::GetFullPath($commonDir).TrimEnd('\')
if ($gitDir -eq $commonDir) {
    Fail 2 ("$root is the main checkout. Run the loop in a worktree, for example:`n" +
            "    git worktree add C:\tmp\mut <branch>`n" +
            "    powershell -ExecutionPolicy Bypass -File C:\tmp\mut\scripts\mutate.ps1 -List <list>")
}
if ($Parts -lt 1 -or $Part -lt 0 -or $Part -ge $Parts) { Fail 2 "-Part must be from 0 to $($Parts - 1)" }

# ---- the list -----------------------------------------------------------------------------------------------
if (-not (Test-Path $List)) { Fail 2 "no list at $List" }
$listPath = (Resolve-Path $List).Path
try { $spec = Get-Content $listPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { Fail 2 "$listPath is not JSON: $($_.Exception.Message)" }
$suite = [string]$spec.suite
if (@('native', 'managed', 'app') -notcontains $suite) { Fail 2 "suite must be native, managed or app (the list says '$suite')" }
$timeout = 420
if ($spec.timeoutSeconds) { $timeout = [int]$spec.timeoutSeconds }

function To-Array($value) { if ($null -eq $value) { return @() } return @($value | ForEach-Object { [string]$_ }) }
function To-Map($value) {
    $map = [ordered]@{}
    if ($null -ne $value) { foreach ($p in $value.PSObject.Properties) { $map[$p.Name] = [string]$p.Value } }
    return $map
}
$listArgs = To-Array $spec.args
$listEnv = To-Map $spec.env

$mutants = @($spec.mutants)
if ($mutants.Count -eq 0) { Fail 2 "$listPath has no mutants" }
$seen = @{}
foreach ($m in $mutants) {
    foreach ($field in 'id', 'file', 'old', 'new') {
        if ($null -eq $m.$field) { Fail 2 "a mutant has no '$field' ($(ConvertTo-Json $m -Compress))" }
    }
    if ($seen.ContainsKey($m.id)) { Fail 2 "two mutants are called $($m.id)" }
    $seen[$m.id] = $true
    if ($m.expect -and @('caught', 'survived', 'compile') -notcontains $m.expect) { Fail 2 "$($m.id): expect must be caught, survived or compile" }
    if ($m.old -eq $m.new) { Fail 2 "$($m.id): old and new are the same" }
    if (-not (Test-Path (Join-Path $root $m.file))) { Fail 2 "$($m.id): no file $($m.file)" }
}
$Only = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($id in $Only) { if (-not $seen.ContainsKey($id)) { Fail 2 "-Only names $id, which the list does not have" } }
if ($Only.Count -gt 0) { $mutants = @($mutants | Where-Object { $Only -contains $_.id }) }
if ($Parts -gt 1) { $i = 0; $mutants = @($mutants | Where-Object { ($i++ % $Parts) -eq $Part }) }

# ---- output -------------------------------------------------------------------------------------------------
if ($Out) {
    if (-not [IO.Path]::IsPathRooted($Out)) { $Out = Join-Path (Get-Location).Path $Out }
    $Out = [IO.Path]::GetFullPath($Out)
    New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
    Set-Content -Path $Out -Value @() -Encoding UTF8
}
function Say([string]$line) {
    Write-Host $line
    if ($Out) { Add-Content -Path $Out -Value $line -Encoding UTF8 }
}

# ---- the tools ----------------------------------------------------------------------------------------------
if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) { . (Join-Path $root 'scripts\env.ps1') *> $null }
if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) { Fail 2 'msbuild not found (install the Visual Studio 2022 C++ build tools)' }
# vcvars sets Platform=x64, which sends a dotnet build's output to bin\x64\. The native builds pass it explicitly.
[Environment]::SetEnvironmentVariable('Platform', $null)

$outDir = Join-Path $root 'native\out\Release'
$coreDll = Join-Path $outDir 'tmdq_core.dll'
switch ($suite) {
    'native'  { $testProject = Join-Path $root 'native\tmdq_core_tests\tmdq_core_tests.vcxproj'; $testExe = Join-Path $outDir 'tmdq_core_tests.exe' }
    'managed' { $testProject = Join-Path $root 'dotnet\TmDq.Core.Tests'; $testExe = Join-Path $testProject 'bin\Release\net10.0\TmDq.Core.Tests.exe' }
    'app'     { $testProject = Join-Path $root 'dotnet\TmDq.App.Tests'; $testExe = Join-Path $testProject 'bin\Release\net10.0\TmDq.App.Tests.exe' }
}
# MSB3073 is a build step's tool failing (mt.exe, merging the manifest: c101008d), which is not the compiler's verdict.
$lockPattern = 'LNK1104|LNK1168|LNK1201|C1041|C1090|MSB3021|MSB3026|MSB3027|MSB3073|c101008d|CS2012|being used by another process'

# Builds one thing, retrying while the failure is a file lock (antivirus, an exiting test process).
function Invoke-Build([string]$what, [scriptblock]$command) {
    $log = ''
    foreach ($attempt in 1..4) {
        $log = & $command 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) { return @{ Ok = $true; Log = $log } }
        if ($log -notmatch $lockPattern) { break }
        Get-ChildItem $outDir -Filter '*.pdb' -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'tmdq_core_tests*' } |
            Remove-Item -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds (3 * $attempt)
    }
    $first = ($log -split "`r?`n" | Where-Object { $_ -match 'error [A-Z]+\d+|error :' } | Select-Object -First 1)
    if (-not $first) { $first = ($log -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -Last 1) }
    $first = "$first".Trim() -replace '\s+', ' '
    if ($first.Length -gt 160) { $first = $first.Substring(0, 160) }
    return @{ Ok = $false; Locked = ($log -match $lockPattern); Detail = "$what`: $first"; Log = $log }
}

function Build-Suite {
    if (Test-Path $testExe) { [IO.File]::Delete($testExe) }   # a stale exe must never pass for a fresh build
    $warnings = 0
    if ($suite -eq 'native') {
        $b = Invoke-Build 'native tests' { & msbuild $testProject /t:Build /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 }
        if (-not $b.Ok) { return $b }
        $warnings = ([regex]::Matches($b.Log, 'warning [A-Z]+\d+')).Count
    } else {
        $dll = Invoke-Build 'tmdq_core.dll' { & msbuild (Join-Path $root 'native\tmdq_core\tmdq_core.vcxproj') /t:Build /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 }
        if (-not $dll.Ok) { return $dll }
        $warnings = ([regex]::Matches($dll.Log, 'warning [A-Z]+\d+')).Count
        $b = Invoke-Build (Split-Path $testProject -Leaf) { & dotnet build $testProject -c Release -nologo -v q }
        if (-not $b.Ok) { return $b }
    }
    if (-not (Test-Path $testExe)) { return @{ Ok = $false; Locked = $false; Detail = "the build reported success but wrote no $testExe" } }
    return @{ Ok = $true; Warnings = $warnings }
}

# What a mutated file compiles into, which must be newer than the file once the build is done.
function Get-Product([string]$file) {
    $full = Join-Path $root $file
    if ($file -match '^dotnet[\\/]') {
        $dir = Split-Path $full
        while ($dir.Length -gt $root.Length) {
            $proj = @(Get-ChildItem $dir -Filter '*.csproj' -ErrorAction SilentlyContinue)
            if ($proj.Count -gt 0) {
                if ($proj[0].FullName -eq (Join-Path $testProject ((Split-Path $testProject -Leaf) + '.csproj'))) { return $testExe }
                $name = [regex]::Match((Get-Content $proj[0].FullName -Raw), '<AssemblyName>\s*([^<]+?)\s*</AssemblyName>').Groups[1].Value
                if (-not $name) { $name = $proj[0].BaseName }   # the app's assembly is tmDataQualityAnalyzer
                return Join-Path (Split-Path $testExe) ($name + '.dll')
            }
            $dir = Split-Path $dir
        }
        return $testExe
    }
    if ($suite -eq 'native') { return $testExe }
    return $coreDll
}

function Quote([string]$arg) {
    if ($arg -notmatch '[\s"]') { return $arg }
    return '"' + ($arg -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

function Run-Suite([string[]]$arguments, $variables) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $testExe
    $all = @($arguments)
    if ($suite -ne 'native') { $all = @('-noColor') + $all }
    $psi.Arguments = (($all | ForEach-Object { Quote $_ }) -join ' ')
    $psi.WorkingDirectory = $root
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    if ($suite -ne 'native') { $psi.EnvironmentVariables['TMDQ_CORE_DLL'] = $coreDll }
    foreach ($k in $variables.Keys) { $psi.EnvironmentVariables[$k] = $variables[$k] }
    $p = [System.Diagnostics.Process]::Start($psi)
    $so = $p.StandardOutput.ReadToEndAsync(); $se = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($timeout * 1000)) {
        & taskkill /T /F /PID $p.Id 2>&1 | Out-Null
        return @{ Hung = $true; Exit = -1; Failed = @(); Summary = "no result in $timeout s" }
    }
    $p.WaitForExit()
    $text = $so.Result + "`n" + $se.Result
    if ($suite -eq 'native') {
        $failed = @([regex]::Matches($text, '(?m)^TEST CASE:\s+(.*?)\s*$') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
        $summary = [regex]::Match($text, 'test cases:\s*\d+\s*\|[^\r\n]*').Value
    } else {
        $failed = @([regex]::Matches($text, '(?m)^\s*(\S.*?)\s*\[FAIL\]\s*$') | ForEach-Object { $_.Groups[1].Value -replace '^TmDq\.(App|Core)\.Tests\.', '' } | Select-Object -Unique)
        $summary = ([regex]::Matches($text, 'Total:[^\r\n]*') | Select-Object -Last 1).Value
    }
    # How many tests ran: a filter that matches none passes, and every mutant would "survive" it.
    $ran = 0
    $count = [regex]::Match($summary, '(test cases|Total):\s*(\d+)')
    if ($count.Success) { $ran = [int]$count.Groups[2].Value }
    return @{ Hung = $false; Exit = $p.ExitCode; Failed = $failed; Summary = "$summary".Trim(); Ran = $ran }
}

# ---- the pristine bytes -------------------------------------------------------------------------------------
$utf8 = New-Object System.Text.UTF8Encoding($false)
$pristine = @{}
foreach ($f in @($mutants | ForEach-Object { $_.file } | Sort-Object -Unique)) { $pristine[$f] = [IO.File]::ReadAllBytes((Join-Path $root $f)) }
function Restore([string]$file) {
    $path = Join-Path $root $file
    $want = [Convert]::ToBase64String($pristine[$file])
    foreach ($try in 1..8) {
        try { [IO.File]::WriteAllBytes($path, $pristine[$file]) } catch { }
        if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -eq $want) { return $true }
        Start-Sleep -Seconds 2
    }
    return $false
}

# ---- the baseline -------------------------------------------------------------------------------------------
Say ("mutate: {0} mutant(s), suite {1}, worktree {2}, list {3}" -f $mutants.Count, $suite, $root, $listPath)
if (-not $NoBaseline) {
    $b = Build-Suite
    if (-not $b.Ok) { Say "BASELINE NOT GREEN: $($b.Detail)"; exit 3 }
    if ($b.Warnings -gt 0) { Say "BASELINE NOT GREEN: $($b.Warnings) compiler warning(s)"; exit 3 }
    $r = Run-Suite $listArgs $listEnv
    if ($r.Hung -or $r.Exit -ne 0) {
        Say ("BASELINE NOT GREEN: exit {0}, {1} {2}" -f $r.Exit, $r.Summary, (($r.Failed | Select-Object -First 3) -join ', '))
        exit 3
    }
    if ($r.Ran -eq 0) { Say "BASELINE NOT GREEN: no test ran ($($r.Summary)); check the list's args"; exit 3 }
    Say "baseline: green, $($r.Summary)"
}

# ---- the mutants --------------------------------------------------------------------------------------------
$counts = [ordered]@{ caught = 0; survived = 0; compile = 0; 'no verdict' = 0 }
$unmet = @()
foreach ($m in $mutants) {
    $path = Join-Path $root $m.file
    $text = $utf8.GetString($pristine[$m.file])
    $old = ([string]$m.old) -replace "`r`n", "`n"
    $new = ([string]$m.new) -replace "`r`n", "`n"
    if ($text.Contains("`r`n")) { $old = $old -replace "`n", "`r`n"; $new = $new -replace "`n", "`r`n" }
    $n = ([regex]::Matches($text, [regex]::Escape($old))).Count
    $verdict = ''; $detail = ''
    if ($n -ne 1) {
        $verdict = 'NO-MATCH'; $detail = "the old text is in the file $n times"
    } else {
        $index = $text.IndexOf($old)
        $mutated = $text.Substring(0, $index) + $new + $text.Substring($index + $old.Length)
        try {
            [IO.File]::WriteAllBytes($path, $utf8.GetBytes($mutated))
            $stamp = (Get-Item $path).LastWriteTimeUtc
            $b = Build-Suite
            if (-not $b.Ok) {
                if ($b.Locked) { $verdict = 'LOCKED'; $detail = $b.Detail } else { $verdict = 'COMPILE'; $detail = $b.Detail }
            } else {
                $product = Get-Product $m.file
                if (-not (Test-Path $product) -or (Get-Item $product).LastWriteTimeUtc -le $stamp) {
                    $verdict = 'STALE'; $detail = "$product is not newer than the mutated file"
                } else {
                    $arguments = $listArgs
                    if ($null -ne $m.args) { $arguments = To-Array $m.args }
                    $variables = [ordered]@{}
                    foreach ($k in $listEnv.Keys) { $variables[$k] = $listEnv[$k] }
                    $own = To-Map $m.env
                    foreach ($k in $own.Keys) { $variables[$k] = $own[$k] }
                    $r = Run-Suite $arguments $variables
                    if ($r.Hung) { $verdict = 'CAUGHT'; $detail = "by a hang ($($r.Summary))" }
                    elseif ($r.Exit -ne 0) {
                        $verdict = 'CAUGHT'
                        if ($r.Failed.Count -gt 0) { $detail = (($r.Failed | Select-Object -First 3) -join ', ') + $(if ($r.Failed.Count -gt 3) { ", +$($r.Failed.Count - 3) more" } else { '' }) }
                        else { $detail = "exit $($r.Exit), no failing test named (a crash?)" }
                    } elseif ($r.Ran -eq 0) { $verdict = 'NO-TESTS'; $detail = "no test ran ($($r.Summary)); check the mutant's args" }
                    else { $verdict = 'SURVIVED'; $detail = $r.Summary }
                }
            }
        } finally {
            if (-not (Restore $m.file)) { Say "RESTORE FAILED after $($m.id): $($m.file) is still mutated. Stopping."; exit 4 }
        }
    }
    switch ($verdict) {
        'CAUGHT'   { $counts.caught++ }
        'SURVIVED' { $counts.survived++ }
        'COMPILE'  { $counts.compile++ }
        default    { $counts['no verdict']++ }
    }
    $mark = ''
    if ($m.expect) {
        if ($verdict.ToLower() -eq $m.expect) { $mark = '' } else { $mark = "  << expected $($m.expect)"; $unmet += $m.id }
    }
    Say ("{0,-6} {1,-9} {2}{3}" -f $m.id, $verdict, $detail, $mark)
}

# ---- the end ------------------------------------------------------------------------------------------------
foreach ($f in $pristine.Keys) {
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $root $f))) -ne [Convert]::ToBase64String($pristine[$f])) {
        Say "!! $f differs from its pristine bytes"; exit 4
    }
}
Say ''
Say ((($counts.Keys | ForEach-Object { "$_ $($counts[$_])" }) -join ', ') + ", of $($mutants.Count)")
if ($unmet.Count -gt 0) { Say ("expectations not met: " + ($unmet -join ', ')); exit 5 }
if ($counts['no verdict'] -gt 0) { exit 1 }
exit 0
