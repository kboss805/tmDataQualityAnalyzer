# .NET migration

**Status: committed (2026-10-20).** The UI moves to **Avalonia 12 on .NET 10**, over the processing
core as a Qt-free native C++ DLL behind a flat C ABI, and Qt leaves the application, the core and the
tests completely.

| document | what it is |
|---|---|
| **[avalonia-plan.md](avalonia-plan.md)** | **The work plan.** Numbered steps, one PR each, ticked as they merge, with the rules every step follows and the decisions (D1-D5). Start here. |
| [presentation-layer.md](presentation-layer.md) | Why Avalonia: the shell options (WPF, Avalonia, MAUI, WinUI) assessed against this application's needs, then measured. |
| this file | The architecture the plan builds on, what the spikes answered, and the earlier decisions this one replaced. |
| [`../future_plans/dotnet-port-assessment.md`](../future_plans/dotnet-port-assessment.md) | The original costed assessment of leaving Qt, which first argued for the hybrid below. |

The exploration that led here was merged to `main` in #170; `explore/dotnet-migration` is no longer
where work happens. Every step now lands on `main`, with the Qt application building and shipping until
the plan's switch-over (plan rule 1).

## The architecture, and why it still holds

- **The performance-critical core stays native C++.** The bit-serial frame-sync scanner, the
  derandomizer and the calibration maths are never rewritten; they become a framework-free MSVC DLL.
  The full C# rewrite including the decommutation loop was costed and argued against in the
  assessment, and nothing since has changed that.
- **Only results cross the boundary.** `ProcessedStreamData` is flat `double` arrays; the worst
  realistic case (12 channels, one hour at 10 ms) is about 34 MB once per run. The 640 MB recording is
  read and scanned entirely on the native side.
- **A flat C ABI with P/Invoke, not C++/CLI.** For pointer-plus-length arrays, C++/CLI's marshalling
  convenience does not pay for its toolchain oddity. Keep the structs blittable, and the `Pack` hazard
  stays confined to the Ch10 headers, which never leave the native side.
- **The core is already Qt-free**, and `TestCoreQtFree` keeps it so. The de-Qt sweep that made it so
  ran on `main` through ordinary PRs, which is the pattern the plan keeps.
- **The core computes; the UI draws** (plan decision D3). The chart's geometry, the culling and
  per-pixel-column envelope, is built natively; Avalonia draws it with Skia (D1).

## What the spikes answered

| spike | question | answer |
|---|---|---|
| [`dotnet/interop-spike/`](../../dotnet/interop-spike/README.md) | Can the core run behind a flat C ABI driven from C#? | **Yes.** Struct layout agrees (72/72, 40/40, 136/136), a real stream runs through it, the boundary costs 0.4% of the run and marshalling 0.005%. .NET 10 and .NET Framework agree exactly. |
| [`dotnet/chart-spike/`](../../dotnet/chart-spike/README.md) | Can the chart be drawn as fast outside Qt? | **Yes.** Direct2D redraws the 12-series case in 18.1 ms against Qt's 34 ms; Direct3D needs 24 draw calls where Direct2D needs ~14,400 and runs in microseconds; Win2D works but is pinned to a 1.x line with no successor. This project's `drawLines()`-not-`drawPath()` rule is a Qt workaround, not a general truth: on Direct2D the two are near-equivalent. |
| [`dotnet/hybrid-spike/`](../../dotnet/hybrid-spike/README.md) section 1 | Is the core really Qt-free? | **Yes, at link level:** a 361 KB DLL with no Qt import, and a C# host reproduces the pre-sweep numbers exactly. |
| hybrid-spike sections 2 and 3 | Can WPF and Avalonia host the chart and draw over it, and does either perform better? | **Both can**; performance is close and decides nothing. |
| hybrid-spike section 4 | Can each framework's UI be tested on the CI runner? | **Both pass in Session 0**; Avalonia's headless platform needs no window system and drives positioned input, which WPF cannot. |
| hybrid-spike section 5 | Should the chart be drawn by Skia in Avalonia or by native Direct2D? | **Skia** (D1): faster where the chart is the bottleneck, PNG/SVG/PDF from one routine, renders in headless tests. |

## Decisions this replaced

Kept so nobody re-derives them, and so the old documents can be read in context. Each was right
under the constraints of its time.

| earlier decision | replaced by | why |
|---|---|---|
| A **Microsoft-only** stack (MSVC + .NET + XAML), so a **WPF** shell | **Avalonia** | The team knows Avalonia, and the rule was dropped (2026-10-20). Measured, Avalonia matches WPF and tests better. |
| A **native Direct2D chart** behind our own interface, Direct3D as the escape hatch | **Skia in Avalonia** over native geometry (D1) | Measured faster where it matters, no GPU interop, one renderer for every export format, testable headless. Direct3D stays the escape hatch if a future workload needs it. |
| Core tests on **MSTest for C++ (`CppUnitTest`)** | **D2, open**: doctest recommended | CppUnitTest ties the tests to Visual Studio; a vendored header framework builds the same under qmake now and MSBuild later. |
| A **hand-written PDF writer**, since Windows has none | **SkiaSharp's PDF and SVG** | The Skia chart draws to a PDF or SVG document from the same routine as the screen. |
| **Branch discipline**: C# on `explore/dotnet-migration`, everything else on `main` | **Everything on `main`** (plan rule 1) | The branch was merged in #170; new code lands beside the Qt app on `main` instead of on a long-lived branch. |

## What is not being attempted

- **A full C# rewrite including the decommutation loop.** See the architecture above.
- **Cross-platform.** The application is Windows-only, and so are its users. Avalonia and Skia would
  leave that door open, but nothing in the plan aims at it.
- **Replacing `irig106`.** It is third-party, deny-listed from editing, and stays native.
