<#
.SYNOPSIS
    Runs here what .github\workflows\ci.yml runs on GitHub, in the same order, and says how long each step took.

.DESCRIPTION
    CI is these scripts, one step each. This runs the same ones on this machine, so a push that would turn the hosted
    run red is found here first, with this machine's cores instead of the runner's four. It does not replace the hosted
    run: the required `ci` check on main is posted by GitHub Actions, and nothing here can post it.

    The steps, in CI's order (the list at the bottom; RepoFilesTests fails when it and ci.yml drift apart):
        1. the release consistency check (version literals)
        2. the PowerShell script analyzer
        3. the native core, Debug and Release (zero warnings, imports on the allow-list)
        4. the native tests (doctest), Release
        5. the managed tests, Release
        6. the application's tests (Avalonia, headless), Release

    It stops at the first step that fails, as CI does; -KeepGoing runs the rest anyway and lists every failure. The
    hosted job also installs .NET 10 and the MSVC environment: here they are whatever is installed, and eng\env.ps1
    (which the build scripts load themselves) pins Visual Studio 2022, as the hosted job does.

    Each step runs in a child Windows PowerShell, as CI's do. Works under Windows PowerShell 5.1 and PowerShell 7.

    Usage:
        powershell -ExecutionPolicy Bypass -File eng\ci_local.ps1 [-KeepGoing] [-SkipDebugCore]
        ... -KeepGoing      run every step even after one fails, then list the failures
        ... -SkipDebugCore  leave out the Debug build of the core (CI builds it only to see that Debug compiles
                            warning-free); the hosted run still does
#>
param(
    [switch]$KeepGoing,
    [switch]$SkipDebugCore
)
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root

$keepGoing = $KeepGoing.IsPresent
$skipDebugCore = $SkipDebugCore.IsPresent
$results = New-Object System.Collections.Generic.List[object]
$failed = $false
$total = [System.Diagnostics.Stopwatch]::StartNew()

# One CI step: the script, with the arguments CI gives it, in a child Windows PowerShell. A step that fails stops the run
# unless -KeepGoing; the steps after it are listed as not run.
function Invoke-CiStep([string]$Name, [string]$Script, [string]$Arguments = '') {
    if ($skipDebugCore -and $Script -like '*build_core_dll*' -and $Arguments -eq '-Configuration Debug') {
        $results.Add([pscustomobject]@{ Name = $Name; Outcome = 'skipped'; Seconds = 0 })
        return
    }
    if ($script:failed -and -not $keepGoing) {
        $results.Add([pscustomobject]@{ Name = $Name; Outcome = 'not run'; Seconds = 0 })
        return
    }
    Write-Host ''
    Write-Host ('=== {0}' -f $Name) -ForegroundColor Cyan
    $argList = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $root $Script))
    if ($Arguments) { $argList += $Arguments.Split(' ') }
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'   # 5.1 turns a child's stderr into a terminating error; the exit code is the verdict
    & powershell @argList
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    $outcome = if ($code -eq 0) { 'passed' } else { 'FAILED' }
    if ($code -ne 0) { $script:failed = $true }
    $results.Add([pscustomobject]@{ Name = $Name; Outcome = $outcome; Seconds = $clock.Elapsed.TotalSeconds })
}

# CI's steps, in CI's order and with CI's arguments (.github\workflows\ci.yml).
Invoke-CiStep 'Release consistency (version literals)' 'eng\check_release_consistency.ps1'
Invoke-CiStep 'PowerShell scripts (PSScriptAnalyzer)' 'eng\check_scripts.ps1'
Invoke-CiStep 'Build native core, Debug' 'eng\build_core_dll.ps1' '-Configuration Debug'
Invoke-CiStep 'Build native core, Release' 'eng\build_core_dll.ps1' '-Configuration Release'
Invoke-CiStep 'Native tests (doctest)' 'eng\test_native.ps1' '-Configuration Release'
Invoke-CiStep 'Managed tests (TmDq.Core against the native DLL)' 'eng\test_dotnet.ps1' '-Configuration Release'
Invoke-CiStep 'App tests (Avalonia, headless)' 'eng\test_app.ps1' '-Configuration Release'

Write-Host ''
Write-Host '============================================'
Write-Host $(if ($failed) { ' CI WOULD FAIL' } else { ' CI would pass' })
Write-Host '============================================'
foreach ($r in $results) {
    $colour = switch ($r.Outcome) { 'passed' { 'Green' } 'FAILED' { 'Red' } default { 'DarkGray' } }
    Write-Host ('  {0,-8} {1,6:N1}s  {2}' -f $r.Outcome, $r.Seconds, $r.Name) -ForegroundColor $colour
}
Write-Host ('  {0,-8} {1,6:N1}s  total' -f '', $total.Elapsed.TotalSeconds)
Write-Host ''
Write-Host 'The hosted run is still the one main requires; this finds the failure first.'
if ($failed) { exit 1 }
