# tmDataQualityAnalyzer — Claude Code guide

Qt 6.10.3 / C++17 desktop app (qmake + MSVC 2022, Windows) for analyzing data quality of IRIG 106
Chapter 10 PCM telemetry: per-stream **Frame Sync Lock** stats and **Receiver AGC/SNR**, plotted vs.
time. MVVM, single-reader / parallel-worker processing core. **Current version: 2.7.0.**

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
  data-file tests fail), then `.\debug\tmDataQualityAnalyzer_tests.exe`. The full run (all **19
  suites**) is dominated by the real-Ch10 integration suites (a few minutes); add `--fast` (or
  `TMDQ_FAST_TESTS=1`) to skip those four `.ch10` suites for ~1 s local iteration — **local only; CI
  and releases run the full suite**. No CLI single-suite filter.
  Green baseline: 341 passed / 0 failed / 1 skipped. The one skip is the heavy PRN throughput
  benchmark, which is opt-in (`TMDQA_RUN_HEAVY_BENCH=1`) because it walks a 640 MB recording nine
  times and takes minutes. In a worktree, junction
  `tests/data` to the main checkout's or the fixture tests skip.

The **`build-and-test`** skill encodes all of this; prefer it.

**IntelliSense:** clangd reads a generated, gitignored `compile_flags.txt` — run
`py scripts/gen_compile_flags.py` after a fresh clone, a Qt bump, or a `.pro`
include/define change. **Run it inside each worktree too**: the paths are absolute, and a
worktree otherwise silently resolves headers from the main checkout while still reporting
zero errors (see `docs/CLAUDE.md` → clangd / IntelliSense).

## Project skills (`.claude/skills/`)

`build-and-test` · `tm-code-review` · `write-qt-test` · `add-stream-setting` · `cut-release`.
Invoke explicitly (`/build-and-test`) or let them trigger by intent.

## Hard rules

- **Never edit** `lib/irig106/**` or `lib/qcustomplot/**` (third-party; deny-listed in
  `.claude/settings.json`). Adapt in app code (`ch10packetreader.cpp`, `frameprocessor.cpp`,
  `plotwidget.cpp`, …) instead.
- **Version is single-sourced** in `AppVersion` (`include/constants.h`); never hand-edit
  `version_autogen.h`, `.pro` VERSION, or the `.rc`.

## Domain invariants (silent-bug territory — see `tm-code-review` skill & `docs/*_logic.md`)

- **SNR:** average raw counts first, calibrate once per window. Out-of-range interpolation **clamps**,
  never extrapolates. The calibration extractor must mirror the main run's word map + raw-affecting
  flags exactly.
- **Frame sync:** lock % is a **bit-span** quantity, not `time × bitrate`. Reject off-phase syncs
  while locked. Don't double-count missed frames (gap extrapolation only when a window has zero bits).
- **TOML boundary:** frame-sync Load/Save round-trips ONLY sync pattern, sync mask, words/frame —
  `Randomized`/`Data Rate`/`Sample Rate` are per-session and excluded. Calibration profiles are
  session-only, never serialized.

## Memory

Project memory index lives at the auto-loaded `MEMORY.md` (frame-sync fixes, calibration history,
review-remediation status). Check it for prior context before deep work.
