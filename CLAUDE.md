# tmDataQualityAnalyzer — Claude Code guide

Qt 6.12.0 / C++20 desktop app (qmake + MSVC 2022, Windows) for analyzing data quality of IRIG 106
Chapter 10 PCM telemetry: per-stream **Frame Sync Lock** stats and **Receiver AGC/SNR**, plotted vs.
time. MVVM, single-reader / parallel-worker processing core. **Current version: 2.14.2.**

> **Full reference:** `docs/CLAUDE.md` is the canonical, detailed guide (user stories, architecture,
> conventions, test catalog). Read it when you need depth. This file is the quick orientation that
> loads every session — keep it lean.

## Build & test

Toolchain is **not on PATH** — set it up first by dot-sourcing `. .\scripts\env.ps1`, which imports
the MSVC environment (via `vcvars64.bat`) and puts the Qt `msvc2022_64` kit on PATH. Use the
**PowerShell** tool.

- **App:** from `build/`, `qmake ..\tmDataQualityAnalyzer.pro -spec win32-msvc CONFIG+=debug` then
  `& $env:TMDQ_MAKE -f Makefile.Debug` (`env.ps1` sets `TMDQ_MAKE` to the parallel `jom` when
  present, else serial `nmake` — ~3.5x faster, so prefer it). Re-run qmake after
  `.pro`/version/`Q_OBJECT` changes. **Zero warnings required.**
- **Tests:** build **in-source inside `tests/`** (the exe must sit one level under `tests/` or the
  data-file tests fail), then `.\debug\tmDataQualityAnalyzer_tests.exe`. The full run (all **29
  suites**) is dominated by the real-Ch10 integration suites (a few minutes); add `--fast` (or
  `TMDQ_FAST_TESTS=1`) to skip those four `.ch10` suites for ~1 s local iteration — **local only; CI
  and releases run the full suite**. No CLI single-suite filter.
  Green baseline: **703 passed / 0 failed / 1 skipped** normally, **704 / 0 / 0** at release. The
  one standing skip is the heavy PRN throughput benchmark, opt-in via `TMDQA_RUN_HEAVY_BENCH=1`
  because it walks a 640 MB recording nine times and takes minutes. At release it is not skipped:
  `scripts\run_throughput_bench.ps1` builds Release, enables it, runs it **pinned to the
  performance cores** (absolute times were bimodal ~1.9x on this hybrid CPU), and **asserts** the
  parallel-vs-solo ratios — it printed its timings and asserted nothing before that, so this is
  the decommutation pipeline's only performance guard. It needs
  `tests/data/test files/prn_testfile, full.ch10` (gitignored); the committed sample carries data
  for 2 of the 4 channels it times and cannot stand in. In a worktree, junction
  `tests/data` to the main checkout's or the fixture tests skip.

**After a Qt version bump, CLEAN every build directory** — `build/` *and* `tests/`, Debug and
Release: `& $env:TMDQ_MAKE -f Makefile.<Config> clean` then rebuild. jom rebuilds an object only
when its dependencies look newer, and a new Qt's headers can carry an *older* date than the objects
already on disk, so objects compiled against the previous Qt are silently relinked against the new
one. **The symptom is a crash in a test that has nothing to do with the change**, at a meaningless
address — `0xc0000005` in `TestStepDetector`, `0xC0000409` entering `TestPlotViewModel`,
`0xC000041D` in the app. Three separate debugging sessions were spent on this before the cause was
identified. CI never sees it because it builds fresh; only local builds after a bump are affected.
(Also regenerate `compile_flags.txt` and delete both `.qmake.stash` files — see `docs/CLAUDE.md`.)

**A change that DELETES a header is also only verified by a clean build** — and this one fails the
other way, as a **false pass** rather than a crash. jom does not notice that a header has gone, so
every translation unit that still `#include`s it keeps its stale `.obj`, relinks, and the build
reports success. v2.14.2's TOML refactor removed `tomlconfighelper.h` and left a dead include in
`streamconfigdialog.cpp`; the full suite ran green at 414/0/0 with zero warnings, entirely against
a stale object, and **CI went red on the very next clean build**. Grepping for the *class* name
cannot catch this: what survives is the *filename*. So after deleting or renaming a header, wipe
`build/` and `tests/` before believing any result.

The **`build-and-test`** skill encodes all of this; prefer it.

