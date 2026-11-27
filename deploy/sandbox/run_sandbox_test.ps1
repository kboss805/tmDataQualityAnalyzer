# =============================================================================
# run_sandbox_test.ps1 - install the shipped setup.msi on a genuinely clean
# machine and report what it did.
#
#   powershell -ExecutionPolicy Bypass -File deploy\sandbox\run_sandbox_test.ps1 [-Setup <setup.msi>] [-Payload <list>]
#
# With no arguments it tests the newest setup.msi in deploy\, with the payload the verifier needs: the portable
# ZIP beside that setup, deploy\staging\unsigned, the committed PRN sample, v2.x's setup.exe (the Inno install the MSI
# must refuse to sit beside; deploy\ of this checkout, else -OldSetupDir, the release folder in Drive), and TWO NEWER
# MSIs of the same product, which this script builds (unsigned, the next two patch versions, from the same staged files)
# for the upgrade tests.
#
# WHY THIS EXISTS
#
# Every other installer check runs on the developer's machine, which already
# holds this application's runtime, its settings and its registry state - so
# it can prove an artifact is correctly signed and versioned, and nothing about
# whether it WORKS somewhere new. v2.13.1 said so outright: the first-run path
# "needs a genuinely clean Windows target; Windows 11 Home offers neither Sandbox
# nor Hyper-V."
#
# Windows Sandbox is that target, and it is disposable - every run starts from a
# machine that has never seen this application.
#
# The .wsb file is GENERATED rather than committed: Sandbox requires absolute
# host paths and does not expand environment variables in them, so a checked-in
# one would carry one developer's directory layout.
# =============================================================================

param(
    # Defaults to the newest setup.msi in deploy\. Pass one explicitly to test an
    # older release, or an artifact downloaded from a GitHub release.
    [string]$Setup,
    # Every install of the 50 MB MSI took about 2 minutes in the Sandbox (not on the development machine), and the test
    # installs it five times: a whole run takes 15 to 25 minutes, so the default is generous. A test that has STOPPED is
    # noticed sooner, by its progress file (below).
    [int]$TimeoutSeconds = 2400,
    # Files or folders the verifier needs, copied beside the setup (a folder keeps its name). Separate them with
    # semicolons; a recording's name may contain a comma. Omitted, it is the default list described above.
    [string[]]$Payload = @(),
    # Where v2.x's setup.exe is looked for after deploy\ of this checkout (it is the GitHub release's asset, kept in Drive).
    [string]$OldSetupDir = 'G:\My Drive\Software Releases'
)

$ErrorActionPreference = 'Stop'

# The script the Sandbox runs, from this folder: it tests the packages.
$Verifier  = 'verify_install.ps1'

$here      = Split-Path -Parent $MyInvocation.MyCommand.Path
$deployDir = Split-Path -Parent $here
$repoRoot  = Split-Path -Parent $deployDir
$work      = Join-Path $here '.work'
$inDir     = Join-Path $work 'in'
$outDir    = Join-Path $work 'out'

if (-not (Test-Path "$env:SystemRoot\System32\WindowsSandbox.exe")) {
    throw ("Windows Sandbox is not available. It needs Windows Pro/Enterprise and the " +
           "'Windows Sandbox' optional feature. (Windows Home has neither Sandbox nor Hyper-V.)")
}

# A running Sandbox blocks a new one, and FORCE-KILLING it leaves the container
# half torn down so the next launch fails silently - found the hard way. Ask
# instead of killing.
# Match the WHOLE family, not just WindowsSandbox.exe. The window process exits
# before WindowsSandboxServer and WindowsSandboxRemoteSession do, so an exact-name
# check reports "clear" while a session is still alive - and the launch that follows
# collides with it and produces no results at all. Measured: three processes alive
# (Server + two RemoteSession) with no WindowsSandbox.exe among them.
$live = @(Get-Process -Name 'WindowsSandbox*' -ErrorAction SilentlyContinue)
if ($live.Count -gt 0) {
    throw ("A Windows Sandbox session is still active (" +
           (($live | ForEach-Object { $_.ProcessName }) -join ', ') +
           "). Close the Sandbox window and wait a few seconds for it to tear down, then re-run. " +
           "Do not force-kill it: that leaves the container half torn down and the next launch fails silently.")
}

