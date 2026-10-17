# =============================================================================
# run_throughput_bench.ps1 - run the decommutation throughput guard.
#
# WHY THIS EXISTS
#
# The processing pipeline - one reader, N parallel workers, a bit-serial frame-sync
# scanner over every bit of the recording - had NO performance regression guard.
# TestProcessingCoordinator::benchmarkHeavyWorkloadSingleVsMultiStream measured it,
# but it was opt-in behind TMDQA_RUN_HEAVY_BENCH, nothing set that variable, and the
# test asserted nothing anyway: it printed timings through qWarning() and passed
# regardless. So the guard existed on paper only, twice over.
#
# Two things were wrong and are now fixed:
#   - The benchmark asserts its RATIOS (see the thresholds in
#     tests/tst_processingcoordinator.cpp). Reporting is not guarding.
#   - It runs at every release, from here, instead of waiting to be remembered.
#
# IT WAS ALSO POINTED AT A FIXTURE THAT COULD NOT SATISFY IT
#
# The test read 'prn_testfile, sample.ch10' - the committed 77 MB excerpt - but it
# times FOUR channels, and that file carries PCM packets for channels 13 and 15
# only; 14 and 16 are in its TMATS with no data behind them. So enabling the
# variable did not produce a number: the benchmark probed four channels, recognized
# two, and skipped blaming channel identification. Being opt-in hid that, so "nobody
# sets the variable" was never the whole story.
#
# It now requires 'prn_testfile, full.ch10' (~640 MB, gitignored, present on the
# development box and the self-hosted runner) and says so plainly when it is absent.
#
# WHY RELEASE AND NOT CI
#
# CI was deliberately cut from 286 s to 93 s (PR #48, jom). This run takes minutes,
# so it would more than triple every PR for a property that is not correctness. A
# release is the right gate: it is where check_release_consistency.ps1 already runs,
# and a release must come from a green full-suite build regardless. CI therefore
# still leaves the benchmark skipped, and its "exactly one skip" gate still expects
# exactly that one.
#
# NOTE ON THE FULL SUITE
#
# The harness has no single-suite or single-test CLI filter (multiple qExec calls,
# so a test-function argument would be rejected by every other suite). This
# therefore runs the WHOLE suite in Release with the benchmark enabled, which is
# exactly what a release needs anyway - it replaces the by-hand Step 5 run rather
# than adding to it.
#
# Usage (exits 1 if any test fails):
#
#   powershell -ExecutionPolicy Bypass -File scripts\run_throughput_bench.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\run_throughput_bench.ps1 -SkipBuild
# =============================================================================

param(
    # Reuse whatever is already in tests\release\. Only safe when nothing has been
    # rebuilt since - a stale test exe is exactly how a Qt bump produced a crash in
    # an unrelated suite three times (see CLAUDE.md).
    [switch]$SkipBuild,

    # Build and measure Debug instead. For iterating on the benchmark itself; the
    # numbers are NOT comparable to the recorded Release baseline.
    # Deliberately not named -Debug: that is a PowerShell common parameter name, and
    # it would start colliding the moment anyone added [CmdletBinding()] above.
    [switch]$DebugBuild
)

$ErrorActionPreference = 'Stop'

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$TestsDir   = Join-Path $ProjectDir 'tests'

$Config  = if ($DebugBuild) { 'debug' } else { 'release' }
$Makefile = if ($DebugBuild) { 'Makefile.Debug' } else { 'Makefile.Release' }

Write-Host ''
Write-Host "=== Decommutation throughput guard ($Config) ===" -ForegroundColor Cyan

# --- the fixture -------------------------------------------------------------
# Checked before spending minutes on a build. The benchmark itself QSKIPs when this
# is missing, and a skip is indistinguishable from a pass in the totals - which is
# the failure mode this whole script exists to close, so do not rely on it.
# Mirrors the test's own choice: the full recording beside the sample if it is there,
# otherwise the sample path, which on the self-hosted runner has been overlaid with the
# full recording from $TMDQA_CH10_FULL. A fixture under ~600 MB is the committed sample,
# which carries PCM data for 2 of the 4 channels the benchmark times - it cannot produce
# a number, so say so here rather than after a build and a multi-minute run.
$FixtureDir = Join-Path $TestsDir 'data\test files'
$Fixture    = Join-Path $FixtureDir 'prn_testfile, full.ch10'
if (-not (Test-Path -LiteralPath $Fixture)) {
    $Fixture = Join-Path $FixtureDir 'prn_testfile, sample.ch10'
}
if (-not (Test-Path -LiteralPath $Fixture)) {
    Write-Host "FAILED - no PRN fixture found in:" -ForegroundColor Red
    Write-Host "  $FixtureDir" -ForegroundColor Red
    Write-Host "In a worktree, junction tests\data to the main checkout (see CLAUDE.md)." -ForegroundColor Red
    exit 1
}
$FixtureMB = [math]::Round((Get-Item -LiteralPath $Fixture).Length / 1MB, 1)
Write-Host ("  fixture   {0} ({1} MB)" -f (Split-Path -Leaf $Fixture), $FixtureMB)
if ($FixtureMB -lt 600) {
    Write-Host ''
    Write-Host "FAILED - that is the committed sample, not the full recording." -ForegroundColor Red
    Write-Host "It holds PCM data for channels 13 and 15 only; 14 and 16 are in its TMATS" -ForegroundColor Red
    Write-Host "with nothing behind them, so the benchmark cannot time four streams and will" -ForegroundColor Red
    Write-Host "skip. Put 'prn_testfile, full.ch10' (~640 MB, gitignored) beside it, or set" -ForegroundColor Red
    Write-Host "TMDQA_CH10_FULL and overlay it onto the sample path as CI does." -ForegroundColor Red
    exit 1
}
Write-Host ("  cores     {0}" -f [Environment]::ProcessorCount)

