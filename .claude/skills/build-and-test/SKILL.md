---
name: build-and-test
description: >-
  Build and run the tmDataQualityAnalyzer Qt/C++ app or its unit-test suite on this Windows/MinGW
  machine. Use this WHENEVER you need to compile the project, run qmake/mingw32-make, run the Qt Test
  suite, verify a change builds cleanly, reproduce a build error, or check that tests still pass —
  even if the user just says "build it", "run the tests", "does this compile?", or "make sure it's
  green". The Qt and MinGW toolchains are NOT on PATH, the test exe has a strict in-source build
  location requirement, and there is no single-suite CLI filter — get any of these wrong and the
  build or the data-file tests fail for reasons unrelated to the code. This skill encodes the exact
  setup so you don't rediscover it each time.
---

# Build and Test — tmDataQualityAnalyzer

This project is Qt 6.10.2 / C++17 built with qmake + MinGW (GCC 13.1.0) on Windows. The toolchains
are **not on PATH**, so every build session must prepend them first. There are two separate qmake
projects: the app (`tmDataQualityAnalyzer.pro` at the root) and the tests (`tests/tests.pro`).

Run all commands with the **PowerShell** tool (this is a Windows machine).

## Step 0 — Put the toolchain on PATH (required, every session)

The repo already has the single source of truth for tool paths in `scripts/env.ps1`. Dot-source it;
it sets `QTDIR`, `MINGW_DIR` and prepends both `bin` dirs to PATH:

```powershell
. .\scripts\env.ps1
```

If for some reason that file is unavailable, the equivalent is:

```powershell
$env:PATH = "C:\Qt\6.10.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH
```

Shell state does not persist between PowerShell tool calls, so either dot-source `env.ps1` at the
start of each command, or chain the build into the same call with `;`/`&&`. Verify with
`qmake --version` if a build fails with "command not found".

## Building the application

```powershell
. .\scripts\env.ps1
New-Item -ItemType Directory -Force -Path build | Out-Null
Set-Location build
qmake ..\tmDataQualityAnalyzer.pro -spec win32-g++ 'CONFIG+=debug'
mingw32-make -f Makefile.Debug -j$env:NUMBER_OF_PROCESSORS
```

- Debug output: `build\debug\tmDataQualityAnalyzer.exe`. For release, swap `CONFIG+=release` /
  `Makefile.Release` (output under `build\release\`).
- Re-run `qmake` whenever you add/remove a source/header in the `.pro`, change `constants.h`'s
  version, or change a class's `Q_OBJECT` wiring (MOC). For ordinary edits to existing `.cpp` files,
  `mingw32-make` alone is enough.
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
qmake tests.pro -spec win32-g++ 'CONFIG+=debug'
mingw32-make -f Makefile.Debug -j$env:NUMBER_OF_PROCESSORS
.\debug\tmDataQualityAnalyzer_tests.exe
```

- Results are written to `tests\output\results.txt` (gitignored) AND printed to the console. A
  non-zero process exit code means at least one suite failed.
- Green baseline as of this writing: **213 passed / 0 failed / 0 skipped** across 13 suites.
- A convenience wrapper exists: `powershell -ExecutionPolicy Bypass -File scripts\build_ide.ps1`
  builds the tests (Debug) using `QTDIR`/`MINGW_DIR` from the environment. It builds but does not run.

### Timing and the no-single-suite-filter reality

A full run is **~85 s**, and `TestFrameProcessor` alone is **~74 s** because it processes a real
Chapter 10 fixture. The custom harness in `tests/main.cpp` runs **all 13 suites unconditionally** and
only honors `-o <file>` — it does **not** forward a `ClassName::testCase` filter, so you cannot
narrow the run from the command line.

When you are iterating on one suite and want fast feedback, temporarily comment out the other
`runSuite<...>()` lines in `tests/main.cpp`, rebuild, and run — then restore the full list before you
finish. Always do a final full-suite run before declaring tests green. Never commit a trimmed
`main.cpp`.

## When a build breaks for non-code reasons

- **"qmake/mingw32-make is not recognized"** → you skipped Step 0 in this shell.
- **Data-file tests fail/skip but logic looks fine** → the test exe is not one level under `tests/`;
  rebuild in-source in `tests/`.
- **MOC / "undefined reference to vtable"** → a `Q_OBJECT` class changed; re-run `qmake` then rebuild.
- **`-Wa,-mbig-obj` errors** → only the tests/QCustomPlot TU needs it; it's already in `tests.pro`.
  Don't add it to clangd flags (`.clangd` strips it on purpose).

## Guardrails

- Never edit files under `lib/irig106/` or `lib/qcustomplot/` to make a build pass — they are
  third-party and protected. Adapt in application code instead.
- The generated `version_autogen.h`, `Makefile*`, and `build/` artifacts are not authored by hand.
- Report build/test outcomes faithfully: if a suite fails, show the failing assertion from
  `tests\output\results.txt`; if you trimmed `main.cpp` to iterate, say so and confirm you restored it.
