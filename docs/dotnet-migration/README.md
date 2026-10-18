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

   **Direct3D is faster again by three orders of magnitude** - 1 to 5 **microseconds**
   per frame, against Direct2D's 18-30 ms for the same picture. Not because the GPU beat
   the GPU (Direct2D already runs on Direct3D) but because Direct2D needs **~14,424 draw
   calls** where Direct3D needs **24**. Direct2D's cost here is CPU-side tessellation and
   call overhead. With Direct3D the renderer stops being the bottleneck at all and the
   geometry build becomes ~2000x the cost of drawing it.

   **Win2D now builds and runs**, after VS Community 2026 supplied the AppxPackage
   MSBuild tooling that VS BuildTools lacks - and after calling `Bootstrap.TryInitialize`
   explicitly, because the auto-initializer kills the process silently (exit 22) with no
   indication of why. **Both are real prerequisites of the chart-in-C# option.**

## The chart decision, now that it is measured

**Every Microsoft option beats the Qt baseline.** The choice is no longer about speed.

- **Direct2D** - 18-31 ms for the 12-series redraw against Qt's 34 ms, text free via
  DirectWrite, no version trap. **The recommended starting point**: lowest risk. But note
  the margin over Qt is *not* large and the figure swings 3x between runs, because
  Direct2D's cost here is CPU-side - so "comfortably faster" overstates it.
- **Direct3D** - 1-5 us/frame. Costs you text (needs a glyph atlas, or the standard
  hybrid: D3D for the data, Direct2D/DirectWrite for the chrome) and explicit AA. **Keep
  it as the escape hatch**, not the starting point - the measurement says it is there and
  it is enormous, so its complexity can wait until something needs it.
- **Win2D** - works, but **pins you to the 1.x line with no announced successor**:
  `Microsoft.Graphics.Win2D` has 28 versions, newest 1.4.0, and **zero 2.x**, while
  WindowsAppSDK 2.5.1 ships no graphics component at all (`.DWrite` and
  `.InteractiveExperiences` exist as 2.x components; `.Graphics`, `.Canvas`, `.Direct2D`
  and `.Win2D` do not). Native Direct2D and Direct3D have no such exposure - they are
  Windows **SDK**, not Windows **App** SDK.

**That reverses the earlier starting point.** WinUI 3 + Win2D was the pick for keeping
everything in C#; the 1.x binding is now the strongest argument against it, and it is an
architectural risk rather than a performance one. **WPF + a native renderer** behind the
flat ABI is the option with measured numbers, no extra Visual Studio workload, and no
exposure to the 2.x restructuring. Its cost is keeping C++ in the UI path and
`HwndSourceHook` rather than `SetDragRectangles` for the frameless chrome - which is the
*better* side of that trade anyway, since the current `WM_NCHITTEST` code transfers
literally.

Either way: **keep the renderer behind your own interface** so the backend stays
swappable. That is the lesson all three options point at.

**Shell framework.** Avalonia and .NET MAUI were assessed against this plan in
[presentation-layer.md](presentation-layer.md): MAUI is ruled out, Avalonia fails the
Microsoft-only rule, and WPF stays - but with an **airspace** gap to close first (WPF cannot
draw over a hosted native surface, and the legend, chips and crosshair sit on the chart).

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
