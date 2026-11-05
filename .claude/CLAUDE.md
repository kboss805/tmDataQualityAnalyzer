# tmDataQualityAnalyzer — Claude Code guide

Windows desktop app for analyzing data quality of IRIG 106 Chapter 10 PCM telemetry: per-stream **Frame Sync
Lock** stats and **Receiver AGC/SNR**, plotted vs. time. **Avalonia 12 on .NET 10** (`dotnet/`, MVVM) over a
C++20 single-reader / parallel-worker processing core built as `tmdq_core.dll` (`include/`, `src/model/`, `native/`).
The Qt application shipped up to v2.14.2 and was deleted in plan step 6.1. **Current version: 3.0.0.**

> **Full reference:** `docs/claude.md` is the canonical, detailed guide (user stories, architecture,
> conventions, test catalog). Read it when you need depth. This file is the quick orientation that
> loads every session — keep it lean. It lives in `.claude/` beside the settings and skills, which
> Claude Code loads exactly as it would a root `CLAUDE.md`; the root `.gitignore` re-includes it
> (`!.claude/CLAUDE.md`), since everything else under `.claude/` is ignored.

## Build & test

Everything is in `tmdq.sln`. The scripts find MSVC themselves: they dot-source `scripts\env.ps1` (which imports
`vcvars64.bat`) when `msbuild` or `mt` is not on PATH. Use the **PowerShell** tool.

- **Native core DLL (no Qt):** `powershell -ExecutionPolicy Bypass -File scripts\build_core_dll.ps1
  [-Configuration Debug|Release] [-Rebuild]` builds `native/tmdq_core/` (MSBuild) into
  `native/out/<config>/tmdq_core.dll`, then fails on any compiler warning or on an import outside a short
  allow-list (the C runtime, `KERNEL32`, `WS2_32`, `USER32`). CI runs it for Debug and Release. **A new core source
  goes in `tmdq_core.vcxproj`**, and a new ABI source (`native/tmdq_core/*.cpp`) in it AND in
  `native/tmdq_core_tests/tmdq_core_tests.vcxproj` (`test_abi.cpp` compiles the ABI into the native test executable
  and calls it as a host would); `NativeProjectTests` (managed suite) fails until every `src/model` source, ABI source
  and test file is listed. **A change that deletes or renames a header is verified only by a `-Rebuild`.**
