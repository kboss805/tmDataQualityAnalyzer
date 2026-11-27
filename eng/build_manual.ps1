# =============================================================================
# build_manual.ps1 - regenerate the user manuals with their figures.
#
# WHY A SCRIPT
#
# The manual is an embedded resource of the application: it is written to a temp
# directory and opened in the default browser, so relative image paths would break.
# The figures therefore have to be embedded as base64 data URIs, which is not
# something to redo by hand every time the UI changes. The old plain-text guide went
# eighteen months stale precisely because updating shipped documentation was manual
# work.
#
# The figures are RENDERED from the application rather than captured by hand (plan
# step 4.9): TmDq.App.Tests' ManualFiguresTests sets up each scene from the committed
# recordings, checks what its caption says, and renders it into docs\manual_images.
# This script runs those tests, scales the figures and embeds them, through
# eng\embed_manual_images.py, into the two manuals:
#
#   base - assets\usermanual.html, compiled into the application
#   full - UserManual.html (the base plus docs\manual\walkthroughs.html), packaged by
#          deploy\build_package.ps1, which generates it itself (-NoCapture) and so does not
#          keep it in the repository: it is untracked and ignored
#
# Run it after a change to what a figure shows, and after every version bump (the log in four figures names the
# version; docs\manual_images\rendered-version.txt records which version rendered them, and
# check_release_consistency.ps1 fails when it is not the current one). -NoCapture reuses the figures already
# rendered, for a change to the prose alone:
#
#   powershell -ExecutionPolicy Bypass -File eng\build_manual.ps1 [-NoCapture]
#
# Safe to run repeatedly: the embed step strips any figures already present before
# inserting fresh ones, so running twice does not double them - and prose edits to
# the manual between runs are preserved.
# =============================================================================

param([switch]$NoCapture)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
. (Join-Path $PSScriptRoot 'common.ps1')
$SrcDir     = Join-Path $ProjectDir 'docs\manual_images'
$OutDir     = Join-Path $SrcDir 'processed'
$MaxWidth   = 1200

# Every figure is rendered from the application, so making them again starts from an empty folder.

if (-not $NoCapture) {
    Write-Host "Rendering the figures from the application (ManualFiguresTests)..."
    # What the render needs is checked before the figures go: a failed build or a missing core used to leave the tracked
    # figures deleted (git checkout -- docs/manual_images put them back).
    $core = Join-Path $ProjectDir 'out\Release\tmdq_core.dll'
    if (-not (Test-Path $core)) { throw "build the native core first: eng\build_core_dll.ps1 -Configuration Release" }
    $tests = Join-Path $ProjectDir 'tests\TmDq.App.Tests'
    & dotnet build $tests -c Release -nologo -v q
    if ($LASTEXITCODE -ne 0) { throw "the tests did not build ($LASTEXITCODE)" }
    Get-ChildItem $SrcDir -Filter *.png | Remove-Item -Force
    $env:TMDQ_CORE_DLL = $core
    $env:TMDQ_MANUAL_FIGURES = $SrcDir
    try {
        & (Get-TestExePath $ProjectDir 'TmDq.App.Tests' 'Release') -class TmDq.App.Tests.ManualFiguresTests
        if ($LASTEXITCODE -ne 0) { throw "a figure's scene failed ($LASTEXITCODE): its caption no longer holds, or it could not be rendered" }
    } finally {
        Remove-Item Env:TMDQ_MANUAL_FIGURES, Env:TMDQ_CORE_DLL -ErrorAction SilentlyContinue
    }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Clear previous output first: processed/ is gitignored build output, and a figure
# that gets renamed otherwise leaves its old file behind forever.
Remove-Item (Join-Path $OutDir '*.png') -Force -ErrorAction SilentlyContinue

# The figures are rendered at their final framing, so none is cropped; each is only scaled down to the manual's width.
Write-Host "Processing figures..."
foreach ($png in Get-ChildItem $SrcDir -Filter *.png | Sort-Object Name) {
    $img = [System.Drawing.Image]::FromFile($png.FullName)
    $scale = [math]::Min(1.0, $MaxWidth / $img.Width)
    $w = [int]($img.Width * $scale); $h = [int]($img.Height * $scale)
    $dst = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.PixelOffsetMode   = 'HighQuality'
    $g.DrawImage($img, 0, 0, $w, $h)
    $g.Dispose()
    $dst.Save((Join-Path $OutDir $png.Name), [System.Drawing.Imaging.ImageFormat]::Png)
    $dst.Dispose(); $img.Dispose()
    Write-Host ("  {0,-46} {1}x{2}" -f $png.Name, $w, $h)
}

Write-Host "`nEmbedding into the manual (via eng\embed_manual_images.py)..."
# The embedder's paths are relative to the repository's root: run it from there, whatever folder this was started in.
Push-Location $ProjectDir
try {
    & py (Join-Path $ProjectDir 'eng\embed_manual_images.py')
    if ($LASTEXITCODE -ne 0) { throw "embed step failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}

Write-Host "`nDone. Two manuals were written (sizes listed above):"
Write-Host "  base - assets\usermanual.html, compiled into the application. Rebuild it to pick"
Write-Host "         it up; it is an embedded resource."
Write-Host "  full - UserManual.html, packaged by deploy\build_package.ps1:"
Write-Host "         always in the portable ZIP, an optional task in the installer."
