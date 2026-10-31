<#
.SYNOPSIS
    Fails when a .NET project resolves any NuGet package whose licence is not on the open-source allow-list.

.DESCRIPTION
    Plan decision D5: the app stays GPL-3.0 on the MIT Avalonia framework, and must never reference a proprietary
    component (an Avalonia Accelerate package, say) that a NuGet suggestion could pull in. This check makes that a
    build failure rather than a review comment.

    It reads the project's obj\project.assets.json, which lists every package restore resolved, transitive ones
    included, and reads each package's licence from its own .nuspec in the packages folder restore used:
      - <license type="expression">: an SPDX expression; every alternative of an OR may satisfy it, every term
        of an AND must, and each identifier must be on $Allowed.
      - <licenseUrl> only (older packages): accepted only when it is a licenses.nuget.org URL naming an allowed
        identifier, or the package is on $Reviewed with that exact URL.
      - <license type="file">: accepted only when the package is on $Reviewed with the SHA-256 of that exact
        file, so a later version that changes its licence fails again and needs a fresh review.
      - anything else (no licence, a missing nuspec) fails.

    PackageLicences.targets runs it after restore for every project that imports it; it is also runnable alone.

    Works under Windows PowerShell 5.1 and PowerShell 7.

    Usage:
        powershell -ExecutionPolicy Bypass -File scripts\check_package_licences.ps1 -AssetsFile <path\project.assets.json>
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$AssetsFile
)
$ErrorActionPreference = 'Stop'

# SPDX identifiers an MIT/GPL-3.0 project can ship. Widen it only for a licence that is open source AND
# compatible with distributing the app under GPL-3.0.
$Allowed = @('MIT', 'Apache-2.0', 'BSD-2-Clause', 'BSD-3-Clause')

# Packages whose licence is a file or a bare URL rather than an SPDX expression, each read by hand. Key: the
# lower-case package id. Licence: the SPDX identifier the text was found to be (it must be on $Allowed).
# Sha256: of the licence file as shipped (a file licence). Url: the exact licence URL (a URL-only licence).
$Reviewed = @{
    # ANGLE, Avalonia's OpenGL ES on Direct3D. The file is the ANGLE Project's standard BSD 3-clause text.
    'avalonia.angle.windows.natives' = @{
        Licence = 'BSD-3-Clause'
        Sha256  = '54AFF7276217DF9F6B5181613999D208C9E40D2B1D51BF55217837E6871A4A63'
    }
}

function Fail([string]$message) {
    # MSBuild's Exec task turns the canonical "error CODE: text" form into a build error.
    Write-Output "error TMDQLIC: $message"
}

# Not Get-FileHash: under a build started from PowerShell 7, this Windows PowerShell 5.1 child inherits 7's
# PSModulePath and cannot load the module Get-FileHash lives in.
function Get-Sha256([string]$path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return (($sha.ComputeHash([System.IO.File]::ReadAllBytes($path)) | ForEach-Object { $_.ToString('X2') }) -join '') }
    finally { $sha.Dispose() }
}

function Test-Expression([string]$expression) {
    $text = ($expression -replace '[()]', ' ').Trim()
    if (-not $text) { return $false }
    foreach ($alternative in ($text -split '\s+OR\s+')) {
        $terms = $alternative -split '\s+AND\s+'
        $ok = $true
        foreach ($term in $terms) {
            if ($Allowed -notcontains $term.Trim()) { $ok = $false; break }
        }
        if ($ok) { return $true }
    }
    return $false
}

if (-not (Test-Path -LiteralPath $AssetsFile)) {
    Fail "no assets file at $AssetsFile (restore the project first)"
    exit 1
}
$assets = Get-Content -LiteralPath $AssetsFile -Raw -Encoding UTF8 | ConvertFrom-Json

$folders = @($assets.packageFolders.PSObject.Properties | ForEach-Object { $_.Name })
if ($folders.Count -eq 0) {
    Fail "$AssetsFile names no packages folder"
    exit 1
}

$packages = @($assets.libraries.PSObject.Properties | Where-Object { $_.Value.type -eq 'package' })
$bad = 0
foreach ($package in $packages) {
    $id, $version = $package.Name -split '/', 2
    $relative = $package.Value.path
    if (-not $relative) { $relative = ($id + '/' + $version).ToLowerInvariant() }

    $nuspec = $null
    foreach ($folder in $folders) {
        $candidate = Join-Path (Join-Path $folder $relative) ($id.ToLowerInvariant() + '.nuspec')
        if (Test-Path -LiteralPath $candidate) { $nuspec = $candidate; break }
    }
    if (-not $nuspec) {
        Fail "$id $version`: no .nuspec found, so its licence cannot be checked"
        $bad++
        continue
    }

    [xml]$xml = Get-Content -LiteralPath $nuspec -Raw -Encoding UTF8
    $metadata = $xml.package.metadata
    $licence = $metadata.license
    $review = $Reviewed[$id.ToLowerInvariant()]
    if ($review -and $Allowed -notcontains $review.Licence) {
        Fail "$id $version`: reviewed as '$($review.Licence)', which is not on the allow-list"
        $bad++
        continue
    }
    $verdict = $null
    if ($licence) {
        $type = $licence.type
        $value = $licence.'#text'
        if ($type -eq 'expression') {
            if (-not (Test-Expression $value)) { $verdict = "licence '$value' is not on the allow-list ($($Allowed -join ', '))" }
        } elseif ($type -eq 'file') {
            $file = Join-Path (Split-Path $nuspec) $value
            if (-not (Test-Path -LiteralPath $file)) {
                $verdict = "licence file '$value' is not in the package"
            } elseif (-not $review -or -not $review.Sha256) {
                $verdict = "licence is a file ('$value'), not an SPDX expression; read it and add the package to `$Reviewed, or drop it"
            } elseif ((Get-Sha256 $file) -ne $review.Sha256) {
                $verdict = "licence file '$value' is not the one reviewed (its SHA-256 changed); read it again"
            }
        } else {
            $verdict = "licence has an unknown type '$type'"
        }
    } elseif ($metadata.licenseUrl) {
        $url = [string]$metadata.licenseUrl
        if ($url -match '^https?://licenses\.nuget\.org/(.+)$') {
            if (-not (Test-Expression ([uri]::UnescapeDataString($Matches[1])))) { $verdict = "licence URL '$url' names a licence not on the allow-list" }
        } elseif (-not $review -or $review.Url -ne $url) {
            $verdict = "has only a licence URL ('$url'); read it and add the package to `$Reviewed with that URL, or drop it"
        }
    } else {
        $verdict = 'declares no licence'
    }

    if ($verdict) {
        Fail "$id $version`: $verdict"
        $bad++
    }
}

if ($bad -gt 0) {
    Fail "$bad of $($packages.Count) package(s) in $AssetsFile are outside the licence allow-list (plan decision D5)"
    exit 1
}
Write-Output "Package licences: $($packages.Count) package(s) checked, all on the allow-list."
exit 0
