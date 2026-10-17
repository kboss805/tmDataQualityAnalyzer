# Interop spike: driving the native core from managed code

The first of the two spikes that decide the Microsoft-only move (see
`docs/dotnet-migration/README.md`). It answers one question: **can the decommutation
pipeline be driven from managed code through a flat C ABI, correctly and cheaply?**

**Result: yes, on all three counts it set out to test.** Reproduced from a clean build.

```powershell
powershell -ExecutionPolicy Bypass -File dotnet\interop-spike\run_spike.ps1
#   -SkipBuild        reuse release\tmdq_core.dll
#   -Channel 15       force a PCM channel instead of trying each
```

## What it measured

| | |
| --- | --- |
| ABI handshake | version 1 |
| Struct layout, native vs managed | `LockRequest` 72/72, `LockResult` 40/40, `FileInfo` 136/136 - **agree** |
| File probe (metadata only) | time channel 1, PCM channels 13, 14, 15, 16 |
| Stream run, ch 13 as PRN15 | **locked**; 116 samples at 1 s, lock avg 93.532% (min 0, max 100), 1100 missed frames, span 115 s |
| Boundary overhead | native 3831.6 ms, total through P/Invoke 3846.0 ms - **14 ms, 0.4%** |

The marshalling number needed a second look. At 116 samples it read 3.86 ms, which
looked expensive for 348 doubles - because it is dominated by first-call JIT, not by
copying. Re-run at a 10 ms sample period:

| | 1 s period | 10 ms period |
| --- | --- | --- |
| samples | 116 (348 doubles) | 11,554 (34,662 doubles, 271 KB) |
| marshalling out | 3.86 ms | **0.19 ms** |
| share of native run | 0.101% | **0.005%** |

**100x the data copied 20x faster**, which settles it: the first figure was warmup. At
the measured 1,362 MB/s, the realistic worst case from the assessment - 12 channels,
one hour at 10 ms, ~34 MB - extrapolates to roughly **25 ms against a multi-minute
run**. The boundary is not a cost worth designing around.

## What it does NOT prove

- **That Qt is gone.** `tmdq_core.dll` still links Qt Core. De-Qt'ing the core is
  separate work that belongs on `main`, where it keeps the Qt application building at
  every step. The ABI is Qt-free; what sits behind it is not yet.
- ~~**The modern managed path.**~~ **Now covered.** SDK 10.0.402 was installed per-user
  (see below) and `InteropSpike.csproj` runs on real .NET 10:

  ```text
  abi version 1, struct layout agrees
  time channel 1, 4 pcm channels
  11554 samples, native 2312.5 ms, total 2313.7 ms, copy 0.22 ms
  span 1777584474.153..1777584589.682s, last missed 838
  span-based mean 92.643 in 2.490 ms (zero copy)
  ```

  **The two runtimes agree exactly** - 11,554 samples, 838 missed frames and a 92.643
  mean from both .NET 10 and the Add-Type path. That matters more than either number
  alone: it rules out the layout agreement being a .NET Framework artifact.

  **The zero-copy measurement is confounded and should not be read as a result.** The
  span iteration shows 2.490 ms against the copy's 0.22 ms, but those are not the same
  operation: `Marshal.Copy` is a bulk memcpy intrinsic, while the span path is 11,554
  individual additions plus first-call JIT. It does not show that zero-copy is slower.
  The honest conclusion is that **the question is already settled by the copy being
  0.005% of the run** - there is nothing left for zero-copy to win, so the real shell
  can use whichever reads better.
- **Value-level agreement with the Qt application.** The numbers are self-consistent
  and plausible - 116 samples over 115 s at a 1 s period is exactly right, lock stays
  in 0..100, missed frames are monotonic - but nothing here cross-checks them against
  the same stream processed by the Qt app. That is the obvious next check and it is
  cheap: assert these values in the C++ test suite, then compare.

## Findings worth keeping

**1. The model layer transitively needs QtGui, and should not.** Building with
`QT -= gui` fails:

```text
constants.h(11): fatal error C1083: Cannot open include file: 'QColor'
```

`include/constants.h` holds `PlotConstants`' `QColor` arrays in the same header as
`PCMConstants` and `AppVersion`, so every model translation unit pulls in QtGui.
Splitting that header into processing constants and presentation constants would let a
native core drop QtGui entirely - a small, mechanical change, and a real item for the
de-Qt work on `main`. Interestingly the DLL has **no runtime Qt6Gui dependency**: the
colour arrays are `inline constexpr` and nothing in these translation units references
them, so no code is emitted. The cost is compile-time coupling only.

