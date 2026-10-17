# Porting to a Microsoft-only native stack (.NET)

**Status: ACTIVE on `explore/dotnet-migration`, under a Microsoft-only goal.** The full
C# rewrite is still not recommended; the hybrid below is the path being explored. Both
standalone phases are done and landed on `main`.

See `docs/dotnet-migration/README.md` **on that branch** for the working plan - the
phase split, the two deciding spikes, and the rule that keeps the branch mergeable
(anything that keeps the Qt app shipping goes to `main`, not the branch). This file stays
the costed assessment; it is not the work plan.

The question was whether the application could move to a native Windows stack built
only with Microsoft tools — .NET, XAML — and what that would cost. This file records
the measurement and the conclusion so the question does not have to be re-derived from
scratch the next time it comes up.

## What would actually move

Measured 2026-10-15 on `main` at v2.14.2.

| Layer | Lines | How it ports |
| --- | --- | --- |
| `src/model` + `include/dto` | 4,512 | **Cleanly.** Effectively GUI-free |
| `src/viewmodel` | 2,569 | **Well.** 150 `connect()` / 146 `emit` across 11 `signals:` blocks map onto `INotifyPropertyChanged` + `ICommand` |
| `src/view` | 7,204 | **Rewrite.** QSS to XAML, QPainter to a different 2D model |
| `include/` | 4,777 | Largely disappears — C# has no headers |
| `tests/tst_*` + `tests/main.cpp` | 11,328 | **Rewrite.** The Qt Test harness does not survive |
| `lib/irig106` | 13,514 (C) | **Stays native.** P/Invoke behind a shim |

First-party source is 19,127 lines (`src` + `include`). Roughly 7k of that genuinely has
to be rebuilt, plus the 11.3k of tests that protect it.

**The model layer is GUI-free in substance**, which is what makes the hybrid option below
viable. The only `QColor` references are a hex string round-tripped through
`processingtemplateschema.cpp` and two value fields in `plotseriesdata.h` /
`seriesappearance.h` — a struct or a string replaces all three. It does depend on Qt Core
(`QString` x247, `QVector` x129, `QSettings` x31), which is a different and much smaller
problem than depending on Qt Widgets.

## Stack choice: WPF on .NET 9, not WinUI 3

Three reasons specific to what this application does:

1. **The frameless chrome transfers almost literally.** `mainviewchrome.cpp:447` handles
   `WM_NCCALCSIZE` and `WM_NCHITTEST`; WPF exposes the same messages through
   `HwndSourceHook`. WinUI 3 offers `AppWindowTitleBar.SetDragRectangles` — *rectangles*,
   not a hit-test callback — so the caption and resize-border logic becomes an
   approximation of what already works.
2. **WPF can render offscreen** via `RenderTargetBitmap`. Headless WinUI 3 effectively
   needs an interactive session, which would break the `verify-ui-change` method and the
   self-hosted runner's UI steps. `QT_QPA_PLATFORM=offscreen` is load-bearing here, not a
   convenience.
3. **WinUI 3 pulls in the Windows App SDK runtime** — either a new install dependency or a
   large self-contained payload, and MSIX packaging pulls against the signed-Inno,
   dual-scope install. QtIFW was already rejected for touching exactly that; this touches
   it harder.

WPF's cost, stated plainly: it is in maintenance mode, and NativeAOT does not support it
(XAML is reflection-heavy), so ReadyToRun is the ceiling.

## The shortfalls, worst first

1. **The chart is a from-scratch re-derivation.** `tmchart.cpp` is 1,089 lines encoding
   findings that took three separate sessions to reach: `drawLines()` rather than
   `drawPath`/`drawPolyline` because a 1.5px pen is not cosmetic so the stroker dominates
   (1423 ms vs 27 ms), the per-pixel-column envelope, geometry cached and culled by binary
   search, the `QPixmap` backing store. **Every one of those is a QPainter-specific
   finding.** WPF is retained-mode — a `DrawingVisual` scene graph, not an immediate-mode
   painter — which works against the per-pixel-column approach; the likely landing place is
   drawing into a `WriteableBitmap` by hand, which means re-deriving the whole optimisation
   story on a different substrate. The 102 s to 34 ms result is not inherited.

