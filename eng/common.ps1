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

# The Visual Studio versions the native build may use, as a vswhere range: TMDQ_VS_RANGE when it is set, else Visual Studio
# 2022 (17.x, MSVC v143), what releases and CI build with. eng\env.ps1 asks vswhere for the same range.
function Get-PinnedVsRange {
    if ($env:TMDQ_VS_RANGE) { return $env:TMDQ_VS_RANGE }
    return '[17.0,18.0)'
}

# Whether a Visual Studio version ("17.0", as VisualStudioVersion has it) is inside a vswhere range: "[17.0,18.0)" (a bracket
# includes its bound, a parenthesis excludes it, an empty bound is open) or a bare version, which is a minimum. Throws on a
# range it cannot read, so a mistyped TMDQ_VS_RANGE fails rather than passing everything.
function ConvertTo-VsVersion([string]$Text) {
    $t = $Text.Trim()
    if ($t -notmatch '\.') { $t += '.0' }   # [version] needs a minor number: "18" is 18.0
    return [version]$t
}
function Test-VsVersionInRange([string]$Version, [string]$Range) {
    $v = ConvertTo-VsVersion $Version
    $r = $Range.Trim()
    if ($r -match '^\d+(\.\d+)*$') { return $v -ge (ConvertTo-VsVersion $r) }
    $m = [regex]::Match($r, '^([\[\(])\s*(\d+(?:\.\d+)*)?\s*,\s*(\d+(?:\.\d+)*)?\s*([\]\)])$')
    if (-not $m.Success) { throw "cannot read the Visual Studio version range '$Range' (expected one like [17.0,18.0))" }
    if ($m.Groups[2].Success) {
        $low = ConvertTo-VsVersion $m.Groups[2].Value
        if ($m.Groups[1].Value -eq '[') { if ($v -lt $low) { return $false } } elseif ($v -le $low) { return $false }
    }
    if ($m.Groups[3].Success) {
        $high = ConvertTo-VsVersion $m.Groups[3].Value
        if ($m.Groups[4].Value -eq ']') { if ($v -gt $high) { return $false } } elseif ($v -ge $high) { return $false }
    }
    return $true
}

# Throws unless this shell's Visual Studio environment (VisualStudioVersion, which vcvars and every developer prompt set) is
# inside the pinned range. Both native projects build with $(DefaultPlatformToolset), the toolset of whichever Visual Studio
# runs msbuild, so a Visual Studio 2026 prompt would otherwise build with MSVC 14.5x and leave two compilers' objects in one
# tree, without a word.
function Assert-PinnedVisualStudio {
    $range = Get-PinnedVsRange
    if (-not $env:VisualStudioVersion) {
        throw "VisualStudioVersion is not set, so which Visual Studio this shell builds with is unknown (the pin is $range). Use a plain shell (the scripts load Visual Studio 2022 through eng\env.ps1) or a Visual Studio 2022 developer prompt."
    }
    if (-not (Test-VsVersionInRange $env:VisualStudioVersion $range)) {
        throw "this shell's Visual Studio is $env:VisualStudioVersion (VisualStudioVersion), outside the pinned range $range. Use a plain shell (the scripts load the pinned Visual Studio through eng\env.ps1) or a developer prompt of a Visual Studio in that range, or set TMDQ_VS_RANGE to choose another range on purpose (then rebuild with -Rebuild)."
    }
}

# Puts msbuild and dumpbin on PATH: eng\env.ps1 (Visual Studio 2022's tools) is dot-sourced when msbuild is not already
# there or this shell names no Visual Studio, as in a plain shell rather than a developer prompt. Throws when either tool
# is still missing, or when the shell's Visual Studio is outside the pinned range (a Visual Studio 2026 prompt).
function Initialize-MsvcEnvironment([string]$EngDir) {
    if (-not (Get-Command msbuild -ErrorAction SilentlyContinue) -or -not $env:VisualStudioVersion) {
        . (Join-Path $EngDir 'env.ps1') *> $null
    }
    if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) { throw 'msbuild not found (install the Visual Studio 2022 C++ build tools)' }
    if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) { throw 'dumpbin not found (it ships with the MSVC tools; use a developer prompt or eng\env.ps1)' }
    Assert-PinnedVisualStudio
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
