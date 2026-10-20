# Hybrid-port spike: a Qt-free native core, and a WPF shell over a native chart

Two questions, answered by running code rather than by argument. Both come from
`docs/dotnet-migration/`: the README's plan (native MSVC core behind a flat C ABI, C# shell, native
Direct2D chart) and `presentation-layer.md`'s open item (can WPF host the chart *and* draw over it).

Everything here is additive. Nothing in `src/`, `include/` or the Qt build changes.

## Layout

| path | what |
|---|---|
| `native/build_core.ps1`, `native/tmdq_core.cpp` | The decommutation core as a framework-free DLL. Plain `cl`/`link`, no Qt anywhere. |
| `driver/` | A console host (C#, net10.0) that drives that DLL through the interop spike's unchanged `TmdqCore.cs` and checks it against recorded numbers. |
| `native/build_chart.ps1`, `native/tmdq_chart.cpp` | The Direct2D chart renderer, drawing into a D3D11 texture shared with a D3D9Ex surface. |
| `wpf/` | A WPF window that shows that surface through `D3DImage`, with a WPF legend and crosshair over it, and runs a scripted benchmark. |
| `wpf/results.txt`, `wpf/screenshot.png` | The final run's output, and the window as the desktop composed it. |

## 1. Is the core really Qt-free? Yes, and it is proven at link level

`native/build_core.ps1` compiles `channeldata`, `chapter10reader`, `ch10packetreader`, `framesetup`,
`frameprocessor`, `stepdetector`, `tomldocument`, `textfile` and `calibrationextractor` plus the ABI
with **no Qt include path and no Qt libraries**, then prints `dumpbin /dependents` and throws if any
`Qt*` import appears.

Result: `tmdq_core.dll`, **361,472 bytes**, 0 warnings, imports `MSVCP140`, `KERNEL32`, `VCRUNTIME140`,
`VCRUNTIME140_1` and the `api-ms-win-crt-*` set. **No Qt imports.** The interop spike's DLL still linked
Qt Core; this is the build-level result the de-Qt sweep was working toward.

It needed exactly one source change on the Qt side: `include/constants.h` still included `<QString>`
(for `AppVersion::toString()` and a `qsizetype`). That was its own PR (#163), with a guard test
(`TestCoreQtFree`) so the property cannot silently regress.

### The managed host agrees with the pre-sweep numbers

`driver/` runs the interop spike's *unchanged* P/Invoke layer against the new DLL. The interop spike
recorded these results on the same fixture before the sweep began; every refactor since changed the
code that produces them (QString to std::string, QObject to event sinks, QVector to std::vector, the
reader, the worker, the DTOs), through a different build system and a different threading primitive.

| check | recorded before | now |
|---|---|---|
| ABI version and the three struct layouts | v1 | agree |
| probe: time channel, PCM channels | 1; 13..16 | same |
| 1 s sample period, ch 13 as PRN15 | 116 samples, 93.532 % mean lock, 1,100 missed | identical |
| 10 ms sample period | 11,554 samples, 838 missed, 92.643 % | identical |
| wrong sync pattern | n/a | rc 3 with the reason in the error text |
| missing file | n/a | rc 2 |
| same stream on two threads at once | n/a | equals one run alone (after the fix below) |

**20 of 20 checks pass.**

### The ABI is a translation layer over `PipelineRunner`

`tmdq_core.cpp` used to carry its own single-stream copy of what `ProcessingCoordinator` does: make the
queue, prepare the reader, start a reader thread, run the processor, abort and close on failure, join. The
Qt application's version of that arrangement is now `PipelineRunner` (PR #167, no Qt), so the ABI no longer
carries one. `tmdq_run_lock_stream` builds a one-stream `StreamJob` from the request struct, calls
`runner.start()`, waits, and reads what a small capture sink kept - the stream's result, or the first error
- into C arrays. The file lost its hand-rolled threading, its abort workaround and its includes of the
reader, the processor and the queue.

Same DLL surface, same results: 20 of 20 checks on eight consecutive runs, the recorded numbers unchanged to
the digit, the same failure reasons, the two-threads-at-once check unchanged, and the same time per run
(2.11 s against 2.19 s for the 1 s case; one run is not a measurement, only evidence nothing got slower). The
DLL grew from 361,472 to 376,320 bytes. It still has no Qt imports.

What a multi-stream ABI would add is small: pass several jobs and collect several results. That is not built.

### Two findings the checks turned up

**1. The vendored irig106 library is not safe to open from two threads at once.**
`GetNextHandle()` in `lib/irig106/src/irig106ch10.c` scans a global handle table and then marks a
slot in use, with no lock. Two simultaneous opens can be given the same slot. The first concurrent
test crashed the host with an access violation. The library also decodes TMATS through static
pointers, so the exposure is the whole of a reader's setup, not just the open.

**Fixed in the application, PR #164.** `lib/irig106` may not be edited, so `Chapter10Reader::loadChannels`
and `Ch10PacketReader::prepare` now hold one process-wide lock (`include/model/irig106lock.h`) for their
whole setup and leave packet reads concurrent. This spike first worked around it with a mutex of its own in
`tmdq_core.cpp`; with `main` merged in, that mutex is gone and the DLL relies on the readers' lock alone.
Rebuilt, the 20-check driver passed eight consecutive runs, including the two-threads-at-once check.

The application did not fail today, because it opens each file once from one thread. Anything that opened
recordings concurrently (a batch worker, a second window, calibration beside a main run) would have hit it,
and as an access violation rather than an error.

**2. A non-ASCII path needs the UTF-8 active code page, and `dotnet run` cannot provide it.**
irig106 opens files with the C runtime's narrow `open()`, which reads the process code page. Under
the default code page (1252) a folder containing `Ω` fails cleanly:
`exception at the ABI boundary: No mapping for the Unicode character exists in the target
multi-byte code page` (rc 5, a clean error, not a crash). With the UTF-8 manifest the same path
opens and returns the same 116 samples. The Qt application embeds that manifest; any host of the
DLL must too.

The trap: setting `<ApplicationManifest>` in the csproj does **not** do it. The SDK embeds that
manifest in the managed assembly, not in the apphost `.exe` that is the process, and `dotnet run`
starts `dotnet.exe`, whose own manifest wins. Verified: the exe carried no manifest and
`GetACP()` stayed 1252. `driver/HybridDriver.csproj` therefore merges the manifest into the exe
with `mt.exe` after the build (`-p:Utf8Manifest=true`), and the exe must be run directly.
With that, `GetACP()` is 65001.

## 2. Can WPF host the chart and draw over it? Yes

### The shape

```
Direct2D  ->  D3D11 shared texture  ==(DXGI shared handle)==  D3D9Ex surface  ->  D3DImage  ->  WPF
```

`D3DImage` accepts only an `IDirect3DSurface9`, so the native chart renders on D3D11/Direct2D (what
`chart-spike` measured) into a texture created with `D3D11_RESOURCE_MISC_SHARED`, and a D3D9Ex device
opens that same texture through its shared handle. The D3D9Ex device exists only to own the surface;
nothing is drawn on it. The managed side never touches COM: it gets one `IntPtr` and calls
`D3DImage.SetBackBuffer`.

**The airspace problem does not arise.** The chart is an `Image` whose source is a `D3DImage`, so it
is an ordinary WPF element that WPF composes. The legend (`Border`), crosshair (`Line`) and readout
(`TextBlock`) are plain XAML elements placed after it in a `Grid`. No `HwndHost`, no layered
windows, no special handling. See `wpf/screenshot.png`: a translucent legend over live series.

Read back from the composed desktop at a point inside the legend's padding: `#272F3D`, where the
chart alone is `#FFFFFF`. The legend is on top, with its alpha applied.

### Measurements (RTX 4070 SUPER, hardware, WPF render tier 2, Release, 1200x700 plot, 1.5 px pen)

Native render time is geometry (cull + per-pixel-column envelope) + Direct2D issue + a GPU fence.
The chart-spike's Direct2D draw alone ranged 5.4-15.4 ms (3 x 360k) and 18-31 ms (12 x 360k).

| scenario | native render, median / p95 | frame interval, median / p95 |
|---|---|---|
| nothing drawn (cost of the `D3DImage` path itself) | 0.3 / 0.4 ms | 11.1 / 11.1 ms |
| 3 x 360k, rendered on the UI thread | 5.9 / 7.8 ms | 16.7 / 27.8 ms |
| 12 x 360k, rendered on the UI thread | 20.9 / 29.2 ms | 33.3 / 44.5 ms |
| 3 x 1M, rendered on the UI thread | 6.6 / 6.8 ms | 16.7 / 22.3 ms |
| 3 x 360k, rendered on a worker thread | 5.6 / 6.0 ms | **11.1 / 11.1 ms** |

What the table says:

- **The `D3DImage` path itself is essentially free:** 0.3 ms for Lock + AddDirtyRect + Unlock. The
  display here runs frames in 11.1 ms steps (intervals are multiples of 5.56 ms), and an empty
  chart holds that.
- **The shared texture adds nothing visible to the render time.** 5.9 ms for 3 x 360k, geometry included,
  through WPF is the same order as chart-spike's offscreen figures; Qt's baseline for the same case
  is an 8 ms repaint.
- **Rendering on the UI thread costs frame rate**: the render time lands inside the UI thread's
  frame, so 5.9 ms of drawing turned an 11.1 ms cadence into 16.7 ms, and 12 series (21 ms) into
  33 ms. That is a design consequence, not a `D3DImage` defect.
- **Rendering on a worker thread removes the cost.** The worker produced 786 frames while the UI
  presented 398; the UI thread's whole contribution was **0.029 ms** per frame, and the interval
  stayed at 11.1 ms with **0 of 389** frames late. This is the design to use: the chart renders on
  its own thread, the UI thread only swaps.

### Tearing

Each frame stamps a colour-coded marker into the top and the bottom of the image. The harness reads
both back from the **composed desktop** (`GetPixel` on the screen DC), so what is checked is what
the compositor produced, not what the renderer intended. Two separate reads are not atomic against
the desktop compositor, so any mismatch is re-read 2 ms later; one that resolves was a sampling
race, one that persists is a tear that stayed on screen.

- **With the GPU fence: 0 persistent tears** across the five fenced phases, about 1,780 sampled
  frames per run, in the two runs that used the re-read check. The first-read mismatches (a few
  percent of frames, always bottom = top + 1) all resolved on the re-read: they are the sampling
  race. My first reading of them as tears was wrong, which is why the re-read exists.
- **Without the fence: not safe, but not deterministic.** One run showed 0 persistent tears and the
  next showed **13 persistent tears of 396** frames, every one of them a cleared white surface
  (marker `FFFF`) - WPF composing the texture after `Clear()` and before the draw calls had
  finished. A fence is mandatory, and the fact that two of three unfenced runs looked fine is
  exactly the way this would have shipped. (Two earlier runs, before the re-read existed, counted
  8 and 10 first-read mismatches in the unfenced phase, which cannot be told apart from the race.)
- **Single buffer on a worker thread: 0 of 396**, with the fence. The worker is rendering the same
  texture WPF is composing from, and that was clean. This is one 400-frame run on one GPU, so it is
  evidence the design works, not a proof. **Double buffering (two textures, swap on `Unlock`)
  was not built**, and would be the next hardening step if a field report ever shows a tear.

## Not done, and what could not be checked

- **Interactive behaviour.** The crosshair and readout exist (`--interactive` leaves the window open)
  but were not exercised by hand. Only the scripted run and one screenshot were checked.
- **`RenderTargetBitmap` does not reproduce a `D3DImage` composition.** Rendering a tree that contains
  one gave a legend pixel of `#C5C7CA` where the real desktop shows `#272F3D`. Not understood. For the
  headless UI-testing story this matters: UI tests of this stack cannot lean on `RenderTargetBitmap`
  for pixels over the chart, and a real-desktop read like this one needs an interactive session.
- **Other GPUs.** One adapter, one monitor refresh, one driver. WARP (software) cannot share a surface
  with a D3D9 device, so a machine without a working hardware adapter would fail at surface creation
  with an HRESULT and a message; there is no fallback built.
- **Lost device.** `IsFrontBufferAvailable` is `True` here; handling a lost front buffer
  (RDP session switches, a driver reset) is not implemented.
- **The legend is not yet draggable, and the plot has no real axes.** This answers "can WPF draw over
  it", not "is the chart UI done".

## Running it

```powershell
# the Qt-free core, then the managed host that checks it
powershell -ExecutionPolicy Bypass -File dotnet\hybrid-spike\native\build_core.ps1
. .\scripts\env.ps1
dotnet build -c Release dotnet\hybrid-spike\driver -p:Utf8Manifest=true
.\dotnet\hybrid-spike\driver\bin\Release\net10.0\HybridDriver.exe

# the chart in WPF (opens a topmost window for about 30 s, then writes results.txt and exits)
powershell -ExecutionPolicy Bypass -File dotnet\hybrid-spike\native\build_chart.ps1
dotnet build -c Release dotnet\hybrid-spike\wpf
.\dotnet\hybrid-spike\wpf\bin\Release\net10.0-windows\WpfChartSpike.exe
```

The driver needs `tests\data\test files\prn_testfile, sample.ch10` (committed). `tmdq_core.dll`
builds against `main` as merged: `constants.h` is Qt-free (PR #163).
