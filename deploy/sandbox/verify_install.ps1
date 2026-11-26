# =============================================================================
# verify_install.ps1 - clean-machine test of the application's packages under Smart App Control: the MSI
# (deploy\tmDataQualityAnalyzer.wxs) installed, upgraded, refused on a downgrade, switched and uninstalled silently, the
# refusal to install beside an Inno Setup install (what every release before the MSI was), and the portable ZIP.
#
# Runs INSIDE Windows Sandbox, launched by
#   run_sandbox_test.ps1 [-Setup <the release's setup.msi>]
# which copies in the payload this script reads from C:\tmdqa: the release's MSI, TWO NEWER MSIs of the same product (built
# unsigned by the runner from the same staged files, for the upgrades), the portable ZIP, deploy\staging\unsigned, the PRN
# sample and v2.14.2's setup.exe (the Inno install the MSI must refuse to sit beside). run_sandbox_test.ps1 -Payload
# overrides the list; semicolon-separated, because the committed recordings' names contain commas.
# Do not run it on a developer machine: it installs to Program Files and uninstalls again.
#
# What it proves: on a machine that has never seen the application, with Smart App Control ON, the signed MSI installs
# silently and every file arrives; the .ch10 offer (never a default taken), the Start menu entry and the settings are as they should be; an
# upgrade replaces the files and keeps an edited settings file; the older MSI over the newer is refused; the installed
# application opens its window and processes a recording, its native core loads, and Code Integrity blocked nothing of
# ours; an uninstall removes it and leaves the settings; the administrator's switches work; the portable ZIP runs too; and
# an UNSIGNED build of the same exe IS blocked, which is what shows the enforcement was real and not absent.
#
# The interactive wizard is NOT exercised: this drives msiexec silently, as a deployment tool does.
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

