# Design Note — Session save / load (Phase 6)

**Status:** DECISIONS LOCKED (2026-07-07). Depends on **Phase 1 (multi-file
input)** landing first — a session is "serialize the `Source` list and replay
it," so it can't start until the N-source model exists. Resolved:
- Format = **JSON** with a `schemaVersion` (§2). ✔
- Calibration on load = **linear-only in v1**; the schema still stores the cal
  input references so auto-re-extraction is an additive follow-up (§6). ✔

**Goal:** persist a whole multi-source analysis configuration and reload it in one
action, so a recurring analysis (which files, which streams, which parameters)
doesn't have to be re-entered by hand every time.

Recommended implementation model: **Sonnet, medium effort** — pattern-following
serialization + UI, guarded by CI and the test suite.

---

## 1. What a session captures — config, not data

**[RECOMMENDED] A session stores the *configuration to reproduce* the plot, not
the processed results.** Loading a session re-runs the multi-source pipeline from
the saved config.

- Pro: small, always-fresh file; reuses the Phase 1 add-source path verbatim.
- Con: the original `.ch10` files must still exist at their recorded paths (handled
  by the missing-file warning in §5); load re-processes (seconds to minutes).

This matches the goal (remove repetitive *setup*), and the lossless "instant load
without the .ch10 files" alternative (embedding processed data) is a much larger,
different feature — out of scope. A user who wants a data snapshot already has CSV
export (US6.1).

---

## 2. File format — JSON (decided)

The session is **nested and repeated**: sources → streams → many config fields.
Two viable formats:

- **[RECOMMENDED] JSON (`.tmdqsession` / `.json`) via `QJsonDocument`.** Qt has
  first-class, well-tested nested read/write (`QJsonObject`/`QJsonArray`); a
  sources-of-streams structure is trivial and robust. The session is a
  machine-managed snapshot, not a hand-edited parameter file, so TOML's
  human-friendliness matters less here.
- **TOML**, for consistency with the parameter configs (frame-sync,
  receiver-params). But `TomlConfigHelper` is a **flat, QSettings-backed** mapping
  that does *not* handle nested arrays-of-tables cleanly; a TOML session would
  need a purpose-built nested serializer — real extra work for a file users rarely
  hand-edit.

**Decided: JSON** for the session; TOML stays for the flat, shareable parameter
files. Each format matched to its job.

Whatever the format: a top-level **`schemaVersion`** integer is mandatory so the
loader can reject/migrate future changes.

---

## 3. Schema (shape, format-agnostic)

```
schemaVersion : 1
appVersion    : "2.7.0"          // informational; not a load gate
savedAtDoy    : ...              // optional, for display
sources : [
  {
    filepath          : "relative/or/absolute path to .ch10"
    timeChannelIndex  : 3
    streams : [
      {
        pcmChannelId  : 5
        mode          : "FrameSyncLock" | "ReceiverChannelInfo"
        // full StreamConfig fields: syncPattern, syncMask, wordsInFrame,
        // randomized, dataRate, sampleRate, code format, receivers/channels,
        // slope, voltageRange, scale, polarity, samplePeriod, ...
        calibration : {            // present only if non-linear cal was enabled
          calCh10Path   : "..."    // input references, NOT the extracted profile
          stepTomlPath  : "..."
          clipStartSec  : 0.0
          clipEndSec    : 0.0
        }
      }
    ]
  }
]
viewState : {                      // §7 — coarse only in v1
  plotTitle       : "..."
  lockAxisView    : "LockPercent" | "MissedFrames"
  leftYMaxOverride: <double|null>
  rightYMaxOverride: <double|null>
}
```

**Serialize StreamConfig from a single place.** Add `toJson()`/`fromJson()` (or a
schema helper) next to `StreamConfig` so the session writer and the eventual
reader share one field list — the same single-source lesson as Phase 5's
`SeriesColumnSchema`. Do **not** hand-map fields in two spots.

**Never serialize the extracted `calibrationByWord` profiles** — that honors the
"calibration profiles are session-only, never serialized" invariant. The session
stores only the *inputs* needed to re-extract them (see §6).

---

## 4. Load semantics

**Open Session** = `clearData()`, then replay: for each source in order, apply its
time channel + stream configs and run the multi-source processing/append path
built in Phase 1. Identical to "Open file 0, then Add Source for 1..N," just
driven from the file. Re-uses the modal progress dialog and re-basing.

---

## 5. File paths & portability

