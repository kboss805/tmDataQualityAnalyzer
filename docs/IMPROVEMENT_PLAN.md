# tmDataQualityAnalyzer — Improvement Plan

A phased roadmap for the next round of work. Ordered as agreed. Each phase is
independently shippable (own branch → PR → merge), so the list can be paused or
reprioritized at any point.

Effort key: **S** ≈ ½–1 day · **M** ≈ 2–4 days · **L** ≈ 1–2 weeks.

**Execution order (decided): infrastructure and refactors first, features last.**
The phases are numbered by topic, not execution order. Actual build order:

> **2 (CI) → 3 (warning-clean) → 4 (id-based API) → 5 (CSV schema) →
> 7 (fast tests) → 1 (multi-file).**

Rationale: land the low-risk infra (CI + warning-clean build) first so it guards
the refactors, do the internal refactors while the surface area is small, and
save the large user-facing feature (multi-file input) for last.

**Phase 8 (performance spike) is optional and slots in any time after Phase 3** —
it's a measurement exercise whose results decide whether any compiler/optimization
work is worth doing at all.

---

## Phase 1 — Multi-file input (SNR file + separate frame-sync file)

**Goal:** let a session draw from more than one `.ch10` file — e.g. Receiver SNR
in one file and Frame Sync Lock in another — merged onto a single plot.

**Why:** real recordings don't always co-locate SNR and frame-sync data. Today
opening a file `clearData()`s the plot and rebinds the reader to that one file.

**What already works (the hard part):**
- `PlotViewModel::addStreamData()` *accumulates* and maps every stream onto a
  shared absolute-time axis: `elapsed = timesSec − m_base_abs_seconds`, base set
  once from the first stream (`plotviewmodel.cpp`). Streams from a second file
  land on the same axis automatically.
- Series identity is `(streamLabel, streamOrder)` — merged streams stay distinct.

**What must change (coordination/UI layer only):**
- `MainViewModel` assumes one file: `m_input_filename`, a single `m_reader`
  rebound per open, `clearState()` → `m_reader->clearSettings()`.
  `startProcessing()` already just hands `m_coordinator->startProcessing(jobs)` a
  job list, so the ViewModel/coordinator boundary is close to source-agnostic.
- Introduce an **"Add source…"** action distinct from **Open** (Open still
  starts a fresh session and `clearData()`s; Add source appends).
- Run the Configure Streams dialog against the *second* file's TMATS channel
  list, build its jobs, and process with its own reader **without** clearing the
  plot.
- Model a **session as N sources** `[{file, stream-configs}]` rather than
  hard-coding "two" — the same path then handles 1, 2, or 5 files.

**The one real gotcha — time re-basing:**
- The base time is locked to the *first* source. If a later source starts
  *earlier*, its `elapsed` values go negative; unrelated recordings produce
  meaningless offsets.
- Decide the rule set: (a) default align-by-absolute-IRIG-time; (b) when a new
  source predates the current base, re-base all existing series
  (recompute `elapsed`, shift `m_x_min/max`, viewport); (c) warn (and offer
  opt-out / align-both-to-t=0) when source time ranges don't overlap.
- This deserves a short design note **before** coding — it's the only part with
  non-obvious correctness.

**Testing:** ViewModel-level tests for accumulate-from-two-sources, the
re-basing path (second source earlier than the first), and the
non-overlapping-ranges warning. Reuse the synthetic `ProcessedStreamData`
helpers; no new fixture needed for the merge logic.

**Effort:** **L** (design note + reader/coordinator generalization + UI + tests).

---

## Phase 2 — Continuous integration (build + full test suite)

**Goal:** every push/PR builds the app and runs the full 255-test suite
automatically.

**Why:** biggest process gap — there is no `.github/` at all, so the crown-jewel
suite only ever runs locally by hand. This repo has a documented
"tests drifted out of sync with src" history that CI would have caught.

**Approach:**
- `windows-latest` GitHub Actions runner.
- Install Qt 6.10.2 + MinGW via `jurplel/install-qt-action` (aqtinstall), also
  pulling the `tools_mingw` package to match the local toolchain; cache the Qt
  install.
- Steps mirror the `build-and-test` skill: put toolchain on PATH → qmake +
  `mingw32-make` (app) → build tests **in-source under `tests/`** → run
  `tmDataQualityAnalyzer_tests.exe` → fail on non-zero exit.
- **Zero-warning gate:** grep the build log for `warning:` excluding
  `lib/irig106` + `lib/qcustomplot`. After Phase 3 this can drop the exclusion
  and assert a fully clean build.
- The real-file `TestFrameProcessor` fixture is already committed under
  `tests/data/`, so integration tests run unchanged in CI.

