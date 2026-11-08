# =============================================================================
# verify_avalonia_install.ps1 - clean-machine test of the Avalonia application's packages
# (plan step 3.3 built it for the preview; step 5.3 made it the release's) under Smart App
# Control, including the UPGRADE from the Qt application.
#
# Runs INSIDE Windows Sandbox, launched by
#   run_sandbox_test.ps1 -Verifier verify_avalonia_install.ps1 -Setup <setup.exe>
#       -Payload "<zip>;deploy\avalonia\staging\unsigned;tests\data\test files\prn_testfile, sample.ch10;deploy\tmDataQualityAnalyzer-v2.14.2_setup.exe"
# (the list is separated by semicolons: the committed recordings' names contain commas)
# Do not run it on a developer machine: it installs to Program Files and uninstalls again.
#
# What it proves: on a machine that has never seen the application, with Smart App
# Control ON, the last Qt release installs as a user has it, and the signed installer
# upgrades it in place - the Qt files gone, the user's settings file kept, the v2.x
# preferences left alone (3.1.0 starts from the defaults), the .ch10 association moved to the new exe. The installed app and the
# portable ZIP both open their window, every PE file carries a valid signature, the
# native core DLL loads, Code Integrity blocked nothing of ours - and an UNSIGNED build
# of the same exe IS blocked, which is what shows the enforcement was real and not absent.
#
# ASCII only, written for Windows PowerShell 5.1 (what Sandbox has).
# =============================================================================

$ErrorActionPreference = 'Continue'
$IN = 'C:\tmdqa'
$OUT = 'C:\tmdqa-out'
$LOG = Join-Path $OUT 'results.txt'
$lines = @()
$fails = 0
$started = Get-Date

function Say($text) { $script:lines += $text; Write-Host $text }
function Check($name, $ok, $detail) {
    if ($ok) { Say ("  PASS  {0}{1}" -f $name, $(if ($detail) { " - $detail" } else { '' })) }
    else     { Say ("  FAIL  {0}{1}" -f $name, $(if ($detail) { " - $detail" } else { '' })); $script:fails++ }
}
function Wait-Window($process, [int]$seconds) {
    # The main window's title once it has one, or '' if the process exits or the time runs out.
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $p = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
        if (-not $p) { return '' }
        if ($p.MainWindowTitle) { return $p.MainWindowTitle }
    }
    return ''
}
function Process-Recording($exePath, $label) {
    # The walking skeleton end to end (plan step 3.4): the app opens the recording on its command line, processes its
    # first PCM channel with the defaults through the native core, writes one line to TMDQ_SMOKE_REPORT and exits.
    $rec = Join-Path $IN 'prn_testfile, sample.ch10'
    if (-not (Test-Path $rec)) { Check "$label processes a recording" $false 'the PRN sample is not in the payload'; return }
    $report = Join-Path $env:TEMP ('tmdq-smoke-' + [guid]::NewGuid().ToString('N') + '.txt')
    $env:TMDQ_SMOKE_REPORT = $report
    try {
        $sp = Start-Process $exePath -ArgumentList ('"' + $rec + '"') -PassThru
        $null = $sp.Handle
        $done = $sp.WaitForExit(180000)
        if (-not $done) { Stop-Process -Id $sp.Id -Force }
    } finally {
        Remove-Item Env:\TMDQ_SMOKE_REPORT -ErrorAction SilentlyContinue
    }
    $line = if (Test-Path $report) { (Get-Content $report -Raw).Trim() } else { '(no report)' }
    # The committed PRN sample's known answer: channel 13, 116 one-second windows, mean lock 93.532 %.
    Check "$label processes the PRN sample through the native core" ($done -and $sp.ExitCode -eq 0 -and $line.StartsWith('ok samples=116 mean=93.532 channel=13 ')) ("exit " + $sp.ExitCode + ": " + $line)
}
function Signatures($dir) {
    Get-ChildItem $dir -File | Where-Object { $_.Extension -in '.exe', '.dll' } | ForEach-Object {
        $s = Get-AuthenticodeSignature $_.FullName
        [pscustomobject]@{ Name = $_.Name; Status = [string]$s.Status
                           Signer = $(if ($s.SignerCertificate) { $s.SignerCertificate.Subject -replace '^CN=(?:"([^"]+)"|([^,]+)).*', '$1$2' } else { '-' }) }
    }
}