function Say($text) {
    $script:lines += $text
    Write-Host $text
    # Also appended as it happens, so the host can read how far a slow step has got (results.txt is written only at the end).
    Add-Content -Path (Join-Path $OUT 'progress.txt') -Value $text -ErrorAction SilentlyContinue
}
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
function Invoke-RecordingRun($exePath, $label) {
    # The walking skeleton end to end: the app opens the recording on its command line, processes its first PCM channel
    # with the defaults through the native core, writes one line to TMDQ_SMOKE_REPORT and exits.
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
function Get-FileSignature($dir) {
    Get-ChildItem $dir -File | Where-Object { $_.Extension -in '.exe', '.dll' } | ForEach-Object {
        $s = Get-AuthenticodeSignature $_.FullName
        [pscustomobject]@{ Name = $_.Name; Status = [string]$s.Status
                           Signer = $(if ($s.SignerCertificate) { $s.SignerCertificate.Subject -replace '^CN=(?:"([^"]+)"|([^,]+)).*', '$1$2' } else { '-' }) }
    }
}
function Invoke-Msi([string[]]$ArgList, [string]$LogName) {
    # msiexec runs elevated here (the Sandbox user is an administrator). The exit code is what a deployment tool reads.
    $logPath = Join-Path $OUT $LogName
    $all = @($ArgList) + @('/qn', '/norestart', '/l*v', ('"' + $logPath + '"'))
    $p = Start-Process msiexec.exe -ArgumentList $all -PassThru
    $null = $p.Handle
    $clock = [Diagnostics.Stopwatch]::StartNew()
    # A heartbeat every 20 s, so a slow step can be told from a hung one: in the Sandbox every install of the 50 MB MSI
    # (signed or not) sat about 2 minutes verifying the package before it started, which the same package does not do
    # on the development machine.
    while (-not $p.WaitForExit(20000)) {
        $size = if (Test-Path $logPath) { (Get-Item $logPath).Length } else { -1 }
        $procs = @(Get-CimInstance Win32_Process -Filter "Name='msiexec.exe'" | ForEach-Object { '{0}:read={1}MB' -f $_.ProcessId, [int]($_.ReadTransferCount / 1MB) })
        Say ('  ... {0}: {1:N0} s, log {2} bytes, msiexec {3}' -f $LogName, $clock.Elapsed.TotalSeconds, $size, ($procs -join ' '))
        if ($clock.Elapsed.TotalSeconds -gt 900) { Say "  ... $LogName gave up after 900 s"; return -1 }
    }
    $p.WaitForExit()
    Say ('  {0} took {1:N0} s' -f $LogName, $clock.Elapsed.TotalSeconds)
    return $p.ExitCode
}
function Get-ArpEntry {
    @(Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall' -ErrorAction SilentlyContinue |
      ForEach-Object { Get-ItemProperty $_.PSPath } | Where-Object { $_.DisplayName -like 'tmDataQualityAnalyzer*' })
}
function Get-MsiVersion([string]$name) { [version]($name -replace '^.*-v(\d+\.\d+\.\d+)_.*$', '$1') }
function Test-Ch10Offered {
    # The MSI offers the application for .ch10 by listing its ProgID under .ch10\OpenWithProgids, and never writes .ch10's
    # default value (which another application may hold, and which an uninstall would delete whoever's it had become).
    $key = Get-Item 'HKLM:\SOFTWARE\Classes\.ch10\OpenWithProgids' -ErrorAction SilentlyContinue
    return (($null -ne $key) -and ($key.GetValueNames() -contains 'tmDataQualityAnalyzer.ch10'))
}

$app = 'C:\Program Files\tmDataQualityAnalyzer'
$exe = Join-Path $app 'tmDataQualityAnalyzer.exe'
$userFile = Join-Path $app 'settings\receiver_params\default.toml'
$marker = '# edited by the clean-machine test'
$startMenu = 'C:\ProgramData\Microsoft\Windows\Start Menu\Programs\tmDataQualityAnalyzer'
$desktopLink = 'C:\Users\Public\Desktop\tmDataQualityAnalyzer.lnk'
$progId = 'HKLM:\SOFTWARE\Classes\tmDataQualityAnalyzer.ch10'
$prefsDir = Join-Path $env:APPDATA 'tmDataQualityAnalyzer'

Say 'tmDataQualityAnalyzer clean-machine test (MSI and portable ZIP)'
Say ('time    : ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Say ('windows : ' + (Get-CimInstance Win32_OperatingSystem).Caption)

$msis = @(Get-ChildItem (Join-Path $IN 'tmDataQualityAnalyzer-v*_setup.msi') | Sort-Object { Get-MsiVersion $_.Name })
$old2x = (Get-ChildItem (Join-Path $IN 'tmDataQualityAnalyzer-v2.*_setup.exe') -ErrorAction SilentlyContinue | Select-Object -First 1)
if ($msis.Count -lt 3) {
    Check 'the payload holds three MSI versions (the release, and two newer for the upgrades)' $false ('found ' + $msis.Count)
    $lines | Out-File -FilePath $LOG -Encoding ASCII
    Write-Host 'Press Enter to close.'; [void](Read-Host); exit 1
}
# Windows reads a package several times (its signature check, its cached copy, its cabinet) and the mapped folder is slow
# to read, so the packages are copied to the local disk once and installed from there.
$LOCAL = 'C:\tmdqa-local'
New-Item -ItemType Directory -Force -Path $LOCAL | Out-Null
function Copy-Local($file) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $dest = Join-Path $LOCAL $file.Name
    Copy-Item $file.FullName $dest -Force
    Say ('  copied {0} to the local disk in {1:N0} s' -f $file.Name, $clock.Elapsed.TotalSeconds)
    return (Get-Item $dest)
}
$first = Copy-Local $msis[0]; $second = Copy-Local $msis[1]; $third = Copy-Local $msis[$msis.Count - 1]
if ($old2x) { $old2x = Copy-Local $old2x }
$v1 = Get-MsiVersion $first.Name; $v2 = Get-MsiVersion $second.Name; $v3 = Get-MsiVersion $third.Name
Say ("release MSI: {0}  newer MSIs (for the upgrades): {1}, {2}" -f $first.Name, $second.Name, $third.Name)
Check 'the three MSIs are different versions' (($v1 -lt $v2) -and ($v2 -lt $v3)) "$v1, $v2 and $v3"
$relSig = Get-AuthenticodeSignature $first.FullName
Check 'the release MSI is signed' ($relSig.Status -eq 'Valid') ([string]$relSig.Status)

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

# --- 2. An Inno Setup install must make the MSI refuse, and say so. ---
Say ''
Say '[2] The MSI refuses to install beside an Inno Setup install'
if (-not $old2x) {
    Check 'the v2.x setup is in the payload' $false 'pass deploy\tmDataQualityAnalyzer-v2.14.2_setup.exe'
} else {
    $ip = Start-Process $old2x.FullName -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/ALLUSERS' -PassThru
    $null = $ip.Handle; $ip.WaitForExit(300000) | Out-Null; $ip.WaitForExit()
    Check 'v2.x installed (Inno)' ($ip.ExitCode -eq 0) ('exit=' + $ip.ExitCode)
    $code = Invoke-Msi @('/i', ('"' + $first.FullName + '"')) 'refused.log'
    Check 'the MSI stopped (a launch condition: exit 1603)' ($code -eq 1603) ('exit=' + $code)
    $why = if (Test-Path (Join-Path $OUT 'refused.log')) { (Select-String -Path (Join-Path $OUT 'refused.log') -Pattern 'earlier tmDataQualityAnalyzer' -SimpleMatch -ErrorAction SilentlyContinue | Select-Object -First 1) } else { $null }
    Check 'the log carries the message that says what to do' ([bool]$why) ''
    Check 'the MSI installed nothing' (-not (Test-Path (Join-Path $app 'tmdq_core.dll'))) ''
    $unins = Get-ChildItem $app -Filter 'unins*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($unins) {
        $up = Start-Process $unins.FullName -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -PassThru
        $null = $up.Handle; $up.WaitForExit(300000) | Out-Null; $up.WaitForExit()
        Start-Sleep -Seconds 3
    }
    Check 'v2.x uninstalled' (-not (Test-Path (Join-Path $app 'bin\tmDataQualityAnalyzer.exe'))) ''
    Remove-Item $app -Recurse -Force -ErrorAction SilentlyContinue
}

