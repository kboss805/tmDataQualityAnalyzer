# =============================================================================
# build_package.ps1 - the release packages from v3.0.0 on: the Avalonia
# application (plan step 3.3 built it as a preview beside v2.x; step 5.3
# made it the release; docs/changelog.md records both).
#
# Produces, beside this script:
#   tmDataQualityAnalyzer-vX.Y.Z_portable.zip
#   tmDataQualityAnalyzer-vX.Y.Z_setup.msi   (per-user or per-machine Windows Installer package: tmDataQualityAnalyzer.wxs, built by build_msi.ps1)
#   tmDataQualityAnalyzer-vX.Y.Z_symbols.zip  (the debug symbols of the application's binaries. NOT shipped to users: kept with
#                                              the release, so a crash dump or stack trace of that version can be read)
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
#                  Default: $env:SIGN_CERT_SHA1, else the project's Certum OSS
#                  certificate. Pass '' to build unsigned (it will not run on a
#                  machine with Smart App Control on).
#   -SignTimestamp timestamp server (default http://timestamp.digicert.com)
#
# Usage (Windows PowerShell 5.1, as the release scripts run):
#   powershell -ExecutionPolicy Bypass -File deploy\build_package.ps1
# =============================================================================

param(
    [string]$SignCertSha1  = $(if ($env:SIGN_CERT_SHA1) { $env:SIGN_CERT_SHA1 } else { '1DCBF23B52067A8D52E9C517345EA77B9D926669' }),
    [string]$SignTimestamp = 'http://timestamp.digicert.com'
)
$ErrorActionPreference = 'Stop'

$Here = $PSScriptRoot
$Root = (Resolve-Path (Join-Path $Here '..')).Path
. (Join-Path $Root 'eng\env.ps1')
. (Join-Path $Root 'eng\common.ps1')
. (Join-Path $Here 'signing.ps1')

$RunStart = Get-Date
$Failures = New-Object System.Collections.Generic.List[string]

# --- Version: AppVersion, the one source (eng/AppVersion.props reads the same three numbers). ---
$constants = Get-Content (Join-Path $Root 'src\core\include\constants.h') -Raw
$version = Get-AppVersionFromHeader $constants

$Stage    = Join-Path $Here 'staging'
$Publish  = Join-Path $Stage 'publish'
$AppDir   = Join-Path $Stage 'app'
$Exe      = Join-Path $AppDir 'tmDataQualityAnalyzer.exe'
$ZipPath  = Join-Path $Here "tmDataQualityAnalyzer-v${version}_portable.zip"
$MsiPath  = Join-Path $Here "tmDataQualityAnalyzer-v${version}_setup.msi"
$SymbolsZip = Join-Path $Here "tmDataQualityAnalyzer-v${version}_symbols.zip"

Write-Host '============================================'
Write-Host " tmDataQualityAnalyzer v$version"
Write-Host '============================================'

# --- The full manual: generated here, not kept in the repository. ---
# UserManual.html is derived (assets\usermanual.html + docs\manual\walkthroughs.html + the figures), so it is made from the figures
# in docs\manual_images, which the consistency check below refuses unless this version rendered them (-NoCapture: they are not
# rendered again here). The same step re-stamps and re-embeds the BASE manual, which is tracked and compiled into the
# application, so it must come out byte for byte as it went in: a changed base means the application was about to be built with
# a stale manual, and the fix is to run eng\build_manual.ps1 and commit, not to ship.
Write-Host 'Generating the full manual...'
$baseManual = Join-Path $Root 'assets\usermanual.html'
# The text, not the bytes: a checkout with core.autocrlf holds the file with CR LF and the embed step writes LF.
function Get-ManualText { [System.IO.File]::ReadAllText($baseManual).Replace("`r`n", "`n") }
$baseBefore = Get-ManualText
& powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'eng\build_manual.ps1') -NoCapture
if ($LASTEXITCODE -ne 0) { throw 'build_manual.ps1 failed: the full manual was not generated' }
if ($baseBefore -ne (Get-ManualText)) {
    throw 'assets\usermanual.html is out of date: generating the manual changed it. Run eng\build_manual.ps1 (not -NoCapture after a version bump), commit the result and package again'
}

