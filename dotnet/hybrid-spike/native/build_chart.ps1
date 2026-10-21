<#
.SYNOPSIS
    Builds tmdq_chart.dll - the Direct2D chart renderer that shares its target with a D3D9Ex
    surface so WPF's D3DImage can display it. Plain cl + link, no Qt.

        powershell -ExecutionPolicy Bypass -File dotnet\hybrid-spike\native\build_chart.ps1
#>
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
. (Join-Path $root 'scripts\env.ps1') *> $null

$out = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $out
try {
    $log = & cl /nologo /std:c++20 /O2 /EHsc /W4 /MD /utf-8 /LD `
        (Join-Path $PSScriptRoot 'tmdq_chart.cpp') `
        /Fe:tmdq_chart.dll /Fo:tmdq_chart.obj `
        /link d2d1.lib d3d11.lib d3d9.lib dxgi.lib user32.lib 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { Write-Host $log; throw 'tmdq_chart build failed' }
    $warn = ([regex]::Matches($log, 'warning C\d+')).Count
    "{0} warnings" -f $warn
    if ($warn -gt 0) { Write-Host $log }
    "{0}  {1:N0} bytes" -f (Join-Path $out 'tmdq_chart.dll'), (Get-Item tmdq_chart.dll).Length
} finally { Pop-Location }
