# Moving to Avalonia, and taking Qt out completely

**Decided 2026-10-20:** the UI moves to Avalonia 12 on .NET 10. This replaces the WPF direction in
`README.md` and `presentation-layer.md`, which assumed a Microsoft-only rule that no longer applies.
The evidence behind the choice is in `dotnet/hybrid-spike/README.md`, sections 3 (performance) and 4
(headless UI tests).

**End state:** a C# Avalonia application over a native, Qt-free C++ core (decoding, frame sync,
calibration, file formats) behind a flat C ABI. No Qt anywhere: not in the app, not in the core, and
not in the tests.

This file is the work plan. Each numbered step is one PR, tick it when it merges, and delete the
file when the last step lands (the `docs/future_plans/` convention).

## Rules for every step

1. **The Qt application keeps building, passing and shipping until step 6.1.** Every step lands on
   `main` through an ordinary PR. There is no long-lived branch: the new app grows beside the old one
   (`dotnet/` next to `src/`), and a release can be cut from `main` at any point.
2. **Tests first, on the old code.** Before changing or porting anything, pin what it does today, run
   the pins against the unchanged code, and only then change it. For a reimplementation, keep the old
   implementation as an oracle and diff the two over generated input.
3. **Mutation-check every step.** A test that cannot fail proves nothing. Single-defect mutants per
   rule; a survivor is a missing test, not a shrug.
4. **Clean build before believing a result** (both build trees), zero warnings, and the Release
   throughput gate for anything on the decode path.
5. **One implementation of every file format.** The CSV export, the TOML settings files and the
   template JSON are read by both apps during the transition. Never maintain two hand-written copies:
   either the format moves into the native core where both apps call it, or one side is the oracle the
   other is tested against.

## Where we start (measured on `main` at `f60297e`)

| layer | size | Qt today | where it goes |
|---|---|---|---|
| Native core (`src/model` core sources) | ~4,200 lines | **none**, guarded by `TestCoreQtFree` | stays C++, becomes `tmdq_core.dll` |
| UI-session model (`csvseriesparser`, `seriescolumnschema`, `templatematcher`, `streamconfigschema`, `processingtemplateschema`) | ~710 lines | `QString`, `QJson*` | core (formats, matching) or C# (template JSON); step 1 |
| DTOs (`StreamConfig`, `FrameSyncParams`, `PlotSeriesData`, `ProcessingTemplate`, `SeriesAppearance`, `Source`) | ~280 lines | `QString`, `QVector`, `QColor` | core types or C# records |
| ViewModels | 2,454 + 719 lines | throughout | rewritten in C# (step 4), logic they own moved to the core first (step 1) |
| View (13 files) | 7,252 lines | throughout | rewritten in Avalonia (step 4) |
| Tests (31 suites) | 18,144 lines | Qt Test throughout | core suites ported to a native framework (step 2); UI suites rewritten on `Avalonia.Headless` (step 4) |
| `lib/irig106` | C | none | unchanged, never edited |

What the spikes already proved, so the plan does not re-prove it: the core runs as a Qt-free DLL and
a C# host reproduces the recorded numbers exactly; Avalonia hosts a native D2D chart at the refresh
rate; UI tests run headless in Session 0; a clean machine runs the result.

## Decisions to make first

Each has a recommendation. Steps that depend on a decision say so.

- **D1. How the chart draws. DECIDED (step 0.2): Skia in Avalonia**, drawing geometry that the native
  core builds (culling and the per-pixel-column envelope). Measured in `dotnet/hybrid-spike/README.md`
  section 5, against the Direct2D chart drawing the very same geometry:
  - **Where the chart is the bottleneck, Skia is faster:** 12 x 360k at 45 fps against 31, and 3 x 1M
    at 90 against 70.
  - **Light loads are equal** (92 against 92), except when the GPU is not waited for at 180 Hz (102
    against 166). Both exceed 60 Hz everywhere except the heaviest case.
  - **Software rendering, with no GPU, draws 12 x 360k in 19.4 ms.**
  - **PNG, SVG and PDF all come from the one drawing routine,** each verified by looking.
  - **It renders in headless tests**; a new test proves it.
  - It removes the Direct3D interop, the keyed mutex, the texture rotation and the second native DLL
    from the plan.
