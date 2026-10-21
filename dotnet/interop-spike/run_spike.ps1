# =============================================================================
# run_spike.ps1 - build the native DLL and drive it from C#.
#
# WHY THIS EXISTS INSTEAD OF `dotnet run`
#
# This machine has the .NET runtimes (6, 8, 10, including WindowsDesktop) but NO SDK -
# there is no C:\Program Files\dotnet\sdk directory, and only VS BuildTools is
# installed, so there is no .NET workload either. `dotnet build` cannot work here.
#
# PowerShell's Add-Type can still compile and run C#, so the spike uses that. What it
# proves is the part that matters: DllImport marshalling of blittable structs and
# pointer-plus-length arrays is the same mechanism on .NET Framework as on .NET 10.
# What it does NOT exercise is the modern zero-copy path (Span<T> over the returned
# arrays instead of Marshal.Copy) - re-measure that once an SDK is installed, using
# InteropSpike.csproj, which is committed beside this script for exactly that.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File dotnet\interop-spike\run_spike.ps1
#   ... -SkipBuild            reuse release\tmdq_core.dll
#   ... -Channel 15           pick a PCM channel (default: auto - first that locks)
# =============================================================================

param(
    [switch]$SkipBuild,
    [int]$Channel = 0
)

$ErrorActionPreference = 'Stop'
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent (Split-Path -Parent $ScriptDir)

Write-Host ''
Write-Host '=== Interop spike: native core driven from C# ===' -ForegroundColor Cyan

# --- fixture -----------------------------------------------------------------
# The committed 77 MB PRN sample. It carries PCM data for channels 13 and 15 only
# (14 and 16 are in its TMATS with nothing behind them), which is enough for a
# single-stream spike - and is the same fact that made the throughput benchmark
# unable to run until it was pointed at the full recording.
$Fixture = Join-Path $ProjectDir 'tests\data\test files\prn_testfile, sample.ch10'
if (-not (Test-Path -LiteralPath $Fixture)) {
    Write-Host "FAILED - fixture missing: $Fixture" -ForegroundColor Red
    exit 1
}
Write-Host ("  fixture   {0} MB" -f [math]::Round((Get-Item -LiteralPath $Fixture).Length / 1MB, 1))

# --- build -------------------------------------------------------------------
$Dll = Join-Path $ScriptDir 'release\tmdq_core.dll'
if ($SkipBuild) {
    Write-Host '  build     skipped (-SkipBuild)'
} else {
    . (Join-Path $ProjectDir 'scripts\env.ps1')
    $ErrorActionPreference = 'Continue'
    Push-Location $ScriptDir
    & qmake tmdq_core.pro -spec win32-msvc CONFIG+=release
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw 'qmake failed' }
    & $env:TMDQ_MAKE -f Makefile.Release > (Join-Path $ScriptDir 'build.log') 2>&1
    $buildCode = $LASTEXITCODE
    Pop-Location
    $ErrorActionPreference = 'Stop'
    if ($buildCode -ne 0) {
        Write-Host '  build     FAILED - see build.log' -ForegroundColor Red
        exit 1
    }
    Write-Host '  build     ok (0 warnings required)'
}
if (-not (Test-Path -LiteralPath $Dll)) {
    Write-Host "FAILED - DLL not produced: $Dll" -ForegroundColor Red
    exit 1
}
Write-Host ("  dll       {0} KB" -f [math]::Round((Get-Item -LiteralPath $Dll).Length / 1KB, 0))

# --- the DLL needs Qt beside it ----------------------------------------------
# tmdq_core still links Qt Core and Gui (see tmdq_core.pro's FINDING note). Rather than
# putting Qt on PATH - the hard-coded-path rot that caused the 0xC0000135 after the
# 6.11.1 bump - copy the runtime next to the DLL, the same approach
# scripts\deploy_qt_local.ps1 takes for the app.
# scripts\env.ps1 owns $env:QTDIR for the current shell. With -SkipBuild it has not
# been sourced, so fall back to wherever qmake resolves from, and finally to the
# QT_VERSION default - never assume the variable is set.
$QtBin = $null
if ($env:QTDIR -and (Test-Path (Join-Path $env:QTDIR 'bin'))) {
    $QtBin = Join-Path $env:QTDIR 'bin'
}
if (-not $QtBin) {
    $qmakeCmd = Get-Command qmake -ErrorAction SilentlyContinue
    if ($qmakeCmd) { $QtBin = Split-Path -Parent $qmakeCmd.Source }
}
if (-not $QtBin) {
    $ver = if ($env:QT_VERSION) { $env:QT_VERSION } else { '6.12.0' }
    $guess = "C:\Qt\$ver\msvc2022_64\bin"
    if (Test-Path $guess) { $QtBin = $guess }
}
if ($QtBin -and (Test-Path $QtBin)) {
    foreach ($q in @('Qt6Core.dll', 'Qt6Gui.dll')) {
        $src = Join-Path $QtBin $q
        $dst = Join-Path (Split-Path -Parent $Dll) $q
        if ((Test-Path $src) -and -not (Test-Path $dst)) { Copy-Item $src $dst }
    }
    Write-Host '  qt        copied beside the DLL (no PATH dependency)'
} else {
    Write-Host '  qt        WARNING: could not locate the Qt bin dir; load may fail' -ForegroundColor Yellow
}

