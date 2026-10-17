# =============================================================================
# run_d2d_bench.ps1 - build and run the Direct2D chart benchmark.
#
# Compiled with cl directly rather than through qmake: the benchmark uses no Qt at all,
# only the Windows SDK (d2d1, d3d11, dxgi), so a .pro file would add a Qt dependency
# the measurement does not want. scripts\env.ps1 is still dot-sourced, because that is
# what puts cl.exe and the SDK on PATH.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File dotnet\chart-spike\run_d2d_bench.ps1
#   ... -SkipBuild
# =============================================================================

param([switch]$SkipBuild)

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$Exe        = Join-Path $ScriptDir 'd2d_chart_bench.exe'

Write-Host ''
Write-Host '=== Direct2D chart benchmark ===' -ForegroundColor Cyan

if (-not $SkipBuild) {
    . (Join-Path $ProjectDir 'scripts\env.ps1')
    $ErrorActionPreference = 'Continue'
    Push-Location $ScriptDir
    # /O2 to match the release build the Qt baselines were measured in - an -Od number
    # would not be comparable. /EHsc for ComPtr. /W4 because this project gates on
    # zero warnings and a spike is not exempt.
    & cl /nologo /std:c++17 /O2 /EHsc /W4 /MD `
        d2d_chart_bench.cpp `
        /Fe:d2d_chart_bench.exe /Fo:d2d_chart_bench.obj `
        /link d2d1.lib d3d11.lib dxgi.lib d3dcompiler.lib 2>&1 | Tee-Object -Variable clOut | Out-Null
    $code = $LASTEXITCODE
    Pop-Location
    $ErrorActionPreference = 'Stop'

    $problems = @($clOut | Select-String -Pattern 'error|warning' -SimpleMatch)
    if ($problems.Count -gt 0) {
        Write-Host '  compiler output:' -ForegroundColor Yellow
        $problems | ForEach-Object { Write-Host ('    ' + $_.Line) }
    }
    if ($code -ne 0) {
        Write-Host '  build FAILED' -ForegroundColor Red
        exit 1
    }
    Write-Host '  build    ok'
}

if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Host "  FAILED - $Exe not produced" -ForegroundColor Red
    exit 1
}

Write-Host ''
& $Exe
$runCode = $LASTEXITCODE
Write-Host ''
if ($runCode -ne 0) {
    Write-Host "  benchmark exited $runCode" -ForegroundColor Red
    exit $runCode
}
exit 0
