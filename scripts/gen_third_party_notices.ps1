<#
.SYNOPSIS
    Writes THIRD-PARTY-NOTICES.txt for the Avalonia application from what its publish actually ships.

.DESCRIPTION
    The app is GPL-3.0 and ships third-party code under MIT and BSD licences, all of which require their copyright
    and licence text to travel with the binaries. This builds that file from the publish's own dependency file
    (obj\<cfg>\<tfm>\<rid>\<app>.deps.json, which lists every package and the runtime pack whose files the publish
    contains, single-file or not), so it cannot drift from the package list:

      - every package with files in the runtime target, and the .NET runtime pack;
      - for each, its licence: the package's own licence file when it carries one, else the standard text of its
        SPDX expression filled with the copyright line the package declares (only MIT is filled in that way; any
        other expression without a licence file is an error, so a new licence is never summarised silently);
      - every third-party notices file a package carries (THIRD-PARTY-NOTICES*, ThirdPartyNotices*), once each
        however many packages carry the same one;
      - the native components this repository compiles into tmdq_core.dll (the irig106 library), from its headers.

    The project's own licence (GPL-3.0) ships beside it as LICENSE.txt and is not repeated here.

    Works under Windows PowerShell 5.1 and PowerShell 7.

    Usage:
        powershell -ExecutionPolicy Bypass -File scripts\gen_third_party_notices.ps1 -DepsFile <deps.json> -Out <file>
                   [-PackagesFolder <folder>] [-NoNativeCore]
#>
param(
    [Parameter(Mandatory = $true)] [string]$DepsFile,
    [Parameter(Mandatory = $true)] [string]$Out,
    [string]$PackagesFolder = $(if ($env:NUGET_PACKAGES) { $env:NUGET_PACKAGES } else { Join-Path $env:USERPROFILE '.nuget\packages' }),
    # Leave out the irig106 notice (for a package that does not ship tmdq_core.dll).
    [switch]$NoNativeCore
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$MitText = @'
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
'@

$Rule = '=' * 100

function Read-Text([string]$path) { return ([System.IO.File]::ReadAllText($path)).Trim() }

if (-not (Test-Path -LiteralPath $DepsFile)) { throw "no deps file at $DepsFile (publish the app first)" }
$deps = Get-Content -LiteralPath $DepsFile -Raw -Encoding UTF8 | ConvertFrom-Json
$target = $deps.targets.($deps.runtimeTarget.name)
if (-not $target) { throw "$DepsFile has no runtime target '$($deps.runtimeTarget.name)'" }

# --- What ships: every package or runtime pack with runtime or native files in the runtime target. ---
$shipped = New-Object System.Collections.Generic.List[object]
foreach ($lib in $target.PSObject.Properties) {
    $files = @()
    foreach ($section in 'runtime', 'native') {
        if ($lib.Value.$section) { $files += @($lib.Value.$section.PSObject.Properties.Name) }
    }
    $files = @($files | Where-Object { $_ -match '\.(dll|exe)$' })
    if ($files.Count -eq 0) { continue }
    $info = $deps.libraries.($lib.Name)
    if ($info.type -notin 'package', 'runtimepack') { continue }   # the app's own projects
    $id, $version = $lib.Name -split '/', 2
    $id = $id -replace '^runtimepack\.', ''
    $shipped.Add([pscustomobject]@{ Id = $id; Version = $version; Files = $files.Count })
}
if ($shipped.Count -eq 0) { throw "$DepsFile lists no third-party files: is it the publish deps file?" }

# --- Each package's licence and notices. ---
$sections = [ordered]@{}        # licence text -> list of "Id Version" sharing it, plus its header lines
$notices = [ordered]@{}         # notices text -> list of packages carrying it
$summary = New-Object System.Collections.Generic.List[string]
foreach ($p in ($shipped | Sort-Object Id)) {
    $dir = Join-Path $PackagesFolder (Join-Path $p.Id.ToLowerInvariant() $p.Version.ToLowerInvariant())
    $nuspecPath = Join-Path $dir ($p.Id.ToLowerInvariant() + '.nuspec')
    if (-not (Test-Path -LiteralPath $dir)) { throw "$($p.Id) $($p.Version): not found under $PackagesFolder (restore first)" }

    $licence = ''; $copyright = ''; $url = ''
    if (Test-Path -LiteralPath $nuspecPath) {
        [xml]$x = Get-Content -LiteralPath $nuspecPath -Raw -Encoding UTF8
        $m = $x.package.metadata
        if ($m.license) { $licence = if ($m.license.type -eq 'expression') { $m.license.'#text' } else { 'see licence text' } }
        $copyright = (($m.copyright + '') -replace '\s+', ' ').Trim()
        $url = (($m.projectUrl + '') -replace '\?.*$', '').Trim()   # no tracking query strings
    }

    # The package's own licence file wins over a template.
    $licenceFile = Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Name -match '^(LICENSE|LICENCE|License)(\.(txt|md))?$' } | Select-Object -First 1
    if ($licenceFile) {
        $body = Read-Text $licenceFile.FullName
        if (-not $licence) { $licence = 'see licence text' }
    } elseif ($licence -eq 'MIT') {
        if (-not $copyright) { throw "$($p.Id) $($p.Version): MIT without a licence file or a copyright line to fill it with" }
        $line = if ($copyright -match '^(Copyright|\(c\)|©)') { $copyright } else { "Copyright (c) $copyright" }
        $body = "MIT License`r`n`r`n$line`r`n`r`n$MitText"
    } else {
        throw "$($p.Id) $($p.Version): licence '$licence' has no licence file to quote and no template here; add one deliberately"
    }
    if (-not $sections.Contains($body)) { $sections[$body] = New-Object System.Collections.Generic.List[string] }
    $sections[$body].Add(("{0} {1}" -f $p.Id, $p.Version) + $(if ($url) { "  ($url)" } else { '' }))
    $summary.Add(('  {0,-44} {1,-20} {2}' -f $p.Id, $p.Version, $licence))

    foreach ($n in (Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Name -match '^(THIRD-PARTY-NOTICES|ThirdPartyNotices)' })) {
        $text = Read-Text $n.FullName
        if (-not $notices.Contains($text)) { $notices[$text] = New-Object System.Collections.Generic.List[string] }
        $notices[$text].Add("$($p.Id) $($p.Version)")
    }
}