2. **The bit-level hot loop in a GC language.** `frameprocessor.cpp`, `stepdetector.cpp`
   and `ch10packetreader.cpp` walk a 640 MB recording doing bit-span arithmetic and
   sync-pattern matching. C# can reach this with `Span<T>`, `stackalloc` and
   `System.Runtime.Intrinsics`, but that is C# shaped like C, with allocation pressure and
   bounds-check elision as live concerns. Combined with having **no throughput guard today**
   — `benchmarkHeavyWorkloadSingleVsMultiStream` is the suite's one skip and nothing sets
   `TMDQA_RUN_HEAVY_BENCH` — this is where a port regresses silently.

3. **Struct marshalling is a silent-failure boundary.** App code calls 11 distinct
   `enI106*` functions from 3 `.cpp` files (`ch10packetreader.cpp`, `chapter10reader.cpp`,
   `frameprocessor.cpp`): five `enI106Ch10*` I/O entry points plus six time and TMATS
   decode helpers. Mirroring the Ch10 header layouts in C# needs
   `[StructLayout(LayoutKind.Sequential, Pack = ...)]` written by hand. **Get `Pack` wrong
   and you read plausible garbage at wrong offsets** — the same class of silent domain bug
   the invariants in `CLAUDE.md` exist to catch, except at an FFI seam no current test
   looks at. `lib/irig106` is deny-listed, so the shim is new code outside it that we own
   and must test.

4. **11,328 lines of tests encode bugs that each cost a debugging session.** Off-phase sync
   rejection, the bit-span lock-% denominator, calibration word-map pairing, the
   missed-frames double-count, the extract-period floor. The *assertions* transfer;
   `QSignalSpy`, `QTest::qWait`, data-driven `_data()` functions and the offscreen platform
   do not. The widget-measurement suites are the least portable and guard the most
   invisible failures — `tst_plotwidget.cpp` (1,193) and `tst_streamconfigdialog.cpp` (894).
   Note the QSS-min-size trap survives the port intact, as a WPF Style-setter versus
   explicit-`Width` problem.

5. **Headless UI verification needs rework** even on WPF — `RenderTargetBitmap` exists, but
   the CI step and the measure-do-not-merely-look method both have to be rebuilt on it.

## What it would genuinely buy

- **The entire Qt-bump ritual disappears**: clean every build directory including
  `tests/`, regenerate `compile_flags.txt`, delete both `.qmake.stash` files — and with it
  the stale-object trap that cost three debugging sessions and surfaces as a crash in an
  *unrelated* test at a meaningless address.
- **`dotnet publish -r win-x64 --self-contained` replaces windeployqt**, retiring most of
  the DLL-closure risk class: the `0xC0000135` on launch, the hard-coded Qt path that rots
  on the next `QT_VERSION` bump, and much of what `deploy/sandbox/` was built to catch.
  Inno Setup, signing and the dual-scope install all survive unchanged.
- **XAML data binding would meaningfully shrink the view layer** — `streamsubdialogs.cpp`
  is 1,083 lines of what is largely wiring.
- Qt licensing stops being a question for a signed, distributed binary.
- Hot Reload, the Visual Studio designer, and a larger pool for handoff.

## Verdict

Not a lateral move and not a port: a **view-layer and test-suite rewrite**, about 18k of
the ~30k first-party lines, buying deployment simplification and the end of Qt-bump
maintenance at the price of re-deriving two measured performance results with no guard
currently in place for either.

**"Microsoft-only" does not mean "C#-only", and that is what makes the performance answer
possible.** MSVC is Microsoft; Qt is the only non-Microsoft *framework* in the tree
(`irig106` is third-party C and is irreplaceable either way). So the constraint is satisfied
by dropping Qt while keeping C++ - which means the bit-serial scanner, the derandomizer and
the calibration maths are never rewritten at all. Only **results** cross the interop
boundary: `ProcessedStreamData` is flat `double` arrays, worst realistic case ~34 MB once
per run, against a 640 MB recording read and scanned entirely on the native side.

