# =============================================================================
# run_sandbox_test.ps1 - install the shipped setup.exe on a genuinely clean
# machine and report what it did.
#
#   powershell -ExecutionPolicy Bypass -File deploy\sandbox\run_sandbox_test.ps1
#
# WHY THIS EXISTS
#
# Every other installer check runs on the developer's machine, which already
# holds this application's Qt runtime, its settings and its registry state - so
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
    [int]$TimeoutSeconds = 600
)

$ErrorActionPreference = 'Stop'

$here      = Split-Path -Parent $MyInvocation.MyCommand.Path
$deployDir = Split-Path -Parent $here
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
if (Get-Process -Name 'WindowsSandbox' -ErrorAction SilentlyContinue) {
    throw "A Windows Sandbox is already running. Close it (do not kill it) and re-run; a force-kill leaves the container in a state where the next launch fails."
}

if (-not $Setup) {
    $Setup = (Get-ChildItem (Join-Path $deployDir 'tmDataQualityAnalyzer-v*_setup.exe') -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime | Select-Object -Last 1).FullName
}
if (-not $Setup -or -not (Test-Path $Setup)) {
    throw "No setup.exe found. Run deploy\build_release.ps1 first, or pass -Setup <path>."
}

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $inDir, $outDir | Out-Null
Copy-Item $Setup $inDir -Force
Copy-Item (Join-Path $here 'verify_install.ps1') $inDir -Force

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
    <Command>powershell.exe -NoExit -ExecutionPolicy Bypass -File C:\tmdqa\verify_install.ps1</Command>
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