if (-not $Setup) {
    $Setup = (Get-ChildItem (Join-Path $deployDir 'tmDataQualityAnalyzer-v*_setup.msi') -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime | Select-Object -Last 1).FullName
}
if (-not $Setup -or -not (Test-Path $Setup)) {
    throw "No setup.msi found. Run deploy\build_package.ps1 first, or pass -Setup <path>."
}

if (-not $Payload -or $Payload.Count -eq 0) {
    # The upgrade tests need TWO NEWER MSIs of the same product (one upgrade keeps the .ch10 association, the next declines
    # it). They are built here, unsigned (only their versions matter), from the staged files the release was made from.
    if ((Split-Path -Leaf $Setup) -notmatch '^tmDataQualityAnalyzer-v(\d+)\.(\d+)\.(\d+)_setup\.msi$') { throw "Cannot read a version from the name of $Setup" }
    $major = $Matches[1]; $minor = $Matches[2]; $patch = [int]$Matches[3]
    $newerMsis = @()
    foreach ($step in 1, 2) {
        $nextVersion = '{0}.{1}.{2}' -f $major, $minor, ($patch + $step)
        $newer = Join-Path $deployDir "staging\upgrade-test\tmDataQualityAnalyzer-v${nextVersion}_setup.msi"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $newer) | Out-Null
        Write-Host "Building a newer MSI for the upgrade tests ($nextVersion, unsigned)..."
        & powershell -ExecutionPolicy Bypass -File (Join-Path $deployDir 'build_msi.ps1') -Version $nextVersion -Stage (Join-Path $deployDir 'staging') -OutFile $newer
        if ($LASTEXITCODE -ne 0) { throw "build_msi.ps1 failed: no $nextVersion MSI for the upgrade tests" }
        $newerMsis += $newer
    }
    # v2.x's setup.exe is not committed (deploy\ ignores it), so a worktree has none: look in deploy\ of this checkout,
    # then in -OldSetupDir (the release folder in Drive, where it lives).
    $oldSetup = $null
    $folders = @($deployDir, $OldSetupDir)
    foreach ($folder in $folders) {
        $oldSetup = Get-ChildItem (Join-Path $folder 'tmDataQualityAnalyzer-v2.*_setup.exe') -ErrorAction SilentlyContinue |
                    Sort-Object LastWriteTime | Select-Object -Last 1
        if ($oldSetup) { break }
    }
    if (-not $oldSetup) {
        throw ("No v2.x setup.exe (tmDataQualityAnalyzer-v2.*_setup.exe) in " + ($folders -join ' or ') +
               ". The test of the MSI's refusal starts from it: take v2.14.2's from its GitHub release, or pass -Payload.")
    }
    $Payload = @(
        ($Setup -replace '_setup\.msi$', '_portable.zip'),
        (Join-Path $deployDir 'staging\unsigned'),
        (Join-Path $repoRoot 'tests\data\test files\prn_testfile, sample.ch10'),
        $oldSetup.FullName
    ) + $newerMsis
}

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
# A results file the delete could not remove (a Sandbox window still holding the folder) would satisfy the wait below at once
# and report the last run's results as this one's: stop instead.
if (Test-Path (Join-Path $outDir 'results.txt')) {
    throw "$outDir still holds the last run's results.txt, which could not be deleted: close any Sandbox window, wait for WindowsSandboxServer to exit, and run this again."
}
New-Item -ItemType Directory -Force -Path $inDir, $outDir | Out-Null
Copy-Item $Setup $inDir -Force
# Through `powershell -File`, an array parameter arrives as ONE string, so the list is split here, on SEMICOLONS:
# the committed recordings' names contain commas ("prn_testfile, sample.ch10").
$Payload = @($Payload | ForEach-Object { $_ -split ';' } | ForEach-Object { $_.Trim().Trim('"').Trim("'") } | Where-Object { $_ })
$verifierPath = Join-Path $here $Verifier
if (-not (Test-Path $verifierPath)) { throw "No verifier $Verifier beside this script." }
Copy-Item $verifierPath $inDir -Force
foreach ($item in $Payload) {
    if (-not (Test-Path $item)) { throw "Payload item not found: $item" }
    Copy-Item $item $inDir -Recurse -Force
}

