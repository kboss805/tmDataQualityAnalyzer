# Design Note — Multi-file input (Phase 1)

**Status:** DECISIONS LOCKED (2026-07-07), ready to implement. Resolved:
- Time alignment = **absolute IRIG time + re-base to the global-earliest sample** (§3). ✔
- Relative-overlay mode = **deferred post-v1** (§9). ✔
- **Source removal = IN v1** (§5). ✔
- **Source-qualified CSV export = IN v1** (§7). ✔

Recommended implementation model: **Sonnet, medium effort** — the hard decisions
are settled here; the rest is layered plumbing guarded by CI + the 267-test suite.

**Goal:** let one plot draw from more than one `.ch10` file — e.g. Receiver SNR
in file A and Frame Sync Lock in file B — merged onto a single time axis. Today,
opening a file `clearData()`s the plot and rebinds the single reader to that one
file (`docs/CLAUDE.md` even states "There is no batch / multi-file mode").

---

## 1. What already works (the enablers)

Two facts make this far smaller than it looks:

1. **`PlotViewModel` already accumulates** via `addStreamData()`, and maps every
   stream onto a **shared absolute-time axis**:
   `elapsed = timesSec − m_base_abs_seconds`, where the base is set **once** from
   the first accumulated stream (guarded by `if (m_series.isEmpty())`). Streams
   from a second file drop onto the same axis with zero ViewModel changes —
   *provided both files carry absolute IRIG time from the same timeline.*
2. **The coordinator boundary is already source-agnostic.**
   `ProcessingCoordinator::startProcessing(QVector<StreamJob>)` just takes a job
   list; it doesn't care that today all jobs come from one file.

The single-file assumption lives entirely in **`MainViewModel`**
(`m_input_filename`, one `m_reader` rebound per open, `startProcessing()` building
jobs from a single `m_stream_configs`) and in the **UI flow** (Open →
`clearData()`).

---

## 2. The model: a session is N sources

Replace "the input file" with an ordered list of **sources**, each:

```
Source {
    QString  filepath;
    int      sourceId;                 // stable within the session, assigned on add
    int      timeChannelIndex;         // this file's chosen time channel
    QVector<StreamConfig> streamConfigs;
}
```

`MainViewModel` holds `QVector<Source> m_sources` instead of a single filename +
configs. **Open** starts a fresh session (clear + source 0). **Add source…**
appends a source and processes it *without* clearing. A processing template
captures one of these `Source`s' stream configs for reuse across other files.

---

## 3. Decision A — Time alignment & re-basing (the crux)

Telemetry `.ch10` files carry **absolute IRIG time**; `timesSec` are absolute
seconds. So every sample from every file has a real timestamp. The question is
only what `t = 0` on the X axis means when sources are combined.

**[RECOMMENDED] Default = align by absolute IRIG time, re-based to the
global-earliest sample across all loaded sources.**

Rationale: the intended use case is *the same test event captured across two
files* (SNR on one recorder, frame-sync on another; or a signal split). Correct
correlation requires that a sample at absolute time *T* in file B lands at the
same X as a sample at *T* in file A. Absolute-time alignment gives that for free.

**Re-basing rule.** The base (`m_base_abs_seconds` / `m_base_day` /
`m_base_time_offset`) must be the **earliest absolute sample across all sources**,
not just the first-loaded. When a newly added source starts *earlier* than the
current base:

```
delta = old_base_abs − new_base_abs            // > 0
for every existing series: xValues[i] += delta // shift right, O(total samples)
m_base_abs_seconds = new_base_abs; recompute m_base_day / m_base_time_offset
recompute m_x_min / m_x_max and the X viewport
new source's elapsed = its_abs − new_base_abs  // computed against the new base
```

This keeps `elapsed ≥ 0`, so **all existing X-axis and `formatTime()` code works
unchanged** (the alternative — allowing negative elapsed — would ripple through
`m_x_min`, the viewport, and the DOY:HH:MM:SS mapping). The re-base is a single
O(n) additive shift over data already in memory — a one-time cost per add-source.

**Concretely in `addStreamData()`:** drop the `if (m_series.isEmpty())`
exclusivity around setting the base. Instead, on every add, compute the incoming
stream's first absolute sample and, if it precedes the current base (or the plot
is empty), re-base as above before appending.

