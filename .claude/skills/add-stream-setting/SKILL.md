---
name: add-stream-setting
description: >-
  Add a new per-stream configuration setting to tmDataQualityAnalyzer end-to-end, across every MVVM
  layer it must touch. Use this WHENEVER the work involves introducing a new knob/parameter that the
  user configures per PCM stream — a new frame-sync field, a new SNR/calibration option, a new
  processing flag — or when the user says "add a setting/field/option for X to the stream config",
  "expose Y in the gear dialog", or "let the user control Z per stream". This change type is the most
  error-prone in the project because one setting must be threaded through StreamConfig → the gear
  sub-dialog → the Apply-to-all fan-out → ProcessingParams/FrameSetup → FrameProcessor → (optionally)
  the TOML round-trip → tests, and skipping any layer produces a control that silently does nothing.
  This skill is the checklist that keeps the layers in sync.
---

# Add a Per-Stream Setting — tmDataQualityAnalyzer

A per-stream setting is captured in the **Configure Streams** dialog and must flow through the whole
MVVM pipeline to actually affect processing. Miss a layer and you get a dead control: the UI shows it
but `FrameProcessor` never sees it, or it's set but never fans out / never persists. Work the layers
in order and verify at the end with a real run.

First decide two things, because they gate which steps apply:
- **Mode:** does it belong to Frame Sync Lock streams, Receiver SNR streams, or both? That picks which
  gear sub-dialog in `streamsubdialogs.h` hosts it.
- **Persistence:** is it a saved/recalled config value, or a per-session operator input? This decides
  whether it round-trips through TOML — and is governed by a hard boundary (see step 5).

## The layers, in order

### 1. Data type — `include/dto/streamconfig.h`
Add the field to `StreamConfig` (camelCase, Qt-property style). Give it a sensible default; if it's a
tunable constant, define the default in `include/constants.h` (`kPascalCase` in the right namespace)
and reference it rather than hardcoding. If the field is frame-sync-related and belongs with the
bundled acquisition params, it may fit inside `FrameSyncParams` (`include/dto/framesyncparams.h`)
which is composed into `StreamConfig`.

### 2. UI — the gear sub-dialog in `include/view/streamsubdialogs.h`
Surface the control in the Frame Sync Lock setup or Receiver SNR setup sub-dialog (whichever matches
the mode). Add the widget, a tooltip (every field has one), wire it to read/write the `StreamConfig`
field, and respect the wireframe separator: persistent config fields go above it, per-session inputs
below. Validate input at entry (US7.0) — e.g. hex-only for patterns, range checks for counts.

### 3. Apply-to-all fan-out (US2.2) — `src/view/streamconfigdialog.cpp`
If the setting should propagate when the user checks "Apply to all <mode> streams", add it to the
field-copy lambda that the gear dialog uses (the same dialog→row copy path as every other fanned-out
field). If it's intentionally per-stream-only, note that in the field's comment so a future reader
knows the omission is deliberate.

### 4. Processing inputs — `src/viewmodel/mainviewmodel.cpp` (`buildStreamJob`)
Carry the field from `StreamConfig` into `ProcessingParams` (`include/dto/processingparams.h`,
snake_case fields). If it affects the **word map or calibration table**, also thread it into
`FrameSetup` where the job is built. This is the bridge from "configured" to "will be processed".

### 5. Persistence (only if it's a saved config value) — `src/model/tomlconfighelper.cpp`
If and only if the field is meant to be saved/recalled, add it to the matching load/save helper —
keeping load and save **symmetric**. Respect the boundaries:
- **Frame-sync TOML round-trips ONLY** sync pattern, sync mask, words/frame. `Randomized`,
  `Data Rate`, `Sample Rate` are per-session and excluded by design (US1.0). Do not widen
  `loadFrameSyncToml`/`saveFrameSyncToml`.
- Receiver-parameter TOML covers the word map + linear calibration (US5.1). Calibration *profiles*
  are session-only and never serialized.
If the field is a per-session operator input, skip this step entirely — that's the correct behavior,
not an omission.

### 6. Consumption — `src/model/frameprocessor.cpp` (`process()`)
Make the setting actually do something. Read it from `ProcessingParams`/`FrameSetup` and apply it in
the extraction loop or `recordTimeSample()`. Honor the existing invariants (see the **tm-code-review**
skill): average-raw-before-calibrate, bit-span lock %, no missed-frame double-count, COW-detach before
mutating payload bytes. This is the layer where a wrong placement causes a real data bug, not just a
dead control.

### 7. Tests — via the **write-qt-test** skill
Add coverage proving the setting changes behavior: a `tst_streamconfigdialog` case for capture +
Apply-to-all fan-out, a `tst_tomlconfighelper`/`tst_framesetup` round-trip if it persists, and a
`tst_frameprocessor` case asserting the processing effect (the strongest test — it pins the behavior
against regression).

## Verify
1. Build the app and the full test suite with the **build-and-test** skill (0 warnings, all green).
2. If feasible, do a real run (the **verify**/**run** skills) and confirm the control visibly changes
   output. A setting that builds and passes unit tests but does nothing in the app means a layer was
   missed — most often step 4 (not carried into `ProcessingParams`) or step 6 (not consumed).
3. Review the change with the **tm-code-review** skill before committing.

## Reference
`docs/CLAUDE.md` → "Adding a New Per-Stream Setting" is the canonical short version of this checklist;
this skill expands it with the mode/persistence decisions and the invariant pointers.
