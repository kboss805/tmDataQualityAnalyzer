<#
.SYNOPSIS
    Builds the managed side (dotnet\TmDq.Core and its tests) and runs the tests against the native core DLL.

.DESCRIPTION
    1. Requires native\out\<Configuration>\tmdq_core.dll (build it with scripts\build_core_dll.ps1). The tests find
       it themselves; TMDQ_CORE_DLL overrides.
    2. `dotnet build` of the test project, treating warnings as errors (the project says so). The build merges a
       UTF-8 active-code-page manifest into the test exe with mt.exe (the Windows SDK), so the Visual Studio
       tools must be on PATH: scripts\env.ps1 is dot-sourced when mt is not already found.
    3. Runs the test exe directly (an xunit v3 test project is an executable) and fails on a non-zero exit.

    Works under Windows PowerShell 5.1 and PowerShell 7.

    Usage:
        powershell -ExecutionPolicy Bypass -File scripts\test_dotnet.ps1 [-Configuration Debug|Release] [-NoBuild]
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

if (-not $env:TMDQ_CORE_DLL) {
    $dll = Join-Path $root "native\out\$Configuration\tmdq_core.dll"
    if (-not (Test-Path $dll)) {
        $other = Join-Path $root 'native\out\Release\tmdq_core.dll'
        if (-not (Test-Path $other)) { $other = Join-Path $root 'native\out\Debug\tmdq_core.dll' }
        if (-not (Test-Path $other)) { throw "tmdq_core.dll has not been built: run scripts\build_core_dll.ps1" }
    }
}

$project = Join-Path $root 'dotnet\TmDq.Core.Tests'
$exe = Join-Path $project "bin\$Configuration\net10.0\TmDq.Core.Tests.exe"

$ErrorActionPreference = 'Continue'   # 5.1 turns native stderr into a terminating error
if (-not $NoBuild) {
    Write-Host "Building TmDq.Core.Tests ($Configuration) ..."
    & dotnet build $project -c $Configuration -nologo -v minimal
    if ($LASTEXITCODE -ne 0) { throw "dotnet build failed ($LASTEXITCODE)" }
}
if (-not (Test-Path $exe)) { throw "the test executable was not produced: $exe" }

Write-Host "Running $exe ..."
& $exe
$code = $LASTEXITCODE
if ($code -ne 0) { throw "the managed tests failed (exit $code)" }
Write-Host 'Managed tests passed.'
