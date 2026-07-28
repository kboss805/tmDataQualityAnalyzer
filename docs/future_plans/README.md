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

### Keyboard shortcuts for the plot

Plot controls now live in a right-click context menu and an on-chart chip bar. The
frequent actions — toggle legend, reset the view, cycle view mode, step the time
window — have no keyboard path, which is the natural next reduction in clicks for an
analyst working through a long recording. Needs a shortcut map that doesn't collide
with the main window's, and a decision on whether shortcuts are global or active only
while the plot has focus.

Tracked as a placeholder criterion in **US4.1** (`docs/CLAUDE.md`), marked *undecided,
may be dropped* — a context menu plus chips may already be enough.

### Time-axis slider

A horizontal scrub/range control under the plot for moving the time window through a
long recording, as an alternative to the context menu's numeric time-window entry and
to wheel-zoom.

Deliberately **not** built during the plot simplification: the point of that work was
removing external chrome, so re-adding a persistent control below the chart needs a
real argument that scrubbing beats the gestures already there (wheel zoom, drag pan,
Ctrl+drag band zoom, double-click reset). Revisit once the context-menu workflow has
some mileage.

### `compile_commands.json` for MSVC (clangd IntelliSense)

clangd works against a compilation database that was generated for the old MinGW
build. Now that the toolchain is MSVC-only, a generator emitting MSVC-flavored entries
— or a `.clangd` config mapping the qmake/nmake command line — would restore accurate
cross-file IntelliSense. It doesn't affect the build, but it affects every editor
session.

---

## Shipped

Plan files deleted on landing; this table is the pointer to where each one's real
documentation lives.

| Initiative | Landed in | Documented in |
| --- | --- | --- |
| Switch the toolchain from MinGW to MSVC | PR #40 | `CLAUDE.md`, `docs/CLAUDE.md`, `scripts/env.ps1`, `deploy/build_release.ps1` |
| Move CI to a self-hosted runner | PR #41, #42 | [`docs/ci_runner.md`](../ci_runner.md), `.github/workflows/ci.yml` |
| Ship sample `.ch10` files for CI | PR #41 | as above — self-hosting delivered it; small fixtures are committed, the full-size recording lives on the runner |
| Simplify the plot window | PR #43 | `docs/CLAUDE.md` (US4.0 / US4.1), `resources/usermanual.html` §3 |
