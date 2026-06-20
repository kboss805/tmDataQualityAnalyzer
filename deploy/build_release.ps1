# =============================================================================
# build_release.ps1  —  Release build, sign, and package script
#
# Builds a release binary, runs windeployqt, and produces both an Inno Setup
# installer and a portable ZIP.
#
# Parameters:
#   -SignCertSha1   SHA-1 thumbprint of the code-signing certificate in the
#                   Windows Certificate Store (CurrentUser\My).
#                   Defaults to the SimplySign Certum OSS cert
#                   (1DCBF23B52067A8D52E9C517345EA77B9D926669). Pass an empty
#                   string (-SignCertSha1 '') to build unsigned.
#   -SignTimestamp  Timestamp server URL (default: http://timestamp.digicert.com)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File deploy\build_release.ps1
# =============================================================================

param(
    [string]$SignCertSha1  = $(if ($env:SIGN_CERT_SHA1) { $env:SIGN_CERT_SHA1 } else { '1DCBF23B52067A8D52E9C517345EA77B9D926669' }),
    [string]$SignTimestamp = 'http://timestamp.digicert.com'
)

if ($env:SIGN_TIMESTAMP) {
    $SignTimestamp = $env:SIGN_TIMESTAMP
}

$ErrorActionPreference = 'Stop'

$ProjectDir = Split-Path -Parent $PSScriptRoot
. "$ProjectDir\scripts\env.ps1"

# --- Extract version from constants.h (single source of truth) ---
$constants = Get-Content "$ProjectDir\include\constants.h" -Raw
$major   = [regex]::Match($constants, 'kMajor\s*=\s*(\d+)').Groups[1].Value
$minor   = [regex]::Match($constants, 'kMinor\s*=\s*(\d+)').Groups[1].Value
$patch   = [regex]::Match($constants, 'kPatch\s*=\s*(\d+)').Groups[1].Value
$version = "$major.$minor.$patch"

$StageDir       = "$ProjectDir\deploy\staging"
$InstallerStage = "$StageDir\installer"
$PortableRoot   = "$StageDir\portable\tmDataQualityAnalyzer-v${version}_portable"

# Locate signtool.exe — prefer WDK x64 path, fall back to PATH
$SigntoolExe = 'signtool'
$wdkSigntool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
if (Test-Path $wdkSigntool) { $SigntoolExe = $wdkSigntool }

Write-Host "============================================"
Write-Host " Building tmDataQualityAnalyzer v$version Release"
Write-Host "============================================"

# --- Step 1: Clean and build release ---
Set-Location $ProjectDir

Write-Host "[1/7] Running qmake..."
New-Item -ItemType Directory -Force -Path "$ProjectDir\build" | Out-Null
Set-Location "$ProjectDir\build"
& qmake ../tmDataQualityAnalyzer.pro -spec win32-g++ 'CONFIG+=release'
if ($LASTEXITCODE -ne 0) { throw "qmake failed" }

Write-Host "[2/7] Building release..."
& mingw32-make -f Makefile.Release clean
& mingw32-make -f Makefile.Release "-j$env:NUMBER_OF_PROCESSORS"
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# --- Step 2: Prepare staging directories ---
Write-Host "[3/7] Preparing staging directories..."
if (Test-Path $StageDir) {
    $resolvedStage = (Resolve-Path $StageDir).Path
    $expectedStage = Join-Path $ProjectDir 'deploy\staging'
    if ($resolvedStage -ne $expectedStage) {
        throw "Refusing to clean staging dir: '$resolvedStage' does not match expected '$expectedStage'"
    }
    $stageItem = Get-Item $StageDir -Force
    if ($stageItem.LinkType) {
        # $StageDir is a reparse point (symlink/junction). Remove-Item -Recurse -Force on a
        # directory reparse point can recurse into and delete the TARGET's contents instead
        # of just the link (this previously wiped sibling release artifacts in deploy\ and
        # deploy\README_portable.txt). Delete() on the link itself removes only the link.
        $stageItem.Delete()
    } else {
        Remove-Item $StageDir -Recurse -Force
    }
}
New-Item -ItemType Directory -Force -Path "$InstallerStage\bin"      | Out-Null
New-Item -ItemType Directory -Force -Path "$InstallerStage\settings\receiver_params"   | Out-Null
New-Item -ItemType Directory -Force -Path "$InstallerStage\settings\rcvr_cals"         | Out-Null
New-Item -ItemType Directory -Force -Path "$InstallerStage\settings\framesync_patterns" | Out-Null
New-Item -ItemType Directory -Force -Path "$PortableRoot\settings\receiver_params"   | Out-Null
New-Item -ItemType Directory -Force -Path "$PortableRoot\settings\rcvr_cals"         | Out-Null
New-Item -ItemType Directory -Force -Path "$PortableRoot\settings\framesync_patterns" | Out-Null

# --- Step 3: Copy exe and run windeployqt for installer layout ---
Write-Host "[4/7] Running windeployqt (installer layout)..."
Copy-Item "$ProjectDir\build\release\tmDataQualityAnalyzer.exe" "$InstallerStage\bin\"
& windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler "$InstallerStage\bin\tmDataQualityAnalyzer.exe"
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

