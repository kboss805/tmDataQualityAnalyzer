# =============================================================================
# build_release.ps1  -  Release build, sign, and package script
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

# Steps that are non-fatal on their own (signing, the Inno compile) record their
# failure here instead of aborting, so the run still produces whatever it can. The
# summary reports them and the script exits non-zero - previously these printed a
# warning that scrolled past and the script exited 0, announcing a "complete" build
# that had shipped an unsigned exe and no installer at all.
$Failures = New-Object System.Collections.Generic.List[string]

# Artifacts are named by version, so a re-run of the same version finds the previous
# run's files already in place. Anything not written after this moment was left behind
# by an earlier run and must not be credited to this one - without this check a failed
# Inno compile still "produced" a signed installer, because last week's was still there.
$RunStart = Get-Date

# Version consistency BEFORE the ~10 minute build. Four user-facing files carry the
# version as prose and nothing derives them from AppVersion; each has shipped stale at
# least once. Failing here costs a second, failing at the end costs the whole run - and
# a mismatched manual or release-notes header is not something to discover afterwards.
Write-Host "`n=== Checking release consistency ==="
& powershell -ExecutionPolicy Bypass -File "$ProjectDir\scripts\check_release_consistency.ps1" -ReleaseMode
if ($LASTEXITCODE -ne 0) {
    Write-Host "`nAborting: fix the inconsistencies above before packaging." -ForegroundColor Red
    exit 1
}

# Verifies an artifact was actually produced and, when signing was requested, that it
# actually carries a valid signature. Signing happens BEFORE the ZIP and the installer
# compile, so a failed signature silently propagates into everything downstream.
function Test-Artifact {
    param(
        [string]$Path,
        [string]$Label,
        [switch]$RequireSignature
    )
    if (-not (Test-Path $Path)) {
        $script:Failures.Add("$Label was not produced ($Path)")
        return $false
    }
    $written = (Get-Item $Path).LastWriteTime
    if ($written -lt $script:RunStart) {
        $script:Failures.Add("$Label is STALE - left over from an earlier run at $written, not produced by this one ($Path)")
        return $false
    }
    if (-not $RequireSignature) { return $true }

    $sig = Get-AuthenticodeSignature $Path
    if ($sig.Status -ne 'Valid') {
        $script:Failures.Add("$Label is NOT signed (status: $($sig.Status)) - $Path")
        return $false
    }
    Write-Host "  Verified signature: $Label"
    return $true
}

# --- Extract version from constants.h (single source of truth) ---
$constants = Get-Content "$ProjectDir\include\constants.h" -Raw
$major   = [regex]::Match($constants, 'kMajor\s*=\s*(\d+)').Groups[1].Value
$minor   = [regex]::Match($constants, 'kMinor\s*=\s*(\d+)').Groups[1].Value
$patch   = [regex]::Match($constants, 'kPatch\s*=\s*(\d+)').Groups[1].Value
$version = "$major.$minor.$patch"

$StageDir       = "$ProjectDir\deploy\staging"
$InstallerStage = "$StageDir\installer"
$PortableRoot   = "$StageDir\portable\tmDataQualityAnalyzer-v${version}_portable"

# Locate signtool.exe: newest Windows SDK x64 build, else whatever vcvars put on
# PATH. Discovered rather than pinned - this used to name 10.0.26100.0 outright, so
# an SDK update would silently demote it to the bare-name fallback and the failure
# would surface at the signing step, minutes into packaging.
$SigntoolExe = 'signtool'
$sdkSigntool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" `
                   -ErrorAction SilentlyContinue |
               Sort-Object { [version]($_.Directory.Parent.Name) } -ErrorAction SilentlyContinue |
               Select-Object -Last 1
if ($sdkSigntool) { $SigntoolExe = $sdkSigntool.FullName }

