# =============================================================================
# setup-env.ps1  —  One-time developer environment setup
#
# Run this once on each machine to register QTDIR and MINGW_DIR as permanent
# Windows user environment variables.  After running, VS Code will expand
# ${env:QTDIR} and ${env:MINGW_DIR} in launch.json, settings.json, and
# c_cpp_properties.json automatically.
#
# Note: WINSDK_BIN is intentionally NOT registered here.  It is only needed
# at build time (added to PATH by env.ps1) and is not referenced by VS Code
# config files, so it does not need to live in the Windows user environment.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\setup-env.ps1
#
# To override the defaults, pass them as arguments, or set QT_VERSION/QTDIR/
# MINGW_DIR before running (see scripts\env.ps1, the single source of truth
# for these defaults):
#   $env:QT_VERSION = "6.11.0"
#   powershell -ExecutionPolicy Bypass -File scripts\setup-env.ps1
# =============================================================================

param (
    [string]$QtDir,
    [string]$MingwDir
)

# Fall back to env.ps1's defaults (derived from QT_VERSION) for any param the
# caller didn't supply, so the version lives in exactly one place.
. (Join-Path $PSScriptRoot 'env.ps1')
if (-not $QtDir)    { $QtDir    = $env:QTDIR }
if (-not $MingwDir) { $MingwDir = $env:MINGW_DIR }

function Set-UserEnvVar {
    param([string]$Name, [string]$Value)
    [System.Environment]::SetEnvironmentVariable($Name, $Value, "User")
    Write-Host "  Set $Name = $Value"
}

Write-Host ""
Write-Host "tmDataQualityAnalyzer — Developer Environment Setup"
Write-Host "============================================"
Write-Host ""
Write-Host "Registering user environment variables..."

Set-UserEnvVar "QTDIR"     $QtDir
Set-UserEnvVar "MINGW_DIR" $MingwDir

Write-Host ""
Write-Host "Done. Restart VS Code (and any open terminals) for the changes to take effect."
Write-Host ""
Write-Host "To verify:"
Write-Host "  [System.Environment]::GetEnvironmentVariable('QTDIR', 'User')"
Write-Host "  [System.Environment]::GetEnvironmentVariable('MINGW_DIR', 'User')"
Write-Host ""