- **Native core tests (doctest, no Qt):** `powershell -ExecutionPolicy Bypass -File scripts\test_native.ps1
  [-Configuration Debug|Release] [-Rebuild] [-NoBuild] [<doctest args>]` builds `native/tmdq_core_tests`
  (MSBuild; the framework is the single header in `lib/doctest`) with **zero warnings and no Qt import**, then runs it.
  Run it **filtered** while iterating: `-tc="TomlDocument*"`. Green baseline **485 test cases** (1 skipped: the
  benchmark below). The suites read `tests/data/test files` (committed recordings) and `tests/fixtures`; in a
  worktree, junction `tests/data` to the main checkout's or the full-recording tests have nothing to read.
  **The throughput guard:** the heavy PRN benchmark ("PipelineRunner throughput", `test_pipelinerunner.cpp`) is
  skipped unless `TMDQA_RUN_HEAVY_BENCH=1`, because it walks a 640 MB recording nine times. At release
  `scripts\run_throughput_bench.ps1` builds the native tests in Release, runs them all, then runs that case **pinned to
  the performance cores** (absolute times were bimodal ~1.9x on this hybrid CPU) and **asserts** the parallel-vs-solo
  ratios: the decommutation pipeline's only performance guard. It needs `tests/data/test files/prn_testfile,
  full.ch10` (gitignored); the committed sample carries data for 2 of the 4 channels it times and cannot stand in.
- **Managed library and its tests (.NET 10):** `powershell -ExecutionPolicy Bypass -File scripts\test_dotnet.ps1
  [-Configuration Debug|Release] [-NoBuild]` builds `dotnet/TmDq.Core` (the C# binding of the DLL's C ABI) and
  `dotnet/TmDq.Core.Tests` (xunit v3, warnings are errors) and runs the test exe against
  `native/out/<config>/tmdq_core.dll` (build that first; `TMDQ_CORE_DLL` overrides). The tests run the
  committed recordings through the library and compare with numbers v2.14.2's pipeline produced. An xunit v3
  project is an executable; the build merges a UTF-8 code-page manifest into it with `mt.exe`, because the vendored
  irig106 opens files with a narrow `open()` that reads the active code page. Green baseline **360 passed / 0
  failed / 1 skipped** (the skip is the probe `IsolatedLoadTests` runs in a child process). CI runs it.
- **The app:** `dotnet/TmDq.App` (Avalonia 12, CommunityToolkit.Mvvm) and `dotnet/TmDq.App.Tests` (xunit v3 on
  `Avalonia.Headless.XUnit`), both in `tmdq.sln`, which VS Code opens by default (`dotnet.defaultSolution`).
  `powershell -ExecutionPolicy Bypass -File scripts\test_app.ps1 [-Configuration Debug|Release] [-NoBuild]` builds the
  app with its tests and runs them against `native\out\<cfg>\tmdq_core.dll` (build it first; `TMDQ_CORE_DLL`
  overrides). The app: Open, or a recording path on the command line, then Configure Streams and every checked stream
  processed; with `TMDQ_SMOKE_REPORT=<file>` it skips the dialog, runs the first channel with the defaults, writes one
  result line and exits. The repository's `settings/` is copied beside the exe (the app root). Its preferences are
  `%APPDATA%\tmDataQualityAnalyzer\preferences.json` (beside the exe in a `tmDataQualityAnalyzer` folder when a
  `portable` file sits there), filled once from v2.x's INI there on the first run: to run a dev build without touching
  yours, put a `portable` file beside it. Its manuals are `resources\usermanual.html` (compiled in) and
  `UserManual.html` (the full one, with `docs\manual\walkthroughs.html`), their figures rendered from the app by
  `ManualFiguresTests` into `docs\manual_images`: after a change to what a figure shows, rebuild with
  `powershell -ExecutionPolicy Bypass -File scripts\build_manual.ps1` (`-NoCapture` reuses the figures). CI runs the
  app suite. Green baseline **604 passed / 0 failed / 0 skipped**, and **34 not run**: the soak's explicit cases.
  **The side-by-side soak (plan step 5.2):** `powershell -ExecutionPolicy Bypass -File scripts\run_soak.ps1 [-NoBuild]
  [-Only <run>,...]` runs every run in `dotnet\TmDq.App.Tests\Soak\soak.json` (`SoakTests`, explicit, one process a
  run) and fails unless each CSV export is byte for byte what v2.14.2 exported (the manifest's `qt` blocks). Most
  runs read the full and local recordings under `tests\data`, which are not committed, so CI never runs it, and a
  missing recording fails. Run it after a change to processing, the plot's series or the CSV writer.
  **Never type a version in a .csproj**: import
  `dotnet/build/AppVersion.props`, which reads `AppVersion`. **Every .NET project imports
  `dotnet/build/PackageLicences.targets`** (plan D5): a package (transitive ones too) outside the open-source allow-list
  in `scripts/check_package_licences.ps1` fails the build, and a licence *file* passes only when its SHA-256 is in that
  script's `$Reviewed` table, so read it before adding it. That script runs under Windows PowerShell 5.1 inside builds
  started from PowerShell 7, which inherits 7's `PSModulePath`: avoid cmdlets from script modules (`Get-FileHash` fails).
- **The release package:** `powershell -ExecutionPolicy Bypass -File
  deploy\avalonia\build_avalonia_package.ps1 [-SignCertSha1 '']` publishes **single-file** (every managed assembly
  inside our exe, so the certificate signs only our own code: the exe and `tmdq_core.dll`; it refuses to sign any other
  unsigned file), adds the VC++ runtime DLLs `tmdq_core.dll` imports, generates `THIRD-PARTY-NOTICES.txt`
  (`scripts\gen_third_party_notices.ps1`, from the publish's deps file), and builds the portable ZIP and the Inno
  installer (`deploy\avalonia\tmDataQualityAnalyzer.iss`: v2.x's AppId and folder, so it upgrades a v2.x
  install in place, deleting its `bin\`, keeping `settings\`, and taking the `.ch10` association). It runs
  `check_release_consistency.ps1 -ReleaseMode` first. **Unlock SimplySign first.** Clean-machine test under Smart App
  Control, which installs v2.14.2 and upgrades it: `deploy\sandbox\run_sandbox_test.ps1 -Setup <setup.exe> -Payload
  "<zip>;deploy\avalonia\staging\unsigned;tests\data\test files\prn_testfile, sample.ch10;deploy\tmDataQualityAnalyzer-v2.14.2_setup.exe"`
  (semicolons: recording names contain commas; v2.14.2's setup is its GitHub release's asset) (an unsigned build must
  be BLOCKED, or the pass means nothing). The UTF-8 manifest is merged into every SDK host (`apphost`,
  `singlefilehost`, `apphost_publish`): patching `bin\` misses a single-file publish.
- **Mutation check (plan rule 3):** `powershell -ExecutionPolicy Bypass -File scripts\mutate.ps1 -List <mutants.json>
  [-Only <id>,...] [-Parts 2 -Part <k>] [-Out <file>]`. For each exact-text mutant in a JSON list it applies the
  change, builds, runs one suite (`native`, `managed` or `app`, with the test exe's arguments) and restores the file
  byte for byte. The verdicts are CAUGHT, SURVIVED, COMPILE, or one saying nothing ran. It refuses the main checkout
  (run it from a `git worktree add C:\tmp\<name>`) and needs a green baseline first. A mutant's `expect` makes a kept
  list a regression check. The list format and the traps it guards are in the script's header;
  `scripts\mutants\example.json` is the sample.

**IntelliSense:** clangd reads a generated, gitignored `compile_flags.txt` — run `py scripts/gen_compile_flags.py`
after a fresh clone. **Run it inside each worktree too**: the paths are absolute, and a worktree otherwise silently
resolves headers from the main checkout while still reporting zero errors (see `docs/claude.md` → clangd /
IntelliSense).

**Toolset is pinned to Visual Studio 2022 (MSVC 14.44, v143).** `env.ps1` asks vswhere for
`[17.0,18.0)` rather than `-latest`; installing VS 2026 had silently moved every build to
MSVC 14.51 and left two compilers' objects in one tree. Override with `TMDQ_VS_RANGE`
(a vswhere range). **After changing the compiler, rebuild the core with `-Rebuild`.**

## Migration: Qt to Avalonia, finishing

**Read `docs/dotnet-migration/avalonia-plan.md` before migration work**: one PR per numbered step, ticked as it merges.
v3.0.0 (step 5.3) released the Avalonia app; step 6.1 deleted the Qt one and 6.2 its tooling. What is left: the Qt
names in code that stays (6.3), and consolidating the new code (6.4).

## Project skills (`.claude/skills/`)

`build-and-test` · `write-test` · `add-stream-setting` · `verify-ui-change` · `tm-code-review` · `cut-release` ·
`code-cleanup`. Invoke explicitly (`/build-and-test`) or let them trigger by intent.

## Hard rules

- **Never edit** `lib/irig106/**` (third-party; deny-listed in
  `.claude/settings.json`). Adapt in the core (`chapter10reader.cpp`, `ch10packetreader.cpp`,
  `frameprocessor.cpp`, …) instead.
- **Version is single-sourced** in `AppVersion` (`include/constants.h`); `dotnet/build/AppVersion.props`, the package
  script and the manuals read it. Never type a version anywhere else.

## Domain invariants (silent-bug territory — see `tm-code-review` skill & `docs/*_logic.md`)

- **Ch10 paths:** both readers pass irig106 the path in the process code page (`path.string()`),
  never as UTF-8 bytes - its narrow `open()` reads that code page, which the UTF-8 code-page manifest
  merged into the app and every test host (`dotnet/TmDq.App/app.manifest`, `resources/utf8codepage.manifest`) makes UTF-8.
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
