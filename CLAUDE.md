# tmDataQualityAnalyzer — Claude Code guide

Qt 6.10.2 / C++17 desktop app (qmake + MinGW 13.1.0, Windows) for analyzing data quality of IRIG 106
Chapter 10 PCM telemetry: per-stream **Frame Sync Lock** stats and **Receiver AGC/SNR**, plotted vs.
time. MVVM, single-reader / parallel-worker processing core. **Current version: 2.6.0.**

> **Full reference:** `docs/CLAUDE.md` is the canonical, detailed guide (user stories, architecture,
> conventions, test catalog). Read it when you need depth. This file is the quick orientation that
> loads every session — keep it lean.

## Build & test

Toolchains are **not on PATH** — prepend them first via `. .\scripts\env.ps1` (or
`C:\Qt\6.10.2\mingw_64\bin` + `C:\Qt\Tools\mingw1310_64\bin`). Use the **PowerShell** tool.

- **App:** from `build/`, `qmake ..\tmDataQualityAnalyzer.pro -spec win32-g++ CONFIG+=debug` then
  `mingw32-make -f Makefile.Debug`. Re-run qmake after `.pro`/version/`Q_OBJECT` changes. **Zero
  warnings required.**
- **Tests:** build **in-source inside `tests/`** (the exe must sit one level under `tests/` or the
  data-file tests fail), then `.\debug\tmDataQualityAnalyzer_tests.exe`. Full run ~85 s
  (FrameProcessor alone ~74 s). The harness runs **all 15 suites** — no CLI single-suite filter.
  Green baseline: 242 passed / 0 failed / 0 skipped.

The **`build-and-test`** skill encodes all of this; prefer it.

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
