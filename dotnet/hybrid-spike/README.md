# Hybrid-port spike: a Qt-free native core, and a WPF or Avalonia shell over a native chart

Four questions, answered by running code rather than by argument. The first two come from
`docs/dotnet-migration/`: the README's plan (native MSVC core behind a flat C ABI, C# shell, native
Direct2D chart) and `presentation-layer.md`'s open item (can WPF host the chart *and* draw over it).
The third is whether Avalonia, which the team knows, performs differently from WPF in that role, and
the fourth is how each one's UI can be tested on this project's CI runner.

Everything here is additive. Nothing in `src/`, `include/` or the Qt build changes.

## Layout

| path | what |
|---|---|
| `native/build_core.ps1`, `native/tmdq_core.cpp` | The decommutation core as a framework-free DLL. Plain `cl`/`link`, no Qt anywhere. |
| `driver/` | A console host (C#, net10.0) that drives that DLL through the interop spike's unchanged `TmdqCore.cs` and checks it against recorded numbers. |
| `native/build_chart.ps1`, `native/tmdq_chart.cpp` | The Direct2D chart renderer, drawing into a D3D11 texture shared with a D3D9Ex surface. |
| `wpf/` | A WPF window that shows that surface through `D3DImage`, with a WPF legend and crosshair over it, and runs a scripted benchmark. |
| `wpf/results.txt`, `wpf/screenshot.png` | The final run's output, and the window as the desktop composed it. |
| `avalonia/` | The Avalonia 12 twin of `wpf/`: the same chart DLL, imported through `ICompositionGpuInterop`, the same phases and checks (section 3). Its own `results.txt` and `screenshot.png`. |
| `common/ScreenProbe.cs` | Reads the composed desktop through DXGI Desktop Duplication. Linked into both apps, so they are judged by the same code. |
| `uitest/` | One view built in Avalonia and in WPF over a shared view model, with the same UI tests in each (section 4), a mutation runner, a script that runs a test exe on a desktop that is never shown, and `uitest/sandbox/`, which runs them in Windows Sandbox including as SYSTEM in Session 0. |

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

The display is a **180 Hz** panel, so a frame that keeps up arrives every 5.56 ms. Native render
time is geometry (cull + per-pixel-column envelope) + Direct2D issue + a GPU fence. The
chart-spike's Direct2D draw alone ranged 5.4-15.4 ms (3 x 360k) and 18-31 ms (12 x 360k).

Medians of three runs with the screen probe off (`--no-sample`), re-measured 2026-10-20. The first
version of this table was taken with a GDI `GetPixel` probe and showed an 11.1 ms cadence
throughout; it is superseded.

| scenario | chart frames / s | native render, median | frame interval, median / p95 |
|---|---|---|---|
| nothing drawn (cost of the `D3DImage` path itself) | 180 | 0.2 ms | 5.6 / 5.6 ms |
| 3 x 360k, rendered on the UI thread | 152 | 5.7 ms | 5.6 / 11.1 ms |
| 12 x 360k, rendered on the UI thread | 40 | 22.3 ms | 22.2 / 33.4 ms |
| 3 x 1M, rendered on the UI thread | 127 | 6.8 ms | 5.6 / 11.1 ms |
| 3 x 360k, UI thread, **no** GPU fence (unsafe, see Tearing) | 158 | 5.2 ms | 5.6 / 11.1 ms |
| 3 x 360k, rendered on a worker thread, one shared surface | 119 | 5.8 ms | 5.6 / 16.8 ms |

What the table says:

- **The `D3DImage` path itself is essentially free:** 0.2 ms, and an empty chart holds the full
  180 Hz.
- **The shared texture adds nothing visible to the render time.** 5.7 ms for 3 x 360k, geometry
  included, is the same order as chart-spike's offscreen figures; Qt's baseline for the same case is
  an 8 ms repaint.
- **Rendering on the UI thread costs frame rate only once the render outgrows the refresh.** 5.7 ms
  against a 5.56 ms budget gives about 150 frames a second, roughly one frame in five a refresh
  late; 22 ms gives 40.
- **The single-surface worker is not the win it looked like.** It renders 480-550 frames while the
  UI shows 340-370 of them, so about a third of the GPU work is thrown away, and about one frame in
  five is late. The earlier claim here ("0 of 389 late") came from the superseded GDI-era run. A
  worker that renders on request into a second surface was not built; this row is a lower bound.

### Tearing

Each frame stamps a colour-coded marker into the top and the bottom of the image. The harness reads
both back from the **composed desktop** through DXGI Desktop Duplication, both pixels from the
**same** desktop frame, so a mismatch is a tear on the glass and not a sampling race. (The first
version used two GDI `GetPixel` calls, which are not atomic, and needed a re-read to tell a race from
a tear; the desktop here is also HDR, which the probe has to undo - see section 3.)

- **With the GPU fence: 0 tears** in every fenced phase of every run, about 1,800 desktop frames per
  run.
- **Without the fence: not safe, and not deterministic.** One sampled run in four showed **2 tears**,
  both with a **white top** and the previous frame's marker at the bottom: WPF composed the
  texture after `Clear()` and before the draw calls had finished. The GDI-era runs found the same
  signature (13 of 396 in one run, 0 in another). `D3DImage`'s D3D9 shared surface has no GPU-side
  hand-over, so the CPU-side fence is mandatory, and the fact that most unfenced runs look fine is
  exactly the way this would have shipped.
- **Single buffer on a worker thread: 0 tears**, with the fence, in every run.

## 3. Avalonia against WPF: is there a performance difference?

Asked because the team knows Avalonia. `avalonia/` is the WPF app's twin: Avalonia 12.1.4 on its
default Windows backend (ANGLE over D3D11, composed through DirectComposition), the **same**
`tmdq_chart.dll`, the same window and overlay, the same six phases and the same checks. Both apps
run on .NET 10 on the same machine, alternated run by run.

### The shape

```
Direct2D  ->  D3D11 texture (keyed mutex)  ==(shared handle)==  ICompositionGpuInterop.ImportImage  ->  Avalonia compositor
```

No D3D9: Avalonia imports the D3D11 texture directly. The hand-over is a **keyed mutex**: the
renderer draws under key 0 and releases key 1; `CompositionDrawingSurface.UpdateWithKeyedMutexAsync`
acquires 1 and gives 0 back once the compositor has read it. That is a GPU-side synchronisation
between the two devices, which `D3DImage` does not have.

The host has to **rotate at least two textures**. The compositor keeps a texture for about one
composition (3-5 ms measured), and with only one the renderer could draw every other refresh at
best: an empty chart ran at **89 frames/s with one texture, 179 with two** (`--buffers 1` reproduces
it). WPF never shows this, because `D3DImage` copies the surface into its own front buffer on
`Unlock`. So it is a design obligation of the Avalonia path, not a defect, but it has to be built.

The legend is an ordinary Avalonia `Border` over the chart: on the glass `#272F3D` over `#FFFFFF`,
the same pixel as WPF. The import needs `TopLeftOrigin = true`, and the orientation block confirms
the image is the right way up. Resize (dispose the imports, recreate, re-import) works.

### Results

Medians of three runs each, screen probe off; per-run values were within a few percent of each
other. "Late" means later than 1.5 x the 5.56 ms refresh.

| scenario | WPF frames / s | Avalonia frames / s | what the intervals look like |
|---|---|---|---|
| nothing drawn | **180** | **179** | both at every refresh |
| 3 x 360k, UI thread, fenced | **152** | 90 | WPF: mostly 5.6 ms, about 1 in 5 late. Avalonia: a steady 11.1 ms, every second refresh |
| 12 x 360k, UI thread, fenced | **40** | 32 | WPF 22.2 ms, Avalonia 33.2 ms |
| 3 x 1M, UI thread, fenced | **127** | 84 | as 3 x 360k |
| 3 x 360k, UI thread, no fence | 158 (**unsafe**: tears) | **147** (safe) | both mostly 5.6 ms, about 1 in 5 late |
| 3 x 360k, worker thread | 119 | **162** | WPF about 1 in 5 late; Avalonia none late, every rendered frame shown |
| process start to first chart frame | **531-586 ms** | 721-758 ms | |
| working set / private bytes at exit | 201 / 187 MB | 190 / 219 MB | the chart's data dominates both |
| self-contained win-x64 publish | 140 MB, 400 files | **104 MB, 221 files** | framework-dependent: WPF 0.2 MB but needs the Windows Desktop runtime; Avalonia 27 MB on the base runtime |

0 tears in every Avalonia phase of every run, fenced or not. The worker phase is shorter in the
Avalonia run (about 190 frames, because its frame clock ticks faster while the worker posts), so
that row rests on fewer frames than the others.

### Why the numbers differ

- **The path itself costs the same.** An empty chart runs at the refresh rate in both, 0.2 ms of
  native work, 0.01 ms for the hand-over call on Avalonia's UI thread.
- **Avalonia's frame clock is quantised to whole refreshes; WPF's is not.** In the UI-thread phases
  the native render (5.7-6.1 ms) is just over the 5.56 ms budget. Avalonia's
  `RequestAnimationFrame` then lands on every second refresh, a steady 90; WPF's `Rendering` event
  catches up and averages about 150 with uneven pacing. On a 60 Hz display (a 16.7 ms budget) every
  UI-thread case except 12 x 360k would fit in both; that is inferred from the budgets, **not
  measured** - the display was not switched.
- **The keyed mutex makes the fence unnecessary on Avalonia, and dropping it is what lets the
  UI-thread render fit the budget.** Unfenced, Avalonia ran at 147 with 0 tears in every run; WPF
  unfenced tore in one run of four (section 2). On WPF the fence is mandatory.
- **The worker pattern favours Avalonia** because the hand-over paces it: the worker renders only
  into a texture the compositor has released, so every frame it renders is shown (190 of 190), and
  the UI thread's cost is 0.011 ms. WPF's single-surface worker free-runs and throws a third of its
  frames away. A paced, double-buffered WPF worker was not built, so this compares each framework's
  straightforward pattern, not each one's best.
- **At the heavy load the same native code waits longer for the GPU under Avalonia.** The render-time
  split at 12 x 360k (two runs each):

  | | CPU geometry | Direct2D issue | GPU fence wait |
  |---|---|---|---|
  | WPF | 2.1 ms | 18.6-19.9 ms | **1.6-1.7 ms** |
  | Avalonia | 2.0 ms | 18.4-18.5 ms | **7.0-7.8 ms** |

  The CPU work is identical; the GPU finishes later when Avalonia hosts the chart. The likely
  causes are Avalonia's own GPU compositor (ANGLE on D3D11) sharing the GPU, or the keyed-mutex
  texture itself. **Not isolated.** Lighter loads show the same direction at a smaller size
  (0.5 against 0.8 ms at 3 x 360k).
- **Startup:** Avalonia reaches its first chart frame about 190 ms later. Memory is the same order.

### Reading the comparison

There is **no performance case against Avalonia** for this chart. Each framework has its own way of
staying smooth:

- **WPF** tolerates UI-thread rendering better, but needs a CPU fence.
- **Avalonia** needs rotated textures, but gets GPU-side synchronisation for free, and its worker
  pattern is the better of the two as built.

With each framework's straightforward safe pattern, the steady states are close:

- **WPF:** 152 frames/s, rendering on the UI thread with the fence.
- **Avalonia:** 162 frames/s, rendering on a worker.

Where WPF leads is the heaviest load (40 against 32), which traces to GPU wait rather than to
anything on the CPU, and startup (about 190 ms).

### Measuring this on an HDR desktop

This desktop is HDR (PQ / BT.2020, SDR content at 240 nits), which cost two false starts worth
recording for anyone re-running it:

- **GDI reads black for Avalonia.** `GetPixel`/`BitBlt` on the screen DC see WPF's output but
  return black for a DirectComposition window, so both apps now read the desktop through DXGI Desktop
  Duplication (`common/ScreenProbe.cs`).
- **8-bit duplication of an HDR desktop clips.** Asking for (or falling back to) 8-bit BGRA gives the
  desktop clamped at 80 nits. SDR content sits at 3.0 in scRGB here, so every channel above about
  `#9C` read `#FF`: the marker `#EFBEA5` read back as white. The probe now asks `DuplicateOutput1`
  for FP16 and divides out the SDR white level from `DisplayConfigGetDeviceInfo`. Readback is then
  exact (`#4200A5` reads `#4200A5`, the marker reads `EF BE A5`).
- **`DuplicateOutput1` needs per-monitor-v2 DPI awareness.** WPF is not by default, and silently
  fell back to the legacy 8-bit call. The probe sets the thread's awareness for the call.
- A fresh duplication's first frame carries **no image**. Reading a still window means draining,
  presenting again, and reading the frame that present produced (`ReadStill`).

## 4. Headless UI tests: Avalonia.Headless against WPF

The other half of `presentation-layer.md`'s recommendation 3. The question is narrower than "can it
be tested": can each framework run **this project's** style of UI test - measure widgets, drive
input, render and read pixels (the `verify-ui-change` skill's two halves) - on the CI runner. That
runner is a service in **Session 0 with no interactive desktop** (`docs/ci_runner.md`); the Qt suite
gets through only because `QT_QPA_PLATFORM=offscreen` needs no window system.

### What was built

`uitest/` holds one small view built twice, `avalonia/PlotView.axaml` and `wpf/PlotView.xaml`: a
title strip with a 30x26 button, the plot surface, and a translucent legend capped at 120 px that
scrolls beyond that. Both versions bind to one shared view model, `common/PlotVm.cs`. Each project
holds the same five tests, ported from `tst_plotwidget` and the project's history:

| test | ported from |
|---|---|
| `LegendGrowsWithItsRowsAndScrollsOnceItIsFull` | `legendOverlaySizesCorrectlyAfterRebuild` (the collapsed-legend regression) |
| `TitleButtonKeepsTheSizeSetInCode` | the PR #60 trap: a theme minimum overriding a size set in code |
| `KeyboardShortcutsDriveTheView` | `keyboardShortcutsDriveTheView` (L, arrows, Home) |
| `DraggingABandZoomsToIt` | `bandZoomAppliesDraggedRange` (both directions; a click does not collapse the view) |
| `ARenderedFrameShowsTheLegendOverThePlotWithText` | the skill's "render and look", as an assertion: legend pixel dark over a white plot, and real glyphs in the label |

Avalonia adds two fact-finding tests: `TheNativeChartSurfaceIsNotAvailableHeadless` and
`TheHeadlessPlatformCreatesNoWin32Window`. Both projects run on xunit v3 3.2.2. Avalonia uses
`Avalonia.Headless.XUnit` 12.1.4 with Skia rendering and the Fluent theme it ships with, so the
size test measures what a user would get.

**Each test is validated against seven single-defect mutants, in each framework, and all 14 runs
caught theirs:**

- a theme style setting `MinHeight` on the title button;
- the legend's height cap removed;
- the L key ignored;
- the legend's visibility not bound;
- the drag mapping x through the plot's height;
- the legend made transparent;
- the label text emptied.

`uitest/run_mutants.py` runs them. It deletes the exe before each rebuild, so a failed build cannot
run the previous binary, restores the source with a fresh mtime, and counts a mutant as caught only
when the test written for it fails, not merely some test.

### Results

| capability | Qt today (offscreen) | Avalonia.Headless | WPF |
|---|---|---|---|
| Runs with no window system | yes | **yes: creates no HWND at all** (tested, with the counter proven able to see one) | no: needs a real, if hidden, window (`HwndSource`) per test |
| Measure geometry, min/max, scroll extent | yes | yes (`Bounds`, `Extent`) | yes (`ActualWidth`, `ExtentHeight`) |
| Catches a theme minimum overriding a code size | yes | yes | yes |
| Keyboard input | `QTest::keyClick` | through the real pipeline: focus, routing, a QWERTY key map | raised on the element (`RaiseEvent`), bypassing focus routing |
| Pointer input at a position | `QTest::mouseClick(pos)` | **yes**: press, move and release at window coordinates, through hit-testing and capture | **no supported way**: a synthesised event reads the real cursor, so the test calls the view's handler (`ApplyBand`), as the Qt test calls `applyBandZoom` |
| Rendered pixels | yes, but **text draws as empty boxes** | yes, Skia, **real text** | yes, `RenderTargetBitmap`, real text |
| The native chart surface in a test | n/a (`TmChart` is `QPainter`) | no: GPU interop is null headless (tested) | no: `RenderTargetBitmap` does not reproduce a `D3DImage` (section 2) |
| Test harness | Qt Test | an attribute (`[AvaloniaFact]`) and a five-line app builder | hand-written: an STA thread per test, dispatcher pumping, an `HwndSource` (about 40 lines) |
| Run time, this suite | - | 0.69-0.74 s for 7 tests | 1.03-1.07 s for 5 tests |

Both suites were also run on a **desktop that is never displayed and gets no input**
(`uitest/run_without_desktop.ps1`), and both passed:

- **Avalonia:** 7 of 7, which is unsurprising since it never touches Win32 windowing.
- **WPF:** 5 of 5, hidden window included.

**That hidden desktop is not Session 0.** The script tries to give the tests a window station of
their own, which is a service's situation. This account is refused (error 5, also outside the tool
sandbox), so the script falls back to a hidden desktop on the interactive window station, and says so
in its output. The Sandbox run below is the stronger evidence.

### A clean Windows Sandbox: both suites pass in Session 0

`uitest/sandbox/run_sandbox_ui_tests.ps1` publishes both suites self-contained (a clean Sandbox has no
.NET), launches a Sandbox from a generated `.wsb`, and inside it runs each suite twice: in the logon
session, and as SYSTEM through a scheduled task, which runs in Session 0 with no desktop (the
Sandbox user is an administrator, so it can register one). A wrapper records the session each run
really executed in, so "Session 0" is observed rather than assumed.

| suite | logon session (1) | SYSTEM, Session 0 |
|---|---|---|
| Avalonia | 7 of 7 | **7 of 7** |
| WPF | 5 of 5 | **5 of 5** |

All 24 tests passed, including the rendered-pixel test, and in WPF's case the hidden `HwndSource`
window in a session that has no desktop. The Sandbox reported Windows 11 Enterprise 26100, no .NET
installed, no GPU (a remote display adapter, so software rendering), and an administrator user.

**What it does not prove:**

- **The account is SYSTEM, not NETWORK SERVICE.** The CI runner's service uses the lower-privileged
  NETWORK SERVICE. Both are non-interactive Session 0 accounts, but this was not run as NETWORK SERVICE.
- **A scheduled task is not a service host.** The process is in Session 0 with no desktop, as a
  service's is, but it was not started by the Service Control Manager.
- **One machine image, one run.** The pass was not repeated, and the rendering was software only.
- The one thing it does not touch is the real runner, so the cheapest remaining confirmation is a
  step in `ci.yml` that runs the two exes there.

**Smart App Control, and signing.** The first Sandbox attempt ran nothing: every run exited 4551
("blocked by your organization's Device Guard policy") before reaching test code. A fresh Sandbox
reports Smart App Control **on** and user-mode code integrity **enforced**, and these builds were
unsigned. This host has Smart App Control off, which is why they run here, and the installer harness
passes in the same Sandbox because `setup.exe` is signed.

Only 23 of the 711 PE files in the two self-contained publishes are unsigned (the other 688 are
signed by Microsoft, .NET or xUnit): 21 Avalonia assemblies and the two test executables with their DLLs.
`-SignCertSha1 <thumbprint>` signs exactly those, in the staged copy under `.work` only, then
verifies each signature. It signs nothing unless passed. **The Avalonia assemblies are third-party
files, so signing them with the project's certificate puts the project's publisher identity on code it
did not write.** That is acceptable for a throwaway staged copy and not for anything shipped. The
certificate is token-backed, so unlock SimplySign first or signtool waits on the PIN prompt. Turning
Smart App Control off was deliberately not used instead: it is a security setting.

### Reading it

- **Avalonia's headless platform is the stronger testing story, in three specific ways:**
  - it drives pointer input through the real pipeline at coordinates, which WPF cannot and Qt can;
  - it renders real text, which Qt's offscreen platform cannot and WPF's `RenderTargetBitmap` can;
  - it needs no window system at all, so Session 0 is a non-issue rather than a risk.
- **WPF is workable for everything the Qt suite does today except positioned pointer input.** The
  Qt tests mostly bypass that anyway, by calling the handler. Its harness is not a supported headless
  mode, but it passed in Session 0 as SYSTEM, so the risk is narrower than it looked: NETWORK SERVICE
  and the real runner are the remaining differences.
- **Neither can put the native chart in a test.** The chart host needs a seam (a stub surface), and
  the chart's own pixels stay tested at the native level, as `TmChart`'s are now.

## Not done, and what could not be checked

- **Interactive behaviour.** The crosshair and readout exist (`--interactive` leaves the window open)
  but were not exercised by hand. Only the scripted run and one screenshot were checked.
- **`RenderTargetBitmap` does not reproduce a `D3DImage` composition.** Rendering a tree that contains
  one gave a legend pixel of `#C5C7CA` where the real desktop shows `#272F3D`. Not understood. For the
  headless UI-testing story this matters: UI tests of this stack cannot lean on `RenderTargetBitmap`
  for pixels over the chart, and a real-desktop read like this one needs an interactive session.
- **The real CI runner.** The UI tests passed in Session 0 as SYSTEM in a clean Sandbox, not as
  NETWORK SERVICE on the runner (section 4).
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

# the same in Avalonia (restores the Avalonia 12.1.4 packages from NuGet on first build)
dotnet build -c Release dotnet\hybrid-spike\avalonia
.\dotnet\hybrid-spike\avalonia\bin\Release\net10.0\AvaloniaChartSpike.exe
```

The UI tests (section 4) are xunit v3 executables:

```powershell
dotnet build -c Release dotnet\hybrid-spike\uitest\avalonia
dotnet build -c Release dotnet\hybrid-spike\uitest\wpf
.\dotnet\hybrid-spike\uitest\avalonia\bin\Release\net10.0\AvaloniaUiTests.exe
.\dotnet\hybrid-spike\uitest\wpf\bin\Release\net10.0-windows\WpfUiTests.exe
# the same, on a desktop that is never shown
powershell -ExecutionPolicy Bypass -File dotnet\hybrid-spike\uitest\run_without_desktop.ps1 dotnet\hybrid-spike\uitest\wpf\bin\Release\net10.0-windows\WpfUiTests.exe
# the mutation check
python dotnet\hybrid-spike\uitest\run_mutants.py avalonia
```

The chart apps both take `--no-sample` (no screen probe, so pacing carries no capture cost; the composition checks
then cannot run). The Avalonia app also takes `--buffers N` (shared textures rotated, default 2) and
`--bottom-left` (import with a bottom-left origin, to see the image flip). Do not move the mouse
over the window during a run: the crosshair is live.

The driver needs `tests\data\test files\prn_testfile, sample.ch10` (committed). `tmdq_core.dll`
builds against `main` as merged: `constants.h` is Qt-free (PR #163).