**2. `DllImport` by bare name does not dependably find the DLL.** The first run failed
with `0x8007007E` ("module could not be found") with the DLL and Qt sitting right
beside it. The working directory is not reliably searched. `TmdqCore.Preload()` loads
it by absolute path with `LOAD_WITH_ALTERED_SEARCH_PATH` first, which also makes the
DLL's own directory the first place its dependencies are found - which is what lets Qt
live next to it rather than on `PATH`. That is the same lesson as
`scripts/deploy_qt_local.ps1`: a hard-coded `PATH` dependency is what produced the
`0xC0000135` after the Qt 6.11.1 bump. **Any real shell should load the core
explicitly, not by name.**

My first diagnosis of this was wrong - `dumpbin` showed `Qt6Core.dll` importing
`icuuc.dll`, which is absent from the Qt bin dir, so I assumed a missing dependency.
Loading both DLLs by absolute path succeeded immediately, proving nothing was missing
and the problem was resolution all along.

**3. Two naming collisions the SDK caught that `Add-Type` had not.** Building the real
project surfaced both:

- `NETSDK1022` - the `.csproj` listed `<Compile Include>` items the SDK already globs by
  default. Removed; the SDK picks up `*.cs` on its own.
- `CS0104` - the managed mirror of `tmdq_file_info` was called `FileInfo`, which is
  ambiguous against `System.IO.FileInfo` as soon as a file says `using System.IO`.
  Renamed `Ch10FileInfo`. The native side needed no change, because the `tmdq_` prefix
  already keeps C names distinct - a reminder that the C and managed sides have
  different collision rules and the managed one is stricter.

Also: the per-user SDK brings its **own** runtime under
`%LOCALAPPDATA%\Microsoft\dotnet\shared`, and `dotnet run` resolves against that root
rather than `C:\Program Files\dotnet`. A `net8.0` target therefore failed to launch
even though runtime 8.0.22 is installed system-wide. Retargeted to `net10.0`, which is
LTS and is what the SDK ships.

**4. PowerShell hex literals above 0x7FFFFFFF need an `L`.** `[uint64]0xA345CA5C`
throws: the literal parses as `Int32`, overflows to -1555707300, and the cast fails.
`0xA345CA5CL` parses as `Int64` first and survives. Worth recording because
`0x334AABBF` (PRN15) fits in a positive `Int32` and works bare - so this would have
shipped broken for exactly one of the two patterns.

**5. Accumulated missed frames depends on the sample period.** The same stream reports
**1100** missed frames at a 1 s period and **838** at 10 ms. Lock % also differs
(93.532 vs 92.643), which is expected - it is an unweighted mean over a different
number of windows. The missed-frames difference is less obviously right: it is a
cumulative count of loss-of-lock events, and the gap-extrapolation rule fires only
when a window has zero bits, so window size changes how often it fires.

**This is pre-existing Qt-side behaviour, not an interop finding** - the ABI passes
`samplePeriodSec` through and changes nothing else. Flagging it rather than calling it
a bug, because the intended semantics are a domain question: an operator switching
between 1 s and 10 ms sees this metric change, and whether that is correct is worth
deciding deliberately. Worth its own look, separate from this spike.

## Files

| | |
| --- | --- |
| `tmdq_abi.h` | the contract - flat C, no Qt, explicit `#pragma pack(8)`, `tmdq_sizeof_*` so the managed side can assert layout rather than assume it |
| `tmdq_abi.cpp` | implementation. `std::thread` rather than QtConcurrent, so the DLL needs neither the Qt thread pool nor a `QCoreApplication` in the host. Nothing throws across the boundary |
| `tmdq_core.pro` | qmake DLL. `QT += core gui` only; no viewmodel, no view, no CSV/template schemas |
| `TmdqCore.cs` | P/Invoke bindings and the layout assertion. Deliberately conservative C# so it compiles under both the modern SDK and `Add-Type` |
| `run_spike.ps1` | builds and runs everything; this is what produced the numbers above |
| `Program.cs`, `InteropSpike.csproj` | the same spike on real .NET 10, including the zero-copy span path |

Build output (`release/`, `Makefile.*`, `.qmake.stash`, `build.log`) is already covered
by the repository's `.gitignore`. The .NET side adds `bin/` and `obj/`.

## The SDK on this machine

Installed **per-user**, not machine-wide: `winget install Microsoft.DotNet.SDK.10
--scope user` reports "No applicable installer found" because that package is
machine-scope only, which needs elevation. Microsoft's `dotnet-install.ps1` was used
instead, giving **SDK 10.0.402** in `%LOCALAPPDATA%\Microsoft\dotnet`.

It is deliberately **not on `PATH`** (`-NoPath`), so a plain `dotnet` still resolves to
the runtime-only `C:\Program Files\dotnet` and reports no SDKs. Prepending it would
shadow the system install for every tool on the machine. Either invoke it by full path,
as the commands above do, or install machine-wide with an elevated
`winget install Microsoft.DotNet.SDK.10` and delete the per-user copy.
