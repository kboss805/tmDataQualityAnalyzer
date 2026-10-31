# Chart spike: Direct2D, Direct3D, and Win2D

The second of the two spikes that decide the Microsoft-only move (see
`docs/dotnet-migration/README.md`). The chart was the real risk: `tmchart.cpp` encodes
performance findings that took three sessions to reach, and the assessment's worry was
that they are **QPainter-specific** and would not transfer.

**They transfer. Every Microsoft option measured is faster than the Qt baseline, and
Direct3D is faster by three orders of magnitude.** The rendering story is safe.

```powershell
powershell -ExecutionPolicy Bypass -File dotnet\chart-spike\run_d2d_bench.ps1   # -SkipBuild
```

## Measured

1200x700, 1.5 px pen, antialiasing on, `/O2`, D3D11 **hardware** - the same shapes and
conditions as the Qt baselines in `docs/claude.md` (v2.13.0, release).

| samples/series | series | geometry | Direct2D | Direct3D | Qt first | Qt repaint |
| --- | --- | --- | --- | --- | --- | --- |
| 360,000 | 3 | 2.2 - 2.6 ms | 5.4 - 15.4 ms | **2 us** | 36 ms | 8 ms |
| 360,000 | 12 | 8.3 - 10.9 ms | 18 - 31 ms | **5 us** | 157 ms | 34 ms |
| 1,000,000 | 3 | 5.6 - 6.2 ms | 8.0 - 15.5 ms | **1 us** | 79 ms | 13 ms |

**Ranges, not single figures.** The Direct2D numbers move by up to **3x between runs** on
an otherwise busy machine: across seven runs the 12-series case measured 18.1, 19.4,
19.6, 21.1, 28.9, 30.6 and 30.6 ms, and the 360k x 3 case ranged 5.4 to 15.4 ms. A single
Direct2D figure would be false precision. Direct3D, by contrast, held at 1-5 us
throughout - unsurprising once you see that its cost is 24 draw calls rather than
fourteen thousand, so there is far less CPU work for machine load to perturb.

That spread is worth noting for its own sake: **Direct2D's figure here is a CPU
measurement wearing a GPU costume**, which is exactly why it is sensitive to what else
the machine is doing. Against Qt's 34 ms it is faster at the good end and a wash at the
bad end - so "Direct2D is comfortably faster than Qt" is the honest claim only for the
12-series case, and even there the margin is not large.

`geometry` is the port of `TmChart::rebuildSeriesGeometry` - binary-search culling plus
the per-pixel-column envelope. It is what Qt's "first paint" includes and its "repaint"
does not, and it is **pure arithmetic, ported unchanged**, which is what the assessment
predicted and the one thing that was never really in doubt.

## Why Direct3D is three orders of magnitude faster

Not because the GPU is faster than the GPU - **Direct2D already runs on Direct3D**; its
device is created from the same `IDXGIDevice`. The difference is the amount of CPU work
and API traffic each path needs for the same picture:

| | Direct2D | Direct3D |
| --- | --- | --- |
| draw calls, 12 series | **~14,424** | **24** |
| per-call work | tessellate path, stroke, submit | bind a vertex buffer, `Draw` |

Direct2D's cost here is overwhelmingly **CPU-side**: tessellation and per-call overhead
across fourteen thousand calls. Direct3D's is a vertex buffer the GPU eats in
microseconds. The envelope algorithm already emits *two vertices per pixel column* for
the band - a triangle strip by construction - so the chart's geometry was already
GPU-shaped without anyone intending it.

**The practical consequence: with Direct3D the renderer stops being the bottleneck
entirely.** The geometry build (2.2 - 10.9 ms) becomes ~2000x the cost of drawing it. If
the chart ever needs to go faster after that, the target is the envelope build - a
parallel reduction of 4.3M samples to 1200 columns, which is textbook compute-shader
work. That is upside the Qt stack cannot reach at all, but it is optimising a budget
nothing is currently blowing.

## Two measurement flaws I found in my own benchmark

Both produced plausible-looking numbers that were wrong. Recorded because the method
matters more than the figures.

**1. A single D3D frame is faster than the GPU fence used to time it.** The first
version reported **0.01 - 0.04 ms** for Direct3D, implying ~500x Direct2D. That was
submission cost, not render cost: neither `ID2D1DeviceContext::Flush()` nor
`ID3D11DeviceContext::Flush()` waits for completion. Adding a `D3D11_QUERY_EVENT` fence
to **both** paths was necessary but not sufficient - the fence's own overhead (~0.035 ms)
is *larger than the frame*, so the one-shot number was a floor.

Caught by a scaling check: timing 50x the work returned only **2.7x to 7.3x** the time.
If a measurement does not scale with the work, it is not measuring the work. The
benchmark now solves `w = (t(50) - t(1)) / 49`, which cancels the fixed fence cost, and
reports the one-shot floor beside it so the reader can see the difference.

**2. The band was built but never drawn.** v2.14.0 added the translucent min-max band to
the Qt chart, so it is part of what the 34 ms baseline pays for. Omitting it measured
less work than Qt. Including it moved the 12-series Direct2D redraw from **7.1 ms to
18.1 ms** - the band is roughly 60% of rasterization - and my first numbers overstated
Direct2D's advantage by about 2.5x. A related fix: `batched` was building its path
geometry *inside* the timed region, comparing Direct2D-building against Qt-reusing.

