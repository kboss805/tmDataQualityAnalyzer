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

### [Comprehensive, task-oriented user manual](comprehensive-user-manual.md)

The manual is organised by UI surface, which answers "what does this control do" but not
"I have an AGC recording and a step file - what do I actually do?" Calibration is the
sharpest case: the most procedural workflow in the product, five nested dialogs deep, gets
six sentences and no figures.

Proposal is to add task walkthroughs in front of the existing reference sections rather
than replacing them, with step calibration as the centrepiece - written against the real
failure modes (turn-on transient, operator down-ramp, inverted polarity, per-channel
fallback), not just the happy path.

**A second gap, arguably wider:** the two gear setup dialogs - everything a user must get
right *before* pressing Process - get three nested bullets, ~90 words. The manual never
mentions Derandomize, Bits Per Frame, Polarity, voltage range, dB/V or channels-per-receiver
at all, and unlike calibration this is the path every user takes. Needs no new figures; the
dialogs are already in the capture list.

**Blocked on screenshots** - **24** of them, enumerated by filename so they can be captured
in one sitting. The list was audited against every `QDialog` in the codebase rather than
written from memory, which caught several whole surfaces the first draft had missed:
*Customize Plot Series*, the hamburger menu, *Set Time Window*, error reporting and the light
theme. 23 can be shot against v2.9.3 today; the installer-components figure is blocked on
building that feature, so **build the installer change first** or accept one more capture
later.

The figure-storage question is **decided**. Measured first: the manual is stored
*uncompressed* in the exe (verified in the shipped binary), so 1804 KB of the 2634 KB
executable - **68%** - is documentation.

The shape is a **base manual that is always present, plus an optional add-on**: today's
manual stays a compiled-in resource, and the new full manual ships as an installed file
offered as an installer component, with the portable ZIP carrying both. The user therefore
always has a manual, and that guarantee is *structural* - a compiled-in resource cannot go
missing - so there is no "manual not found" state at all. The benefit is editorial freedom:
the full manual's figure budget is no longer bounded by what is tolerable to compile into
every copy of the program.

The risk is two artifacts drifting, so both must be emitted from one source by
`build_manual.ps1` (the full manual is the base plus walkthroughs), and
`check_release_consistency.ps1` must assert version markers in **both** - it checks only one
manual today.

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
| Replace QCustomPlot with first-party charting | PR #55, #56, #57 | `include/view/tmchart.h`, `src/view/tmchart.cpp`, `tests/tst_tmchart.cpp`. Engine -> swap -> delete, so each step was revertible. Fixed a latent bug on the way: PDF export silently dropped the legend |
| Keyboard shortcuts for the plot | PR #54 | `docs/CLAUDE.md` (US4.1), `resources/usermanual.html` section 8. Widget-scoped in `PlotWidget::keyPressEvent`, not application-wide |
| Gate warnings in test code too | PR #50 | `.github/workflows/ci.yml` - one gate over both build logs |
| MSVC-flavored clangd config | PR #40 (found already done) | `docs/CLAUDE.md` -> clangd / IntelliSense; `scripts/gen_compile_flags.py`. Verified with `clangd --check` across QCustomPlot / Win32 / irig106 / test TUs: 0 errors |
| Speed up CI with a parallel build (`jom`) | PR #48 | `scripts/env.ps1` (`TMDQ_MAKE`), [`../ci_runner.md`](../ci_runner.md) - CI run 286s -> 93s |
| Simplify the plot window | PR #43 | `docs/CLAUDE.md` (US4.0 / US4.1), `resources/usermanual.html` §3 |
| Make the remaining plot shortcuts visible in the context menu | PR #71 | `PlotWidget::buildContextMenu`. `(V)` in the View Mode submenu title (Qt will not draw a shortcut on a submenu); new top-level **Reset View** carrying `R`, which is also the menu's only single-action equivalent of the on-chart Reset view chip. Both were tooltips, which discover nothing |
| Hide a Y axis when nothing is plotted against it | PR #68 | `include/view/tmchart.h` (`setLeftAxisVisible`/`setRightAxisVisible`), `PlotViewModel::hasVisibleLeftAxisSeries`/`hasVisibleRightAxisSeries`, `PlotWidget::updateAxisVisibility`. Keys on **visible** series. Rendering the reclaimed layout also caught the first X tick label clipping off the left edge |
