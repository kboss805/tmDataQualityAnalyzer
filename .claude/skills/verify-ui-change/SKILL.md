---
name: verify-ui-change
description: >-
  Verify a visual or layout change to the tmDataQualityAnalyzer UI actually looks and measures
  right, before committing it. Use this WHENEVER a change touches widget geometry, sizing, spacing,
  margins, stylesheets, painting, the plot's rendering (TmChart), or anything the user describes in
  visual terms — "the buttons are too big", "make room for", "the labels overlap", "tighten the
  highlight", "that looks wrong" — and also before shipping any change to plot rendering or export.
  The 357-test suite asserts almost nothing about appearance, so a visual defect passes CI silently;
  worse, a control that is the WRONG SIZE still looks plausible in a screenshot. This skill encodes
  how to render the UI headlessly, how to measure widgets, and which of the two catches what — both
  have shipped real bugs in this project.
---

# Verify a UI change — tmDataQualityAnalyzer

**The suite will not catch you.** 357 tests pass while tick labels overlap into an unreadable
smear, or while a title-bar button is half its intended height. Appearance is deliberately not
asserted — pinning pixels would pin styling and break on every legitimate tweak.

So verification is manual, and it has **two halves that catch different things**:

| Technique | Catches | Blind to |
| --- | --- | --- |
| **Render and look** | overlap, clipping, wrong colours, missing elements, bad spacing | a control that is plausibly the wrong size |
| **Measure the widget** | wrong size/position, a defeated `setFixedSize`, stale layout | anything about how it actually looks |

Do both. Each has shipped a real bug here that the other would have caught.

## 1. Render it and look

Everything renders headlessly — no need to run the app.

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
```

- **A whole widget:** `widget->grab()` → `QPixmap::save(path)`.
- **Part of it, magnified** (for small chrome like buttons — a 1:1 grab is too small to judge):
  `w->grab(QRect(x, y, w, h)).scaled(w*4, h*4, Qt::IgnoreAspectRatio, Qt::FastTransformation)`
- **The plot, with data:** `PlotWidget::exportImage(path)` — the same path PNG/SVG/PDF export uses,
  so it also verifies exports.

Then **actually look at the image** (read it back). Do not infer from the numbers.

### Two traps when rendering

- **A widget that is resized but never shown gets no layout.** `resize()` alone leaves child
  geometry unset, so the grab is tiny or empty. Call `show()` + `QApplication::processEvents()`
  first. (`TmChart` is immune — its layout is lazy for exactly this reason — but `PlotWidget` and
  `MainView` are not.)
- **Offscreen has no font rendering**, so text renders as empty boxes. That is expected and is
  *not* a bug to chase. Judge layout, spacing and geometry from these renders; judge text
  appearance from a real screenshot.

## 2. Measure the widget

Rendering cannot tell you a 30x13 button should have been 30x26 — it just looks like a slightly
squat button. Dump the numbers:

```cpp
qDebug() << b->objectName() << b->geometry()
         << "min"  << b->minimumSize() << "max" << b->maximumSize()
         << "hint" << b->sizeHint();
```

`qDebug()` is swallowed by the multi-`qExec` test harness — write to a file (`QFile` +
`QTextStream`) if you are probing from a test.

**Read `min` and `max`, not just the geometry.** `setFixedSize()` sets both; if `min` is `0x0` when
you expected your fixed size, something overwrote it — see the trap below.

## The QSS-vs-code sizing trap

**A stylesheet `min-width`/`min-height` REPLACES the minimum that `setFixedSize()` installed**,
leaving the maximum intact — so the widget collapses to its `sizeHint`. This shipped a bug in
PR #60: buttons meant to be 30x26 were 27x26 and 30x13, and the render looked entirely reasonable.

**Rule: style for paint (colour, border, radius), size in code.** Never set both.

## Removing the probe

Probe code is temporary. Before committing:

```bash
git diff <test file>          # must show no probe residue
git checkout <test file>      # simplest, if the probe was the only change there
```

Leaving a stray blank line is enough to put an unrelated hunk in the PR.

## Judgement calls belong to the user

Sizes, spacing and colour are the user's call, not yours. Verify the change does what was asked and
report *measured* before/after numbers; do not silently substitute your own taste. If a requested
value looks wrong to you (e.g. it stops controls touching but does not fully clear them), implement
what was asked, say what the alternative would cost, and let them decide.

## Worked examples from this project

- **Colliding tick labels** — 357 tests green; rendering a real two-axis plot showed
  `DDD:HH:MM:SS` labels smeared together at the default tick count. Fixed by decluttering labels
  (tick marks unaffected). *Only rendering could have caught this.*
- **Title-bar buttons 30x13** — the render looked like a plausible compact pill and was nearly
  shipped twice; `min=0x0` in the measurement named the cause immediately. *Only measuring could
  have caught this.*
- **`plotArea()` empty** — `TmChart` computed layout only in `resizeEvent`, which a never-shown
  widget never receives, collapsing every coordinate to zero. Caught by writing the transform tests
  first.
