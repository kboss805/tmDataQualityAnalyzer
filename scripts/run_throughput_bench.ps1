# =============================================================================
# run_throughput_bench.ps1 - run the decommutation throughput guard.
#
# WHY THIS EXISTS
#
# The processing pipeline - one reader, N parallel workers, a bit-serial frame-sync
# scanner over every bit of the recording - had NO performance regression guard.
# A benchmark measured it, but it was opt-in behind TMDQA_RUN_HEAVY_BENCH, nothing set
# that variable, and the test asserted nothing anyway: it printed timings and passed
# regardless. So the guard existed on paper only, twice over.
#
# Two things were wrong and are now fixed:
#   - The benchmark asserts its RATIOS (see the thresholds in
#     native/tmdq_core_tests/test_pipelinerunner.cpp). Reporting is not guarding.
#   - It runs at every release, from here, instead of waiting to be remembered.
#
# WHERE IT LIVES NOW
#
# It was the Qt TestProcessingCoordinator's benchmarkHeavyWorkloadSingleVsMultiStream.
# It measures PipelineRunner - the single reader and the parallel workers - and that is
# Qt-free, so it moved to the native doctest suite (plan step 2.4) as
# "PipelineRunner throughput: ...". It needs no Qt to build or to run, so this script
# no longer builds the Qt tests at all; the full Qt suite is run separately, by the
# build-and-test skill, at release.
#
# ONCE ASKED FOR, IT CANNOT SKIP QUIETLY
#
# Unset, the case is skipped and listed as skipped. With TMDQA_RUN_HEAVY_BENCH set (this
# script sets it) it runs, and a recording that cannot satisfy it is a FAILURE, not a
# skip: a guard that passes by not running is the failure this exists to close. This
# script also refuses to pass unless doctest reports that exactly that case ran and
# passed.
#
# IT WAS ALSO POINTED AT A FIXTURE THAT COULD NOT SATISFY IT
#
# The test read 'prn_testfile, sample.ch10' - the committed 77 MB excerpt - but it
# times FOUR channels, and that file carries PCM packets for channels 13 and 15
# only; 14 and 16 are in its TMATS with no data behind them. So enabling the
# variable did not produce a number. It now requires 'prn_testfile, full.ch10'
# (~640 MB, gitignored, present on the development box and the self-hosted runner)
# and says so plainly when it is absent.
#
# WHY RELEASE AND NOT CI
#
# CI was deliberately cut from 286 s to 93 s (PR #48, jom). This run takes minutes,
# so it would more than triple every PR for a property that is not correctness. A
# release is the right gate: it is where check_release_consistency.ps1 already runs,
# and a release must come from a green full-suite build regardless. CI therefore
# leaves the benchmark off.
#
# THE RUN IS PINNED TO THE PERFORMANCE CORES
#
# On a hybrid CPU the absolute timings were bimodal. On the i7-14700KF development box
# one unchanged executable measured 14962 ms and then 26386 ms for the 4-stream run,
# every channel ~1.9x slower at once and every ratio unchanged - which read as a
# regression in a change that could not have caused one. Pinning settled the cause:
# the same binary measured 26112 ms on the efficiency cores alone and 14189 ms on the
# performance cores alone. Windows was sometimes scheduling the single-threaded
# decoder onto efficiency cores. The guard asserts ratios, so it held throughout, but
# the milliseconds were useless as before/after evidence. So the benchmark now runs
# pinned to the highest efficiency class, discovered from Windows at run time
# (GetLogicalProcessorInformationEx) rather than hard-coded, and the mask is printed
# so a result says what it measured. On a CPU with one core class nothing is pinned.
#
# Usage (exits 1 if anything fails):
#
#   powershell -ExecutionPolicy Bypass -File scripts\run_throughput_bench.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\run_throughput_bench.ps1 -SkipBuild
#   powershell -ExecutionPolicy Bypass -File scripts\run_throughput_bench.ps1 -AllCores
# =============================================================================

param(
    # Reuse whatever is already in native\out\<config>\. Only safe when nothing has been
    # rebuilt since - a stale test exe is exactly how a Qt bump produced a crash in
    # an unrelated suite three times (see CLAUDE.md).
    [switch]$SkipBuild,

    # Build and measure Debug instead. For iterating on the benchmark itself; the
    # numbers are NOT comparable to the recorded Release baseline.
    # Deliberately not named -Debug: that is a PowerShell common parameter name, and
    # it would start colliding the moment anyone added [CmdletBinding()] above.
    [switch]$DebugBuild,

    # Do not pin to the performance cores. For comparing against an unpinned run;
    # the absolute timings may then land in either mode (see the header).
    [switch]$AllCores
)

