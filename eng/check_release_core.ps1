<#
.SYNOPSIS
    Fails when tmdq_core.dll links the debug C runtime, which is what a Debug build looks like from outside.

.DESCRIPTION
    The package must carry the Release core. A Debug core is unoptimised: it processed the full PRN recording's first
    stream in about 14 s where the Release core takes 4.35 s (docs/changelog.md, PR #264), and nothing about it looks
    wrong until someone times it.

    Nothing else in the release build said so. build_core_dll.ps1's import allow-list admits the debug runtime
    (VCRUNTIME140D, MSVCP140D, ucrtbased), so a Debug DLL passes it; and the step that copies the C++ runtime beside the
    DLL would stop on one, but with a message about a file missing from the redist folder, not about the build.

    A Debug build links the debug runtime and a Release build never does, so the DLL's own import list tells them
    apart. The names refused, in any case:
        VCRUNTIME140D.dll, VCRUNTIME140_1D.dll, MSVCP140D.dll, CONCRT140D.dll, VCOMP140D.dll, ucrtbased.dll

    DESIGN NOTE - this check must be able to FAIL. A DLL that lists no imports at all is a failure ("cannot verify"),
    not a pass: a check that finds nothing to look at must not read as a clean result.

    deploy\build_package.ps1 runs it on the core straight after building it (so a mistake costs seconds, not the
    packaging run) and again on the copy that is staged to ship. It is also runnable alone.

    Works under Windows PowerShell 5.1 and PowerShell 7. Exit code: 0 when the core is clean, 1 when it imports the
    debug runtime or cannot be checked, 2 for a bad invocation.

    Usage:
        powershell -ExecutionPolicy Bypass -File eng\check_release_core.ps1 -Dll out\Release\tmdq_core.dll
        powershell -ExecutionPolicy Bypass -File eng\check_release_core.ps1 -Imports MSVCP140.dll,VCRUNTIME140.dll
    -Imports takes the import list itself, comma separated, in place of reading it from a DLL (the tests use it).
#>
param(
    [string]$Dll,
    [string]$Imports
)
$ErrorActionPreference = 'Stop'

if ((-not $Dll -and -not $PSBoundParameters.ContainsKey('Imports')) -or ($Dll -and $PSBoundParameters.ContainsKey('Imports'))) {
    Write-Host 'usage: check_release_core.ps1 -Dll <path\tmdq_core.dll>   or   -Imports <name>,<name>,...'
    exit 2
}

if ($Dll) {
    if (-not (Test-Path -LiteralPath $Dll -PathType Leaf)) {
        Write-Host "cannot check $Dll : there is no such file"
        exit 1
    }
    if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) {
        . (Join-Path $PSScriptRoot 'env.ps1') *> $null
    }
    if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) {
        Write-Host 'dumpbin not found (it ships with the MSVC tools; use a developer prompt or eng\env.ps1)'
        exit 1
    }
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'   # 5.1 turns native stderr into a terminating error
    $names = @(& dumpbin /nologo /dependents $Dll 2>&1 | Where-Object { "$_" -match '^\s+\S+\.dll\s*$' } | ForEach-Object { "$_".Trim() })
    $ErrorActionPreference = $prev
    $what = $Dll
}
else {
    # Not $imports: PowerShell variables ignore case, so that would be the [string] parameter, and an array assigned to
    # it collapses to one string (every list then "had one import", and an empty one passed).
    $names = @($Imports -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $what = 'the import list'
}

if ($names.Count -eq 0) {
    Write-Host "cannot verify $what : it lists no imports, so there is nothing to check"
    exit 1
}

# -match ignores case, which is what is wanted: a name is not ours to expect in any one case. (An inline (?i) here would
# make it case-insensitive under -cmatch too, and hide the one switch that matters.)
$debugRuntime = '^(?:(?:VCRUNTIME|MSVCP|CONCRT|VCCORLIB|VCOMP)\d+(?:d(?:_\d+)?|(?:_\d+)?d)|ucrtbased)\.dll$'
$found = @($names | Where-Object { $_ -match $debugRuntime })
if ($found.Count -gt 0) {
    Write-Host ("tmdq_core.dll imports the debug C runtime ({0}): this is a Debug build, which is about three times slower than Release." -f ($found -join ', '))
    Write-Host 'Build the core with  eng\build_core_dll.ps1 -Configuration Release  and package that one.'
    exit 1
}

Write-Host ("  OK: {0} imports no debug runtime ({1} imports checked)" -f $what, $names.Count)
exit 0
