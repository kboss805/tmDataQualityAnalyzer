<#
.SYNOPSIS
    Builds tmdq_core.dll (native\tmdq_core) and proves it has no Qt in it.

.DESCRIPTION
    1. MSBuild builds the native project (native\tmdq_core\tmdq_core.vcxproj, which tmdq.sln also holds beside
       the C# projects, so this script never needs the .NET SDK) for x64 in the chosen configuration. The project has no Qt include
       path or library, so a core source that reached for a Qt header would not compile.
    2. The build log is scanned: any compiler warning fails the script (the project's zero-warning rule).
    3. `dumpbin /dependents` lists the DLL's imports, and every one must be on a short allow-list (the C
       runtime, KERNEL32, WS2_32, USER32). An allow-list rather than a "no Qt" check, so ANY new
       dependency is noticed and has to be added here on purpose.

    Output: native\out\<Configuration>\tmdq_core.dll (+ .lib, .pdb), native\obj\ for intermediates.
    Both are gitignored.

    Works under Windows PowerShell 5.1 and PowerShell 7. If msbuild is not already on PATH (a plain shell
    rather than a developer prompt), scripts\env.ps1 is dot-sourced to import the Visual Studio 2022 tools.

    Usage:
        powershell -ExecutionPolicy Bypass -File scripts\build_core_dll.ps1 [-Configuration Debug|Release]
        ... -Rebuild      clean first (do this after deleting or renaming a header)
#>
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) {
    . (Join-Path $PSScriptRoot 'env.ps1') *> $null
}
if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) { throw 'msbuild not found (install the Visual Studio 2022 C++ build tools)' }
if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) { throw 'dumpbin not found (it ships with the MSVC tools; use a developer prompt or scripts\env.ps1)' }

$outDir = Join-Path $root "native\out\$Configuration"
New-Item -ItemType Directory -Force $outDir | Out-Null
$log = Join-Path $root "native\out\build_$Configuration.log"
$dll = Join-Path $outDir 'tmdq_core.dll'
if (Test-Path $dll) { Remove-Item $dll -Force }   # a stale DLL must never pass for a fresh build

$target = if ($Rebuild) { '/t:Rebuild' } else { '/t:Build' }
Write-Host "Building tmdq_core ($Configuration, x64) ..."
$prev = $ErrorActionPreference
$ErrorActionPreference = 'Continue'   # 5.1 turns native stderr into a terminating error
& msbuild (Join-Path $root 'native\tmdq_core\tmdq_core.vcxproj') $target /m /nologo /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64 2>&1 |
    Tee-Object -FilePath $log | Out-Host
$code = $LASTEXITCODE
$ErrorActionPreference = $prev
if ($code -ne 0) { throw "msbuild failed ($code); see $log" }
if (-not (Test-Path $dll)) { throw "msbuild reported success but $dll was not produced" }

# --- zero warnings -----------------------------------------------------------------------------------
$warnings = @(Select-String -Path $log -Pattern 'warning [A-Z]+\d+')
if ($warnings.Count -gt 0) {
    $warnings | Select-Object -First 10 | ForEach-Object { Write-Host "  $($_.Line)" }
    throw "$($warnings.Count) compiler warning(s); the build must be warning-clean"
}
Write-Host '  0 warnings'

# --- imports: an allow-list ----------------------------------------------------------------------------
$allowed = '^(KERNEL32|WS2_32|USER32|VCRUNTIME140D?(_1D?)?|MSVCP140D?|ucrtbased|api-ms-win-crt-[a-z0-9-]+-l\d+-\d+-\d+)\.dll$'
$deps = @(& dumpbin /nologo /dependents $dll 2>&1 | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() })
if ($deps.Count -eq 0) { throw 'dumpbin listed no imports; cannot verify the DLL' }
$unexpected = @($deps | Where-Object { $_ -notmatch $allowed })
Write-Host '  imports:'
$deps | ForEach-Object { Write-Host "    $_" }
if ($unexpected.Count -gt 0) {
    throw "tmdq_core.dll imports something outside the allow-list: $($unexpected -join ', '). If it is deliberate, add it to `$allowed in scripts\build_core_dll.ps1."
}
Write-Host ("  {0}  {1:N0} bytes - no Qt, imports are on the allow-list" -f $dll, (Get-Item $dll).Length)