$ErrorActionPreference = 'Stop'

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir

$Config = if ($DebugBuild) { 'Debug' } else { 'Release' }
$TestExe = Join-Path $ProjectDir "native\out\$Config\tmdq_core_tests.exe"
$CaseFilter = 'PipelineRunner throughput*'

# A variable left in the caller's environment would make the build step below run the
# benchmark too, unpinned, which is exactly the measurement this script exists to avoid.
Remove-Item Env:\TMDQA_RUN_HEAVY_BENCH -ErrorAction SilentlyContinue

Write-Host ''
Write-Host "=== Decommutation throughput guard ($Config) ===" -ForegroundColor Cyan

# --- the fixture -------------------------------------------------------------
# Checked before spending minutes on a build. Mirrors the test's own choice: the full
# recording beside the sample if it is there, otherwise the sample path, which on the
# self-hosted runner has been overlaid with the full recording from $TMDQA_CH10_FULL. A
# fixture under ~600 MB is the committed sample, which carries PCM data for 2 of the 4
# channels the benchmark times - it cannot produce a number, so say so here rather than
# after a build and a multi-minute run.
$FixtureDir = Join-Path $ProjectDir 'tests\data\test files'
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
    Write-Host "with nothing behind them, so the benchmark cannot time four streams and would" -ForegroundColor Red
    Write-Host "fail. Put 'prn_testfile, full.ch10' (~640 MB, gitignored) beside it, or set" -ForegroundColor Red
    Write-Host "TMDQA_CH10_FULL and overlay it onto the sample path as CI does." -ForegroundColor Red
    exit 1
}
Write-Host ("  cores     {0}" -f [Environment]::ProcessorCount)

# --- build, and run everything else ------------------------------------------
# test_native.ps1 builds the native tests (zero warnings, no Qt import) and runs the whole
# suite, unpinned and with the benchmark off: a release needs those green as well.
if ($SkipBuild) {
    Write-Host "  build     skipped (-SkipBuild)"
} else {
    Write-Host "  build     native tests ($Config), and the rest of the native suite"
    & powershell -ExecutionPolicy Bypass -File (Join-Path $ScriptDir 'test_native.ps1') -Configuration $Config
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED - the native tests did not build or did not pass." -ForegroundColor Red
        exit 1
    }
}
if (-not (Test-Path -LiteralPath $TestExe)) {
    Write-Host "FAILED - test executable not found: $TestExe" -ForegroundColor Red
    exit 1
}

# --- pin to the performance cores (see the header) ----------------------------
function Get-PerformanceCoreMask {
    # One PROCESSOR_RELATIONSHIP per physical core. Offsets: Size @4, EfficiencyClass
    # @9 (higher = faster), GroupMask[0] @32 = KAFFINITY Mask (8 bytes) + WORD Group.
    $src = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class TmdqCoreClasses {
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetLogicalProcessorInformationEx(int relation, IntPtr buffer, ref uint length);
    public static List<long[]> Cores() {
        uint len = 0;
        GetLogicalProcessorInformationEx(0, IntPtr.Zero, ref len);
        IntPtr buf = Marshal.AllocHGlobal((int)len);
        var result = new List<long[]>();
        try {
            if (!GetLogicalProcessorInformationEx(0, buf, ref len))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            long off = 0;
            while (off < len) {
                IntPtr p = new IntPtr(buf.ToInt64() + off);
                result.Add(new long[] { Marshal.ReadByte(p, 9), Marshal.ReadInt16(p, 40), Marshal.ReadInt64(p, 32) });
                off += Marshal.ReadInt32(p, 4);
            }
        } finally { Marshal.FreeHGlobal(buf); }
        return result;
    }
}
'@
    if (-not ('TmdqCoreClasses' -as [type])) { Add-Type -TypeDefinition $src }
    $cores = [TmdqCoreClasses]::Cores()
    $classes = @($cores | ForEach-Object { $_[0] } | Sort-Object -Unique)
    if ($classes.Count -lt 2) { return $null }                    # not a hybrid CPU
    if (@($cores | Where-Object { $_[1] -ne 0 }).Count -gt 0) { return $null }  # >1 processor group
    $top = ($classes | Measure-Object -Maximum).Maximum
    $mask = [long]0
    foreach ($c in $cores) { if ($c[0] -eq $top) { $mask = $mask -bor $c[2] } }
    return $mask
}

