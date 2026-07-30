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
