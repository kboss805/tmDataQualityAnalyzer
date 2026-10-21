# Presentation layer: Avalonia or .NET MAUI?

Assessed 2026-10-18 against the plan already recorded in [README.md](README.md): a native MSVC
core behind a flat C ABI, a C# shell, and the chart drawn by a **native** Direct2D renderer with
Direct3D as the escape hatch, behind our own interface. That README picked **WPF** for the shell.
The question here is whether Avalonia or MAUI would be a better shell than WPF.

## Short answer

- **MAUI: no.** On Windows it is WinUI underneath a mobile-first abstraction. It adds a layer
  between us and WinUI, pins an old Windows App SDK we may not upgrade, and has no first-class way
  to host our native renderer. It earns its keep on phones, and this product does not run on
  phones. If WinUI is wanted, use WinUI directly.
- **Avalonia: the strongest technical fit, but it fails the stated constraint.** It is not a
  Microsoft technology. If "Microsoft-only" is a hard rule, it is out. If the real goal is
  "C#, XAML, long-lived, and fast", it deserves a spike, because it solves the two problems the
  WPF plan has not solved yet: drawing UI **over** a native GPU surface, and headless UI testing.
- **WPF stays the recommendation under the current rule**, with one issue that this assessment
  surfaced and the README did not: **airspace**. See below, and the spike proposed at the end.

## What this application needs from a shell

These come from the code and the history on `main`, not from a generic checklist:

| need | why it matters here |
|---|---|
| Host a native D3D11 / Direct2D surface | The chart is the product. The renderer is native, behind the ABI. |
| Draw UI **over** that surface | The movable legend, the on-chart chip bar, the crosshair readout and the zoom band all sit on top of the plot today. |
| Desktop-shaped UI | Frameless window with custom chrome (the `WM_NCHITTEST` code), menus, modal dialogs, a dockable sidebar, right-click menus, drag-and-drop of files. |
| Headless, measurable UI tests | The `verify-ui-change` method and suites like `tst_plotwidget` measure widgets rather than look at them. The README already flags headless UI testing as "expected to be worse than today". |
| Unpackaged deployment | The installer **and** the portable ZIP. A runtime that must be installed separately breaks the ZIP. |
| No version trap | The Win2D spike found a library pinned to a 1.x line with no successor. That is the risk class to avoid. |
| Windows only | Every user is on Windows. Cross-platform reach is worth little. |

## .NET MAUI

