---
name: add-stream-setting
description: >-
  Add a new per-stream configuration setting to tmDataQualityAnalyzer end-to-end, across every layer
  it must cross. Use this WHENEVER the work introduces a new knob the user configures per PCM stream -
  a new frame-sync field, a new SNR/calibration option, a new processing flag - or the user says "add
  a setting/field/option for X to the stream config", "expose Y in the setup dialog", or "let the user
  control Z per stream". One setting must be threaded from the setup dialog's view model through Apply
  to all, the templates, the C# binding, the C ABI and the job builder to FrameProcessor (and the
  calibration extractor, if it affects raw values), and a layer skipped is a control that silently
  does nothing. This skill is the checklist that keeps the layers in sync.
---

# Add a per-stream setting — tmDataQualityAnalyzer

A per-stream setting is chosen in a stream's setup dialog (Configure Streams' gear) and has to reach
`FrameProcessor` to affect anything. `Randomized` (Derandomize) takes the whole path, so follow it:
`grep -rn -i randomized src/TmDq.App src/TmDq.Core native/tmdq_core include src/model`.

Decide two things first, because they gate which steps apply:

- **Mode:** Frame Sync Lock streams, Receiver SNR streams, or both.
- **Persistence:** is it part of what a pattern or receiver-parameters file describes, or a
  per-session operator input? The TOML boundary in `.claude/CLAUDE.md` decides (step 6).

## The layers, in order

1. **The app's settings record** — `src/TmDq.App/Streams/StreamSetup.cs` (`StreamSetup`), with its
   default in `Streams/StreamDefaults.cs` if it has one.
2. **The setup dialog** — `ViewModels/StreamSetupViewModel.cs` (the field, its property, loading it from
   and writing it to `StreamSetup`, and any validation: a problem is shown as it arises, and OK waits),
   and the control in `Views/StreamSetupWindow.axaml`, with a tooltip like every other field.
3. **The rules** — `Streams/StreamRules.cs`: the edited row, **Apply to all** (US2.2: a setting that
   should not fan out says so in a comment), and the conversion to the run's `StreamConfig`.
4. **Templates** — `Templates/TemplateFile.cs`: a template carries every row's settings, in the JSON
   v2.x wrote; read with a default when the key is missing (older templates), write it in the same
   form. `TemplateTests` holds the format byte for byte.
5. **The C# binding and the ABI** — `src/TmDq.Core/RunTypes.cs` (`StreamConfig`),
   `ProcessingRun.cs` (marshalling), `Native.cs` (the struct), and `native/tmdq_core/tmdq_abi.h` plus
   `abi_run.cpp`. A field added to an ABI struct changes its size: append it, bump the ABI revision
   (and `Library.MinimumRevision`), and update the layout pins on both sides (`tmdq_sizeof`).
6. **Persistence, only if it belongs to the file** — the core's format (`src/model/framesyncfile.cpp`
   for a pattern file, the receiver-parameters reader in `framesetup.cpp`), both through
   `TomlDocument`, symmetric in load and save, and its ABI functions and binding (`abi_formats.cpp`,
   `Formats.cs`). The app's Load/Save is `Streams/StreamFiles.cs`. **The boundary:** a pattern file
   round-trips the sync pattern, sync mask, words/frame and the two stream-format flags (`Inverted`,
   `Randomized`); Data Rate and Sample Rate are per-session and stay out; calibration profiles are
   never serialised. A file without the new key keeps the operator's choice (US8.0: readers default
   what a file does not carry).
7. **The job** — `include/model/streamjobbuilder.h` (`StreamJobConfig`) and `src/model/streamjobbuilder.cpp`,
   into `ProcessingParams` (and `FrameSetup` if it affects the word map). **A raw-affecting setting goes
   through `applyAcquisition`** (`include/model/framesync.h`), which the calibration extractor shares, so
   extraction sees it too; then carry it in the calibration request (`calibrationrequest.cpp`,
   `abi_calibration.cpp`, `Calibration.cs`, `ExtractCalibrationViewModel`).
8. **The effect** — `src/model/frameprocessor.cpp`. Honour the invariants (the **tm-code-review**
   skill): average raw counts before calibrating, bit-span lock %, no missed-frame double count.

## Tests, at every layer (the **write-test** skill)

- **native:** `test_streamjob.cpp` (the builder carries it), `test_frameprocessor.cpp` (it changes the
  output: the strongest test), `test_abi.cpp` (the struct and its conversion), and `test_framesyncfile.cpp`
  if it persists;
- **managed:** the layout pin and a run through `ProcessingRun` that it changes;
- **app:** `StreamSetupViewModelTests` (shown, validated, kept), `StreamRulesTests` (Apply to all and the
  conversion), `TemplateTests` (the round trip, and an older template without the key), `StreamFilesTests`
  if it persists;
- **the soak manifest** (`tests/TmDq.App.Tests/Soak/soak.json`) states every setting of every run, so
  add the new one there with its default.

Then mutation-check each layer's line (`scripts\mutate.ps1`): a mutant that drops the field at any one
layer must fail a test.

## Verify

1. Build and run every suite with the **build-and-test** skill.
2. Run the app on a recording where the setting matters and see the output change. A setting that
   passes the unit tests and changes nothing in the app means a layer was missed: most often the
   conversion in `StreamRules`, or the ABI.
3. Review with the **tm-code-review** skill before committing.
