# =============================================================================
# build_avalonia_package.ps1 - the Avalonia application's packaging prototype
# (docs/dotnet-migration/avalonia-plan.md, step 3.3).
#
# Produces, beside this script:
#   tmDataQualityAnalyzer-preview-vX.Y.Z_portable.zip
#   tmDataQualityAnalyzer-preview-vX.Y.Z_setup.exe
#
# THE LAYOUT IS A SINGLE-FILE PUBLISH, and that is what keeps signing honest. A
# self-contained folder publish ships 27 unsigned PE files: 22 Avalonia assemblies,
# MicroCom.Runtime, Tmds.DBus.Protocol and our 3. Smart App Control blocks unsigned
# code (the spike saw error 4551 in a clean Sandbox), and signing the third-party
# assemblies would put this project's publisher identity on code it did not write.
# Single-file bundles every managed assembly into OUR exe, so the one signature
# covers them; the three native libraries left beside it are already signed by their
# own publishers (libSkiaSharp and libHarfBuzzSharp by Microsoft, av_libglesv2 by
# AvaloniaUI OU). This script therefore signs ONLY our own binaries - the exe and
# tmdq_core.dll - and fails rather than sign anything else it finds unsigned.
#
# Parameters:
#   -SignCertSha1  thumbprint of the code-signing certificate (CurrentUser\My).
#                  Default: $env:SIGN_CERT_SHA1, else the Certum OSS certificate the
#                  Qt release uses. Pass '' to build unsigned (it will not run on a
#                  machine with Smart App Control on).
#   -SignTimestamp timestamp server (default http://timestamp.digicert.com)
#
# Usage (Windows PowerShell 5.1, like the Qt release script):
#   powershell -ExecutionPolicy Bypass -File deploy\avalonia\build_avalonia_package.ps1
# =============================================================================

param(
    [string]$SignCertSha1  = $(if ($env:SIGN_CERT_SHA1) { $env:SIGN_CERT_SHA1 } else { '1DCBF23B52067A8D52E9C517345EA77B9D926669' }),
    [string]$SignTimestamp = 'http://timestamp.digicert.com'
)
$ErrorActionPreference = 'Stop'

$Here = $PSScriptRoot
$Root = (Resolve-Path (Join-Path $Here '..\..')).Path
. (Join-Path $Root 'scripts\env.ps1')
. (Join-Path $Here 'signing.ps1')

$RunStart = Get-Date
$Failures = New-Object System.Collections.Generic.List[string]

# --- Version: AppVersion, the one source (dotnet/build/AppVersion.props reads the same three numbers). ---
$constants = Get-Content (Join-Path $Root 'include\constants.h') -Raw
$version = ('kMajor', 'kMinor', 'kPatch' | ForEach-Object {
    $m = [regex]::Match($constants, "$_\s*=\s*(\d+)")
    if (-not $m.Success) { throw "AppVersion::$_ not found in include\constants.h" }
    $m.Groups[1].Value
}) -join '.'

$Stage    = Join-Path $Here 'staging'
$Publish  = Join-Path $Stage 'publish'
$AppDir   = Join-Path $Stage 'app'
$Exe      = Join-Path $AppDir 'tmDataQualityAnalyzer.exe'
$ZipPath  = Join-Path $Here "tmDataQualityAnalyzer-preview-v${version}_portable.zip"
$SetupExe = Join-Path $Here "tmDataQualityAnalyzer-preview-v${version}_setup.exe"

Write-Host '============================================'
Write-Host " tmDataQualityAnalyzer Preview v$version (Avalonia)"
Write-Host '============================================'