**Risks:** runner has ~2 cores, so the ~85 s local run may be slower; acceptable
for CI. Qt install is the main flake source — pin the exact version and cache.

**Effort:** **S–M** (mostly workflow authoring + getting the Qt/MinGW install
green on the runner).

---

## Phase 3 — Warning-clean build (isolate third-party noise)

**Goal:** the build emits **zero** warnings from app code, so a real regression
warning actually stands out (and the "zero-warning policy" becomes greppable).

**Why:** the release build emits 18 warnings, *all* from `lib/irig106` +
`lib/qcustomplot`, which are deny-listed and can't be edited. They bury any real
app warning.

**Approach:**
- Move the third-party sources (`lib/irig106/src/*.c`, `lib/qcustomplot/
  qcustomplot.cpp`) out of the main `.pro`'s `SOURCES` into a **separate
  `staticlib` sub-project** compiled with warnings off (`QMAKE_CFLAGS += -w`,
  `QMAKE_CXXFLAGS += -w`, no `-Wall -Wextra`); the app links it. This also
  compiles the vendored code once instead of every clean build.
- Mark the third-party include dirs as system includes so any header-origin
  warnings in app TUs are suppressed too: replace the `lib/…` entries on
  `INCLUDEPATH` (`.pro` lines 48–49) with
  `QMAKE_CXXFLAGS += -isystem $$PWD/lib/irig106/include -isystem $$PWD/lib/qcustomplot`.
- Keep `-Wa,-mbig-obj` scoped to the QCustomPlot TU (already handled in
  `tests.pro`).

**Risks:** low. Verify the static-lib split doesn't disturb the
`version_autogen.h` / `.rc` generation and that `deploy/build_release.ps1` still
finds all objects. Confirm the debug + release + tests builds all link.

**Testing:** the build itself is the test — CI zero-warning gate (Phase 2) with
no `lib/` exclusion needed afterward.

**Effort:** **S–M.**

---

## Phase 4 — Retire the raw-index ViewModel API

**Goal:** eliminate the *class* of bug behind the PlotCustomizationDialog
stale-index fix by making series identity id-based across the ViewModel surface.

**Why:** `PlotViewModel` still exposes position-based mutators —
`seriesAt(int)`, `setSeriesVisible(int)`, `setSeriesVisibleQuiet(int)`,
`renameSeries(int)`, `recolorSeries(int)` — which are fragile for any caller
that outlives a data change. We patched one call site; the shape invites more.

**Approach:**
- Add id-based counterparts (`seriesById(int id)`, `setSeriesVisibleById(...)`,
  `renameSeriesById(...)`, `recolorSeriesById(...)`) implemented on top of the
  existing `indexOfSeriesId()`, each a no-op on a missing id.
- Migrate callers: `PlotCustomizationDialog` already resolves via
  `indexOfSeriesId()` — point it at the id-based methods directly; `PlotWidget`
  already looks up graphs by stable id.
- Keep the index-based accessors only where a position genuinely is the right
  key (or mark them `// internal/test-only`); consider a debug assert that they
  aren't called across a data-version change.

**Risks:** the test suite uses index-based accessors heavily — migrate
incrementally, suite green at each step.

**Testing:** existing PlotViewModel + PlotCustomizationDialog suites; the
Phase-N regression test `applyChangesRobustToSeriesListChange` already pins the
invariant.

**Effort:** **M.**

---

## Phase 5 — Single-source the CSV column schema

**Goal:** one module owns the CSV column-name format; export and import can't
drift.

**Why:** import re-parses the app's *own* export by string-matching headers
(`_RCVR<N>`, `Lock (%)`, the `"<pcmChannelId> - <streamLabel> <ch.name>"` SNR
name). There's an explicit "keep the two in lockstep if this format changes"
comment linking `PlotViewModel::addStreamData()` and `CsvSeriesParser` — a rename
away from a silent break.

**Approach:**
- Extract a `SeriesColumnSchema` (header + free functions) with two inverse
  operations: `formatHeader(const PlotSeriesData&) → QString` and
  `parseHeader(QString) → {metricType, streamOrder, streamLabel, receiverIndex,
  channelIndex}`.
- `exportCsv()` and `CsvSeriesParser::parse()` both call it; delete the
  duplicated string logic in each.

**Risks:** must preserve the on-disk format **byte-for-byte** for
backward-compat with already-exported CSVs. Pin with the existing
`importExportRoundTrip` test plus a golden-file test against a checked-in sample
CSV.

**Effort:** **M.**

---

## Phase 7 — Speed up the inner test loop

**Goal:** a fast local test mode so TDD doesn't pay the ~85 s full-run tax every
time.

**Why:** `TestFrameProcessor` alone is ~74 s of the ~85 s run (real Ch10
fixture), dominating iteration cost.

