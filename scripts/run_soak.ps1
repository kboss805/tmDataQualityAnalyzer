<#
.SYNOPSIS
    Runs the side-by-side soak (plan step 5.2): every run in tests\TmDq.App.Tests\Soak\soak.json through the Avalonia
    application, each CSV export compared byte for byte with the Qt application's.
.DESCRIPTION
    The soak's runs read the full recordings and the local ones under tests\data, which are not committed, so its tests
    (SoakTests) are explicit: the app suite lists them and does not run them. This script runs them, one process a run
    so that each run's peak working set is its own, and prints a line a run: whether its CSV export matched the Qt
    application's (the manifest's "qt" block), how long it took and the process's peak working set.

    Each run writes its CSV, a 1600 x 900 image of its plot (light theme) and result.json to <Out>\<run>\, for
    comparing the plots by eye. A run whose recording is missing fails; it does not skip.

    The tmdq_core.dll the tests load is native\out\<Configuration>\tmdq_core.dll (TMDQ_CORE_DLL overrides); build it
    with scripts\build_core_dll.ps1 first. Works under Windows PowerShell 5.1 and PowerShell 7.

    Usage:
        powershell -ExecutionPolicy Bypass -File scripts\run_soak.ps1 [-Configuration Debug|Release] [-NoBuild]
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
$exe = Join-Path $root "tests\TmDq.App.Tests\bin\$Configuration\net10.0\TmDq.App.Tests.exe"

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
    $output = & $exe -explicit only -class TmDq.App.Tests.SoakTests 2>&1
    $code = $LASTEXITCODE
    $result = Join-Path (Join-Path $Out $name) 'result.json'
    $seconds = ''; $peak = ''
    if (Test-Path $result) {
        $r = Get-Content $result -Raw | ConvertFrom-Json
        $seconds = '{0,7:F1} s' -f [double]$r.seconds
        $peak = '{0,7:F0} MB' -f [double]$r.peakWorkingSetMb
    }
    $verdict = if ($code -eq 0) { 'same' } else { 'DIFFERENT' }
    Write-Host ('{0,-32} {1,-9} {2} {3}' -f $name, $verdict, $seconds, $peak)
    if ($code -ne 0) {
        $failed += $name
        $output | Where-Object { "$_" -match '\[FAIL\]|bytes, SHA-256|holds no Qt result|not on this machine|Exception' } |
            Select-Object -First 6 | ForEach-Object { Write-Host "    $_" }
    }
}
Remove-Item Env:TMDQ_SOAK_ONLY, Env:TMDQ_SOAK_OUT -ErrorAction SilentlyContinue

Write-Host "Output: $Out"
if ($failed.Count -gt 0) { throw "$($failed.Count) of $($names.Count) run(s) did not export what the Qt application exported: $($failed -join ', ')" }
Write-Host "All $($names.Count) run(s) exported what the Qt application exported."
