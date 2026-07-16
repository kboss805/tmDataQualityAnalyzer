# =============================================================================
# setup-env.ps1  —  One-time developer environment setup
#
# Run this once on each machine to register QTDIR as a permanent Windows user
# environment variable. After running, VS Code will expand ${env:QTDIR} in its
# config files automatically. (The MSVC compiler comes from vcvars — imported by
# env.ps1 at build time — so no compiler path is registered here.)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\setup-env.ps1
#
# To override the default, pass -QtDir, or set QT_VERSION / QT_ROOT before running
# (see scripts\env.ps1, the single source of truth for these defaults):
#   $env:QT_VERSION = "6.11.0"
#   powershell -ExecutionPolicy Bypass -File scripts\setup-env.ps1
# =============================================================================

param (
    [string]$QtDir
)

# Fall back to env.ps1's default (derived from QT_VERSION) when -QtDir isn't
# supplied, so the version lives in exactly one place.
. (Join-Path $PSScriptRoot 'env.ps1')
if (-not $QtDir) { $QtDir = $env:QTDIR }

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

Set-UserEnvVar "QTDIR" $QtDir

Write-Host ""
Write-Host "Done. Restart VS Code (and any open terminals) for the changes to take effect."
Write-Host ""
Write-Host "To verify:"
Write-Host "  [System.Environment]::GetEnvironmentVariable('QTDIR', 'User')"
Write-Host ""
