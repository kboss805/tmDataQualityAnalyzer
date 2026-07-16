# =============================================================================
# env.ps1  —  Shared environment configuration for tmDataQualityAnalyzer build scripts.
#
# QT_VERSION is the single source of truth for the installed Qt version. QT_ROOT
# (default C:\Qt\<QT_VERSION>) is the single knob for a relocated Qt install; QTDIR
# is derived from it plus the toolchain's kit, so it always matches the compiler.
#
# TMDQ_TOOLCHAIN selects the compiler:
#   'msvc'  (default) — Visual Studio 2022 C++ (cl / nmake), Qt msvc2022_64 kit.
#                       The MSVC + Windows SDK environment is imported from
#                       vcvars64.bat (located via vswhere, then a path probe).
#   'mingw'           — the legacy GCC 13.1.0 toolchain, Qt mingw_64 kit.
# Override the toolchain before dot-sourcing:
#   $env:TMDQ_TOOLCHAIN = 'mingw'
#   . "$PSScriptRoot\env.ps1"
#
# QTDIR (and, for mingw, MINGW_DIR) are the single source of truth for tool paths.
# These same variable names are used in the .vscode\*.json config files.
# For VS Code to expand them, register them once by running:
#   powershell -File scripts\setup-env.ps1
#
# Dot-source this file to set up PATH in the current session:
#   . "$PSScriptRoot\env.ps1"
# =============================================================================

if (-not $env:QT_VERSION)     { $env:QT_VERSION     = '6.10.3' }
if (-not $env:QT_ROOT)        { $env:QT_ROOT        = "C:\Qt\$env:QT_VERSION" }
if (-not $env:TMDQ_TOOLCHAIN) { $env:TMDQ_TOOLCHAIN = 'msvc' }

if ($env:TMDQ_TOOLCHAIN -eq 'mingw') {
    # ---- Legacy MinGW / GCC toolchain -------------------------------------
    # QTDIR is derived from the toolchain so a value left over from the other kit
    # can't silently point the build at the wrong Qt.
    $env:QTDIR = "$env:QT_ROOT\mingw_64"
    if (-not $env:MINGW_DIR)  { $env:MINGW_DIR  = 'C:\Qt\Tools\mingw1310_64' }
    if (-not $env:WINSDK_BIN) { $env:WINSDK_BIN = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64' }
    $env:PATH = "$env:QTDIR\bin;$env:MINGW_DIR\bin;$env:WINSDK_BIN;$env:PATH"
}
else {
    # ---- MSVC toolchain (default) -----------------------------------------
    $env:QTDIR = "$env:QT_ROOT\msvc2022_64"

    # Import the MSVC + Windows SDK environment from vcvars64.bat, once per shell.
    # Guard on our OWN marker rather than VSCMD_VER: a Developer-shell profile can
    # set VSCMD_VER while leaving the env incomplete (no INCLUDE/LIB), which makes
    # qmake fail with "QMAKE_MSC_VER isn't set". Always run a full import once.
    if (-not $env:TMDQ_VCVARS_DONE) {
        $vcvars = $env:TMDQ_VCVARS  # explicit override wins

        # 1) vswhere, if the VS Installer shipped it at the standard location.
        if (-not $vcvars) {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
            if (Test-Path $vswhere) {
                $vsPath = & $vswhere -latest -products * `
                    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                    -property installationPath 2>$null | Select-Object -First 1
                if ($vsPath) {
                    $candidate = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
                    if (Test-Path $candidate) { $vcvars = $candidate }
                }
            }
        }

        # 2) Fallback: probe the known 2022 install roots/editions directly. Build
        #    Tools installs don't always ship vswhere in the Installer dir.
        if (-not $vcvars) {
            foreach ($pf in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
                foreach ($ed in @('BuildTools','Community','Professional','Enterprise')) {
                    $candidate = Join-Path $pf "Microsoft Visual Studio\2022\$ed\VC\Auxiliary\Build\vcvars64.bat"
                    if (Test-Path $candidate) { $vcvars = $candidate; break }
                }
                if ($vcvars) { break }
            }
        }

        if (-not $vcvars -or -not (Test-Path $vcvars)) {
            throw "vcvars64.bat not found. Install the Visual Studio 2022 C++ Build Tools " +
                  "(workload 'Desktop development with C++'), or set `$env:TMDQ_VCVARS to its " +
                  "vcvars64.bat, or use MinGW with `$env:TMDQ_TOOLCHAIN='mingw'."
        }

        # Run vcvars in cmd and copy the resulting environment into this session.
        cmd /c "`"$vcvars`" && set" | ForEach-Object {
            if ($_ -match '^([^=]+)=(.*)$') {
                [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
            }
        }
        $env:TMDQ_VCVARS_DONE = '1'
    }
    # Put the Qt msvc kit's bin ahead of everything for qmake / Qt DLLs.
    $env:PATH = "$env:QTDIR\bin;$env:PATH"
}
