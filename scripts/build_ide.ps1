# =============================================================================
# build_ide.ps1  —  Build the test suite from within VS Code / IDE terminals
#
# Reads QTDIR and MINGW_DIR from the Windows user environment (set once by
# setup-env.ps1).  Falls back to defaults if neither is set.
#
# Usage (from the project root or any subdirectory):
#   powershell -ExecutionPolicy Bypass -File scripts\build_ide.ps1
# =============================================================================

# Resolve paths relative to this script's location so it works on any machine
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir

# Use environment variables if already set; otherwise fall back to defaults
if (-not $env:QTDIR)     { $env:QTDIR     = 'C:\Qt\6.10.2\mingw_64' }
if (-not $env:MINGW_DIR) { $env:MINGW_DIR = 'C:\Qt\Tools\mingw1310_64' }

$env:PATH = "$env:QTDIR\bin;$env:MINGW_DIR\bin;$env:PATH"

Set-Location (Join-Path $ProjectDir 'tests')
& "$env:MINGW_DIR\bin\mingw32-make.exe" -f Makefile.Debug
Write-Host "Tests exit code: $LASTEXITCODE"
