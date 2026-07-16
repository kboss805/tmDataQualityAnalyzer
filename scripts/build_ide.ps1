# =============================================================================
# build_ide.ps1  —  Build the test suite from within VS Code / IDE terminals
#
# Sets up the MSVC toolchain + Qt via env.ps1, then builds the test suite.
#
# Usage (from the project root or any subdirectory):
#   powershell -ExecutionPolicy Bypass -File scripts\build_ide.ps1
# =============================================================================

# Resolve paths relative to this script's location so it works on any machine
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir

# env.ps1 is the single source of truth for the MSVC toolchain, QTDIR/QT_VERSION
# and PATH.
. (Join-Path $ScriptDir 'env.ps1')

Set-Location (Join-Path $ProjectDir 'tests')

# nmake has no parallel mode, so prefer jom if it is on PATH.
if (Get-Command jom -ErrorAction SilentlyContinue) {
    & jom -f Makefile.Debug
}
else {
    & nmake -f Makefile.Debug
}
Write-Host "Tests exit code: $LASTEXITCODE"
