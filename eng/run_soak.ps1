<#
.SYNOPSIS
    Runs the side-by-side soak (plan step 5.2): every run in tests\TmDq.App.Tests\Soak\soak.json through the Avalonia
    application, each CSV export compared byte for byte with the manifest's "qt" block: v2.14.2's exported values, with
    their times on each column's sample grid (the export rounds each column's times to its sample rate since PR #380;
    every column was checked value for value against v2.14.2's own export when the blocks were re-recorded).
.DESCRIPTION
    The soak's runs read the full recordings and the local ones under tests\data, which are not committed, so its tests
    (SoakTests) are explicit: the app suite lists them and does not run them. This script runs them, one process a run
    so that each run's peak working set is its own, and prints a line a run: whether its CSV export matched
    the manifest's "qt" block (v2.14.2's values on the sample grid), how long it took and the process's peak working set.

    Each run writes its CSV, a 1600 x 900 image of its plot (light theme) and result.json to <Out>\<run>\, for
    comparing the plots by eye. A run whose recording is missing fails; it does not skip.

    The tmdq_core.dll the tests load is out\<Configuration>\tmdq_core.dll (TMDQ_CORE_DLL overrides); build it
    with eng\build_core_dll.ps1 first. Works under Windows PowerShell 5.1 and PowerShell 7.

    Usage:
        powershell -ExecutionPolicy Bypass -File eng\run_soak.ps1 [-Configuration Debug|Release] [-NoBuild]
                                                                      [-Only <run>[,<run>...]] [-Out <folder>]
#>
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$NoBuild,
    [string[]]$Only,
    [string]$Out = (Join-Path ([System.IO.Path]::GetTempPath()) 'tmdq-soak')
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$manifest = Join-Path $root 'tests\TmDq.App.Tests\Soak\soak.json'
. (Join-Path $PSScriptRoot 'common.ps1')
$exe = Get-TestExePath $root 'TmDq.App.Tests' $Configuration

if (-not $NoBuild) {
    # test_app.ps1 builds the app and its tests (and sets up mt.exe); its run of the ordinary suite comes with it.
    & (Join-Path $PSScriptRoot 'test_app.ps1') -Configuration $Configuration
}
if (-not (Test-Path $exe)) { throw "the test executable was not produced: $exe" }

$names = @((Get-Content $manifest -Raw | ConvertFrom-Json).runs | ForEach-Object { $_.name })
if ($Only) {
    $wanted = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
    $unknown = @($wanted | Where-Object { $names -notcontains $_ })
    if ($unknown.Count -gt 0) { throw "no such run in the manifest: $($unknown -join ', ')" }
    $names = @($names | Where-Object { $wanted -contains $_ })
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$env:TMDQ_SOAK_OUT = $Out
$failed = @()
$ErrorActionPreference = 'Continue'   # 5.1 turns native stderr into a terminating error
foreach ($name in $names) {
    $env:TMDQ_SOAK_ONLY = $name
    # The run writes result.json before it compares: one left by an earlier run must not stand in for this one.
    $result = Join-Path (Join-Path $Out $name) 'result.json'
    Remove-Item $result -Force -ErrorAction SilentlyContinue
    $output = & $exe -explicit only -class TmDq.App.Tests.SoakTests 2>&1
    $code = $LASTEXITCODE
    $seconds = ''; $peak = ''
    $ran = Test-Path $result
    if ($ran) {
        $r = Get-Content $result -Raw | ConvertFrom-Json
        $seconds = '{0,7:F1} s' -f [double]$r.seconds
        $peak = '{0,7:F0} MB' -f [double]$r.peakWorkingSetMb
    }
    # Exit 0 with no result is a run that did not happen (a renamed class, a filter that matched nothing), not a pass.
    if ($code -eq 0 -and -not $ran) { $code = 1 }
    $verdict = if ($code -eq 0) { 'same' } elseif (-not $ran) { 'NOT RUN' } else { 'DIFFERENT' }
    Write-Host ('{0,-32} {1,-9} {2} {3}' -f $name, $verdict, $seconds, $peak)
    if ($code -ne 0) {
        $failed += $name
        $output | Where-Object { "$_" -match '\[FAIL\]|bytes, SHA-256|holds no Qt result|not on this machine|Exception' } |
            Select-Object -First 6 | ForEach-Object { Write-Host "    $_" }
    }
}
Remove-Item Env:TMDQ_SOAK_ONLY, Env:TMDQ_SOAK_OUT -ErrorAction SilentlyContinue

Write-Host "Output: $Out"
if ($failed.Count -gt 0) { throw "$($failed.Count) of $($names.Count) run(s) did not export the manifest's qt block (v2.14.2's values on the sample grid): $($failed -join ', ')" }
Write-Host "All $($names.Count) run(s) exported the manifest's qt block (v2.14.2's values on the sample grid)."
