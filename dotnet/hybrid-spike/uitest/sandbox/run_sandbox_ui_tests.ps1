# =============================================================================
# run_sandbox_ui_tests.ps1 - run the Avalonia and WPF UI tests on a clean machine, in
# the interactive session and as SYSTEM in Session 0.
#
#   powershell -ExecutionPolicy Bypass -File dotnet\hybrid-spike\uitest\sandbox\run_sandbox_ui_tests.ps1
#
# Publishes both test projects self-contained (a clean Sandbox has no .NET), generates
# a .wsb with absolute host paths, starts Windows Sandbox, and waits for results.txt.
# Same conventions as deploy\sandbox\run_sandbox_test.ps1: the .wsb is generated, and
# a running Sandbox is never killed - close its window and re-run.
# =============================================================================

param([int]$TimeoutSeconds = 900, [switch]$Keep)

$ErrorActionPreference = 'Stop'

$here  = Split-Path -Parent $MyInvocation.MyCommand.Path
$uitest = Split-Path -Parent $here
$work  = Join-Path $here '.work'
$inDir = Join-Path $work 'in'
$outDir = Join-Path $work 'out'

if (-not (Test-Path "$env:SystemRoot\System32\WindowsSandbox.exe")) {
    throw "Windows Sandbox is not available (it needs Windows Pro/Enterprise and the 'Windows Sandbox' feature)."
}
$live = @(Get-Process -Name 'WindowsSandbox*' -ErrorAction SilentlyContinue)
if ($live.Count -gt 0) {
    throw ("A Windows Sandbox session is still active (" + (($live | ForEach-Object { $_.ProcessName }) -join ', ') +
           "). Close the Sandbox window and wait a few seconds, then re-run. Do not force-kill it: " +
           "that leaves the container half torn down and the next launch fails silently.")
}

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $inDir, $outDir | Out-Null

foreach ($p in @(@('avalonia', 'AvaloniaUiTests.csproj'), @('wpf', 'WpfUiTests.csproj'))) {
    Write-Host ("Publishing " + $p[0] + " self-contained...")
    dotnet publish (Join-Path $uitest "$($p[0])\$($p[1])") -c Release -r win-x64 --self-contained true -o (Join-Path $inDir $p[0]) -nologo -v q
    if ($LASTEXITCODE -ne 0) { throw "publish failed for $($p[0])" }
}
Copy-Item (Join-Path $here 'run_in_sandbox.ps1') $inDir -Force
if ($Keep) { New-Item -ItemType File -Path (Join-Path $inDir 'keep') | Out-Null }   # leave the Sandbox open afterwards

$wsb = Join-Path $work 'tmdq-ui-tests.wsb'
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
    <Command>powershell.exe -NoExit -ExecutionPolicy Bypass -File C:\tmdqa\run_in_sandbox.ps1</Command>
  </LogonCommand>
  <Networking>Disable</Networking>
  <vGPU>Disable</vGPU>
  <MemoryInMB>4096</MemoryInMB>
</Configuration>
"@ | Out-File -FilePath $wsb -Encoding ASCII

Write-Host "Starting Windows Sandbox. It runs both suites twice and writes results back here."
Start-Process $wsb

$results = Join-Path $outDir 'results.txt'
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not (Test-Path $results) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 5 }
if (-not (Test-Path $results)) {
    Write-Host "No results after $TimeoutSeconds s. The Sandbox window shows what it is doing."
    exit 2
}
Get-Content $results | ForEach-Object { Write-Host $_ }
Write-Host ""
Write-Host "Per-run output is in $outDir."
if ($Keep) { Write-Host "The Sandbox was left open (-Keep). Close its window when done; do not kill it." }
else { Write-Host "The Sandbox shuts itself down. Wait for its processes to exit before re-running." }