# --- 0. Signing pre-flight, before anything is built. ---
$Signtool = $null
if ($SignCertSha1) {
    Write-Host '[0/8] Signing pre-flight...'
    $Signtool = Find-Signtool
    Test-SigningWorks -Signtool $Signtool -Thumbprint $SignCertSha1 -Timestamp $SignTimestamp
    Write-Host "  OK: certificate $SignCertSha1 can sign ($Signtool)"
} else {
    Write-Host '[0/8] Signing disabled (-SignCertSha1 ''''): the package will NOT run under Smart App Control.'
}

# --- 1. The native core, Release. ---
Write-Host '[1/8] Building tmdq_core.dll (Release)...'
& powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\build_core_dll.ps1') -Configuration Release
if ($LASTEXITCODE -ne 0) { throw 'build_core_dll.ps1 failed' }
$CoreDll = Join-Path $Root 'native\out\Release\tmdq_core.dll'

# --- 2. Single-file, self-contained publish. ---
Write-Host '[2/8] Publishing the application (self-contained, single-file)...'
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
& dotnet publish (Join-Path $Root 'dotnet\TmDq.App') -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true -p:DebugType=none -o $Publish -nologo -v minimal
if ($LASTEXITCODE -ne 0) { throw 'dotnet publish failed' }

# --- 3. The manifest must have reached the bundled host (it once did not; TmDq.App.csproj says why). ---
Write-Host '[3/8] Checking the published exe''s manifest...'
$manifest = Join-Path $Stage 'manifest.xml'
& mt -nologo "-inputresource:$(Join-Path $Publish 'tmDataQualityAnalyzer.exe');#1" "-out:$manifest" | Out-Null
$text = if (Test-Path $manifest) { Get-Content $manifest -Raw } else { '' }
if ($text -notmatch '<activeCodePage[^>]*>\s*UTF-8\s*</activeCodePage>') { throw 'the published exe does not select the UTF-8 code page' }
if ($text -notmatch '8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a') { throw 'the published exe does not declare Windows 10 compatibility' }
Write-Host '  OK: UTF-8 code page and Windows 10 compatibility'

# --- 4. Stage the application folder. ---
Write-Host '[4/8] Staging...'
New-Item -ItemType Directory -Force -Path $AppDir | Out-Null
# Everything the publish produced except debug symbols: the native packages ship their .pdb files (100 MB) even
# with DebugType=none.
Get-ChildItem $Publish -File | Where-Object { $_.Extension -ne '.pdb' } | Copy-Item -Destination $AppDir
# The same files, never signed: the clean-machine test's negative control (Smart App Control must block these, or a
# pass there proves nothing). deploy\sandbox\run_sandbox_test.ps1 -Payload takes this folder.
$Unsigned = Join-Path $Stage 'unsigned'
New-Item -ItemType Directory -Force -Path $Unsigned | Out-Null
Get-ChildItem $Publish -File | Where-Object { $_.Extension -ne '.pdb' } | Copy-Item -Destination $Unsigned
Copy-Item $CoreDll $AppDir
# The shipped settings (TmDq.App.csproj copies the repository's settings\ into the publish): the Receiver SNR defaults
# file the setup dialog opens on, the patterns and word maps its Load buttons start in, and receiver_params\default.toml,
# the word map the core falls back to. The installer never overwrites them (US8.0).
$settings = Join-Path $Publish 'settings'
foreach ($needed in 'framesync_patterns\framesync_rcvr_default.toml', 'receiver_params\default.toml') {
    if (-not (Test-Path (Join-Path $settings $needed))) { throw "the publish has no settings\$needed" }
}
Copy-Item $settings (Join-Path $AppDir 'settings') -Recurse
# The Visual C++ runtime DLLs tmdq_core.dll imports, beside it. A clean Windows has the Universal CRT (the
# api-ms-win-crt-* imports) but not MSVCP140 / VCRUNTIME140*, and without them the DLL fails to load with error 126
# (found by the Sandbox test). Taken from the same toolset's redist folder, as deploy\build_release.ps1 does for the
# Qt package, and chosen from the DLL's own import list so a new runtime import cannot be missed. Microsoft signs
# them, so Smart App Control accepts them.
$crtDir = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $crtDir) { throw "VC++ runtime redist not found under '$env:VCToolsRedistDir\x64' - is the MSVC environment loaded?" }
$crtImports = @(& dumpbin /nologo /dependents $CoreDll | ForEach-Object { $_.Trim() } |
                Where-Object { $_ -match '^(MSVCP|VCRUNTIME|CONCRT|VCCORLIB)\d+[_A-Za-z0-9]*\.dll$' })
if ($crtImports.Count -eq 0) { throw 'tmdq_core.dll imports no Visual C++ runtime DLL: has its runtime linkage changed? Then this step should go.' }
foreach ($name in $crtImports) {
    $src = Join-Path $crtDir.FullName $name
    if (-not (Test-Path $src)) { throw "tmdq_core.dll imports $name, which is not in $($crtDir.FullName)" }
    Copy-Item $src $AppDir
}
Write-Host "  VC++ runtime for tmdq_core.dll ($($crtDir.Name)): $($crtImports -join ', ')"
Copy-Item (Join-Path $Root 'LICENSE') (Join-Path $AppDir 'LICENSE.txt')
$deps = Join-Path $Root 'dotnet\TmDq.App\obj\Release\net10.0\win-x64\tmDataQualityAnalyzer.deps.json'
& powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\gen_third_party_notices.ps1') -DepsFile $deps -Out (Join-Path $AppDir 'THIRD-PARTY-NOTICES.txt')
if ($LASTEXITCODE -ne 0) { throw 'gen_third_party_notices.ps1 failed' }
Get-ChildItem $AppDir | ForEach-Object { Write-Host ('  {0,10:N0} KB  {1}' -f ($_.Length / 1KB), $_.Name) }

# --- 5. Sign our own binaries, and nothing else. ---
Write-Host '[5/8] Signing...'
$ours = @('tmDataQualityAnalyzer.exe', 'tmdq_core.dll')
$pe = @(Get-ChildItem $AppDir -File | Where-Object { $_.Extension -in '.exe', '.dll' })
$unsignedOthers = @($pe | Where-Object { $ours -notcontains $_.Name -and (Get-AuthenticodeSignature $_.FullName).Status -ne 'Valid' })
if ($unsignedOthers.Count) {
    throw ('third-party files are unsigned and will not be signed with this project''s certificate: ' +
           (($unsignedOthers | ForEach-Object { $_.Name }) -join ', ') + '. A layout change has exposed code that must ship inside the single-file bundle or signed by its own publisher.')
}
if ($SignCertSha1) {
    foreach ($name in $ours) {
        Invoke-Signtool -Signtool $Signtool -Thumbprint $SignCertSha1 -Timestamp $SignTimestamp -File (Join-Path $AppDir $name)
        Write-Host "  signed $name"
    }
}

# --- 6. Every PE file in the package verifies (ours by this certificate, the rest by their publishers). ---
Write-Host '[6/8] Verifying every signature...'
foreach ($f in $pe) {
    $s = Get-AuthenticodeSignature $f.FullName
    $who = if ($s.SignerCertificate) { $s.SignerCertificate.Subject -replace '^CN=(?:"([^"]+)"|([^,]+)).*', '$1$2' } else { '-' }
    Write-Host ('  {0,-10} {1,-30} {2}' -f $s.Status, $who, $f.Name)
    if ($SignCertSha1 -and $s.Status -ne 'Valid') { $Failures.Add("$($f.Name) is not validly signed ($($s.Status))") }
    if ($SignCertSha1 -and $ours -contains $f.Name -and $s.SignerCertificate.Thumbprint -ne $SignCertSha1) {
        $Failures.Add("$($f.Name) is signed, but not by certificate $SignCertSha1")
    }
}

# --- 7. Smoke test: the staged app processes a recording end to end (window, native core, VC++ runtime). ---
# With a recording on its command line and TMDQ_SMOKE_REPORT set, the app opens it, processes its first PCM channel
# with the defaults, writes one line and exits (dotnet\TmDq.App\Startup.cs). The committed PRN sample's answer is
# known: 116 one-second windows of channel 13, mean lock 93.532 %.
Write-Host '[7/8] Processing a recording with the staged application...'
$Recording = Join-Path $Root 'tests\data\test files\prn_testfile, sample.ch10'
$Expected  = 'ok samples=116 mean=93.532 channel=13 '
$report = Join-Path $Stage 'smoke.txt'
$env:TMDQ_SMOKE_REPORT = $report
try {
    $p = Start-Process $Exe -ArgumentList "`"$Recording`"" -PassThru
    $null = $p.Handle
    if (-not $p.WaitForExit(120000)) { $p.Kill(); $Failures.Add('the staged application did not finish processing the PRN sample in 120 s') }
} finally {
    Remove-Item Env:\TMDQ_SMOKE_REPORT -ErrorAction SilentlyContinue
}
$line = if (Test-Path $report) { (Get-Content $report -Raw).Trim() } else { '(no report)' }
if ($p.HasExited -and $p.ExitCode -eq 0 -and $line.StartsWith($Expected)) {
    Write-Host "  OK: $line"
} else {
    $Failures.Add("the staged application did not process the PRN sample as expected (exit $($p.ExitCode)): $line")
}

# --- 8. The packages. ---
Write-Host '[8/8] Packaging...'
$portable = Join-Path $Stage "tmDataQualityAnalyzer-preview-v${version}_portable"
Copy-Item $AppDir $portable -Recurse
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
foreach ($attempt in 1..5) {
    # A freshly written exe is briefly held by the virus scanner.
    try { Compress-Archive -Path $portable -DestinationPath $ZipPath -Force; break }
    catch { if ($attempt -eq 5) { throw }; Start-Sleep -Seconds 3 }
}

$iscc = @((Get-Command iscc -ErrorAction SilentlyContinue).Source,
          "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
          "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) {
    $Failures.Add('Inno Setup 6 not found: no installer was produced')
} else {
    $isccArgs = @("/DMyAppVersion=$version")
    if ($SignCertSha1) {
        # Bare 'signtool': env.ps1 put the SDK on PATH, and a quoted full path gets mis-split on the way to iscc.
        $isccArgs += '/DSIGN', "/Ssigntool=signtool sign /sha1 $SignCertSha1 /tr $SignTimestamp /td sha256 /fd sha256 `$f"
    }
    $isccArgs += (Join-Path $Here 'tmDataQualityAnalyzer-preview.iss')
    & $iscc @isccArgs
    if ($LASTEXITCODE -ne 0) { $Failures.Add('Inno Setup compile or installer signing failed') }
}

# --- Verify what this run actually produced. ---
foreach ($a in @(@{ Path = $ZipPath; Label = 'portable ZIP'; Signed = $false }, @{ Path = $SetupExe; Label = 'installer'; Signed = [bool]$SignCertSha1 })) {
    if (-not (Test-Path $a.Path)) { $Failures.Add("$($a.Label) was not produced"); continue }
    if ((Get-Item $a.Path).LastWriteTime -lt $RunStart) { $Failures.Add("$($a.Label) is left over from an earlier run"); continue }
    if ($a.Signed -and (Get-AuthenticodeSignature $a.Path).Status -ne 'Valid') { $Failures.Add("$($a.Label) is not signed") }
}

Write-Host ''
Write-Host '============================================'
Write-Host $(if ($Failures.Count) { " INCOMPLETE - $($Failures.Count) problem(s)" } else { ' Packaging complete' })
Write-Host '============================================'
Write-Host " Version:   $version"
Write-Host " Signing:   $(if (-not $SignCertSha1) { 'UNSIGNED (requested)' } elseif ($Failures.Count) { 'see problems' } else { 'every PE file verified' })"
foreach ($a in $ZipPath, $SetupExe) {
    if (Test-Path $a) { Write-Host (' {0}  ({1:N1} MB)' -f $a, ((Get-Item $a).Length / 1MB)) }
}
if ($Failures.Count) {
    foreach ($f in $Failures) { Write-Host "   - $f" }
    exit 1
}
exit 0