**Running a built exe (IDE or by hand):** `py`-free one-liner
`powershell -File scripts\deploy_qt_local.ps1 -Config debug [-Tests]` copies the Qt
runtime next to the exe, so launching needs **no Qt on PATH**. Prefer this to adding Qt
to PATH in an IDE config: a hard-coded Qt path silently rots on the next `QT_VERSION`
bump (that is what caused a `0xC0000135` DLL-not-found after the 6.11.1 upgrade).
**Re-run it after any clean**: wiping `tests/debug/` removes the deployed Qt runtime
along with the objects, and the suite then dies before its first test with exit **127**
and `Qt6Cored.dll: cannot open shared object file` — which looks nothing like a missing
deploy step, and leaves `output/results.txt` holding the PREVIOUS run's numbers for
anyone who reads it without checking. Delete that file before a run you intend to
trust. The
script also copies the **offscreen** platform plugin, which `windeployqt` omits and
headless runs (`QT_QPA_PLATFORM=offscreen`) need.

**IntelliSense:** clangd reads a generated, gitignored `compile_flags.txt` — run
`py scripts/gen_compile_flags.py` after a fresh clone, a Qt bump, or a `.pro`
include/define change. **Run it inside each worktree too**: the paths are absolute, and a
worktree otherwise silently resolves headers from the main checkout while still reporting
zero errors (see `docs/CLAUDE.md` → clangd / IntelliSense).

**Toolset is pinned to Visual Studio 2022 (MSVC 14.44, v143).** `env.ps1` asks vswhere for
`[17.0,18.0)` rather than `-latest`; installing VS 2026 had silently moved every build to
MSVC 14.51 and left two compilers' objects in one tree. Override with `TMDQ_VS_RANGE`
(a vswhere range). **After changing the compiler, wipe `build/` and `tests/` objects** —
same stale-object trap as a Qt bump.

**Visual Studio:** `powershell -File scripts\gen_vs_project.ps1` generates
`tmDataQualityAnalyzer.sln` (app + tests, Debug|x64 and Release|x64) from the `.pro` files
via `qmake -tp vc`, then fixes what the raw output gets wrong. Open it in Visual Studio
Community and F5 (it targets VS 2026 / toolset v145; `env.ps1` itself stays on VS 2022 —
see below). The solution and `vs/` are **generated and gitignored** (absolute Qt
paths) — **regenerate after a `.pro` change, a Qt bump, or a Visual Studio update**. It
builds with VS's own MSBuild (no Qt VS Tools needed); the Qt runtime is deployed by a
post-build step. The qmake build stays the supported/CI one; the `.pro` files remain the
source of truth. Rationale for each correction: `docs/CLAUDE.md` → Unreleased.

## Project skills (`.claude/skills/`)

`build-and-test` · `tm-code-review` · `write-qt-test` · `add-stream-setting` · `cut-release` ·
`verify-ui-change` · `code-cleanup`.
Invoke explicitly (`/build-and-test`) or let them trigger by intent.

## Hard rules

- **Never edit** `lib/irig106/**` (third-party; deny-listed in
  `.claude/settings.json`). Adapt in app code (`ch10packetreader.cpp`, `frameprocessor.cpp`,
  `plotwidget.cpp`, …) instead.
- **Version is single-sourced** in `AppVersion` (`include/constants.h`); never hand-edit
  `version_autogen.h`, `.pro` VERSION, or the `.rc`.

## Domain invariants (silent-bug territory — see `tm-code-review` skill & `docs/*_logic.md`)

- **Ch10 paths:** both readers pass irig106 the path in the process code page (`path.string()`),
  never UTF-8 from `toUtf8()` - its narrow `open()` reads that code page, which
  `codepage.pri`'s manifest makes UTF-8.
- **SNR:** average raw counts first, calibrate once per window. Out-of-range interpolation **clamps**,
  never extrapolates. The calibration extractor must mirror the main run's word map + raw-affecting
  flags exactly: both build them through `applyAcquisition` (`include/model/framesync.h`), so a new
  raw-affecting field goes there and nowhere else.
- **irig106 setup is serialised:** the vendored library keeps unlocked process-wide state (the handle
  table, the TMATS decoder's statics), so two simultaneous setups can crash. `Chapter10Reader::loadChannels`
  and `Ch10PacketReader::prepare` hold `lockIrig106Setup()` (`include/model/irig106lock.h`) for their whole
  setup; anything new that opens a recording must too. Reading from an open handle needs no lock.
- **Frame sync:** lock % is a **bit-span** quantity, not `time × bitrate`. Reject off-phase syncs
  while locked. Don't double-count missed frames (gap extrapolation only when a window has zero bits).
- **TOML boundary:** frame-sync Load/Save round-trips the sync pattern, sync mask, words/frame and
  the two **stream-format flags** (`Inverted`, `Randomized`) — they describe the recording's PCM code
  format, like the pattern itself. `Data Rate`/`Sample Rate` are per-session operator inputs and stay
  out. Calibration profiles are session-only, never serialized.

## Memory

Project memory index lives at the auto-loaded `MEMORY.md` (frame-sync fixes, calibration history,
review-remediation status). Check it for prior context before deep work.
