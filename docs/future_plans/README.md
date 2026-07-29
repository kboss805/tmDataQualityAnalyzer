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

### Speed up CI with a parallel build (`jom`)

A CI run is about **6 minutes**, and roughly **84% of it is compiling**. Measured on
run 30381725647 (self-hosted, v2.8.0):

| Step | Time |
| --- | --- |
| Build tests (Debug, in-source) | 195 s |
| Build app (Debug) | 107 s |
| Run full test suite | 36 s |
| Checkout, toolchain, warning gate, upload | ~12 s |

Two things make the compile slower than the hardware requires:

- **`nmake` is serial.** It has no `-j`, so it uses one core no matter what the box
  has. `jom` is Qt's drop-in parallel replacement — `scripts/run_release.ps1` already
  prefers it when present (`if (Get-Command jom …)`), and `.claude/skills/build-and-test`
  documents it for local use. `.github/workflows/ci.yml` still calls `nmake` in both
  build steps.
- **Every run is a cold rebuild.** `actions/checkout` defaults to `clean: true`, which
  wipes `build/` and `tests/debug/`, so nothing is reused between runs. That is
  deliberate — stale objects masking a failure is a worse problem than a slow build,
  and it is what restores the committed `.ch10` fixtures each run — so the fix is to
  make the cold build *faster*, not to make it warm.

**Plumbing is DONE** (`env.ps1` exports `TMDQ_MAKE`, all four CI build sites and
`run_release.ps1` invoke it, `nmake` fallback intact for the hosted runner). Until
`jom` is actually installed this is a no-op and CI still uses `nmake`.

**What's left:**

1. Install `jom` on the self-hosted runner, somewhere `NETWORK SERVICE` can read —
   `env.ps1` probes `<Qt>\Tools\jom\jom.exe` and
   `<Qt>\Tools\QtCreator\bin\jom\jom.exe`, or set `TMDQ_JOM`. See
   [`../ci_runner.md`](../ci_runner.md).
2. **Confirm the zero-warning gate still holds.** It greps `build\app_build.log` for
   `warning [A-Z]\d+`. Parallel jobs interleave output, so verify a warning is still
   matched on its own line and that a compile error still fails the step rather than
   being lost in the interleaving. Worth deliberately introducing a warning once to
   prove the gate still fires.
3. **Measure.** The box has 28 cores, but `/bigobj` translation units like
   QCustomPlot's may dominate regardless of job count, so the real gain is unknown
   until timed. Compare against the 195 s / 107 s baseline above.

Note the gate only ever covered the **app** build — the test build's output isn't
teed to a log — so a warning introduced in test code has never been gated. Worth
deciding whether that's intentional while in here.

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
