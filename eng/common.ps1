# common.ps1 - what the eng and deploy scripts share, dot-sourced: . (Join-Path $PSScriptRoot 'common.ps1')
#
# Each piece used to be copied into every script that needed it (the msbuild-and-check block in two, the version read in
# two, the target framework in six); a fix to one copy missed the others. Works under Windows PowerShell 5.1 and 7.
# KEEP THIS FILE PURE ASCII (see eng\env.ps1: 5.1 reads a file without a BOM as Windows-1252).

# The .NET target framework of the projects, which names the folder a built test exe or deps file is in.
$TargetFramework = 'net10.0'

# The imports tmdq_core.dll and the native test exe may have: the C runtime, KERNEL32, WS2_32, USER32. An allow-list rather than
# a list of what must be absent, so ANY new dependency is noticed and has to be added here on purpose.
$ImportAllowList = '^(KERNEL32|WS2_32|USER32|VCRUNTIME140D?(_1D?)?|MSVCP140D?|ucrtbased|api-ms-win-crt-[a-z0-9-]+-l\d+-\d+-\d+)\.dll$'

# The built test exe of a .NET test project ('TmDq.Core.Tests' or 'TmDq.App.Tests') in a configuration.
function Get-TestExePath([string]$Root, [string]$Project, [string]$Configuration) {
    return Join-Path $Root "tests\$Project\bin\$Configuration\$TargetFramework\$Project.exe"
}

# Puts msbuild and dumpbin on PATH: eng\env.ps1 (Visual Studio 2022's tools) is dot-sourced when msbuild is not already
# there, as in a plain shell rather than a developer prompt. Throws when either is still missing.
function Initialize-MsvcEnvironment([string]$EngDir) {
    if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) {
        . (Join-Path $EngDir 'env.ps1') *> $null
    }
    if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) { throw 'msbuild not found (install the Visual Studio 2022 C++ build tools)' }
    if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) { throw 'dumpbin not found (it ships with the MSVC tools; use a developer prompt or eng\env.ps1)' }
}

# Builds a native project with MSBuild for x64, teeing the output to a log, and throws unless it built, produced
# $Product, and had no compiler warning (the project's zero-warning rule).
function Invoke-MsbuildChecked([string]$Project, [string]$Configuration, [switch]$Rebuild, [string]$Log, [string]$Product) {
    $target = if ($Rebuild) { '/t:Rebuild' } else { '/t:Build' }
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'   # 5.1 turns native stderr into a terminating error
    & msbuild $Project $target /m /nologo /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64 2>&1 |
        Tee-Object -FilePath $Log | Out-Host
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($code -ne 0) { throw "msbuild failed ($code); see $Log" }
    if (-not (Test-Path $Product)) { throw "msbuild reported success but $Product was not produced" }

    $warnings = @(Select-String -Path $Log -Pattern 'warning [A-Z]+\d+')
    if ($warnings.Count -gt 0) {
        $warnings | Select-Object -First 10 | ForEach-Object { Write-Host "  $($_.Line)" }
        throw "$($warnings.Count) compiler warning(s); the build must be warning-clean"
    }
    Write-Host '  0 warnings'
}

# The DLLs a built PE file imports (dumpbin /dependents), and a throw if any is outside $ImportAllowList. Returns the list.
function Assert-ImportsAllowed([string]$File) {
    $deps = @(& dumpbin /nologo /dependents $File 2>&1 | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() })
    if ($deps.Count -eq 0) { throw "dumpbin listed no imports; cannot verify $File" }
    $unexpected = @($deps | Where-Object { $_ -notmatch $ImportAllowList })
    Write-Host '  imports:'
    $deps | ForEach-Object { Write-Host "    $_" }
    if ($unexpected.Count -gt 0) {
        throw "$(Split-Path $File -Leaf) imports something outside the allow-list: $($unexpected -join ', '). If it is deliberate, add it to `$ImportAllowList in eng\common.ps1."
    }
    return $deps
}

# The version, "Major.Minor.Patch", from the text of src\core\include\constants.h: AppVersion is its one source (eng\AppVersion.props
# reads the same three numbers). Throws when one is missing.
function Get-AppVersionFromHeader([string]$ConstantsText) {
    $parts = foreach ($k in 'Major', 'Minor', 'Patch') {
        $m = [regex]::Match($ConstantsText, "k$k\s*=\s*(\d+)")
        if (-not $m.Success) { throw "AppVersion::k$k not found in src\core\include\constants.h" }
        $m.Groups[1].Value
    }
    return $parts -join '.'
}