- **D2. The native test framework that replaces Qt Test for the core.** **Decided: doctest**, a
  single vendored header, so it needs no package manager and builds the same under qmake now and
  MSBuild later. The project is public and GPL-3.0, so MIT, BSD and BSL licences are all
  compatible and licensing does not separate the options.
- **D3. Where session logic lives.** **Decided:** anything that **defines a file format or
  computes data** moves into the native core (CSV columns, CSV parse and export, stream-config to job
  conversion, template channel matching, series construction and time re-basing); anything that is
  **presentation state** is C# (palette, visibility, axis ranges, zoom history, recent files,
  preferences). Template JSON is the one judgement call: C# reads and writes it (System.Text.Json),
  tested against templates written by the Qt app.
- **D4. Release numbering.** *Recommended:* the first Avalonia release is **v3.0.0**, and the last Qt
  release stays downloadable as the fallback for one release cycle.
- **D5. Avalonia's tooling licence** (yours to check): the framework is MIT, but the paid tooling tier
  applies above five developers or to enterprises. Check what the team's Visual Studio workflow needs
  before step 3.

## Step 0: groundwork

- [x] **0.1 Merge the exploration** (PR #170, merged as `9d5e6c8`) so the spikes are on `main`.
- [x] **0.2 Measure a Skia chart (decides D1).** Add a Skia-drawn chart to the Avalonia spike beside
  the native one, same phases, same machine: redraw time for 3 x 360k, 12 x 360k and 3 x 1M, and a
  PDF and SVG export of the same frame. Done when the README records both and D1 is settled.
  *Done: `dotnet/hybrid-spike/README.md` section 5; D1 is Skia.*
- [x] **0.3 Bring the docs in line with the decision.** `README.md` and `presentation-layer.md` stop
  describing WPF as the plan; `docs/future_plans/dotnet-port-assessment.md` gets its status updated
  (it still argues for WPF); `CLAUDE.md` gains a short "migration in progress" note pointing here.

## Step 1: finish taking Qt out of everything below the view (on `main`, Qt app unchanged in behaviour)

Each item moves one piece into the Qt-free core with `std::` types, extends `TestCoreQtFree`'s core
list, and keeps the Qt app calling the moved code through a thin conversion. Same method as the de-Qt
sweep.

- [x] **1.1 `SeriesColumnSchema`** to the core: the CSV column header format and its parse (the
  export/import inverse pair, already round-trip tested). PR #174, merged as `9c05056`.
- [x] **1.2 `CsvSeriesParser`** to the core, plus the CSV **export** that today lives in
  `PlotViewModel::exportCsv`, so both directions of the format live in one place. Oracle: the Qt
  export over every fixture must be byte-identical before and after. PR #175, merged as `5224a22`.
- [x] **1.3 `TemplateMatcher`** to the core (pure set logic, smallest step). PR #176, merged as `0f18eb4`.
- [x] **1.4 The frame-sync and receiver-parameter TOML helpers** out of `streamsubdialogs.cpp` (a view
  file today) into the core, beside `TomlDocument`. Keeps the US1.0 / US5.1 TOML boundaries exactly as
  they are; `tomlFrameSyncRoundTripsStreamFormatFlags` and its siblings are the oracle. PR #177, merged as `0b71c85`.
- [x] **1.5 Stream-config to job conversion** (`MainViewModel::buildStreamJob` and the validation in
  it) into the core, taking a plain `StreamConfig` value. Its 51 existing cases move with it. PR #178, merged as `24226d5`.
- [x] **1.6 Series construction and time re-basing** (the data half of `PlotViewModel::addStreamData`:
  building series from `ProcessedStreamData`, identity keys, the shared elapsed axis across sources)
  into the core. The presentation half (colours, visibility, ranges) stays for C#. PR #179, merged as `40d8481`.
- [x] **1.7 DTO cleanup:** whatever the moved code needs (`StreamConfig`, `FrameSyncParams`,
  `SeriesAppearance` colour as a plain RGB value) becomes `std`; the Qt-only DTOs that remain are
  presentation types, rewritten in C# in step 4. *Nothing the moved code needs was left Qt (1.1-1.6 take plain
  copies), so the DTOs stay as the UI's own types; the step instead makes the end state a guard:
  `TestCoreQtFree` fails on any file outside the View that uses Qt and is not on a named list, and on any other
  header that reaches Qt or a higher layer. The list is longer than "Done when" below says: it also holds the UI-form
  DTOs and the Qt bridges.* PR #180, merged as `e637e2e`.

