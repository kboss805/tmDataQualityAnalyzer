# =============================================================================
# run_sandbox_test.ps1 - install the shipped setup.exe on a genuinely clean
# machine and report what it did.
#
#   powershell -ExecutionPolicy Bypass -File deploy\sandbox\run_sandbox_test.ps1 [-Setup <setup.exe>] [-Payload <list>]
#
# With no arguments it tests the newest setup.exe in deploy\, with the payload the verifier needs: the portable
# ZIP beside that setup, deploy\staging\unsigned, the committed PRN sample, and v2.x's setup.exe (the one the
# upgrade starts from; deploy\ of this checkout, or of the main checkout when this is a worktree).
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
    # Defaults to the newest setup.exe in deploy\. Pass one explicitly to test an
    # older release, or an artifact downloaded from a GitHub release.
    [string]$Setup,
    [int]$TimeoutSeconds = 600,
    # Files or folders the verifier needs, copied beside the setup (a folder keeps its name). Separate them with
    # semicolons; a recording's name may contain a comma. Omitted, it is the default list described above.
    [string[]]$Payload = @()
)

$ErrorActionPreference = 'Stop'

# The script the Sandbox runs, from this folder: it tests the packages and the upgrade from v2.x.
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
    $Setup = (Get-ChildItem (Join-Path $deployDir 'tmDataQualityAnalyzer-v*_setup.exe') -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime | Select-Object -Last 1).FullName
}
if (-not $Setup -or -not (Test-Path $Setup)) {
    throw "No setup.exe found. Run deploy\build_package.ps1 first, or pass -Setup <path>."
}

if (-not $Payload -or $Payload.Count -eq 0) {
    # v2.x's setup.exe is not committed (deploy\ ignores it), so a worktree has none: look in the main checkout too.
    $oldSetup = $null
    $folders = @($deployDir)
    $common = & git -C $repoRoot rev-parse --path-format=absolute --git-common-dir 2>$null
    if ($LASTEXITCODE -eq 0 -and $common) { $folders += (Join-Path (Split-Path -Parent ($common | Select-Object -First 1)) 'deploy') }
    foreach ($folder in $folders) {
        $oldSetup = Get-ChildItem (Join-Path $folder 'tmDataQualityAnalyzer-v2.*_setup.exe') -ErrorAction SilentlyContinue |
                    Sort-Object LastWriteTime | Select-Object -Last 1
        if ($oldSetup) { break }
    }
    if (-not $oldSetup) {
        throw ("No v2.x setup.exe (tmDataQualityAnalyzer-v2.*_setup.exe) in " + ($folders -join ' or ') +
               ". The upgrade test starts from it: take v2.14.2's from its GitHub release, or pass -Payload.")
    }
    $Payload = @(
        ($Setup -replace '_setup\.exe$', '_portable.zip'),
        (Join-Path $deployDir 'staging\unsigned'),
        (Join-Path $repoRoot 'tests\data\test files\prn_testfile, sample.ch10'),
        $oldSetup.FullName
    )
}

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
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
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not (Test-Path $results) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
}

if (-not (Test-Path $results)) {
    Write-Host "No results after $TimeoutSeconds s. The Sandbox window shows what it is doing; its console stays open."
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
