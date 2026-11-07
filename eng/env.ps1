# =============================================================================
# env.ps1  —  Shared environment configuration for tmDataQualityAnalyzer builds.
#
# Imports the Visual Studio 2022 C++ environment (cl, msbuild, mt, dumpbin, the Windows
# SDK) from vcvars64.bat, located via vswhere, then a direct path probe. The build and
# test scripts dot-source it when msbuild or mt is not already on PATH:
#   . "$PSScriptRoot\env.ps1"
#
# The toolset is PINNED to Visual Studio 2022 (17.x, MSVC v143). vswhere -latest used to
# pick whatever was newest, so installing Visual Studio 2026 silently moved every build
# to MSVC 14.51 (v145) while the docs and CI all said 2022. Objects compiled by two
# compilers then sit in one build tree. Set TMDQ_VS_RANGE (a vswhere version range, e.g.
# '[18.0,19.0)') to choose another on purpose.
#
# vcvars sets Platform=x64, which a .NET build then takes for a solution platform that
# does not exist; the scripts that run dotnet clear it after sourcing this one.
# =============================================================================

# Import the MSVC + Windows SDK environment from vcvars64.bat, once per shell.
# Guard on our OWN marker: a Developer-shell profile can set VSCMD_VER while leaving
# the env incomplete (no INCLUDE/LIB). Always run a full import once.
if (-not $env:TMDQ_VCVARS_DONE) {
    $vcvars = $env:TMDQ_VCVARS  # explicit override wins
    $vsRange = if ($env:TMDQ_VS_RANGE) { $env:TMDQ_VS_RANGE } else { '[17.0,18.0)' }

    # 1) vswhere, if the VS Installer shipped it at the standard location.
    if (-not $vcvars) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -latest -products * -version $vsRange `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath 2>$null | Select-Object -First 1
            if ($vsPath) {
                $candidate = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
                if (Test-Path $candidate) { $vcvars = $candidate }
            }
        }
    }

    # 2) Fallback: probe the known 2022 install roots/editions directly. Build
    #    Tools installs don't always ship vswhere in the Installer dir. Skipped when
    #    TMDQ_VS_RANGE asks for something else, so a failed lookup cannot quietly hand
    #    back 2022 to a caller that wanted 2026.
    if (-not $vcvars -and -not $env:TMDQ_VS_RANGE) {
        foreach ($pf in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
            foreach ($ed in @('BuildTools','Community','Professional','Enterprise')) {
                $candidate = Join-Path $pf "Microsoft Visual Studio\2022\$ed\VC\Auxiliary\Build\vcvars64.bat"
                if (Test-Path $candidate) { $vcvars = $candidate; break }
            }
            if ($vcvars) { break }
        }
    }

    if (-not $vcvars -or -not (Test-Path $vcvars)) {
        throw "vcvars64.bat not found for Visual Studio version range $vsRange. Install the " +
              "Visual Studio 2022 C++ Build Tools (workload 'Desktop development with C++'), " +
              "or set `$env:TMDQ_VCVARS to a vcvars64.bat / `$env:TMDQ_VS_RANGE to another range."
    }

    # Run vcvars in cmd and copy the resulting environment into this session.
    #
    # Its stderr is discarded on purpose. vcvars64.bat invokes `vswhere.exe` by bare
    # name, so on a machine where vswhere exists but is not on PATH it prints
    #   'vswhere.exe' is not recognized as an internal or external command
    # and carries on with its own fallbacks. The import is fine - but that line was
    # printed by every script that dot-sources this one, and output nobody can act on
    # is how output stops being read at all. It is Microsoft's script, not ours to fix.
    cmd /c "`"$vcvars`" 2>nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
        }
    }

    # Judge the import by its RESULT, now that its chatter is gone. A PARTIAL import is
    # the failure that matters and it is otherwise silent: the compiler fails much later,
    # with a message that names neither vcvars nor this script. INCLUDE and LIB are what
    # the compiler cannot work without.
    if (-not $env:INCLUDE -or -not $env:LIB) {
        Write-Host "vcvars64.bat left INCLUDE/LIB unset - re-running it with its output shown:"
        # 2>&1 inside cmd: under Windows PowerShell 5.1 its stderr would otherwise become a terminating error here,
        # before the message below could say what went wrong.
        cmd /c "`"$vcvars`" 2>&1" | ForEach-Object { Write-Host $_ }
        throw "vcvars64.bat did not produce a usable build environment. Ran: $vcvars"
    }

    $env:TMDQ_VCVARS_DONE = '1'
}