# --- compile the managed side -------------------------------------------------
Write-Host ''
Write-Host 'Compiling C# with Add-Type (no SDK on this machine)...' -ForegroundColor Cyan
$src = Get-Content (Join-Path $ScriptDir 'TmdqCore.cs') -Raw
try {
    Add-Type -TypeDefinition $src -ErrorAction Stop
} catch {
    Write-Host ('FAILED to compile TmdqCore.cs: ' + $_.Exception.Message) -ForegroundColor Red
    if ($_.Exception.InnerException) { Write-Host ('  ' + $_.Exception.InnerException.Message) -ForegroundColor Red }
    exit 1
}
Write-Host '  compiled  TmdqCore.cs'

# Load the DLL by ABSOLUTE PATH before any DllImport call. Setting the working
# directory is not enough: DllImport resolves by bare name through the OS search order,
# which does not dependably include the cwd, and the spike's first run failed with
# 0x8007007E even with the DLL and Qt sitting right there. LOAD_WITH_ALTERED_SEARCH_PATH
# also makes the DLL's own directory the first place its dependencies are found, which
# is what lets the Qt copy beside it work.
$preloadError = [Tmdq.Interop.TmdqCore]::Preload((Resolve-Path $Dll).Path)
if ($preloadError) {
    Write-Host "FAILED - $preloadError" -ForegroundColor Red
    exit 1
}
Write-Host '  loaded    tmdq_core.dll by absolute path'

