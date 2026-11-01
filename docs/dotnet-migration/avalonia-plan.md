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
- **D5. Avalonia's tooling licence. DECIDED (2026-10-29): no licensed Avalonia tooling.** The
  framework is MIT, and MIT combines with this project's GPL-3.0, so the licence stays GPL-3.0. The
  licensed part is the tooling (the new Visual Studio extension with its previewer, DevTools, Parcel,
  Hot Reload) and the Accelerate UI components. We use neither:
  - **Editing** is done in VS Code with Avalonia's free extension, or in Rider (which understands
    `.axaml` itself; the MIT-licensed AvaloniaRider plugin adds a live previewer). Avalonia's own FAQ
    says both "remain freely available". **Not** the legacy open-source Visual Studio extension: its
    repository (`AvaloniaUI/AvaloniaVS`) was archived on 2026-09-24 and points to Accelerate, so it
    can still be built from source but will not follow Avalonia 12. Visual Studio stays usable as an
    editor and debugger, without an Avalonia previewer. The build needs no tooling at all, so CI is
    unaffected either way.
  - **No Accelerate component is ever referenced** (its TreeDataGrid, media player, Markdown viewer,
    rich-text editor, PDF viewer, charts). They are proprietary, a GPL-3.0 application cannot ship
    them, and the free Community tier gives no right to redistribute them anyway. The chart is our
    own Skia drawing (D1), so nothing in the plan needs one. Step 3.1 adds a check that enforces this.
  - Rejected: the free Community licence (account, always-on telemetry, workstation-only, re-attested
    yearly; terms Schedule 1D v1.2, 30 October 2025) and Plus (EUR 299 per seat per year).

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
- [x] **2.4 Port the core test suites off Qt Test** (D2), one suite per PR, old and new running side
  by side until the new one has every case. Order, smallest first: `TestTomlDocument`,
  `TestTextFormat`, `TestFrameSync`, `TestEventReporter`, `TestPacketQueue`, `TestIntervalGate`,
  `TestFrameSetup`, `TestStepDetector`, `TestCh10PacketReader`, `TestChapter10Reader`,
  `TestFrameProcessor`, `TestPipelineRunner`, `TestCalibrationExtractor`, and the suites for the code
  moved in step 1. Each ported suite is mutation-checked against the same mutants as the original;
  then the Qt Test version is deleted. The throughput benchmark moves with `TestPipelineRunner`.
  The tests live in `native/tmdq_core_tests` (doctest, vendored in `lib/doctest`; MSBuild, no Qt;
  `scripts/test_native.ps1`; a CI step in every job, and the `core-without-qt` job (#213 `ac8c67b`) builds and runs them,
  with the DLL and the managed tests, on a hosted runner that has never had Qt and fails if it finds
  any), and `TestCoreQtFree` holds the project to core sources and no Qt. **Ported so far:** `TestTomlDocument` (#190 `10af53e`, which also brought the framework), `TestTextFormat` (#191 `9e70e44`), `TestFrameSync` (#192 `67752db`), `TestEventReporter` (#193 `8da2345`), `TestPacketQueue` (#194 `64c9cb5`), `TestIntervalGate` (#195 `9405758`), `TestFrameSetup` (#196 `9701d54`), `TestStepDetector` (#197 `e7edd88`), `TestCh10PacketReader` (#198 `f2bedfd`), `TestChapter10Reader` with `TestChannelData` (#200 `3912bf8`), `TestFrameProcessor` (#201 `c779afe`), `TestPipelineRunner` (#202 `5d1ef5a`, which brought the throughput benchmark with it), `TestCalibrationExtractor` (#203 `38446d9`), `TestTemplateMatcher` (#204 `656cfa0`), `TestCalibrationRequest` (#205 `681f6da`), `TestSeriesColumnSchema` (#206 `a196a06`), and the core halves of `TestFrameSyncFile` (#207 `28b8e5c`), `TestCsvSeries` (#208 `af48cb9`), `TestSeriesBuild` (#209 `73719cc`) and `TestStreamJob` (#210 `d642a7e`), and the core half of `TestConstants` (#214 `9b8f554`). The four suites left (`TestFrameSyncFile`, `TestCsvSeries`, `TestSeriesBuild`, `TestStreamJob`) drive the core through a dialog or a ViewModel, so each is **split**: the core's cases move here with expected values worked out for the core (or the answers the Qt code gave, recorded as digests), and the Qt-bound cases stay in a trimmed Qt suite until the switch-over. All four are split, and so is `TestConstants` (the Qt-free `constants.h` native, the Qt `plotconstants.h` left in a one-case Qt suite). The last core suite, `TestAbi` (#216), is ported too: it tests the C ABI, which is core, and used Qt only as its harness, so the native tests now compile the ABI sources in and call `tmdq_*` as a host would. (An earlier version of this note called `TestConstants` and `TestAbi` "deliberately Qt"; they were not.) Qt on purpose, and staying until the switch-over or step 6: `TestStreamConfigSchema` and `TestProcessingTemplateSchema` (the template JSON goes to C#, D3), `TestQtProcessingEvents` (the Qt bridge), and `TestCoreQtFree` (the guard over the Qt build's own source lists).

**Done when** the core and its tests build and pass with no Qt installed, and CI runs them.

## Step 3: the Avalonia app's skeleton and pipeline

- [x] **3.1 Projects and conventions:** `TmDq.App` (Avalonia, MVVM with CommunityToolkit.Mvvm),
  `TmDq.App.Tests` (`Avalonia.Headless.XUnit`), `TmDq.Core` from 2.3, all in the solution. Version
  single-sourced from `AppVersion` (generated into the assembly, never typed twice). The UTF-8
  code-page manifest merged into the exe with `mt.exe` (the SDK's own manifest setting does not reach
  an apphost; found in the spike). A build check (D5) fails on any package outside an allow-list of
  open-source licences, so an Accelerate component cannot arrive through a NuGet suggestion.
  PR #217. The version comes from `dotnet/build/AppVersion.props` (all four projects' assemblies, and the exe's
  version resource); the licence check is `dotnet/build/PackageLicences.targets` over
  `scripts/check_package_licences.ps1`, imported by every .NET project, allow-list MIT, Apache-2.0, BSD-2-Clause and
  BSD-3-Clause, with a licence file accepted only by its reviewed SHA-256 (one so far: ANGLE's BSD-3-Clause text in
  `Avalonia.Angle.Windows.Natives`, which `Avalonia.Desktop` brings). The app and its tests build locally only;
  CI is 3.2.
- [x] **3.2 CI for the new app:** build and test on the self-hosted runner (NETWORK SERVICE, Session 0)
  and the hosted fallback. This is where the one open question from the spike closes: the UI tests
  run as the runner's own account.
  PR #221. **Closed:** on the self-hosted runner the app tests ran as `NT AUTHORITY\NETWORK SERVICE`, Windows
  session 0, not interactive, and all 36 passed, including one that renders the main window through Skia and reads
  its pixels (the spike had only run them as SYSTEM in a Sandbox). `scripts/test_app.ps1` prints that line in every
  run; CI runs it after the managed tests in all three jobs, so `core-without-qt` also proves the app needs no Qt.
- [x] **3.3 Packaging prototype:** self-contained publish, Inno Setup installer and portable ZIP,
  signed. **Every unsigned assembly must be signed** (21 Avalonia DLLs plus our own): a user machine
  with Smart App Control on blocks unsigned code with error 4551, exactly as the Sandbox did. Test a
  single-file publish as the alternative. Verified with `deploy/sandbox/` on a clean machine with
  Smart App Control on. Both packages carry the third-party licence notices (Avalonia, SkiaSharp,
  Skia, CommunityToolkit.Mvvm, the .NET runtime), which the GPL-3.0 distribution owes them.
  PR #222. **Done, and single-file is the answer, not the alternative.** Measured: a self-contained folder publish ships 27
  unsigned PE files (22 Avalonia assemblies, MicroCom.Runtime, Tmds.DBus.Protocol, our 3), and signing them would put
  this project's publisher identity on third-party code. A single-file publish bundles every managed assembly into our
  exe, and the three native libraries beside it are already signed by their publishers (Microsoft for SkiaSharp and
  HarfBuzz, AvaloniaUI OU for ANGLE), so the certificate signs only our own exe and `tmdq_core.dll`.
  `deploy/avalonia/build_avalonia_package.ps1` builds the signed installer and portable ZIP as a **preview installed
  beside the Qt app** (its own AppId and folder, no `.ch10` association) and refuses to sign any other unsigned file;
  `scripts/gen_third_party_notices.ps1` writes the notices from the publish's deps file (20 packages, and the irig106
  code inside `tmdq_core.dll`). Clean Windows 11 Sandbox, Smart App Control on: the unsigned build is blocked (Code
  Integrity event 3077), the signed packages install, run, load the native core and call it, and nothing of theirs is
  blocked. Found on the way: a single-file publish had shipped without the UTF-8 manifest (it bundles its own host from
  `obj\`), and `tmdq_core.dll` needs the VC++ runtime beside it (`MSVCP140`, `VCRUNTIME140*`), which a clean Windows
  lacks. Statically linking the CRT into the DLL would drop those three files; that is a core-build decision left open.
- [x] **3.4 A walking skeleton:** open a `.ch10`, process one stream with defaults, show the lock %
  series. Ugly is fine; it proves the whole path end to end in the new app, in CI, and in the
  installer.
  PR #223. **Done, in all three.** The app opens a recording (Open, or a path on the command line), processes its first PCM
  channel with the Qt app's Frame Sync Lock defaults (PRN15: `334AABBF`, 32767 bits, 1 s) through `TmDq.Core` and
  `tmdq_core.dll`, and draws the lock % series (`walking-skeleton.png`, beside this plan). The committed PRN sample's
  channel 13 gives 116 windows, mean 93.531893 %, the numbers the managed tests already pin, and the app's series is
  compared with the core's own run of the same settings. **In the app:** 74 headless tests. **In CI:** the same tests in
  all three jobs, `tmdq_core.dll` beside the test exe. **In the installer:** with `TMDQ_SMOKE_REPORT` set, the app writes
  one line for a recording on its command line and exits; the packaging script runs the staged app that way, and the
  clean Windows 11 Sandbox (Smart App Control on) has both the installed and the portable app report
  `ok samples=116 mean=93.532 channel=13`.

## Step 4: feature parity, in the order a user meets the features

Each step ports one area with its tests rewritten on the headless platform (measure widgets, drive
input, render pixels; see the `verify-ui-change` skill for what each catches), and each acceptance
criterion in `docs/claude.md` for the stories it covers gets a test or a recorded manual check.

- **4.1 Configure Streams** and its setup dialogs, TOML load and save, Apply to all, validation (US1.0, US2.0 to
  US2.2, US7.0), in two PRs. The Extract Calibration dialog and Apply Cal, which the Qt Receiver SNR dialog opens, are
  4.6's: they are calibration (US5.3), and they arrive with the rest of it. Four Qt behaviours are deliberately not
  ported: Process was enabled once any checked row was ready and ran the others on whatever they held (now every
  checked row must be ready); a Mode change left the row green holding the other mode's frame sync (now it is
  unconfigured); opening a Receiver SNR row in Frame Sync Lock cleared its receiver-parameters file on a copy that was
  then discarded (now the clear sticks); and calibration extracted with the first time channel rather than the one
  selected (fixed in 4.6).
  - [x] **4.1a The rules, without a window** (PR #224): the receiver-parameters file through the C ABI (`tmdq_receiver_params_read`
    / `_save`, `tmdq_words_in_minor_frame`, ABI revision 1) and `TmDq.Core` (`ReceiverParamsFile`, `FrameGeometry`), so
    the app reads and writes it through the core's one implementation (rule 5); and in `TmDq.App/Streams`, a row's
    settings (`StreamSetup`), the defaults a row and a mode change start from (`StreamDefaults`), the dialog's rules
    (`StreamRules`: readiness, Process, a Mode change, what the dialog opens with and what OK keeps, Apply to all, the
    field checks, the run) and the Load and Save buttons (`StreamFiles`).
  - [x] **4.1b The dialogs** (PR #225, merged as `d87ba23`): Configure Streams, the Frame Sync Lock and Receiver SNR setup dialogs, wired into the
    app's Open in place of the walking skeleton's first-channel defaults.
    **Done:** Open (or a recording on the command line) now asks through Configure Streams (`configure-streams.png`,
    `receiver-snr-setup.png`, beside this plan), processes every checked stream in one run, and draws every Frame Sync
    Lock stream (Receiver SNR results are counted in the status line; drawing them is 4.3's). One setup window serves
    both modes. A problem is shown in the dialog as it arises and holds OK, where the Qt dialogs raised a message box on
    OK. The app now ships `settings/` beside its exe, and the preview installer installs it as the Qt one does: never
    overwritten, left by an uninstall. The installer's smoke test, which has no one to answer the dialog, still runs
    the first channel with the defaults. **Each acceptance criterion** of the stories covered, with what holds it:
    - US1.0: every criterion has a test, except the progress dialog and cancelling a run, which are 4.2's.
    - US2.0, US2.1 and US2.2: every criterion has a test.
    - US7.0: every criterion has a test. "Pattern no larger than the frame" holds by the two ranges.
    The changelog entry maps each criterion to its test.
- [x] **4.2 Processing** (PR #226): progress dialog, cancel, the log sidebar with its colouring and the
  multi-line messages (US1.0, US3.0 to US3.2).
  **Done:** a modal progress dialog (processing-progress.png) whose Cancel only asks and stops every stream, and which
  cannot be closed or escaped while the run goes on; the log sidebar left of the chart (processing-log.png), timestamped
  and coloured by the Qt rules, keeping a message's own lines, saying what the Qt log says in its order. Every
  criterion of US1.0 now has a test, and so does US3.0's and US3.2's progress bar; the rest of US3.0 to US3.2 is the
  chart's (4.3), the setup dialog's (4.1b) or the core's. The sidebar's toggle is 4.8's, the log's export 4.5's.
- **4.3 The chart** (per D1): envelope and culling, dual axes with hiding, round clock ticks,
  wheel zoom, drag pan, band zoom, zoom history, crosshair and pinned readout, backing-store
  behaviour, the interval band (US3.x, US4.0). Port `TestTmChart`'s cases, including the dropout and
  stale-cache ones. In two PRs, the user's choice. The Qt `PlotWidget` (1,270 lines) is not refactored, since step 6
  deletes it; its jobs become separate pieces here instead: the chart control and a plot view model (ranges, zoom
  history, the readout's pin) in 4.3b, the legend, chip bar and context menu in 4.4, image export in 4.5.
  - [x] **4.3a The geometry, in the core** (PR #227): the transforms between data and pixels, a series' line (the
    samples in view only) and its per-pixel-column envelope (each column's mean, and the band from its minimum to its
    maximum, the "interval band"), and the time ticks, moved out of the Qt `TmChart` into the core's `chartgeometry`
    (decision D1). The Qt chart now draws from it, held bit for bit to its old code, which the native suite keeps as the
    oracle. Through the C ABI (revision 2) and `TmDq.Core` (`ChartGeometry`) for 4.3b.
  - [x] **4.3b The chart control** (PR #228): the Avalonia chart over that geometry: dual axes with hiding, the ticks and their
    labels, Receiver SNR on the right axis, the backing store, wheel zoom, drag pan, band zoom, zoom history, the
    crosshair and pinned readout, double-click to the full span; `TestTmChart`'s widget cases ported.
    **Done:** `PlotViewModel` holds the plot's state (series built by the core on one time base, the Qt colours to the
    shade, both axes' ranges and overrides, the view mode, the zoom history, the readout and its pin) and `PlotChart` draws
    it and takes every gesture, over a cached data layer (`plot-lock.png`, `plot-snr.png`). The run now keeps the core's
    results for the plot. Menus, legend and chips that drive the rest are 4.4's. US4.0's "highlighted style" for lock
    curves is not true of the Qt app either, and is flagged in the changelog rather than invented.
- [x] **4.4 Around the chart** (PR #229): legend overlay (drag, cap, scroll, translucency), chip bar, context
  menu and its advertised shortcuts, keyboard shortcuts scoped to the chart, Customize View dialog
  (US4.0, US4.1).
  **Done:** `PlotView` holds 4.3b's chart with the chip bar and the legend (`PlotLegend`) over it; the right-click menu
  in the Qt order, its prompts (`PlotCommands` over `IPlotPrompts`) and the keys it advertises, which act only while the
  chart has focus; Customize View (`CustomizeViewWindow` over `CustomizeViewModel`), applied only on OK
  (`plot-chips-legend.png`, `plot-menu.png`, `customize-view.png`). The colour dialog is Avalonia's `ColorView`, a new
  MIT package. The legend's state across sessions is 4.8's; export and the legend in images are 4.5's.
- [x] **4.5 Export and import** (PR #230): CSV, PNG/SVG/PDF with the legend composited, log export, CSV import
  with its rejections (US6.0 to US6.3). Oracle: byte-identical CSV against the Qt app on every
  fixture.
  **Done:** the Export dialog (`ExportWindow` over `ExportViewModel`), the image from one Skia routine for all three
  formats with the legend at its place (`PlotImageExport`), the CSV both ways through the core
  (`PlotViewModel.ExportCsv` and `LoadCsv`), the log as text, and Import CSV with the Qt messages and "Loading...";
  Import CSV and Export are top-bar buttons until 4.8's menu (`export-dialog.png`). The CSV matches the Qt
  application's byte for byte on seven runs over all five committed recordings. US6.0 and US6.1 reworded at the
  user's decision: the image is exactly what the chart shows, the CSV every series every time.
- [x] **4.6 Calibration** (PR #231): Extract Calibration dialog (opened from the Receiver SNR setup dialog) and Apply Cal, clips,
  the summary report and its warnings, preloaded step file (US5.0 to US5.3). Extraction uses the time channel selected
  in Configure Streams, not the recording's first (a Qt defect, decided in 4.1). A row already carries the calibration
  and its references from 4.1a, and Apply to all copies them.
  **Done:** Apply Cal and its label in the Receiver SNR setup dialog; Extract Calibration (`ExtractCalibrationWindow`
  over `ExtractCalibrationViewModel`) with the preloaded step file, the clips, the Qt status lines, the extraction
  through the core under the progress dialog, and the "Calibration Extracted" report (`extract-calibration.png`,
  `calibration-report.png`). The selected time channel and the recording's byte order reach the extraction; Apply Cal
  is unavailable, with its reason, rather than warning in a box.
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

- [ ] **5.1 Parity review:** every acceptance criterion in `docs/claude.md` checked against the new app,
  every Qt UI test either ported or recorded as deliberately dropped with a reason.
- [ ] **5.2 Side-by-side soak:** the same recordings processed in both apps, CSV exports compared
  byte for byte, plots compared by eye, across every fixture and the full PRN recording.
- [ ] **5.3 Release v3.0.0** (D4) with the `cut-release` skill updated for the new build: installer,
  portable ZIP, signing of every assembly, clean-machine Sandbox test, release notes that say what
  moved and how preferences carried over.

## Step 6: retire Qt

- [ ] **6.1 Delete the Qt application:** `src/view`, `src/viewmodel`, their headers, the Qt Test UI
  suites, `tmDataQualityAnalyzer.pro` and `tests/tests.pro`, `qtbridge.h`, `QtProcessingEvents`.
  Clean build from an empty tree, because a deletion is only verified by one (`.claude/CLAUDE.md`).
- [ ] **6.2 Remove Qt from the tooling:** `env.ps1`, `deploy_qt_local.ps1`, `gen_compile_flags.py`,
  `gen_vs_project.ps1`, the CI Qt install, `QT_QPA_PLATFORM`, and the skills that describe the Qt
  build (`build-and-test`, `verify-ui-change`, `write-qt-test`, `add-stream-setting`, `cut-release`).
- [ ] **6.3 Rewrite `.claude/CLAUDE.md` and `docs/claude.md`** for the new architecture, keeping the domain
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
