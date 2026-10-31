<#
.SYNOPSIS
    Builds the Avalonia application (dotnet\TmDq.App) and its headless tests, and runs the tests.

.DESCRIPTION
    1. `dotnet build` of dotnet\TmDq.App.Tests, which builds the application with it, treating warnings as errors
       (the projects say so). The build merges the UTF-8 code-page manifest into the application exe with mt.exe
       (the Windows SDK) and runs the package licence check, so the Visual Studio tools must be on PATH:
       scripts\env.ps1 is dot-sourced when mt is not already found.
    2. Says which account, Windows session and kind of process the tests run under. The tests run on Avalonia's
       headless platform, which needs no window system; on the CI runner that is a service (NETWORK SERVICE,
       Session 0, no desktop), and this line is the record that the UI tests pass there.
    3. Runs the test exe directly (an xunit v3 test project is an executable) and fails on a non-zero exit.

    The tests need no native core DLL. Works under Windows PowerShell 5.1 and PowerShell 7.

    Usage:
        powershell -ExecutionPolicy Bypass -File scripts\test_app.ps1 [-Configuration Debug|Release] [-NoBuild]
#>
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not (Get-Command mt -ErrorAction SilentlyContinue)) {
    . (Join-Path $PSScriptRoot 'env.ps1')
}
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) { throw 'dotnet not found (install the .NET 10 SDK)' }
if (-not (Get-Command mt -ErrorAction SilentlyContinue)) { throw 'mt.exe not found (it ships with the Windows SDK; use a developer prompt or scripts\env.ps1)' }

$project = Join-Path $root 'dotnet\TmDq.App.Tests'
$exe = Join-Path $project "bin\$Configuration\net10.0\TmDq.App.Tests.exe"

$ErrorActionPreference = 'Continue'   # 5.1 turns native stderr into a terminating error
if (-not $NoBuild) {
    # A stale exe must not run if the build fails to write a new one.
    if (Test-Path $exe) { [System.IO.File]::Delete($exe) }
    Write-Host "Building TmDq.App.Tests ($Configuration) ..."
    & dotnet build $project -c $Configuration -nologo -v minimal
    if ($LASTEXITCODE -ne 0) { throw "dotnet build failed ($LASTEXITCODE)" }
}
if (-not (Test-Path $exe)) { throw "the test executable was not produced: $exe" }

$account = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$session = (Get-Process -Id $PID).SessionId
Write-Host ("Running as {0}, Windows session {1}, interactive: {2}" -f $account, $session, [Environment]::UserInteractive)

Write-Host "Running $exe ..."
& $exe
$code = $LASTEXITCODE
if ($code -ne 0) { throw "the app tests failed (exit $code)" }
Write-Host 'App tests passed.'
