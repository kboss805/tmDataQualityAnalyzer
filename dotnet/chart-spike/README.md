# Chart spike: is Direct2D fast enough?

The second of the two spikes that decide the Microsoft-only move (see
`docs/dotnet-migration/README.md`). The chart is the real risk in the port: `tmchart.cpp`
encodes performance findings that took three separate sessions to reach, and the
assessment's worry was that they are **QPainter-specific** and would not transfer.

**Result: they transfer, and Direct2D is comfortably faster. The rendering story is
safe.** One of the three findings turns out not to apply at all - see below.

```powershell
powershell -ExecutionPolicy Bypass -File dotnet\chart-spike\run_d2d_bench.ps1
#   -SkipBuild
```

## Measured

1200x700 plot, 1.5 px pen, antialiasing on, `/O2` - the same shapes and conditions as
the Qt baselines in `docs/CLAUDE.md` (v2.13.0, release build). Device: **D3D11
hardware**. Direct2D figures are best-of-5 after a warm pass, each with an explicit
`Flush()` so the timer measures rendering rather than queue submission.

| samples/series | series | geometry | per-segment | batched | Qt first | Qt repaint |
| --- | --- | --- | --- | --- | --- | --- |
| 360,000 | 3 | 2.11 ms | 7.48 ms | **6.75 ms** | 36 ms | 8 ms |
| 360,000 | 12 | 8.33 ms | 19.66 ms | **18.08 ms** | 157 ms | 34 ms |
| 1,000,000 | 3 | 5.57 ms | 7.97 ms | **7.74 ms** | 79 ms | 13 ms |

- `geometry` is the port of `TmChart::rebuildSeriesGeometry` - binary-search culling
  plus the per-pixel-column envelope. It is what Qt's "first paint" includes and its
  "repaint" does not.
- `per-segment` / `batched` are two ways to rasterize the same cached geometry, both
  including the min-max band (filled and outlined) and the mean line.

So, like for like:

| | Direct2D | Qt | |
| --- | --- | --- | --- |
| Redraw from cached geometry, 12 series | **18.1 ms** | 34 ms | ~1.9x faster |
| Build + redraw, 12 series | **26.4 ms** | 157 ms | ~5.9x faster |
| Build + redraw, 1M x 3 | **13.3 ms** | 79 ms | ~5.9x faster |

Comfortable, not miraculous - and comfortable is what the decision needed.

## The finding that matters most

**This project's `drawLines()`-not-`drawPath()` rule does not carry over.** On Qt the
difference was enormous: the same geometry and pen measured 1423 ms through
`drawPath()` against 27 ms through `drawLines()`, because a non-cosmetic pen sends a
path through Qt's stroker, which builds an outline polygon across the whole run.

On Direct2D the two are **near-equivalent**, and the batched path is marginally the
*faster* of the two (6.75 vs 7.48; 18.08 vs 19.66; 7.74 vs 7.97 - a 3-10% edge). There
is no stroker cliff to avoid.

That is worth knowing because the Qt rule is load-bearing in `tmchart.cpp` and a port
would naturally carry it across as gospel. It is a **Qt-specific workaround**, not a
general truth, and on Direct2D the more natural batched-geometry style is also the
slightly quicker one.

## Two flaws I found in my own benchmark

Recorded because both changed the answer, and the first version of each looked
perfectly plausible.

**1. The band was built but never drawn.** v2.14.0 added the translucent min-max band
to the Qt chart, so it is part of what the 34 ms baseline pays for. Omitting it measured
less work than Qt. With the band included, the 12-series redraw went **7.10 ms to
18.08 ms** - the band is roughly 60% of rasterization cost. The first set of numbers
overstated Direct2D's advantage by about 2.5x.

**2. `batched` built its path geometry inside the timed region.** `TmChart` caches its
segments, so timing geometry construction on the Direct2D side compared
building-every-frame against reusing. That made batched look ~40% worse than it is and
inverted the per-segment/batched ordering. Both bands and line paths are now built once,
outside the loop, which is what makes the comparison against Qt's cached-geometry
repaint honest.

## What is NOT measured

- **Win2D, and therefore the chart in C#.** See the blocker below. Win2D *is* Direct2D -
  `CanvasDrawingSession` wraps `ID2D1DeviceContext` and the rasterizer is identical - so
  these numbers describe the rasterizer Win2D would use. What they do not include is the
  per-call managed-to-native overhead Win2D adds. With batched geometry that is a handful
  of calls per frame rather than thousands, so it should be small, but **should be is not
  measured**.
- **The geometry build in C#.** The 8.33 ms envelope build is C++ here. In an all-C# shell
  that loop would be C# over 4.3M samples - the one piece of the chart that is real
  per-sample work. `Span<T>` should carry it, but this does not show that.
- **Software rendering.** These are hardware D3D11 numbers. The benchmark falls back to
  WARP and says so when it does, but WARP was not measured.
- **The backing-store blit.** Qt's v2.14.0 cached-pixmap trick makes a repaint that
  changed nothing cost under 1 ms. That is orthogonal to the rasterizer and available on
  Direct2D identically, so it is not part of this comparison - but it means neither stack
  pays the numbers above for a crosshair move.

## Blocker: Win2D does not build on this machine

The intended spike was Win2D, so the chart could live in C#. `Win2DProbe.csproj` is kept
as the record of why it could not run, and will build as-is on a machine with the right
tooling.

`Microsoft.Graphics.Win2D` pulls in `Microsoft.WindowsAppSDK` transitively, whose
`MrtCore.PriGen.targets` require Visual Studio's **AppxPackage MSBuild tooling**:

```text
error MSB4062: The "Microsoft.Build.Packaging.Pri.Tasks.ExpandPriContent" task could not
be loaded from the assembly ...\Microsoft\VisualStudio\v18.0\AppxPackage\
Microsoft.Build.Packaging.Pri.Tasks.dll
```

Only VS **BuildTools** is installed here, without that workload. Setting
`AppxGeneratePriEnabled=false` clears that task and the build then fails on the next one
(`Microsoft.Build.AppxPackage.RemovePayloadDuplicates`). Chasing a package's own tooling
requirements with property overrides is not a foundation to measure on, so the attempt
was abandoned rather than hacked around.

**The prerequisite, stated plainly:** the WinUI 3 / Win2D path needs Visual Studio with
the WinUI or UWP workload - not just the .NET SDK and BuildTools. That is a real cost of
the "chart in C#" option, and it is the kind of thing worth knowing before committing to
a framework rather than after. The Windows App **Runtime** (1.5 and 1.6) is already
installed; it is the **build-time** tooling that is missing.

## Files

| | |
| --- | --- |
| `d2d_chart_bench.cpp` | the benchmark. Offscreen D3D11 + Direct2D, no window - which is also why render tests get easier on this stack than with `QT_QPA_PLATFORM=offscreen` |
| `run_d2d_bench.ps1` | builds with `cl` directly (no Qt involved) and runs it |
| `Win2DProbe.csproj`, `Probe.cs` | the blocked Win2D attempt, kept as evidence |

`NOMINMAX` is defined before any Windows header in the benchmark: `<windows.h>` arrives
via `d3d11.h` and defines `min`/`max` as macros, which breaks every `std::min`/`std::max`
with `error C2589: '(': illegal token on right side of '::'`.