## The finding worth carrying forward

**This project's `drawLines()`-not-`drawPath()` rule does not transfer.** On Qt the
difference was enormous - the same geometry and pen measured 1423 ms through
`drawPath()` against 27 ms through `drawLines()`, because a non-cosmetic pen goes
through Qt's stroker, which builds an outline polygon across the whole run.

On Direct2D the two are **within noise of each other** (per-segment vs batched: 6.21 vs
6.22, 30.55 vs 28.94, 15.50 vs 15.25). There is no stroker cliff. It is a Qt-specific
workaround, not a general truth, and a port would naturally carry it across as gospel
and write awkward code for no reason.

## Win2D: it works, but it binds you to the 1.x line

Win2D **builds and runs** now - the earlier blocker was Visual Studio tooling, resolved
by installing VS Community 2026:

```text
Windows App Runtime 1.8 initialized
Win2D OK: offscreen 1200x700 rendered
```

Two things were needed, neither of them a missing runtime:

- **VS supplies the AppxPackage MSBuild tooling** (`Microsoft.Build.Packaging.Pri.Tasks.dll`).
  With only VS BuildTools, `Microsoft.Graphics.Win2D` cannot build at all, because it
  pulls `Microsoft.WindowsAppSDK` transitively and its `MrtCore.PriGen.targets` need
  that tooling. **This is a real prerequisite of the chart-in-C# option.**
- **`Bootstrap.TryInitialize` must be called explicitly.** The auto-initializer kills the
  process on failure with no message - a silent `exit 22` that says nothing about which
  runtime family it wanted. An explicit call returns an HRESULT instead and initialized
  first try.

**But Win2D is a 1.x component with no successor.** Verified against the NuGet API:

| | |
| --- | --- |
| `Microsoft.Graphics.Win2D` | 28 versions, newest **1.4.0**, **zero 2.x**, not even prerelease |
| `Microsoft.WindowsAppSDK` 2.5.1 | a 72 KB metapackage with **no graphics component** |
| `.Graphics` / `.Canvas` / `.Direct2D` / `.Win2D` 2.x packages | **do not exist** |
| `.DWrite`, `.InteractiveExperiences` | **do** exist as 2.x components |

So text and composition made the 2.x split and 2D drawing did not. An app on Win2D is
pinned to a 1.x Windows App SDK and a 1.x runtime, with nothing announced to migrate to.
Native Direct2D and Direct3D have **no such exposure** - they are Windows SDK, not
Windows App SDK, and are not versioned by this split.

Caveat on that inference: absence of a 2.x package is strong evidence, not an official
statement. Microsoft's own WinUI 3 Win2D page still reads as work-in-progress.

## What Direct3D would cost

Not GPU power - the opposite. Two real costs:

- **Text.** Axis labels, legend, title. Direct2D gives this free through DirectWrite; raw
  D3D needs a glyph atlas. **The hybrid is the standard answer**: D3D for the data,
  Direct2D/DirectWrite for the chrome, both targeting the same D3D surface.
- **Antialiasing.** Direct2D does it per-primitive. The D3D path here uses 4x MSAA and
  expands each segment to a quad at the same 1.5 px width, so the comparison is
  pixel-equivalent rather than 1-pixel line primitives - which would have flattered D3D.

## Recommendation

**Direct2D is already enough**, and it is the lower-risk starting point: measured well
clear of Qt, text for free, and no Win2D version trap. **Keep Direct3D as the escape
hatch** - the measurement says it is there and it is enormous, so there is no need to
spend its complexity up front. Either way, keep the renderer behind your own interface
so the backend stays swappable.

## What is still not measured

- **The envelope build in C#.** The 2.2 - 10.9 ms geometry build is C++ here. In an
  all-C# shell that loop is C# over 4.3M samples - the one piece of real per-sample work
  in the chart. `Span<T>` should carry it; this does not show that.
- **Win2D's managed call overhead.** Win2D *is* Direct2D, so the rasterizer numbers
  apply, but the per-call managed-to-native cost is not included. With ~14,000 Direct2D
  calls per frame that overhead is the thing most likely to matter, which is another
  argument for the batched or D3D styles.
- **Software rendering.** These are hardware D3D11 numbers; the benchmark falls back to
  WARP and says so, but WARP was not measured.
- **The backing-store blit.** Qt's v2.14.0 cached-pixmap trick makes an unchanged repaint
  cost under 1 ms. That is orthogonal and available identically here, so neither stack
  pays these numbers for a crosshair move.

## Files

| | |
| --- | --- |
| `d2d_chart_bench.cpp` | the benchmark - offscreen D3D11, no window, which is also why render tests get easier here than with `QT_QPA_PLATFORM=offscreen` |
| `run_d2d_bench.ps1` | builds with `cl` directly (no Qt) and runs it |
| `Win2DProbe.csproj`, `Probe.cs` | the Win2D probe, now working, with an explicit bootstrap |

`NOMINMAX` is defined before any Windows header: `<windows.h>` arrives via `d3d11.h` and
defines `min`/`max` as macros, breaking every `std::min`/`std::max` with
`error C2589: '(': illegal token on right side of '::'`.
