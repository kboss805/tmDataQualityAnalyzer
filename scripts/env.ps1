# =============================================================================
# env.ps1  —  Shared environment configuration for tmDataQualityAnalyzer builds.
#
# QT_VERSION is the single source of truth for the installed Qt version. QT_ROOT
# (default C:\Qt\<QT_VERSION>) is the knob for a relocated Qt install; QTDIR is
# derived from it plus the MSVC kit.
#
# Imports the Visual Studio 2022 C++ environment (cl / nmake / Windows SDK) from
# vcvars64.bat — located via vswhere, then a direct path probe — and puts the Qt
# msvc2022_64 kit on PATH. Dot-source it before building:
#   . "$PSScriptRoot\env.ps1"
# =============================================================================

if (-not $env:QT_VERSION) { $env:QT_VERSION = '6.10.3' }
if (-not $env:QT_ROOT)    { $env:QT_ROOT    = "C:\Qt\$env:QT_VERSION" }

$env:QTDIR = "$env:QT_ROOT\msvc2022_64"

# Import the MSVC + Windows SDK environment from vcvars64.bat, once per shell.
# Guard on our OWN marker: a Developer-shell profile can set VSCMD_VER while leaving
# the env incomplete (no INCLUDE/LIB), which makes qmake fail with
# "QMAKE_MSC_VER isn't set". Always run a full import once.
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
              "(workload 'Desktop development with C++'), or set `$env:TMDQ_VCVARS to its vcvars64.bat."
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

# --- Parallel make (optional) ------------------------------------------------
# nmake has no -j and builds on a single core. jom is Qt's drop-in parallel
# replacement (same -f syntax, defaults to one job per core). Everything that builds
# should invoke $env:TMDQ_MAKE rather than naming a tool, so a machine without jom -
# notably the GitHub-hosted CI runner - transparently falls back to nmake instead of
# failing. Set TMDQ_JOM to point at a jom.exe in a non-standard location.
if (-not $env:TMDQ_JOM) {
    $qtTools = Join-Path (Split-Path -Parent $env:QT_ROOT) 'Tools'
    foreach ($candidate in @(
        (Join-Path $qtTools 'jom\jom.exe'),
        (Join-Path $qtTools 'QtCreator\bin\jom\jom.exe')
    )) {
        if (Test-Path $candidate) { $env:TMDQ_JOM = $candidate; break }
    }
}

if ($env:TMDQ_JOM -and (Test-Path $env:TMDQ_JOM)) {
    $env:PATH = "$(Split-Path -Parent $env:TMDQ_JOM);$env:PATH"
    $env:TMDQ_MAKE = 'jom'
} elseif (Get-Command jom -ErrorAction SilentlyContinue) {
    $env:TMDQ_MAKE = 'jom'
} else {
    $env:TMDQ_MAKE = 'nmake'
}
