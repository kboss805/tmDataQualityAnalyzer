<#
.SYNOPSIS
    Runs PSScriptAnalyzer over the build, test and release scripts (eng\ and deploy\) and fails on any finding.

.DESCRIPTION
    The rules and what is set aside are in eng\PSScriptAnalyzerSettings.psd1. It would have caught an approved-verb slip
    and an empty catch that hid a failed restore, and it catches a variable assigned and never read. A script that does
    not parse is also a finding. PSScriptAnalyzer is pinned to one version ($AnalyzerVersion below), so a new release with
    new rules cannot turn CI red on a commit that changed nothing: that version is imported when it is installed (Windows
    PowerShell 5.1 and 7 each look in their own module folders) and installed from the PowerShell Gallery for the current
    user when it is not. To move to a newer one, change $AnalyzerVersion and clear its findings in the same change.

    Usage:
        powershell -ExecutionPolicy Bypass -File eng\check_scripts.ps1
#>
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$AnalyzerVersion = '1.25.0'
if (-not (Get-Module -ListAvailable -Name PSScriptAnalyzer | Where-Object { $_.Version -eq [version]$AnalyzerVersion })) {
    Write-Host "Installing PSScriptAnalyzer $AnalyzerVersion for the current user ..."
    if (-not (Get-PackageProvider -Name NuGet -ListAvailable -ErrorAction SilentlyContinue)) {
        Install-PackageProvider -Name NuGet -MinimumVersion 2.8.5.201 -Scope CurrentUser -Force | Out-Null
    }
    Install-Module -Name PSScriptAnalyzer -RequiredVersion $AnalyzerVersion -Scope CurrentUser -Force -Repository PSGallery
}
Import-Module PSScriptAnalyzer -RequiredVersion $AnalyzerVersion

$settings = Join-Path $PSScriptRoot 'PSScriptAnalyzerSettings.psd1'
$findings = @()
foreach ($folder in 'eng', 'deploy') {
    $findings += @(Invoke-ScriptAnalyzer -Path (Join-Path $root $folder) -Recurse -Settings $settings)
}
if ($findings.Count -gt 0) {
    foreach ($f in $findings) {
        $relative = $f.ScriptPath.Substring($root.Length + 1)
        Write-Host ("{0}:{1}  {2}  {3}" -f $relative, $f.Line, $f.RuleName, $f.Message)
    }
    throw "$($findings.Count) PSScriptAnalyzer finding(s)"
}
Write-Host 'PSScriptAnalyzer: no findings in eng\ and deploy\.'
