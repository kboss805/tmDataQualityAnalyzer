# Future plans

Scratch space for work that is scoped but not yet started, before it graduates into
`docs/IMPROVEMENT_PLAN.md` or a user story in `docs/CLAUDE.md`. Anything substantial
gets its own file here plus a paragraph in this index; smaller items can live as a
section below until they earn one.

**Nothing here is committed to a version or a schedule.**

When an initiative ships, **delete its plan file** and add a row to the table at the
bottom. The shipped behavior is documented by the code, the user manual, and whatever
docs the work updated; a stale plan describing a *proposal* that no longer matches
what was built is worse than no plan at all.

## Open items

### Replace QCustomPlot with a first-party plotting implementation

**Goal (decided): remove the protected third-party dependency, keeping behaviour
identical.** No user-visible change is intended - this is a swap, not a redesign, so
every existing US4.0/US4.1 criterion must still hold afterwards.

Landing incrementally so each step is revertible, rather than as one unreviewable
diff.

#### Step 1 - the chart engine (DONE, PR #55)

`TmChart` (`include/view/tmchart.h`, `src/view/tmchart.cpp`) is a self-contained
QWidget line chart covering exactly the QCustomPlot surface this app used, which an
inventory showed to be small: series data/pen/visibility/name, three axis ranges plus
labels, pixel<->coordinate transforms, an N-evenly-spaced-tick time axis, a crosshair
and a zoom band, four mouse signals, and render-to-painter.

Notably **simpler** than what it replaces in two places: the crosshair and zoom band
are painted rather than being scene items, and one `renderTo()` serves screen, PNG,
SVG and PDF alike - so "what you see" and "what you export" cannot drift apart.

Not yet wired in: `PlotWidget` still uses QCustomPlot, so this step changed no
behaviour. Covered by `TestTmChart` (11 cases).

#### Step 2 - swap `PlotWidget` over (NEXT)

Replace the `QCustomPlot* m_plot` member and its ~40 call sites with `TmChart`. The
pieces needing care, in rough order of risk:

1. **Export.** Three formats, and the draggable legend overlay is composited in at its
   placed position. The legend is already a plain child QWidget, so it is unaffected -
   only the chart-rendering half changes, and `renderTo()` is a direct substitute for
   `toPixmap`/`toPainter`/`savePdf`.
2. **Range round-trip.** QCustomPlot's `rangeChanged` currently feeds
   `handlePlotXRangeChanged`, guarded by `m_updating_from_vm` against feedback loops.
   `TmChart::xRangeChangedByUser` is emitted *only* for user gestures, so that guard
   may become unnecessary - verify rather than assume.
3. **The graph<->series mapping.** `removeSeries()` shifts later indices down, so the
   `PlotSeriesData -> chart index` map must be rebuilt on removal, not cached.
4. **`tst_plotwidget`** asserts against QCP graph state in places; those assertions
   move to the equivalent `TmChart` queries. This is the part where the safety net is
   itself under change, so port assertion-by-assertion rather than rewriting wholesale.

#### Step 3 - delete the dependency

Remove `lib/qcustomplot/`, its `.pro` entries, the `/bigobj` flag it needed, and the
protected-file rules naming it in `CLAUDE.md`, `docs/CLAUDE.md` and the
`build-and-test` skill.

---

## Shipped

Plan files deleted on landing; this table is the pointer to where each one's real
documentation lives.

| Initiative | Landed in | Documented in |
| --- | --- | --- |
| Switch the toolchain from MinGW to MSVC | PR #40 | `CLAUDE.md`, `docs/CLAUDE.md`, `scripts/env.ps1`, `deploy/build_release.ps1` |
| Move CI to a self-hosted runner | PR #41, #42 | [`docs/ci_runner.md`](../ci_runner.md), `.github/workflows/ci.yml` |
| Ship sample `.ch10` files for CI | PR #41 | as above — self-hosting delivered it; small fixtures are committed, the full-size recording lives on the runner |
| Upgrade to Qt 6.11.1 | PR #53 | `scripts/env.ps1` + `.github/workflows/ci.yml` (`QT_VERSION`). Clean: 0 warnings, 341/0/1. NOTE the shipped v2.8.0 binaries were built on 6.10.3, so the next release needs a NEW version number - do not repackage 2.8.0 |
| Keyboard shortcuts for the plot | PR #54 | `docs/CLAUDE.md` (US4.1), `resources/usermanual.html` section 8. Widget-scoped in `PlotWidget::keyPressEvent`, not application-wide |
| Gate warnings in test code too | PR #50 | `.github/workflows/ci.yml` - one gate over both build logs |
| MSVC-flavored clangd config | PR #40 (found already done) | `docs/CLAUDE.md` -> clangd / IntelliSense; `scripts/gen_compile_flags.py`. Verified with `clangd --check` across QCustomPlot / Win32 / irig106 / test TUs: 0 errors |
| Speed up CI with a parallel build (`jom`) | PR #48 | `scripts/env.ps1` (`TMDQ_MAKE`), [`../ci_runner.md`](../ci_runner.md) - CI run 286s -> 93s |
| Simplify the plot window | PR #43 | `docs/CLAUDE.md` (US4.0 / US4.1), `resources/usermanual.html` §3 |