**What it is on Windows:** a WinUI app with MAUI's cross-platform controls and handlers on top.
The current stable MAUI pins Windows App SDK **1.8.x**, the Windows App SDK itself has shipped
**2.2 and 2.3** stable, and manually upgrading the Windows App SDK under MAUI is
[documented as not supported](https://maboasoft.com/blog/maui-windows-target-migration-scope/).
The .NET 11 previews still pin 1.8.

Against the needs above:

- **Native surface:** no MAUI-level control for it. You drop into the WinUI handler and use a
  `SwapChainPanel`, which is WinUI code reached through an extra layer.
- **Version trap:** this is the Win2D lesson again, one level up. Our Windows App SDK version
  would be decided by the MAUI workload's release schedule, not by us.
- **Desktop shape:** keyboard navigation, multi-window, drag and drop, file access and printing
  are [separate workstreams](https://maboasoft.com/blog/maui-windows-target-migration-scope/)
  rather than things a MAUI app gets for free. Every one of them matters here.
- **Deployment:** it carries the Windows App SDK runtime dependency. The Win2D spike already ran
  into this: the Windows App Runtime had to be installed, and then bootstrapped explicitly in code.
- **Where its attention goes:** its recent churn is mobile (the Android 16 edge-to-edge work in
  .NET 10, per [this discussion](https://github.com/dotnet/maui/discussions/34171)), which tells
  you what the framework is for.

MAUI is the right tool for one codebase on phones, tablets and desktop. For a Windows-only
desktop tool it is all cost.

## Avalonia

**What it is:** an open-source (MIT) XAML framework from AvaloniaUI OÜ that draws every pixel
itself (Skia) rather than wrapping native controls. **12.0 shipped in April 2026**, **12.1 in
July 2026**, and it requires .NET 8 or later
([12.1 notes](https://avaloniaui.net/blog/release-12-1), [12.0 preview](https://avaloniaui.net/blog/avalonia-12.0-preview-1/)).
XAML and the MVVM patterns are close to WPF's, so the skills transfer.

Against the needs above:

- **Native surface, and drawing over it:** its strongest point. `ICompositionGpuInterop` imports
  a D3D11 shared texture into Avalonia's own compositor, synchronised with a keyed mutex
  ([API](https://api-docs.avaloniaui.net/docs/T_Avalonia_Rendering_Composition_ICompositionGpuInterop),
  and HelixToolkit uses exactly this path ([package](https://www.nuget.org/packages/HelixToolkit.Nex.Avalonia/))).
  The chart becomes an ordinary visual, so the legend, chips and crosshair can stay XAML on top
  of it. This is the property WPF lacks (below).
- **Headless tests:** Avalonia has a headless platform for UI tests. That is the README's one
  "expected to be worse than today" area. **Not yet verified here.**
- **Desktop shape:** a full desktop framework. Custom window chrome is supported, but our
  `WM_NCHITTEST` code would **not** transfer literally the way it does to WPF. **To verify:**
  Windows 11 snap layouts on a custom title bar.
- **Deployment:** self-contained .NET, no extra Windows runtime. The portable ZIP keeps working.
- **Version trap:** it has its own release line, and the company is smaller than Microsoft.

**Why it fails the rule:** it is not Microsoft. The framework stays MIT, but the professional
tooling is licensed. The free Community tier for that tooling is limited to organisations with no
more than **five developers**, and to non-enterprises (more than 250 users/PCs or €1M revenue
counts as enterprise). Larger teams need a paid per-user licence
([terms](https://avaloniaui.net/legal), [announcement](https://avaloniaui.net/blog/building-a-sustainable-future-for-avalonia)).
**Check which of the Visual Studio extension's features fall under that tooling licence before
budgeting.**

## WPF, revisited: the airspace problem

WPF remains Microsoft's, mature and fully supported, with improvements planned through .NET 11
([summary of Build 2026](https://developer.mescius.com/blogs/microsoft-recommitted-winui-what-build-2026-means-for-windows-developers),
a vendor blog, so read it as a summary). But the README's plan has a gap: **WPF cannot draw over a
child HWND.** It has two ways to host a native surface:

- **`HwndHost`:** the simple one, and the chart becomes a separate window that WPF cannot paint on
  ("airspace"). The legend, chips, crosshair and zoom band could not be WPF elements. They would
  have to move into the native renderer, which is possible since they are simple shapes and text,
  but it moves UI into C++.
- **`D3DImage`:** composes properly with WPF content on top, but it speaks Direct3D 9. A D3D11
  renderer reaches it through a shared surface with D3D9Ex. Microsoft has published a helper for
  this (the `Microsoft.Wpf.Interop.DirectX` sample library), but it is old, it adds a copy and a
  synchronisation point per frame, and **nobody here had measured it** (since done: see
  "Measured since" below).

The airspace issue is the real comparison: **Avalonia (shared texture) and WinUI (`SwapChainPanel`)
both compose XAML over native GPU content natively, and WPF does not.**

## Measured since: hosting the chart, and testing the UI, in WPF and Avalonia (2026-10-20)

Both halves of the GPU-hosting question were spiked; details and every number are in
[`dotnet/hybrid-spike/README.md`](../../dotnet/hybrid-spike/README.md), sections 2 and 3.

- **WPF `D3DImage` works and composes XAML over the chart**, with no airspace problem. The path costs
  0.2 ms per frame. It needs a D3D9Ex device and a CPU-side GPU fence: without the fence it tore in
  one run of four.
- **Avalonia's `ICompositionGpuInterop` works too.** It imports the D3D11 texture directly, with a
  keyed mutex that synchronises on the GPU, so it needs no fence and showed 0 tears in every run.
  The host must rotate at least two textures, or the frame rate halves.
- **Performance is close; neither framework is ruled out by it.** On a 180 Hz display:
  - **WPF leads:**
    - rendering on the UI thread: 152 against 90 frames/s. Avalonia's frame clock drops to every
      second refresh once the render overruns the 5.6 ms budget;
    - the heaviest load: 40 against 32, from a longer GPU wait under Avalonia, not yet isolated;
    - startup: about 190 ms faster.
  - **Avalonia leads:**
    - rendering on a worker thread: 162 against 119;
    - unfenced UI-thread rendering: 147, and safe;
    - a smaller self-contained deployment: 104 against 140 MB.
  - **No difference:** an empty chart (both at the refresh rate) and memory.

- **Headless UI tests favour Avalonia** (section 4 of the same README). The same five tests are
  ported from `tst_plotwidget` and the PR #60 sizing trap, written in both frameworks, and each test
  catches all seven of its mutants in both.
  - `Avalonia.Headless` needs no window system (it creates no HWND, tested). It drives
    keyboard and pointer input through the real pipeline at coordinates. It renders real text
    through Skia, which Qt's offscreen platform cannot.
  - WPF can measure, render (`RenderTargetBitmap`) and raise key events. But it needs a hidden
    window per test and a hand-written STA harness, and it has no supported way to inject pointer
    input at a position.
  - Both passed in Session 0 with no desktop, as SYSTEM in a clean Windows Sandbox (24 of 24 tests
    across the two suites). The runner's own account, NETWORK SERVICE, was not tried. Avalonia
    does not depend on the session at all; WPF does, and passed.
  - Neither can render the native chart in a test, so the chart host needs a seam either way.

So the measured performance does not decide between them, and testability leans to Avalonia. The
deciding questions are still:

- the "Microsoft-only" rule;
- the tooling licence;
- custom chrome with snap layouts, which remains unverified.

## Recommendation

1. **Rule out MAUI.**
2. **Keep WPF as the plan while "Microsoft-only" is a hard rule**, but close the airspace gap
   before anything is built on top of it, with a short spike: the existing D3D11 renderer from
   `dotnet/chart-spike/` hosted in WPF through `D3DImage`, with a WPF legend over it. Measure the
   frame time and look for tearing. If it fails, the fallback is `HwndHost` with the overlays drawn
   natively, which is known to work.
3. **Consider Avalonia only if the rule is relaxed** to "C# and XAML". If it is, run the same
   spike in Avalonia (the shared-texture import), plus its headless test platform against one of
   our measured widget tests, before choosing. It would likely win on both, but that should be
   measured, the way the chart decision was.
4. **WinUI directly, not through MAUI**, remains the one Microsoft option with native
   composition over the chart. Its cost is the Windows App SDK versioning the README already
   weighed. If the WPF `D3DImage` spike disappoints, WinUI is the next Microsoft option to test,
   ahead of anything MAUI offers.

## Facts that could not be verified here

- Avalonia custom chrome with Windows 11 snap layouts.
- Which parts of the Avalonia Visual Studio experience need the paid tooling licence.
- WPF UI tests on the CI runner itself, as NETWORK SERVICE (they pass in Session 0 as SYSTEM in a
  clean Sandbox).

(The cost of WPF `D3DImage` with a D3D11 shared surface, its Avalonia equivalent, and Avalonia's
headless test platform against this codebase's measured-widget style of test were measured
afterwards: see the section above.)

Dates and version numbers above come from the linked pages as of 2026-10-18.
