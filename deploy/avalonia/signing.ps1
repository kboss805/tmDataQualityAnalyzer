# =============================================================================
# signing.ps1 - code-signing helpers for the Avalonia package (dot-source it).
#
# The same rules deploy\build_release.ps1 learned for the Qt package, in one place
# for the new one: find the newest SDK signtool, prove the certificate can sign
# BEFORE a long build (a locked SimplySign token still shows the certificate as
# valid), and verify every signature instead of trusting signtool's exit code.
# The Qt script keeps its own copy until the switch-over retires it.
#
# Written for Windows PowerShell 5.1, which is how the release scripts run.
# =============================================================================

function Find-Signtool {
    # Newest Windows SDK x64 build, else whatever vcvars put on PATH. Discovered, not
    # pinned: a pinned SDK version silently demotes to the fallback after an update.
    $sdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
           Sort-Object { [version]($_.Directory.Parent.Name) } -ErrorAction SilentlyContinue |
           Select-Object -Last 1
    if ($sdk) { return $sdk.FullName }
    $onPath = Get-Command signtool -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw 'signtool not found. Install the Windows SDK signing tools, or pass -SignCertSha1 '''' to build unsigned.'
}

function Invoke-Signtool {
    # Runs signtool on one file under a timeout. A locked token makes signtool wait on
    # the PIN prompt forever, so a hang is reported as that, not waited out.
    param([string]$Signtool, [string]$Thumbprint, [string]$Timestamp, [string]$File, [int]$TimeoutSeconds = 120)
    $logDir = Join-Path $env:TEMP ('tmdq-sign-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    try {
        $p = Start-Process $Signtool -ArgumentList @('sign', '/sha1', $Thumbprint, '/tr', $Timestamp, '/td', 'sha256',
                                                     '/fd', 'sha256', "`"$File`"") -NoNewWindow -PassThru `
                 -RedirectStandardOutput (Join-Path $logDir 'out.txt') -RedirectStandardError (Join-Path $logDir 'err.txt')
        # Touch .Handle BEFORE waiting: under Windows PowerShell 5.1 a Start-Process -PassThru object that has not
        # cached its handle reads ExitCode back EMPTY once the process has exited (measured; deploy\build_release.ps1).
        $null = $p.Handle
        if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
            $p.Kill()
            throw "signtool timed out after $TimeoutSeconds s on $File. Most likely the signing token is locked: log in to SimplySign and re-run."
        }
        $p.WaitForExit()
        if ($p.ExitCode -ne 0) {
            $why = (Get-Content (Join-Path $logDir 'out.txt'), (Join-Path $logDir 'err.txt') -ErrorAction SilentlyContinue) -join ' '
            throw "signtool failed (exit $($p.ExitCode)) on $File`: $why"
        }
    } finally {
        Remove-Item $logDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-SigningWorks {
    # Signs a throwaway copy of signtool itself: any PE file does, and nothing built yet is touched.
    param([string]$Signtool, [string]$Thumbprint, [string]$Timestamp)
    $dir = Join-Path $env:TEMP ('tmdq-signprobe-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    try {
        $probe = Join-Path $dir 'probe.exe'
        Copy-Item $Signtool $probe -Force
        # 90 s: a healthy probe measured 36 s, nearly all of it the timestamp server.
        Invoke-Signtool -Signtool $Signtool -Thumbprint $Thumbprint -Timestamp $Timestamp -File $probe -TimeoutSeconds 90
        if ((Get-AuthenticodeSignature $probe).Status -ne 'Valid') { throw 'the signing probe ran but its signature does not verify' }
    } finally {
        Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