Say 'tmDataQualityAnalyzer (Avalonia) clean-machine test, with the upgrade from v2.x'
Say ('time    : ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Say ('windows : ' + (Get-CimInstance Win32_OperatingSystem).Caption)

# --- 0. The machine: Smart App Control must be ON, or nothing below says anything about it. ---
Say ''
Say '[0] Smart App Control'
$sac = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' -ErrorAction SilentlyContinue).VerifiedAndReputablePolicyState
$dg = Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard -ErrorAction SilentlyContinue
Check 'Smart App Control is on (1 = on, 0 off, 2 evaluation)' ($sac -eq 1) ("state " + $sac)
Say ('  code integrity: ' + $(if ($dg) { 'user mode enforcement ' + $dg.UsermodeCodeIntegrityPolicyEnforcementStatus } else { 'not reported' }))

# --- 1. Negative control: an unsigned build of the same exe must be blocked. ---
Say ''
Say '[1] Negative control: the UNSIGNED single-file exe'
$control = Join-Path $env:TEMP 'tmdq-unsigned'
New-Item -ItemType Directory -Force -Path $control | Out-Null
Copy-Item (Join-Path $IN 'unsigned\*') $control -Force
$u = Join-Path $control 'tmDataQualityAnalyzer.exe'
Check 'control exe is unsigned' ((Get-AuthenticodeSignature $u).Status -ne 'Valid') (Get-AuthenticodeSignature $u).Status
$blocked = $false; $how = ''
try {
    $cp = Start-Process $u -PassThru -ErrorAction Stop
    $null = $cp.Handle
    $title = Wait-Window $cp 15
    if ($title) { $how = "it RAN and opened '$title'"; Stop-Process -Id $cp.Id -Force } else { $blocked = $true; $how = 'process ended without a window, exit ' + $cp.ExitCode }
} catch {
    $blocked = $true; $how = 'refused to start: ' + $_.Exception.Message
}
Check 'Smart App Control blocks unsigned code here' $blocked $how

$app = 'C:\Program Files\tmDataQualityAnalyzer'
$exe = Join-Path $app 'tmDataQualityAnalyzer.exe'
$userFile = Join-Path $app 'settings\receiver_params\default.toml'
$marker = '# edited by the clean-machine test'
$prefsDir = Join-Path $env:APPDATA 'tmDataQualityAnalyzer'
function Install($path, $label) {
    $ip = Start-Process $path -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/ALLUSERS' -PassThru
    $null = $ip.Handle
    $ip.WaitForExit(300000) | Out-Null
    $ip.WaitForExit()
    Check "$label exit code 0" ($ip.ExitCode -eq 0) ('exit=' + $ip.ExitCode)
}

# --- 2. The Qt application, installed as a user has it: v2.14.2 with its default tasks (the .ch10 association and the
# full manual), a settings file the user edited, and the preferences it keeps in its INI file. ---
Say ''
Say '[2] Install v2.x (the Qt application) as a user has it'
$old = (Get-ChildItem (Join-Path $IN 'tmDataQualityAnalyzer-v2.*_setup.exe') | Select-Object -First 1).FullName
if (-not $old) { Check 'the v2.x setup is in the payload' $false 'pass deploy\tmDataQualityAnalyzer-v2.14.2_setup.exe' }
else {
    Say ('  setup: ' + (Split-Path $old -Leaf))
    Install $old 'v2.x installer'
    Check 'v2.x installed its exe in bin\' (Test-Path (Join-Path $app 'bin\tmDataQualityAnalyzer.exe')) ''
    Check 'v2.x installed its text guide' (Test-Path (Join-Path $app 'UserGuide.txt')) ''
    Check 'v2.x installed its full manual' (Test-Path (Join-Path $app 'UserManual.html')) ''
    if (Test-Path $userFile) { Add-Content -Path $userFile -Value $marker }
    Check 'the user''s edit is in a v2.x settings file' ((Test-Path $userFile) -and ((Get-Content $userFile -Raw) -match [regex]::Escape($marker))) ''
    # What the Qt application keeps, as its QSettings writes it (an INI file, main.cpp sets IniFormat). v3.1.0 does not
    # read it, so it must be left as it is.
    New-Item -ItemType Directory -Force -Path $prefsDir | Out-Null
    "[General]`r`nTheme=light`r`nLegendVisible=false`r`n" | Out-File -FilePath (Join-Path $prefsDir 'tmDataQualityAnalyzer.ini') -Encoding ASCII
}

# --- 3. The upgrade: v3.x over v2.x, silent and per-machine. ---
Say ''
Say '[3] Upgrade to this version'
$setup = (Get-ChildItem (Join-Path $IN 'tmDataQualityAnalyzer-v*_setup.exe') | Where-Object { $_.Name -notlike 'tmDataQualityAnalyzer-v2.*' } | Select-Object -First 1).FullName
Say ('  setup: ' + (Split-Path $setup -Leaf))
Check 'setup signed' ((Get-AuthenticodeSignature $setup).Status -eq 'Valid') (Get-AuthenticodeSignature $setup).Status
Install $setup 'installer'
foreach ($f in 'tmDataQualityAnalyzer.exe', 'tmdq_core.dll', 'libSkiaSharp.dll', 'libHarfBuzzSharp.dll', 'av_libglesv2.dll', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt') {
    Check "installed $f" (Test-Path (Join-Path $app $f)) ''
}
Check 'the Qt application''s bin\ folder is gone' (-not (Test-Path (Join-Path $app 'bin'))) ''
Check 'the Qt application''s text guide is gone' (-not (Test-Path (Join-Path $app 'UserGuide.txt'))) ''
$manualFile = Join-Path $app 'UserManual.html'
Check 'the full manual (a task v2.x had chosen) is this version''s' ((Test-Path $manualFile) -and ((Get-Content $manualFile -Raw) -match 'Help .{1,3} User Manual') -and ((Get-Content $manualFile -Raw) -notmatch 'Version 2\.')) ''
Check 'the user''s edited settings file survived the upgrade' ((Test-Path $userFile) -and ((Get-Content $userFile -Raw) -match [regex]::Escape($marker))) ''
$command = (Get-ItemProperty 'HKLM:\SOFTWARE\Classes\tmDataQualityAnalyzer.ch10\shell\open\command' -ErrorAction SilentlyContinue).'(default)'
Check '.ch10 opens the new exe' ($command -eq ('"' + $exe + '" "%1"')) ([string]$command)
$ext = (Get-ItemProperty 'HKLM:\SOFTWARE\Classes\.ch10' -ErrorAction SilentlyContinue).'(default)'
Check '.ch10 still names the ProgID' ($ext -eq 'tmDataQualityAnalyzer.ch10') ([string]$ext)
$entries = @(Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall' -ErrorAction SilentlyContinue |
             ForEach-Object { Get-ItemProperty $_.PSPath } | Where-Object { $_.DisplayName -like 'tmDataQualityAnalyzer*' })
Check 'one entry in Installed apps, not two' ($entries.Count -eq 1) (($entries | ForEach-Object { $_.DisplayName + ' ' + $_.DisplayVersion }) -join '; ')
# Compared as version numbers: the setup's ProductVersion string carries padding (seen: '2.14.2' followed by blanks),
# which a text comparison with the exe's '2.14.2.0' calls a mismatch.
$want = ((Get-Item $setup).VersionInfo.ProductVersion -replace '[^0-9.]', '').Trim('.')
if (Test-Path $exe) {
    $have = ((Get-Item $exe).VersionInfo.FileVersion -replace '[^0-9.]', '').Trim('.')
    $same = $false
    try { $a = [version]$want; $b = [version]$have; $same = ($a.Major -eq $b.Major -and $a.Minor -eq $b.Minor -and $a.Build -eq $b.Build) } catch { }
    Check 'exe version matches the setup' $same ($have + ' vs ' + $want)
}
foreach ($f in 'MSVCP140.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll') {
    Check "installed $f (the native core's C++ runtime)" (Test-Path (Join-Path $app $f)) ''
}
# The settings files (US8.0): installed, and never overwritten by an upgrade or a reinstall. The user's edit is the line
# appended under v2.x; a second silent install must leave it there too.
foreach ($f in 'framesync_patterns\framesync_rcvr_default.toml', 'framesync_patterns\framesync_PRN15.toml', 'receiver_params\default.toml', 'rcvr_cals\default.toml') {
    Check "installed settings\$f" (Test-Path (Join-Path $app "settings\$f")) ''
}
if (Test-Path $userFile) {
    $again = Start-Process $setup -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/ALLUSERS' -PassThru
    $null = $again.Handle
    $again.WaitForExit(300000) | Out-Null
    Check 'a reinstall keeps an edited settings file' ((Get-Content $userFile -Raw) -match [regex]::Escape($marker)) ''
}
Say '  signatures:'
$sigs = @(Signatures $app)
foreach ($s in $sigs) { Say ('    {0,-10} {1,-28} {2}' -f $s.Status, $s.Signer, $s.Name) }
Check 'every PE file validly signed' ((@($sigs | Where-Object { $_.Status -ne 'Valid' })).Count -eq 0) ("$($sigs.Count) files")

# --- 4. The installed app runs under Smart App Control; its first run starts from the default preferences and leaves the
# Qt application's INI file alone. ---
Say ''
Say '[4] Launch the installed app'
$ap = Start-Process $exe -PassThru
$null = $ap.Handle
$title = Wait-Window $ap 30
Check 'main window opened' ($title -eq 'TM Data Quality Analyzer') ("title '" + $title + "'")
$live = Get-Process -Id $ap.Id -ErrorAction SilentlyContinue
if ($live) {
    $natives = @($live.Modules | Where-Object { $_.ModuleName -match '^(libSkiaSharp|libHarfBuzzSharp)' })
    Check 'Skia and HarfBuzz loaded from the install folder' (($natives.Count -eq 2) -and (@($natives | Where-Object { $_.FileName -notlike "$app*" })).Count -eq 0) (($natives | ForEach-Object { $_.ModuleName }) -join ', ')
    Stop-Process -Id $ap.Id -Force
    Start-Sleep -Seconds 2
}
$prefs = Join-Path $prefsDir 'preferences.json'
$json = if (Test-Path $prefs) { Get-Content $prefs -Raw } else { '' }
Check 'the first run wrote the default preferences' (($json -match '"[Tt]heme"\s*:\s*"dark"') -and ($json -match '"[Ll]egendVisible"\s*:\s*true')) $(if ($json) { ($json -replace '\s+', ' ') } else { 'no preferences.json' })
$ini = Join-Path $prefsDir 'tmDataQualityAnalyzer.ini'
Check 'the Qt application''s INI file was left as it was' ((Test-Path $ini) -and ((Get-Content $ini -Raw) -match 'Theme=light')) ''
Process-Recording $exe 'the installed app'

# --- 5. The native core DLL loads and answers, on its own. ---
Say ''
Say '[5] Load tmdq_core.dll'
# LOAD_WITH_ALTERED_SEARCH_PATH: the DLL's own folder is searched for ITS dependencies (the VC++ runtime beside it),
# as it is for the app, which sits in that folder, and for .NET's NativeLibrary.Load. A plain LoadLibrary from this
# PowerShell searches beside powershell.exe instead, and failed with error 126 although the runtime was installed.
Add-Type -Namespace TmdqProbe -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
public static extern System.IntPtr LoadLibraryExW(string path, System.IntPtr file, uint flags);
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Ansi, SetLastError = true)]
public static extern System.IntPtr GetProcAddress(System.IntPtr module, string name);
[System.Runtime.InteropServices.UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.Cdecl)]
public delegate int IntFn();
'@
$h = [TmdqProbe.Native]::LoadLibraryExW((Join-Path $app 'tmdq_core.dll'), [IntPtr]::Zero, 0x8)
$err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
Check 'tmdq_core.dll loads with its C++ runtime' ($h -ne [IntPtr]::Zero) $(if ($h -eq [IntPtr]::Zero) { 'error ' + $err } else { '' })
if ($h -ne [IntPtr]::Zero) {
    # And it is usable, not merely mapped: call into it.
    $fn = [TmdqProbe.Native]::GetProcAddress($h, 'tmdq_abi_version')
    $abi = if ($fn -ne [IntPtr]::Zero) { [System.Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($fn, [TmdqProbe.Native+IntFn]).Invoke() } else { -1 }
    Check 'tmdq_abi_version() answers' ($abi -gt 0) ('ABI version ' + $abi)
}

# --- 6. The portable ZIP, extracted somewhere new, also runs. ---
Say ''
Say '[6] Portable ZIP'
$zip = (Get-ChildItem (Join-Path $IN '*_portable.zip') | Select-Object -First 1).FullName
$pdir = Join-Path $env:USERPROFILE 'Desktop\portable'
Expand-Archive -Path $zip -DestinationPath $pdir -Force
$pexe = (Get-ChildItem $pdir -Recurse -Filter 'tmDataQualityAnalyzer.exe' | Select-Object -First 1).FullName
Check 'ZIP holds the exe' ([bool]$pexe) $pexe
Check 'ZIP holds the third-party notices' ([bool](Get-ChildItem $pdir -Recurse -Filter 'THIRD-PARTY-NOTICES.txt')) ''
Check 'ZIP holds the full manual, the readme and the portable marker' ((Get-ChildItem $pdir -Recurse -Filter 'UserManual.html') -and (Get-ChildItem $pdir -Recurse -Filter 'README.txt') -and (Get-ChildItem $pdir -Recurse -Filter 'portable')) ''
if ($pexe) {
    $pp = Start-Process $pexe -PassThru
    $null = $pp.Handle
    $ptitle = Wait-Window $pp 30
    Check 'portable app opened its window' ($ptitle -eq 'TM Data Quality Analyzer') ("title '" + $ptitle + "'")
    if (Get-Process -Id $pp.Id -ErrorAction SilentlyContinue) { Stop-Process -Id $pp.Id -Force }
    Process-Recording $pexe 'the portable app'
}

# --- 7. What Code Integrity blocked while this ran. ---
Say ''
Say '[7] Code Integrity events since the test began'
$events = @(Get-WinEvent -FilterHashtable @{ LogName = 'Microsoft-Windows-CodeIntegrity/Operational'; StartTime = $started } -ErrorAction SilentlyContinue |
            Where-Object { $_.Id -in 3033, 3034, 3077, 3089 })
$ours = @($events | Where-Object { $_.Message -match 'tmDataQualityAnalyzer|tmdq_core|libSkiaSharp|libHarfBuzzSharp|av_libglesv2' })
foreach ($e in $ours | Select-Object -First 12) { Say ('  event ' + $e.Id + ': ' + (($e.Message -split "`n")[0]).Trim()) }
$blockedOurs = @($ours | Where-Object { $_.Message -notmatch 'tmdq-unsigned' -and $_.Id -in 3033, 3077 })
Check 'nothing of the signed packages was blocked' ($blockedOurs.Count -eq 0) $(if ($blockedOurs.Count) { "$($blockedOurs.Count) block event(s)" } else { "$($ours.Count) event(s) about our files, all for the unsigned control or informational" })

# --- 8. Uninstall. ---
Say ''
Say '[8] Uninstall'
$unins = Get-ChildItem $app -Filter 'unins*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($unins) {
    Check 'uninstaller signed' ((Get-AuthenticodeSignature $unins.FullName).Status -eq 'Valid') (Get-AuthenticodeSignature $unins.FullName).Status
    $up = Start-Process $unins.FullName -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -PassThru
    $null = $up.Handle
    $up.WaitForExit(300000) | Out-Null
    Start-Sleep -Seconds 5
    Check 'application removed' (-not (Test-Path $exe)) ''
    Check 'settings left for the user' (Test-Path $userFile) ''
    # uninsdeletevalue on .ch10 and uninsdeletekey on the ProgID, as v2.x: the empty .ch10 key is left on purpose.
    $left = (Get-ItemProperty 'HKLM:\SOFTWARE\Classes\.ch10' -ErrorAction SilentlyContinue).'(default)'
    Check '.ch10 association cleared' ([string]::IsNullOrEmpty($left)) $(if ($left) { "still '$left'" } else { '' })
    Check 'ProgID key removed' (-not (Test-Path 'HKLM:\SOFTWARE\Classes\tmDataQualityAnalyzer.ch10')) ''
    Check 'no entry left in Installed apps' (@(Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' | ForEach-Object { Get-ItemProperty $_.PSPath } | Where-Object { $_.DisplayName -like 'tmDataQualityAnalyzer*' }).Count -eq 0) ''
} else {
    Check 'uninstaller found' $false 'no unins*.exe'
}

Say ''
Say ('RESULT: ' + $(if ($fails -eq 0) { 'ALL CHECKS PASSED' } else { "$fails CHECK(S) FAILED" }))
$lines | Out-File -FilePath $LOG -Encoding ASCII
Write-Host ''
Write-Host "Results written to $LOG - this window stays open."
Write-Host 'Press Enter to close.'
[void](Read-Host)
