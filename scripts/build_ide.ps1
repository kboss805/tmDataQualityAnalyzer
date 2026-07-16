# =============================================================================
# build_ide.ps1  —  Build the test suite from within VS Code / IDE terminals
#
# Reads QTDIR and MINGW_DIR from the Windows user environment (set once by
# setup-env.ps1), falling back to env.ps1's defaults if neither is set.
#
# Usage (from the project root or any subdirectory):
#   powershell -ExecutionPolicy Bypass -File scripts\build_ide.ps1
# =============================================================================

# Resolve paths relative to this script's location so it works on any machine
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir

# env.ps1 is the single source of truth for QTDIR/MINGW_DIR/QT_VERSION and PATH
. (Join-Path $ScriptDir 'env.ps1')

Set-Location (Join-Path $ProjectDir 'tests')
& "$env:MINGW_DIR\bin\mingw32-make.exe" -f Makefile.Debug
Write-Host "Tests exit code: $LASTEXITCODE"