function Test-SigningWorks {
    <#
        Prove the certificate can actually produce a signature, BEFORE the build.

        The certificate being present in the store proves nothing: it is token-backed
        (Certum SimplySign), and with the token locked it still reports as valid with
        HasPrivateKey = True. The two failure shapes are signtool erroring with "No
        certificates were found" or HANGING on the PIN prompt - so this signs a
        throwaway copy under a timeout.

        Signs a copy of signtool itself: any PE file will do, nothing in the build
        exists yet, and a copy is never the file we ship.
    #>
    param([string]$Thumbprint, [string]$Signtool, [string]$Timestamp)

    $probeDir = Join-Path $env:TEMP ("tmdq-signprobe-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $probeDir | Out-Null
    try {
        $sample = if (Test-Path $Signtool) { $Signtool } else { (Get-Command signtool).Source }
        $probe  = Join-Path $probeDir 'probe.exe'
        Copy-Item $sample $probe -Force

        $p = Start-Process $Signtool -ArgumentList @(
                'sign', '/sha1', $Thumbprint, '/tr', $Timestamp,
                '/td', 'sha256', '/fd', 'sha256', $probe
             ) -NoNewWindow -PassThru `
               -RedirectStandardOutput (Join-Path $probeDir 'out.txt') `
               -RedirectStandardError  (Join-Path $probeDir 'err.txt')

        # Touch .Handle BEFORE waiting. Windows PowerShell 5.1 - which is how this
        # script is invoked - returns a Process from Start-Process -PassThru that has
        # not cached its handle, and once the process has exited the handle can no
        # longer be obtained, so $p.ExitCode reads back EMPTY. Measured both ways
        # against a deliberately bad thumbprint: without this line the exit code is
        # '', with it the real 1 comes through. Empty is not 0, so the check below
        # used to report a SUCCESSFUL signing as a failure - "exit " alongside
        # signtool's own "Successfully signed" in one message.
        $null = $p.Handle

        # 90s, not 45: a healthy pre-flight measured 36s here, nearly all of it the
        # timestamp-server round trip, so a slow network would otherwise trip the
        # timeout and report a locked token - a confident, wrong diagnosis.
        if (-not $p.WaitForExit(90000)) {
            $p.Kill()
            throw "Signing pre-flight timed out after 90s. Most likely the signing token is locked (signtool waits on the PIN prompt) - log in to SimplySign and re-run. A very slow or unreachable timestamp server ($Timestamp) would look the same. Nothing was built."
        }
        # WaitForExit(Int32) can return before the redirected streams are flushed;
        # .NET documents calling the no-argument overload afterwards for this case.
        $p.WaitForExit()
        $exit = $p.ExitCode

        if ($null -eq $exit) {
            # Should not happen now the handle is cached, but guessing either way is
            # worse than saying so: calling it success defeats the pre-flight, and
            # calling it failure is the bug this replaced.
            Write-Host "  Signing pre-flight: signtool finished but reported no exit code; continuing. The signing steps below will surface a real failure."
        }
        elseif ($exit -ne 0) {
            $why = (Get-Content (Join-Path $probeDir 'out.txt'), (Join-Path $probeDir 'err.txt') -ErrorAction SilentlyContinue) -join ' '
            throw "Signing pre-flight FAILED (exit $exit): $why`nFix the certificate, or pass -SignCertSha1 '' to build unsigned."
        }
        Write-Host "  Signing pre-flight OK (cert $Thumbprint can sign)."
    } finally {
        Remove-Item $probeDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "============================================"
Write-Host " Building tmDataQualityAnalyzer v$version Release"
Write-Host "============================================"

# Before the build, not after it. Packaging is a ten-minute run that signs near the
# end, so a locked token used to be discovered only once everything else was done.
if ($SignCertSha1) {
    Write-Host "[0/8] Signing pre-flight..."
    if (-not (Get-Command $SigntoolExe -ErrorAction SilentlyContinue) -and -not (Test-Path $SigntoolExe)) {
        throw "signtool not found. Install the Windows SDK signing tools, or pass -SignCertSha1 '' to build unsigned."
    }
    Test-SigningWorks -Thumbprint $SignCertSha1 -Signtool $SigntoolExe -Timestamp $SignTimestamp
} else {
    Write-Host "[0/8] Signing disabled (-SignCertSha1 '') - skipping pre-flight."
}

# --- Step 1: Clean and build release ---
Set-Location $ProjectDir

Write-Host "[1/7] Running qmake..."
New-Item -ItemType Directory -Force -Path "$ProjectDir\build" | Out-Null
Set-Location "$ProjectDir\build"
& qmake ../tmDataQualityAnalyzer.pro -spec win32-msvc 'CONFIG+=release'
if ($LASTEXITCODE -ne 0) { throw "qmake failed" }

Write-Host "[2/7] Building release..."
& nmake -f Makefile.Release clean
& nmake -f Makefile.Release
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
# --no-compiler-runtime: windeployqt otherwise stages vc_redist.x64.exe (25.6 MB) into
# bin\, and the .iss ships bin\* wholesale - so every installer carried a redist
# INSTALLER that nothing ever runs, roughly a third of the payload. The CRT is deployed
# as loose DLLs a few lines below instead, which is what both the installed and the
# portable layout actually need.
& windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime "$InstallerStage\bin\tmDataQualityAnalyzer.exe"
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# App-local Visual C++ runtime: copy the CRT redist DLLs next to the exe so the
# installed AND portable app launch on a machine without the VC++ redistributable.
# (The MSVC build links the dynamic CRT. windeployqt --compiler-runtime only stages
# the vc_redist *installer*, which the portable build can't use and the .iss doesn't
# run, so deploy the DLLs directly. MinGW previously shipped its own runtime DLLs.)
$crtDir = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $crtDir) { throw "VC++ CRT redist not found under '$env:VCToolsRedistDir\x64' - is the MSVC toolchain env loaded?" }
Copy-Item "$($crtDir.FullName)\*.dll" "$InstallerStage\bin\"
Write-Host "  Bundled app-local VC++ runtime from $($crtDir.Name)"

foreach ($dir in @('receiver_params', 'rcvr_cals', 'framesync_patterns')) {
    if (Test-Path "$ProjectDir\settings\$dir\*.toml") {
        Copy-Item "$ProjectDir\settings\$dir\*.toml" "$InstallerStage\settings\$dir\"
    }
}

# --- Step 4: Code signing (optional) ---
Write-Host "[5/8] Code signing..."
if ($SignCertSha1) {
    & $SigntoolExe sign /sha1 $SignCertSha1 /tr $SignTimestamp /td sha256 /fd sha256 "$InstallerStage\bin\tmDataQualityAnalyzer.exe"
    if ($LASTEXITCODE -ne 0) {
        # The usual cause is a token-backed certificate whose token is locked: the
        # cert still shows up in CurrentUser\My with HasPrivateKey = True, so its
        # presence proves nothing. signtool reports either "No certificates were
        # found that met all the given criteria" or simply hangs waiting on the PIN.
        Write-Warning "Code signing FAILED - the app exe is unsigned"
        Write-Warning "  If this cert is token-backed (SimplySign), log in to the token and re-run."
        $Failures.Add("Code signing failed for the app exe")
    }
    # Verify rather than trust signtool's exit code - this exe is the source for both
    # the portable layout and the installer payload, so an unsigned one contaminates
    # every artifact downstream.
    Test-Artifact "$InstallerStage\bin\tmDataQualityAnalyzer.exe" 'app exe (installer payload)' -RequireSignature | Out-Null
} else {
    Write-Host "  Skipping - built UNSIGNED (-SignCertSha1 '')."
}

# --- Step 5: Create portable layout ---
Write-Host "[6/8] Creating portable layout..."
# Copied from the (signed) installer payload, so it inherits that signature - which is
# exactly why an unsigned payload silently becomes an unsigned portable build.
Copy-Item "$InstallerStage\bin\tmDataQualityAnalyzer.exe" "$PortableRoot\"
Get-ChildItem "$InstallerStage\bin\*.dll" | Copy-Item -Destination "$PortableRoot\"
if ($SignCertSha1) {
    Test-Artifact "$PortableRoot\tmDataQualityAnalyzer.exe" 'app exe (portable layout)' -RequireSignature | Out-Null
}

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
# The portable ZIP always carries the full manual - there is no installer to ask.
Copy-Item "$ProjectDir\UserManual.html"           "$PortableRoot\UserManual.html"
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
    # gated by /DSIGN). 'signtool' is referenced by bare name: env.ps1 imports the
    # MSVC/Windows SDK environment (vcvars), which puts the SDK's signtool directory
    # on PATH, and iscc inherits that PATH when it spawns the tool. A bare name has
    # no spaces and no embedded quotes, so it survives PowerShell native-argument
    # passing - a fully-qualified, quoted signtool path gets mis-split by PowerShell
    # and makes iscc reject the command line.
    $isccArgs = @("/DMyAppVersion=$version")
    if ($SignCertSha1) {
        $signCmd = "signtool sign /sha1 $SignCertSha1 /tr $SignTimestamp /td sha256 /fd sha256 `$f"
        $isccArgs += '/DSIGN'
        $isccArgs += "/Ssigntool=$signCmd"
    }
    $isccArgs += "$ProjectDir\deploy\tmDataQualityAnalyzer.iss"
    & $IsccPath @isccArgs
    if ($LASTEXITCODE -ne 0) {
        # iscc aborts the whole compile when its SignTool step fails, so this usually
        # means NO installer was produced at all - not merely an unsigned one.
        Write-Warning "Inno Setup compilation or installer signing FAILED"
        $Failures.Add("Inno Setup compile/signing failed")
    }
} else {
    Write-Host "  Skipping - Inno Setup not found. Install from https://jrsoftware.org/isinfo.php"
    $Failures.Add("Inno Setup not found - no installer was produced (install Inno Setup 6)")
}
$isccFound = [bool]$IsccPath

$InstallerExe = "$ProjectDir\deploy\tmDataQualityAnalyzer-v${version}_setup.exe"

# --- Step 8: Verify what was actually produced -------------------------------
# Report only artifacts that exist, and only call them signed once verified. The
# summary used to print both paths unconditionally, so a run in which signing failed
# and iscc aborted still announced an installer that was never written.
Write-Host ""
Write-Host "Verifying artifacts..."
$zipOk = Test-Artifact $ZipPath 'portable ZIP'
# When iscc was missing that is already recorded; re-checking would report the same
# missing installer twice.
$installerOk = if ($isccFound) {
    Test-Artifact $InstallerExe 'installer EXE' -RequireSignature:([bool]$SignCertSha1)
} else {
    $false
}

function Format-Artifact {
    param([bool]$Ok, [string]$Path)
    if (-not $Ok) { return 'NOT PRODUCED' }
    $mb = [math]::Round((Get-Item $Path).Length / 1MB, 1)
    return "$Path  (${mb} MB)"
}

$signState = if (-not $SignCertSha1)      { 'UNSIGNED (requested)' }
             elseif ($Failures.Count -eq 0) { 'signed and verified' }
             else                           { 'SEE FAILURES BELOW' }

Write-Host ""
Write-Host "============================================"
if ($Failures.Count -eq 0) {
    Write-Host " Build and packaging complete!"
} else {
    Write-Host " BUILD INCOMPLETE - $($Failures.Count) step(s) failed"
}
Write-Host "============================================"
Write-Host ""
Write-Host " Version:           $version"
Write-Host " Signing:           $signState"
Write-Host " Installer staging: $InstallerStage"
Write-Host " Portable staging:  $PortableRoot"
Write-Host " Portable ZIP:      $(Format-Artifact $zipOk $ZipPath)"
Write-Host " Installer EXE:     $(Format-Artifact $installerOk $InstallerExe)"

if ($Failures.Count -gt 0) {
    Write-Host ""
    Write-Host " FAILURES:"
    foreach ($f in $Failures) { Write-Host "   - $f" }
    Write-Host ""
    Write-Host " Do NOT ship these artifacts. A failed signing step still leaves a"
    Write-Host " portable ZIP behind - containing an UNSIGNED exe - so the ZIP existing"
    Write-Host " is not evidence the release is good."
    Write-Host "============================================"
    exit 1
}

Write-Host "============================================"
exit 0
