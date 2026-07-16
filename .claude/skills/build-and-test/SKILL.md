---
name: build-and-test
description: >-
  Build and run the tmDataQualityAnalyzer Qt/C++ app or its unit-test suite on this Windows/MSVC
  machine. Use this WHENEVER you need to compile the project, run qmake/nmake, run the Qt Test
  suite, verify a change builds cleanly, reproduce a build error, or check that tests still pass —
  even if the user just says "build it", "run the tests", "does this compile?", or "make sure it's
  green". The Qt and MSVC toolchains are NOT on PATH, the test exe has a strict in-source build
  location requirement, and there is no single-suite CLI filter — get any of these wrong and the
  build or the data-file tests fail for reasons unrelated to the code. This skill encodes the exact
  setup so you don't rediscover it each time.
---

# Build and Test — tmDataQualityAnalyzer

This project is Qt 6.10.3 / C++17 built with qmake + MSVC 2022 (Visual Studio 2022 C++ Build Tools)
on Windows. The toolchain is **not on PATH**, so every build session must set it up first. There are
two separate qmake projects: the app (`tmDataQualityAnalyzer.pro` at the root) and the tests
(`tests/tests.pro`).

Run all commands with the **PowerShell** tool (this is a Windows machine).

## Step 0 — Set up the toolchain (required, every session)

The single source of truth is `scripts/env.ps1`. Dot-source it; it imports the MSVC environment
(via `vcvars64.bat`, located with vswhere or a path probe) and puts the Qt `msvc2022_64` kit bin on
PATH:

```powershell
. .\scripts\env.ps1
```

Shell state does not persist between PowerShell tool calls, so dot-source `env.ps1` at the start of
each command (or chain the build into the same call with `;`). Verify with `qmake --version` /
`cl` if a build fails with "command not found". A harmless `'vswhere.exe' is not recognized` line may
print (emitted from inside vcvars) — ignore it as long as `cl` resolves.

## Building the application

```powershell
. .\scripts\env.ps1
New-Item -ItemType Directory -Force -Path build | Out-Null
Set-Location build
qmake ..\tmDataQualityAnalyzer.pro -spec win32-msvc 'CONFIG+=debug'
nmake -f Makefile.Debug
```

- Debug output: `build\debug\tmDataQualityAnalyzer.exe`. For release, swap `CONFIG+=release` /
  `Makefile.Release` (output under `build\release\`).
- `nmake` is serial (no `-j`); for a faster parallel build install `jom` and use `jom -f
  Makefile.Debug` instead.
- Re-run `qmake` whenever you add/remove a source/header in the `.pro`, change `constants.h`'s
  version, or change a class's `Q_OBJECT` wiring (MOC). For ordinary edits to existing `.cpp` files,
  `nmake` alone is enough.
- **Zero-warning policy:** the build must produce 0 warnings. If a change introduces one, fix it
  before considering the build done — do not report success over new warnings.

## Building and running the tests

The test executable resolves its data fixtures with `applicationDirPath()` → `cdUp()` → `data/`
(see `testDataPath()` in `tests/tst_frameprocessor.cpp`). That means **the test exe must live exactly
one level below `tests/`** — i.e. `tests/debug/` or `tests/release/`. Build *in-source inside
`tests/`*; do NOT build into a nested or out-of-tree directory, or every Chapter10Reader /
FrameProcessor / FrameSetup data-file test fails or skips with no obvious cause.

```powershell
. .\scripts\env.ps1
Set-Location tests
qmake tests.pro -spec win32-msvc 'CONFIG+=debug'
nmake -f Makefile.Debug
.\debug\tmDataQualityAnalyzer_tests.exe
```

- Results are written to `tests\output\results.txt` (gitignored) AND printed to the console. A
  non-zero process exit code means at least one suite failed.
- Green baseline: **325 passed / 0 failed / 0 skipped** across 19 suites (full run with the `.ch10`
  fixtures present).
- A convenience wrapper exists: `powershell -ExecutionPolicy Bypass -File scripts\build_ide.ps1`
  builds the tests (Debug) using the active toolchain from `env.ps1`. It builds but does not run.

### Fast mode and the no-single-suite-filter reality

The full run is dominated by the real-Ch10 integration suites — `TestFrameProcessor` and
`TestProcessingCoordinator` process a real Chapter 10 fixture and take a few minutes between them.
For quick local iteration, pass **`--fast`** (or set **`TMDQ_FAST_TESTS=1`**):

```powershell
.\debug\tmDataQualityAnalyzer_tests.exe --fast   # ~1 s: skips the 4 .ch10 integration suites
```

Fast mode runs the 12 pure-logic + widget/dialog suites (the same set CI runs, where the `.ch10`
fixtures are absent) and skips `TestChapter10Reader`, `TestFrameProcessor`, `TestProcessingCoordinator`,
and `TestCalibrationExtractor`. It is a **local convenience only** — always do a final **full** run
(no `--fast`) before declaring tests green, and never rely on it for CI or a release gate. This is the
built-in replacement for hand-trimming `main.cpp` (which must never be committed). The harness still
does not honor a `ClassName::testCase` single-suite filter.

## When a build breaks for non-code reasons

- **"qmake/nmake/cl is not recognized"** → you skipped Step 0 in this shell.
- **qmake: "msvc-version.conf loaded but QMAKE_MSC_VER isn't set"** → a stale `.qmake.stash` (qmake
  caches compiler detection and shares it up the directory tree) is masking MSVC detection. Delete
  `.qmake.stash` (and any in parent dirs) and build in a clean dir.
- **`C1041 ... cannot open program database`** → stale artifacts in the build dir. Clean it
  (`Remove-Item debug -Recurse -Force` + delete the `Makefile*`) and re-`qmake`.
- **`LNK2019` unresolved Win32 symbol** → a Win32 API (e.g. user32's `GetWindowRect`) needs its import
  lib linked explicitly. Add it to the `win32` `LIBS` in the `.pro`.
- **Data-file tests fail/skip but logic looks fine** → the test exe is not one level under `tests/`;
  rebuild in-source in `tests/`.
- **MOC / "unresolved external symbol ... vtable/metaObject"** → a `Q_OBJECT` class changed; re-run
  `qmake` then rebuild.
- **`/bigobj`** → QCustomPlot's TU needs the large-object switch; it's already in the `.pro`/`tests.pro`.

## Guardrails

- Never edit files under `lib/irig106/` or `lib/qcustomplot/` to make a build pass — they are
  third-party and protected. Adapt in application code instead.
- The generated `version_autogen.h`, `Makefile*`, and `build/` artifacts are not authored by hand.
- Report build/test outcomes faithfully: if a suite fails, show the failing assertion from
  `tests\output\results.txt`; if you trimmed `main.cpp` to iterate, say so and confirm you restored it.
