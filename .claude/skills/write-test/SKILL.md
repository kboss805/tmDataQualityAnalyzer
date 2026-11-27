---
name: write-test
description: >-
  Add or extend tests in tmDataQualityAnalyzer the right way, in whichever of its three suites the
  code under test belongs to: the native core's doctest suite, the C# library's xunit suite, or the
  Avalonia application's headless xunit suite. Use this WHENEVER you are writing new tests, adding a
  case to an existing suite, creating a test file for code that has none, or the user asks to "add
  tests", "improve coverage", "write a test for X" or "test this edge case". A native test file left
  out of its project never runs, a UI test written as a plain [Fact] crashes or tests nothing, and a
  test that cannot fail proves nothing - this skill encodes where each test goes, how it is wired,
  the helpers the suites already have, and how to prove the test can fail.
---

# Writing tests — tmDataQualityAnalyzer

Put the test in the suite that owns the code, and read a neighbouring test file before writing:
each suite has its house style and helpers. Build and run with the **build-and-test** skill.

| Code under test | Suite | Where |
| --- | --- | --- |
| The core (`src/core/include/`, `src/core/src/`) and the C ABI (`src/core/abi/`) | doctest | `tests/core/test_<name>.cpp` |
| The C# binding (`src/TmDq.Core`) | xunit v3 | `tests/TmDq.Core.Tests/<Name>Tests.cs` |
| The application (`src/TmDq.App`) | xunit v3 on `Avalonia.Headless.XUnit` | `tests/TmDq.App.Tests/<Area>/<Name>Tests.cs` (`Plot`, `Streams`, `Files`, `Shell` or `Build`; the namespace stays `TmDq.App.Tests`) |

## The native suite (doctest)

- **A case:** `TEST_CASE("<Suite>: <what it holds, as words>")` (`RepoFilesTests` holds the form, and that every `-tc=` filter of a kept mutant list still matches a case) with `CHECK`/`REQUIRE`, `INFO`
  for context in loops, `SUBCASE` sparingly. Doubles compare with an explicit tolerance or
  `doctest::Approx`.
- **A new file must be added to `tests/core/tmdq_core_tests.vcxproj`** (`<ClCompile
  Include="test_<name>.cpp" />`). MSBuild compiles only what the project lists, so a file left out is
  a suite that silently never runs. `NativeProjectTests` (the managed suite) fails until it is listed.
- **Files the tests read:** `recordingFile("<recording>")`, `repoFile("settings/...")`,
  `repoFile("tests/fixtures/...")` (`test_support.h`, which also holds `asciiOnly`, `utf16le` and the `step_cal` fixture,
  `loadStepCalFixture()`: add a helper there when a second test file needs it, never a second copy). A missing committed file is a failure, never a
  skip: the recordings are in the repository.
- **The ABI** is compiled into the test executable (the `test_abi_*.cpp` files call `tmdq_*` as a host would),
  so a new ABI function gets its native case there, and its struct layout a pin.