# --- 3. A fresh silent install. ---
Say ''
Say '[3] Fresh silent install of the release MSI'
$code = Invoke-Msi @('/i', ('"' + $first.FullName + '"')) 'install1.log'
Check 'msiexec exit code 0' ($code -eq 0) ('exit=' + $code)
foreach ($f in 'tmDataQualityAnalyzer.exe', 'tmdq_core.dll', 'libSkiaSharp.dll', 'libHarfBuzzSharp.dll', 'av_libglesv2.dll', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'RELEASENOTES.txt', 'UserManual.html', 'MSVCP140.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll') {
    Check "installed $f" (Test-Path (Join-Path $app $f)) ''
}
foreach ($f in 'framesync_patterns\framesync_rcvr_default.toml', 'framesync_patterns\framesync_PRN15.toml', 'receiver_params\default.toml', 'rcvr_cals\default.toml') {
    Check "installed settings\$f" (Test-Path (Join-Path $app "settings\$f")) ''
}
$entries = @(Get-ArpEntry)
Check 'one entry in Installed apps' ($entries.Count -eq 1) (($entries | ForEach-Object { $_.DisplayName + ' ' + $_.DisplayVersion }) -join '; ')
if ($entries.Count -eq 1) { Check 'its version is the MSI''s' ([version]$entries[0].DisplayVersion -eq $v1) ([string]$entries[0].DisplayVersion) }
$ext = (Get-ItemProperty 'HKLM:\SOFTWARE\Classes\.ch10' -ErrorAction SilentlyContinue).'(default)'
Check '.ch10 offers the application (OpenWithProgids)' (Test-Ch10Offered) ''
Check 'the install took no .ch10 default' ([string]::IsNullOrEmpty($ext)) $(if ($ext) { "it set '$ext'" } else { '' })
$command = (Get-ItemProperty (Join-Path $progId 'shell\open\command') -ErrorAction SilentlyContinue).'(default)'
Check 'the ProgID opens the installed exe' ($command -eq ('"' + $exe + '" "%1"')) ([string]$command)
Check 'a Start menu shortcut' (Test-Path (Join-Path $startMenu 'tmDataQualityAnalyzer.lnk')) ''
Check 'no desktop shortcut by default' (-not (Test-Path $desktopLink)) ''
Say '  signatures:'
$sigs = @(Get-FileSignature $app)
foreach ($s in $sigs) { Say ('    {0,-10} {1,-28} {2}' -f $s.Status, $s.Signer, $s.Name) }
Check 'every PE file validly signed' ((@($sigs | Where-Object { $_.Status -ne 'Valid' })).Count -eq 0) ("$($sigs.Count) files")
if (Test-Path $userFile) { Add-Content -Path $userFile -Value $marker }
Check 'the user edit is in a settings file' ((Test-Path $userFile) -and ((Get-Content $userFile -Raw) -match [regex]::Escape($marker))) ''