**Done when** the only Qt left outside `src/view` is the four ViewModels and `StreamConfigSchema` /
`ProcessingTemplateSchema`, and `TestCoreQtFree` lists everything else as core.

## Step 2: the core as a product, and its tests off Qt

- [x] **2.1 Build `tmdq_core.dll` from `main`.** A native project (MSBuild `.vcxproj` in a new
  `tmDataQualityAnalyzer.sln`, the long-term build) compiles the core sources and `irig106` into a DLL
  with no Qt include path; `dumpbin /dependents` shows no Qt. The Qt app keeps compiling the same
  sources through qmake until step 6. CI builds both. *The solution is `tmdq.sln`, because
  `tmDataQualityAnalyzer.sln` is the gitignored, generated Visual Studio solution. The ABI is still the spike's
  version 1; the spike's 20-check driver passes against the new DLL.* PR #181, merged as `698bcfc`.
- [x] **2.2 The full C ABI** (`tmdq_abi.h`), grown from the spike's `probe` and `run_lock_stream` to
  everything the app needs. Blittable structs, an ABI version and revision, and the size-of checks from the
  interop spike. It is the largest step of stage 2 (every capability the app uses crosses the boundary), so it
  lands as four PRs, each tested in-process by a `TestAbi` suite that compiles the ABI sources into the test
  executable and mutation-checked like any other step:
  - [x] **2.2a Conventions, the file, and runs.** PR #182, merged as `fa5ca1a`. Opaque handles, versioned structs (`struct_size` first, fields
    only appended), UTF-8 strings, one error per thread, no exception across the boundary. Open a file and list
    its time and PCM channels with their TMATS rates; run N streams of either mode over one read of the file
    (settings in, the core's own `buildStreamJob` and `PipelineRunner` behind), with calibration profiles, log,
    error, progress, per-stream-result and finished callbacks, cancel, and result accessors that view the
    result's arrays without copying. *The app has no time-range selection (its summary says "Time range: all"),
    so none is exposed; the request's struct can grow one.*
  - [x] **2.2b Calibration.** PR #183, merged as `3da4cb6`. Step-file parsing (already in the core) and calibration extraction with its
    callbacks, cancel and per-channel results. *What the dialog did inline - decoding the sync text and resolving the
    default word map when assembling the extractor's request - moved into the core first
    (`buildCalibrationRequest`), pinned against the old code.*
  - [x] **2.2c The formats.** PR #184, merged as `d7cb61f`. The frame-sync pattern file (read and write), CSV import and export, and template
    channel matching, plus clock stamps. *Pure translation of core code stage 1 moved; tested against the core
    called directly, with written files compared byte for byte.*
  - [x] **2.2d Series construction.** PR #185, merged as `4f880b3`. `buildStreamSeries`, the shared time base, stream identity and the import
    ordering. *With this the ABI covers everything 2.2 listed.*
- [x] **2.3 A C# interop library** (`TmDq.Core`) over the ABI, with a test project (xunit v3) that runs the
  committed fixtures through it and compares against numbers the Qt app produces today (the spike
  driver's 20/20 check, made permanent and widened to every fixture and both modes). Four PRs:
  - [x] **2.3a The library and its tests, for what the walking skeleton needs:** PR #186, merged as `36f1ecb`.
    Loading and verifying the DLL (ABI version, every mirrored struct's size against `tmdq_sizeof`), the file's
    channels, and runs of N streams with callbacks, cancel and results. The spike driver and the version-1
    calls are retired; the ABI becomes version 2. The managed tests run in CI (`scripts/test_dotnet.ps1`).
  - [x] **2.3b Step files and calibration extraction:** PR #187, merged as `032ab33`. `StepFile`, `CalibrationRun`
    and the managed copy of its result, whose profiles feed a run. The callbacks both runs use move into one
    shared bridge. *Tested against the numbers the core's own `CalibrationExtractor` makes: all 48 channels, the
    report, and a run with the profiles it found.*
  - [x] **2.3c The file formats:** PR #188, merged as `87729d7`. The frame-sync pattern file (read and write), template matching, CSV import
    and export, and clock stamps. *Tested over a corpus (58 pattern files, 56 CSV files, writes, matches,
    stamps) against what the core's own functions answer for the same inputs.*
  - [x] **2.3d Series construction:** PR #189, merged as `125afe4`. `buildStreamSeries`, the shared time base, stream keys and the import
    ordering, each wrapped and tested against the same numbers. *Split from 2.3c because the surface is as
    large again.*
- [ ] **2.4 Port the core test suites off Qt Test** (D2), one suite per PR, old and new running side
  by side until the new one has every case. Order, smallest first: `TestTomlDocument`,
  `TestTextFormat`, `TestFrameSync`, `TestEventReporter`, `TestPacketQueue`, `TestIntervalGate`,
  `TestFrameSetup`, `TestStepDetector`, `TestCh10PacketReader`, `TestChapter10Reader`,
  `TestFrameProcessor`, `TestPipelineRunner`, `TestCalibrationExtractor`, and the suites for the code
  moved in step 1. Each ported suite is mutation-checked against the same mutants as the original;
  then the Qt Test version is deleted. The throughput benchmark moves with `TestPipelineRunner`.
  The tests live in `native/tmdq_core_tests` (doctest, vendored in `lib/doctest`; MSBuild, no Qt;
  `scripts/test_native.ps1`; a CI step in both jobs), and `TestCoreQtFree` holds the project to core
  sources and no Qt. **Ported so far:** `TestTomlDocument` (#190 `10af53e`, which also brought the framework), `TestTextFormat` (#191 `9e70e44`), `TestFrameSync` (#192 `67752db`), `TestEventReporter` (#193 `8da2345`), `TestPacketQueue` (#194 `64c9cb5`), `TestIntervalGate` (#195 `9405758`), `TestFrameSetup` (#196 `9701d54`), `TestStepDetector` (#197 `e7edd88`), `TestCh10PacketReader` (#198 `f2bedfd`), `TestChapter10Reader` with `TestChannelData` (#200 `3912bf8`), `TestFrameProcessor` (#201 `c779afe`), `TestPipelineRunner` (#202 `5d1ef5a`, which brought the throughput benchmark with it), and `TestCalibrationExtractor`. Still to port: the suites for the code moved in step 1.

**Done when** the core and its tests build and pass with no Qt installed, and CI runs them.

## Step 3: the Avalonia app's skeleton and pipeline

- [ ] **3.1 Projects and conventions:** `TmDq.App` (Avalonia, MVVM with CommunityToolkit.Mvvm),
  `TmDq.App.Tests` (`Avalonia.Headless.XUnit`), `TmDq.Core` from 2.3, all in the solution. Version
  single-sourced from `AppVersion` (generated into the assembly, never typed twice). The UTF-8
  code-page manifest merged into the exe with `mt.exe` (the SDK's own manifest setting does not reach
  an apphost; found in the spike).
- [ ] **3.2 CI for the new app:** build and test on the self-hosted runner (NETWORK SERVICE, Session 0)
  and the hosted fallback. This is where the one open question from the spike closes: the UI tests
  run as the runner's own account.
- [ ] **3.3 Packaging prototype:** self-contained publish, Inno Setup installer and portable ZIP,
  signed. **Every unsigned assembly must be signed** (21 Avalonia DLLs plus our own): a user machine
  with Smart App Control on blocks unsigned code with error 4551, exactly as the Sandbox did. Test a
  single-file publish as the alternative. Verified with `deploy/sandbox/` on a clean machine with
  Smart App Control on.
- [ ] **3.4 A walking skeleton:** open a `.ch10`, process one stream with defaults, show the lock %
  series. Ugly is fine; it proves the whole path end to end in the new app, in CI, and in the
  installer.

## Step 4: feature parity, in the order a user meets the features

Each step ports one area with its tests rewritten on the headless platform (measure widgets, drive
input, render pixels; see the `verify-ui-change` skill for what each catches), and each acceptance
criterion in `docs/CLAUDE.md` for the stories it covers gets a test or a recorded manual check.

- [ ] **4.1 Configure Streams** and its three setup dialogs, TOML load and save, Apply to all,
  validation (US1.0, US2.0 to US2.2, US7.0).
- [ ] **4.2 Processing:** progress dialog, cancel, the log sidebar with its colouring and the
  multi-line messages (US1.0, US3.0 to US3.2).
- [ ] **4.3 The chart** (per D1): envelope and culling, dual axes with hiding, round clock ticks,
  wheel zoom, drag pan, band zoom, zoom history, crosshair and pinned readout, backing-store
  behaviour, the interval band (US3.x, US4.0). Port `TestTmChart`'s cases, including the dropout and
  stale-cache ones.
- [ ] **4.4 Around the chart:** legend overlay (drag, cap, scroll, translucency), chip bar, context
  menu and its advertised shortcuts, keyboard shortcuts scoped to the chart, Customize View dialog
  (US4.0, US4.1).
- [ ] **4.5 Export and import:** CSV, PNG/SVG/PDF with the legend composited, log export, CSV import
  with its rejections (US6.0 to US6.3). Oracle: byte-identical CSV against the Qt app on every
  fixture.
- [ ] **4.6 Calibration:** Extract Calibration dialog, clips, the summary report and its warnings,
  preloaded step file (US5.0 to US5.3).
- [ ] **4.7 Templates and batch:** Save as Template, Apply Template, Plot File selector, per-file
  export (US1.1). Templates written by the Qt app must load unchanged.
- [ ] **4.8 The shell:** frameless chrome with **Windows 11 snap layouts** (unverified in Avalonia;
  verify first), hamburger menu with sections and Help submenu, recent files, drag and drop, light and
  dark themes with native frames (US9.0), the sidebar toggle, and preferences. **Existing users'
  preferences migrate:** read the Qt app's `QSettings` values (registry, or the portable ini) on first
  run.
- [ ] **4.9 Help:** both manuals (base compiled in, full optional), About. The manual's figures are
  re-captured from the new app; `build_manual.ps1` grabs Qt widgets today and needs a new capture
  path.

## Step 5: switch over

- [ ] **5.1 Parity review:** every acceptance criterion in `docs/CLAUDE.md` checked against the new app,
  every Qt UI test either ported or recorded as deliberately dropped with a reason.
- [ ] **5.2 Side-by-side soak:** the same recordings processed in both apps, CSV exports compared
  byte for byte, plots compared by eye, across every fixture and the full PRN recording.
- [ ] **5.3 Release v3.0.0** (D4) with the `cut-release` skill updated for the new build: installer,
  portable ZIP, signing of every assembly, clean-machine Sandbox test, release notes that say what
  moved and how preferences carried over.

## Step 6: retire Qt

- [ ] **6.1 Delete the Qt application:** `src/view`, `src/viewmodel`, their headers, the Qt Test UI
  suites, `tmDataQualityAnalyzer.pro` and `tests/tests.pro`, `qtbridge.h`, `QtProcessingEvents`.
  Clean build from an empty tree, because a deletion is only verified by one (`CLAUDE.md`).
- [ ] **6.2 Remove Qt from the tooling:** `env.ps1`, `deploy_qt_local.ps1`, `gen_compile_flags.py`,
  `gen_vs_project.ps1`, the CI Qt install, `QT_QPA_PLATFORM`, and the skills that describe the Qt
  build (`build-and-test`, `verify-ui-change`, `write-qt-test`, `add-stream-setting`, `cut-release`).
- [ ] **6.3 Rewrite `CLAUDE.md` and `docs/CLAUDE.md`** for the new architecture, keeping the domain
  invariants (they are about telemetry, not Qt). Delete this file.

## Risks worth watching

- **The chart is where the time goes.** `tmchart.cpp` encodes three sessions of performance findings;
  D1 and step 4.3 decide whether they carry over. Measure before believing.
- **Two apps reading one set of files.** Templates, TOML and CSV written by either must load in the
  other until step 6. Rule 5 is the guard.
- **Smart App Control on user machines.** Any unsigned assembly in a release blocks the app on those
  machines with no useful message. Step 3.3 makes signing every assembly part of the release.
- **Snap layouts with custom chrome** are unverified in Avalonia; the Qt app's `WM_NCHITTEST` code does
  not transfer literally. Prove it early in 4.8, or keep the native frame.
- **Preferences migration** is easy to forget and visible to every existing user on day one.
