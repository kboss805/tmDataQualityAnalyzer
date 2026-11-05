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

## 5. Running a batch — one entry point, one loop

The only entry point is **Apply Template** (`applyTemplateButtonPressed()`): pick +
parse a saved template `.json`, then multi-select the files, and hand off to
`startBatchFromTemplate(template, files, showReuseAppearance = true)`. (There is no
direct "open multiple files" path and no Add/Remove Source — a template is the sole
way to configure a batch.)

`startBatchFromTemplate()` **validates each file up front** (cheap TMATS-only
`Chapter10Reader::loadChannels` + `TemplateMatcher`) → `BatchApplyDialog` shows each
file OK/rejected(reason), an optional "also export a CSV + plot images per file"
checkbox (+ output folder), and a reuse-appearance checkbox → starts the run.

The run is a sequential one-run-at-a-time state machine (`advanceBatch()` ↔
`onProcessingFinished()`). **Every file is retained in memory** (accumulated onto the
shared axis) so the user can browse them afterward — there is no merged-vs-separate
mode:

- `advanceBatch()` — `addSource(file)`, then wait. Never clears between files.
- `onSourceReadyForStreamConfig()` gains an `m_batch_active` branch →
  `applyBatchSourceConfig()`: re-validate the loaded file (belt-and-suspenders),
  then `setStreamConfigs(template configs)` + `setTimeChannelIndex` +
  `startProcessing()` — no dialog.
- `onProcessingFinished()` gains an `m_batch_active` branch →
  `onBatchProcessingFinished()`: label the source (`setSourceLabel`) for the file
  selector, and reapply appearance (if enabled); then advance.
- `finishBatch()` — if per-file export was requested, a **post-pass** isolates each
  source (`setVisibleSource`) and writes `exportCsv(path, sourceId)` plus, per file,
  a `_framesync_lock.png` and a `_missed_frames.png` (flipping `setLockAxisView`
  between the two left-axis metrics before each headless `exportImage`). Then
  defaults the view to the first file and logs a summary (`N processed, M skipped[,
  output → dir]`).

**Plot File selector (US1.1 browsing):** the plot toolbar's far-left **Plot File**
dropdown (`PlotWidget::m_source_combo`) picks which retained file to view, or "All
files (overlaid)". It drives `PlotViewModel::setVisibleSource` (−1 = all), which sets
`m_visible_source` and zooms the X viewport to that file.
`effectiveVisible(s) = s.visible && sourceMatches` is the single gate the chart,
legend, and Y-ranging consult — so the source filter never clobbers per-series
visibility or the **View Mode** (lock%/accumulation) selector. The dropdown disables
for ≤1 source.
Retaining every file inherits the multi-source shared-time-axis re-basing for free
via `addSource()`/`PlotViewModel::addStreamData()` — no new time-axis logic.

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
