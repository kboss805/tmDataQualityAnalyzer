# =============================================================================
# setup-env.ps1  —  Developer environment setup (QTDIR registration)
#
# Registers QTDIR as a permanent Windows user environment variable, so VS Code can
# expand ${env:QTDIR} in its config files. (The MSVC compiler comes from vcvars —
# imported by env.ps1 at build time — so no compiler path is registered here.)
#
# RUN THIS AGAIN AFTER EVERY Qt VERSION BUMP. The value is a snapshot, and it is read
# by tools that never source env.ps1: cpptools resolves ${env:QTDIR} in
# .vscode/c_cpp_properties.json, as do Qt Creator and the Qt VS Tools. A stale one
# survives a QT_VERSION change and points them at a kit that may no longer exist —
# which is what caused a 0xC0000135 DLL-not-found after the 6.11.1 upgrade. env.ps1
# warns when the persistent value has drifted, and points back here.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\setup-env.ps1
#
# To override the default, pass -QtDir, or set QT_VERSION / QT_ROOT before running
# (see scripts\env.ps1, the single source of truth for these defaults):
#   $env:QT_VERSION = "6.12.0"
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
