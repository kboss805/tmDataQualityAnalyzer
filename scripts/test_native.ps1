<#
.SYNOPSIS
    Builds and runs the native core's tests (native\tmdq_core_tests, doctest), with no Qt.

.DESCRIPTION
    1. MSBuild builds native\tmdq_core_tests\tmdq_core_tests.vcxproj for x64 in the chosen configuration. The
       project has no Qt include path or library, so a test or core source that reached for Qt would not compile.
    2. The build log is scanned: any compiler warning fails the script (the project's zero-warning rule).
    3. `dumpbin /dependents` must show no Qt DLL among the executable's imports.
    4. The executable is run; its exit code is the result. Arguments after -- go to doctest (for example
       --list-test-cases, or -tc="TomlDocument*" to run some).

    Output: native\out\<Configuration>\tmdq_core_tests.exe, native\obj\ for intermediates (gitignored).

    Works under Windows PowerShell 5.1 and PowerShell 7. If msbuild is not already on PATH (a plain shell rather
    than a developer prompt), scripts\env.ps1 is dot-sourced to import the Visual Studio 2022 tools.

    Usage:
        powershell -ExecutionPolicy Bypass -File scripts\test_native.ps1 [-Configuration Debug|Release] [-Rebuild] [-NoBuild]
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
$outDir = Join-Path $root "native\out\$Configuration"
$exe = Join-Path $outDir 'tmdq_core_tests.exe'

if (-not $NoBuild) {
    if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) {
        . (Join-Path $PSScriptRoot 'env.ps1') *> $null
    }
    if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) { throw 'msbuild not found (install the Visual Studio 2022 C++ build tools)' }
    if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) { throw 'dumpbin not found (it ships with the MSVC tools; use a developer prompt or scripts\env.ps1)' }

    New-Item -ItemType Directory -Force $outDir | Out-Null
    $log = Join-Path $root "native\out\test_native_$Configuration.log"
    if (Test-Path $exe) { Remove-Item $exe -Force }   # a stale executable must never pass for a fresh build

    $target = if ($Rebuild) { '/t:Rebuild' } else { '/t:Build' }
    Write-Host "Building tmdq_core_tests ($Configuration, x64) ..."
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'   # 5.1 turns native stderr into a terminating error
    & msbuild (Join-Path $root 'native\tmdq_core_tests\tmdq_core_tests.vcxproj') $target /m /nologo /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64 2>&1 |
        Tee-Object -FilePath $log | Out-Host
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { throw "msbuild failed ($code); see $log" }
    if (-not (Test-Path $exe)) { throw "msbuild reported success but $exe was not produced" }

    $warnings = @(Select-String -Path $log -Pattern 'warning [A-Z]+\d+')
    if ($warnings.Count -gt 0) {
        $warnings | Select-Object -First 10 | ForEach-Object { Write-Host "  $($_.Line)" }
        throw "$($warnings.Count) compiler warning(s); the build must be warning-clean"
    }
    Write-Host '  0 warnings'

    $deps = @(& dumpbin /nologo /dependents $exe 2>&1 | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() })
    if ($deps.Count -eq 0) { throw 'dumpbin listed no imports; cannot verify the executable' }
    $qt = @($deps | Where-Object { $_ -match '^Qt' })
    if ($qt.Count -gt 0) { throw "tmdq_core_tests.exe imports Qt: $($qt -join ', ')" }
    Write-Host ("  imports: {0} (no Qt)" -f ($deps -join ', '))
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