# --- 4. Upgrade, then a downgrade the MSI must refuse. ---
Say ''
Say '[4] Upgrade to the newer MSI'
$code = Invoke-Msi @('/i', ('"' + $second.FullName + '"')) 'upgrade.log'
Check 'msiexec exit code 0' ($code -eq 0) ('exit=' + $code)
$entries = @(Get-ArpEntry)
Check 'still one entry in Installed apps' ($entries.Count -eq 1) (($entries | ForEach-Object { $_.DisplayName + ' ' + $_.DisplayVersion }) -join '; ')
if ($entries.Count -eq 1) { Check 'its version is the newer MSI''s' ([version]$entries[0].DisplayVersion -eq $v2) ([string]$entries[0].DisplayVersion) }
Check 'the exe is still installed' (Test-Path $exe) ''
Check 'the edited settings file survived the upgrade' ((Test-Path $userFile) -and ((Get-Content $userFile -Raw) -match [regex]::Escape($marker))) ''
Check 'the Start menu shortcut is still there' (Test-Path (Join-Path $startMenu 'tmDataQualityAnalyzer.lnk')) ''
Check '.ch10 still offers the application' (Test-Ch10Offered) ''

# An upgrade that DECLINES the association (the case the Inno installer's [Code] section handled, #397: an upgrade that
# declined it left .ch10 naming a ProgID that had just been deleted, and Explorer offered a dead entry). The MSI over an
# older MSI removes the older product's registry entries, so the offer and the ProgID should both be gone.
Say ''
Say '[4b] Upgrade that declines the association (ASSOCIATE_CH10=0)'
$code = Invoke-Msi @('/i', ('"' + $third.FullName + '"'), 'ASSOCIATE_CH10=0') 'upgrade-declined.log'
Check 'msiexec exit code 0' ($code -eq 0) ('exit=' + $code)
$entries = @(Get-ArpEntry)
Check 'still one entry in Installed apps' ($entries.Count -eq 1) (($entries | ForEach-Object { $_.DisplayName + ' ' + $_.DisplayVersion }) -join '; ')
if ($entries.Count -eq 1) { Check 'its version is the newest MSI''s' ([version]$entries[0].DisplayVersion -eq $v3) ([string]$entries[0].DisplayVersion) }
$ext = (Get-ItemProperty 'HKLM:\SOFTWARE\Classes\.ch10' -ErrorAction SilentlyContinue).'(default)'
Check '.ch10 no longer offers the application' (-not (Test-Ch10Offered)) ''
Check '.ch10 names no ProgID that is gone' ([string]::IsNullOrEmpty($ext)) $(if ($ext) { "still '$ext'" } else { '' })
Check 'the ProgID key is gone' (-not (Test-Path $progId)) ''
Check 'the exe is installed' (Test-Path $exe) ''
Check 'the edited settings file survived' ((Test-Path $userFile) -and ((Get-Content $userFile -Raw) -match [regex]::Escape($marker))) ''
$code = Invoke-Msi @('/i', ('"' + $first.FullName + '"')) 'downgrade.log'
Check 'installing the older MSI over the newer one is refused' ($code -ne 0) ('exit=' + $code)
$entries = @(Get-ArpEntry)
if ($entries.Count -eq 1) { Check 'the newest version is still installed' ([version]$entries[0].DisplayVersion -eq $v3) ([string]$entries[0].DisplayVersion) }

