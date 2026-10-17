# `explore/dotnet-migration`

Exploring a **Microsoft-only** stack - MSVC C++ core, C# shell - while keeping the
measured performance of the current build. See
`docs/future_plans/dotnet-port-assessment.md` on `main` for the costed assessment that
led here, including why the full C# rewrite is **not** recommended.

**This is an exploration, not a committed direction.** `main` stays shippable and is
where maintenance happens. Nothing here blocks a release.

## The one rule that keeps this branch mergeable

> **If a change keeps the Qt application building and shipping, it belongs on `main`,
> not here.**

Drift is proportional to how many *existing* files this branch modifies. A branch that
only **adds** files merges from `main` forever; a branch that rewrites the model layer
conflicts with every maintenance fix, because the model layer is exactly what
maintenance touches.

So the split is:

| Work | Where | Why |
| --- | --- | --- |
| De-Qt the native core (431 Qt occurrences over ~4,170 lines) | **`main`** | Keeps the Qt app working at every step, is independently valuable, and touches the files maintenance touches. Putting it here is the maximum-conflict choice. |
| Throughput guard (`scripts/run_throughput_bench.ps1`) | **`main`** | Already landed. It is how every claim below gets measured. |
| C# solution, projects, XAML shell | here | New files |
| Flat `extern "C"` ABI + P/Invoke interop | here | New files |
| Win2D / Direct2D chart | here | New files |
| `.sln` alongside the existing `.pro` | here | Coexistence is branch-only |

That is deliberate: the long pole (de-Qt) runs on `main` through ordinary PRs, and this
branch accumulates almost entirely new code.

## Why native C++ stays, and why that is still "Microsoft-only"

**MSVC is Microsoft.** Qt is the only non-Microsoft *framework* in the tree (`irig106`
is a third-party C library and is irreplaceable either way - it is also deny-listed from
editing). So "Microsoft-only" means dropping Qt, not dropping C++ - which is what makes
the performance answer possible:

- The bit-serial frame-sync scanner, the derandomizer and the calibration maths are
  **never rewritten**. They become a framework-free MSVC DLL.
- Only **results** cross the boundary. `ProcessedStreamData` is flat `double` arrays;
  the worst realistic case (12 channels, one hour at 10 ms) is ~34 MB crossing **once
  per run**. The 640 MB recording is read and scanned entirely on the native side.
- The hot files already use a short, mechanical list of Qt Core types: `QString`,
  `QVector`, `QByteArray`, `QObject`, `QElapsedTimer`. Signals become `std::function`
  callbacks; `QThread`/`QMutex`/`QWaitCondition` become `std::thread`/`mutex`/
  `condition_variable`.

Use a **flat C ABI with P/Invoke**, not C++/CLI: for a boundary that passes
pointer-plus-length arrays, C++/CLI's marshalling convenience does not pay for its
toolchain oddity. Keep the structs blittable and the `Pack` hazard stays confined to the
Ch10 headers, which never leave the native side.

## The two spikes that decide this - BOTH DONE

Neither was a rewrite. Both passed; details in their own READMEs.

1. **Interop spike** - `dotnet/interop-spike/`. **Passed.** Struct layout agrees
   (72/72, 40/40, 136/136), a real stream runs through a flat C ABI, and the boundary
   costs 0.4% with marshalling at 0.005% of the run. .NET 10 and .NET Framework agree
   exactly on the results, which rules out the layout agreement being a runtime
   artifact.
