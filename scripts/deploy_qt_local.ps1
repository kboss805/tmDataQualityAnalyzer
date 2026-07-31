# =============================================================================
# deploy_qt_local.ps1 - copy the Qt runtime next to a locally built exe.
#
# WHY THIS EXISTS
#
# Running a freshly built exe needs Qt's DLLs findable at load time. Putting the
# Qt bin directory on PATH works, but every consumer then has to know where Qt
# lives - and an IDE launch config that hard-codes that path silently rots the
# moment QT_VERSION changes. That is exactly what happened after the 6.10.3 ->
# 6.11.1 bump: .vscode/launch.json still pointed at the old kit, and a stale
# user-level QTDIR pointed at a MinGW kit that no longer exists at all, which
# fails at launch with 0xC0000135 (DLL not found) or 0xC0000139 (wrong Qt
# version -> entry point not found).
#
# Deploying the DLLs into the output directory removes the dependency entirely:
# Windows finds them beside the exe, so nothing needs PATH and nothing needs to
# know the Qt path. The only knob stays QT_VERSION in env.ps1.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\deploy_qt_local.ps1 [-Config debug|release]
# =============================================================================

param(
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',
    # The test exe needs the same treatment: it is a separate binary in
    # tests/<config>/ and is launched by the IDE the same way.
    [switch]$Tests
)

$ErrorActionPreference = 'Stop'

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir

. (Join-Path $ScriptDir 'env.ps1')

$exe = if ($Tests) {
    Join-Path $ProjectDir "tests\$Config\tmDataQualityAnalyzer_tests.exe"
} else {
    Join-Path $ProjectDir "build\$Config\tmDataQualityAnalyzer.exe"
}
if (-not (Test-Path $exe)) {
    Write-Host "Nothing to deploy: $exe not built yet."
    exit 0
}

# windeployqt comes from the kit env.ps1 selected, so the deployed DLLs always
# match the Qt the exe was just linked against - the mismatch this script exists
# to prevent cannot recur by pointing at the wrong tool.
$deployed = Join-Path (Split-Path $exe -Parent) 'Qt6Core.dll'
$deployedDebug = Join-Path (Split-Path $exe -Parent) 'Qt6Cored.dll'
if ((Test-Path $deployed) -or (Test-Path $deployedDebug)) {
    # Already deployed and still newer than the kit: skip, so this can be wired
    # as a pre-launch step without adding seconds to every F5.
    $stamp = if (Test-Path $deployedDebug) { $deployedDebug } else { $deployed }
    if ((Get-Item $stamp).LastWriteTime -ge (Get-Item (Join-Path $env:QTDIR 'bin\Qt6Core.dll')).LastWriteTime) {
        Write-Host "Qt runtime already deployed beside $Config exe (from $env:QTDIR)."
        exit 0
    }
}

Write-Host "Deploying Qt runtime from $env:QTDIR next to the $Config exe..."
# NOT $args: that is a PowerShell automatic variable holding unbound arguments,
# and overwriting it is asking for confusing behaviour.
$deployArgs = @('--no-translations', '--no-opengl-sw', '--no-system-d3d-compiler')
if ($Config -eq 'debug') { $deployArgs += '--debug' } else { $deployArgs += '--release' }
& windeployqt @deployArgs $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed ($LASTEXITCODE)" }

# windeployqt deploys only the platform plugin it infers is needed - qwindows -
# and this project also runs HEADLESS (QT_QPA_PLATFORM=offscreen: CI, the batch
# export path, and the IDE's test launch configs). Once the Qt DLLs sit beside the
# exe, Qt resolves plugins from the LOCAL platforms/ directory, so an un-deployed
# offscreen plugin is no longer found on the Qt install either: the process aborts
# at startup with "no Qt platform plugin could be initialized" and exit code 3,
# before a single test runs. Copy it explicitly.
$platformsSrc = Join-Path $env:QTDIR 'plugins\platforms'
$platformsDst = Join-Path (Split-Path $exe -Parent) 'platforms'
New-Item -ItemType Directory -Force -Path $platformsDst | Out-Null
$offscreen = if ($Config -eq 'debug') { 'qoffscreend.dll' } else { 'qoffscreen.dll' }
$src = Join-Path $platformsSrc $offscreen
if (Test-Path $src) {
    Copy-Item $src $platformsDst -Force
    Write-Host "  + $offscreen (headless runs)"
} else {
    Write-Warning "$offscreen not found in $platformsSrc - headless (offscreen) runs will fail."
}

Write-Host "Done - the exe now runs without Qt on PATH."
