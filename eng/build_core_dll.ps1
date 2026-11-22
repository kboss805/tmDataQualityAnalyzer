<#
.SYNOPSIS
    Builds tmdq_core.dll (src\core) and checks it is warning-clean and imports only what is allowed.

.DESCRIPTION
    1. MSBuild builds the native project (src\core\tmdq_core.vcxproj, which tmdq.sln also holds beside
       the C# projects, so this script never needs the .NET SDK) for x64 in the chosen configuration.
    2. The build log is scanned: any compiler warning fails the script (the project's zero-warning rule).
    3. `dumpbin /dependents` lists the DLL's imports, and every one must be on a short allow-list (the C
       runtime, KERNEL32, WS2_32, USER32). An allow-list, so ANY new
       dependency is noticed and has to be added to eng\common.ps1 on purpose.

    Output: out\<Configuration>\tmdq_core.dll (+ .lib, .pdb), out\obj\ for intermediates.
    Both are gitignored.

    Works under Windows PowerShell 5.1 and PowerShell 7. If msbuild is not already on PATH (a plain shell
    rather than a developer prompt), eng\env.ps1 is dot-sourced to import the Visual Studio 2022 tools.

    Usage:
        powershell -ExecutionPolicy Bypass -File eng\build_core_dll.ps1 [-Configuration Debug|Release]
        ... -Rebuild      clean first (do this after deleting or renaming a header)
#>
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'common.ps1')
Initialize-MsvcEnvironment $PSScriptRoot

$outDir = Join-Path $root "out\$Configuration"
New-Item -ItemType Directory -Force $outDir | Out-Null
$log = Join-Path $root "out\build_$Configuration.log"
$dll = Join-Path $outDir 'tmdq_core.dll'
if (Test-Path $dll) { Remove-Item $dll -Force }   # a stale DLL must never pass for a fresh build

Write-Host "Building tmdq_core ($Configuration, x64) ..."
# Built, and with zero compiler warnings (the project's zero-warning rule): see eng\common.ps1.
Invoke-MsbuildChecked (Join-Path $root 'src\core\tmdq_core.vcxproj') $Configuration -Rebuild:$Rebuild $log $dll

# Imports: an allow-list (eng\common.ps1), so any new dependency has to be added there on purpose.
$null = Assert-ImportsAllowed $dll
Write-Host ("  {0}  {1:N0} bytes - imports are on the allow-list" -f $dll, (Get-Item $dll).Length)
