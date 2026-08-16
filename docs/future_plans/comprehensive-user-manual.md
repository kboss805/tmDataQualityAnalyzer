# Comprehensive, task-oriented user manual

**Status:** scoped. Tooling and the installer option are built (PR #78, unreleased).
Capture in progress: **17 of 23 done** (batches A and B complete, including the whole
calibration set); 6 remain.

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

The list below was audited against **every `QDialog` in the codebase** plus the menus, rather
than written from memory, because the first draft missed several whole surfaces - *Customize
Plot Series*, the hamburger menu, *Set Time Window*, error reporting and the light theme were
all absent. Existing figures are reused where they still depict the current UI.

**23 captures** (24 planned, one dropped as redundant - see the calibration group), of which
**17 are done** and 6 remain. Plus 7 existing figures reused.

Batches A and B are complete: the frame-sync walkthrough, application shell, light theme,
installer, the SNR walkthrough and the whole calibration set. The 6 outstanding are batch
processing (3), the Customize SNR tree and recolour (2) and one error-log figure.

Dialog coverage after this list: every `QDialog` in the app is figured except *About*, which
needs none.

**Frame sync walkthrough**
- `walk-fs-01-open.png` - the Open dialog / drag-drop target
- `walk-fs-02-configure-streams.png` - Configure Streams, several PCM rows, Mode column
- `walk-fs-03-framesync-setup.png` - gear -> *Configure Frame Sync Lock*, fields filled
- `walk-fs-04-processing.png` - the modal progress dialog mid-run
- (reuse the existing Lock % and Accumulation plot figures)

**SNR walkthrough** - **CAPTURED**
- `walk-snr-01-mode-select.png` - setting a row's Mode to Receiver SNR
- `walk-snr-02-receiver-setup.png` - *Configure Receiver SNR*, receivers/channels/scale/range/
  polarity. Also carries the **Apply Cal** button and its calibration-status text, so it
  doubles as the "Extract Calibration in context" figure.
- `walk-snr-03-load-params.png` - loading a receiver-parameters TOML

**Calibration walkthrough** (the priority) - **CAPTURED**
- `walk-cal-01-extract-button.png` - *Extract Calibration* as opened, both fields empty
- `walk-cal-03-clip-controls.png` - the same dialog populated: step TOML, calibration
  .ch10, and Clip Start / Clip End set to non-zero
- `walk-cal-04-summary.png` - the "Applied non-linear calibration to N channel(s)" box
- `walk-cal-05-step-toml.png` - a `[[Step]]` TOML open in an editor, so the format is concrete
- (reuse the existing uncalibrated/calibrated SNR plots for before/after)

  **`walk-cal-02` was dropped, not skipped.** It was to show the dialog with both files
  populated but before the clip values; `walk-cal-01` (empty) and `walk-cal-03` (fully
  populated) bracket that, and a middle state adds nothing. Its other intended job - the
  *Extract Calibration* action in context - is covered by `walk-snr-02-receiver-setup.png`,
  which shows the **Apply Cal** button and its "(none - using linear calibration)" status.

### The calibration section must explain "3 of 48"

The captured summary reads **"Applied non-linear calibration to 3 channel(s)"** against a
48-channel file, and the extract dialog says **"Calibrated 3 of 48 channel(s)"**. That is the
CORRECT result for `agc_rnrz-l_trc_testfile.ch10`: the real stepped sweep exists only on
RCVR3's L/R/C words, and the other 45 words are low-level noise that must *not* calibrate.
They fall back to linear, exactly as designed.

Unexplained, that number reads as catastrophic failure. A user will assume the feature is
broken, retry with different clip values, and get the same answer. **This is the single most
valuable thing the section can say** - it turns the summary box from alarming into
informative, and it is the concrete form of the plan's "write against the real failure modes"
rule.

The captured set is self-consistent and checkable, which is how it was verified:

| Figure claim | Verified against |
| --- | --- |
| "11 steps parsed" | `settings/rcvr_cals/TRC.toml` - 11 `[[Step]]` entries, 0..60 dB |
| "3 of 48 channel(s)" | `settings/receiver_params/TRC.toml` - 16 rcvrs x 3 channels |
| Scale 20.000, Polarity Positive | same file - `Scale = "20"`, `Polarity = 0` |
| calibrated plot tops out at 60 dB | the step TOML's range |

**Batch walkthrough**
- `walk-batch-01-save-template.png`
- `walk-batch-02-apply-dialog.png` - including a mismatched file flagged as skipped
- `walk-batch-03-plot-file-menu.png` - the Plot File submenu with several sources

**Plot customisation** - a major dialog (US4.0) with no figure at all today
- `walk-cust-01-lock-tab.png` - *Customize Plot Series*, Frame Sync Lock tab, several streams
- `walk-cust-02-snr-tree.png` - Receiver SNR tab: the receiver/channel tree, a group in its
  tri-state (partially checked) state, which is the part that reads as broken if unexplained
- `walk-cust-03-recolor.png` - renaming or recolouring one series

**Application shell** - the entry point to everything, never shown
- `walk-app-01-hamburger.png` - the ≡ menu open, showing all four sections (Process,
  Import/Export, Settings, Help). This also covers **Import CSV**, which needs no figure of
  its own since it opens a standard file dialog.
- `walk-app-02-sidebar.png` - the log sidebar, with the toggle indicated
- `walk-app-03-time-window.png` - *Set Time Window*, the `DDD:HH:MM:SS` entry people most
  often get wrong; show it filled in

**Error handling** (US7.0) - described in prose today, never shown
- `walk-err-01-log-error.png` - the log with a red error entry, e.g. a rejected CSV or an
  invalid sync pattern, so users recognise the app's own error reporting

**Theme** (US9.0) - a shipped feature with zero representation; every figure is dark
- `walk-theme-01-light-plot.png` - one plot in the light theme

  Deliberately **one** figure, not a light twin of every other. Shooting all 22 twice would
  double the drift surface for very little - the manual only needs to establish that the
  theme exists and looks coherent.

**Installer** (US8.0) - **CAPTURED**
- `walk-inst-01-components.png` - the installer's Select Additional Tasks page, showing
  the optional full manual alongside the desktop-shortcut and file-association options.
  Cropped to drop the title bar: it carries a version string, and keeping it would stamp
  one release into a figure that never changes, so it would read as stale from the next
  version onward. The page heading inside the client area identifies the screen.

### Sequencing - resolved

The installer work was built first (PR #78) precisely so this figure would not become a
one-off capture later. It is done. **The remaining 23 are all capturable against the current
build**, with nothing else gated on code.

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

### The decision: a base manual that is always there, plus an optional add-on

Not "full **or** condensed" - that framing was wrong, because it leaves an installation with
no manual at all if the user picks wrong or later deletes the file. The rule is instead:

1. **Base manual** - today's `usermanual.html`. Stays a **compiled-in Qt resource**, exactly
   as now. Always present, in the installer *and* the portable ZIP, and impossible to delete.
2. **Full manual** - the walkthroughs and their figures, shipped as an **installed file** and
   offered as an optional component in the installer.
3. **Portable ZIP always carries both**, since it has no installer to ask.

The user always has at least one manual, and that guarantee is **structural** - a resource
compiled into the executable cannot go missing - rather than something the installer has to
get right. That is what makes this better than making both external: there is no
"manual not found" state to design an error message for.

Consequences:

- The exe keeps its ~1.8 MB base manual, so **there is no binary size win** - and none is
  needed. The point is that the *full* manual's figure budget is no longer bounded by what
  is tolerable to compile into every copy of the program.
- `QMAKE_RESOURCE_FLAGS += -threshold 0 -compress 9` becomes worth taking on its own merits:
  a measured **27%** off the embedded base manual, one line, no downside.
- `MainView`'s Help > User Manual opens the full manual when it is installed and the base one
  otherwise. The fallback is a normal path, not an error path.
- The installer already has `[Tasks]` for desktop icon and file association; an optional
  add-on fits `[Components]` (or a `[Tasks]` checkbox - either is idiomatic here).

### The risk to design against

**Two manual variants are two artifacts that can drift**, and shipped-artifact drift is the
exact failure this project keeps paying for - the eighteen-month-stale `UserGuide.txt`, the
manual that read "Version 2.7.0" at v2.9.0, the figures illustrating the empty axes v2.9.1
removed.

The base and full manuals share all their reference prose, so they must not be maintained as
two documents. `build_manual.ps1` should emit **both from one source**: the full manual is the
base plus the walkthrough sections and their figures. Nothing is hand-copied between them.

`scripts/check_release_consistency.ps1` must then assert the version markers in **both**
outputs. Today it checks `resources/usermanual.html` only; a second manual that nothing
checks is a stale artifact waiting to happen, and the whole point of that script is that a
marker which stops being checked fails loudly rather than silently.

## Not in scope

Re-litigating the reference sections. They are current as of v2.9.2 and were rewritten from
scratch in PR #64/#65; this adds a layer in front of them rather than replacing them.