- **[RECOMMENDED]** store each `.ch10` path **relative to the session file** when
  it resolves there, else absolute (so a session + its files move together).
- At load, resolve relative → absolute against the session file's directory.
- **Missing file:** don't hard-fail the whole load. Report which source's file is
  missing and offer **[Skip this source] [Locate…] [Cancel]**. A session that
  loads 3 of 4 sources is more useful than an all-or-nothing failure.

---

## 6. Calibration in a session — **[DECISION NEEDED]** (the meaty part)

Non-linear calibration is *extracted at runtime* from a cal `.ch10` + a step TOML
(the "Extract Calibration…" action); the resulting `calibrationByWord` profiles
are session-only. To restore them, the session stores the **input references**
(§3's `calibration` block), and on load **re-extracts** before processing that
stream.

**Decided: v1 restores linear config only.** Calibrated streams load as linear;
the user re-runs Extract Calibration manually after load. The session schema
**still stores the cal input references** (`calibration` block, §3), so turning on
auto-re-extraction on load later is purely additive — no format change. That
follow-up would re-run `CalibrationExtractor` for each calibrated stream from its
stored references (async, before processing), falling back to linear + warn when
the inputs are missing.

---

## 7. View state — **[RECOMMENDED]** coarse only in v1

Restore the cheap, series-independent view state: **plot title, left-axis view,
manual axis-max overrides** (§3 `viewState`).

**Defer** per-series appearance (visibility, rename, recolor). Those are keyed to
series that only exist *after* the async re-processing completes, so restoring
them means a deferred "apply appearance once all sources finish loading" pass that
matches saved edits by `(sourceId, streamOrder, streamLabel, metricType)`. Doable,
but it's a distinct chunk best done after the core session round-trips — and it
leans on the id-based appearance API from Phase 4. Note it as a fast-follow.

---

## 8. UX

- **File > Save Session As…** and **File > Open Session…** (native file dialogs).
- **[RECOMMENDED]** track a **modified** flag; if there are unsaved sources when
  opening a session or a new file, prompt to save (standard document behavior).
  Optional for v1 — can start with explicit Save/Open only.
- Recent Sessions list: **defer** (reuse the Recent Files pattern later if wanted).

---

## 9. Testing

- **`StreamConfig` round trip** — `toJson`→`fromJson` recovers every field
  (guards the single-source serializer; the analogue of `tst_seriescolumnschema`).
- **Session round trip** — a 2-source session with mixed modes serializes and
  reloads to an equal `QVector<Source>` (files + configs + view state).
- **schemaVersion** — an unknown/newer version is rejected with a clear message.
- **Relative-path resolution** and the **missing-file** branch (skip vs cancel).
- These are pure model/serialization tests — **no `.ch10` fixture, run in CI.**
- The end-to-end "load session → processed plot" path is covered by the Phase 1
  add-source integration tests (local, fixture-backed).

---

## 10. v1 scope

**In:** JSON session format with `schemaVersion` · single-source `StreamConfig`
JSON serializer · Save/Open Session UX · replay-on-load over the Phase 1 pipeline
(linear calibration) · relative-path storage + missing-file handling · coarse
view-state restore · serialization tests.

**Deferred (call out, don't build):**
- **Auto re-extracting non-linear calibration on load** (§6) — the schema stores
  the references; enabling it later is additive, not a format change.
- Per-series appearance restore (§7) — fast-follow, leans on Phase 4's id API.
- Embedding processed data / offline sessions (§1).
- Recent Sessions list; unsaved-changes prompt can be v1-optional.

---

## 11. Decisions (resolved 2026-07-07)

1. **Format** — **JSON** with `schemaVersion` (§2).
2. **Calibration on load** — **linear-only in v1**; references stored for an
   additive follow-up (§6).
3. (Minor, open) unsaved-changes prompt vs explicit Save/Open only — implementer's
   discretion; explicit Save/Open is fine to start.

---

## 12. Build order (for the build session)

1. `StreamConfig` `toJson`/`fromJson` + its round-trip test (single-source
   serializer; unblocks everything, no UI).
2. Session (de)serialize: `QVector<Source>` ↔ JSON with `schemaVersion` + view
   state; round-trip + version-reject + path-resolution tests.
3. Save/Open Session UX; Open Session replays over the Phase 1 pipeline.
4. Missing-file handling (§5).
5. Update `docs/CLAUDE.md` (new SessionSerializer component + File-menu actions).

(Deferred, additive later: auto re-extracting non-linear calibration on load, §6.)
