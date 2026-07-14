# Processing Templates & Batch Apply — design note

Status: **implemented.** Lets a user capture one configured source's per-stream
processing settings as a reusable, file-path-independent *template* and apply it
to many `.ch10` files in one action.

## 1. Why

Users want to configure the per-stream processing once and run it against many
different `.ch10` files without re-entering everything per file. A **template**
stores the *settings* (no file paths), so it can be applied to arbitrary future
files whose channel layout matches.

## 2. What a template is

`ProcessingTemplate` (`include/dto/processingtemplate.h`) = `schemaVersion`,
`appVersion`, optional `name`, `timeChannelIndex`, and an ordered list of
`TemplateStreamEntry`. Each entry = one `StreamConfig` (reused verbatim) + an
optional list of `SeriesAppearance` (`include/dto/seriesappearance.h`: name +
color, keyed by `metricType`/`receiverIndex`/`channelIndex`).

Serialized by `ProcessingTemplateSchema` (`src/model/processingtemplateschema.cpp`),
which **delegates each entry's config to `StreamConfigSchema`** so the ~15-field
`StreamConfig` list is never hand-mapped twice. As with `StreamConfigSchema`,
`calibrationByWord` is never serialized — only the calibration *input references*
(`calCh10Path`/`stepTomlPath`/`clipStartSec`/`clipEndSec`). No file paths appear
anywhere in a template.

## 3. Matching — exact channel-ID set (decided)

**A target file is accepted only if its PCM channel-ID set is identical to the
template's.** Any missing or extra channel rejects the file (with a reason).
`TemplateMatcher` (`src/model/templatematcher.cpp`) is the pure, disk-free
validator (`matchFile()` → `MatchResult{ok, missing, extra}`).

This was a deliberate simplification over positional/name-based mapping: it
removes all mapping ambiguity (no confirm/adjust table, no fewer/more-channel
edge cases) and — crucially — makes it *safe to feed the template's stored
`pcmChannelId`s straight to processing*, because a matched file is guaranteed to
contain exactly those channels. `setStreamConfigs()` does no validation of its
own (`mainviewmodel.cpp`), so the batch loop runs the exact-set check itself
before every run. (A future opt-in "subset match" would be purely additive.)

## 4. Save as Template

`MainView::saveTemplateButtonPressed()` (enabled once a source has processed).
Picks a source (prompts if several), then
`buildTemplateFromSource()` copies that source's `streamConfigs` + its
`timeChannelIndex`, and snapshots each stream's current series appearance by
walking `PlotViewModel::allSeries()` for `(sourceId, streamOrder ==
pcmChannelId)`. This is the only place series names/colors get persisted.

## 5. Running a batch — two entry points, one loop

Two ways in, both ending at `startBatchFromTemplate(template, files, showReuseAppearance)`:

- **Open Multiple Files** (`openMultipleButtonPressed()`) — the direct path for
  "process a string of same-channel-ID files." Multi-selects files, loads the
  first for metadata, shows the **Configure Streams dialog once**
  (`configureMultiThenBatch()`), and builds an **in-memory** `ProcessingTemplate`
  from that config (no saved `.json`, no series appearance). `showReuseAppearance =
  false`.
- **Apply Template** (`applyTemplateButtonPressed()`) — pick+parse a saved template
  `.json`, then pick the files. `showReuseAppearance = true`.

`startBatchFromTemplate()` then **validates each file up front** (cheap TMATS-only
`Chapter10Reader::loadChannels` + `TemplateMatcher`) → `BatchApplyDialog` shows each
file OK/rejected(reason), a merged-vs-separate toggle, a reuse-appearance checkbox
(hidden for Open Multiple), and (separate) an output folder → starts the run.

The run is a sequential one-run-at-a-time state machine (`advanceBatch()` ↔
`onProcessingFinished()`):

- `advanceBatch()` — separate mode `clearData()` per file (fresh plot each);
  merged mode accumulates. `addSource(file)`, then wait.
- `onSourceReadyForStreamConfig()` gains an `m_batch_active` branch →
  `applyBatchSourceConfig()`: re-validate the loaded file (belt-and-suspenders),
  then `setStreamConfigs(template configs)` + `setTimeChannelIndex` +
  `startProcessing()` — no dialog.
- `onProcessingFinished()` gains an `m_batch_active` branch →
  `onBatchProcessingFinished()`: reapply appearance (if enabled), and in separate
  mode export CSV (`PlotViewModel::exportCsv`) + image (`PlotWidget::exportImage`,
  extracted headless in Phase A) auto-named from the file's base name; then
  advance.
- `finishBatch()` — merged: one accumulated plot + combined title; separate:
  a summary log (`N processed, M skipped, output → dir`).

Merged mode inherits the multi-source shared-time-axis re-basing for free via
`addSource()`/`PlotViewModel::addStreamData()` — no new time-axis logic.

## 6. Appearance reapply

`reapplyTemplateAppearance(sourceId)` maps each template entry (by
`pcmChannelId`) to the just-finished source's freshly-created series, matches by
`(metricType, receiverIndex, channelIndex)`, and applies via the id-based
`renameSeriesById`/`recolorSeriesById` + one `commitAppearanceChanges()`. Exact,
because matching files share channel ids and receiver/channel shape.

## 7. Testing

- `tst_processingtemplateschema` — schema round-trip (both modes, appearance,
  calibration refs, `timeChannelIndex`), version accept/reject, and the inherited
  "never serialize `calibrationByWord`" guarantee. CI-safe.
- `tst_templatematcher` — exact-set pass/reject with missing/extra reporting.
  CI-safe.
- `tst_plotwidget` — `exportImage()` headless PNG/SVG/PDF (first coverage of the
  image-export path).
- `tst_mainview` — `reapplyTemplateAppearance()` maps appearance onto the right
  series; `buildTemplateFromSource()` captures configs + appearance. CI-safe.

The full sequential `advanceBatch()` orchestration (addSource → real processing)
is exercised by the running app, not an automated test — it needs a real `.ch10`
fixture, so like the other real-file integration paths it is not covered in CI.