$wsb = Join-Path $work 'tmdqa-clean-install.wsb'
@"
<Configuration>
  <MappedFolders>
    <MappedFolder>
      <HostFolder>$inDir</HostFolder>
      <SandboxFolder>C:\tmdqa</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
    <MappedFolder>
      <HostFolder>$outDir</HostFolder>
      <SandboxFolder>C:\tmdqa-out</SandboxFolder>
      <ReadOnly>false</ReadOnly>
    </MappedFolder>
  </MappedFolders>
  <LogonCommand>
    <Command>powershell.exe -NoExit -ExecutionPolicy Bypass -File C:\tmdqa\$Verifier</Command>
  </LogonCommand>
  <Networking>Enable</Networking>
  <vGPU>Disable</vGPU>
  <MemoryInMB>4096</MemoryInMB>
</Configuration>
"@ | Out-File -FilePath $wsb -Encoding ASCII

Write-Host ("Testing: " + (Split-Path $Setup -Leaf))
Write-Host "Starting Windows Sandbox. It installs, launches, uninstalls, and writes results back here."
Write-Host ""
Start-Process $wsb

$results = Join-Path $outDir 'results.txt'
$progress = Join-Path $outDir 'progress.txt'
$stallSeconds = 600
$began = Get-Date
$deadline = $began.AddSeconds($TimeoutSeconds)
$stalled = $false
while (-not (Test-Path $results) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    # The verifier appends to progress.txt as it goes, with a heartbeat every 20 s while an install runs, so a file that has
    # stopped changing means the verifier has stopped (a hang, or its process was killed: an uninstall once shut it down
    # through Restart Manager, and this loop waited the whole timeout for results that were never written).
    $last = if (Test-Path $progress) { (Get-Item $progress).LastWriteTime } else { $began }
    if (((Get-Date) - $last).TotalSeconds -gt $stallSeconds) { $stalled = $true; break }
}

if (-not (Test-Path $results)) {
    if ($stalled) {
        Write-Host "No progress for $stallSeconds s: the test has stopped. The last lines it wrote:"
        if (Test-Path $progress) { Get-Content $progress -Tail 12 | ForEach-Object { Write-Host "  $_" } }
        Write-Host "The Sandbox window shows what it is doing; its console stays open."
    } else {
        Write-Host "No results after $TimeoutSeconds s. The Sandbox window shows what it is doing; its console stays open."
    }
    exit 2
}

Get-Content $results | ForEach-Object { Write-Host $_ }

$failed = (Select-String -Path $results -Pattern '^\s*FAIL' -ErrorAction SilentlyContinue).Count
Write-Host ""
Write-Host "Close the Sandbox window when you are done reading it (do not kill it)."
if ($failed -gt 0) {
    Write-Host "$failed check(s) failed. Before treating any of them as a product defect, confirm the"
    Write-Host "check itself is right - the first run of this harness reported three failures and all"
    Write-Host "three were the harness: a Windows system DLL matched by a loose name filter, a registry"
    Write-Host "KEY checked where the VALUE is the contract, and a check for a setting added after the"
    Write-Host "build under test."
    exit 1
}
exit 0