### Non-overlapping / unrelated recordings

If file A was recorded Monday and file B Friday, absolute-time alignment is
technically correct but useless — days of empty axis with one cluster at each
end. **[RECOMMENDED]** detect it: if the new source's `[min_abs, max_abs]` does
not overlap the existing data's absolute range, **warn before committing**:

> "This source's recording time (DOY 132 14:02–14:19) does not overlap the
> current data (DOY 128 …). Plotting them on a shared time axis will show a large
> gap." — **[Add anyway] [Cancel]**

Non-blocking, informational; the user decides. (See the deferred "relative
overlay" option in §9.)

---

## 4. Decision B — Cross-source stream identity

Today `addStreamData()` **erases any existing series matching
`(streamLabel, streamOrder)`** before appending (the reprocess-replace path), and
rename/recolor propagate to siblings matched on `(streamLabel, streamOrder)`.

**Problem:** if file A has stream `("Ch 05", 5)` and file B *also* has
`("Ch 05", 5)`, adding B would **erase A's series — silent data loss.**

**[RECOMMENDED] Add `sourceId` to the identity tuple.**

- Add `int sourceId` to `ProcessedStreamData` and `PlotSeriesData`.
- Reprocess-replace matches `sourceId AND streamLabel AND streamOrder` — so
  reprocessing a stream within the *same* source still replaces it, but a
  same-named stream from a *different* source is preserved.
- `isFrameSyncSibling()` gains the `sourceId` check, so a rename/recolor never
  crosses sources.
- SNR grouping in the Customize Plot Series dialog keys on
  `(sourceId, streamOrder, streamLabel)` so two sources' identically-numbered
  channels stay in separate groups.

This is a contained change (it rides the same identity plumbing hardened in #10/
#17/#22) but it is a **correctness requirement**, not optional — without it,
colliding channel ids across files lose data.

In the common intended case (SNR channels in A, frame-sync channels in B) labels
and orders differ and no collision occurs; `sourceId` is the safety net for when
they don't.

---

## 5. UI / UX flow

- **File > Open** and the toolbar **Open** — unchanged meaning: start a **new**
  session (`clearData()`, then this file becomes source 0 via the existing
  Configure Streams flow).
- **New: File > Add Source… / toolbar "Add Source"** — opens a second `.ch10`,
  runs the Configure Streams dialog against *its* TMATS channel list, builds its
  jobs, processes, and **appends** (no clear, applying §3 re-basing and the §3
  overlap check first).
- Disabled while processing; CSV Import remains its own path (an imported CSV is
  a single source with no `.ch10` behind it).
- **Source removal (IN v1).** A "Sources" list (small dock or a manage-sources
  dialog) shows each loaded source with a remove control. Removing source X drops
  every series with `sourceId == X`, then **re-bases in whichever direction is
  needed**:
  - If X held the earliest sample, the new base is the earliest across the
    *remaining* sources → base moves **later**, so every remaining series shifts
    **left**: `xValues[i] -= (new_base_abs − old_base_abs)`.
  - If X was not the earliest, the base is unchanged and no shift is needed.

  Then recompute `m_x_min/m_x_max` and the viewport; removing the last source is
  equivalent to `clearData()`. This is the mirror of §3's add re-base, so the
  re-base math should live in **one shared helper** used by both add and remove.

---

## 6. Coordinator / reader lifecycle

Add-source runs a **fresh reader pass over file B**, feeding the same
`PlotViewModel`. Sequentially (one processing run at a time; no concurrent
multi-file reads) — the second run reuses the existing modal progress dialog and
cancel path. `MainViewModel` builds file B's jobs from source B's configs and
calls `m_coordinator->startProcessing(jobsB)`; the coordinator's existing
`reset()`-between-runs behavior already supports a second run. The main
remaining work is making `MainViewModel` hold **per-source** filename/configs
rather than the single `m_input_filename` / `m_stream_configs`.

No change to the single-reader/parallel-worker core — each source is still read
once with per-stream workers; we simply allow a second such run to append.

---

## 7. Edge cases

- **Source-qualified CSV columns (IN v1).** Two sources sharing an SNR channel id
  + label would otherwise export as identical column headers. Add a source
  qualifier to `SeriesColumnSchema`, designed to stay **backward-compatible**:
  - **Source 0 (first/only source) keeps today's exact header format** — a
    single-file export stays byte-identical and every previously exported CSV
    still imports (gated by the existing `importExportRoundTrip` +
    `tst_seriescolumnschema` round trips, which must stay green).
  - **Sources 1+ get a leading qualifier token** (proposed `"S<n>| "`, chosen not
    to collide with the digit-then-`" - "` SNR shape or the `Lock (%)` /
    `Accumulated Missed Frames` suffixes). `parseColumnHeader()` strips a leading
    `S<n>|` into `sourceId` first; absence ⇒ source 0.
  - On import, the recovered `sourceId` rides onto each `PlotSeriesData` so the
    imported view keeps its per-source grouping (reconstructing the
    `MainViewModel` source *list* from a CSV is out of scope — see §9).
  - **Risk:** this is the fiddliest sub-item — it touches the on-disk format.
    Treat the round-trip tests as the gate and validate the exact qualifier token
    against real header shapes before committing to `"S<n>| "`.
- **Different time-channel resolutions / clock quality across files.** Absolute
  IRIG time is the common reference; we trust each file's time channel as chosen
  in its Configure Streams dialog. No cross-file clock reconciliation in v1.
- **A source with zero processable streams** (user unchecked everything) — no-op,
  no re-base, source not added.

---

## 8. Testing strategy

All at the ViewModel layer with synthetic `ProcessedStreamData` (no new `.ch10`
fixture needed — these run in CI):

- Accumulate from two sources on a shared absolute axis → correct elapsed for both.
- **Re-base:** add a second source that starts *earlier* → existing series shift
  right, base updates, `x_min == 0`, both sources' absolute correlation preserved.
- **Identity:** two sources with colliding `(streamLabel, streamOrder)` stay
  independent; reprocessing within a source still replaces; rename/recolor does
  not cross sources.
- Non-overlap detection returns the expected "warn" verdict for disjoint ranges.
- `MainViewModel`: add-source appends without clearing; Open resets.

---

## 9. v1 scope

**In:** N-source session model · Add Source UI · absolute-time alignment +
re-basing (add **and** remove) · non-overlap warning · cross-source `sourceId`
identity · **source removal** (§5) · **source-qualified CSV export** (§7) ·
ViewModel + schema tests.

**Deferred (call out, don't build):**
- **Relative-overlay mode** — align each source to *its own* `t = 0` to compare
  two independent runs' shapes. Needs a per-source base / mode flag that breaks
  the single-shared-base model. Post-v1.
- **Reconstructing the N-source structure from an imported CSV** — a multi-source
  export re-imports as one dataset (series keep their `sourceId` for grouping, but
  the `MainViewModel` source *list* isn't rebuilt). Not currently persisted.

**Docs:** update `docs/CLAUDE.md` — the "no batch / multi-file mode" architecture
statement and the data-flow section — once this lands.

---

## 10. Decisions (resolved 2026-07-07)

1. **Time alignment** — absolute IRIG time + re-base to the global-earliest
   sample. **Locked** (§3); everything else hangs on this.
2. **Relative-overlay mode** — **deferred** post-v1.
3. **Source removal** — **in v1** (§5).
4. **Source-qualified CSV export** — **in v1** (§7); the single-source path stays
   byte-identical.

## 11. Suggested implementation order (for the build session)

1. `sourceId` on `ProcessedStreamData` + `PlotSeriesData`; thread through
   `addStreamData` identity/sibling checks (§4). Tests first — cheapest, unblocks
   everything.
2. Shared re-base helper + drop the `isEmpty()` base exclusivity in
   `addStreamData` (§3). ViewModel tests for add/re-base.
3. `MainViewModel` N-source model + "Add Source…" action (§2, §5, §6).
4. Non-overlap warning (§3) + source removal UI/logic (§5).
5. Source-qualified CSV in `SeriesColumnSchema` + round-trip tests (§7) — do this
   last; it's the highest-risk on-disk-format change.
6. Update `docs/CLAUDE.md` (drop "no multi-file mode"; refresh the data-flow).