$self = [System.Diagnostics.Process]::GetCurrentProcess()
$originalAffinity = $self.ProcessorAffinity
$pinned = $false
if ($AllCores) {
    Write-Host "Not pinned (-AllCores): absolute timings may land in either mode." -ForegroundColor Yellow
} else {
    $pMask = Get-PerformanceCoreMask
    if ($pMask) {
        # The test exe inherits this process's affinity when it starts.
        $self.ProcessorAffinity = [IntPtr]$pMask
        $pinned = $true
        Write-Host ("Pinned to the performance cores: affinity 0x{0:X} ({1} logical processors)." -f `
                    $pMask, ([Convert]::ToString($pMask, 2) -replace '0', '').Length)
    } else {
        Write-Host "One core class on this CPU (or several processor groups): not pinned."
    }
}

# --- run ---------------------------------------------------------------------
Write-Host ''
Write-Host "Running the throughput benchmark." -ForegroundColor Cyan
Write-Host "This takes several minutes - four channel probes, four solo runs, one parallel run."
Write-Host ''

$OutDir = Join-Path $ProjectDir 'native\out'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Log = Join-Path $OutDir "throughput_$Config.txt"
if (Test-Path -LiteralPath $Log) { Remove-Item -LiteralPath $Log -Force }

$env:TMDQA_RUN_HEAVY_BENCH = '1'
try {
    # Invoked directly rather than through Start-Process: under Windows PowerShell
    # 5.1 a -PassThru process has not cached its handle, so $p.ExitCode reads back
    # EMPTY once it exits. That reported a successful signing as a failure in
    # build_release.ps1 and is not worth re-learning here.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'   # 5.1 turns native stderr into a terminating error
    & $TestExe "-tc=$CaseFilter" 2>&1 | Tee-Object -FilePath $Log | Out-Host
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
} finally {
    Remove-Item Env:\TMDQA_RUN_HEAVY_BENCH -ErrorAction SilentlyContinue
    if ($pinned) { $self.ProcessorAffinity = $originalAffinity }
}

# --- report ------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $Log)) {
    Write-Host "FAILED - no output was captured ($Log); the exit code was $code." -ForegroundColor Red
    exit 1
}

Write-Host ''
Write-Host "--- throughput numbers ---" -ForegroundColor Cyan
$benchLines = @(Select-String -Path $Log -Pattern '\[benchmark-heavy\]' | ForEach-Object { $_.Line })
if ($benchLines.Count -eq 0) {
    Write-Host "FAILED - the benchmark produced no output at all." -ForegroundColor Red
    Write-Host "It was enabled, so it should at minimum have printed its channel probes." -ForegroundColor Red
    exit 1
}
foreach ($l in $benchLines) { Write-Host ("  " + ($l -replace '^\s*\[benchmark-heavy\]\s*', '')) }

# A benchmark that did not RUN is the quiet failure this script exists to prevent: the
# totals look healthy and nothing was measured. Require doctest to say that exactly the
# benchmark case ran and passed, rather than trusting an exit code.
$summary = Select-String -Path $Log -Pattern 'test cases:\s+(\d+)\s+\|\s+(\d+) passed\s+\|\s+(\d+) failed' | Select-Object -Last 1
if (-not $summary) {
    Write-Host "FAILED - doctest printed no summary; the run died before reporting (exit $code)." -ForegroundColor Red
    exit 1
}
$m = $summary.Matches[0]
$ran = [int]$m.Groups[1].Value; $passed = [int]$m.Groups[2].Value; $failed = [int]$m.Groups[3].Value
Write-Host ''
Write-Host "Results: $passed passed, $failed failed (exit $code)"
if ($passed -ne 1 -or $failed -ne 0) {
    Write-Host "FAILED - expected exactly the one benchmark case to run and pass." -ForegroundColor Red
    if ($passed -eq 0 -and $failed -eq 0) {
        Write-Host "It did not run at all: it is skipped unless TMDQA_RUN_HEAVY_BENCH is set." -ForegroundColor Red
    }
    exit 1
}
if (@(Select-String -Path $Log -Pattern 'NOT GUARDED').Count -gt 0) {
    Write-Host ''
    Write-Host "FAILED - the benchmark ran but did not assert (too few cores)." -ForegroundColor Red
    Write-Host "Its ratios describe this machine rather than the architecture." -ForegroundColor Red
    exit 1
}
if ($code -ne 0) {
    Write-Host "FAILED - the benchmark exited non-zero." -ForegroundColor Red
    exit 1
}

Write-Host "Throughput guard passed." -ForegroundColor Green
exit 0