# --- The native core's vendored library, quoted from its own header. ---
$native = ''
if (-not $NoNativeCore) {
    $header = Join-Path $root 'lib\irig106\include\config.h'
    $lines = [System.IO.File]::ReadAllLines($header)
    $end = [Array]::FindIndex($lines, [Predicate[string]] { param($l) $l -match '^\s*\*{10,}/\s*$' })
    if ($end -lt 0) { throw "cannot find the end of the licence comment in $header" }
    $comment = ($lines[1..($end - 1)] | ForEach-Object { $_.TrimEnd() }) -join "`r`n"
    $licenceStart = $comment.IndexOf('Copyright')
    if ($licenceStart -lt 0 -or $comment -notmatch 'Redistribution and use in source and binary forms') {
        throw "the irig106 header no longer carries the licence this script expects: read $header"
    }
    $native = $comment.Substring($licenceStart).Trim()
    $summary.Add(('  {0,-44} {1,-20} {2}' -f 'irig106 library (in tmdq_core.dll)', '', 'BSD-3-Clause'))
}

# --- Write it. ---
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('TM Data Quality Analyzer - third-party notices')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('TM Data Quality Analyzer is free software under the GNU General Public License v3.0 (see LICENSE.txt).')
[void]$sb.AppendLine('It includes the third-party software listed below. Each is used under its own licence, reproduced in full')
[void]$sb.AppendLine('in this file together with the copyright notices it requires.')
[void]$sb.AppendLine('')
foreach ($l in $summary) { [void]$sb.AppendLine($l) }
foreach ($body in $sections.Keys) {
    [void]$sb.AppendLine(''); [void]$sb.AppendLine($Rule)
    foreach ($who in $sections[$body]) { [void]$sb.AppendLine($who) }
    [void]$sb.AppendLine($Rule); [void]$sb.AppendLine(''); [void]$sb.AppendLine($body)
}
if ($native) {
    [void]$sb.AppendLine(''); [void]$sb.AppendLine($Rule)
    [void]$sb.AppendLine('irig106 library, compiled into tmdq_core.dll  (http://www.irig106.org)')
    [void]$sb.AppendLine($Rule); [void]$sb.AppendLine(''); [void]$sb.AppendLine($native)
}
foreach ($text in $notices.Keys) {
    [void]$sb.AppendLine(''); [void]$sb.AppendLine($Rule)
    [void]$sb.AppendLine('Third-party notices carried by ' + ($notices[$text] -join ', '))
    [void]$sb.AppendLine($Rule); [void]$sb.AppendLine(''); [void]$sb.AppendLine($text)
}
$dirOut = Split-Path -Parent $Out
if ($dirOut -and -not (Test-Path $dirOut)) { New-Item -ItemType Directory -Force -Path $dirOut | Out-Null }
# UTF-8 with a byte-order mark, so Notepad shows the copyright signs correctly.
[System.IO.File]::WriteAllText($Out, $sb.ToString(), (New-Object System.Text.UTF8Encoding($true)))
Write-Output ("Third-party notices: {0} packages, {1} licence texts, {2} notices files{3} -> {4}" -f `
    $shipped.Count, $sections.Count, $notices.Count, $(if ($native) { ', irig106' } else { '' }), $Out)
