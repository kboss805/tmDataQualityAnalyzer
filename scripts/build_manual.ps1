# =============================================================================
# build_manual.ps1 - regenerate resources/usermanual.html with its screenshots.
#
# WHY A SCRIPT
#
# The manual is a Qt resource: it is copied to a temp directory and opened in the
# default browser, so relative image paths would break. The screenshots therefore
# have to be embedded as base64 data URIs, which is not something to redo by hand
# every time the UI changes. UserGuide.txt went eighteen months stale precisely
# because updating shipped documentation was manual work.
#
# Run this after replacing or adding a screenshot in docs/manual_images/:
#
#   powershell -ExecutionPolicy Bypass -File scripts\build_manual.ps1
#
# It works from the COMMITTED manual (git checkout first) so it is idempotent -
# running it twice does not embed the images twice.
# =============================================================================

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$SrcDir     = Join-Path $ProjectDir 'docs\manual_images'
$OutDir     = Join-Path $SrcDir 'processed'
$Manual     = Join-Path $ProjectDir 'resources\usermanual.html'

# Full-window captures are 1936x1119. This is the chart region: past the log
# sidebar, below the title bar, above the status bar. Cropping keeps the figure on
# the subject and roughly halves the embedded size.
$ChartCrop = @(405, 38, 1520, 1045)
$MaxWidth  = 1200

# name -> crop? ($true means crop to the chart region first)
$Images = [ordered]@{
    'Config Streams Dialg.png'       = $false   # already a dialog-sized capture
    'Main Context Menu.png'          = $true
    'FrameSync Perctentage Plot.png' = $true
    'Frame Accumulation Plot.png'    = $true
    'Uncalibrated SNR plot.png'      = $true
    'calibrated SNR plot.png'        = $true
    'Export Dialog.png'              = $false
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host "Processing screenshots..."
foreach ($name in $Images.Keys) {
    $path = Join-Path $SrcDir $name
    if (-not (Test-Path $path)) { throw "Missing screenshot: $path" }

    $img  = [System.Drawing.Image]::FromFile($path)
    $work = $img
    if ($Images[$name]) {
        $r = New-Object System.Drawing.Rectangle($ChartCrop[0], $ChartCrop[1], $ChartCrop[2], $ChartCrop[3])
        $work = (New-Object System.Drawing.Bitmap($img)).Clone($r, $img.PixelFormat)
    }
    $scale = [math]::Min(1.0, $MaxWidth / $work.Width)
    $w = [int]($work.Width * $scale); $h = [int]($work.Height * $scale)
    $dst = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.PixelOffsetMode   = 'HighQuality'
    $g.DrawImage($work, 0, 0, $w, $h)
    $g.Dispose()
    $dst.Save((Join-Path $OutDir $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $dst.Dispose(); if ($Images[$name]) { $work.Dispose() }; $img.Dispose()
    Write-Host ("  {0,-34} {1}x{2}" -f $name, $w, $h)
}

Write-Host "`nEmbedding into the manual (via scripts\embed_manual_images.py)..."
& py (Join-Path $ProjectDir 'scripts\embed_manual_images.py')
if ($LASTEXITCODE -ne 0) { throw "embed step failed ($LASTEXITCODE)" }

Write-Host ("`nDone. {0} is now {1} KB." -f (Split-Path $Manual -Leaf),
            [math]::Round((Get-Item $Manual).Length / 1KB))
Write-Host "Rebuild the app to pick it up (it is compiled in as a Qt resource)."