foreach ($dir in @('receiver_params', 'rcvr_cals', 'framesync_patterns')) {
    if (Test-Path "$ProjectDir\settings\$dir\*.toml") {
        Copy-Item "$ProjectDir\settings\$dir\*.toml" "$InstallerStage\settings\$dir\"
    }
}

# --- Step 4: Code signing (optional) ---
Write-Host "[5/8] Code signing..."
if ($SignCertSha1) {
    & $SigntoolExe sign /sha1 $SignCertSha1 /tr $SignTimestamp /td sha256 /fd sha256 "$InstallerStage\bin\tmDataQualityAnalyzer.exe"
    if ($LASTEXITCODE -ne 0) { Write-Warning "Code signing failed — continuing without signature" }
} else {
    Write-Host "  Skipping — pass -SignCertSha1 to enable signing."
}

# --- Step 5: Create portable layout ---
Write-Host "[6/8] Creating portable layout..."
Copy-Item "$InstallerStage\bin\tmDataQualityAnalyzer.exe" "$PortableRoot\"
Get-ChildItem "$InstallerStage\bin\*.dll" | Copy-Item -Destination "$PortableRoot\"

foreach ($dir in @('platforms', 'styles', 'imageformats', 'tls', 'networkinformation')) {
    $src = "$InstallerStage\bin\$dir"
    if (Test-Path $src) { Copy-Item $src "$PortableRoot\$dir" -Recurse }
}

foreach ($dir in @('receiver_params', 'rcvr_cals', 'framesync_patterns')) {
    if (Test-Path "$ProjectDir\settings\$dir\*.toml") {
        Copy-Item "$ProjectDir\settings\$dir\*.toml" "$PortableRoot\settings\$dir\"
    }
}

Copy-Item "$ProjectDir\LICENSE"                    "$PortableRoot\LICENSE.txt"
Copy-Item "$ProjectDir\deploy\README_portable.txt" "$PortableRoot\README.txt"
Copy-Item "$ProjectDir\UserGuide.txt"              "$PortableRoot\UserGuide.txt"
New-Item  -ItemType File -Path "$PortableRoot\portable" -Force | Out-Null

# --- Step 6: Create portable ZIP ---
Write-Host "[7/8] Creating portable ZIP..."
$ZipPath = "$ProjectDir\deploy\tmDataQualityAnalyzer-v${version}_portable.zip"
# Freshly-copied .exe/.dll files are briefly locked by antivirus real-time
# scanning, which makes Compress-Archive fail with "being used by another
# process". Retry with a short delay to ride out the scan.
$maxAttempts = 5
for ($attempt = 1; $attempt -le $maxAttempts; $attempt++) {
    try {
        Compress-Archive -Path $PortableRoot -DestinationPath $ZipPath -Force
        break
    } catch {
        if ($attempt -eq $maxAttempts) { throw }
        Write-Host "  Compress-Archive attempt $attempt failed (likely antivirus scan lock), retrying..."
        Start-Sleep -Seconds 3
    }
}

# --- Step 7: Compile Inno Setup installer ---
Write-Host "[8/8] Compiling Inno Setup installer..."
$IsccPath = $null
if     (Get-Command iscc -ErrorAction SilentlyContinue)                        { $IsccPath = 'iscc' }
elseif (Test-Path "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")          { $IsccPath = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" }
elseif (Test-Path "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe")            { $IsccPath = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }

if ($IsccPath) {
    # When signing, hand iscc a SignTool definition so it signs BOTH the setup
    # executable and the embedded uninstaller (SignedUninstaller=yes in the .iss,
    # gated by /DSIGN). 'signtool' is referenced by bare name: env.ps1 puts the
    # WDK signtool directory on PATH and iscc inherits that PATH when it spawns the
    # tool. A bare name has no spaces and no embedded quotes, so it survives
    # PowerShell native-argument passing — a fully-qualified, quoted signtool path
    # gets mis-split by PowerShell and makes iscc reject the command line.
    $isccArgs = @("/DMyAppVersion=$version")
    if ($SignCertSha1) {
        $signCmd = "signtool sign /sha1 $SignCertSha1 /tr $SignTimestamp /td sha256 /fd sha256 `$f"
        $isccArgs += '/DSIGN'
        $isccArgs += "/Ssigntool=$signCmd"
    }
    $isccArgs += "$ProjectDir\deploy\tmDataQualityAnalyzer.iss"
    & $IsccPath @isccArgs
    if ($LASTEXITCODE -ne 0) { Write-Warning "Inno Setup compilation or installer signing failed" }
} else {
    Write-Host "  Skipping — Inno Setup not found. Install from https://jrsoftware.org/isinfo.php"
}

$InstallerExe = "$ProjectDir\deploy\tmDataQualityAnalyzer-v${version}_setup.exe"

Write-Host ""
Write-Host "============================================"
Write-Host " Build and packaging complete!"
Write-Host "============================================"
Write-Host ""
Write-Host " Installer staging: $InstallerStage"
Write-Host " Portable staging:  $PortableRoot"
Write-Host " Portable ZIP:      $ZipPath"
Write-Host " Installer EXE:     $InstallerExe"
Write-Host "============================================"