**Approach:**
- Add a **runtime** fast-mode to `tests/main.cpp` — a `--fast` flag or
  `TMDQ_FAST_TESTS` env var — that skips the heavy real-file integration cases
  and runs the synthetic-bitstream (`packBitString`) logic tests. **Not** a
  commented-out `main.cpp` (the house rule forbids committing a trimmed harness).
- Optionally split `TestFrameProcessor` into a pure-logic suite (synthetic,
  fast) and a real-file integration suite (slow), so fast-mode is a clean
  suite-level filter.
- CI (Phase 2) and pre-release always run the **full** suite; fast-mode is a
  local convenience only.
- **Committable CI fixtures (follow-on from Phase 2):** the real `.ch10`
  fixtures are gitignored and ~1.8 GB, so CI can't run the fixture-dependent
  integration/domain suites (FrameProcessor, CalibrationExtractor,
  ProcessingCoordinator, Chapter10Reader) — they skip, and CI covers only the
  ~234 logic/widget tests. Those skipped suites are exactly the domain-invariant
  coverage (SNR calibration, frame-sync, missed-frames) most worth guarding on
  every PR. Build **small synthetic `.ch10` fixtures** (KB-sized, committed) —
  or trim a real capture to a handful of frames — so a meaningful integration
  subset runs in CI too. The `write-qt-test` skill already sketches synthetic
  step-cal fixtures; this generalizes that to the reader/processor paths.

**Risks:** low — fast-mode must never be the gate for a release or CI.

**Effort:** **S–M.**

---

## Phase 8 — Performance spike: measure before optimizing (optional)

**Goal:** get real numbers on the *release-build* hot path so any optimization —
or a compiler change (MinGW-GCC → MSVC) — is a data-driven decision, not a guess.

**Why:** the felt slowness (the ~74 s `TestFrameProcessor` run) is a **debug**
build — unoptimized regardless of compiler. The shipped release build is plain
`-O2` with **no LTO, no `-O3`, no `-march`** (the `.pro` only adds `-lws2_32` and
`-Wa,-mbig-obj`), so there's untapped headroom on the *current* toolchain before
a compiler swap is even the right lever. And for multi-GB recordings the wall is
often I/O, not codegen. Measure first.

**On MinGW-GCC vs MSVC specifically:** expect a *lateral* move (~±10%,
workload-dependent), not a step change — both are mature optimizers. MSVC's real
draw is tooling (VS debugger, ETW/WPA profiling, PGO ergonomics), not raw speed,
and switching costs are real: `-Wa,-mbig-obj` → `/bigobj`, VC++ runtime
deployment instead of the MinGW runtime DLLs, GCC-isms in `lib/irig106`, and an
MSVC branch in CI. So GCC tuning + profiling comes first; MSVC is only a
*separate* head-to-head spike if the profile points at codegen and GCC tuning is
exhausted.

**Approach:**
1. **Benchmark harness** — process a representative fixture `.ch10` through the
   real `FrameProcessor` pipeline (reuse the `runWithReader` path) N times in a
   **release** build, report median wall-clock. Keep it out of the normal suite
   (separate target or a `--bench` flag) so it never inflates CI.
2. **Baseline** the current `-O2` release.
3. **Cheap GCC wins, one at a time, recording deltas:** `-flto` (usually the
   biggest easy win), `-O3` on the processing TUs, `-march=x86-64-v2/v3` or
   `-mavx2` if a CPU baseline is acceptable. Optionally GCC PGO
   (`-fprofile-generate/use`) for the last few percent.
4. **Profile the baseline** (sampling profiler / Very Sleepy / WPA) to confirm
   where time actually goes — I/O vs bit-scan vs calibration. This also tells you
   whether the large-file *downsampling* backlog item is warranted.
5. **Only then**, if justified, an MSVC head-to-head spike on the same fixture.

**Deliverable:** a short findings note (baseline + per-flag deltas + profile
hotspots) and a recommendation — which flags to adopt in the `.pro` /
`build_release.ps1`, and whether a compiler change is worth it.

**Risks:** benchmark noise — pin to median-of-N on a quiet machine; keep the
bench target out of the release artifact and the default test run.

**Effort:** **S–M** for the harness + GCC-flag experiments; the MSVC head-to-head
is a separate **M** only if pursued.

---

## Backlog (revisit if it becomes a pain point)

- **Large-file display downsampling.** `PlotViewModel` holds full
  `xValues/yValues` in memory and QCustomPlot draws every point. For very long
  recordings, adaptive sampling for *draw* (keeping full data for export) would
  cut memory/latency. QCustomPlot supports adaptive sampling. **Effort M–L**,
  only if long recordings start to hurt.