2. **Chart spike** - `dotnet/chart-spike/`. **Passed, with one surprise.** Direct2D
   redraws the 12-series case in **18.1 ms against Qt's 34 ms**, and 26.4 ms against
   157 ms including the geometry build. The envelope and culling are pure arithmetic and
   ported unchanged.

   **The surprise: this project's `drawLines()`-not-`drawPath()` rule does not carry
   over.** On Qt that choice was worth 1423 ms vs 27 ms, because a non-cosmetic pen goes
   through Qt's stroker. On Direct2D the two are near-equivalent and the batched path is
   marginally *faster*. It is a Qt workaround, not a general truth - do not carry it
   across as gospel.

   **But Win2D does not build here**, so the chart-in-C# option is not yet proven:
   `Microsoft.Graphics.Win2D` needs Visual Studio's AppxPackage MSBuild tooling, which VS
   BuildTools alone does not provide. The measurement was taken against Direct2D natively
   instead - Win2D *is* Direct2D, so the rasterizer numbers hold, but the managed
   per-call overhead is unmeasured. **The WinUI 3 / Win2D path needs VS with the WinUI or
   UWP workload**, which is a real prerequisite to weigh before choosing the framework.

The chart is the real decision, and it couples to the UI framework:

- **WinUI 3 + Win2D** keeps everything in C#. Win2D is Microsoft-published over
  Direct2D, and `CanvasDrawingSession` is immediate-mode like `QPainter`. Plausibly
  *faster* than today, because the `drawLines()`-vs-`drawPath()` finding was about Qt's
  CPU stroker building an outline polygon - a problem a GPU rasterizer does not have.
  Cost: `AppWindowTitleBar.SetDragRectangles` is an approximation of the current
  `WM_NCHITTEST` frameless chrome.
- **WPF + a native Direct2D renderer** (via `D3DImage`) preserves the rendering work
  almost literally and keeps `HwndSourceHook` for the chrome, but keeps C++ in the UI
  path. Win2D is not supported on WPF.

Starting point was **WinUI 3 + Win2D**. The chart spike did not overturn that on
performance - it could not test it at all, because Win2D needs build tooling this
machine lacks. That shifts the balance slightly toward **WPF + a native Direct2D
renderer**, which is now the option with measured numbers behind it and no extra
Visual Studio workload. Decide once someone has tried Win2D on a machine with the WinUI
workload installed; the rasterizer is the same either way, so the open question is only
the managed call overhead and the tooling cost.

## Testing

- **Domain suites stay C++** on **MSTest for C++ (`CppUnitTest`)**, which ships with
  Visual Studio. `tst_framesetup`, `tst_stepdetector`, `tst_frameprocessor`,
  `tst_calibrationextractor` carry the invariants that each cost a debugging session -
  off-phase sync rejection, calibration pairing, the missed-frames double-count.
  `QCOMPARE` to `Assert::AreEqual` is close to mechanical.
- **Render tests get easier.** Direct2D and Win2D both render to an off-screen bitmap
  with no display and no platform plugin - simpler than today's
  `QT_QPA_PLATFORM=offscreen`.
- The widget-measurement suites (`tst_plotwidget`, `tst_streamconfigdialog`) are the
  genuine loss and get rewritten against XAML. **Headless WinUI for non-chart widgets is
  the one area expected to be worse than today**, and it is load-bearing for the
  `verify-ui-change` method - verify it early.

## Export

PNG is trivial (WIC). There is **no Microsoft programmatic PDF writer** -
`Windows.Data.Pdf` renders PDF, it does not create it, and XPS is not what users want.

But SVG and PDF are both just **serializations of geometry the renderer already has**,
and one `renderTo()` already produces all three formats. Emitting SVG is XML; a minimal
PDF for line art plus text is a few hundred lines of a text-based format. No Microsoft
API and no third-party library - and it keeps the one-renderer property that fixed the
silently-dropped PDF legend in v2.9.0.

## Staying current with `main`

Merge `main` **into** this branch regularly - never the other way until a phase is
genuinely ready:

```powershell
git checkout explore/dotnet-migration
git merge main
```

If a change here turns out to keep the Qt app working, move it to `main` instead. That
is not a detour; it is the rule at the top of this file.

## What is NOT being attempted

- A full C# rewrite including the decommutation loop. The assessment costs it as a
  view-layer plus test-suite rewrite with two measured performance results re-derived
  from scratch, and argues against it.
- Cross-platform anything. This application is Windows-only today and that is not
  changing.
- Replacing `irig106`.
