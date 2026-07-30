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

### Replace QCustomPlot with a first-party plotting implementation

QCustomPlot (`lib/qcustomplot/`, ~1.6 MB vendored) is third-party and protected — it
can never be edited, so every rendering behaviour we want has to be adapted around it in
app code. Replacing it with our own Qt-native plotting would remove that constraint and
the vendored dependency.

**Scope is smaller than it looks.** Only four files touch QCustomPlot or `QCP*` types:

| File | Role |
| --- | --- |
| `src/view/plotwidget.cpp` | all chart construction, axes, gestures, overlays, export |
| `include/view/plotwidget.h` | member declarations |
| `src/viewmodel/plotviewmodel.cpp` | incidental (colors/ranges), not rendering |
| `tests/tst_plotwidget.cpp` | asserts against widget/graph state |

Everything else already goes through `PlotViewModel`, which is rendering-agnostic — the
MVVM split means the ViewModel and every other suite are unaffected.

**What makes this genuinely hard** is not drawing lines, it's matching what QCustomPlot
gives us for free and US4.0/US4.1 now depend on: dual Y axes with independent
auto/manual ranging, wheel zoom and drag pan on one axis only, the draggable translucent
legend overlay composited into PNG/SVG/PDF exports at its placed position, the crosshair
and rubber-band items that deliberately are not widgets so they stay out of exports, and
`rpQueuedReplot` performance with 48+ SNR series over long recordings. Export in three
formats is the part most likely to be underestimated.

Worth deciding first whether the goal is removing a *protected* dependency or gaining
*capability* — if it's the former, note the vendored library is stable and has cost us
nothing but the no-edit rule, so the honest comparison is a large rewrite against a
constraint we have so far always been able to work around.

### Upgrade to Qt 6.11.x

Currently pinned to **6.10.3**, single-sourced in `scripts/env.ps1` (`QT_VERSION`) and
mirrored by `QT_VERSION` in `.github/workflows/ci.yml`, which must match the kit
installed on the self-hosted runner.

**6.11.1 is installed and pre-validated.** `C:\Qt\6.11.1\msvc2022_64` is present
(`Qt6Core.dll` 6.11.1.0), alongside mingw/android/wasm kits the project doesn't use.

A trial build against it on 2026-07-30, driven purely by `QT_VERSION=6.11.1` in the
environment with no source changes, came out **completely clean**:

| | Result |
| --- | --- |
| App build (shadow dir, so 6.10.3 tree untouched) | 0 warnings, 0 errors |
| Test build | 0 warnings |
| Full suite | **341 passed / 0 failed / 1 skipped** |

That retires the main risk. A minor Qt bump usually brings new deprecation warnings, and
since PR #50 those fail CI for app *and* test code — here there are none. Because
`env.ps1` derives `QTDIR` from `QT_VERSION`, the whole trial needed no edits, which is
also the rollback story: revert one constant.

**The work:** bump `QT_VERSION` in `env.ps1`, match it in `ci.yml`, re-run
`py scripts/gen_compile_flags.py` (its Qt paths are version-pinned and absolute, so
clangd otherwise keeps resolving 6.10.3 headers). Then rebuild clean and confirm the suite
and the zero-warning gate. Also re-run `deploy/build_release.ps1` and re-verify
signatures: `windeployqt` comes from the kit, so the packaged Qt DLLs all change — this is
effectively a re-release of the binary, not a config tweak.

Keep both runners in step: CI routes to the self-hosted runner whenever it is online, so
bumping `ci.yml` before the runner's kit is upgraded breaks the self-hosted path while
the hosted fallback (which provisions Qt per run) still passes — an asymmetry that is
easy to misread.

---

## Shipped

Plan files deleted on landing; this table is the pointer to where each one's real
documentation lives.

| Initiative | Landed in | Documented in |
| --- | --- | --- |
| Switch the toolchain from MinGW to MSVC | PR #40 | `CLAUDE.md`, `docs/CLAUDE.md`, `scripts/env.ps1`, `deploy/build_release.ps1` |
| Move CI to a self-hosted runner | PR #41, #42 | [`docs/ci_runner.md`](../ci_runner.md), `.github/workflows/ci.yml` |
| Ship sample `.ch10` files for CI | PR #41 | as above — self-hosting delivered it; small fixtures are committed, the full-size recording lives on the runner |
| Gate warnings in test code too | PR #50 | `.github/workflows/ci.yml` - one gate over both build logs |
| MSVC-flavored clangd config | PR #40 (found already done) | `docs/CLAUDE.md` -> clangd / IntelliSense; `scripts/gen_compile_flags.py`. Verified with `clangd --check` across QCustomPlot / Win32 / irig106 / test TUs: 0 errors |
| Speed up CI with a parallel build (`jom`) | PR #48 | `scripts/env.ps1` (`TMDQ_MAKE`), [`../ci_runner.md`](../ci_runner.md) - CI run 286s -> 93s |
| Simplify the plot window | PR #43 | `docs/CLAUDE.md` (US4.0 / US4.1), `resources/usermanual.html` §3 |