# --- 5. The installed app runs under Smart App Control; its first run starts from the default preferences. ---
Say ''
Say '[5] Launch the installed app'
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
Invoke-RecordingRun $exe 'the installed app'

# --- 6. The native core DLL loads and answers, on its own. ---
Say ''
Say '[6] Load tmdq_core.dll'
# LOAD_WITH_ALTERED_SEARCH_PATH: the DLL's own folder is searched for ITS dependencies (the VC++ runtime beside it),
# as it is for the app, which sits in that folder, and for .NET's NativeLibrary.Load. A plain LoadLibrary from this
# PowerShell searches beside powershell.exe instead, and failed with error 126 although the runtime was installed.
Add-Type -Namespace TmdqProbe -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
public static extern System.IntPtr LoadLibraryExW(string path, System.IntPtr file, uint flags);
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Ansi, SetLastError = true)]
public static extern System.IntPtr GetProcAddress(System.IntPtr module, string name);
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern bool FreeLibrary(System.IntPtr module);
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
    # Let go of it. This process would otherwise hold the installed DLL when the MSI is uninstalled: Windows Installer's
    # Restart Manager then shuts down the process that holds a file (it did: this very script, which never wrote its results).
    [void][TmdqProbe.Native]::FreeLibrary($h)
}

# --- 7. Uninstall. ---
Say ''
Say '[7] Silent uninstall'
$code = Invoke-Msi @('/x', ('"' + $third.FullName + '"')) 'uninstall.log'
Check 'msiexec exit code 0' ($code -eq 0) ('exit=' + $code)
Check 'the exe is removed' (-not (Test-Path $exe)) ''
Check 'the native core is removed' (-not (Test-Path (Join-Path $app 'tmdq_core.dll'))) ''
Check 'the settings are left for the user, edit included' ((Test-Path $userFile) -and ((Get-Content $userFile -Raw) -match [regex]::Escape($marker))) ''
$left = (Get-ItemProperty 'HKLM:\SOFTWARE\Classes\.ch10' -ErrorAction SilentlyContinue).'(default)'
Check '.ch10 no longer offers the application' (-not (Test-Ch10Offered)) ''
Check '.ch10 names no ProgID that is gone' ([string]::IsNullOrEmpty($left)) $(if ($left) { "still '$left'" } else { '' })
Check 'ProgID key removed' (-not (Test-Path $progId)) ''
Check 'no entry left in Installed apps' (@(Get-ArpEntry).Count -eq 0) ''
Check 'the Start menu folder is removed' (-not (Test-Path $startMenu)) ''

# --- 8. The switches an administrator uses. ---
Say ''
Say '[8] Silent install with ASSOCIATE_CH10=0 FULLMANUAL=0 DESKTOPSHORTCUT=1'
$code = Invoke-Msi @('/i', ('"' + $third.FullName + '"'), 'ASSOCIATE_CH10=0', 'FULLMANUAL=0', 'DESKTOPSHORTCUT=1') 'switches.log'
Check 'msiexec exit code 0' ($code -eq 0) ('exit=' + $code)
Check 'the exe is installed' (Test-Path $exe) ''
Check 'the application is not offered for .ch10, and no ProgID' ((-not (Test-Ch10Offered)) -and (-not (Test-Path $progId))) ''
Check 'no full manual' (-not (Test-Path (Join-Path $app 'UserManual.html'))) ''
Check 'a desktop shortcut' (Test-Path $desktopLink) ''
Check 'the edited settings file is still there (never overwritten)' ((Test-Path $userFile) -and ((Get-Content $userFile -Raw) -match [regex]::Escape($marker))) ''
$code = Invoke-Msi @('/x', ('"' + $third.FullName + '"')) 'uninstall2.log'
Check 'uninstall exit code 0' ($code -eq 0) ('exit=' + $code)
Check 'the desktop shortcut is removed' (-not (Test-Path $desktopLink)) ''
Check 'no entry left in Installed apps' (@(Get-ArpEntry).Count -eq 0) ''