**If the gains are wanted without that risk, the hybrid is the honest answer:** keep
`src/model` as a native DLL behind a flat `extern "C"` ABI — it is already GUI-free, it is
where the performance lives, and it is where the hard-won tests are — and rewrite only the
view in WPF. That preserves the decommutation loop's measured behaviour and the domain
test suite, and still yields self-contained .NET deployment for the shell. The cost is two
toolchains and one marshalling boundary, which is strictly less than two re-derivations
plus 11k lines of rewritten tests.

## The only part proposed for action

Both of these improve the Qt application whether or not the port ever happens, and both are
reversible. They are the cheap way to keep the option open rather than steps toward taking
it.

**Both are now DONE** — see the `Unreleased` section of `docs/CLAUDE.md`. What they found in
passing is worth recording here, because both were worse than this file assumed:

- **~~De-Qt the model layer.~~ Done, in the narrow sense that was worth doing.** The TOML
  support moved off `QSettings::registerFormat` to a plain `TomlDocument`. The payoff was
  larger than "a layering smell": `FrameSetup` had been parsing every word-map file **twice**
  (QSettings sorts sections, a word map is meaningful in file order), writes needed a
  `QFile::remove()` first because QSettings merges, and the format needed process-wide
  registration from two separate `main()`s. **The `QString`/`QVector` sweep was dropped at the
  time** - this file was wrong to bundle it with the TOML work, which stands alone.

  **That reasoning is now superseded, and the sweep is REQUIRED.** It was dropped because
  the hybrid keeps the model as a native DLL linking Qt Core, so the sweep bought nothing
  short of a full C# rewrite. Under a **Microsoft-only** goal that no longer holds: Qt Core
  has to leave the core too. Re-measured against the six files that would actually form the
  native core (~4,170 lines), it is far smaller than the ~380 call sites quoted here -
  **431 Qt token occurrences**, every one a mechanical substitution:

  | | | | |
  | --- | --- | --- | --- |
  | `QString` 182 | `QVector` 90 | `QObject` 20 | `QStringList` 17 |
  | `QFileInfo` 14 | `QFile` 9 | `QList` 8 | `QByteArray` 8 |
  | `QIODevice` 7 | `QMap` 6 | `QHash` 5 | `QTextStream` 5 |
  | `QElapsedTimer` 4 | `QThread`/`QMutex`/`QWaitCondition` 8 | rest <= 3 | |

  The hot files specifically - `frameprocessor.cpp`, `stepdetector.cpp`,
  `ch10packetreader.cpp` - use only `QString`, `QVector`, `QByteArray`, `QObject` and
  `QElapsedTimer`; the bit-serial scanner is already plain arithmetic. Signals become
  `std::function` callbacks and the threading primitives become their `std` equivalents.

  **This work belongs on `main`, not the branch** - it keeps the Qt application building at
  every step, and it touches the files maintenance touches, so doing it on a branch is the
  maximum-conflict choice.
- **~~Turn on the throughput benchmark.~~ Done, and the gap was deeper than "nothing sets
  the variable".** The benchmark **asserted nothing** - it printed timings through
  `qWarning()` and passed regardless - so describing it above as a guard that merely needed
  enabling was wrong. It was also pointed at a fixture that *cannot* satisfy it: v2.11.1's
  fixture reorganization repointed it at the committed 77 MB sample, which carries PCM data
  for 2 of the 4 channels it times. So it had been unable to produce a number since v2.11.1,
  silently, because being opt-in meant nobody ever saw it fail. It now asserts the
  parallel-vs-solo ratios and runs at every release.

**The remaining TOML follow-up**, left deliberately: `StepDetector::parseStepConfig` is a
second hand-written TOML parser, which exists precisely because the QSettings format could
not express `[[Step]]` arrays. Folding it into `TomlDocument` is the obvious consolidation,
but it feeds calibration and deserves its own verifiable step rather than riding along.
