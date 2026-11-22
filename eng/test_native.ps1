<#
.SYNOPSIS
    Builds and runs the native core's tests (tests\core, doctest).

.DESCRIPTION
    1. MSBuild builds tests\core\tmdq_core_tests.vcxproj for x64 in the chosen configuration.
    2. The build log is scanned: any compiler warning fails the script (the project's zero-warning rule).
    3. `dumpbin /dependents` must show only allowed DLLs among the executable's imports: the same allow-list as
       the core's own (eng\common.ps1).
    4. The executable is run; its exit code is the result. Arguments after -- go to doctest (for example
       --list-test-cases, or -tc="TomlDocument*" to run some).

    Output: out\<Configuration>\tmdq_core_tests.exe, out\obj\ for intermediates (gitignored).

    Works under Windows PowerShell 5.1 and PowerShell 7. If msbuild is not already on PATH (a plain shell rather
    than a developer prompt), eng\env.ps1 is dot-sourced to import the Visual Studio 2022 tools.

    Usage:
        powershell -ExecutionPolicy Bypass -File eng\test_native.ps1 [-Configuration Debug|Release] [-Rebuild] [-NoBuild]
        ... -Rebuild      clean first (do this after deleting or renaming a header)
        ... -NoBuild      run the executable that is there
#>
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Rebuild,
    [switch]$NoBuild,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$DoctestArgs
)
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outDir = Join-Path $root "out\$Configuration"
$exe = Join-Path $outDir 'tmdq_core_tests.exe'

if (-not $NoBuild) {
    . (Join-Path $PSScriptRoot 'common.ps1')
    Initialize-MsvcEnvironment $PSScriptRoot

    New-Item -ItemType Directory -Force $outDir | Out-Null
    $log = Join-Path $root "out\test_native_$Configuration.log"
    if (Test-Path $exe) { Remove-Item $exe -Force }   # a stale executable must never pass for a fresh build

    Write-Host "Building tmdq_core_tests ($Configuration, x64) ..."
    # Built, and with zero compiler warnings (the project's zero-warning rule): see eng\common.ps1.
    Invoke-MsbuildChecked (Join-Path $root 'tests\core\tmdq_core_tests.vcxproj') $Configuration -Rebuild:$Rebuild $log $exe

    $null = Assert-ImportsAllowed $exe
}
if (-not (Test-Path $exe)) { throw "the test executable is not there: $exe (build it, or drop -NoBuild)" }

Write-Host "Running $exe ..."
$prev = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $exe @DoctestArgs
$code = $LASTEXITCODE
$ErrorActionPreference = $prev
if ($code -ne 0) { throw "the native tests failed (exit $code)" }
Write-Host 'Native tests passed.'
