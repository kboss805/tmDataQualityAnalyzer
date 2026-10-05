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
# Captures kept on purpose but used in neither manual. Naming one here is what
# separates "decided against" from "forgotten": the coverage check reports anything
# in docs/manual_images/ that is neither processed nor listed below, and a check that
# also fired on a deliberate choice would be noise on every build - which is how a
# warning stops being read at all.
#
# CURRENTLY EMPTY, and that is the honest state rather than an oversight. It held
# three: two unused halves of with/without-legend pairs, and a plain sidebar shot
# superseded by walk-err-01-log-error.png. All three were deleted in the v2.14.1
# re-capture round - a spare is only worth keeping while it still shows the current
# UI, and these predated the removal of the on-chart legend toggle, so each would have
# needed re-capturing before it could ever be placed. A stale spare is worse than no
# spare: it looks like an option and is not one.
#
# Each entry records WHY, because the reason is the whole value of keeping the file.
$Spares = [ordered]@{
}

$Images = [ordered]@{
    'Config Streams Dialg.png'                       = $false  # already a dialog-sized capture
    # Full-screen grab (app floating over the IDE), 1949x1107 - the chart sits at a
    # different offset, so this one carries its own rectangle. Re-captured for the
    # menu that no longer carries Export; this capture has the log sidebar HIDDEN,
    # so the crop starts at the left edge. The previous rectangle began at x=425 to
    # skip a sidebar that was open then, and reusing it here would have cut off the
    # Y axis - the reason each entry declares its own rectangle rather than sharing
    # the standard one.
    'Main Context Menu.png'                          = @(14, 45, 1918, 1055)
    # The Y Axes submenu, captured the same way and for the same reason as the menu
    # above: a popup cannot be grabbed with its parent any other way. 1946x1129 with
    # the sidebar hidden, so like its sibling the crop starts at the left edge.
    'Y Axes Submenu.png'                             = @(14, 45, 1918, 1060)
    'FrameSync Perctentage Plot with Legend.png'     = $true
    'FrameSync Perctentage Plot without Legend.png'  = $true
    'Frame Error Accumulation Plot with Legend.png'  = $true
    'Uncalibrated SNR Plot without Legend.png'       = $true
    # Used by the SNR walkthrough: a 48-row legend is the concrete motivation for
    # the "narrow it down" step, so here the covered axis is the point.
    'Uncalibrated SNR Plot with Legend.png'          = $true
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
    #
    # (walk-app-02-sidebar.png is deliberately not processed - see $Spares above.)
    # The menu is a popup window, so a window-only grab captures either the menu
    # without the app or the app without the menu. Captured as the whole window and
    # cropped to the menu, which is the only technique that shows both.
    'walk-app-01-hamburger.png'                      = @(0, 0, 640, 430)
    'walk-theme-01-light-plot.png'                   = $false
    # Full-SCREEN grab: the figure needs the progress dialog AND the log filling in
    # beside it, which a window-only capture cannot show. Cropped to the app window
    # to drop the surrounding desktop.
    'walk-fs-04-processing.png'                      = @(14, 14, 1936, 1119)

    # --- Walkthrough figures (batch B): SNR + calibration -----------------
    # All window grabs of dialogs, already tight - no crop. The calibration set is
    # self-consistent and checkable against the shipped settings files:
    #   TRC.toml has 11 [[Step]] entries 0..60 dB      -> "11 steps parsed"
    #   receiver_params/TRC.toml: 16 rcvrs x 3 chans   -> "3 of 48 channel(s)"
    #   the calibrated plot tops out at 60 dB          -> matches the step range
    'walk-snr-01-mode-select.png'                    = $false
    'walk-snr-02-receiver-setup.png'                 = $false
    'walk-snr-03-load-params.png'                    = $false
    'walk-cal-01-extract-button.png'                 = $false
    'walk-cal-03-clip-controls.png'                  = $false
    'walk-cal-04-summary.png'                        = $false
    'walk-cal-05-step-toml.png'                      = $false

    # --- Walkthrough figures: batch processing ----------------------------
    # Two dialog grabs plus one full window. walk-batch-03 shows the Plot File
    # submenu open over the chart, so it is NOT chart-cropped - the menu is the
    # subject and sits outside the plot area.
    'walk-batch-01-save-template.png'                = $false
    'walk-batch-02-apply-dialog.png'                 = $false
    'walk-batch-03-plot-file-menu.png'               = $false

    # --- Walkthrough figures (batch C): customisation + error reporting ---
    'walk-cust-02-snr-tree.png'                      = $false
    'walk-cust-03-recolor.png'                       = $false
    # Full window, NOT chart-cropped: the log sidebar is the subject here, so the
    # chart region would crop out the very thing being illustrated.
    'walk-err-01-log-error.png'                      = $false
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Clear previous output first: processed/ is gitignored build output, and a
# capture that gets renamed otherwise leaves its old file behind forever. Three
# such leftovers had accumulated, which is what made the directory useless as a
# record of what the manual actually uses.
if (Test-Path $OutDir) { Remove-Item (Join-Path $OutDir '*.png') -Force }

# The embed step does the coverage check and needs to know which unused captures
# are deliberate. Passed as a file rather than a parameter so the two scripts do not
# grow a positional contract.
$SparesFile = Join-Path $OutDir '.spares'
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
# Record the deliberate spares for the embedder's coverage check.
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
# @() forces enumeration: without it the key collection stringifies to one line of
# "System.Collections.Specialized...KeyValueCollection", which the reader then takes
# as a single filename - a spares list that silently covers nothing.
Set-Content -Path $SparesFile -Value @($Spares.Keys) -Encoding UTF8

& py (Join-Path $ProjectDir 'scripts\embed_manual_images.py')
if ($LASTEXITCODE -ne 0) { throw "embed step failed ($LASTEXITCODE)" }

Write-Host "`nDone. Two manuals were written (sizes listed above):"
Write-Host "  base - resources\usermanual.html, compiled into the exe. Rebuild the app to"
Write-Host "         pick it up; it is a Qt resource."
Write-Host "  full - UserManual.html, shipped as a file. Always in the portable ZIP; an"
Write-Host "         optional task in the installer."
