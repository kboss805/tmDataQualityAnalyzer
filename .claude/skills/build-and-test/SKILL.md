---
name: build-and-test
description: >-
  Build and test tmDataQualityAnalyzer - the native core (tmdq_core.dll), the C# library over it and
  the Avalonia application - on this Windows machine. Use this WHENEVER you need to compile anything,
  run a test suite, check a change builds cleanly with zero warnings, reproduce a build error, or
  confirm the suites are still green - even if the user just says "build it", "run the tests", "does
  this compile?" or "make sure it's green". There are three suites with three scripts, each against
  its own baseline; the managed and app suites run against a DLL the native build produced, and a
  stale one, a stale binary or a filter that matches nothing all produce results that look real and
  are not. This skill encodes the order and the traps.
---

# Build and test — tmDataQualityAnalyzer

Three layers, each built and tested by its own script, from the repository root, in **PowerShell**.
The scripts import Visual Studio 2022 themselves (`scripts\env.ps1`) when `msbuild` or `mt` is not
on PATH; nothing needs to be set up first. The baselines are in `.claude/CLAUDE.md` — read them
there, not from memory.

## The order

```powershell
# 1. The native core. Everything else loads this DLL.
powershell -ExecutionPolicy Bypass -File scripts\build_core_dll.ps1 -Configuration Release

# 2. Its doctest suite (builds native\tmdq_core_tests, zero warnings, no Qt import, then runs it).
powershell -ExecutionPolicy Bypass -File scripts\test_native.ps1 -Configuration Release

# 3. The C# library's tests, against native\out\Release\tmdq_core.dll.
powershell -ExecutionPolicy Bypass -File scripts\test_dotnet.ps1 -Configuration Release

# 4. The application's tests, on Avalonia's headless platform, against the same DLL.
powershell -ExecutionPolicy Bypass -File scripts\test_app.ps1 -Configuration Release
```

CI runs all four in Release, in every job. Use the same configuration for the DLL and the suites
that load it: the managed and app scripts take `native\out\<config>\tmdq_core.dll` (or
`TMDQ_CORE_DLL`), and a Debug suite against a Release DLL built an hour ago tests the hour-old core.

**Zero warnings is the rule everywhere:** `build_core_dll.ps1` and `test_native.ps1` fail on one, and
every .NET project treats warnings as errors.

## Iterating

- **Native, filtered:** `scripts\test_native.ps1 -tc="TomlDocument*"` (any doctest argument passes
  through); `-NoBuild` reruns the last build.
- **Managed or app, filtered:** run the test executable directly after the script has built it:
  `dotnet\TmDq.App.Tests\bin\Release\net10.0\TmDq.App.Tests.exe -class TmDq.App.Tests.ShellTests`
  (or `-method "*Name*"`), with `TMDQ_CORE_DLL` set to the DLL. **Check the summary's `Total:`** — a
  filter that matches nothing runs zero tests, and a run of nothing must not be read as a pass.
- **The soak** (`scripts\run_soak.ps1`) after a change to processing, the plot's series or the CSV
  writer: it holds the app's exports to v2.14.2's bytes on the full and local recordings, so it runs
  here, never in CI.
- **The throughput guard** (`scripts\run_throughput_bench.ps1`) at release, or after a change to the
  pipeline's threading: it needs the full PRN recording.

## The traps

- **A deleted or renamed header is verified only by `-Rebuild`.** An incremental build keeps the
  objects that still include it. Wipe `native\obj` (or pass `-Rebuild`) before believing a result.
- **The binary must be newer than the source.** A failed link can leave the previous executable in
  place and a script can print a stale result; after a build, check the test exe's time, and for the
  app or managed suite that the DLL it loads was rebuilt too.
- **`Platform=x64`.** `vcvars` sets it, and a `dotnet build` in that shell then looks for a solution
  platform that does not exist. The scripts clear it; clear it yourself (`$env:Platform = $null`)
  before calling `dotnet` by hand in a shell that sourced `env.ps1`.
- **An IDE's design-time build shares `obj` and `bin`.** VS Code's C# Dev Kit builds the projects in
  the background, and two builds racing over one `obj` fail or test the wrong thing. Run long loops
  (mutation checks) in a worktree: `git worktree add C:\tmp\<name>`.
- **Worktrees need the recordings:** junction `tests\data` to the main checkout's, or the suites that
  read the full recordings have nothing to read.
- **The app suite's preferences:** a test never touches `%APPDATA%`; when running the built app by
  hand, put a file named `portable` beside the exe first, or it reads and writes your installed
  copy's `preferences.json`.

## Running the application

```powershell
$env:Platform = $null; dotnet build dotnet\TmDq.App -c Debug
dotnet\TmDq.App\bin\Debug\net10.0\tmDataQualityAnalyzer.exe "tests\data\test files\prn_testfile, sample.ch10"
```

The build copies `native\out\Debug\tmdq_core.dll` and the repository's `settings\` beside the exe
(build the DLL first). With `TMDQ_SMOKE_REPORT=<file>` it skips Configure Streams, processes the
first channel with the defaults, writes one line and exits.

## Reporting

Report each suite's numbers against its baseline, warnings included. A failure is reported with its
output, not summarised away; a skipped suite is said to be skipped.
