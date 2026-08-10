# =============================================================================
# check_release_consistency.ps1 - fail if anything user-facing disagrees with
# the version in include/constants.h.
#
# WHY THIS EXISTS
#
# The version has ONE source of truth (AppVersion in include/constants.h) and the
# build derives version_autogen.h, the .pro VERSION and the .rc from it. But four
# files carry the version as PROSE, and nothing derives those:
#
#   CLAUDE.md                  "Current version: X.Y.Z"   (sat at 2.7.0 for 2 releases)
#   docs/CLAUDE.md             "Project Version: X.Y.Z"
#   resources/usermanual.html  subtitle + footer          (read 2.7.0 at v2.9.0)
#   deploy/RELEASENOTES.txt    top section header
#
# Every one of those has shipped stale at least once. The cut-release skill says to
# grep for the old version by hand, which works exactly as long as someone remembers
# and greps for the RIGHT old string. This makes it mechanical.
#
# DESIGN NOTE - this check must be able to FAIL.
#
# A missing marker is an ERROR, not a pass. The failure mode that keeps recurring in
# this project is a check that silently verifies nothing: the release banner that
# announced artifacts it never wrote, the warning gate that grepped a log that did
# not exist. So every assertion below proves it found its marker before comparing,
# and a file that cannot be read is a hard failure rather than a skipped check.
#
# Usage (exits 1 on any mismatch, printing every problem, not just the first):
#
#   powershell -ExecutionPolicy Bypass -File scripts\check_release_consistency.ps1
# =============================================================================

param(
    # Release-time only. Between releases an "Unreleased" section in the changelog is
    # the CORRECT place to record landed-but-unshipped work, so CI must not reject it;
    # at release time it means work is about to ship attributed to no version at all,
    # which is how two cosmetic changes got credited to a build without them.
    [switch]$ReleaseMode
)

$ErrorActionPreference = 'Stop'

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Problems   = New-Object System.Collections.Generic.List[string]

function Read-Tracked {
    param([string]$RelPath)
    $full = Join-Path $ProjectDir $RelPath
    if (-not (Test-Path $full)) {
        $Problems.Add("$RelPath : FILE MISSING - cannot verify")
        return $null
    }
    return Get-Content $full -Raw
}

# --- the single source of truth -------------------------------------------
$constants = Read-Tracked 'include\constants.h'
if (-not $constants) { Write-Host 'FATAL: include/constants.h unreadable'; exit 1 }

$nums = foreach ($k in 'Major', 'Minor', 'Patch') {
    $m = [regex]::Match($constants, "k$k\s*=\s*(\d+)")
    if (-not $m.Success) { Write-Host "FATAL: AppVersion::k$k not found in constants.h"; exit 1 }
    $m.Groups[1].Value
}
$Version = $nums -join '.'
Write-Host "Source of truth: AppVersion = $Version`n"

# --- each user-facing version literal --------------------------------------
# Pattern must capture the version in group 1 so a mismatch can name what it found.
$checks = @(
    @{ File = 'CLAUDE.md'
       Pattern = '\*\*Current version:\s*([0-9]+\.[0-9]+\.[0-9]+)\.\*\*'
       What = 'Current version line' }

    @{ File = 'docs\CLAUDE.md'
       Pattern = '\*\*Project Version\*\*:\s*([0-9]+\.[0-9]+\.[0-9]+)'
       What = 'Project Version line' }

    @{ File = 'resources\usermanual.html'
       Pattern = '<p class="subtitle">User Manual .{1,3} Version ([0-9]+\.[0-9]+\.[0-9]+)</p>'
       What = 'manual subtitle' }

    @{ File = 'resources\usermanual.html'
       Pattern = 'This manual describes\s+version ([0-9]+\.[0-9]+\.[0-9]+)\.'
       What = 'manual footer' }

    @{ File = 'deploy\RELEASENOTES.txt'
       Pattern = '^tmDataQualityAnalyzer v([0-9]+\.[0-9]+\.[0-9]+) - What''s New'
       What = 'top release-notes header' }

    # The FULL manual is a second shipped artifact, generated alongside the base one
    # by build_manual.ps1. A second manual that nothing checks is precisely the stale
    # artifact this script exists to prevent, so it gets the same two markers.
    @{ File = 'UserManual.html'
       Pattern = '<p class="subtitle">User Manual .{1,3} Version ([0-9]+\.[0-9]+\.[0-9]+)</p>'
       What = 'full manual subtitle' }

    @{ File = 'UserManual.html'
       Pattern = 'This manual describes\s+version ([0-9]+\.[0-9]+\.[0-9]+)\.'
       What = 'full manual footer' }
)

foreach ($c in $checks) {
    $text = Read-Tracked $c.File
    if ($null -eq $text) { continue }
    $m = [regex]::Match($text, $c.Pattern)
    if (-not $m.Success) {
        # Not "nothing to check" - the marker this script exists to verify is gone.
        $Problems.Add("$($c.File) : $($c.What) NOT FOUND (pattern no longer matches - fix the pattern or the file)")
    }
    elseif ($m.Groups[1].Value -ne $Version) {
        $Problems.Add("$($c.File) : $($c.What) says $($m.Groups[1].Value), expected $Version")
    }
    else {
        Write-Host ("  OK  {0,-28} {1}" -f $c.What, $m.Groups[1].Value)
    }
}

# --- the changelog must have a section for THIS version --------------------
$devlog = Read-Tracked 'docs\CLAUDE.md'
if ($devlog) {
    if ($devlog -notmatch "###\s+v$([regex]::Escape($Version))\b") {
        $Problems.Add("docs/CLAUDE.md : no '### v$Version' section in Version History")
    } else {
        Write-Host ("  OK  {0,-28} {1}" -f 'changelog section', "### v$Version")
    }
    # An "Unreleased" heading at release time means work was never filed under the
    # version that is about to ship it - exactly how two cosmetic changes ended up
    # attributed to a build that did not contain them.
    if ($ReleaseMode) {
        if ($devlog -match '###\s+Unreleased') {
            $Problems.Add("docs/CLAUDE.md : an 'Unreleased' section still exists - fold it into ### v$Version before releasing")
        } else {
            Write-Host ("  OK  {0,-28} {1}" -f 'no Unreleased section', 'clean')
        }
    } else {
        Write-Host ("  --  {0,-28} {1}" -f 'Unreleased section', 'not checked (use -ReleaseMode)')
    }
}

# --- report ----------------------------------------------------------------
Write-Host ''
if ($Problems.Count -gt 0) {
    Write-Host "FAILED - $($Problems.Count) inconsistency(ies) vs AppVersion $Version :" -ForegroundColor Red
    foreach ($p in $Problems) { Write-Host "  - $p" -ForegroundColor Red }
    exit 1
}

Write-Host "All release artifacts agree on version $Version." -ForegroundColor Green
exit 0
