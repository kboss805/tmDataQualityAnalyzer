---
name: verify-ui-change
description: >-
  Verify a visual or layout change to the tmDataQualityAnalyzer UI actually looks and measures
  right, before committing it. Use this WHENEVER a change touches control sizes, spacing, margins,
  styles, the themes, the plot's drawing (PlotChart) or export, the legend, or anything the user
  describes in visual terms - "the buttons are too big", "make room for", "the labels overlap",
  "tighten the highlight", "that looks wrong". The app suite asserts some appearance (frames read back
  pixel by pixel) but not most of it, so a visual defect can pass CI; worse, a control that is the
  WRONG SIZE still looks plausible in a picture. This skill encodes how to render the UI headlessly,
  how to measure controls, and which of the two catches what - both have shipped real bugs here.
---

# Verify a UI change — tmDataQualityAnalyzer

Verification has **two halves that catch different things**:

| Technique | Catches | Blind to |
| --- | --- | --- |
| **Render and look** | overlap, clipping, wrong colours, missing elements, bad spacing | a control that is plausibly the wrong size |
| **Measure the control** | wrong size or position, a style overriding a size, stale layout | anything about how it actually looks |

Do both. Each has caught a real defect here that the other missed.

## 1. Render it and look

The app's tests run on Avalonia's headless platform **drawing through Skia** (`TestAppBuilder`:
`UseHeadlessDrawing = false`), so a frame is the real rendering, text included — no need to run the
app. In an `[AvaloniaFact]`:

```csharp
var window = new Window { Width = 900, Height = 600, Content = control };
window.Show();
Dispatcher.UIThread.RunJobs();                 // layout and render before capturing
using var frame = Frames.Capture(window);      // window.CaptureRenderedFrame()
frame.Save(@"C:\tmp\look.png");                // then READ the image back and look at it
```

- **The plot, with data:** `PlotImageExport` writes PNG, SVG and PDF through the same routine the
  screen uses, so a render of an export also verifies export.
- **Small chrome** (a chip, a title-bar button): capture, then crop and scale the region up before
  judging; a 1:1 render of a 30 px button is too small to read.
- **The manual's figures** are this technique already: `ManualFiguresTests` sets up each scene,
  checks its caption's claims and renders it (`eng\build_manual.ps1` keeps them).

Then **actually look at the image**. Do not infer from the numbers.

### Traps when rendering

- **No layout until it is shown and the dispatcher has run.** A control that was never shown, or
  shown without `RunJobs()`, has empty bounds and an empty frame.
- **Popups are not drawn into their window's frame** (context menus, the hamburger flyout, tooltips):
  draw the menu in a window of its own, as `ManualFiguresTests` does.
- **Subpixel text antialiasing puts coloured fringes on black text**, which a colour test reads as the
  wrong colour. Tests that read text colour set `TextOptions.SetTextRenderingMode(window,
  TextRenderingMode.Antialias)`; the app keeps the default.
- **The theme is the application's:** set it (`((App)Application.Current!).ApplyTheme(dark)`) before rendering a scene that depends
  on it, and render both themes when the change is a colour.

## 2. Measure the control

A render cannot tell you a 30x13 button should have been 30x26. Read the numbers after layout:

```csharp
var b = window.FindControl<Button>("MaximizeButton")!;
Console.WriteLine($"{b.Bounds} desired {b.DesiredSize} min {b.MinWidth}x{b.MinHeight} max {b.MaxWidth}x{b.MaxHeight}");
Rect inWindow = Frames.InWindow(b, window);     // its bounds in window coordinates
```

**Read the constraints, not just the bounds.** A style setter or a template can override a size
set on the control; when `Bounds` disagree with what you set, look for a style that matches it,
including styles leaking into a template's parts (`/template/` selectors).

## Probe code

A probe is temporary. Before committing, `git diff` the test file and remove every line of it —
a stray blank line puts an unrelated hunk in the PR. A measurement worth keeping becomes an
assertion instead (the legend's 16 px scroll-bar gutter is one, found by measuring).

## Judgement calls belong to the user

Sizes, spacing and colour are the user's call. Verify the change does what was asked and report
*measured* before/after numbers; do not substitute your own taste. If a requested value looks wrong,
implement it, say what the alternative would cost, and let them decide.

## Worked examples from this project

- **The legend's scroll bar covered the labels** — the first version reserved 12 px; a test
  measuring the rendered rows found Fluent's bar is 16 px. *Only measuring caught it.*
- **The colour dialog clipped its own slider**, and **the clip boxes read "1000."** — both found by
  looking at a render, not by any test. *Only looking caught them.*
- **Avalonia drew its own caption buttons over the custom title bar** — found by capturing the
  running window; `WindowDecorations="BorderOnly"` removed them.
- **(Qt era) colliding tick labels and 30x13 title-bar buttons** — the same two lessons, from the
  application this one replaced: render to see overlap, measure to see size.