# --- 1. ABI version and struct layout ----------------------------------------
Write-Host ''
Write-Host '--- 1. ABI handshake and struct layout ---' -ForegroundColor Cyan
try {
    $ver = [Tmdq.Interop.TmdqCore]::tmdq_abi_version()
} catch {
    Write-Host ('FAILED to call into the DLL: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host '  Usually a missing Qt DLL beside tmdq_core.dll, or an x86/x64 mismatch.' -ForegroundColor Red
    exit 1
}
Write-Host ("  abi version            {0}" -f $ver)
Write-Host ("  LockRequest  native {0,3}  managed {1,3}" -f
    [Tmdq.Interop.TmdqCore]::tmdq_sizeof_lock_request(),
    [Tmdq.Interop.TmdqCore]::ManagedSizeOfLockRequest())
Write-Host ("  LockResult   native {0,3}  managed {1,3}" -f
    [Tmdq.Interop.TmdqCore]::tmdq_sizeof_lock_result(),
    [Tmdq.Interop.TmdqCore]::ManagedSizeOfLockResult())
Write-Host ("  FileInfo     native {0,3}  managed {1,3}" -f
    [Tmdq.Interop.TmdqCore]::tmdq_sizeof_file_info(),
    [Tmdq.Interop.TmdqCore]::ManagedSizeOfCh10FileInfo())

$layout = [Tmdq.Interop.TmdqCore]::CheckLayout()
if ($layout) {
    Write-Host ''
    Write-Host "FAILED - struct layout disagrees: $layout" -ForegroundColor Red
    Write-Host 'This is the hazard the spike exists to catch: wrong offsets read' -ForegroundColor Red
    Write-Host 'plausible garbage rather than failing.' -ForegroundColor Red
    exit 1
}
Write-Host '  layout                 AGREE (this is the check that matters)' -ForegroundColor Green

# --- 2. probe the file --------------------------------------------------------
Write-Host ''
Write-Host '--- 2. Probe the recording (metadata only) ---' -ForegroundColor Cyan
$pathPtr = [Tmdq.Interop.TmdqCore]::Utf8Alloc($Fixture)
try {
    $info = New-Object Tmdq.Interop.Ch10FileInfo
    $rc = [Tmdq.Interop.TmdqCore]::tmdq_probe_file($pathPtr, [ref]$info)
    if ($rc -ne 0) {
        Write-Host ("FAILED - probe returned {0}: {1}" -f $rc, [Tmdq.Interop.TmdqCore]::LastError()) -ForegroundColor Red
        exit 1
    }
    $ids = @($info.PcmChannelIds[0..([math]::Max($info.PcmChannelCount - 1, 0))])
    Write-Host ("  time channel           {0}" -f $info.TimeChannelId)
    Write-Host ("  pcm channels           {0} -> {1}" -f $info.PcmChannelCount, ($ids -join ', '))
} finally {
    [System.Runtime.InteropServices.Marshal]::FreeHGlobal($pathPtr)
}

# --- 3. run a real stream -----------------------------------------------------
Write-Host ''
Write-Host '--- 3. Process one stream through the ABI ---' -ForegroundColor Cyan

# PRN specs, from settings/framesync_patterns/. words = ceil(bits / 16).
#
# The L suffix is load-bearing: PowerShell parses a bare 0xA345CA5C as Int32, which
# overflows to -1555707300, and casting THAT to [uint64] throws. 0x...L parses as Int64
# first, so the value survives. 0x334AABBF happens to fit in a positive Int32 and would
# have worked bare - which is exactly how this would ship broken for one pattern only.
$specs = @(
    @{ Name = 'PRN15'; Sync = [uint64]0x334AABBFL; Bits = 32767; Words = 2048 },
    @{ Name = 'PRN11'; Sync = [uint64]0xA345CA5CL; Bits = 2047;  Words = 128  }
)
$candidates = if ($Channel -gt 0) { @($Channel) } else { $ids }

$done = $false
foreach ($ch in $candidates) {
    foreach ($spec in $specs) {
        $pathPtr = [Tmdq.Interop.TmdqCore]::Utf8Alloc($Fixture)
        try {
            $req = New-Object Tmdq.Interop.LockRequest
            $req.FilenameUtf8      = $pathPtr
            $req.TimeChannelId     = $info.TimeChannelId
            $req.PcmChannelId      = $ch
            $req.FrameSync         = $spec.Sync
            $req.FrameSyncMask     = [uint64]0xFFFFFFFFL   # L suffix, same Int32 overflow trap as the sync patterns above
            $req.SyncPatternLength = 32
            $req.BitsInMinorFrame  = $spec.Bits
            $req.WordsInMinorFrame = $spec.Words
            $req.SamplePeriodSec   = 1.0
            $req.SwapBytes         = 1    # an ordinary Chapter 10 recording needs the swap
            $req.IsRandomized      = 0
            $req.IsInverted        = 0

            $res = New-Object Tmdq.Interop.LockResult
            $sw  = [System.Diagnostics.Stopwatch]::StartNew()
            $rc  = [Tmdq.Interop.TmdqCore]::tmdq_run_lock_stream([ref]$req, [ref]$res)
            $sw.Stop()

            if ($rc -ne 0) {
                Write-Host ("  ch {0,-3} {1}  no lock ({2})" -f $ch, $spec.Name, $rc)
                continue
            }

            $copySw = [System.Diagnostics.Stopwatch]::StartNew()
            $times  = [Tmdq.Interop.TmdqCore]::ReadDoubles($res.TimesSec,      $res.SampleCount)
            $lock   = [Tmdq.Interop.TmdqCore]::ReadDoubles($res.LockPercent,   $res.SampleCount)
            $missed = [Tmdq.Interop.TmdqCore]::ReadDoubles($res.MissedFrames,  $res.SampleCount)
            $copySw.Stop()

            $avg = ($lock | Measure-Object -Average).Average
            $min = ($lock | Measure-Object -Minimum).Minimum
            $max = ($lock | Measure-Object -Maximum).Maximum

            Write-Host ''
            Write-Host ("  ch {0} as {1}: LOCKED" -f $ch, $spec.Name) -ForegroundColor Green
            Write-Host ("    samples              {0}" -f $res.SampleCount)
            Write-Host ("    lock %%               avg {0:F3}  min {1:F3}  max {2:F3}" -f $avg, $min, $max)
            Write-Host ("    missed frames (last) {0}" -f $missed[$missed.Length - 1])
            Write-Host ("    span (s)             {0:F3} .. {1:F3}" -f $times[0], $times[$times.Length - 1])
            Write-Host ''
            Write-Host '    --- boundary cost ---'
            Write-Host ("    native run           {0:F1} ms  (reported by the DLL)" -f $res.ElapsedMs)
            Write-Host ("    total via P/Invoke   {0:F1} ms  (measured in C#)" -f $sw.Elapsed.TotalMilliseconds)
            Write-Host ("    marshalling out      {0:F2} ms  for {1} doubles x3" -f $copySw.Elapsed.TotalMilliseconds, $res.SampleCount)
            $pct = if ($res.ElapsedMs -gt 0) { 100.0 * $copySw.Elapsed.TotalMilliseconds / $res.ElapsedMs } else { 0 }
            Write-Host ("    marshalling share    {0:F3} %% of the native run" -f $pct)

            [Tmdq.Interop.TmdqCore]::tmdq_free_lock_result([ref]$res)

            # --- scaling check -----------------------------------------------
            # 348 doubles says nothing about marshalling at scale, and at that size the
            # measurement above is dominated by first-call JIT rather than copying. Run
            # the same stream at 10 ms - the finest period the UI offers, and what the
            # realistic worst case uses - for a second point two orders of magnitude up.
            Write-Host ''
            Write-Host '    --- scaling: same stream at 10 ms instead of 1 s ---'
            $req.SamplePeriodSec = 0.01
            $res2 = New-Object Tmdq.Interop.LockResult
            $sw2  = [System.Diagnostics.Stopwatch]::StartNew()
            $rc2  = [Tmdq.Interop.TmdqCore]::tmdq_run_lock_stream([ref]$req, [ref]$res2)
            $sw2.Stop()
            if ($rc2 -eq 0) {
                $copySw2 = [System.Diagnostics.Stopwatch]::StartNew()
                $t2 = [Tmdq.Interop.TmdqCore]::ReadDoubles($res2.TimesSec,     $res2.SampleCount)
                $l2 = [Tmdq.Interop.TmdqCore]::ReadDoubles($res2.LockPercent,  $res2.SampleCount)
                $m2 = [Tmdq.Interop.TmdqCore]::ReadDoubles($res2.MissedFrames, $res2.SampleCount)
                $copySw2.Stop()
                $doubles = $res2.SampleCount * 3
                $bytes   = $doubles * 8
                Write-Host ("    samples              {0}  ({1} doubles, {2:N0} KB)" -f
                    $res2.SampleCount, $doubles, ($bytes / 1KB))
                Write-Host ("    native run           {0:F1} ms" -f $res2.ElapsedMs)
                Write-Host ("    marshalling out      {0:F2} ms" -f $copySw2.Elapsed.TotalMilliseconds)
                $pct2 = if ($res2.ElapsedMs -gt 0) { 100.0 * $copySw2.Elapsed.TotalMilliseconds / $res2.ElapsedMs } else { 0 }
                Write-Host ("    marshalling share    {0:F3} %% of the native run" -f $pct2)
                $mbps = if ($copySw2.Elapsed.TotalSeconds -gt 0) { ($bytes / 1MB) / $copySw2.Elapsed.TotalSeconds } else { 0 }
                Write-Host ("    copy throughput      {0:N0} MB/s" -f $mbps)
                $avg2 = ($l2 | Measure-Object -Average).Average
                Write-Host ("    lock %% avg           {0:F3}   (1 s run said {1:F3})" -f $avg2, $avg)
                Write-Host ("    missed frames (last) {0}   (1 s run said {1})" -f $m2[$m2.Length-1], $missed[$missed.Length-1])
                [Tmdq.Interop.TmdqCore]::tmdq_free_lock_result([ref]$res2)
            } else {
                Write-Host ("    FAILED rc={0}: {1}" -f $rc2, [Tmdq.Interop.TmdqCore]::LastError()) -ForegroundColor Red
            }

            $done = $true
            break
        } finally {
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($pathPtr)
        }
    }
    if ($done) { break }
}

Write-Host ''
if (-not $done) {
    Write-Host 'FAILED - no channel locked with either PRN spec.' -ForegroundColor Red
    Write-Host ('  last native error: ' + [Tmdq.Interop.TmdqCore]::LastError()) -ForegroundColor Red
    exit 1
}
Write-Host 'Spike passed: layout agrees, the pipeline ran through a flat C ABI, and the' -ForegroundColor Green
Write-Host 'results came back into managed code intact.' -ForegroundColor Green
exit 0
