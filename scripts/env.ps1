# =============================================================================
# env.ps1  —  Shared environment configuration for tmDataQualityAnalyzer build scripts.
# QT_VERSION is the single source of truth for the installed Qt version — bump
# it here and QTDIR (and every script/doc that dot-sources or reads this file)
# picks it up. QTDIR and MINGW_DIR are, in turn, the single source of truth for
# all tool paths.
#
# These same variable names are used in:
#   .vscode\launch.json           (${env:QTDIR}, ${env:MINGW_DIR})
#   .vscode\settings.json         (${env:MINGW_DIR})
#   .vscode\c_cpp_properties.json (${env:QTDIR}, ${env:MINGW_DIR})
#
# For VS Code to expand them, register them as Windows user environment
# variables once by running:  powershell -File scripts\setup-env.ps1
#
# Dot-source this file to set up PATH in the current session:
#   . "$PSScriptRoot\env.ps1"
#
# Each variable can be overridden by setting it before dot-sourcing:
#   $env:QT_VERSION = "6.11.0"
#   . "$PSScriptRoot\env.ps1"
# =============================================================================

if (-not $env:QT_VERSION) { $env:QT_VERSION = '6.10.3' }

if (-not $env:QTDIR)      { $env:QTDIR      = "C:\Qt\$env:QT_VERSION\mingw_64" }
if (-not $env:MINGW_DIR)  { $env:MINGW_DIR  = 'C:\Qt\Tools\mingw1310_64' }
if (-not $env:WINSDK_BIN) { $env:WINSDK_BIN = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64' }

$env:PATH = "$env:QTDIR\bin;$env:MINGW_DIR\bin;$env:WINSDK_BIN;$env:PATH"
