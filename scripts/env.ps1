# =============================================================================
# env.ps1  —  Shared environment configuration for tmDataQualityAnalyzer build scripts.
# QTDIR and MINGW_DIR are the single source of truth for all tool paths.
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
#   $env:QTDIR = "D:\Qt\6.10.3\mingw_64"
#   . "$PSScriptRoot\env.ps1"
# =============================================================================

if (-not $env:QTDIR)      { $env:QTDIR      = 'C:\Qt\6.10.3\mingw_64' }
if (-not $env:MINGW_DIR)  { $env:MINGW_DIR  = 'C:\Qt\Tools\mingw1310_64' }
if (-not $env:WINSDK_BIN) { $env:WINSDK_BIN = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64' }

$env:PATH = "$env:QTDIR\bin;$env:MINGW_DIR\bin;$env:WINSDK_BIN;$env:PATH"
