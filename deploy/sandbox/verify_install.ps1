# =============================================================================
# verify_install.ps1 - clean-machine acceptance test for the shipped installer.
#
# Runs INSIDE Windows Sandbox, launched by run_sandbox_test.ps1 beside this file.
# Do not run it on a developer machine: it installs to Program Files, launches the
# app, and uninstalls again.
#
# Sandbox is the point: a machine that has never held
# this application's files, settings or registry state. The host can only ever
# prove the weaker claim that an install is correctly signed and versioned.
#
# ASCII only, and written for Windows PowerShell 5.1 - that is what Sandbox has,
# and this project has twice shipped a script verified under pwsh that could not
# run under 5.1.
# =============================================================================

$ErrorActionPreference = 'Continue'
$OUT = 'C:\tmdqa-out'
$LOG = Join-Path $OUT 'results.txt'
$lines = @()
$fails = 0

function Say($text) {
    $script:lines += $text
    Write-Host $text
}
function Check($name, $ok, $detail) {
    if ($ok) { Say ("  PASS  {0}{1}" -f $name, $(if ($detail) { " - $detail" } else { "" })) }
    else     { Say ("  FAIL  {0}{1}" -f $name, $(if ($detail) { " - $detail" } else { "" })); $script:fails++ }
}

Say "tmDataQualityAnalyzer clean-machine install test"
Say ("host time: " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Say ("windows  : " + (Get-CimInstance Win32_OperatingSystem).Caption)
Say ""

# --- 0. Nothing of ours exists yet -------------------------------------------
Say "[0] Clean slate"
Check "no install directory" (-not (Test-Path 'C:\Program Files\tmDataQualityAnalyzer')) ''
Check "no .ch10 association" (-not (Test-Path 'HKLM:\SOFTWARE\Classes\.ch10')) ''

# --- 1. Silent install --------------------------------------------------------
Say ""
Say "[1] Silent install (per-machine)"
$setup = (Get-ChildItem 'C:\tmdqa\*_setup.exe' | Select-Object -First 1).FullName
Say ("  setup: " + (Split-Path $setup -Leaf))
$p = Start-Process $setup -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/ALLUSERS' -PassThru
$null = $p.Handle          # 5.1 caches no handle otherwise; ExitCode reads empty
$p.WaitForExit(300000) | Out-Null
$p.WaitForExit()
Check "installer exit code 0" ($p.ExitCode -eq 0) ("exit=" + $p.ExitCode)

$app = 'C:\Program Files\tmDataQualityAnalyzer'
$exe = Join-Path $app 'bin\tmDataQualityAnalyzer.exe'
Check "exe installed" (Test-Path $exe) $exe
if (Test-Path $exe) {
    # Derived from the setup.exe under test, never a literal. This line used to
    # assert -like '2.14.1*', which passed for exactly one release and then called a
    # correct 2.14.2.0 install a failure. The project keeps ONE version source and
    # treats a hardcoded version anywhere else as a bug to remove.
    #
    # An Inno setup.exe carries its version in ProductVersion, not FileVersion.
    $want = (Get-Item $setup).VersionInfo.ProductVersion
    $v    = (Get-Item $exe).VersionInfo.FileVersion
    Check "exe version matches the setup it came from" ($v -like "$want*") ("installed $v, setup says $want")
    Check "exe signature" ((Get-AuthenticodeSignature $exe).Status -eq 'Valid') (Get-AuthenticodeSignature $exe).Status
}
Check "qwindows platform plugin" (Test-Path (Join-Path $app 'bin\platforms\qwindows.dll')) 'the classic omission'
Check "settings tree" (Test-Path (Join-Path $app 'settings')) ''
Check "UserGuide.txt" (Test-Path (Join-Path $app 'UserGuide.txt')) ''
Check "no vc_redist left behind" (-not (Test-Path (Join-Path $app 'bin\vc_redist.x64.exe'))) 'removed in v2.13.1'

# --- 2. Shipped settings, and what a first run does to them -------------------
Say ""
Say "[2] Shipped settings (pre-launch)"
$before = @{}
Get-ChildItem (Join-Path $app 'settings') -Recurse -Filter *.toml -ErrorAction SilentlyContinue | ForEach-Object {
    $before[$_.FullName] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
    Say ("  " + $_.FullName.Substring($app.Length + 1))
}
Check "settings files present" ($before.Count -gt 0) ("$($before.Count) .toml")

$prn = Join-Path $app 'settings\framesync_patterns\framesync_PRN15.toml'
if (Test-Path $prn) {
    # Randomized = false landed AFTER v2.14.1, so it is informational against that
    # build and a real check against anything later. Asserting it unconditionally
    # reported a failure for an artifact that could not have carried it.
    #
    # This literal is a HISTORICAL boundary - the release the key landed after - not
    # the current version, so it does not rot the way the exe-version check did.
    $hasRand = (Get-Content $prn -Raw) -match '(?m)^\s*Randomized\s*=\s*false'
    $ver = if (Test-Path $exe) { [version](Get-Item $exe).VersionInfo.FileVersion } else { [version]'0.0.0.0' }
    if ($ver -gt [version]'2.14.1.0') {
        Check "PRN15 carries Randomized = false" $hasRand ''
    } else {
        Say ("  INFO  PRN15 Randomized key: " + $(if ($hasRand) { 'present' } else { 'absent - expected, it landed after 2.14.1' }))
    }
}

# --- 3. Launch with no Qt anywhere on the machine -----------------------------
Say ""
Say "[3] Launch (no Qt on PATH, no QTDIR - a clean machine by construction)"
Remove-Item Env:QTDIR -ErrorAction SilentlyContinue
$proc = Start-Process $exe -PassThru
$null = $proc.Handle
Start-Sleep -Seconds 12
$alive = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
Check "process alive after 12s" ($null -ne $alive) $(if ($alive) { "pid " + $proc.Id } else { "exited: " + $proc.ExitCode })

if ($alive) {
    Check "main window created" ($alive.MainWindowHandle -ne 0) ("title='" + $alive.MainWindowTitle + "'")
    # Only the DLLs we SHIP. msvcp_win.dll is a Windows UCRT component that lives
    # in System32 on every machine and is not in our payload - matching it here was a
    # false alarm on the first run of this script.
    $mods = @($alive.Modules | Where-Object { $_.ModuleName -match '^(Qt6|qwindows|MSVCP140|VCRUNTIME140)' })
    $outside = @($mods | Where-Object { $_.FileName -notlike "$app*" })
    Check "Qt/CRT modules loaded" ($mods.Count -gt 0) ("$($mods.Count) modules")
    Check "all loaded from install dir" ($outside.Count -eq 0) $(if ($outside.Count) { ($outside | ForEach-Object { $_.ModuleName + ' <- ' + $_.FileName }) -join '; ' } else { 'none from system' })
    Say "  loaded Qt/CRT modules:"
    $mods | Sort-Object ModuleName | ForEach-Object { Say ("    " + $_.ModuleName) }
    Stop-Process -Id $proc.Id -Force
    Start-Sleep -Seconds 3
}

# --- 4. What the first run wrote ---------------------------------------------
Say ""
Say "[4] First-run side effects"
$changed = @()
foreach ($f in $before.Keys) {
    if (-not (Test-Path $f)) { $changed += "DELETED $f"; continue }
    if ((Get-FileHash $f -Algorithm SHA256).Hash -ne $before[$f]) { $changed += "MODIFIED $f" }
}
Check "shipped settings untouched by a run" ($changed.Count -eq 0) $(if ($changed.Count) { $changed -join '; ' } else { 'unchanged' })
$newFiles = @(Get-ChildItem (Join-Path $app 'settings') -Recurse -File -ErrorAction SilentlyContinue |
              Where-Object { -not $before.ContainsKey($_.FullName) })
Say ("  new files under settings\: " + $(if ($newFiles.Count) { ($newFiles | ForEach-Object { $_.Name }) -join ', ' } else { 'none' }))
$reg = 'HKCU:\SOFTWARE\tmDataQualityAnalyzer'
Say ("  registry key written: " + $(if (Test-Path $reg) { 'yes - ' + $reg } else { 'no' }))

# --- 5. File association ------------------------------------------------------
Say ""
Say "[5] File association (.ch10 task is on by default)"
$assoc = Get-ItemProperty 'HKLM:\SOFTWARE\Classes\.ch10' -ErrorAction SilentlyContinue
Check ".ch10 registered" ($null -ne $assoc) $(if ($assoc) { $assoc.'(default)' } else { 'absent' })

# --- 6. Uninstall -------------------------------------------------------------
Say ""
Say "[6] Uninstall"
$unins = Get-ChildItem $app -Filter 'unins*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($unins) {
    Check "uninstaller signed" ((Get-AuthenticodeSignature $unins.FullName).Status -eq 'Valid') (Get-AuthenticodeSignature $unins.FullName).Status
    $u = Start-Process $unins.FullName -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -PassThru
    $null = $u.Handle
    $u.WaitForExit(300000) | Out-Null
    Start-Sleep -Seconds 5
    Check "program files removed" (-not (Test-Path (Join-Path $app 'bin'))) ''
    Check "settings deliberately kept" (Test-Path (Join-Path $app 'settings')) 'uninsneveruninstall, US8.0'
    # The .iss uses uninsdeletevalue on .ch10 and uninsdeletekey on the ProgID, which
    # is the safe pair: the empty .ch10 key is left behind deliberately, because
    # deleting it outright could clobber an association another application added
    # later. So the VALUE is what must be gone, not the key.
    $left = (Get-ItemProperty 'HKLM:\SOFTWARE\Classes\.ch10' -ErrorAction SilentlyContinue).'(default)'
    Check ".ch10 value cleared" ([string]::IsNullOrEmpty($left)) $(if ($left) { "still '$left'" } else { 'empty key left behind by design' })
    Check "ProgID key removed" (-not (Test-Path 'HKLM:\SOFTWARE\Classes	mDataQualityAnalyzer.ch10')) 'uninsdeletekey'
} else {
    Check "uninstaller found" $false 'no unins*.exe in the install dir'
}

Say ""
Say ("RESULT: " + $(if ($fails -eq 0) { "ALL CHECKS PASSED" } else { "$fails CHECK(S) FAILED" }))
$lines | Out-File -FilePath $LOG -Encoding ASCII
Write-Host ""
Write-Host "Results written to $LOG - this window stays open."
Write-Host "Press Enter to close."
[void](Read-Host)
