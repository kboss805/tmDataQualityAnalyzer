---
name: write-qt-test
description: >-
  Add or extend a Qt Test unit-test suite in the tmDataQualityAnalyzer project the right way. Use this
  WHENEVER you are writing new tests, adding a test case to an existing suite, creating a test file
  for a class that has none, or the user asks to "add tests", "improve coverage", "write a test for
  X", or "test this edge case". Wiring a new suite requires touching THREE files in lockstep
  (tst_*.h/.cpp, tests/tests.pro, tests/main.cpp) or it silently won't run; data-file tests depend on
  an exact exe location; and the project has established helpers (testDataPath, packBitString,
  loadDefaultFrameSetup). This skill encodes all of that so new tests actually compile, run,
  and match house style.
---

# Writing Qt Tests — tmDataQualityAnalyzer

Tests use the **Qt Test** framework, live in `tests/`, and build as a separate qmake project
(`tests/tests.pro`). A custom `tests/main.cpp` runs every suite in sequence. Match the existing style
in `tests/tst_*.cpp` — read a neighbor before writing.

## Adding a test CASE to an existing suite (the common case)

1. Add a `private slots:` method to the suite's `tst_<name>.h`.
2. Implement it in `tst_<name>.cpp` using `QVERIFY` / `QCOMPARE` / `QVERIFY2` (message on failure) /
   `QTRY_VERIFY` (for signal-driven async). Use `QFUZZYCOMPARE`-style tolerance for doubles
   (`qFuzzyCompare`, or `QVERIFY(std::abs(a-b) < eps)` — the SNR/calibration tests do the latter).
3. No `.pro`/`main.cpp` changes are needed — the slot is auto-discovered by MOC within the suite.
4. Build and run with the **build-and-test** skill; confirm the new case appears and passes.

## Adding a NEW suite (wire all three files or it won't run)

1. **`tests/tst_<name>.h`** — a `QObject` subclass with `Q_OBJECT` and one `private slots:` method per
   case. Keep `init()`/`cleanup()` for per-test fixtures if needed.
2. **`tests/tst_<name>.cpp`** — implementations. Include project headers flat (`#include "foo.h"`);
   the `tests.pro` INCLUDEPATH covers every `include/<layer>/` folder.
3. **`tests/tests.pro`** — add the `.cpp` under `SOURCES +=` and the `.h` under `HEADERS +=` (MOC
   needs the header listed). If the suite exercises a `src/` class not already compiled into the test
   binary, confirm that `.cpp` is in the `SOURCES` list too.
4. **`tests/main.cpp`** — add `#include "tst_<name>.h"` and a `status |= runSuite<TestName>(log_path);`
   line. **This is the step most often forgotten** — without it the suite compiles but never executes,
   so it silently adds zero coverage.
5. Re-run `qmake` (a new file changed the `.pro`) then rebuild and run via **build-and-test**.

## House helpers — reuse these, don't reinvent

From `tst_frameprocessor.cpp` (copy the pattern; some are file-local statics):
- `testDataPath("file")` → resolves `tests/data/<file>` via `applicationDirPath().cdUp()`. This is
  **why the test exe must sit one level under `tests/`** — see the build-and-test skill.
- `packBitString("10110…")` → packs an MSB-first `'1'/'0'` string into a `QByteArray`, for building
  synthetic PCM bitstreams without a real Chapter 10 file. Prefer this for fast, deterministic
  bit-level logic tests.
- `loadDefaultFrameSetup(setup)` / `setupParams(setup, slope, offset)` → load the 49-word
  (16 rcvr × 3 ch + 1 sync) default word map from `settings/receiver_params/default.toml`.
- `runWithReader(fp, params, setup)` → drives the real `Ch10PacketReader` + `PacketQueue` +
  `FrameProcessor` pipeline synchronously, mirroring `ProcessingCoordinator`. Use for integration-level
  tests over a real fixture.

Async signal tests use `QSignalSpy` + a `QEventLoop` (see the `loadCsvFileAsync` tests in
`tst_plotviewmodel.cpp`). Tests must run headless and self-contained — `main.cpp` already isolates
`QSettings` under a `_tests` org/app scope so they never touch real app settings.

## What good coverage looks like here

Use this checklist as the bar — for any class under test, look for:
- **negative tests** (invalid file/channel/time, empty input, malformed TOML row),
- **boundary tests** (off-by-one on frame/word counts, 0-sample windows, first/last frame at a
  packet seam, sync at the exact boundary vs off-phase),
- **integration coverage** through the reader pipeline, not just isolated units,
- **flaky-risk** avoidance (no real wall-clock dependence; drive signals deterministically),
- **invariant round-trips** (e.g. calibration extract→apply on clean steps; CSV export→import).

Mirror the domain invariants the **tm-code-review** skill lists — a good test pins an invariant so a
future regression fails loudly (e.g. "off-phase sync after lock-loss is not extracted",
"missed-frames stay monotonic", "averaging precedes calibration").

## Reference suites for tricky patterns

Every app class now has a dedicated suite; two are worth copying from when you hit the same shapes:
- **`tst_plotcustomizationdialog`** — dialog test that reaches private widgets/slots via a
  `friend class` declaration (tree build, tri-state group cascade, expand/collapse, per-stream
  visibility round-trip to the ViewModel). Mirror this for any View-layer widget with no public
  accessors.
- **`tst_calibrationextractor`** — async pipeline orchestration: drives the reader + FrameProcessor
  workers to completion via a `QEventLoop` on the `finished(bool, QString)` signal, then asserts
  per-channel success/fallback (over `rnrz-l_testfile`, exactly words 6/7/8 calibrate). Mirror this
  for any signal-driven, thread-backed run.

When these change, add cases to the existing suite rather than starting a new one.

## Finish

Always end with a **full-suite** green run (0 failed, 0 skipped — current baseline 251 passing) via
the build-and-test skill, not a trimmed `main.cpp`. Report the pass/fail counts from
`tests/output/results.txt`.
