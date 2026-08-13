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
# Safe to run repeatedly: the embed step strips any figures already present before
# inserting fresh ones, so running twice does not double them - and prose edits to
# the manual between runs are preserved.
# =============================================================================

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$SrcDir     = Join-Path $ProjectDir 'docs\manual_images'
$OutDir     = Join-Path $SrcDir 'processed'
$Manual     = Join-Path $ProjectDir 'resources\usermanual.html'

# Full-window captures are 1936x1119. This is the chart region within one: past the
# log sidebar, below the title bar, above the status bar. Cropping keeps the figure
# on the subject and roughly halves the embedded size.
$ChartCrop    = @(405, 38, 1520, 1045)
$ExpectedSize = @(1936, 1119)
$MaxWidth     = 1200

# name -> crop instruction:
#   $false          no crop (already a dialog-sized capture)
#   $true           crop to $ChartCrop - REQUIRES the capture to be $ExpectedSize
#   @(x,y,w,h)      explicit crop, for a capture framed differently
#
# $true is checked against the window size rather than trusted. A screen grab of the
# app floating over an IDE is a different size, and the fixed rectangle would then
# cut somewhere arbitrary - producing a plausible-looking but wrongly framed figure,
# which is exactly the kind of defect that survives review.
# Which legend variant is used where is a judgement call, not a default:
#   - frame-sync plots carry 4 streams, so the legend is compact, sits clear of the
#     data and names the curves - it earns its place;
#   - SNR plots carry 48 receiver channels, and that legend is tall enough to cover
#     the right-hand axis tick labels, so the plain capture reads better.
# The frame-sync pair is kept BOTH ways because the manual's legend-toggle section is
# the one place where the contrast itself is the subject.
$Images = [ordered]@{
    'Config Streams Dialg.png'                       = $false  # already a dialog-sized capture
    # Full-screen grab (app floating over the IDE), 1973x1137 - the chart sits at a
    # different offset, so this one carries its own rectangle.
    'Main Context Menu.png'                          = @(425, 63, 1520, 1045)
    'FrameSync Perctentage Plot with Legend.png'     = $true
    'FrameSync Perctentage Plot without Legend.png'  = $true
    'Frame Error Accumulation Plot with Legend.png'  = $true
    'Uncalibrated SNR Plot without Legend.png'       = $true
    'Calibrated SNR Plot without Legend.png'         = $true
    'Export Dialog.png'                              = $false
    # Installer wizard page. Cropped to drop the title bar, which carries a version
    # string: keeping it would stamp one release into a figure that never changes, so
    # it would read as stale from the next version onward. The page's own heading
    # inside the client area identifies the screen. Staged here so the crop is
    # validated now; it is embedded when the walkthrough sections are written.
    'walk-inst-01-components.png'                    = @(1, 32, 596, 431)

    # --- Walkthrough figures (batch A) ------------------------------------
    # Registered before they are embedded so their crops and sizes are validated
    # now rather than discovered when the walkthroughs are written.
    #
    # Dialog captures are window grabs, already tight - no crop.
    'walk-fs-01-open.png'                            = $false
    'walk-fs-02-configure-streams.png'               = $false
    'walk-fs-03-framesync-setup.png'                 = $false
    'walk-app-03-time-window.png'                    = $false
    'walk-cust-01-lock-tab.png'                      = $false
    # Full-window figures: these show the whole application deliberately - the log
    # sidebar and the light theme are the subjects, so the chart-region crop would
    # remove the very thing being illustrated.
    'walk-app-02-sidebar.png'                        = $false
    # The menu is a popup window, so a window-only grab captures either the menu
    # without the app or the app without the menu. Captured as the whole window and
    # cropped to the menu, which is the only technique that shows both.
    'walk-app-01-hamburger.png'                      = @(0, 0, 640, 430)
    'walk-theme-01-light-plot.png'                   = $false
    # Full-SCREEN grab: the figure needs the progress dialog AND the log filling in
    # beside it, which a window-only capture cannot show. Cropped to the app window
    # to drop the surrounding desktop.
    'walk-fs-04-processing.png'                      = @(14, 14, 1936, 1119)
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host "Processing screenshots..."
foreach ($name in $Images.Keys) {
    $path = Join-Path $SrcDir $name
    if (-not (Test-Path $path)) { throw "Missing screenshot: $path" }

    $img  = [System.Drawing.Image]::FromFile($path)
    $work = $img
    $spec = $Images[$name]
    # Read the dimensions BEFORE any Dispose() - a disposed Image reports nothing, and
    # the size is the whole point of the error messages below.
    $iw = $img.Width; $ih = $img.Height
    if ($spec) {
        if ($spec -is [array]) {
            $rect = $spec
        } else {
            # Bare $true means "the standard chart region of a standard window" - so
            # verify the window really is standard rather than cropping blind.
            if ($iw -ne $ExpectedSize[0] -or $ih -ne $ExpectedSize[1]) {
                $img.Dispose()
                throw ("$name is ${iw}x${ih}, expected $($ExpectedSize[0])x$($ExpectedSize[1]). " +
                       "Recapture at the standard window size, or give this entry an explicit @(x,y,w,h) crop.")
            }
            $rect = $ChartCrop
        }
        if (($rect[0] + $rect[2]) -gt $iw -or ($rect[1] + $rect[3]) -gt $ih) {
            $img.Dispose()
            throw "$name : crop @($($rect -join ',')) falls outside the ${iw}x${ih} image."
        }
        $r = New-Object System.Drawing.Rectangle($rect[0], $rect[1], $rect[2], $rect[3])
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
    $dst.Dispose(); if ($spec) { $work.Dispose() }; $img.Dispose()
    Write-Host ("  {0,-34} {1}x{2}" -f $name, $w, $h)
}

Write-Host "`nEmbedding into the manual (via scripts\embed_manual_images.py)..."
& py (Join-Path $ProjectDir 'scripts\embed_manual_images.py')
if ($LASTEXITCODE -ne 0) { throw "embed step failed ($LASTEXITCODE)" }

Write-Host "`nDone. Two manuals were written (sizes listed above):"
Write-Host "  base - resources\usermanual.html, compiled into the exe. Rebuild the app to"
Write-Host "         pick it up; it is a Qt resource."
Write-Host "  full - UserManual.html, shipped as a file. Always in the portable ZIP; an"
Write-Host "         optional task in the installer."