# --- 8b. Another application's association is left alone by an uninstall (what the Inno [Code] section did for an upgrade:
# it deleted .ch10's default value only when that named OUR ProgID). The application is offered for .ch10, another
# application then takes .ch10 for itself (as an installer of its own would), and the MSI is uninstalled: that value must
# still be there, and ours must be gone. The first version of the MSI wrote .ch10's default itself, and the uninstall
# deleted whoever's it had become. ---
Say ''
Say '[8b] An association another application took is left alone by an uninstall'
$other = 'OtherApp.ch10'
$code = Invoke-Msi @('/i', ('"' + $third.FullName + '"')) 'install-assoc.log'
Check 'msiexec exit code 0' ($code -eq 0) ('exit=' + $code)
Check '.ch10 offers the application after the install' (Test-Ch10Offered) ''
if (-not (Test-Path 'HKLM:\SOFTWARE\Classes\.ch10')) { New-Item -Path 'HKLM:\SOFTWARE\Classes\.ch10' -Force | Out-Null }
Set-ItemProperty -Path 'HKLM:\SOFTWARE\Classes\.ch10' -Name '(default)' -Value $other
$code = Invoke-Msi @('/x', ('"' + $third.FullName + '"')) 'uninstall3.log'
Check 'uninstall exit code 0' ($code -eq 0) ('exit=' + $code)
$ext = (Get-ItemProperty 'HKLM:\SOFTWARE\Classes\.ch10' -ErrorAction SilentlyContinue).'(default)'
Check 'the other application''s .ch10 value is left alone' ($ext -eq $other) $(if ($ext) { "now '$ext'" } else { 'now empty: the uninstall deleted it' })
Check 'our offer for .ch10 is gone' (-not (Test-Ch10Offered)) ''
Check 'our ProgID key is gone' (-not (Test-Path $progId)) ''
if (Test-Path 'HKLM:\SOFTWARE\Classes\.ch10') { Remove-Item 'HKLM:\SOFTWARE\Classes\.ch10' -Recurse -Force -ErrorAction SilentlyContinue }

# --- 9. The portable ZIP, extracted somewhere new, also runs. ---
Say ''
Say '[9] Portable ZIP'
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
    Invoke-RecordingRun $pexe 'the portable app'
}

# --- 10. What Code Integrity blocked while this ran. ---
Say ''
Say '[10] Code Integrity events since the test began'
$events = @(Get-WinEvent -FilterHashtable @{ LogName = 'Microsoft-Windows-CodeIntegrity/Operational'; StartTime = $started } -ErrorAction SilentlyContinue |
            Where-Object { $_.Id -in 3033, 3034, 3077, 3089 })
$ours = @($events | Where-Object { $_.Message -match 'tmDataQualityAnalyzer|tmdq_core|libSkiaSharp|libHarfBuzzSharp|av_libglesv2' })
foreach ($e in $ours | Select-Object -First 12) { Say ('  event ' + $e.Id + ': ' + (($e.Message -split "`n")[0]).Trim()) }
$blockedOurs = @($ours | Where-Object { $_.Message -notmatch 'tmdq-unsigned' -and $_.Id -in 3033, 3077 })
Check 'nothing of the signed packages was blocked' ($blockedOurs.Count -eq 0) $(if ($blockedOurs.Count) { "$($blockedOurs.Count) block event(s)" } else { "$($ours.Count) event(s) about our files, all for the unsigned control or informational" })

Say ''
Say ('RESULT: ' + $(if ($fails -eq 0) { 'ALL CHECKS PASSED' } else { "$fails CHECK(S) FAILED" }))
$lines | Out-File -FilePath $LOG -Encoding ASCII
Write-Host ''
Write-Host "Results written to $LOG - this window stays open."
Write-Host 'Press Enter to close.'
[void](Read-Host)
