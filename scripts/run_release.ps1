# =============================================================================
# run_release.ps1 — Build and launch the tmDataQualityAnalyzer Release executable
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\run_release.ps1
# =============================================================================

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir

. (Join-Path $ScriptDir 'env.ps1')

New-Item -ItemType Directory -Force -Path (Join-Path $ProjectDir 'build') | Out-Null
Set-Location (Join-Path $ProjectDir 'build')

Write-Host "Building Release..."
& qmake ..\tmDataQualityAnalyzer.pro -spec win32-msvc CONFIG+=release
if ($LASTEXITCODE -ne 0) { throw "qmake failed" }

# env.ps1 sets TMDQ_MAKE to jom when it's available, else nmake.
Write-Host "Building with $env:TMDQ_MAKE..."
& $env:TMDQ_MAKE -f Makefile.Release
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "Launching tmDataQualityAnalyzer Release..."
Start-Process ".\release\tmDataQualityAnalyzer.exe"
