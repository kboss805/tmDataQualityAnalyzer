# Comprehensive, task-oriented user manual

**Status:** scoped, not started. Blocked on screenshots (see the capture list below).

## The problem

`resources/usermanual.html` is organised **by UI surface**, not by task:

```
1. Getting started      5. Templates & batch processing
2. Configuring streams  6. Import & export
3. Working with the plot   7. Interface & settings
4. Understanding the metrics  8. Keyboard shortcuts
```

That answers *"what does this control do?"* It does not answer *"I have an AGC recording
and a step file — what do I actually do?"* For an application this procedural, the second
question is the one users arrive with.

**Calibration is the sharpest case.** Non-linear step calibration (US5.3) is the most
involved workflow in the product — it spans five nested dialogs, needs two input files
prepared in advance, has optional clip controls that change the result, and reports a
per-channel outcome where some channels succeed and others silently fall back to linear.
It currently gets **six sentences** inside §4 *Understanding the metrics*, with no section
of its own and no figure of any dialog in the chain.

The full path a user must discover unaided:

```
Open .ch10 -> Configure Streams
           -> gear on the row -> "Configure Receiver SNR - <label>"
           -> "Extract Calibration"  (CalibrationSetupDialog)
                 step TOML + calibration .ch10 + optional Clip Start / Clip End
           -> per-channel summary (which channels got a profile, which fell back)
           -> Process -> compare against the uncalibrated plot
```

Nothing in the manual shows any of that.

## Proposed shape

Keep the existing reference sections — they are accurate and useful for lookup — and add a
**walkthrough** ahead of them. Reference answers "what is this control"; the walkthrough
answers "how do I do the job".

1. **Walkthrough: frame sync lock** — open a PRN recording, configure one stream, process,
   read the plot, switch to Accumulation, export.
2. **Walkthrough: receiver SNR, uncalibrated** — configure receivers/channels, scale,
   voltage range, polarity; process; read the staircase.
3. **Walkthrough: receiver SNR with step calibration** — the centrepiece. Every dialog
   figured, both input files explained, Clip Start/End shown with *why* you would set them,
   the per-channel summary explained including what a fallback means for that curve, and a
   before/after comparison.
4. **Walkthrough: batch processing with a template** — build from one configured file,
   apply across many, read the skipped-file report.

Then the existing reference sections, renumbered.

**Write the calibration section against the real failure modes**, not just the happy path.
`docs/CLAUDE.md` (US5.3) and the calibration memories record what actually goes wrong:
a signal-generator turn-on transient at the start, an operator down-ramp at the end,
inverted-polarity receivers, and channels whose plateau count does not match the expected
step count. Those are what Clip Start/End exist for, and a user who has not been told will
read a fallback channel as a bug.

## Screenshots needed

Capture in **one sitting** - the trickle approach cost several rounds during v2.9.1/2.9.2.
Names are the filenames to save into `docs/manual_images/`.

**Frame sync walkthrough**
- `walk-fs-01-open.png` - the Open dialog / drag-drop target
- `walk-fs-02-configure-streams.png` - Configure Streams, several PCM rows, Mode column
- `walk-fs-03-framesync-setup.png` - gear -> *Configure Frame Sync Lock*, fields filled
- `walk-fs-04-processing.png` - the modal progress dialog mid-run
- (reuse the existing Lock % and Accumulation plot figures)

**SNR walkthrough**
- `walk-snr-01-mode-select.png` - setting a row's Mode to Receiver SNR
- `walk-snr-02-receiver-setup.png` - *Configure Receiver SNR*, receivers/channels/scale/range/polarity
- `walk-snr-03-load-params.png` - loading a receiver-parameters TOML

**Calibration walkthrough** (the priority)
- `walk-cal-01-extract-button.png` - the *Extract Calibration* action in context
- `walk-cal-02-extract-dialog.png` - *Extract Calibration*, both file fields populated
- `walk-cal-03-clip-controls.png` - Clip Start / Clip End set to non-zero
- `walk-cal-04-summary.png` - the per-channel success/fallback summary box
- `walk-cal-05-step-toml.png` - a `[[Step]]` TOML open in an editor, so the format is concrete
- (reuse the existing uncalibrated/calibrated SNR plots for before/after)