# --- toolchain ---------------------------------------------------------------
. (Join-Path $ScriptDir 'env.ps1')

# --- build -------------------------------------------------------------------
# Built in-source under tests\ on purpose: the exe resolves its fixtures with
# applicationDirPath() -> cdUp() -> data\, so it must sit exactly one level below
# tests\ or every data-file test fails for reasons unrelated to the code.
Set-Location $TestsDir
if ($SkipBuild) {
    Write-Host "  build     skipped (-SkipBuild)"
} else {
    Write-Host "  build     qmake + $env:TMDQ_MAKE ($Config)"
    & qmake tests.pro -spec win32-msvc "CONFIG+=$Config"
    if ($LASTEXITCODE -ne 0) { throw "qmake failed" }
    & $env:TMDQ_MAKE -f $Makefile
    if ($LASTEXITCODE -ne 0) { throw "Test build failed" }
}

$TestExe = Join-Path $TestsDir "$Config\tmDataQualityAnalyzer_tests.exe"
if (-not (Test-Path -LiteralPath $TestExe)) {
    Write-Host "FAILED - test executable not found: $TestExe" -ForegroundColor Red
    exit 1
}

# --- run ---------------------------------------------------------------------
New-Item -ItemType Directory -Force -Path (Join-Path $TestsDir 'output') | Out-Null
$Results = Join-Path $TestsDir 'output\results.txt'
if (Test-Path -LiteralPath $Results) { Remove-Item -LiteralPath $Results -Force }

Write-Host ''
Write-Host "Running the full suite with the throughput benchmark enabled." -ForegroundColor Cyan
Write-Host "This takes several minutes - four channel probes, four solo runs, one"
Write-Host "parallel run, plus the four .ch10 integration suites."
Write-Host ''

$env:TMDQA_RUN_HEAVY_BENCH = '1'
try {
    # Invoked directly rather than through Start-Process: under Windows PowerShell
    # 5.1 a -PassThru process has not cached its handle, so $p.ExitCode reads back
    # EMPTY once it exits. That reported a successful signing as a failure in
    # build_release.ps1 and is not worth re-learning here.
    & $TestExe
    $code = $LASTEXITCODE
} finally {
    Remove-Item Env:\TMDQA_RUN_HEAVY_BENCH -ErrorAction SilentlyContinue
}

# --- report ------------------------------------------------------------------
$pass = 0; $fail = 0; $skip = 0
if (Test-Path -LiteralPath $Results) {
    $pass = @(Select-String -Path $Results -Pattern '^PASS').Count
    $fail = @(Select-String -Path $Results -Pattern '^FAIL').Count
    $skip = @(Select-String -Path $Results -Pattern '^SKIP').Count
} else {
    Write-Host "FAILED - no results file was written ($Results)." -ForegroundColor Red
    Write-Host "The run died before reporting; the exit code was $code." -ForegroundColor Red
    exit 1
}

Write-Host ''
Write-Host "--- throughput numbers ---" -ForegroundColor Cyan
$benchLines = @(Select-String -Path $Results -Pattern 'benchmark-heavy' |
                ForEach-Object { $_.Line })
if ($benchLines.Count -eq 0) {
    Write-Host "FAILED - the benchmark produced no output at all." -ForegroundColor Red
    Write-Host "It was enabled, so it should at minimum have printed its channel probes." -ForegroundColor Red
    exit 1
}
foreach ($l in $benchLines) {
    # Strip the Qt Test "QWARN  : Suite::test() " prefix so the numbers read cleanly.
    Write-Host ("  " + ($l -replace '^\s*QWARN\s*:\s*\S+\s*', ''))
}

# A benchmark that SKIPPED is the quiet failure this script exists to prevent: the
# totals look healthy and nothing was measured. Name it rather than letting the
# aggregate hide it.
$benchSkip = @(Select-String -Path $Results -Pattern '^SKIP.*benchmarkHeavyWorkload')
if ($benchSkip.Count -gt 0) {
    Write-Host ''
    Write-Host "FAILED - the throughput benchmark SKIPPED, so nothing was measured:" -ForegroundColor Red
    $benchSkip | ForEach-Object { Write-Host "  $($_.Line)" -ForegroundColor Red }
    exit 1
}
if (@(Select-String -Path $Results -Pattern 'NOT GUARDED').Count -gt 0) {
    Write-Host ''
    Write-Host "FAILED - the benchmark ran but did not assert (too few cores)." -ForegroundColor Red
    Write-Host "Its ratios describe this machine rather than the architecture." -ForegroundColor Red
    exit 1
}

Write-Host ''
Write-Host "Results: $pass passed, $fail failed, $skip skipped (exit $code)"

if ($code -ne 0) {
    Write-Host "FAILED - the suite exited non-zero." -ForegroundColor Red
    exit 1
}
if ($fail -gt 0) {
    Write-Host "FAILED - $fail test(s) failed:" -ForegroundColor Red
    Select-String -Path $Results -Pattern '^FAIL' | ForEach-Object {
        Write-Host "  $($_.Line)" -ForegroundColor Red
    }
    exit 1
}
# With the benchmark enabled it is no longer the suite's one standing skip, so a
# release run should have none at all. Anything left is a missing fixture.
if ($skip -gt 0) {
    Write-Host "FAILED - $skip unexpected skip(s); with the benchmark on there should be none:" -ForegroundColor Red
    Select-String -Path $Results -Pattern '^SKIP' | ForEach-Object {
        Write-Host "  $($_.Line)" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Throughput guard passed, and the full suite is green ($pass tests)." -ForegroundColor Green
exit 0