- **A seeded generator is drawn from one statement at a time.** C++ does not say which operand of `a() * x + b()` or
  which argument of `f(a(), b())` is evaluated first, so two draws in one expression give a sequence the compiler
  picks: Debug and Release differed, and an edit elsewhere in the function flipped Release, and a pinned hash then
  failed in one build only (`scenarioHash`, fixed in the entry "The pinned scan tests no longer depend on the order the
  compiler evaluates their draws in"). Draw into a named local, then combine. A draw in a ternary's branch is fine:
  the condition is sequenced before it.
- **Reimplementing or moving code:** keep the old implementation, restated over std types, as an
  oracle in the test, and compare the two over generated input (`docs/guide.md`, Working Method). Most suites here do;
  `test_chartgeometry.cpp` and `test_streamjob.cpp` are worked examples.

## The C# library's suite

- **`[Fact]` / `[Theory]` with `[InlineData]`**; `Env.EnsureLoaded()` before anything that calls the
  DLL, `Env.RepoRoot` for files. The suite runs against the real `tmdq_core.dll`, so a binding test
  is an end-to-end test of the ABI.
- **A struct the binding marshals** is pinned against `tmdq_sizeof`, so a field added on one side only
  fails loudly.
- **Numbers from the recordings** are compared with what v2.14.2's pipeline produced; a new expected
  number states where it came from.
- **A file the library just wrote or opened is read back and deleted through `TempFiles`** (`Delete`,
  `DeleteDirectory`, `ReadAllBytes`, `ReadAllText`; `tests/TmDq.Core.Tests/Support/TempFiles.cs`, linked into the
  app tests), never a bare `File.Delete` in a `finally`: a virus scanner's hold on a new file fails a test
  whose every check passes, and `TempFiles` retries for a few seconds yet still throws on a handle the
  library leaked. A scratch folder of a test that runs a build tool or a script goes through
  `TempFiles.TryDeleteDirectory`, which gives up quietly because a child process may still hold a file in it (a
  leftover folder in `%TEMP%` is no failure of the product): never an empty `catch` in a `Dispose`.

- **Expected values are literals, and no assertion sits within a timer tick of what it measures.** A test that builds
  its expectation from the constant it tests passes when the constant changes (pin the number once, in its own test);
  an elapsed-time bound within a `Task.Delay`'s 15.6 ms Windows tick of the delay fails a correct chart on a CI runner
  (hold it to 3/4 of a delay of 100 ms or more); and a stall too long to tell two cases apart is `Assert.Skip`, not a
  failure. Wait for the event a thread is about to produce (a log line, a callback), not for a fixed time or a poll with
  no limit.

## The application's suite (headless Avalonia)

- **A test that creates a control, a window or touches the dispatcher is an `[AvaloniaFact]`**, never a
  plain `[Fact]`: a control built off the headless UI thread crashes or silently does nothing.
- **Rules belong in view models, and are tested there** (`ConfigureStreamsViewModel`,
  `StreamSetupViewModel`, `ExportViewModel`, ...): most app tests need no window at all.
- **A window:** show it, run the dispatcher's jobs (`Dispatcher.UIThread.RunJobs()`), then read its
  controls or capture a frame (`Frames.Capture`, `Frames.Pixel`, `Frames.Near`, `Frames.CountDifferent`).
  `IsEffectivelyEnabled`, not `IsEnabled`, is what the user sees.
- **A menu:** read it open, through `Menus` (`Menus.Open(view)`, `Menus.Open(window)`, `Menus.Entries`,
  `Menus.Item`, `Menus.Headers`). The menus are declared in AXAML and bound: closed, an item has no view model and
  a list's items do not exist; a submenu's items follow their commands only once it is open, which `Menus.Entries`
  does as the user would. The plot's menu opens by a context request, as a right-click opens it:
  `ContextMenu.Open` alone skips its `Opening`.
- **Never await a modal dialog in a test.** Give the code under test a fake (`Support/StreamFakes.cs`, `Support/MainWindowFakes.cs`, `Support/Fixtures.cs`: use one there before writing another, and the
  `I*` services in `src/TmDq.App/Services`) and assert on what it was asked.
- **A test that changes a process-wide variable** (`TMDQ_CORE_DLL` pointing at a missing file) goes in
  `MissingCoreTests`' collection, which runs alone after the rest: no other class shares a collection, so the classes that
  load the native core run side by side (the suite went from 53 s to 38 s when they stopped queueing behind that one).
- **A test that only needs a recording that processes** takes `IClassFixture<ShortRecordings>` (the first 8 MB of the PRN sample, cut on a packet, made once for the class), not the whole 77 MB sample: two such classes went from 18 s to 2.3 s.
- **Files:** `Repo.Recording("<name>")`, `Repo.File("settings/...")` (both through `RepoPaths`, which the library tests
  use too). A temporary path is `TempFiles.NewPath("<stem>", ".ext")`, never composed inline; a window shown for a test
  is `Frames.Shown`. A test that writes a preferences
  file writes it to a temporary folder, never to `%APPDATA%`.

## Naming

Name a test for what it holds, as a sentence: `ATimeWindowIsClampedIntoTheRecordingWithAWarningForEachEnd`,
`TEST_CASE("ChartGeometry: a gap breaks the line and the band")`. The name is what a failure reports.

## Prove it can fail

A test that cannot fail proves nothing (`docs/guide.md`, Working Method). For each rule the new test holds, break the
code once and watch the test fail:

- by hand for one or two, restoring the file byte for byte after;
- for more, with `eng\mutate.ps1` and a JSON list of exact-text mutants, run from a worktree
  (`git worktree add C:\tmp\<name>`). Its header documents the list format; `eng\mutants\example.json`
  is the sample. A survivor is a missing test, not a shrug: close it, or record why it is equivalent.
  `eng\mutants_affected.ps1` names the kept lists a change reaches (`-Run` runs them), so a change does not re-run all 61.
