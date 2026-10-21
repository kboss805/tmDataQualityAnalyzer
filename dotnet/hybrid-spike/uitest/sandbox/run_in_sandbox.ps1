# =============================================================================
# run_in_sandbox.ps1 - run the two UI test executables on a clean machine, both in
# the interactive session and as SYSTEM in Session 0 (no desktop).
#
# Runs INSIDE Windows Sandbox, launched by run_sandbox_ui_tests.ps1 beside this file.
#
# WHY SESSION 0
# The CI runner is a service (NETWORK SERVICE, Session 0). Whether WPF can run its UI
# tests there is the one thing the host machine could not answer: this account may not
# create a window station of its own. A scheduled task running as SYSTEM runs in
# Session 0 with no interactive desktop, which is the situation a service is in, and
# Sandbox's user is an administrator, so it can register one.
#
# ASCII only, written for Windows PowerShell 5.1 (that is what Sandbox has).
# =============================================================================

$ErrorActionPreference = 'Continue'
$IN   = 'C:\tmdqa'
$OUT  = 'C:\tmdqa-out'
$WORK = 'C:\work'
$LOG  = Join-Path $OUT 'results.txt'
$lines = @()

function Say($text) { $script:lines += $text; Write-Host $text }

New-Item -ItemType Directory -Force -Path $WORK | Out-Null

Say "UI tests on a clean machine"
Say ("time     : " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Say ("windows  : " + (Get-CimInstance Win32_OperatingSystem).Caption + " " + (Get-CimInstance Win32_OperatingSystem).BuildNumber)
Say ("user     : " + [Security.Principal.WindowsIdentity]::GetCurrent().Name)
Say ("session  : " + (Get-Process -Id $PID).SessionId)
Say ("display  : " + ((Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name }) -join ', '))
# Which application-control feature is active decides whether UNSIGNED test binaries may run at all.
# Smart App Control (0 = off, 1 = enforce, 2 = evaluation) and a code-integrity policy (0 = off,
# 1 = audit, 2 = enforce) both end in the same "blocked by Device Guard policy" message.
$sac = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' -ErrorAction SilentlyContinue).VerifiedAndReputablePolicyState
$dg  = Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard -ErrorAction SilentlyContinue
Say ("smart app control : " + $(if ($sac -eq $null) { 'not reported' } else { $sac }) + "  (0 off, 1 on, 2 evaluation)")
Say ("code integrity    : " + $(if ($dg) { "policy enforcement " + $dg.CodeIntegrityPolicyEnforcementStatus + ", user mode " + $dg.UsermodeCodeIntegrityPolicyEnforcementStatus } else { 'not reported' }) + "  (0 off, 1 audit, 2 enforce)")
Say ("dotnet   : " + $(if (Get-Command dotnet -ErrorAction SilentlyContinue) { 'installed' } else { 'NOT installed (the suites are self-contained)' }))
Say ""

# Copy out of the read-only mapped folder so the exes may write beside themselves.
Copy-Item "$IN\avalonia" "$WORK\avalonia" -Recurse -Force
Copy-Item "$IN\wpf"      "$WORK\wpf"      -Recurse -Force

$suites = @(
    @{ Name = 'AvaloniaUiTests'; Exe = "$WORK\avalonia\AvaloniaUiTests.exe" },
    @{ Name = 'WpfUiTests';      Exe = "$WORK\wpf\WpfUiTests.exe" }
)

function Summarise($file) {
    $t = Get-Content $file -ErrorAction SilentlyContinue
    $sess = ($t | Select-String '^SESSION=') | Select-Object -First 1
    $total = ($t | Select-String 'Total:') | Select-Object -First 1
    $fail = @($t | Select-String '\[FAIL\]')
    [pscustomobject]@{ Session = $(if ($sess) { $sess.Line } else { 'SESSION=?' }); Total = $(if ($total) { $total.Line.Trim() } else { 'no summary line' }); Fails = $fail }
}

foreach ($s in $suites) {
    $name = $s.Name
    # The wrapper reports which session it really ran in, so "Session 0" is observed, not assumed.
    $cmd = Join-Path $WORK "run_$name.cmd"
    @"
@echo off
for /f %%i in ('powershell -NoProfile -Command "(Get-Process -Id `$PID).SessionId"') do echo SESSION=%%i
"$($s.Exe)"
echo EXIT=%errorlevel%
exit /b %errorlevel%
"@ | Out-File -FilePath $cmd -Encoding ASCII

    # --- interactive session: the baseline -------------------------------------------------
    Say "[$name] interactive session"
    $o1 = Join-Path $WORK "$name-interactive.txt"
    $p = Start-Process cmd.exe -ArgumentList "/c `"`"$cmd`" > `"$o1`" 2>&1`"" -PassThru -WindowStyle Hidden
    $null = $p.Handle
    if (-not $p.WaitForExit(240000)) { $p.Kill(); Say "  TIMED OUT after 240 s" }
    $r = Summarise $o1
    Say ("  " + $r.Session + "  exit=" + $p.ExitCode + "  " + $r.Total)
    if (Get-Content $o1 -ErrorAction SilentlyContinue | Select-String 'Device Guard policy') {
        Say "  BLOCKED by application control before any test ran: the executable is unsigned. This says nothing about the framework."
    }
    foreach ($f in $r.Fails) { Say ("  " + $f.Line.Trim()) }

    # --- SYSTEM, Session 0: what the CI runner's service is ---------------------------------
    Say "[$name] as SYSTEM (scheduled task, Session 0, no desktop)"
    $o0 = Join-Path $WORK "$name-session0.txt"
    $task = "tmdq-$name"
    $action = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument "/c `"`"$cmd`" > `"$o0`" 2>&1`""
    $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
    Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue
    Register-ScheduledTask -TaskName $task -Action $action -Principal $principal | Out-Null
    Start-ScheduledTask -TaskName $task
    $deadline = (Get-Date).AddSeconds(240)
    Start-Sleep -Seconds 2
    while ((Get-ScheduledTask -TaskName $task).State -eq 'Running' -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
    if ((Get-ScheduledTask -TaskName $task).State -eq 'Running') { Stop-ScheduledTask -TaskName $task; Say "  TIMED OUT after 240 s (stopped)" }
    $info = Get-ScheduledTaskInfo -TaskName $task
    $r = Summarise $o0
    Say ("  " + $r.Session + "  task result=" + $info.LastTaskResult + "  " + $r.Total)
    foreach ($f in $r.Fails) { Say ("  " + $f.Line.Trim()) }
    # Keep the full output of a failing or odd run: the summary alone would hide why.
    Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue
    Say ""
}

Copy-Item "$WORK\*.txt" $OUT -Force -ErrorAction SilentlyContinue
$lines | Out-File -FilePath $LOG -Encoding ASCII
Write-Host ""
Write-Host "Results are in C:\tmdqa-out. Shutting the Sandbox down in 10 s (the host script's -Keep leaves it open)."
if (-not (Test-Path "$IN\keep")) { Start-Sleep -Seconds 10; Stop-Computer -Force }
