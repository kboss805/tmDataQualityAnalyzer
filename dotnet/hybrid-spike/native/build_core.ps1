<#
.SYNOPSIS
    Builds tmdq_core.dll - the decommutation core behind the flat C ABI - with NO Qt.

.DESCRIPTION
    Plain cl + link. No qmake, no Qt include path, no Qt libraries: if any file reached for a Qt
    header this would fail to compile, which is the point. It is the build-level proof of what the
    de-Qt sweep set out to achieve, and the thing the interop spike could not show (its DLL still
    linked Qt Core).

    Output goes to native\out\. Afterwards it reports the DLL's imports, so "no Qt" is a printed
    fact and not a claim.

    Run it under Windows PowerShell 5.1, as the project's other scripts are:
        powershell -ExecutionPolicy Bypass -File dotnet\hybrid-spike\native\build_core.ps1
#>
param(
    [switch]$Debug
)
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
. (Join-Path $root 'scripts\env.ps1') *> $null

$out = Join-Path $PSScriptRoot 'out'
$obj = Join-Path $out 'obj'
New-Item -ItemType Directory -Force $obj | Out-Null
Remove-Item (Join-Path $obj '*.obj') -ErrorAction SilentlyContinue

$includes = @('include', 'include\dto', 'include\model', 'lib\irig106\include', 'dotnet\interop-spike') |
    ForEach-Object { '/I' + (Join-Path $root $_) }

$opt = if ($Debug) { @('/Od', '/Zi', '/MDd') } else { @('/O2', '/MD', '/DNDEBUG') }

# The core, exactly the sources the Qt application's decode pipeline is built from.
$coreSources = @(
    'channeldata', 'chapter10reader', 'ch10packetreader', 'framesetup', 'frameprocessor',
    'stepdetector', 'tomldocument', 'textfile', 'calibrationextractor'
) | ForEach-Object { Join-Path $root "src\model\$_.cpp" }
$abi = Join-Path $PSScriptRoot 'tmdq_core.cpp'

Write-Host "Compiling the core and the ABI (C++20, no Qt)..."
$cppFlags = @('/nologo', '/c', '/std:c++20', '/EHsc', '/utf-8', '/W3', '/DTMDQ_ABI_BUILD') + $opt
$warnings = 0
foreach ($src in ($coreSources + $abi)) {
    $name = [IO.Path]::GetFileName($src)
    $log = & cl @cppFlags @includes "/Fo$obj\" $src 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { Write-Host $log; throw "compile failed: $name" }
    $warnings += ([regex]::Matches($log, 'warning C\d+')).Count
}

Write-Host "Compiling the vendored irig106 C library..."
$cSources = Get-ChildItem (Join-Path $root 'lib\irig106\src') -Filter *.c | Where-Object { $_.Name -match '^(irig106ch10|i106_time|i106_data_stream|i106_decode_time|i106_decode_tmats.*|i106_decode_pcmf1)\.c$' }
foreach ($c in $cSources) {
    # Third-party code: compiled quietly, as thirdparty.pri does for the Qt build.
    $log = & cl /nologo /c /TC /W0 @opt @includes "/Fo$obj\" $c.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { Write-Host $log; throw "compile failed: $($c.Name)" }
}

$dll = Join-Path $out 'tmdq_core.dll'
Write-Host "Linking $dll ..."
$linkFlags = @('/nologo', '/DLL', "/OUT:$dll", "/IMPLIB:$(Join-Path $out 'tmdq_core.lib')")
if ($Debug) { $linkFlags += '/DEBUG' }
& link @linkFlags (Get-ChildItem $obj -Filter *.obj | ForEach-Object { $_.FullName }) ws2_32.lib user32.lib 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'link failed' }

"{0} warnings in our code" -f $warnings
$size = (Get-Item $dll).Length
"{0}  {1:N0} bytes" -f $dll, $size

Write-Host ""
Write-Host "Imports (dumpbin /dependents):"
$deps = & dumpbin /nologo /dependents $dll 2>&1 | Where-Object { $_ -match '\.dll\s*$' } | ForEach-Object { $_.Trim() }
$deps | ForEach-Object { "  $_" }

$qt = @($deps | Where-Object { $_ -match '^Qt\d' })
if ($qt.Count -gt 0) { throw "the DLL imports Qt: $($qt -join ', ')" }
Write-Host ""
Write-Host "No Qt imports."