# --- Every user-facing version literal agrees with AppVersion, and the changelog has this version's section. ---
# Before anything is built: a mismatched manual or release-notes header is not something to discover afterwards.
Write-Host 'Checking release consistency...'
& powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'eng\check_release_consistency.ps1') -ReleaseMode -FullManual
if ($LASTEXITCODE -ne 0) { throw 'check_release_consistency.ps1 failed: fix the inconsistencies above before packaging' }

# --- 0. Signing pre-flight, before anything is built. ---
$Signtool = $null
if ($SignCertSha1) {
    Write-Host '[0/8] Signing pre-flight...'
    $Signtool = Find-Signtool
    Test-CodeSigning -Signtool $Signtool -Thumbprint $SignCertSha1 -Timestamp $SignTimestamp
    Write-Host "  OK: certificate $SignCertSha1 can sign ($Signtool)"
} else {
    Write-Host '[0/8] Signing disabled (-SignCertSha1 ''''): the package will NOT run under Smart App Control.'
}

# --- 1. The native core, Release. ---
Write-Host '[1/8] Building tmdq_core.dll (Release)...'
& powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'eng\build_core_dll.ps1') -Configuration Release
if ($LASTEXITCODE -ne 0) { throw 'build_core_dll.ps1 failed' }
$CoreDll = Join-Path $Root 'out\Release\tmdq_core.dll'
# What ships must be the Release core: a Debug one is about three times slower and nothing else here would say so (the
# import allow-list in build_core_dll.ps1 admits the debug runtime). Checked now so a mistake costs seconds, and again on
# the copy that is staged (step 4).
& powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'eng\check_release_core.ps1') -Dll $CoreDll
if ($LASTEXITCODE -ne 0) { throw 'the core just built is not a Release build (see above)' }

# --- 2. Single-file, self-contained, ReadyToRun publish. ---
# ReadyToRun: the managed code is shipped already compiled, so a launch does not compile it first. Measured (2026-11-24, this
# machine, 15 launches each): the window is up in 370 ms instead of 742 ms, and a run of the PRN sample takes 835 ms instead of
# 1299 ms; the exe is 132 MB instead of 87 MB (the ZIP about 16 MB bigger). The results of a run are the same, and so are the
# IL offsets of a crash report. It needs the Crossgen2 package for win-x64, which NuGet fetches into the package cache.
Write-Host '[2/8] Publishing the application (self-contained, single-file, ReadyToRun)...'
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
& dotnet publish (Join-Path $Root 'src\TmDq.App') -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true -p:PublishReadyToRun=true -p:DebugType=portable -o $Publish -nologo -v minimal
if ($LASTEXITCODE -ne 0) { throw 'dotnet publish failed' }

# --- 3. The manifest and the icon must have reached the bundled host (the manifest once did not; TmDq.App.csproj says why). ---
Write-Host '[3/8] Checking the published exe''s manifest and icon...'
$manifest = Join-Path $Stage 'manifest.xml'
& mt -nologo "-inputresource:$(Join-Path $Publish 'tmDataQualityAnalyzer.exe');#1" "-out:$manifest" | Out-Null
$text = if (Test-Path $manifest) { Get-Content $manifest -Raw } else { '' }
if ($text -notmatch '<activeCodePage[^>]*>\s*UTF-8\s*</activeCodePage>') { throw 'the published exe does not select the UTF-8 code page' }
if ($text -notmatch '8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a') { throw 'the published exe does not declare Windows 10 compatibility' }
Write-Host '  OK: UTF-8 code page and Windows 10 compatibility'

# The icon, likewise: v3.0.0 shipped without one, so Explorer, the taskbar and the shortcuts showed a generic exe. The
# exe's icon group must hold assets\icon.ico's images, byte for byte (TmDq.App.Tests' IconTests hold the build's exe;
# this holds the single-file host). C# 5 for Windows PowerShell 5.1's Add-Type.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class TmDqExeIcon {
    delegate bool EnumProc(IntPtr m, IntPtr t, IntPtr n, IntPtr p);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr LoadLibraryExW(string p, IntPtr f, uint fl);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr m);
    [DllImport("kernel32.dll")] static extern bool EnumResourceNamesW(IntPtr m, IntPtr t, EnumProc cb, IntPtr p);
    [DllImport("kernel32.dll")] static extern IntPtr FindResourceW(IntPtr m, IntPtr n, IntPtr t);
    [DllImport("kernel32.dll")] static extern IntPtr LoadResource(IntPtr m, IntPtr r);
    [DllImport("kernel32.dll")] static extern IntPtr LockResource(IntPtr d);
    [DllImport("kernel32.dll")] static extern uint SizeofResource(IntPtr m, IntPtr r);
    static byte[] Res(IntPtr m, IntPtr n, IntPtr t) {
        IntPtr r = FindResourceW(m, n, t); if (r == IntPtr.Zero) return null;
        byte[] b = new byte[SizeofResource(m, r)]; Marshal.Copy(LockResource(LoadResource(m, r)), b, 0, b.Length); return b;
    }
    // null when the exe's first icon group holds exactly the icon file's images, else what differs.
    public static string Compare(string exe, byte[] ico) {
        IntPtr m = LoadLibraryExW(exe, IntPtr.Zero, 0x22);
        if (m == IntPtr.Zero) return "cannot open its resources";
        try {
            List<IntPtr> groups = new List<IntPtr>();
            EnumResourceNamesW(m, new IntPtr(14), delegate(IntPtr a, IntPtr b, IntPtr n, IntPtr c) { groups.Add(n); return true; }, IntPtr.Zero);
            if (groups.Count == 0) return "no icon group (RT_GROUP_ICON)";
            byte[] g = Res(m, groups[0], new IntPtr(14));
            int count = BitConverter.ToUInt16(ico, 4);
            if (BitConverter.ToUInt16(g, 4) != count) return "a different number of images";
            for (int i = 0; i < count; i++) {
                int e = 6 + 16 * i, size = BitConverter.ToInt32(ico, e + 8), offset = BitConverter.ToInt32(ico, e + 12);
                byte[] image = Res(m, new IntPtr(BitConverter.ToUInt16(g, 6 + 14 * i + 12)), new IntPtr(3));
                if (image == null || image.Length != size) return "image " + i + " differs";
                for (int j = 0; j < size; j++) if (image[j] != ico[offset + j]) return "image " + i + " differs";
            }
            return null;
        } finally { FreeLibrary(m); }
    }
}
'@
$iconFile = Join-Path $Root 'assets\icon.ico'
$iconProblem = [TmDqExeIcon]::Compare((Join-Path $Publish 'tmDataQualityAnalyzer.exe'), [System.IO.File]::ReadAllBytes($iconFile))
if ($iconProblem) { throw "the published exe does not carry assets\icon.ico: $iconProblem" }
Write-Host '  OK: the application icon'

# --- 4. Stage the application folder. ---
Write-Host '[4/8] Staging...'
New-Item -ItemType Directory -Force -Path $AppDir | Out-Null
# Everything the publish produced except debug symbols: the native packages ship their .pdb files (100 MB), and our
# own are kept apart in the symbols ZIP (step 8), not shipped.
Get-ChildItem $Publish -File | Where-Object { $_.Extension -ne '.pdb' } | Copy-Item -Destination $AppDir
# The same files, never signed: the clean-machine test's negative control (Smart App Control must block these, or a
# pass there proves nothing). deploy\sandbox\run_sandbox_test.ps1 -Payload takes this folder.
$Unsigned = Join-Path $Stage 'unsigned'
New-Item -ItemType Directory -Force -Path $Unsigned | Out-Null
Get-ChildItem $Publish -File | Where-Object { $_.Extension -ne '.pdb' } | Copy-Item -Destination $Unsigned
Copy-Item $CoreDll $AppDir
& powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'eng\check_release_core.ps1') -Dll (Join-Path $AppDir 'tmdq_core.dll')
if ($LASTEXITCODE -ne 0) { throw 'the core staged for the package is not a Release build (see above)' }
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
# (found by the Sandbox test). Taken from the same toolset's redist folder, and chosen from the DLL's own import list
# so a new runtime import cannot be missed. Microsoft signs
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
$deps = Join-Path $Root "src\TmDq.App\obj\Release\$TargetFramework\win-x64\tmDataQualityAnalyzer.deps.json"
& powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'eng\gen_third_party_notices.ps1') -DepsFile $deps -Out (Join-Path $AppDir 'THIRD-PARTY-NOTICES.txt')
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
# with the defaults, writes one line and exits (src\TmDq.App\Startup.cs). The committed PRN sample's answer is
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
$portable = Join-Path $Stage "tmDataQualityAnalyzer-v${version}_portable"
Copy-Item $AppDir $portable -Recurse
# The full manual: always in the portable copy (there is no installer to ask), an optional task in the installer, which
# takes it from beside the staged application rather than from within it (plan step 4.9).
$manual = Join-Path $Root 'UserManual.html'
if (-not (Test-Path $manual)) { throw 'UserManual.html was not generated' }
New-Item -ItemType Directory -Force -Path (Join-Path $Stage 'manual') | Out-Null
Copy-Item $manual (Join-Path $Stage 'manual\UserManual.html') -Force
Copy-Item $manual (Join-Path $portable 'UserManual.html') -Force
# A portable copy keeps its preferences in a folder beside itself, as the v2.x portable ZIP did (plan step 4.8).
New-Item -ItemType File -Path (Join-Path $portable 'portable') -Force | Out-Null
Copy-Item (Join-Path $Root 'deploy\README_portable.txt') (Join-Path $portable 'README.txt')
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
foreach ($attempt in 1..5) {
    # A freshly written exe is briefly held by the virus scanner.
    try { Compress-Archive -Path $portable -DestinationPath $ZipPath -Force; break }
    catch { if ($attempt -eq 5) { throw }; Start-Sleep -Seconds 3 }
}

# The debug symbols of our own binaries, kept per release and not shipped: a user has no use for them, but with them a
# crash dump or a stack trace of exactly this version can be read down to source lines. A symbol file matches its binary
# by an id the build wrote into both, so signing the binary afterwards does not unmatch it. Attach this ZIP to the GitHub
# release; the application's crash report (src\TmDq.App\CrashHandling.cs) is read with these.
$Symbols = Join-Path $Stage 'symbols'
New-Item -ItemType Directory -Force -Path $Symbols | Out-Null
foreach ($pdb in @((Join-Path $Publish 'tmDataQualityAnalyzer.pdb'), (Join-Path $Publish 'TmDq.Core.pdb'), [System.IO.Path]::ChangeExtension($CoreDll, '.pdb'))) {
    if (Test-Path $pdb) { Copy-Item $pdb $Symbols } else { $Failures.Add("the symbols ZIP is missing $(Split-Path $pdb -Leaf)") }
}
Set-Content -Path (Join-Path $Symbols 'README.txt') -Encoding ASCII -Value @(
    "tmDataQualityAnalyzer v$version - debug symbols",
    '',
    'Not part of the installer or the portable ZIP. They match that version''s binaries:',
    '  tmDataQualityAnalyzer.pdb, TmDq.Core.pdb   the managed code inside tmDataQualityAnalyzer.exe',
    '  tmdq_core.pdb                              tmdq_core.dll, the native core',
    '',
    'Keep them with the release. Load them in WinDbg or Visual Studio to read a crash dump, or a stack trace from a crash report, of this version.'
)
if (Test-Path $SymbolsZip) { Remove-Item $SymbolsZip -Force }
Compress-Archive -Path (Join-Path $Symbols '*') -DestinationPath $SymbolsZip -Force

# The Windows Installer package, from the same staged folders. The signing arguments are added only when there is a
# certificate: Windows PowerShell 5.1 drops an empty string passed to a native command, which would let -SignCertSha1
# swallow the next parameter's name.
$msiArgs = @('-Version', $version, '-Stage', $Stage, '-OutFile', $MsiPath)
if ($SignCertSha1) { $msiArgs += @('-SignCertSha1', $SignCertSha1, '-SignTimestamp', $SignTimestamp) }
& powershell -ExecutionPolicy Bypass -File (Join-Path $Here 'build_msi.ps1') @msiArgs
if ($LASTEXITCODE -ne 0) { $Failures.Add('the MSI build or its signing failed (build_msi.ps1)') }

# --- Verify what this run actually produced. ---
foreach ($a in @(@{ Path = $ZipPath; Label = 'portable ZIP'; Signed = $false },
                 @{ Path = $MsiPath; Label = 'MSI'; Signed = [bool]$SignCertSha1 },
                 @{ Path = $SymbolsZip; Label = 'symbols ZIP'; Signed = $false })) {
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
foreach ($a in $ZipPath, $MsiPath, $SymbolsZip) {
    if (Test-Path $a) { Write-Host (' {0}  ({1:N1} MB)' -f $a, ((Get-Item $a).Length / 1MB)) }
}
if ($Failures.Count) {
    foreach ($f in $Failures) { Write-Host "   - $f" }
    exit 1
}
exit 0
