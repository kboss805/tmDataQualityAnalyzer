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
| The application (`src/TmDq.App`) | xunit v3 on `Avalonia.Headless.XUnit` | `tests/TmDq.App.Tests/<Name>Tests.cs` |

## The native suite (doctest)

- **A case:** `TEST_CASE("<Suite>: <what it holds, as a sentence>")` with `CHECK`/`REQUIRE`, `INFO`
  for context in loops, `SUBCASE` sparingly. Doubles compare with an explicit tolerance or
  `doctest::Approx`.
- **A new file must be added to `tests/core/tmdq_core_tests.vcxproj`** (`<ClCompile
  Include="test_<name>.cpp" />`). MSBuild compiles only what the project lists, so a file left out is
  a suite that silently never runs. `NativeProjectTests` (the managed suite) fails until it is listed.
- **Files the tests read:** `repoFile("tests/data/test files/<recording>")`, `repoFile("settings/...")`,
  `repoFile("tests/fixtures/...")` (`test_support.h`). A missing committed file is a failure, never a
  skip: the recordings are in the repository.
- **The ABI** is compiled into the test executable (`test_abi.cpp` calls `tmdq_*` as a host would),
  so a new ABI function gets its native case there, and its struct layout a pin.
- **Reimplementing or moving code:** keep the old implementation, restated over std types, as an
  oracle in the test, and compare the two over generated input (`docs/claude.md`, Working Method). Most suites here do;
  `test_chartgeometry.cpp` and `test_streamjob.cpp` are worked examples.

## The C# library's suite

- **`[Fact]` / `[Theory]` with `[InlineData]`**; `Env.EnsureLoaded()` before anything that calls the
  DLL, `Env.RepoRoot` for files. The suite runs against the real `tmdq_core.dll`, so a binding test
  is an end-to-end test of the ABI.
- **A struct the binding marshals** is pinned against `tmdq_sizeof`, so a field added on one side only
  fails loudly.
- **Numbers from the recordings** are compared with what v2.14.2's pipeline produced; a new expected
  number states where it came from.

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
- **Never await a modal dialog in a test.** Give the code under test a fake (`StreamFakes.cs` and the
  `I*` services in `src/TmDq.App/Services`) and assert on what it was asked.
- **Classes that load the native core** join `[Collection(NativeCoreCollection.Name)]`, which runs them
  one at a time (one of them points `TMDQ_CORE_DLL` at a missing file for a moment).
- **Files:** `Repo.Recording("<name>")`, `Repo.File("settings/...")`. A test that writes a preferences
  file writes it to a temporary folder, never to `%APPDATA%`.

## Naming

Name a test for what it holds, as a sentence: `ATimeWindowIsClampedIntoTheRecordingWithAWarningForEachEnd`,
`TEST_CASE("ChartGeometry: a gap breaks the line and the band")`. The name is what a failure reports.

## Prove it can fail

A test that cannot fail proves nothing (`docs/claude.md`, Working Method). For each rule the new test holds, break the
code once and watch the test fail:

- by hand for one or two, restoring the file byte for byte after;
- for more, with `eng\mutate.ps1` and a JSON list of exact-text mutants, run from a worktree
  (`git worktree add C:\tmp\<name>`). Its header documents the list format; `eng\mutants\example.json`
  is the sample. A survivor is a missing test, not a shrug: close it, or record why it is equivalent.