**Batch walkthrough**
- `walk-batch-01-save-template.png`
- `walk-batch-02-apply-dialog.png` - including a mismatched file flagged as skipped
- `walk-batch-03-plot-file-menu.png` - the Plot File submenu with several sources

**Capture convention.** Full app window at the standard size (**1936x1119**) so the shared
`$ChartCrop` applies; dialog-only captures need no crop and should be tight to the dialog.
`build_manual.ps1` now validates this: a bare `$true` entry verifies the window size and
fails with the actual dimensions rather than cropping blind, and an oddly-sized capture can
be given its own explicit `@(x,y,w,h)`.

## Figure storage - MEASURED, and the decision

This was flagged as "decide before writing". It has now been measured, and a proposal
from the user resolves it.

### What the numbers actually are

| | |
| --- | --- |
| Manual as shipped (9 figures) | **1804 KB** |
| Release exe | **2634 KB** |
| So documentation is | **68% of the executable** |
| Prose alone | 15 KB |
| Per figure, in the binary | ~200 KB |

**The resource is stored UNCOMPRESSED.** Verified directly: the base64 PNG prefix
`iVBORw0KGgo` appears verbatim in the shipped exe at offset 617291. rcc only compresses
when it would save more than 30%, and base64 of already-compressed PNG data compresses to
about 75% - just under the threshold. So the manual pays base64's 33% expansion **in full**.

Corroborated across releases: the v2.9.0 exe (manual had no figures) was **844 KB**;
today's is 2634 KB, and 844 + 1804 = 2648. The growth is essentially all manual.

Forcing compression (`QMAKE_RESOURCE_FLAGS += -threshold 0 -compress 9`) was measured by
running rcc both ways over the real `.qrc`: **27% saving**, i.e. roughly 490 KB today and
~1.3 MB once the figure count triples. One line in the `.pro`. Worth taking **if the manual
stays embedded** - but see below, because it may not.

### The proposal that supersedes it

Make the manual **an installed file rather than a compiled-in resource**, and let the
installer offer a choice:

- **Full manual** - every walkthrough and figure.
- **Condensed manual** - prose and a handful of key figures.
- **Portable ZIP always ships the full manual** (no installer, no choice to make).

This is not merely another storage option; it is the *only* one that supports the choice.
A compiled-in resource cannot vary per installation - the exe is one binary. So picking this
answers the storage question outright, and the compression tweak becomes moot.

Consequences, good and bad:

- Exe drops from 2634 KB to roughly **830 KB**, and figure count stops constraining the
  manual at all - which is what unblocks writing it properly.
- Download size barely moves. The installer is 39 MB and the ZIP 22 MB; Qt's DLLs dominate
  both, so this is a rounding error either way. **Do not sell this as a size win** - the
  win is editorial freedom, not bytes.
- `MainView`'s Help > User Manual currently copies `:/resources/usermanual.html` to temp
  because a browser cannot read `qrc:/`. Reading an installed file removes that dance, but
  adds a new failure mode: the file can be missing (condensed install, or a user deleted
  it). Needs a clear message, not a silent no-op.
- The installer already has `[Tasks]` for desktop icon and file association, so the
  machinery is familiar - though an either/or choice fits Inno's `[Types]`/`[Components]`
  better than `[Tasks]`.

### The risk to design against

**Two manual variants are two artifacts that can drift**, and shipped-artifact drift is the
exact failure this project keeps paying for - the eighteen-month-stale `UserGuide.txt`, the
manual that read "Version 2.7.0" at v2.9.0, the figures illustrating the empty axes v2.9.1
removed. A condensed manual that quietly falls behind the full one would be the same bug in
a new place.

So if this is taken: generate the condensed variant **from** the full one in
`build_manual.ps1` (never hand-maintain it), and extend
`scripts/check_release_consistency.ps1` to assert the version markers in **both**.

## Not in scope

Re-litigating the reference sections. They are current as of v2.9.2 and were rewritten from
scratch in PR #64/#65; this adds a layer in front of them rather than replacing them.
