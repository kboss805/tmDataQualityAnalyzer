# tmDataQualityAnalyzer — AI Assistant Guide

This file provides context and guidelines for AI assistants working on the tmDataQualityAnalyzer project.

## Version Information

- **Qt Version**: 6.11.1 (minimum: Qt 6.0.0)
- **Compiler**: MSVC 2022 (Visual Studio 2022 C++ Build Tools, `cl` / `nmake`), Qt `msvc2022_64` kit.
  This is the only supported toolchain (and what CI uses).
- **C++ Standard**: C++17 (required — `inline constexpr` used throughout constants.h)
- **Project Version**: 2.12.1 — defined once in the `AppVersion` struct in `include/constants.h`; qmake parses it from that header and propagates it to the Qt `VERSION` and the Windows resource file (`version_autogen.h`), so no other file carries a duplicate version literal

## User Stories

The stories below follow the workflow a first-time user takes through the application: open a file and choose what to process (US1), define the per-stream parameters (US2), view the resulting data-quality metrics (US3), customize the plot (US4), manage configuration files and calibration (US5), export and re-import results (US6), and the cross-cutting concerns of input validation (US7), installation (US8), and theming (US9).

**Status: all user stories are Complete** — implemented, tested, and shipped as of v2.10.0. Every acceptance criterion below is delivered functionality (`[x]`), and each story carries a **Complete** marker in its heading. This section is no longer a draft backlog; new work is tracked as new stories appended after US9.0.

### US1.0: Open a Ch10 file and configure which streams to process — Complete

**As a** telemetry engineer or data analyst
**I want to** select streams to process and configure their parameters in a single configuration/processing dialog window upon opening a Ch10 file
**So that** I can apply the specific parameters defined in US2.0 and US2.1 to each individual telemetry stream before starting the decommutation process.

**Acceptance Criteria:**

- [x] A dialog window is presented when the user opens a new ch10 file.
- [x] The dialog window lists all streams identified in the ch10 file.
- [x] The dialog window allows the user to select the streams to process for SNR data and/or framesync lock statistics.
- [x] Each stream row shows a Ready indicator (checkmark or X) reflecting whether that stream is configured and valid to process.
- [x] An "All" toggle checks or unchecks every stream's Process flag in one action.
- [x] The dialog window allows the user to specify the framesync pattern, frame length, framesync mask, and bitrate parameters for each telemetry stream.
- [x] The dialog window allows the user to recall/restore previous frame parameters for each stream using configuration files.

  - **Scope:** Frame-sync Load/Save round-trips the frame sync pattern, sync mask, words/frame (bits per frame), and the two stream-format flags — **Invert Data** and **Derandomize**. Those two describe how the recording's bits are encoded (US2.0 calls NRZ-L / RNRZ-L a PCM code format), so a saved pattern file is only usable if it carries them: loading PRN-15 into a randomized stream and having to tick Derandomize by hand every time is the bug this fixes. **Data Rate** and **Sample Rate** stay out — the first is usually TMATS-derived and the second is an output resolution the operator chooses per session, so neither belongs to the pattern. Keep `loadFrameSyncFromToml`/`saveFrameSyncToToml` symmetric.
  - **Compatibility:** a file with no `Randomized` key (every pattern file written before this, including the shipped PRN ones) leaves the operator's current choice alone rather than clearing it.
- [x] The dialog window allows the user to specify the SNR signal data parameters to be used for SNR signal data streams.
- [x] The dialog window allows the user to recall/restore previous SNR signal data parameters using configuration files.
- [x] The dialog window closes when the user clicks the "Process" button.
- [x] The dialog window closes when the user clicks the "Cancel" button.
- [x] Once the user selects "Process", the dialog window closes and the main window is updated with the processed data.
- [x] Progress is shown in a modal dialog, separate from the main window, while streams are processing.
- [x] Once processing has started, the user can cancel it via a Cancel action that stops every stream currently being decommutated.

### US1.1: Batch-process multiple Ch10 files that share the same channel layout — Complete

**As a** telemetry engineer or data analyst
**I want to** select several Chapter 10 files that share the same PCM channel IDs, configure the per-stream processing once, and process them all in a single action
**So that** I don't have to open and re-configure every file individually when running the same analysis across many recordings.

**Acceptance Criteria:**

- [x] The user can batch-process multiple `.ch10` files in one action by applying a saved template (File > **Apply Template to Files…**): pick a template, then multi-select the files.
- [x] The per-stream processing is configured **once** when the template is created (**Open** a file → configure in the Configure Streams dialog → **Save as Template…**); that configuration is applied to every selected file.
- [x] Files are matched to the template by PCM channel ID; a file whose channel-ID set differs is skipped and the user is told which channels are missing/extra.
- [x] All processed files are retained in memory; the user can optionally also export a CSV + Frame Sync Lock and Missed Frames images per file to a chosen folder, auto-named from each source file.
- [x] Progress is shown while the batch runs, and a summary reports how many files were processed vs. skipped.
- [x] A saved template round-trips the same per-stream settings the Configure Streams dialog captures (frame-sync, SNR, calibration references), and — optionally — per-series names/colors.

  - **Scope:** Matching is **exact channel-ID set** (a file must have the same PCM channel IDs as the template, no more/fewer) — decided for simplicity; revisit if a looser "subset" match is ever needed. This story is orthogonal to US2.2 (apply-to-all *within* one file). Templates carry **no file paths** — settings only, applied to whatever files the user picks. The only batch entry point is **Apply Template** (there is no direct multi-file open, and no Add/Remove Source); templates are the sole way to configure a batch.
  - **Config application:** each file's run feeds the template's `StreamConfig`s through unchanged (`applyBatchSourceConfig`); the reader dispatches each stream by `pcmChannelId`, which is safe precisely because of the exact-set match. **Non-linear calibration:** a template stores only the calibration *input references* (not the extracted profile), so **all batch files process with the linear slope/offset fallback**. Non-linear step calibration is available for single-file processing only; re-extraction on template apply is a possible future add.
  - **Custom names/colors:** captured only when **saving a template** from an already-processed, customized plot (`buildTemplateFromSource`), and reapplied per file after each run (`reapplyTemplateAppearance`, matched by channel id + metric/receiver/channel). Every file's copy of a channel intentionally shares the same name/color (they are the same channel across recordings); use the **Plot File** selector to view one file at a time when per-file distinction matters.
  - **Retain-all + file selector:** a batch always keeps every processed file in memory; the plot's right-click **Plot File** submenu chooses which file to view, defaulting to every source overlaid, with "All files (overlaid)" for comparison. The filter is a `PlotViewModel` source gate (`setVisibleSource`/`effectiveVisible`) orthogonal to per-series visibility and the **View Mode** (lock %/accumulation) selector. Optional per-file export runs as a post-pass (`finishBatch` isolates each source, then `exportCsv(path, sourceId)` + a `_framesync_lock.png` and `_missed_frames.png` via `exportImage`).
  - **Status: implemented** (Save/Apply Template, one `BatchController` batch loop, retain-all + Plot File selector). Schema/matcher/appearance/headless-export/source-view are unit-tested; the full sequential run is app-verified (needs real multi-file `.ch10` recordings). See `docs/processing-template-design.md`.

### US2.0: Define the key parameters required to process the framesync lock statistics and frame sync error accumulation — Complete

**As a** telemetry engineer or data analyst
**I want to** define the rules and key parameters (frame sync pattern, frame length, framesync mask, PCM code format, etc.) required to properly decommutate and calculate frame lock statistics and frame sync error accumulation for telemetry streams
**So that** these parameters can be applied per-stream in the processing dialog window (defined in US1.0) and saved/loaded from configuration files (defined in US5.0).

**Acceptance Criteria:**

- [x] The user can define the frame sync pattern and frame length for each telemetry stream of interest.
- [x] The application accepts frame sync pattern sizes up to 64 bits in length.
- [x] The application accepts frame sync masks up to 64 bits in length.
- [x] The application accepts frame lengths up to 65,536 bits in length.
- [x] The user can select from one of the following PCM code formats: NRZ-L, RNRZ-L.
- [x] The user can calculate frame sync lock statistics for the telemetry streams present in the .ch10 file (one or more PCM channels; no fixed stream-count cap is enforced).

### US2.1: Define the key parameters required to process the receiver AGC sample data — Complete

**As a** telemetry engineer or data analyst
**I want to** define the rules and key parameters (frame sync pattern, frame length, framesync mask, PCM code format, etc.) required to decommutate and process telemetry streams containing receiver AGC sample data
**So that** these parameters can be applied per-stream in the processing dialog window (defined in US1.0) and saved/loaded from configuration files (defined in US5.1).

**Acceptance Criteria:**

- [x] The user can define the frame sync pattern and frame length for the telemetry stream of interest.
- [x] The application accepts frame sync pattern sizes up to 64 bits in length.
- [x] The application accepts frame sync masks up to 64 bits in length.
- [x] The application accepts frame lengths up to 65,536 bits in length.
- [x] The user can select from one of the following PCM code formats: NRZ-L, RNRZ-L.
- [x] The user can define the number of receivers and the number of channels per receiver in the telemetry stream of interest.
- [x] The user can define the scale of the receiver AGC sample data in dB/V.
- [x] The user can define the voltage range of the receiver AGC sample from one of the following values: 0-5V, 0-10V, +/-5V, +/-10V.
- [x] The user can define the polarity of the receiver AGC sample data: positive or negative.

### US2.2: Apply one stream's configuration to all matching streams — Complete

**As a** telemetry engineer or data analyst
**I want to** configure one telemetry stream in the per-stream setup dialog and apply those settings to every other selected stream of the same mode in a single action
**So that** I don't have to re-enter identical parameters across many streams when a file contains several streams with the same format.

**Acceptance Criteria:**

- [x] The Frame Sync Lock and Receiver SNR setup dialogs each provide an "Apply to all `<mode>` streams" toggle next to the OK/Cancel buttons.
- [x] When enabled and the user clicks OK, the entered settings are copied to every stream that is selected for processing and shares the same mode.
- [x] Streams of a different mode are left unchanged.
- [x] Each affected stream is marked configured/ready and its readiness indicator updates.

  - **Scope:** Orthogonal to US3.1 (frame sync error accumulation); split out as its own story. Fan-out copies the same fields written by the gear dialog today (see `openGearDialog` in `streamconfigdialog.cpp`); it does not widen the Frame Sync Load/Save TOML boundary defined in US1.0.

### US3.0: View the framesync lock statistics for IRIG 106 formatted PCM telemetry streams contained in .ch10 files — Complete

**As a** telemetry engineer or data analyst
**I want to** view the framesync lock statistics (percentage of time the telemetry stream is in sync) for PCM data streams in IRIG 106 formatted .ch10 files
**So that** I can view the framesync lock statistics for one or more telemetry streams versus time.

**Acceptance Criteria:**

- [x] The user can view the framesync lock statistics (left hand y-axis) versus time (x-axis) in a plot window.
- [x] The user can select which telemetry stream(s) framesync lock statistics to view in the plot window.
- [x] The user can select the time window to view the framesync lock statistics in the plot window.
- [x] The user can select framesync lock statistics for the following sample windows: 10ms, 100ms, and 1s.
- [x] A progress bar updates as the application is processing the framesync lock statistics.

### US3.1: View Accumulated Missed Frames Over Time — Complete

**As a** telemetry data analyst
**I want to** quantify and plot the accumulated missed frames over time for my PCM streams
**So that** I can quantitatively evaluate how frame sync errors are accumulating over time.

**Acceptance Criteria:**

- [x] The user can view the accumulated missed frames (left hand y-axis) versus time (x-axis) in a plot window.
- [x] The user can switch the left-axis view between framesync lock (%) and accumulated missed frames modes (context menu > View Mode).
- [x] The user can select which telemetry stream's accumulated missed frames to view in the plot window.
- [x] Missed frames are accumulated per telemetry stream against that stream's own frame parameters.

  - **Scope:** A "missed frame" is a discrete loss-of-lock event — while in lock, the stream ran past the expected minor-frame boundary (`bits_in_frame`) without a sync match. The metric is a cumulative count of these events per stream, counted only within the selected `[start, stop]` processing window, and only ever increases. It is NOT a bit-level (Hamming/BER) error count. The user-facing label is "Accumulated Missed Frames" (`PlotConstants::kMissedFramesAxisLabel`); this is the canonical term across the UI, release notes, and code.

### US3.2: View SNR signal data from IRIG 106 formatted PCM telemetry streams contained in .ch10 files — Complete

**As a** telemetry engineer or data analyst
**I want to** view SNR signal data (in dB) derived from telemetry receiver AGC samples (raw integer energy values from 0 to 65,535) included in PCM data streams within IRIG 106 formatted .ch10 files
**So that** I can view the SNR signal data for one or more receiver channels versus time.

**Acceptance Criteria:**

- [x] The user can view the SNR signal data (right hand y-axis) versus time (x-axis) in a plot window.
- [x] The user can select which receiver/channels SNR signal data to view in the plot window.
- [x] The user can select the time window to view the SNR signal data in the plot window.
- [x] The user can select one of the following time periods to average SNR signal data over: 100 Hz, 10 Hz, 1 Hz.
- [x] Samples are converted from raw integer energy values to Volts (V) using the voltage range and polarity.
- [x] Volts are then converted to decimal dB values using the scale (dB/V).
- [x] A progress bar updates as the application is processing the SNR signal data.

### US4.0: Configure plot window navigation and information — Complete

**As a** telemetry engineer or data analyst
**I want to** configure how the processed framesync lock statistics and SNR sample data is displayed in a plot window
**So that** I can quickly analyze data in the plot window.

**Acceptance Criteria:**

- [x] The plot fills the central area of the main application window (it is the window's central widget; the log occupies a toggleable left sidebar).
- [x] The chart occupies the entire plot area: there are no external control rows. Every plot control is reached from a **right-click context menu** on the chart, whose items are disabled until data is loaded.
- [x] The context menu provides: **Plot File** (which processed file to view), **View Mode** (Lock Percentage / Accumulation), **Customize View…**, **Set Plot Title…**, **Show Legend** (checkable), **X Axis** (Set Time Window… / Reset Span), **Y Axes** (Set Left Min… / Set Left Max… / Set Right Min… / Set Right Max… / Reset), and **Reset View**. Export is deliberately not here — it writes a CSV, an image and the log, which is a file operation on the session rather than a plot control, so it lives only in the main menu beside Import CSV.
- [x] The user can specify a custom title for the plot (context menu > Set Plot Title…).
- [x] The left Y axis is labeled with its unit of measure, average framesync lock percent.
- [x] The right Y axis is labeled with its unit of measure, SNR in decibels.
- [x] The bottom X axis is labeled with elapsed file time (DDD:HH:MM:SS), not raw seconds.
- [x] The Y axis automatically scales to the data's min/max values.
- [x] The X axis automatically scales to the full time span of the loaded data.
- [x] The user can set a time window to zoom and pan the X axis to just that range (context menu > X Axis > Set Time Window…, entered as DDD:HH:MM:SS; values outside the file range are clamped with a warning in the log). **X Axis > Reset Span** restores the full span.
- [x] The user can manually override either end of either Y axis (context menu > Y Axes > Set Left/Right Min… and Set Left/Right Max…). A limit left alone still scales automatically; a minimum is taken as given and the maximum yields to it, never closer than `kMinAxisSpan`, so no entry can invert or collapse an axis. **Y Axes > Reset** clears all four overrides.
- [x] The mouse wheel zooms the X axis.
- [x] A mouse click and hold pans the X axis.
- [x] The user can show or hide individual Frame Sync Lock and Receiver SNR series.
- [x] A movable legend overlay floats inside the chart instead of sitting in a separate panel below it, showing a line-swatch and label for each visible series.
- [x] The user can click-drag the legend anywhere inside the plot area to keep it clear of data of interest.
- [x] The legend defaults to the top-right corner and resets there each new session.
- [x] The user can show or hide the legend from a small toggle button overlaid on the chart's top-left corner (mirrored by the context menu's **Show Legend** item). The choice persists across sessions, and a hidden legend is excluded from exported images.
- [x] The legend background is translucent so plot data behind it stays visible.
- [x] The legend's height is capped with a vertical scrollbar for dense plots, such as a Receiver SNR file with 48+ channels.
- [x] Receiver SNR legend rows show a short label (channel id plus receiver/channel code) instead of the full TMATS-derived stream title, though the full name remains available via tooltip and in CSV export headers.
- [x] The legend is composited into exported PNG and SVG plot images at its placed position.
- [x] The user can rename or recolor an individual series from the Customize Plot Series dialog.
- [x] Frame Sync Lock curves are assigned purple, blue, and green primary colors automatically, one per stream.
- [x] Receiver SNR curves are assigned red, orange, and yellow primary colors automatically, one per receiver.
- [x] Additional streams, receivers, and channels beyond the primaries use progressively lighter shades of their primary color so related series stay visually grouped.
- [x] Frame Sync Lock curves are drawn with a visually highlighted style, while Receiver SNR curves are not.

### US4.1: Distraction-free plot window — controls on the plot, not around it — Complete

**As a** telemetry engineer or data analyst
**I want to** see nothing but the plot itself, with every control reached by right-clicking the plot or from a small overlay on it
**So that** the maximum amount of screen space shows data, and the interface stays uncluttered while I analyze.

**Acceptance Criteria:**

- [x] The plot window has no external control rows: the chart fills the entire plot area.
- [x] Every plot control is reached from a right-click context menu on the chart, or from a control overlaid on the chart itself.
- [x] The context menu leads with **Set Plot Title…**, then **Plot File**, **View Mode**, **Customize View…**, **Show Legend**, **X Axis**, **Y Axes** and **Reset View**; entries are disabled until data is loaded. Every entry is a plot control — the menu holds nothing that acts on files.
- [x] The legend can be shown or hidden from a toggle overlaid on the chart, and the choice persists across sessions.
- [x] The persistent on-chart controls are gathered into a single chip bar at the chart's top-left (legend toggle, View Mode chip, Reset view chip) rather than scattered around the plot; each chip hides itself when it would be meaningless, so the bar is empty-by-default clutter-free.
- [x] Frequent actions have chrome-free gestures: Ctrl+drag / middle-drag rubber-bands a time range to zoom, double-click restores the full span, and a crosshair follows the cursor for reading several series at one instant.
- [x] Mouse-wheel zoom and click-drag pan of the X axis are retained as the primary navigation.
- [x] The log/console occupies a left sidebar that the user can toggle on and off, and which is open by default.
- [x] Each on-chart chip is labelled and carries a tooltip naming the action it performs, so no control on the chart is an unexplained glyph.
- [x] The crosshair readout can be pinned to a chosen series (context menu > **Readout**) instead of always following whichever curve is nearest the cursor.
- [x] The most-used view actions have single-key shortcuts while the chart has focus - arrows step the time window, `+`/`-` zoom, `Home` restores the full span, `R` resets both axes, `V` switches metric, `L` toggles the legend - and the context menu advertises each key so they are discoverable rather than hidden.

  - **Delivered:** keyboard shortcuts now exist (criterion above). They live in
    `PlotWidget::keyPressEvent` and are deliberately **widget-scoped**, not
    application-wide: a bare-letter application shortcut is dispatched ahead of the
    focus widget, so it would swallow that character everywhere the user types (the
    plot-title dialog, the time-window fields, any config dialog). A click on the
    chart focuses it, which is what makes them reachable.
    Any *further* plot control that gets identified goes to the context menu
    or an on-chart overlay, never back to an external widget; that's the standing
    rule this story sets, and `TestPlotWidget` enforces it (no QComboBox /
    QSpinBox / QLineEdit / QAbstractButton may live outside the chart).
  - **Advertising them** took two forms, because Qt only renders a `QKeySequence` on a
    plain action in a context menu (and only with `setShortcutVisibleInContextMenu`):
    `L`, `Home` and `R` are real shortcuts on their actions, while `V` is spelled into
    the **View Mode** submenu title. `TestPlotWidget::contextMenuAdvertisesEveryPlotShortcut`
    pins all four, including the visible-in-context-menu flag - without it the key is
    set but invisible, which is the state this change fixed.
  - **Scope:** This story governs the *placement* of plot controls, not what they do — the underlying behaviors are specified by US4.0 (navigation/appearance), US3.1 (view mode), and US1.1 (Plot File selector). It is deliberately additive to those stories: US4.0's criteria were reworded for the new location, not replaced.

### US5.0: Recall/store framesync pattern and frame length parameters from/to configuration files — Complete

**As a** telemetry engineer or data analyst
**I want to** recall/store the parameters used to process framesync pattern and frame length in a configuration file
**So that** I don't have to re-enter the parameters every time I open the application.

**Acceptance Criteria:**

- [x] Configuration files include the framesync pattern and frame length.
- [x] User is able to save the framesync pattern and frame length to a configuration file.
- [x] User is able to recall the framesync pattern and frame length from a configuration file.
- [x] The configuration file is stored in a user specified location.
- [x] The configuration file is stored in TOML format.

### US5.1: Recall/store the default SNR signal data parameters from/to configuration files — Complete

**As a** telemetry engineer or data analyst
**I want to** recall/store the parameters used to process SNR data in a configuration file
**So that** I don't have to re-enter the parameters every time I open the application.

**Acceptance Criteria:**

- [x] Configuration files include the default parameters used to process SNR data.
- [x] User is able to save default parameters to a configuration file.
- [x] User is able to recall default parameters from a configuration file.
- [x] The configuration file is stored in a user specified location.
- [x] The configuration file is stored in TOML format.

### US5.2: Edit default SNR signal data parameters in configuration files via a dialog window — Complete

**As a** telemetry engineer or data analyst
**I want to** edit the default key parameters used to process SNR signal data streams via a dialog window
**So that** I can update the default key parameters to meet the requirements of different telemetry streams formats.

**Acceptance Criteria:**

- [x] The dialog window includes the following default parameters used to process SNR signal data streams:
  - number of receivers
  - number of channels per receiver
  - scale
  - voltage range
  - slope
- [x] The dialog window includes a "Reset" button to reset the key parameters to their default values.
- [x] The dialog window includes a "Save" button to save the key parameters to the configuration file.
- [x] The dialog window includes a "Cancel" button to cancel the operation.

### US5.3: Non-Linear Receiver SNR Step Calibration — Complete

**As a** telemetry engineer or data analyst
**I want to** apply a non-linear step calibration to Receiver SNR streams using a Calibration CH10 file and a TOML step configuration
**So that** I can accurately groom out receiver non-linearities and plot true SNR values instead of relying on a simple linear slope/offset.

**Acceptance Criteria:**

- [x] The user can enable non-linear step calibration for a specific Receiver SNR stream in the per-stream setup dialog (via the "Extract Calibration…" action).
- [x] The user can load a TOML file defining the expected step values (in dB).
- [x] The user can load a Calibration CH10 file containing the recorded step data.
- [x] The Apply Cal dialog provides optional Clip Start and Clip End controls to trim leading or trailing seconds from the calibration recording before step detection.
- [x] The application automatically extracts "ideal" steps from the CAL file.
- [x] The application evaluates step extraction success independently for each receiver channel (up to 48).
- [x] Channels that successfully map the expected number of steps are assigned a non-linear calibration profile for the session.
- [x] Channels that fail step extraction (e.g., due to noise or no data) fall back to the standard linear (slope/offset) calibration.
- [x] A summary message box informs the user which channels succeeded and which fell back.
- [x] The summary flags a result that looks successful but may not be: a receiver whose sweep held more levels than the step file lists, and a receiver with one channel swept and another flat (the signature of a word map that does not match the recording).
- [x] Clip values that exceed the recording are reported rather than silently ignored.
- [x] The step-config TOML is preloaded when the dialog opens — the file this stream last used, else the shipped default — and the status says it was loaded automatically.
- [x] During main data processing, the application applies the non-linear calibration profile using piece-wise linear interpolation between steps.
- [x] Raw values outside the calibrated range clamp to the nearest end-step dB instead of extrapolating, so a receiver driven past the calibrated range reads the ceiling or floor value.

  - **Saturated receivers (v2.12.0):** a receiver driven past its converter's range
    mid-sweep is now calibrated over the steps that stayed in scale, with readings clamped at
    the top measured step, instead of losing the whole channel to the linear fallback.
  - **Summary report (v2.12.0):** the summary criterion above is now met literally — the message box names the calibrated receivers ("Calibrated: receivers 1, 3, 5, 6.") and lists each receiver that fell back with its channels and the reason, where it previously reported only a channel count.
  - **Status (v2.2.5): COMPLETE.** Step selection is polarity-agnostic and robust to real recordings: it takes the first maximal monotonic plateau run and keeps its last `expected` plateaus, which excludes a signal-generator turn-on transient (leading) and the optional operator down-ramp (trailing) for both normal and inverted-polarity receivers — fixing the calibrated-staircase time skew. The extraction sample period is frame-rate-adaptive (100 ms floor) for fast frames, and the Apply Cal dialog adds optional **Clip Start / Clip End** controls so operators can trim leading/trailing seconds before detection. Out-of-range raw values clamp (not extrapolate) to the nearest end-step dB.

### US6.0: Export the plot as an image file — Complete

**As a** telemetry engineer or data analyst
**I want to** export the current plot to an image file (e.g. png, pdf, etc.)
**So that** I can drop it into third-party applications such as PowerPoint to build reports and presentations without re-processing the source data.

**Acceptance Criteria:**

- [x] The current plot is exported as a single image containing every visible Frame Sync Lock, Accumulated Missed Frames, and Receiver SNR series.
- [x] The user selects the image format from one of the following: svg, png, pdf.
- [x] The user specifies the image filename and location via the export dialog.
- [x] The movable legend is rendered into the exported image at its on-plot position.
- [x] A single export dialog lets the user select any combination of image, data, and log exports and run them in one action.

### US6.1: Export the plotted data to a CSV file — Complete

**As a** telemetry engineer or data analyst
**I want to** export the plotted data (framesync lock statistics, accumulated missed frames, and SNR signal data) to a CSV file
**So that** I can import the file into third-party applications such as Excel and MATLAB for further analysis.

**Acceptance Criteria:**

- [x] The current plot data is exported to a single CSV file covering every visible Frame Sync Lock, Accumulated Missed Frames, and Receiver SNR series.
- [x] The CSV file includes a timestamp column and one data column per series.
- [x] Each column header identifies its stream and receiver/channel so the exported data is self-describing.
- [x] The user specifies the CSV filename and location via the export dialog.

### US6.2: Export the log window contents to a text file — Complete

**As a** telemetry engineer or data analyst
**I want to** export the contents of the log window to a text file
**So that** I can keep a record of processing messages, warnings, and calibration results for later review.

**Acceptance Criteria:**

- [x] The current log window contents are exported to a plain-text file.
- [x] The user specifies the log filename and location via the export dialog.

### US6.3: Import a previously exported CSV file to visualize historical data — Complete

**As a** telemetry engineer or data analyst
**I want to** open a CSV file that this application exported in the past (US6.1) and load it directly into the plot window
**So that** I can review and visualize old data sets without re-processing the original .ch10 file.

**Acceptance Criteria:**

- [x] The user can open a previously exported CSV file via the menu's **Import CSV…** entry (Import/Export section), drag-and-drop of a `.csv` file, or the Recent Files list.
- [x] Import CSV… opens a CSV-only file dialog and is disabled while processing.
- [x] **Open…** remains Chapter-10-only; Import CSV… is the dedicated CSV entry point.
- [x] Importing a CSV bypasses the Configure Streams dialog and processing pipeline, parsing the file straight into the plot.
- [x] The importer accepts the application's own export format: a `Time (DOY:HH:MM:SS.mmm)` first column followed by one column per series.
- [x] Frame Sync Lock (`Lock (%)` suffix), Accumulated Missed Frames (`Accumulated Missed Frames` suffix), and Receiver SNR (`_RCVR<N>`) series are recognized by their column headers and routed to the correct axis, metric type, and color.
- [x] Imported series populate the plot, the on-plot legend, and the Customize Plot Series dialog the same way processed data does (axis auto-ranging, time window, visibility toggles all apply).
- [x] Imported data can be re-exported (CSV/image) and the recent-files list records the imported CSV.
- [x] A CSV that is not in the application's export format (e.g. the legacy `Day,Time,…` layout, a wrong/missing header, or a file with no parseable data rows) is rejected with a clear error in the log rather than producing garbage series.
- [x] Malformed or short data rows are skipped, and the user is informed how many rows were skipped.

  - **Scope:** Reuses the existing Model/ViewModel import path (`CsvSeriesParser::parse` + `PlotViewModel::loadCsvFile` / `loadCsvFileAsync`), which is the matched partner of `exportCsv`; this story is primarily the View-layer wiring (menu/drag-drop entry, format routing on file open) plus user-facing error reporting. It does not add a CSV import dialog with column mapping, and it does not attempt to import arbitrary third-party CSVs — only files this application produced.

### US7.0: Error Checking — Complete

**As a** telemetry engineer or data analyst
**I want to** ensure the values I enter into the application are valid
**So that** I can avoid errors and ensure the data I export is accurate.

**Acceptance Criteria:**

- [x] Ensure all input fields are validated.
- [x] Error messages are displayed to the user when invalid values are entered.
- [x] Application ensures frame sync pattern only contains hexadecimal values.
- [x] Application ensures frame sync pattern is no larger than the user specified frame length.

### US8.0: Application Installer — Complete

**As a** developer
**I want to** create an application installer
**So that** I can quickly deploy the software/updates to users with all the necessary folders and settings files.

**Acceptance Criteria:**

- [x] The installer produces a code-signed application executable.
- [x] The installer can install to "Program Files" for admin users.
- [x] The installer can install to a user-selected directory for users without admin privileges.
- [x] The installer shows progress while installing.
- [x] The installer never overwrites an existing user TOML settings file.
- [x] If the shipped default TOML has new fields the user's file lacks, the installer saves the new version as "new_x.toml" instead of overwriting the user's file.
- [x] The installer carries over as many parameter values as possible from the user's old TOML file into the new "new_x.toml" file.

### US9.0: Switch between light and dark theme — Complete

**As a** telemetry engineer or data analyst
**I want to** switch the application's visual theme between light and dark
**So that** I can use the application comfortably in different lighting conditions or to match my system preference.

**Acceptance Criteria:**

- [x] The user can toggle between light and dark theme via a menu action.
- [x] The selected theme persists across application restarts.
- [x] All windows, dialogs, and the plot update to match the selected theme immediately.
- [x] The menu and title-bar icons swap to theme-appropriate variants when the theme changes.

## Version History

### Unreleased — since the v2.12.1 tag

#### The menu carries icons, the tests carry the resources, and both manual tabs are shown (US1.0, US8.0)

- **Open and Save as Template carry the folder and save glyphs** - the same
  `folder-open.svg` / `floppy-save.svg` the setup dialogs use for Load/Save, not new
  artwork: a menu entry and a toolbutton that both mean "open a file" should not be
  drawn two ways. They carry their own colour and read on both themes, so unlike
  export/import they need no per-theme variant.
- **The test binary had no Qt resources at all.** `RESOURCES` was listed inline in the
  app `.pro`, and qmake derives each alias from the file's path *relative to the .pro
  that lists it* - so the app resolved `:/resources/folder-open.svg` while a test
  building from `tests/` resolved the same file as `:/folder-open.svg`. Every icon in
  every widget under test was a null QIcon, and 96 `qt.svg` warnings scrolled past on
  each run. Nothing failed, because a widget whose icon is missing still constructs and
  still lays out - it simply draws nothing, which is precisely the class of defect the
  UI tests exist to catch. A shared `resources.qrc` (paths inside a `.qrc` resolve
  relative to the `.qrc`) plus `resources.pri` gives both binaries the same prefix.
  **96 warnings to 0.**
- `openAndSaveTemplateCarryIcons` asserts both icons are non-null *and* that each
  produces a non-empty pixmap - a QIcon built from a missing or malformed SVG is
  non-null but draws nothing, so the first check alone would pass on artwork that never
  appears.
- **The base manual now shows both Customize View tabs.** The Receiver SNR tree was in
  the walkthroughs only - and the walkthroughs are an *optional* install, so a user who
  declined them saw one half of a two-tab dialog. Same reasoning as v2.12.1 moving the
  receiver SNR field descriptions into the base manual.
- **The manual build now fails on a capture that reaches neither manual**, and warns on
  a capture sitting in `docs/manual_images/` that is registered nowhere (three were:
  two deliberate legend-variant spares and `walk-app-02-sidebar.png`). A capture can be
  placed two ways - a `fig()` call or a `{{FIG:}}` placeholder - and checking only one
  reports figures as orphaned when they are not; both funnel through `fig()`, which is
  what makes the check honest. `build_manual.ps1` also clears `processed/` first: it is
  gitignored build output, and three stale files from renamed captures had accumulated
  there, enough to make any check against that directory meaningless.
- `Main Context Menu.png` is **re-captured** for the menu without Export. Its crop
  rectangle changed with it - the new capture has the log sidebar hidden, and the old
  rectangle began at x=425 to skip a sidebar that was open then, so reusing it would
  have cut off the Y axis.

#### Export leaves the plot's context menu, and Exit moves to the bottom (US4.1, US6.0)

Two menu corrections, both about what belongs where.

- **Export is no longer on the plot's right-click menu.** It writes a CSV, an image and
  the log - a file operation on the session, not a plot control - and the menu it sat in
  is otherwise entirely plot controls. It keeps its home in the main menu's
  Import/Export section, beside its counterpart Import CSV, so there is one place to
  look for it rather than two. v2.8.0 added it to the plot menu *as well*; that is what
  is being undone. `PlotWidget::onExportPlot()` is unchanged and still the handler - the
  main menu's action connects to it directly, so nothing is dead.
  The `TestPlotWidget` case now asserts Export is **absent**, so restoring a second home
  for it is a test failure rather than a quiet regression.
- **Exit is the last entry in the main menu**, below the Help submenu, instead of sitting
  inside the Process section with the file commands. It ends the application rather than
  doing anything to a file, and the bottom of the menu is where a quit command is looked
  for. `exitIsTheLastMenuEntry` pins the position, which is precisely what a later
  addition to the menu breaks without breaking anything else: a new action appended after
  it still works, still looks reasonable, and quietly moves Exit off the bottom. Verified
  to fail with Exit back in the Process section.

**This invalidates the manual's context-menu figure** (`Main Context Menu.png`), which
shows the `Export...` row. It needs re-capturing before the next release.

#### Either end of either Y axis can be pinned (US4.0, US4.1)

Only the maxima were settable, and the axis that most needed a minimum was the one that
could not have one: a healthy stream's lock % lives in the top two or three percent of a
0-100 axis, where several streams overlap into a single line at the top of the plot. Set
the left minimum to 98 and the difference between 98.4% and 99.7% fills the plot.

- **`Y Axes > Set Left Min…` and `Set Right Min…`** join the two maxima in the context menu,
  each axis' pair kept together. `PlotViewModel` gained `leftYMin()` and the four
  `has/valueOverride` accessors to match, plus `hasYRangeOverride()` - which the Reset chip
  now asks instead of testing the two maxima it used to know about.
- **One rule decides a crossed pair, on both axes: the minimum is taken as given and the
  maximum yields**, never closer than `kMinAxisSpan`. With both limits settable they can be
  driven past each other, and a chart handed an inverted range still draws something. The
  dialogs also bound each entry by the opposite limit, so it takes a later change to the
  other end to reach the guard at all.
- `updateAxes()` passed a literal `0.0` as the left minimum, which would have made a working
  menu item and a dead one look identical. `yMinOverrideReachesTheChartAxis` pins the axis
  minimum to the bottom edge of the plot area; it was verified to fail against that literal.

#### The plot repaints in milliseconds: cached geometry, culling, and drawLines() (US4.0)

Follows the per-pixel-column envelope directly below. That change predicted the remaining
cost was "the per-sample loop that builds the envelope - not the path or the rasterizer."
**That prediction was wrong, and measuring it is what found the real bottleneck.**

Instrumenting the draw showed the envelope builder was not the problem: caching the built
path dropped rebuilds per repaint to zero and the repaint took exactly as long as before.
Almost all of it was inside the single `QPainter::drawPath()` call - and the time did not
track the element count, which is what made it worth chasing. 1M samples x 3 series and
360k x 12 produce 12,738 and 50,880 path elements, yet the first took 1,327 ms and the
second 1,512 ms. The cost was not the geometry, it was stroking it.

Same geometry, same 1.5 px pen, per repaint on a 1200 px plot:

| case | `drawPath()` | `drawPolyline()` | `drawLines()` |
| --- | --- | --- | --- |
| 360k x 3 | 343 ms | 340 ms | **6 ms** |
| 360k x 12 | 1,423 ms | 1,725 ms | **27 ms** |
| 1M x 3 | 1,815 ms | 1,552 ms | **12 ms** |

A non-cosmetic pen sends both a path and a polyline through the stroker, which builds an
outline polygon across the whole run; `drawLines()` strokes each segment on its own and
skips it. Disabling antialiasing was tried first and rejected - it helped zoomed views,
left full-span repaints unchanged and made the 1M case *worse* (1,327 → 1,815 ms).

Three changes, all in `TmChart`:

- **Geometry is kept as `QVector<QLineF>` and drawn with one `drawLines()` per series.**
  `seriesPath()` became `seriesSegments()`. A gap in the data breaks the line by emitting
  no segment across it, which a `QPainterPath` needed a subpath to express. Steep slopes
  were checked by rendering, not just measured: segment ends meet cleanly at zigzag peaks.
- **The segments are cached per series** and rebuilt only when something they were built
  from changes - the data, either Y range, the X range, or the plot area. A crosshair move,
  a tooltip or a legend drag repaints without touching any of those, and now costs nothing
  to prepare.
- **Only the samples in view are built**, by binary search on the times when they are
  sorted (`Series::sortedX`, computed once in `setSeriesData()` rather than assumed - the
  chart is general, only `PlotViewModel`'s time series are ordered by construction). One
  sample is kept on each *side* of the range: zoomed in far enough, the view can contain no
  sample at all, and the line crossing it is still real data.

Release build, one hour of data, per paint:

| samples/series | series | first paint | repaint | zoomed to 1% |
| --- | --- | --- | --- | --- |
| 360,000 | 3 | 36 ms | **8 ms** | 4 ms |
| 360,000 | 12 | 157 ms | **34 ms** | 20 ms |
| 1,000,000 | 3 | 79 ms | **13 ms** | 5 ms |

The twelve-channel repaint that opened this work at **102 seconds** is now **34 ms**.

Two tests, each verified to fail against the defect it describes:

- `linePersistsWhenNoSampleIsInsideTheView` - a window between two sparse samples still
  draws the line. Without the one-sample margin the plot goes blank at exactly the zoom an
  operator uses to read a value off it.
- `rangeChangeRedrawsInsteadOfReusingGeometry` - a step function zoomed onto its high half
  must move up the image. A stale cache shows the previous view, which still looks like a
  plausible plot - the hard kind of wrong.

#### Image export and the frameless-window handlers move out (US6.0, US4.1)

The last of the file splits; the two remaining large View files are now ordinary sizes.

- `plotexport.cpp` takes `onExportPlot()` and `exportImage()` out of `plotwidget.cpp`
  (1398 → 1239). One `TmChart::renderTo()` serves screen, PNG, SVG and PDF, so the three
  formats cannot drift - which is the point worth keeping together in one file.
- The frameless-window handlers - `nativeEvent()`, `changeEvent()` and
  `updateMaximizeButton()` - join `mainviewchrome.cpp` rather than getting a file of their
  own: they *are* the title bar's behaviour (caption hit-testing, resize borders, the
  maximize glyph), and the bar itself is already there. `mainview.cpp` 986 → 833.
- The guarded `<windows.h>` / `<windowsx.h>` block moved with `nativeEvent()`, its only
  user. `mainview.cpp` no longer includes any Win32 header.

Where the View files stand after the whole pass:

| File | Before | After |
| --- | --- | --- |
| `plotwidget.cpp` | 1878 | **1239** |
| `mainview.cpp` | 1564 | **833** |
| `streamsubdialogs.h` | 1288 | **301** (declarations) |

#### The plot's right-click menu is its own translation unit (US4.1)

- `plotcontextmenu.cpp` takes `buildContextMenu()` (200 lines) and its caller
  `showPlotContextMenu()` out of `plotwidget.cpp`, which drops from 1608 to 1398 lines.
  Every plot control is reached from that menu or an on-chart chip, so building it is a
  long run of `QAction` wiring with little to do with the rest of the widget.
- Still `PlotWidget` members: the handlers they connect to mutate the widget, so the menu
  is not separable into a type of its own without inventing an interface for them to call
  back through. A file split, not an extraction - as the plan said it would be.
- It needed nothing else to move, unlike the chrome split: `kReadoutNearest` is already a
  static member on the class, and `plotwidget.cpp`'s two file-local helpers
  (`legendDisplayName`, `isLeftAxisMetric`) belong to the legend and chart rebuilds rather
  than the menu. `<QMenu>` and `<QActionGroup>` left `plotwidget.cpp` with it.
- Covered by the six `TestPlotWidget` context-menu cases, which assert the item list, the
  disabled-until-data states, the Plot File submenu, the View Mode round trip, the reset
  actions and the advertised shortcuts - the strongest test coverage of anything split so
  far, so no render check was needed here.

#### The window chrome is its own translation unit (US4.1)

- `mainviewchrome.cpp` takes the hamburger menu, the recent-files submenu, the
  theme-drawn glyphs and the custom title bar out of `mainview.cpp`, which drops from
  1333 to 986 lines. These are still `MainView`'s own members - the class is unchanged -
  but menu and title-bar construction is self-contained and reads better away from the
  window's wiring.
- **`setUpMenuBar()` is now `setUpChrome()`**, which is what it always did: its own
  comment said "the menu hangs off the hamburger button in the custom title bar (built at
  the end of this method)". The name described a third of the function.
- **Nine title-bar constants moved to `UIConstants`** - `kTitleBarHeight`,
  `kTitleBarEdgeGap`, the three `kToolGlyph*` metrics and the four caption-button
  codepoints. They were file-local to `mainview.cpp`, which was fine while the title bar
  and the window lived in one file; the split makes them shared, and a file-local copy in
  each file is how two sources of truth start. `TestConstants` pins all nine - the glyphs
  especially, since they are private-use-area codepoints where a wrong value is a blank
  button that no other test would notice.
- Also cleaned up: the empty anonymous namespace `describeMismatch()` left behind in
  `mainview.cpp` when the batch loop moved, and the two orphaned Doxygen `@}` closers its
  constants left when they moved to `UIConstants`.
- **Verified by rendering.** The title bar was grabbed and measured before and after: the
  PNGs are byte-identical and all five buttons report the same geometry (`30x26` at the
  same positions, same icons). Pure code motion, but the constants moved with it, and a
  mistyped glyph codepoint would not have failed a single test.

#### The legend is its own widget (US4.0)

- **`PlotLegendOverlay`** (`include/view/plotlegendoverlay.h`) owns the movable legend:
  its rows, its size, where it sits, how it is styled, and the drag that moves it. It was
  eight `PlotWidget` methods, six members and a nested `LegendRow` struct, with the drag
  spread across `PlotWidget::eventFilter()`.
- `PlotWidget` decides *what* to list - which series, in which order, and whether the user
  wants the legend shown - and hands over a finished `QVector<Entry>`. The overlay knows
  nothing about `PlotViewModel`.
- The drag moved with it: presses on the frame's padding are ordinary mouse events on the
  overlay now, and presses on the click-through rows still arrive via a filter the overlay
  installs on its own scroll viewport. `PlotWidget::eventFilter()` keeps only the
  chart-resize hook, which calls `reposition()`.
- `rowCount()` / `rowAt()` / `rowGutter()` are observation points for the tests, the same
  role `TmChart`'s accessors play - the tests no longer reach through a friend declaration
  into a layout.
- `plotwidget.cpp` 1878 → 1608 lines.
- **Verified by rendering, not by the suite passing.** The legend was rendered with three
  series before and after the extraction (`exportImage`, offscreen) and the PNGs are
  **byte-identical**; the measurements (geometry 950,8 234x54, three rows, 14px scrollbar
  gutter, 338-char stylesheet) match exactly. The `verify-ui-change` skill exists because
  the suite asserts almost nothing about appearance, and this is a View refactor.

#### The batch loop leaves the View (US1.1)

- **`MainView` ran a state machine.** Nine members and eight `m_batch_*` fields drove
  Apply Template's sequential run: the queue, the file in flight, the per-file config
  application, the appearance reuse and the export post-pass. `docs/CLAUDE.md` documented
  where it lived, which made it known rather than accidental - but it is business logic,
  and the project's own rule is that the View displays.
- **`BatchController` (ViewModel layer) owns it now.** The View keeps the half that is
  genuinely the View's: `MainView::confirmAndStartBatch()` shows the confirmation dialog,
  and an injected `ImageExporter` callback renders each per-file image, because only the
  widget can draw the plot. The controller reports through `message(LogLevel, QString)`
  and `finished()` instead of calling `logError()` / `logWarning()` directly.
- `validate()`, `buildTemplate()` and `applyAppearance()` are static and dialog-free, so
  the parts worth testing no longer need a window. `describeMismatch()` moved with them.
- `MainView` drops ~330 lines (1564 → 1333) and two includes it no longer needs.

#### StepDetector::detect() is an orchestrator, not a 649-line function (US5.3)

No behaviour change - the real-recording pairing assertions on
`step_cal, example.ch10` hold throughout, which is what made the split safe to
attempt at all.

- `detect()` was **649 lines** holding nine numbered stages and five lambdas,
  two of which (`selectSweep`, `calibrateSaturatedTop`) were themselves 66 and
  260 lines. It is now **185 lines** that read as the algorithm's outline:
  derivative, noise sigma, threshold, the relax loop, sweep selection, the
  saturated-top fallback, pairing.
- Eleven file-local helpers carry the stages, each taking what it needs
  explicitly rather than capturing the enclosing scope: `sampleDerivative`,
  `robustStdDev`, `liveNoiseSigma`, `markStableSamples`, `confirmedPlateaus`,
  `levelsOf`, `coalesceLevels`, `valuesOf`, `transitionDirection`,
  `selectSweepStart`, `calibrateSaturatedTop`.
- The saturated-top path is split further: `isRailedLevel`, `buildDwellGrid`
  (with a `DwellGrid` struct for the boundaries, their step slots and the dwell
  length) and `measureUnconfirmedDwells` - the last being the subtlest logic in
  the file, the part that stops a dwell plateau detection missed from being
  bridged by interpolation (which read 23.09 dB at a 24 dB dwell).
- `Plateau` and `robustStdDev` move out of the header: neither had a caller
  outside this translation unit, so `StepDetector` no longer has a private
  section at all.
- The `1.`/`7b.`/`7d.` stage numbering is gone with the lambdas it indexed - the
  function names carry it now.

#### The stream setup dialogs are testable (US1.0)

- **`streamsubdialogs.h` held 1288 lines of implementation in an anonymous
  namespace**, private to the one translation unit that included it
  (`streamconfigdialog.cpp`). Nothing else could name the types, so no test could
  construct a dialog - which is why the receiver-parameters Save/Load round trip
  and the step-file preload had to be checked by hand, from screenshots.
- The header is now **301 lines of declarations**; `src/view/streamsubdialogs.cpp`
  carries the definitions. The shared layout helpers stay file-local in an
  anonymous namespace there, except the four `StreamConfigDialog` also builds its
  own rows with (`styleIconButton`, `makeDialogButtons`, `addBottomBar`,
  `loadReceiverFrameDefaults`), which are declared in the header. The widget
  includes stay in the header rather than becoming forward declarations, because
  the inline accessors read their widgets.
- No behaviour change: same classes, same members, same call sites.

#### Tests

- `TestStreamConfigDialog::subDialogsRoundTripAStreamConfig` builds a
  `ReceiverSNRDialog` and a `FrameLockSetupDialog` from a config that differs from
  the defaults in every field and reads all of it back, including
  `frameSyncParams()` agreeing with the individual accessors - a widget wired to
  the wrong field was previously invisible to the suite.
- `TestStreamConfigDialog::calibrationDialogPreloadsTheStepFile` pins the v2.12.1
  preload from the dialog rather than from `StepDetector`: the shipped default
  when the stream has no file, the stream's own file when it has one, and the
  default again when a stored path no longer resolves.
- `TestTmChart::denseSeriesStillDrawsASingleSampleDropout`: 200k samples, flat, with one
  sample at the opposite end of the range, placed **mid-column** - computed from the
  rendered plot area rather than guessed. The first version of this test put the dropout at
  exactly half the series, which (x being the sample index, the mapping linear) makes it the
  column's first sample and therefore drawn either way; it passed against the reduction it
  exists to reject. With the dropout mid-column it fails against a first/last-only envelope
  and passes against min/max, which was verified both ways.
- The two tests that guarded the old boundary **never called the production code**: they
  wrote a TOML by hand with `QSettings` and asserted that a key they had not written was
  absent. That is why nothing failed when `Inverted` started being written.
- They are replaced by two that drive the real functions, which are now declared in
  `streamsubdialogs.h` rather than being file-local:
  `tomlFrameSyncRoundTripsStreamFormatFlags` (save → file → load, both flags, plus a
  legacy file with no key leaving the checkbox alone) and `tomlFrameSyncOmitsPerSessionFields`
  (Data Rate and Sample Rate absent from a real save). The round-trip case was verified to
  fail against the old save: *Actual 0, Expected 1*.
- New suite **TestBatchController**. `buildTemplateCapturesConfigsAndAppearance` and
  `applyAppearanceRenamesMatchingSeries` moved here from `TestMainView` - same
  assertions, no `MainView` required, and they reach the real production path rather
  than a friend-accessed private member. Two further cases the loop's old home made
  impractical: a queue whose files have all gone missing drains to `finished()` with
  both counted as skipped (rather than stalling on a reader that never opens), and
  `start()` with nothing matched neither clears the session nor announces a run.
- Baseline **398 / 0 / 1** across **21 suites**, zero warnings.

#### The plot draws at pixel resolution, not sample resolution (US3.0, US3.2)

Reported as the plot "choking" on 10 ms data. Measured before changing anything, on a
1200x700 plot, synthetic SNR series (debug build):

| samples/series | series | addStreamData | rebuildChart | paint |
| --- | --- | --- | --- | --- |
| 10,000 | 3 | 46 ms | 0 ms | 310 ms |
| 100,000 | 3 | 91 ms | 0 ms | 4,245 ms |
| 360,000 | 3 | 194 ms | 0 ms | 30,197 ms |
| 360,000 | 12 | 936 ms | 0 ms | **102,126 ms** |
| 1,000,000 | 3 | 477 ms | 0 ms | 84,689 ms |

One hour of twelve channels at 10 ms is 360k samples each, and **every repaint took 102
seconds** - and a repaint happens on every pan, zoom, crosshair move and tooltip. Painting
was over 99% of the cost and grew faster than linearly (10x the samples cost 13.7x the
time), which is `QPainterPath` accumulating millions of segments.

- `TmChart::seriesPath()` now draws a dense series from its **per-pixel-column envelope**:
  for each column, the first sample, the column's minimum and maximum, then the last.
  A plot 1200 px wide cannot show more than 1200 columns, so beyond
  `PlotConstants::kMaxSamplesPerPixelColumn` (2) samples per column the extra points cost
  time and change nothing on screen. Below that threshold every sample is still drawn.
- **Min and max are what keep it honest.** Taking every Nth sample would drop a one-sample
  dropout whenever it fell between the kept samples - and on this plot that lone sample is
  the event an operator is hunting for. A spike is always its column's min or max.
- **The samples are untouched.** `PlotViewModel` still holds every point, so CSV export,
  the crosshair readout and the Y-range computation are unaffected. This is a drawing
  decision, made where the drawing happens.

Release build, after:

| samples/series | series | paint before (debug) | paint after (release) |
| --- | --- | --- | --- |
| 360,000 | 3 | 30,197 ms | **371 ms** |
| 360,000 | 12 | 102,126 ms | **1,483 ms** |
| 1,000,000 | 3 | 84,689 ms | **1,422 ms** |

**What is left.** (Superseded - see the entry above. Measurement showed the cost was the
rasterizer after all, not this loop.) The remaining cost is the per-sample loop that builds
the envelope - 4.3M samples for the twelve-channel case - not the path or the rasterizer. Two further
steps would address it, in this order: cache each series' path and rebuild it only when
the data, the ranges or the plot area change (a crosshair move or a legend drag would then
cost nothing), and cull to the visible X range by binary search on the sorted times (a
zoomed-in view would cost in proportion to what it shows). Neither is in this change.

#### Derandomize is saved with the frame-sync pattern (US1.0)

- **Reported as: "the randomized setting is not saved to the TOML files."** It was not, by
  design - but `Invert Data`, the checkbox directly below it, *was*, and had been since the
  calibration work. Two adjacent flags of the same kind, one persisted and one not, with
  nothing in the docs explaining the split.
- `saveFrameSyncToToml()` now writes `Randomized` alongside `Inverted`, and
  `loadFrameSyncFromToml()` reads it back. Both describe the recording's PCM code format
  (US2.0: NRZ-L vs RNRZ-L), like the sync pattern itself, so a pattern file is only usable
  if it carries them.
- **A file with no `Randomized` key leaves the operator's current choice alone.** Every
  pattern file written before this - including the shipped PRN-11 and PRN-15 files - has no
  such key, and defaulting them to false would silently clear a setting that was already
  made. `Inverted` still defaults to false when absent; that asymmetry is left alone here
  rather than changed on the way past.
- `Data Rate` and `Sample Rate` stay out: the first is usually TMATS-derived, the second is
  an output resolution chosen per session.
- The US1.0 scope note and the `CLAUDE.md` hard rule are updated to match. The rule said
  "do not widen them past that boundary", so widening it is a deliberate change to a
  documented invariant, not a quiet one.

### v2.12.1 — Calibration Warnings, a Preloaded Step File, and a Receiver-Parameters File That Round-Trips

#### Calibration warns when a result looks right but may not be (US5.3)

Three setup mistakes found while fixing v2.12.0 all produced results that looked
successful. Each now says so.

- **A step file shorter than the recording.** The 8-step `rcvr_cals/RASA.toml` on the
  eleven-level `step_cal, example.ch10` sweep "calibrated 12 of 12" while pairing every
  step with the wrong level: the detector keeps the run's last `expected` levels and sets
  the rest aside as lead-in, by design. `StepDetector::Result::sweepLevels` now reports
  how many levels the run held, and `summarize()` adds a **Check the step file** section
  when a calibrated channel's sweep exceeds the file by more than
  `CalibrationConstants::kMaxLeadInLevels` (1) - one extra level is the ordinary turn-on
  transient, and warning on it would fire on healthy recordings. Real report:
  `Receiver 3 — 11 levels in the recording, 8 in the step file`.
- **A word map that does not match the recording.** RASA's four-word-card map on a
  three-words-per-receiver recording files words under the wrong receivers and reports
  live channels as dead. `summarize()` adds a **Check the word map** section for any
  receiver with one channel calibrated and another flat / no signal - the signature a
  shifted map leaves. A dead channel looks the same, so it is worded as a prompt. A
  receiver with every channel flat (most of them, in a calibration recording) does not
  trigger it.
- **Clips that exceed the recording.** Clip Start + Clip End adding up to more than the
  recording are ignored and the whole recording used; the dialog gave no sign. The
  extractor now records it (`clipIgnored()`, `recordingSeconds()`), logs it, and the
  Extract Calibration dialog shows it in amber beside the channel count with a new
  `Warn` status. Clip semantics are unchanged.
- **The step file is preloaded.** Almost every calibration uses the same step file, so the
  Extract Calibration dialog now opens with one loaded instead of empty:
  `StepDetector::initialStepConfigPath()` prefers the stream's previous step file (an
  earlier extraction, or a template's stored reference) and falls back to the shipped
  `settings/rcvr_cals/default.toml`; a stored path that no longer exists falls through to
  the default. The status reads *"11 steps parsed (loaded automatically)"* so the file in
  play is always visible - a wrong step file still calibrates, just wrongly. Browse still
  overrides it. `CalibrationSetupDialog` takes the initial path; parsing moved into
  `loadStepFile()`, shared by the preload and Browse.
- Both "check" sections are placed **before** the fallback list, because they flag
  results that look successful - the ones an operator would otherwise only catch on the
  plot. The manual's calibration walkthrough and `UserGuide.txt` explain both prompts and
  the clip warning.

#### The base manual documents the receiver SNR fields (US8.0)

Closing PR #80 (a v2.10.0-era plan for these sections) meant checking what the shipped
manual actually covers. The walkthroughs cover it; the **base** manual did not.

- The base manual is the Qt resource compiled into the exe - it cannot be declined or
  deleted, which is what makes "the user always has a manual" structural. It described the
  whole SNR half of the gear dialog in one line ("word layout of the receiver channels
  within the frame, plus inversion/randomization flags") and never named **Polarity**, the
  voltage range, **dB/V** or the receiver counts. Those appeared only in the walkthroughs,
  which ship as an *optional* installer task - so a user who declined it had no
  description of the four fields that most quietly corrupt an SNR run.
- New "The receiver SNR fields" section: a field table giving each one's meaning and, more
  usefully, what a wrong value looks like on the plot (other channels' words, an
  upside-down trace, a constant scale error), plus what a receiver-parameters TOML carries
  and why polarity must be right before a calibration profile is extracted. It is terser
  than the walkthrough's treatment rather than a copy of it - the walkthrough teaches the
  task, the base manual is the reference.
- `UserGuide.txt` had the same hole and gains the same fields, since it ships in both the
  installer and the portable ZIP and nothing in CI reads it.

#### Cleanup sweep: dead code, one owner per value, one naming convention

A whole-codebase pass (the `code-cleanup` skill). Every source file on disk is built,
all 138 constants are referenced, no Model or ViewModel file reaches for a widget header,
and there are no TODO/FIXME markers - what follows is what the sweep did turn up. No
user-facing behaviour changes.

- **Dead code removed.** `PlotWidget::handlePlotYRangeChanged` was declared with no
  definition and no caller (its X twin is live; `TmChart` emits no Y-range signal).
  `Chapter10Reader::addChannelInfoEntry()` had no caller in `src/` or `tests/`.
- **The manual-Y-range path is gone.** `setYManualRange()`, `setYAutoScale()`,
  `yAutoScale()` and the `m_y_manual_min/max` + `m_y_auto_scale` members were reachable
  only from a test: production has pinned `m_y_auto_scale` true since the left/right max
  overrides replaced them, so four `if (m_y_auto_scale)` branches never ran either.
  `yMin()`/`yMax()` now read the computed range directly. The test that drove the removed
  setters is replaced by one covering the override that *is* live - and which, unlike the
  old one, actually reaches `computeYRange()` (the old fixture's negative dB values were
  clipped at 0 by a path the manual range bypassed).
- **The lock axis has one owner.** `PlotConstants::kLockAxisMin` / `kLockAxisMax` replace
  the `0` / `100` literals in `PlotWidget`'s initial left range and its Set-Left-Max bound,
  and back `PlotViewModel`'s lock-axis accessors and `leftYMax()` - four places that each
  spelled the same fixed 0-100 range.
- **`MainViewModel::channelPrefix()` / `parameterName()` deleted.** Both were pass-through
  forwarders to `FrameSetup`, called only by `TestMainViewModelHelpers` - so that suite was
  testing the forwarder rather than the naming. Its six naming cases moved to
  `TestFrameSetup` as two, aimed at `FrameSetup` directly; the suite keeps
  `classifyLogMessageSeverity`, which tests a real `MainViewModel` helper.
- **`streamsubdialogs.h` is one naming convention again.** 29 camelCase members across its
  three dialogs are now `m_snake_case` like the rest of the project (180 occurrences). The
  file had held `m_tomlDir` *and* `m_toml_dir`, `m_appRoot` *and* `m_app_root` - the same
  concept spelled two ways in one file. Same drift v2.11.1 fixed in
  `plotcustomizationdialog`; nothing outside this private header names these members.
- **Include order.** `plotcustomizationdialog.cpp` and `tst_exportdialog.cpp` had project
  headers ahead of the Qt group, and eight files had unalphabetized Qt groups. An unused
  `<QtMath>` in `csvseriesparser.cpp` is gone. (`main.cpp`'s leading `"mainview.h"` and
  `mainview.cpp`'s trailing `<windows.h>` are both correct as they stand - a related header
  and a guarded platform include.)
- **Stale prose.** The PlotWidget component notes still said "all replots use
  `rpQueuedReplot`", a QCustomPlot call removed in v2.9.0, and a `PlotViewModel` comment
  still reasoned about handing `QCPRange` an inverted range. US5.3 gains criteria for the
  two "check" prompts, the ignored-clip warning and the preloaded step file, which had
  shipped with only a changelog entry.
- Kept deliberately, with a reason: `PlotViewModel::loadCsvFile()` (the documented
  `exportCsv` partner), the id-based `setSeriesVisibleById()` (one of four in a coherent
  family), `TmChart`'s observation accessors - `seriesPen()` had no caller at all and is
  now covered by `TestTmChart` rather than deleted, since its siblings are - and
  `MainView`'s batch loop, whose location `docs/CLAUDE.md` already explains.

#### A saved receiver-parameters file is one the application can load again (US5.1)

Found during a codebase cleanup sweep, by following an unused function.

- **Save wrote half a file.** The Receiver SNR dialog's *Save receiver parameters* wrote the
  `[Parameters]` scalars and no word map. Loading that file back gives a setup with zero
  parameters, which both consumers reject ("Receiver Parameters file contains no
  parameters") - so the button produced a file the application refuses. The half that was
  missing is `FrameSetup::saveToSettings()`, which writes the word map and had no
  production caller at all.
- **The counts were read from a key the shipped files do not use.** Load read
  `[Parameters] NumReceivers` / `ReceiverChannels`; `RASA.toml`, `TRC.toml` and
  `default.toml` all carry their counts under `[Receivers] Count` /
  `ChannelsPerReceiver`. So loading RASA showed 16x3 instead of its real 12x3 - the
  dialog quietly disagreed with the file it had just loaded.
- **Both halves of the file are now read and written in one place.** `FrameSetup` gains
  `ReceiverParams` (the five scalars), `readReceiverParams()`, `writeReceiverParams()`
  and the static `saveReceiverParamsFile()`, which resolves *which* word map to write -
  the file this stream loaded, or the default map for the counts on screen, i.e. whichever
  map the run would actually use - and **refuses to write a file with no word map at all**.
  Reading accepts the old `[Parameters]` count keys as well, so files the dialog wrote
  before this still populate it. The dialog now only moves values between widgets and the
  Model.
- `FrameSetup::wordsInMinorFrame()` replaces the ceiling division that `MainViewModel` and
  `CalibrationExtractor` each spelled out, since the word map's bounds check depends on it.
- `tryLoadingFile()`'s doc comment claimed it returned "true if at least one parameter was
  loaded" while the code returns true for a file with no word-map groups at all. The
  behaviour is what both callers actually need (each reports a clearer cause than a bare
  false would allow), so the comment was corrected to state it, and a test pins it.

#### Tests

- `TestFrameSetup` gains seven cases: the word-count rounding; a metadata-only file parsing
  to zero parameters (the contract both callers depend on); the shipped RASA counts read
  from `[Receivers]`; the older `[Parameters]` count keys still read; a save/load round trip
  carrying both the scalars and the word map; the default-map fallback matching
  `buildDefaultReceiverMap()`; and nothing written at all when no map can be built. The
  round-trip case fails against the previous Save.
- **Trap:** `QSettings` writes through a rename, which does not land on a path a live
  `QTemporaryFile` still owns on Windows - the write silently produces an empty file. Tests
  that write TOML use a plain path plus `QFile::remove()`.
- The cleanup sweep nets **-4** cases: six channel-naming cases consolidate into two aimed
  at `FrameSetup`, while the SNR max-override and `TmChart::seriesPen` cases replace what
  the removals took with coverage of live code, and `TestConstants` pins the two new
  lock-axis constants. Baseline **392 / 0 / 1**, zero warnings.
- `TestStepDetector::detectReportsHowManyLevelsTheSweepHeld`: 11 levels against 8 steps
  reports 11; an exact match reports 8.
- `TestCalibrationExtractor::summaryFlagsAShortStepFileAndAMismatchedWordMap` (pure): the
  step-file prompt fires at 11-vs-8 but not for a single lead-in level; the word-map prompt
  fires for one-swept-two-flat but not for an all-flat receiver.
- Real-recording tests for each trap on `step_cal, example.ch10`:
  `realStepCalWithShortStepFileIsFlagged` (RASA 8-step file),
  `realStepCalWithWrongWordMapIsFlagged` (RASA receiver map), and
  `clipsBeyondTheRecordingAreReported` (6 + 60 s ignored and reported, 6 + 2 s honoured
  silently). `realStepCalRecordingPairsEveryStepCorrectly` now also asserts that neither
  prompt fires on the correct setup - a false alarm would teach operators to ignore them.
- `TestStepDetector::initialStepConfigPrefersTheStreamsFileThenTheDefault`: no previous file
  gives the shipped default; the stream's own file is kept even when it is not the default;
  a stale path falls back to the default; no settings tree gives nothing to preload.
  Baseline **389 / 0 / 1**, zero warnings.

### v2.12.0 — Calibration That Pairs Every Step Correctly, Even on Out-of-Alignment Receivers

#### Calibration pairs every step with its own dwell on out-of-alignment receivers (US5.3)

- **"Calibrated" was not the same as "right".** On `step_cal, example.ch10` with its real
  step config (0–60 dB in 6 dB steps: eleven levels, the first being the level each channel
  holds before the generator steps up), nine channels attached a profile — and four of them
  were paired off by a step: L/R_RCVR3 and R_RCVR6 by one, L_RCVR6 by two. The profile
  attached and nothing reported it. The operator saw receiver 3's three channels a full
  step apart in time, and a 6 dB swing across one dwell near the top.
- **Root cause: the noise estimate.** The edge threshold is 6σ of the derivative's robust
  spread. Samples pinned exactly at the converter floor (the pre-sweep at 0, the post-sweep
  back at 0) or rail (65472) carry no noise, yet they were a third of the series and
  dragged σ far under the live steps' jitter — which itself grows with level, from ~3
  counts on the low steps to ~60 on the high ones. The resulting 11–17-count thresholds
  split noisy high dwells into two plateaus 20–80 counts apart, on steps ~3800 counts tall.
  The extra level took a pairing slot, the real 0 dB level was dropped as "pre-roll", and
  every step below the split read 6 dB low. The same undersized threshold shattered
  receiver 1's 42 dB dwell into runs too short to confirm, losing that step outright.
- **The fix has two parts.** Pinned samples no longer vote on σ, which lifts those
  thresholds to 28–40 counts. And adjacent levels closer than 10% of the channel's median
  step are merged as one dwell (`CalibrationConstants::kSameLevelFractionOfStep`), because
  noise that grows with signal can still out-run a threshold sized by the quiet steps. The
  splits here are 0.5–2% of a step and the smallest genuine step ~30%; a synthetic
  compressed receiver's smallest step (~16%) is pinned as surviving.
- **Coalescing within the edge threshold** stays as the first pass. It heals 1–3-count
  splits like R/C_RCVR1's, which otherwise read as "no transition" and sever the monotonic
  run mid-sweep.
- **Threshold search for compressed receivers.** A receiver whose steps are far smaller
  than nominal can fall under the edge threshold entirely. `detect()` retries at
  `nominal × 0.7^n` down to a floor of `max(1.5σ, 2 counts)` (`kEdgeRelaxFactor`,
  `kMinEdgeSigmaMultiple`, `kMaxEdgeRelaxAttempts = 8`), accepting the **first** — i.e.
  largest — threshold that yields a monotonic sweep, and flags a relaxed success so the
  report can call it out. It never fires on this recording.
- **Every failure now says which gate rejected it.** New `CalibrationOutcome`
  (`Calibrated` / `NoData` / `FlatNoSignal` / `TooFewPlateaus` / `NoMonotonicSweep`)
  travels from `StepDetector::Result` into `CalibrationChannelResult`, alongside the
  accepted edge threshold, the relaxed flag, and the coalesced plateau levels.
  `FlatNoSignal` separates a channel that held one level for the whole recording
  (0 = dead, 65535 = railed) from a genuine detection shortfall — different problem,
  different remedy, and previously both read as "too few plateaus".
- **The 8-step config first used on this file was wrong for it.** `rcvr_cals/RASA.toml`
  (0, 3, 6, 12 … 36 dB) on an eleven-level sweep keeps the top eight levels and pairs them
  with the wrong dB; its "12 of 12 calibrated" was that mis-pairing, not a success. The
  regression test uses the 11-step `rcvr_cals/default.toml` and asserts the pairing itself.

#### A receiver that runs out of range is calibrated over the range it kept (US5.3)

- **Receiver 1's gain is set too high for this sweep**, so its top two steps (54 and 60 dB)
  both sit on the 65472 rail: ten distinct levels for eleven steps, and no threshold can
  find an eleventh. It used to lose the whole channel to the linear fallback, including the
  nine steps it had measured perfectly well.
- **The railed dwells are dropped and the rest are calibrated.** They carry no information -
  the receiver had no headroom left - so they are not paired with a step.
  `interpolateCalibration()` already clamps above its top point, so a raw value that reaches
  the rail reads the top measured step (48 dB here) rather than a number invented by
  extrapolating past the data. The report says
  `readings hold at 48 dB (no measurement for 54, 60 dB)`.
- **Step numbers come from the dwell grid, not from position.** Once a level is missing,
  position cannot say which step the next one is - and L_RCVR1 loses its 24 dB dwell to noise
  as well as its top two to the rail. Counting transitions gives each level its own step:
  L_RCVR1's 30 dB dwell stays 30 dB rather than sliding down to 24. Plateau *ends* cannot do this - noise shatters the tail of a dwell,
  ending R_RCVR1's 36 dB plateau a third of a dwell early - but transitions can.
- **A dwell the grid can see but plateau detection missed is measured, not bridged.**
  L_RCVR1's 24 dB plateau shatters under noise. Left as a hole, readings there were
  interpolated straight from 18 dB to 30 dB and read **23.09 dB** at a dwell that sits at
  28556 counts - the receiver is not linear across that gap. The dwell's position is known
  from the transitions either side, so its level is the median of its settled tail; it must
  fall between its measured neighbours, and only a one-dwell gap qualifies.
- **Guards, because this path infers rather than measures:** only a run whose top is actually
  railed takes it, at least half the steps (and at least three) must survive, and every gap
  between transitions must land on the dwell grid within a quarter of a dwell. A recording
  whose steps were not each held for the same length of time keeps the linear fallback rather
  than being handed a silently mis-numbered profile.
- On `step_cal, example.ch10` this is the difference between 9 and **12 of 48** channels: all
  three channels of receivers 1, 3, 5 and 6 - the operator's own count of what carries the
  sweep.

#### The extraction window: finer is not better (US5.3)

- Extraction averages ~10 frames per window, floored at 100 ms. Sampling **once per minor
  frame** - the finest a recording can give - was tried and is measurably wrong: the AGC word
  updates more slowly than the frame rate, so consecutive frames repeat the same count, the
  series becomes a quantised staircase whose median absolute derivative is exactly zero, the
  edge threshold collapses to its 2-count floor, and every jitter step reads as an edge,
  shattering every dwell. Measured on `step_cal, example.ch10` (1 ms frames, so every option
  is available): one sample per frame calibrates **0** of 48 channels, 10 ms calibrates
  **10**, 100 ms calibrates **12**.
- The window is also the resolution of each dwell *boundary*, but that costs nothing here:
  those samples are marked unstable and excluded, and each level is taken from the END of its
  dwell. The intermediate points an operator sees between plateaus on the plot come from the
  **main run's** sample period (1 s / 100 ms / 10 ms in the Receiver SNR dialog), which is a
  separate setting.
- Both bounds now carry those measured counts in `constants.h`, so the next person to assume
  finer must be better has the numbers in front of them.

#### The calibration summary names receivers, not just a channel count (US5.3)

- `CalibrationExtractor::summarize()` builds the "Calibration Extracted" message:
  `Calibrated: receivers 1, 3, 5, 6.`, then one line per receiver that fell back, naming its
  channels and the reason (`too few plateaus (4 of 8 steps resolved)`,
  `flat / no signal (held at 0)`), then any receivers that calibrated only with a relaxed
  threshold, as worth checking. A receiver whose channels partly calibrated is spelled out
  rather than hidden in the count. Channels from a hand-written receiver-params TOML carry
  no `_RCVR<N>` and are reported by name.
- `FrameSetup::receiverIndexFromName()` / `channelPartOfName()` invert
  `receiverParameterName()`, so the grouping reads the same names the word map writes
  rather than re-deriving receiver numbers from word indices.
- The per-channel log line now carries the reason and threshold on failure, and notes a
  relaxed-threshold success.
- **The manual's calibration walkthrough is rewritten for it.** `walk-cal-04-summary.png`
  is re-captured against the new report (on `step_cal, example.ch10`), and step 4 now
  explains how to read it: the four sections, what each fallback reason means and points
  at, and what a saturated receiver looks like. The troubleshooting table gains three rows:
  a swept receiver reading *flat / no signal* (word map), steps landing at the wrong levels
  despite success (step file shorter than the recording), and a trace that flattens below
  the top step (saturation). `walk-cal-03-clip-controls.png` is re-captured on the same recording
  (Clip Start 6, Clip End 2, 12 of 48), and step 3 now states plainly that both clip values
  are amounts trimmed from each end - Clip End counts back from the end of the recording, not
  forward from its start or from Clip Start - with a worked example of how a mistaken
  "stop at 50 s" entry keeps almost none of the sweep, and that clips adding up to more than
  the recording are ignored.

#### Word-map trap found along the way

- `step_cal, example.ch10` is an 800-bit frame like RASA's, but its receivers are laid out
  **sequentially** (3 words each), not on RASA's stride-4 card layout. Extracting it with
  `receiver_params/RASA.toml` skips words 3, 7, 11, 15 … and mis-attributes every receiver
  above 1 — live channels report as dead, with nothing pointing at the map as the cause.
  The map must match the recording; the regression test states which one this fixture
  needs and why.

#### Tests

- New committed fixture `tests/data/test files/step_cal, example.ch10` (6.8 MB), the
  recording that exposed the failure. CI requires exactly one skip, so it had to be
  committed rather than left local.
- `TestCalibrationExtractor::realStepCalRecordingPairsEveryStepCorrectly` pins the known
  truth under the real 11-step config and the shipped word map: receivers {1, 3, 5, 6}
  calibrate (twelve channels); every profile pairs 0 dB with the pre-sweep level and no two
  points sit closer than 1000 counts (a split dwell is tens); the unsaturated ones carry all
  eleven steps to 60 dB while receiver 1 carries all nine from 0 to 48 dB, holds there with
  54 and 60 unresolved, and reads exactly 48 dB at the rail; receiver 2 reads flat / no signal; and the report
  names the receivers.
  `summaryNamesReceiversAndWhyTheyFellBack` covers the report without a fixture.
- `TestStepDetector` gains eight cases: outcome codes; a compressed out-of-tolerance
  receiver calibrating via the relaxed threshold (and a healthy one *not* relaxing);
  noise-split plateaus coalescing, built from R_RCVR1's real levels with dither because the
  split only exists under noise; flat channels reporting no signal; pinned floor samples kept
  out of the noise estimate (without that, the dwells shatter and the channel reads flat); a
  split dwell far above the noise still pairing as one step (noiseless, so only the
  step-relative merge can heal it); railed top steps calibrating what stayed in range, with
  a rail-level raw reading the top measured step rather than an extrapolated one; and a dwell
  that never confirms being measured from the dwell grid rather than bridged by linear
  interpolation (which read 23.09 dB at a 24 dB dwell) or shifting the steps after it. The pairing
  cases fail against the previous detector.
  `TestFrameSetup::receiverIndexFromNameInvertsParameterName`.
- `realFileCalibratesRcvr3Only` still passes unchanged: none of this adds a false profile
  on the other fixture's 45 noise channels. Baseline **383 / 0 / 1**, zero warnings.

### v2.11.1 — Code Cleanup (no user-facing change)

The application binary is functionally unchanged from v2.11.0 (a handful of dead
symbols removed shrinks it trivially); this release exists to land a codebase-health
pass that had been accumulating.

#### Added

- New `.claude/skills/code-cleanup/SKILL.md` — a whole-codebase counterpart to
  `tm-code-review` (which stays diff-scoped): orphaned code, readability/convention
  drift, MVVM layering leaks, and documentation that's drifted from the code it
  describes. Its first move is always checking `docs/CLAUDE.md` and the session's
  `MEMORY.md` for a documented reason before flagging anything, built around the
  `PlotViewModel::loadCsvFile` case that looks orphaned but isn't.

#### Removed (dead code, first sweep's findings)

- Two unused `PlotConstants`: `kTitleFontSize` (`TmChart::drawTitle()` never read
  it) and `kFrameSyncLockColor` (production color assignment uses
  `kFrameSyncLockPrimaryColors` exclusively; this was a standalone duplicate of its
  first entry). `TestPlotViewModel::lockSeriesColor` now asserts against the array
  element that's actually alive rather than the deleted duplicate.
- Four unused public getters with no production caller and no planned one:
  `Chapter10Reader::getCurrentPCMChannelID()`/`getTimeChannelIndex()`/
  `getPCMChannelIndex()` (plus the `findChannelIndex()` helper that existed only to
  back the latter two) and `PlotViewModel::dataYMin()`/`dataYMax()`. Real behavioral
  coverage that happened to route through `getCurrentPCMChannelID()` as its
  observation mechanism was preserved via a new `friend class TestChapter10Reader;`
  reaching `m_current_pcm_channel` directly, rather than deleted along with the
  getter.

#### Fixed

- `docs/CLAUDE.md`'s Test Suites section claimed the 20 suites run "in this order" —
  `tests/main.cpp` actually runs the fast, no-fixture suites first and the four
  heavy `.ch10`-integration suites last (skippable via `--fast`). Reworded to
  describe the real grouping instead of a false ordering claim.
- Naming drift in `plotcustomizationdialog.h`/`.cpp`, the one view file using
  camelCase members (`m_viewModel`, `m_tabWidget`, …) instead of this project's
  `m_`-prefixed snake_case convention.
- Include ordering in `frameprocessor.cpp` and `ch10packetreader.cpp`, which had
  irig106 headers after the project-headers group instead of before, reversing the
  documented six-group order.
- A markdown-lint scrub (`.pymarkdown.json`, `pymarkdown scan` now exits 0 across
  all tracked `.md` files) turned up two real bugs along the way, not just lint
  noise: `cut-release/SKILL.md`'s copy-pasteable verification commands had four
  file paths silently corrupted by the project's known heredoc `\b`/`\t`-mangling
  failure mode, and `docs/CLAUDE.md` had two separate "#### Fixed" sections under
  the same v2.11.0 heading (merged into one, matching every other version's
  one-per-category convention).

#### Reorganized

- `tests/data/test files/` is now the canonical fixture folder; the four small
  `.ch10` fixtures CI needs moved there (git recognizes the moves as pure renames),
  and everything else directly under `tests/data/` (stray loose exports and older
  recordings nothing reads at runtime) is gitignored outright instead of living
  half-tracked. `tests/.gitignore`, every `testDataPath(...)` call site across the
  four heavy suites, and the CI workflow's full-recording overlay step were updated
  to match.

#### Tests

- **372 / 0 / 1**, zero warnings — down from 377 at the v2.11.0 tag: one test
  dropped with `kTitleFontSize` (no invariant left to assert once the constant was
  gone) and four more with the removed `Chapter10Reader` index-lookup getters
  (`getTimeChannelIndex`/`getPCMChannelIndex`, which they tested directly). The
  fixture-folder move and the markdown scrub touched no test code.

### v2.11.0 — Legacy Chapter 10 Becomes a Setting, and the Log Gets Quieter

#### The manual documents Legacy Chapter 10 (US1.0)

- **Section 2 now describes the two file-level settings together.** Time Channel and
  Legacy Chapter 10 both describe the recording rather than a stream, so they are
  presented as a pair, with the reason the byte-order setting is file-scoped stated
  where an operator will read it - a channel-by-channel search for the right setting
  is the wrong instinct and the manual should head it off.
- **New section 9, "When a stream will not sync"** - the manual had no troubleshooting
  section at all. It reads the three log outcomes (locked / no sync found / sync found
  but no frames extracted), then lists what to check: pattern and bits-per-frame as a
  pair, Legacy Chapter 10, derandomize, and the channel itself. The section is much
  shorter than it would have been a week ago: with the automatic probe gone and the
  reports down to one line, there is less behaviour to explain.
- It also documents the **odd-length payload** case, which is otherwise inexplicable
  from the outside - one channel in a recording ignoring the byte-order setting that
  its neighbours obey.
- **The whole-file rule is stated as a diagnostic**, not just a scoping note: if some
  streams lock and others do not, byte order is *not* the cause. That inference is the
  useful half, and it is the one an operator cannot make without being told the setting
  is file-wide.
- The walkthroughs gain the same file-level pair, and the frame-sync figure caption now
  says PRN-15 is the default - the caption described the dialog as "configured for a
  PRN 15 stream" when that is now simply what a new stream opens on.

#### The processing log is concise

- **Four manual figures re-captured** against this behaviour: the three Configure
  Streams shots now show the **Legacy Chapter 10** toggle, and the log figure shows the
  one-line reports. The first set of re-captures came from a build that predated the
  toggle — worth checking a figure contains the thing it is meant to illustrate, not
  just that its crop and dimensions are right.
  `walk-err-02-try-this.png` is deleted: it was named for the removed suggestion, and
  the log figure already carries the errors, the successful streams and the concise
  format in one shot.

- **One line per outcome.** The failure reports were a five-line block plus a
  paragraph of explanation; they are now single lines that state what was configured
  and what came of it:
  > `[CH 13 PCM03] no frame sync found — pattern 0x334AABBF (32 bits), 32767 bits/frame.`
  > `[CH 12 PCM02] sync found but no frames extracted — pattern 0xFE6B2840 (32 bits), 1234 bits/frame; 198543 syncs, 0 boundary-aligned.`
- **The automatic probe is removed.** A run that found no sync used to re-scan a
  retained sample under other byte-order/derandomize combinations and suggest one.
  The operators of this application know their own formats — the useful answer is
  whether it worked, not the application guessing at settings — so the probe, the
  1 MiB sample buffer it needed and its two tests are gone. `countPeriodicSyncs` and
  `diagnoseNoSync` no longer exist.
- **The sync pattern is still printed**, because it is the one field that separates
  two otherwise identical-looking configurations: PRN-11 and PRN-15 are both 32 bits.
  The **mask is not**, unless it is non-default — all-ones is every ordinary setup, so
  printing it was noise.
- The unbounded processing window (`stop=18446744073709551615s`) no longer appears; the
  window is named only when it is actually bounded.

#### Frame Sync Lock defaults to PRN-15 (US2.0)

- `kDefaultFrameSyncLockPattern` / `kDefaultFrameSyncLockBits` move from PRN-11
  (`A345CA5C` / 2047) to **PRN-15 (`334AABBF` / 32767)**, so a Frame Sync Lock stream
  opens on the pattern most recordings here actually use instead of needing the
  PRN-15 TOML loaded every time.
- **The two constants are a matched pair and must move together**: a PRN-15 pattern
  with a PRN-11 frame length can never lock, and nothing in the build would notice.
  `TestConstants` now pins both, where it previously pinned only the pattern — so the
  half-change is a test failure rather than a stream that silently never syncs.

#### Byte order is now settable, per file (US1.0)

- **Labelled "Legacy Chapter 10", and it is the INVERSE of `swapBytes`.** The label
  names the *kind of file* the operator has rather than the transform it needs — byte
  order is not a distinction the operators of this application work in.
  The byte-pair swap is what an **ordinary** Chapter 10 recording needs: three of the
  four reference recordings require it and only the legacy one does not. So the swap is
  the normal case, the box is **unticked by default**, and ticking it turns the swap
  *off*. `StreamConfigDialog::swapBytes()` is the single place the two vocabularies
  meet; no model-layer code knows the label exists.
  This was wired the same way round at first, which put the common case behind a ticked
  box and made the label mean the reverse of the truth. `legacyToggleIsInverseOfByteSwap`
  pins the widget state as well as the accessor — an accessor-only test passes just as
  well if both ends are flipped back together. This is
  deliberately *not* the same default as `ProcessingTemplate::swapBytes`, which stays
  `true`: a template written before the field existed came from a build that always
  swapped, and reading it as `false` would silently change how that template processes
  its files. `ProcessingParams::swapBytes` follows the UI to `false`, so "unset" means
  modern — and flipping it surfaced three call sites (two coordinator benchmarks, two
  calibration requests) that had been silently inheriting the legacy transform. Each
  now states what it needs, which is the point: a default that quietly supplies a
  raw-affecting transform is how the bug below happened.
- **Fixed before it shipped: the calibration extractor did not mirror the flag.**
  `CalibrationExtractor` sets `isRandomized` and `isInverted` but was never given
  `swapBytes`, so once byte order became settable it extracted with the DTO default
  rather than the operator's choice — building a profile from a different bitstream
  than the run that profile would be applied to. Nothing would have surfaced it: the
  profile attaches and the numbers are simply wrong. This is the invariant `CLAUDE.md`
  states ("the calibration extractor must mirror the main run's word map +
  raw-affecting flags exactly"), and the same shape as the earlier `isInverted`
  omission. The flag now travels on `CalibrationExtractor::Request`, plumbed from the
  Configure Streams dialog through the gear dialog exactly as `timeChannelId` was.
- `TestCalibrationExtractor::byteOrderReachesTheExtraction` asserts the mirror
  **behaviourally** rather than by reading `m_params`: with the wrong byte order the
  sync pattern is destroyed and extraction cannot succeed, which is only observable if
  the flag truly reaches the scan. Verified to fail when the mirror line is removed.
  Baseline **374 / 0 / 1**.

- **The application byte-swapped every PCM payload unconditionally, and nothing could
  turn it off.** `Ch10PacketReader` set `ResolvedPcmAttrs::needsSwap` from
  `bDontSwapRawData`, which `Set_Attributes_PcmF1` memsets to 0 and which only
  `Set_Attributes_Ext_PcmF1` ever assigns — and only when its `lNoByteSwap` argument is
  not `-1`, which is exactly what we pass, because TMATS has no field for it. So the
  expression was a constant `true` wearing the costume of a lookup.
  Recordings whose payload is not byte-swapped could not be processed at all: the sync
  pattern was destroyed before the scanner saw it, and the user got an empty plot.
- **The setting is file-scoped, not per-stream** — a "Swap byte pairs" checkbox on the
  Configure Streams dialog's Time Channel row, which is where the other file-level
  control already lives. Byte order is a property of the recorder that wrote the file
  (this project ingests recordings from two vendors' hardware), so every stream in one
  recording shares it. Offering it per stream would let a file be configured
  inconsistently with itself, which is never correct. `MainViewModel` stamps the one
  value onto every `StreamJob` it builds.
- **Defaults to on**, which is what the application did before, so every existing
  recording and template behaves identically. Templates carry it alongside
  `timeChannelIndex` (a batch is one vendor's files); a template written before the
  field existed has no key for it and reads back as `true`, reproducing what that
  template actually did, so no `schemaVersion` bump is needed.
- **Not in the frame-sync TOML.** It is a per-session operator input like `Data Rate`,
  so `loadFrameSyncToml`/`saveFrameSyncToml` were untouched and the US1.0 boundary
  still holds. (That boundary later grew to carry the two stream-format flags,
  `Inverted` and `Randomized`; byte order remains outside it, file-scoped rather
  than per-stream.)
- **A skipped byte swap is no longer silent.** `SwapBytes_PcmF1` returns
  `I106_BUFFER_OVERRUN` on an odd byte count *before touching the buffer*, and
  `FrameProcessor` discarded that return — so with the swap on, an odd-length payload
  was left unswapped while its even-length neighbours were swapped, and one stream could
  carry two byte orders with nothing in the log to say so. On the Safran recording that
  is exactly why channel 14 (875-byte payloads) locks while channel 13 (1250-byte) does
  not, under identical settings.
  The return is now checked and the skips counted, reported **once after the drain**
  rather than per packet. The two cases read differently on purpose: when *every* packet
  is odd the swap is inert for that stream, so the message says the setting had no
  effect at all; when only *some* are, the message warns that the stream carries two
  byte orders and sync may be unreliable. The transform behaviour is deliberately
  unchanged — swapping all-but-the-last-byte would invent semantics the library
  declines to define, and would break channel 14, which currently works precisely
  because of the skip.

#### Fixed

- **The section renumbering regex was a counted range.** `embed_manual_images.py`
  matched `[1-8]` when there were eight reference sections, so adding a ninth left it
  unshifted - numbered 9 in a full manual whose neighbours had moved into the teens,
  with the nav and the heading disagreeing. Widened to `\d+`. The generated manuals are
  now checked for nav/heading agreement and unresolved anchors, which is what caught
  it: 9 numbered sections in the base, 14 in the full, every internal link resolving.

- **The log collapsed every multi-line message into one paragraph.** `logError()` and
  friends escape the text with `toHtmlEscaped()`, which handles the markup characters
  but leaves a newline as a literal newline — and HTML collapses that to a single
  space. So the frame-sync diagnostics, whose whole readability comes from their
  aligned `Looking for:` / `Config:` / `Syncs:` layout, arrived in the sidebar as a
  run-on blob. Every log path now carries `white-space: pre-wrap`, which preserves the
  newlines *and* the runs of spaces that align the columns while still wrapping lines
  too long for the sidebar.
  Found by looking at a screenshot taken for the manual — no test could see it, because
  the messages were correct at every layer except the one the operator reads.
  `TestMainView::logPreservesMultiLineMessages` now asserts on `toPlainText()` rather
  than the CSS, so it pins what is shown rather than how. Verified to fail against the
  old rendering, which produced `first line second line third line`.

- **Every message a stream worker logs now names its channel**, e.g.
  `[CH 13 PCM03] …` (`FrameProcessor::streamTag`). Streams are processed by parallel
  workers writing into one shared log, so their output interleaves in an order that is
  not even deterministic between runs — a recording with four PCM channels produced
  four unattributed "pattern was not found" lines with no way to tell which channel
  each belonged to. The tag leads with the PCM channel ID because that is what the
  operator configured the stream by; the TMATS label is appended only when it adds
  something, since `MainViewModel` falls back to `Ch <id>` when TMATS has no name and
  `[CH 13 Ch 13]` is noise.
- **The failure report now prints the sync pattern and mask it was actually looking
  for** (`FrameProcessor::syncSpec`). It printed `sync_len` but never the pattern —
  and PRN-11 and PRN-15 are *both* 32-bit patterns, so a stream configured with the
  wrong one was invisible in the report: every printed field looked reasonable. A
  PRN-11 configuration on a PRN-15 stream could only be spotted by noticing
  `bits/frame=2047` and knowing what that implied. The mask is reported as
  "all bits significant" when it is the default, since a non-default mask silently
  excuses mismatches and is worth seeing.

- **The sync-failure diagnostic reported off-phase matches as boundary-aligned.** The
  scanner's off-phase branch — a sync match that is *not* on a frame boundary —
  incremented `boundary_syncs` anyway, and set `current_sync_run` without ever folding
  it into `max_sync_run`. So the "no valid frames were extracted" report could read
  `198543 total | 99272 boundary-aligned | longest run=0`, which is self-contradictory
  (99,272 aligned syncs cannot yield a longest run of zero) and points the reader at
  the wrong cause: it says the syncs were landing correctly when not one of them was.
  Both counters are diagnostic-only — written in the scanner, read once, printed once —
  so no processed data was ever affected. The report was the only casualty, and the
  report is what an operator uses to decide what to change.
  The message now also explains a zero-aligned result instead of leaving it to be
  inferred: the pattern matched somewhere but never at the expected frame spacing, so
  the pattern, bits/frame or byte order is wrong — and chance alone yields about one
  32-bit match per 4 Gbit, which is what a handful of "found" syncs across a multi-hour
  recording actually means.
- `TestFrameProcessor::offPhaseSyncsAreNotCountedAsBoundaryAligned` reproduces it
  deterministically — the right sync pattern with a deliberately wrong frame length, so
  every match is off-phase by construction — and asserts both `0 boundary-aligned` and
  the new explanation. Verified against the old accounting, which reported 99,272
  boundary-aligned syncs where the correct answer is zero.
- `TestFrameProcessor::failureReportNamesChannelAndSyncPattern` pins the channel tag on
  the failure report **and on every routine log line** — a tagged error among untagged
  progress messages is still ambiguous once four streams interleave — plus the printed
  pattern and mask. Baseline **374 / 0 / 1**.

- **The hamburger menu's section headers were never drawn.** `Process`,
  `Import/Export` and `Settings` were added with `QMenu::addSection(tr(…))`, but once
  *any* stylesheet is set on the application `QStyleSheetStyle` takes over menu-item
  painting and never renders a section's text — so each one degraded silently to a
  plain separator. `main()` installs the theme at startup, so **no themed build has
  ever shown them**, while `docs/CLAUDE.md`, the user manual and the manual's figure
  caption all described named groups. Shipped that way through v2.10.0.
  Measured on the `Process` band: unstyled it is 12 px and draws the label; with the
  theme applied it is 9 px — identical to a plain separator — and blank. Deleting the
  `QMenu::separator { height: 1px; … }` rule does **not** bring the text back, which
  is the useful part: the cause is the stylesheet existing at all, not what it says,
  so there is no QSS-only fix.
  Sections are now built by a file-local `addMenuSection()` — a `QWidgetAction`
  carrying a `QLabel`, which paints itself and is therefore immune. Its colour comes
  from the theme QSS by object name (`QLabel#menuSectionLabel`, the same approach as
  `QLabel#streamHeaderLabel` in `StreamConfigDialog`) so it follows the light/dark
  switch. The action is disabled so a header is not selectable or keyboard-reachable,
  and the QSS gives the disabled state the same colour — otherwise it would pick up
  `QMenu::item:disabled` and read as an unavailable command rather than a heading.

#### Cosmetic

- **User Manual… and About… are collapsed into a `Help` submenu** rather than sitting
  as two flat entries under a `Help` section header. Both are read-once-then-forget,
  so they were costing two permanent rows in a menu whose other entries are the actual
  workflow. The section header is gone with them, so the menu is two rows shorter, not
  one. A separator precedes the submenu row: a submenu carries no section header of its
  own, so without one it would read as part of the **Settings** section above it.
  Sections remain the default for the menu; a submenu is used only where the entries
  are reached rarely and by name, which is now Recent Files and Help.

#### Tests

- `TestMainView::menuSectionHeadersSurviveTheStylesheet` applies the **real** theme
  stylesheet before building the window, because unstyled is precisely the
  configuration in which the broken code also passed — the bug only exists once a
  stylesheet is set. It pins all three header labels, that each has height to draw in,
  and that none is selectable. Verified to fail (0 headers found) against the
  `addSection` version.
- `TestMainView::helpSubmenuHoldsManualAndAbout` asserts both entries are inside
  `m_help_menu` **and that neither is left behind at the top level** — the second half
  is the one that catches a half-done move. Asserted structurally because a screenshot
  cannot tell a submenu from a section header: both render as one row of text with the
  same two entries under it, and only one of them collapses. Verified to fail by
  putting `About…` back on the top-level menu.
- `TestFrameProcessor::swapBytesOffFindsSyncThatTheSwapDestroys` drives the Safran
  fixture's channel 13 both ways: with the swap off the PRN-15 sync is found and the
  stream locks; with it on the processor reports "frame sync pattern was not found" and
  fails. **Both** directions are asserted — checking only that swap-off locks would pass
  against a build that never swapped anything, which would break every other fixture.
- `TestStreamConfigDialog::byteOrderToggleIsFileLevel` pins that the control is a single
  dialog-level accessor and did not become a per-row field.
- `TestProcessingTemplateSchema::swapBytesRoundTripsAndDefaultsTrue` covers the round
  trip and that a legacy template missing the key reads back as `true`.
- `TestFrameProcessor::oddPayloadSwapSkipIsReported` drives the Safran fixture's
  channel 14 (every payload odd) with the swap **on** and asserts the log says the
  setting had no effect — distinguishing the inert-stream message from the
  mixed-byte-order warning, since asserting merely that *something* was logged would
  not catch the two branches being swapped. Baseline **372 / 0 / 1**.

### v2.10.0 — The Illustrated Manual, Optional as an Installer Component

#### One source, two manuals (US8.0)

- **The user manual is now two artifacts generated from one source.** The *base*
  manual stays compiled into the exe as a Qt resource; a new *full* manual
  (`UserManual.html`) ships as a file, always in the portable ZIP and as an optional
  task in the installer ("Install the full illustrated user manual"). 16 figures in
  the base, 36 in the full.
- **The always-have-a-manual guarantee is structural.** Because the base manual is a
  compiled-in resource it cannot be deleted or declined, so `Help > User Manual`
  always has something to open. `MainView::fullManualPathIn()` returns empty when the
  optional file is absent and the action falls back to the resource - that fallback is
  a normal path, not an error path, and there is no "manual not found" state.
- Both manuals come out of `scripts/embed_manual_images.py` from the **same document
  object**; the full one is the base plus walkthrough sections. They are never
  maintained as two documents, because two hand-maintained manuals would drift - the
  failure this project has paid for repeatedly.
- `check_release_consistency.ps1` now asserts the version markers in **both** manuals.
  A second shipped artifact that nothing checks is exactly the stale artifact that
  script exists to prevent.

#### The walkthroughs (the content that made the option worth offering)

- Five task-oriented walkthroughs in `docs/manual/walkthroughs.html`, illustrated at
  every dialog: opening a file and configuring streams, a frame sync lock run, a
  receiver SNR run, non-linear step calibration (US5.3), and batch processing with a
  template (US1.1). They are the *full* manual's leading sections; the reference
  chapters that were the whole manual follow them.
- **Headings carry no numbers and cross-references are anchors, never numbers.** The
  embedder numbers walkthrough headings 1..N and shifts every reference section by
  `SHIFT = len(walk_ids)`, so the same prose is correctly numbered in a manual that
  has the walkthroughs and one that does not. A hand-written "see section 4" would be
  wrong in exactly one of the two.
- 23 new captures registered in `scripts/build_manual.ps1`. Each entry declares its
  crop, and a bare `$true` (the standard chart region) is **validated against the
  expected window size** rather than trusted - an app floating over an IDE is a
  different size and the fixed rectangle would silently produce a plausible-looking
  but wrongly framed figure.
- **Every registered capture is now placed.** Eight had been registered so their crops
  were validated during the capture batches but were referenced by no section, leaving
  them in the repository and in neither manual; they illustrate reference sections, so
  that is where they went. `after()`/`before()` assert their anchor text, so a
  reworded paragraph fails the build rather than dropping a figure.
- **Import & export was rewritten** to enumerate what each export actually contains -
  every CSV column, what the image export composites, what the log export captures -
  and the series readout is documented, both nearest-series and pinned.
- A short **"About this manual"** note explains that two versions exist and how to
  tell which one you are reading. It is worded to read correctly in both, since both
  are generated from it.

#### Cosmetic

- **Configure Streams: the Channel and Mode columns are left-justified.** Both were
  right-justified — Mode so it "no longer crowds the Channel column", and Channel to
  pair with left-side elision. With the short names most files produce, both simply
  read as text pushed away from its label.
  Channel's elision follows its alignment (`ElideRight`), which **costs something
  real**: these names share a long common prefix and the distinguishing detail (band,
  rate, code) sits at the end, so a truncated long name no longer shows the part that
  tells two channels apart. Accepted deliberately; the tooltip is now the only place
  the full name is recoverable, which is why the test asserts it.
- **Batch Process Files: the status glyphs are green/red**, matching the Configure
  Streams Ready column. Both read from `UIConstants::kStatusOkColor` /
  `kStatusFailColor` / `kStatusIdleColor` so they cannot drift apart again.
  Rejected rows are now *unselectable* rather than *disabled*: Qt greys a disabled
  item regardless of any colour set on it, so leaving them disabled would have
  silently defeated the red glyph — and it also greyed the rejection reason, the most
  useful text in the row.

#### Tests

- `TestMainView::fullManualPathEmptyWhenNotInstalled` /
  `fullManualPathFoundWhenInstalled`, both against a `QTemporaryDir`. The first draft
  probed the real app root and failed, correctly: in a developer checkout
  `UserManual.html` always exists, because it is generated and tracked. The resolution
  logic was made static and root-parameterised so the test asks about the logic rather
  than about the machine it runs on. Baseline **366 / 0 / 1**.

### v2.9.3 — CI Skip Gate (no user-facing change)

The application binary is functionally identical to v2.9.2; this release exists to
keep the tag history aligned with `main`. The only change is to CI.

#### CI

- **The test-result gate now pins which test is allowed to skip.** A skipped test is
  indistinguishable from a passing one in the totals, and the suite has **40 `QSKIP`
  sites**: 39 are "fixture not available" guards, one is the deliberate opt-in heavy
  PRN benchmark (`TMDQA_RUN_HEAVY_BENCH`). The old gate only checked
  `$pass -lt 320` — with the real count now 364, an entire suite could vanish and
  still pass, and its comment still claimed "expect 357".
  Both runners now require **exactly one skip, and that it is the benchmark by name**;
  any other skip fails and prints the skip list. All three `.ch10` fixtures are
  committed, so this holds on the hosted fallback too.
  Verified against three doctored result files: clean run passes; an injected extra
  skip fails; and a run where the count is still 1 but it is the *wrong* test also
  fails — which a count-only check would have passed.

### v2.9.2 — Discoverable Plot Shortcuts

#### Improved

- **Every plot shortcut is now advertised in the context menu** (US4.1). Only `L` and
  `Home` were shown as real shortcuts; `V` and `R` had degenerated into tooltips,
  which discover nothing. `V` now sits in the **View Mode** submenu title as `(V)` -
  Qt will not render a `QKeySequence` on a submenu, and the key cycles rather than
  selecting one entry, so the title is the only place it can be seen. `R` gets a new
  top-level **Reset View** action.
- **Reset View** also closes a real gap: the on-chart *Reset view* chip restores span
  and scaling in one click, but the menu split that across `X Axis > Reset Span` and
  `Y Axes > Reset` with no single equivalent. `Y Axes > Reset` no longer carries a
  tooltip claiming `R` resets both axes - it resets the Y axes, and the new top-level
  action is the one that does both.

### v2.9.1 — Empty-Axis Hiding and Title-Bar Polish

#### Cosmetic

- Every title-bar button (hamburger, sidebar toggle, minimize/maximize/close) is now
  30x26 with a rounded hover box that hugs its glyph, instead of filling a full-height
  cell. Note this departs from the Windows convention where close spans the top-right
  corner.
- The plot reserves 12 px of headroom (`TmChart::setTopInset()`) so the on-chart chip
  bar no longer sits on the top axis line and the topmost Y tick label.

#### Fixed

- **A Y axis with nothing plotted against it is now hidden.** Both axes were always
  drawn: `PlotWidget` called `setRightAxisVisible(true)` once at construction and the
  left axis had no switch at all. So a frame-sync-only file advertised
  "Receiver SNR (dB)" auto-ranged to 0.0-1.0 with no curve on it - which reads as an
  SNR measurement pinned near zero rather than as absent data - and an AGC-only file
  showed an empty "Framesync Lock (%)" 0-100 the same way. Pre-existing, not a
  TmChart regression; the old code showed both unconditionally too.
  `TmChart` gains `setLeftAxisVisible()`, and `PlotWidget::updateAxisVisibility()`
  recomputes both from `PlotViewModel::hasVisibleLeftAxisSeries()` /
  `hasVisibleRightAxisSeries()` wherever series visibility is established - so hiding
  the last SNR series in Customize View, switching View Mode, or filtering to a
  source with no SNR each reclaim the axis. A hidden axis reserves no margin, giving
  the space back to the chart, and the horizontal grid follows whichever Y axis is
  drawn. With nothing plotted at all the left axis is kept, so an empty chart still
  reads as a chart. `setRightAxisVisible()`'s doc comment already claimed this
  behaviour and now matches it.
- The **first X tick label** no longer clips off the left edge. It was centred on its
  tick unconditionally, which only fit because the left-axis gutter happened to be
  there; reclaiming that gutter exposed it. Both end labels are now pulled inside.
  Found by rendering the new layout - the 362-test suite passed throughout.

#### Release tooling

- New `scripts/check_release_consistency.ps1`. The version has one source of truth
  (`AppVersion`), but four user-facing files carry it as **prose** that nothing
  derives - the root `CLAUDE.md` line, `docs/CLAUDE.md`'s Project Version, the
  manual's subtitle and footer, and the release-notes header. Each has shipped stale
  at least once, and the guard was "remember to grep for the old string". The check
  derives the version and asserts all of them, plus that the changelog has a section
  for it and (`-ReleaseMode`) that no `Unreleased` section survives into a release -
  the exact mis-filing that credited two cosmetic changes to a v2.9.0 build without
  them. `build_release.ps1` runs it before packaging; CI runs it on every PR without
  `-ReleaseMode`, since an `Unreleased` section is correct *between* releases.
  A missing marker fails rather than passes, so rewording a checked line cannot
  silently disable the check.
- `scripts/embed_manual_images.py` no longer hardcodes an old-to-new version
  replacement, which would have silently matched nothing once the manual already held
  the new string. It parses `AppVersion` and hard-errors if either version marker is
  absent.

#### Tests

- Five new cases: `TestTmChart::hiddenAxisReclaimsItsMargin` and four in
  `TestPlotWidget` covering lock-only / SNR-only / empty / last-SNR-series-hidden.
  Green baseline: **362 / 0 / 1** across 20 suites (357 at the v2.9.0 tag).

### v2.9.0 — First-Party Charting, Plot Shortcuts, Qt 6.11.1

#### QCustomPlot replaced by first-party charting

- The vendored QCustomPlot library (44,700 lines / 1.7 MB) is **gone**. Plotting is
  now `TmChart` (`include/view/tmchart.h`, `src/view/tmchart.cpp`), a ~700-line
  QWidget that paints in `paintEvent()` — no retained scene graph, no `replot()`.
- **Behaviour-preserving by design.** Every US4.0/US4.1 criterion still holds: dual Y
  axes with independent auto/manual ranging, horizontal-only wheel zoom and drag pan,
  the draggable translucent legend composited into exports at its placed position, the
  crosshair and rubber band, and the DDD:HH:MM:SS time axis.
- Landed in three revertible steps: engine + `TestTmChart` inert (#55), `PlotWidget`
  swapped over (#56), dependency deleted (#57).
- **Fixes a latent export bug:** `savePdf()` was a third rendering path that rendered
  only the chart, so PDF exports silently dropped the legend that PNG and SVG both
  composited in. One `renderTo()` now serves screen, PNG, SVG and PDF, so they cannot
  drift.
- `TmChart` is deliberately simpler where it can be: the crosshair and zoom band are
  painted rather than owned items (no widgets, excluded from exports by construction),
  and a `setTimeFormatter()` callback replaces an entire `QCPAxisTicker` subclass.
- X tick labels are now decluttered — `DDD:HH:MM:SS` labels are far wider than numeric
  ones and collided at the default tick count; a label is drawn only when it clears the
  previous one, with the last always kept. Tick marks are unaffected.
- Removing the library also removed its build accommodations: `/bigobj` (needed only
  for its oversized translation unit) and, importantly, `thirdparty.pri`'s `/wd4996`,
  which had been suppressing Qt **deprecation warnings for application code too**.
  Those are visible and gated again.

#### Plot keyboard shortcuts (US4.1)

- Single keys while the chart has focus: arrows step the time window keeping its width,
  `+`/`-` zoom about the centre, `Home` restores the full span (keeping a pinned Y max),
  `R` resets both axes, `V` switches the left-axis metric, `L` toggles the legend.
- Handled in `PlotWidget::keyPressEvent` and deliberately **widget-scoped**: a
  bare-letter shortcut registered application-wide is dispatched ahead of the focus
  widget, so it would swallow that character in every text field. A click on the chart
  focuses it. `TestPlotWidget` pins this — a sibling `QLineEdit` keeps its keystrokes.
- The context menu advertises each key, so they are discoverable rather than hidden.

#### Toolchain and CI

- **Qt 6.11.1** (from 6.10.3). `QT_VERSION` in `scripts/env.ps1` remains the single
  knob; `compile_flags.txt` must be regenerated on any bump, since its Qt paths are
  absolute and version-pinned.
- **CI is ~3x faster**: 286 s → 93 s. Builds go through `$env:TMDQ_MAKE`, which
  `env.ps1` sets to the parallel `jom` when present and `nmake` otherwise, so the
  GitHub-hosted fallback runner is unaffected.
- The zero-warning gate now covers **test** code too. It previously grepped only
  `app_build.log`, so warnings in `tests/` were never gated. A missing log is now a
  hard failure rather than a silent pass.

#### Fixed

- `deploy/build_release.ps1` announced "Build and packaging complete!" and listed an
  installer path that was never written, when signing had failed for every artifact and
  Inno Setup had aborted. It now verifies each artifact with
  `Get-AuthenticodeSignature`, reports `NOT PRODUCED`, flags artifacts left over from an
  earlier run as STALE, and exits non-zero.
- Launching a locally built exe no longer depends on Qt being on `PATH`:
  `scripts/deploy_qt_local.ps1` copies the runtime beside the exe (including the
  **offscreen** platform plugin, which `windeployqt` omits and headless runs need).
  Fixes a `0xC0000135` DLL-not-found after the Qt bump, caused by a hard-coded Qt path
  in a gitignored IDE config plus a stale user-level `QTDIR` pointing at a removed
  MinGW kit. The shared `.vscode` configs are now tracked so a toolchain change has to
  confront them.

#### Tests

- New suite `TestTmChart` (11 cases). Green baseline: **357 / 0 / 1** across 20 suites.

### v2.8.0 — Plot Window Simplification

#### Plot controls moved into a right-click context menu (US4.0)

- The plot window's external control rows are **gone**: the chart now fills the
  entire plot area. The plot title field, Plot File selector, View Mode selector,
  X start/stop fields, Reset button, left/right Y-max spin boxes and the
  "Customize Plot…" button were all removed.
- Everything is reached from a **right-click menu on the chart**
  (`PlotWidget::showPlotContextMenu()`, built by `buildContextMenu()`): **Plot
  File** (radio list of processed sources + "All files (overlaid)"; disabled for a
  single source), **View Mode** (Lock Percentage / Accumulation; disabled unless
  both left-axis metrics exist), **Customize View…**, **Set Plot Title…**,
  **X Axis** (Set Time Window… / Reset Span), **Y Axes** (Set Left Max… / Set
  Right Max… / Reset), and **Export…**. The menu is rebuilt on every request, so
  it can never show a stale source list or mode.
- The single Reset button became **two** items — `X Axis > Reset Span` and
  `Y Axes > Reset` — which together do what the old button did.
- **Export…** is now also on the plot (it remains in the hamburger menu too).
- Unchanged: mouse-wheel X zoom, click-drag X pan, and the draggable legend
  overlay. Y-axis minima remain automatic (only maxima are user-settable).
- Time-window entry keeps the exact `formatTime`/`parseTime` (DDD:HH:MM:SS)
  round-trip the old Start/Stop fields used, including clamping to the file range
  with a log warning; the dialog supplies both values at once, so an inverted
  range now pulls Stop up to Start (the old form clamped whichever field had focus).
- New: an on-chart **legend show/hide toggle** - a small translucent button
  overlaid at the chart's top-left (the legend defaults to the top-right, so they
  never collide), mirrored by a checkable **Show Legend** context-menu item. The
  preference persists via QSettings (`LegendVisible`), survives legend rebuilds,
  and a hidden legend is omitted from exported PNG/SVG images (export composites
  only a visible overlay). The button itself is never rendered into exports.
- New: an on-chart **chip bar** (top-left) collecting the persistent overlay
  controls - the legend toggle, a one-click **View Mode** chip (labelled with the
  metric it switches *to*, hidden unless the data has both), and a **Reset view**
  chip that appears only once the view is zoomed or an axis max is pinned.
- New chrome-free gestures: **Ctrl+drag / middle-drag** rubber-bands a time range
  and zooms to it on release (the right button is taken by the context menu and
  plain left-drag stays panning, so neither could be reused); **double-click**
  restores the full span; a dashed **crosshair** follows the cursor so several
  series can be read at the same instant. The crosshair and rubber band are
  painted by the chart itself rather than widgets, so they add no chrome and are absent from exports.
- New: **grab-cursor panning** - the chart shows an open hand once data is loaded
  and a closed hand while the left button is held, so the drag-to-pan gesture is
  discoverable now that no toolbar hints at it. Shown regardless of zoom level;
  a plain arrow before any data loads.
- New: **Readout series selection.** The crosshair readout previously always
  described whichever series was nearest the cursor, which on a dense plot meant
  it kept flipping between curves. The context menu gains a **Readout** submenu:
  *Nearest series* (the default, unchanged behavior) or any single series pinned
  by name. A pinned series is read at the cursor's time regardless of how far the
  cursor is from the curve, so the 10-pixel proximity gate is bypassed while
  pinned. Selection is held as a **stable series id**
  (`PlotViewModel::indexOfSeriesId`), not a positional index, so a reprocess or an
  async CSV import that reorders the series list can't silently re-point the
  readout at a different curve; a pinned series that disappears falls back to
  nearest.
- The chip bar's buttons are labelled and described: each chip carries a tooltip
  naming the action it performs (the legend chip's text updates with its state, so
  it never offers the action just taken), and the legend chip is **text-only**
  ("Toggle Legend") to match the View Mode chip beside it - a bare glyph gave no
  indication of what it did, and glyph-plus-text cost more of the chart than the
  label alone. `PlotConstants::kLegendToggleSizePx` was renamed
  `kOverlayChipHeightPx` to match what it actually sizes (all three chips).
- Tooltips are suppressed while the cursor is over the chip bar, so a chip's own
  tooltip can't collide with the crosshair readout's.
- No ViewModel changes: every menu item drives the existing `PlotViewModel` API.

#### Title bar

- Minimize / maximize / close now draw their glyphs from the Windows icon font
  (Segoe Fluent Icons, falling back to Segoe MDL2 Assets), the same set Windows
  uses for its own caption buttons, so all three share one set of metrics. The
  previous mix of ordinary Unicode look-alikes (U+2212 / U+25A1 / U+2715) rendered
  at visibly different sizes. They stay *text* rather than icons so the close
  button's white-on-red hover still recolors the glyph, and the codepoints are
  named constants - a private-use-area literal in the source is fragile and can be
  dropped silently, leaving a blank button.

#### Maintenance

- Validation limits that existed only as constants are now enforced where they
  matter: the sync pattern/mask field derives its length bound from
  `kMaxSyncPatternBits` instead of repeating `{1,16}`, and `StreamConfigSchema`
  clamps `samplePeriodIndex` / `slopeIndex` on load so a template from a newer
  build can't silently change a stream's sample rate or volts-to-dB conversion.
- Dead weight removed: 37 unused Qt includes, ~20 unreferenced constants, and the
  vestigial `TimeFields` type (nothing constructed it; processing carries its
  window as IRIG seconds).

#### Build, CI, and tooling

- **The Windows toolchain is now MSVC 2022 only** (Qt `msvc2022_64` kit, `cl` /
  `nmake`); the MinGW/GCC build and every artifact that supported it are gone.
  `scripts/env.ps1` imports the MSVC environment via `vcvars64.bat` and is the
  single source of truth for a local build; `deploy/build_release.ps1` packages
  with `win32-msvc` and copies the app-local CRT from `$env:VCToolsRedistDir`.
- **CI runs on a self-hosted Windows runner** with the toolchain and the
  full-size `.ch10` recordings on disk, falling back to GitHub-hosted
  `windows-latest` when that runner is offline or a contributor lacks access
  (a `pick-runner` job routes between them). Small `.ch10` fixtures are now
  committed, so the four integration suites run on **both** paths rather than
  being skipped. Machine setup and recovery: `docs/ci_runner.md`.
- The heavy PRN throughput benchmark is **opt-in** (`TMDQA_RUN_HEAVY_BENCH=1`).
  It walks a 640 MB recording nine times, and a timeout left the stack-local
  `ProcessingCoordinator` destroying live reader/worker `QThread`s, which makes Qt
  fail-fast (`0xC0000409`) and truncate the whole run's results rather than fail
  one test. `runAndTime`/`probeChannel` now cancel and drain before returning, and
  the benchmark itself no longer runs unless asked for. Green baseline is
  **357 passed / 0 failed / 1 skipped** (the skip is that benchmark).

### v2.7.0 — Processing Templates & Batch Apply, Hamburger Menu, Frameless Title Bar, User Manual

#### Processing templates & batch apply (US1.1)

- New: **Save as Template…** captures one processed source's per-stream settings
  (`StreamConfig`s + time channel) and each series' name/color (`SeriesAppearance`)
  as a file-path-independent JSON template (`ProcessingTemplate` DTO,
  `ProcessingTemplateSchema` with `schemaVersion` gating; delegates per-entry
  serialization to `StreamConfigSchema`).
- New: **Apply Template to Files…** batch-processes N `.ch10` files with one
  template. `TemplateMatcher` enforces exact channel-ID-set equality per file
  (`MatchResult` reports missing/extra ids); non-matching files are flagged in
  `BatchApplyDialog` and skipped — never silently mis-applied. The batch loop is a
  sequential state machine (`advanceBatch()` ↔ `onBatchProcessingFinished()`)
  mirroring the old session-load turn-taking. (Moved to `BatchController` after v2.12.1.)
- All processed files are retained in memory; `PlotViewModel` gained per-source
  labels/filtering (`setVisibleSource`, `sourceList`, `effectiveVisible`) and the
  plot toolbar a **Plot File** selector ("All files (overlaid)" or any single file).
- Optional per-file export post-pass writes a CSV plus two images per file
  (`…_framesync_lock.png`, `…_missed_frames.png`) via the new headless
  `PlotWidget::exportImage()` (extracted from `onExportPlot()`).
- Removed: Save/Open Session (Phase 6), Open Multiple Files, Add/Remove Source —
  Apply Template is the sole batch entry point.

#### Single hamburger menu + frameless title bar

- The menu bar and icon toolbar collapsed into one ☰ menu with flat
  `addSection` groups (Process / Import/Export / Settings / Help); Import/Export
  became menu actions; Recent Files sits under Open in the Process section.
- Frameless window: a custom title bar (`setMenuWidget`) hosts the hamburger,
  the sidebar toggle, and min/maximize/close. `MainView::nativeEvent()` handles
  `WM_NCCALCSIZE`/`WM_NCHITTEST` so native move/resize/snap/double-click-maximize
  survive; hit-testing converts through `ScreenToClient` and reports any
  `QToolButton` under the cursor as client so title-bar buttons receive clicks.
- New sidebar (log) show/hide: title-bar toggle + Ctrl+B, persisted in QSettings
  (`SidebarVisible`); `m_controls_dock`/`kControlsDockMinWidth` renamed to
  `m_sidebar_dock`/`kSidebarMinWidth`. Hamburger/sidebar glyphs are QPainter-drawn
  and re-render per theme in `applyActionIconsForTheme()`.

#### User manual

- New: **Help > User Manual…** opens an HTML manual (embedded resource
  `resources/usermanual.html`, copied to the temp dir and opened in the default
  browser).

#### Test Suite Updates

- New suites: `TestProcessingTemplateSchema`, `TestTemplateMatcher`; extended
  `TestPlotWidget` (headless `exportImage`), `TestPlotViewModel` (source
  filtering/labels/CSV source filter), `TestMainView` (batch appearance reapply,
  template capture, `titleBarExists`). Full-run green baseline: 325/0/0 across
  19 suites (with `.ch10` fixtures present).

### v2.6.0 — CSV Import, Movable Legend, Configure Streams Redesign, Stability Fixes

#### CSV Import (US6.3)

- New: the application can open a CSV it previously exported and load it straight
  into the plot, so old data sets can be visualized without re-processing the
  source `.ch10` file (US6.3).
- A dedicated toolbar **Import** button (orange icon, left of Export) opens a
  CSV-only dialog; it is enabled like Open and disabled while processing. CSV
  files are imported through `importCsv()` and bypass the Configure Streams dialog
  and processing pipeline entirely.
- File > Open and the toolbar Open button remain Chapter-10-only; drag-and-drop
  and the Recent Files list route by extension via the new `MainView::openPath()`
  (`.ch10` → process, `.csv` → import).
- A successful async import is finalized off a dedicated
  `PlotViewModel::loadSucceeded()` signal (not `dataChanged()`), so recent-files /
  title / success-log side effects only run after the parse actually succeeds.
- Import reuses the existing Model/ViewModel path (`CsvSeriesParser::parse` +
  `PlotViewModel::loadCsvFileAsync`), so Frame Sync Lock, Accumulated Missed
  Frames, and Receiver SNR columns are recognized by header and routed to the
  correct axis/metric/color; the plot, legend, and Customize Plot Series dialog
  all populate as with processed data.
- A CSV that isn't in the app's export format (wrong/missing
  `Time (DOY:HH:MM:SS.mmm)` header, or no parseable rows) is rejected with a
  clear log error rather than producing garbage series; malformed rows are
  skipped and the skipped count is surfaced as a log warning. On success the
  imported file is added to Recent Files and the plot title is set to its base
  name.

#### Movable plot legend (US4.0)

- The on-plot legend is now a translucent, draggable overlay floating inside the
  chart (not a fixed panel below it), so it can be repositioned to avoid
  obscuring data of interest. Defaults to the top-right corner and resets there
  each session; height is capped with a vertical scrollbar for dense plots
  (e.g. 48+ Receiver SNR series), with a permanent gutter so the scrollbar never
  overlaps row text.
- Receiver SNR legend rows show a short `CH<id> <L/R/C>_RCVR<n>` label instead
  of the full TMATS-derived stream title; the full name is still used for CSV
  headers, renaming, and hover tooltips.
- Composited into exported PNG and SVG images at its placed position. Per-series
  color and name editing moved from the (now read-only) legend into the
  Customize Plot Series dialog.

#### Configure Streams dialog redesign

- Progress bar and Cancel button moved out of the main window into a modal
  `ProcessingProgressDialog`; primary-action/Cancel button order corrected to
  match WinUI 3 convention (primary left of Cancel) across `StreamConfigDialog`
  and its three sub-dialogs.
- Channel column narrowed and styled to match the Mode combo box's border/fill.
  (Originally right-justified with left-side elision; both columns were changed to
  left-justified afterwards — see v2.10.0.)
- The three per-stream sub-dialogs (Frame Sync Lock, Calibration, Receiver SNR)
  now share one layout vocabulary (spacing, grid geometry, icon-button sizing,
  a consistent text-left/buttons-centered alignment rule) so their
  look-and-feel stays consistent instead of drifting per dialog.
- Toolbar Export/Import icons switched to Microsoft's actual Fluent System Icons
  glyphs (`ArrowExport` / `ArrowImport`, MIT licensed), recolored to the app's
  existing green/orange convention.

#### Bug Fixes & Stability

- Two configured streams sharing a TMATS-derived channel name could silently
  cross-contaminate each other's plot series — renaming, recoloring, or
  reprocessing one stream could affect the other's Frame Sync Lock / Accumulated
  Missed Frames series. Stream identity is now matched by channel name **and**
  PCM channel id together, not name alone.
- Assorted correctness/robustness hardening from a pre-release code review:
  plot series lookups by stable id instead of position (so a stream finishing
  processing in the background can't recolor/hide the wrong curve), and a few
  internal lifetime/ownership tightenings with no user-visible behavior change.
- The Customize Plot Series dialog now resolves its edits by stable series id
  (`PlotViewModel::indexOfSeriesId`) rather than the raw positional indices it
  captured when opened, so a series-list change while the dialog is open (an
  async CSV import replacing the data, or an in-flight reprocess) can no longer
  apply an edit to the wrong series or read past the end of the series list.

### v2.5.2 — Stream Config Dialog Polish

- Configure Streams dialog table background changed from near-black (#202020) to match the dialog background (#2C2C2C) in the dark theme QSS, removing the black-background appearance behind table row controls.
- Added an "All" toggle (QCheckBox) to the bottom-left of the Configure Streams dialog; toggling it on/off sets all stream Process checkboxes in one action.

### v2.5.1 — Frame Sync UX Polish and Resolution Fix

- Frame Sync Lock and Accumulated Missed Frames plot series names now use the bare stream label only (no metric suffix). `renameSeries` propagates a rename to the sibling series (same `streamLabel`, other frame-sync metric) so a custom name survives mode switching.
- Switching between Lock % and Missed Frames modes now clears `m_left_y_max_user_set` and emits `axisRangeChanged()` in `setLockAxisView`, resetting the left Y-axis to its automatic range.
- Main window opens at 1920×1080 (`UIConstants::kInitialWindowWidth/Height`); `adjustSize()` removed.
- Resolution fix: `kPlotDockMinHeight` removed; only the chart (`m_plot`) carries a 250 px floor (`PlotConstants::kPlotMinChartHeight`), so the bottom controls (Start, Stop, L/R Max) remain visible when maximized at high DPI.

### v2.5.0 — Cleaner Stream Names, Plot Axis Overrides, Dialog Polish

- Stream labels carry the **bare channel name** (no `<id>-` TMATS prefix): drives
  the Configure Streams dialog and Frame Sync Lock plot series. Receiver SNR plot
  series re-add the channel number (`PlotViewModel::addStreamData`) so multiple SNR
  streams in one file stay distinguishable; `getPCMChannelList()` now returns the
  bare name while the channel-selection combos keep `<id> - name`.
- Plot toolbar gains **L Max / R Max** y-axis maximum override spinboxes; `yMax()`
  keeps a user override above `yMin()` (`PlotConstants::kMinAxisSpan`) and `Reset`
  clears both overrides.
- Source tree reorganized into `dto/model/view/viewmodel` subfolders under
  `include/` and `src/`; CSV parsing extracted from `PlotViewModel` into the
  Model-layer `CsvSeriesParser`.
- Setup-dialog cleanup: shared `buildFrameSyncRow` / `makeDialogButtons` helpers
  remove the duplicated frame-sync and button blocks; tooltips on every field;
  "Randomized" relabelled "Derandomize".
- The bundled receiver frame-sync defaults file was renamed
  `default_rcvr.toml` → `framesync_rcvr_default.toml`
  (`UIConstants::kDefaultReceiverFrameSyncFilename`).
- Full test suite green (212 passing).

### v2.2.5 — Receiver SNR Step Calibration Complete

- US5.3 (Non-Linear Receiver SNR Step Calibration) is **complete and field-validated**.
- Robust, polarity-agnostic plateau selection in `StepDetector`: the calibration
  sweep is the first maximal monotonic plateau run, of which the last `expected`
  plateaus are kept. This excludes a leading signal-generator turn-on transient and
  a trailing operator down-ramp, and works whether the receiver's raw count rises
  (normal) or falls (inverted) with signal — fixing the calibrated-staircase time
  skew and inversion seen on RASA receivers.
- Out-of-range raw values now **clamp** to the nearest end-step dB instead of
  extrapolating, so receivers driven past the calibrated range read the ceiling/
  floor rather than diverging per channel (`interpolateCalibration()`).
- Frame-rate-adaptive extraction sample period with a 100 ms floor
  (`CalibrationConstants::kMinAdaptiveExtractPeriodSec`) so fast (e.g. 800-bit)
  minor frames don't over-resolve plateaus.
- Apply Cal dialog adds optional **Clip Start / Clip End** controls to trim
  leading/trailing seconds before step detection, and logs a per-channel plateau
  count (flagging any channel whose detected plateaus ≠ expected steps).
- All settings files (StepCal/receiver-params/framesync TOMLs) are tracked in git
  and shipped in the deployment package; receiver SNR framesync defaults live in
  `settings/framesync_patterns/framesync_rcvr_default.toml`.
- `TestStepDetector` extended with leading-transient and inverted-polarity
  regression cases; full suite green (212 passing).

### v2.2.0 — Non-Linear Calibration, On-Plot Legend, Log Export, and Single-Source Versioning

- Non-linear step calibration for Receiver SNR (US5.3): an "Extract Calibration…"
  action in the Receiver SNR setup dialog builds a per-channel raw→dB profile from
  a calibration Chapter 10 file and a `[[Step]]` TOML, applied via piecewise-linear
  interpolation/extrapolation during processing; channels that fail fall back to
  linear math. New `StepDetector`, `CalibrationExtractor`, and
  `interpolateCalibration()`; session-only, keyed by word index. New
  `TestStepDetector` suite.
- New on-plot legend panel below the chart: a fixed-height, vertically
  scrolling 4-column grid of color swatch + series-name pairs that lists every
  visible series and updates as series are toggled or the left-axis view
  changes (US4.0)
- PNG export now composites the legend panel beneath the chart into a single
  image (US6.0); SVG/PDF export the chart as before
- Export dialog gains an "Export Log (Text)" option that writes the log window
  contents to a `.txt` file; the dialog now lists Image first and defaults to
  Image export (US6.0)
- Switching the left-axis view between Framesync Lock (%) and Accumulated Missed
  Frames now preserves each stream's per-stream visibility selection instead of
  re-showing every stream
- Frame Sync Lock and Receiver SNR setup dialogs gain a separator line above the
  OK/Cancel row, matching the in-body wireframe separator
- Single-source versioning: `AppVersion` in `include/constants.h` is the only
  place the version is defined; qmake parses it and generates
  `version_autogen.h` for the Windows resource file, so the `.pro` and `.rc` no
  longer carry duplicate version literals

### v2.1.0 — Accumulated Missed Frames

- New plot view: the left axis can toggle between "Framesync Lock (%)" and
  "Accumulated Missed Frames" via a toolbar button (US3.1)
- Missed frames are accumulated per telemetry stream against that stream's
  own frame parameters — a monotonic count of loss-of-lock events within the
  selected processing window (not a bit-level/BER metric)
- Left axis auto-scales to the maximum accumulated missed-frame value in that
  mode; lock % retains its fixed 0–100 range
- Lock and missed-frame curves for a stream share a color (only one shown at a
  time); both metrics are included in CSV export
- Renamed the misleading `PlotConstants::kLockAxisLabel` (which held the SNR
  label) to `kSnrAxisLabel`; added `kMissedFramesAxisLabel`
- Redesigned "Customize Plot Series" dialog with per-stream Frame Sync Lock
  toggles and a collapsible receiver/channel tree on the Receiver SNR tab; new
  purple/blue/green (lock) and red/orange/yellow (SNR) primary color scheme
- US2.2 (apply one stream's configuration to all matching streams) tracked as a
  separate story; the frame-sync and missed-frames stories' terminology
  standardized on "missed frames"

### v1.0.5 — Legend Layout, Toolbar Export, and Stream Config UI Polish

- Fixed a SIGSEGV crash on file open caused by a double-free during legend
  layout teardown
- Plot legend redesigned as a fixed 3-column grid (1 column for Frame Sync
  Lock groups, 2 columns for receiver groups) so the layout no longer
  rearranges between files; columns scroll vertically beyond the visible
  row count (`UIConstants::kLegendGridColumns`)
- Plot export moved from a button below the plot to a toolbar Export
  action; the "Copy Data to Clipboard" button was removed
- Configure Streams dialog: Load/Save TOML and gear "Setup" icon buttons
  resized to fill the button with a transparent, borderless background, and
  the Ready column's check/X icons enlarged to match
- Configure Streams dialog: Load/Save TOML buttons relocated to the top row
  of the Frame Sync Lock and Receiver SNR setup dialogs, away from OK/Cancel
- Configure Streams dialog: table cell backgrounds made consistent across
  the Process, Channel, Mode, Setup, and Ready columns
- Fixed: Receiver SNR streams now show "Ready" after gear-dialog
  configuration even without loading a Receiver Parameters TOML

### v1.0.1 — Plot Legend & Lock Color Improvements

- Frame Sync Lock plot lines rendered in distinct shades of blue, one per stream
- Legend entries below the plot ordered by source PCM channel number rather than
  parallel-processing completion order
- Added framesync_PRN11.toml and framesync_PRN15.toml to installer/portable packages

### v1.0.0 — Initial Public Release

- User stories US1.0–US9.0 complete
- Frame Sync Lock analysis with off-phase rejection and bit-span lock percentage
- Receiver SNR / AGC analysis with voltage-to-dB calibration
- Multi-stream concurrent processing with single-pass I/O (one reader + per-stream queues)
- Per-stream stream configuration dialog with TOML load/save
- Plot window with dual Y-axes, mouse zoom/pan, series visibility toggles, and light/dark theme
- CSV and image (SVG/PNG/PDF) export via unified export dialog
- Inno Setup installer (admin + non-admin) and portable ZIP packaging
- Automated unit tests (Qt Test framework)

### v0.8.0 — Internal Milestone

- Core user stories complete
- Initial automated unit test suite

## Tech Stack

- **Language**: C++
- **Framework**: Qt
- **Build System**: qmake
- **Platform**: Windows (primary target)
- **External Libraries**: irig106utils (embedded C library). Charting is first-party (`TmChart`, `src/view/tmchart.cpp`) - the vendored QCustomPlot was removed once it was replaced.

## ⚠️ CRITICAL: Protected Files — DO NOT MODIFY

The following files are third-party library code and **MUST NOT be modified** under any circumstances:

### Protected Source Files

- `lib/irig106/src/irig106*.c` - All IRIG 106 C source files
- `lib/irig106/src/i106_*.c` - All i106 prefixed C source files

### Protected Header Files

- `lib/irig106/include/irig106*.h` - All IRIG 106 header files
- `lib/irig106/include/i106_*.h` - All i106 prefixed header files
- `lib/irig106/include/config.h` - IRIG 106 configuration

**File Patterns to Exclude**: Any file containing `i106` or `irig106` in its name

**Reason**: These files are from an external library ([irig106utils](https://github.com/atac/irig106utils)) and are maintained separately. Modifications would:

- Break compatibility with the upstream library
- Make future updates difficult
- Potentially introduce bugs in tested code

**If changes are needed**: They should be made by wrapping/adapting the library in application code (e.g., `chapter10reader.cpp`, `frameprocessor.cpp`, `plotwidget.cpp`), NOT by modifying the library files directly.

## Architecture

The application follows the **MVVM (Model-View-ViewModel)** pattern. The processing
core is a **single-reader / parallel-worker** pipeline: opening a Ch10 file shows the
per-stream **Configure Streams** dialog, and pressing Process reads the file exactly
once while each selected stream is processed concurrently on its own worker thread.
Results are accumulated **in memory** (no CSV-on-disk intermediate) and appended to
the plot as each stream finishes.

**Multi-source model:** the plot can hold more than one `.ch10` file's streams at
once. This is now driven **only** by the Apply Template batch loop (there is no
Add/Remove Source UI, and no direct multi-file open). Every source gets a stable
`sourceId` (`include/dto/source.h`) that travels with its
`ProcessedStreamData`/`PlotSeriesData`, so two sources that happen to reuse the same
PCM channel id never cross-contaminate. All sources share one elapsed-seconds X
axis: `PlotViewModel` re-bases to the **earliest absolute sample across every
source**. See `docs/multi-file-input-design.md` for the underlying design (time
alignment, non-overlap detection, source-qualified CSV export). There is only ever
**one active processing run at a time** — a batch is N sequential runs into one
accumulating `PlotViewModel`, not concurrent reads. (`MainViewModel::addSource()`
and `removeSource()` remain the model's add/remove primitives; only `addSource` has
a caller now — the batch loop.)

**Processing templates / batch apply** (docs/processing-template-design.md) is the
sole way to process many `.ch10` files that share the same PCM channel IDs.
**File > Save as Template…** captures a processed source's `streamConfigs` +
`timeChannelIndex` + each stream's series appearance (name/color) via
`ProcessingTemplateSchema` (which reuses `StreamConfigSchema` per entry, so no field
list is duplicated; `calibrationByWord` is never serialized — only the calibration
*input references* `calCh10Path`/`stepTomlPath`/`clipStartSec`/`clipEndSec`, so batch
runs use the **linear** calibration fallback). **File > Apply Template to Files…**
picks a template + N files, validates each file's PCM channel-ID set against the
template (`TemplateMatcher`, **exact-set match** — a file with any missing/extra
channel is rejected with a reason), then processes every matching file in one batch
(`BatchController`, `include/viewmodel/batchcontroller.h`). Every processed file is **retained in
memory**; the plot's right-click **Plot File** submenu
chooses which one to view (or "All files (overlaid)"), driven by
`PlotViewModel::setVisibleSource` / `effectiveVisible` (a source gate orthogonal to
per-series visibility and the **View Mode** lock%/accumulation selector). A batch
can **also** export a CSV + a Frame Sync Lock and a Missed Frames image per file
(optional checkbox → a post-pass in `finishBatch` that isolates each source). The
batch loop is a sequential one-run-at-a-time state machine (`BatchController::advance()`
↔ `onProcessingFinished()`) that can re-apply the template's saved series names/colors
onto each file's freshly-created series. It lives in the **ViewModel** layer; the View
keeps only its own half - `MainView::confirmAndStartBatch()` shows the confirmation
dialog, and an `ImageExporter` callback renders the per-file images, because only the
widget can draw the plot. Exact-set matching is what makes it safe to
feed the template's stored `pcmChannelId`s straight to `setStreamConfigs()`.
`schemaVersion` gates the template format so a newer/unrecognized file is rejected,
not mis-read.

### Core Components

#### View

1. **MainView** (`src/view/mainview.cpp`, `include/view/mainview.h`)
   - Thin GUI layer; creates and lays out all Qt widgets, the hamburger menu, and the custom title bar
   - Frameless window: a custom title bar (set via `setMenuWidget`) hosts the hamburger (≡) menu button, the sidebar toggle, and min/maximize/close buttons; `nativeEvent()` handles `WM_NCCALCSIZE`/`WM_NCHITTEST` so Windows still provides native move/resize/snap/double-click-maximize. The hamburger + sidebar glyphs are QPainter-drawn per theme
   - All commands live in the single hamburger menu, grouped into sections via the file-local `addMenuSection()` helper (**not** `QMenu::addSection`, whose label a stylesheet blanks — see below): Process (Open…, Recent Files, Save as Template…, Apply Template to Files…, Exit), Import/Export (Import CSV…, Export…), Settings (theme toggle). **Help is a submenu** (`m_help_menu`, after a separator) holding User Manual… and About… — sections are the default, and a submenu is used only where the entries are reached rarely and by name (Recent Files is the other one)
   - Binds to MainViewModel Q_PROPERTYs and connects signals/slots; contains no business logic
   - `logError()` / `logWarning()` / `logSuccess()` append colored HTML entries (red / #DAA520 / green) to the log window
   - Errors/warnings shown inline in the log (`QTextBrowser`, clickable links, persistent, auto-scroll); QMessageBox reserved for About and the calibration summary
   - Status bar shows the file metadata summary (filename, size, channel counts, time range)
   - Recent Files submenu (Process section) with QSettings persistence
   - Drag-and-drop and Open… of a single `.ch10` file (launches the StreamConfigDialog) or a previously exported `.csv` file (US6.3: routed by `openPath()`/`importCsv()` straight into the plot via `PlotViewModel::loadCsvFileAsync`, bypassing the dialog and processing pipeline)
   - Save as Template… (enabled once a source has processed) captures a source's per-stream settings + series appearance to a template JSON; Apply Template to Files… (always enabled) picks a template + N `.ch10` files and batch-processes every file with the same channel IDs via `startBatchFromTemplate()` → the batch loop (retain-all + Plot File selector; optional per-file CSV+image export). This is the only batch entry point — there is no Add/Remove Source or direct multi-file open. See the processing-templates section above and `docs/processing-template-design.md`
   - Progress/Cancel live in the modal ProcessingProgressDialog, shown only while processing runs
   - Log window fills the left sidebar dock (`m_sidebar_dock`); the title-bar sidebar button or Ctrl+B toggles it, with the shown/hidden state persisted in QSettings (`SidebarVisible`). The plot (PlotWidget) is the central widget
   - Help > User Manual… copies the embedded `resources/usermanual.html` to the temp dir and opens it in the default browser

2. **StreamConfigDialog** (`src/view/streamconfigdialog.cpp`, `include/view/streamconfigdialog.h`)
   - Modal "Configure Streams" dialog listing one row per PCM channel in the file
   - Five columns: Process, Channel, Mode, Configure (gear), Ready (status icon); a Time Channel combo at top
   - The gear opens a per-stream sub-dialog — Frame Sync Lock setup or Receiver SNR setup — keyed to the row's Mode
   - Sub-dialogs Load/Save frame-sync and receiver-parameter TOML files (US5.0/US5.1) and host the "Extract Calibration…" action (US5.3) and the "Apply to all `<mode>` streams" fan-out (US2.2)
   - The three sub-dialogs (Frame Lock, Calibration, Receiver SNR) live in the private header `include/view/streamsubdialogs.h` and share one layout vocabulary so their look-and-feel stays consistent: a `DialogLayout` constants block (outer spacing, section/control gaps, grid spacing, icon-button size) plus helpers `configureFormGrid`, `addLoadSaveButtons` (owns the frame-sync grid's gap column + Load/Save cell offsets — no caller hard-codes them), `addCheckboxRow`, `addBottomBar`, `matchControlHeight`, `buildFrameSyncRow`, and `makeDialogButtons`. Convention: text inputs (line edits, combos, spin boxes) are left-aligned; buttons and status icons (gear, Load/Save, ✓/✗) are centered. Table-header/separator colors come from the theme QSS (`QLabel#streamHeaderLabel`, `QFrame#streamHeaderSeparator`), not inline stylesheets, so they read on both light and dark themes
   - Returns the configured `QVector<StreamConfig>` via `configs()` and the two **file-level** values via `timeChannelIndex()` and `swapBytes()`. Byte order is file-scoped because it is a property of the recorder that wrote the file; it is stamped onto every `StreamJob` rather than stored per stream

3. **PlotCustomizationDialog** (`src/view/plotcustomizationdialog.cpp`, `include/view/plotcustomizationdialog.h`)
   - "Customize Plot Series" dialog: a Frame Sync Lock tab (one row per stream — visibility toggle, color swatch, editable name) and a Receiver SNR tab (collapsible per-stream receiver/channel tree with tri-state group toggles and Expand/Collapse All; right-click a channel to rename or recolor it). Color/name edits are applied to the ViewModel on OK via `commitAppearanceChanges()`

4. **ExportDialog** (`src/view/exportdialog.cpp`, `include/view/exportdialog.h`)
   - Unified export dialog: any combination of CSV data, a plot image (PNG/SVG/PDF), and the log text, each with its own filename/location; checkbox-to-field enable logic and export-button validation

5. **PlotLegendOverlay** (`src/view/plotlegendoverlay.cpp`, `include/view/plotlegendoverlay.h`)
   - The movable legend that floats over the chart: a translucent, rounded frame parented to `TmChart`, holding a scrolling single-column list of line-swatch + label rows
   - Owns its rows (reconciled by series id across rebuilds), its size and placement (top-right until dragged, clamped inside afterwards), its light/dark styling, and the drag itself
   - Takes a finished list of `Entry{id, label, color}`; `PlotWidget` decides which series belong in it, so the overlay never sees a ViewModel

6. **PlotWidget** (`src/view/plotwidget.cpp`, `include/view/plotwidget.h`)
   - Self-contained `TmChart` chart filling the whole widget - no external control rows; every control (Plot File, View Mode, Customize View, plot title, X/Y axis ranges, Export) lives in the right-click context menu built by `buildContextMenu()`/`showPlotContextMenu()` - plus a movable legend overlay (a translucent, draggable frame parented to the chart; single-column line-swatch + label rows with a vertical scrollbar for dense plots; composited into PNG/SVG exports)
   - Mouse wheel zoom and click-drag pan; `onSeriesVisibilityToggled()` toggles one series without a full rebuild
   - `TmChart` repaints from `paintEvent()`, so there is no replot call to schedule; context-menu entries stay disabled until data loads, and `applyTheme(bool dark)` syncs colors with the app theme

#### ViewModel

1. **MainViewModel** (`src/viewmodel/mainviewmodel.cpp`, `include/viewmodel/mainviewmodel.h`)
   - Owns application state, validation, and the per-stream `StreamConfig` captured by StreamConfigDialog
   - Exposes Q_PROPERTYs (`inputFilename`, channel lists/indices, `fileLoaded`, `progressPercent`, `processing`) for the View to bind to
   - `openFile()` loads metadata via Chapter10Reader and logs channel/time/frame info; `clearState()` first, so it always starts a fresh session (`sources()` reset, source-id counter reset to 0)
   - `addSource()` (multi-file input) loads a second/later file's metadata the same way but *without* `clearState()` — the file being configured is tracked the same either way (`m_input_filename`/`m_stream_configs`/`m_reader` all refer to whichever file is currently being configured), shared via the `loadFileMetadata()` helper both call
   - Builds a list of `StreamJob` objects (validated `ProcessingParams`, now stamped with the pending file's `sourceId`, + an owned `FrameSetup`) and hands them to ProcessingCoordinator
   - Receives each `ProcessedStreamData` and forwards it to PlotViewModel; on a successful run, `onCoordinatorProcessingFinished()` appends a `Source` record (`include/dto/source.h`) to `sources()` — a failed/cancelled run leaves no phantom entry; `removeSource()` drops one. Manages recent files

2. **ProcessingCoordinator** (`src/viewmodel/processingcoordinator.cpp`, `include/viewmodel/processingcoordinator.h`)
   - Owns the reader + worker thread lifecycle for multi-stream processing
   - `startProcessing(QVector<StreamJob>)` takes ownership of each job's FrameSetup, spins up one `Ch10PacketReader` thread plus one `FrameProcessor` worker thread (and a `PacketQueue`) per stream
   - `cancelProcessing()` requests a cooperative abort of all workers and the reader; `reset()` clears transient state
   - Emits `progressChanged(int)`, `processingStateChanged(bool)`, `streamProcessed(ProcessedStreamData)`, `processingFinished(bool)`, `logMessageReceived(QString)`, `errorOccurred(QString)`

3. **BatchController** (`src/viewmodel/batchcontroller.cpp`, `include/viewmodel/batchcontroller.h`)
   - *Processing templates / batch apply (US1.1)*; owns the sequential Apply Template run: the file queue, which file is in flight, the per-file config application, the appearance reuse, and the export post-pass
   - `validate()` checks each file's channel set against the template (the slow part of starting a batch, since it reads every file's channels); `buildTemplate()` captures a processed source as a template; `applyAppearance()` reapplies a template's saved names/colors. All three are static and free of dialog code, which is what makes them testable
   - Reports through `message(LogLevel, QString)` and `finished()` rather than calling the View, and renders per-file images through an injected `ImageExporter` callback — the widget draws, the controller decides when and where
   - Lived in `MainView` until v2.12.1; the View kept a state machine, which is the layering leak this removes

4. **PlotViewModel** (`src/viewmodel/plotviewmodel.cpp`, `include/viewmodel/plotviewmodel.h`)
   - Converts each `ProcessedStreamData` into in-memory `PlotSeriesData` vectors (name, receiver/channel indices, x/y values, cached Y min/max, color), carrying its `sourceId` through so cross-source identity checks (reprocess-replace, rename/recolor sibling-sync) never cross a source boundary
   - Converts absolute IRIG seconds to elapsed seconds against a shared base that tracks the **earliest absolute sample across every source** (not just the first-added one): `addStreamData()` re-bases — shifting every existing series right — when a later-added source started earlier; `removeSource()` is the mirror, shifting left only if the removed source held the earliest sample. Emits `nonOverlappingSourceWarning(sourceId)` (informational) the first time a newly added source's absolute range doesn't intersect what's already loaded
   - Assigns the purple/blue/green (lock) and red/orange/yellow (SNR) palette
   - Manages axis ranges (auto Y with margin, manual Y override, X time window), the left-axis view toggle (lock % vs accumulated missed frames), and per-series visibility
   - Signals `dataChanged()`, `axisRangeChanged()`, `seriesVisibilityChanged()`, `nonOverlappingSourceWarning(int)`; `computeYRange()` uses per-series cached min/max
   - `hasLeftYMaxOverride()`/`leftYMaxOverrideValue()` and the right-axis counterparts expose whether/what the user overrode the axis maxima

#### Model

1. **Chapter10Reader** (`src/model/chapter10reader.cpp`, `include/model/chapter10reader.h`)
   - Reads Ch10 file **metadata** up front: scans TMATS to catalog time and PCM channels, provides channel lists, time accessors, and channel ID resolution. Wraps the irig106utils C library.

2. **Ch10PacketReader** (`src/model/ch10packetreader.cpp`, `include/model/ch10packetreader.h`)
   - The single-pass reader. `prepare()` opens the file, parses TMATS, resolves each stream's PCM attributes, and builds the channel-ID → `PacketQueue` routing; `run()` (on its own QThread) reads the file once, tracks IRIG time, and dispatches each PCM packet's payload to the matching queues, then posts end-of-stream sentinels
   - Owns the `SuChanInfo` per-channel bookkeeping table

3. **PacketQueue** (`include/model/packetqueue.h`) — bounded, thread-safe per-stream packet queue connecting the reader to one worker (header-only)

4. **FrameProcessor** (`src/model/frameprocessor.cpp`, `include/model/frameprocessor.h`)
   - Per-stream worker: drains its PacketQueue, runs the bit-serial frame-sync scanner (acquire/lock, off-phase rejection, bit-span lock %), accumulates lock %, missed frames, and (SNR mode) calibrated channel values into a `ProcessedStreamData`
   - Private helpers include `derandomizeBitstream()` and `hasSyncPattern()`; applies linear slope/offset or a non-linear `CalibrationProfile` per channel

5. **FrameSetup** (`src/model/framesetup.cpp`, `include/model/framesetup.h`) — frame configuration / word-map + calibration table built per job

6. **ChannelData** (`src/model/channeldata.cpp`, `include/model/channeldata.h`) — channel metadata value object

7. **StepDetector** (`src/model/stepdetector.cpp`, `include/model/stepdetector.h`) — *US5.3*; pure (UI-free) logic that parses the `[[Step]]` step-config TOML and detects step plateaus in a raw-count series via derivative/edge detection, building a per-channel `CalibrationProfile`

8. **CalibrationExtractor** (`src/model/calibrationextractor.cpp`, `include/model/calibrationextractor.h`) — *US5.3*; drives a raw extraction over a calibration Ch10 file (reusing the Ch10PacketReader + FrameProcessor pipeline with unit slope / zero offset) and runs StepDetector per channel to build session-only profiles

9. **TomlConfigHelper** (`src/model/tomlconfighelper.cpp`, `include/model/tomlconfighelper.h`) — registers the custom QSettings TOML format every settings file is read and written through. It is the format only: the frame-sync helpers live with the dialogs that use them, and the receiver-parameters file (scalars + word map) is read and written by `FrameSetup`

10. **CsvSeriesParser** (`src/model/csvseriesparser.cpp`, `include/model/csvseriesparser.h`) — pure (UI-free) static parser that turns a FrameProcessor `Day,Time,param…` CSV into `PlotSeriesData` (returned as a `CsvParseResult`). Extracted out of PlotViewModel so file parsing lives in the Model layer; thread-safe, so PlotViewModel runs `parse()` on a worker thread via `loadCsvFileAsync()`

11. **SeriesColumnSchema** (`src/model/seriescolumnschema.cpp`, `include/model/seriescolumnschema.h`) — single source of truth for the CSV column-header format: builds each series' column header and the SNR series name, and parses a header back to its identity fields (metric type, name, stream label/order, receiver, source id). Shared by `PlotViewModel::exportCsv`/`addStreamData` and `CsvSeriesParser` so export and import stay inverses (pinned by `tst_seriescolumnschema`'s round-trip). Multi-file input: a series from source 0 exports with the unqualified, byte-identical-since-v2.6 header format; source 1+ gets a leading `"S<n>| "` qualifier that `parseColumnHeader()` strips back off before recovering identity

12. **StreamConfigSchema** (`src/model/streamconfigschema.cpp`, `include/model/streamconfigschema.h`) — single source of truth for `StreamConfig`'s JSON representation (`toJson()`/`fromJson()`), so no field list is hand-mapped twice. Excludes `calibrationByWord` (extracted profiles are session-only, never serialized) but includes the calibration *input references* (`calCh10Path`, `stepTomlPath`, `clipStartSec`, `clipEndSec`). Used by `ProcessingTemplateSchema`
13. **ProcessingTemplateSchema** (`src/model/processingtemplateschema.cpp`, `include/model/processingtemplateschema.h`) — *processing templates / batch apply*; serializes/parses a `ProcessingTemplate` (`schemaVersion`, `appVersion`, `name`, `timeChannelIndex`, and a list of `TemplateStreamEntry`). Delegates each entry's config to `StreamConfigSchema` and adds the per-entry `SeriesAppearance` array; no file paths (a template is location-independent); a `schemaVersion` newer than `kCurrentSchemaVersion` is rejected
14. **TemplateMatcher** (`src/model/templatematcher.cpp`, `include/model/templatematcher.h`) — *processing templates / batch apply*; pure, disk-free validator that a target file's PCM channel-ID set matches a template's **exactly** (`matchFile()` → `MatchResult{ok, missing, extra}`). Exact-set matching is what lets the batch loop feed the template's stored `pcmChannelId`s straight to processing
15. **IRIG 106 Library** (`lib/irig106/`) — third-party C library for the Chapter 10 file format (see Protected Files)

### Data Flow

```text
User opens .ch10 ─► MainView ─► MainViewModel ─► Chapter10Reader (metadata)
     (or Add Source)                  │           [assigns/reuses sourceId]
                            StreamConfigDialog (per-stream config)
                                      │
                      MainViewModel builds QVector<StreamJob>
                              [ProcessingParams.sourceId stamped]
                                      │
                             ProcessingCoordinator
                          ┌───────────┴───────────┐
                  Ch10PacketReader (one pass)      │
                          │  routes packets        │
                     PacketQueue ─► FrameProcessor (one worker per stream, parallel)
                                              │
                                   ProcessedStreamData (in memory, carries sourceId)
                                              │
                                   PlotViewModel ─► PlotWidget (TmChart)
                          [re-bases shared elapsed axis to the earliest
                           sample across all loaded sources; identity
                           checks key on (sourceId, streamLabel, order)]
```

## Qt-Specific Considerations

### Qt Version Compatibility

- **Target**: Qt 6.11.1
- **Important**: Qt 6 made significant changes to container classes
  - `QStringList` methods differ from Qt 5
  - Prefer range-based for loops when iterating over Qt containers

### Container Iteration Pattern

**Preferred** (range-based for loop):

```cpp
QStringList items = getItems();
for (const QString& item : items)
    doSomething(item);
```

**Avoid** (index-based with .length()):

```cpp
// This may not compile in Qt 6!
for (int i = 0; i < items.length(); i++)
    doSomething(items[i]);
```

### Required Includes

When using Qt classes, ensure proper headers are included:

- `QStringList` requires `#include <QStringList>`
- `QString` requires `#include <QString>`
- Composite widgets require their specific headers

## Coding Conventions

### File Organization

- **Headers**: `include/` directory
- **Implementation**: `src/` directory
- **Resources**: `resources/` directory
- **UI files**: If using Qt Designer (currently hand-coded)

### Naming Conventions

- **Classes**: PascalCase (e.g., `MainView`, `MainViewModel`, `Chapter10Reader`, `FrameProcessor`)
- **Constants**: kPascalCase in namespaces (e.g., `PCMConstants::kWordsInMinorFrame`, `UIConstants::kDefaultScaleIndex`)
- **Member variables**: `m_` prefix with snake_case (e.g., `m_frame_setup`, `m_reader`); widget members drop type suffixes when the declared type is clear (e.g., `m_input_file` not `m_input_file_lineedit`); buttons use `_btn` suffix (e.g., `m_process_btn`); settings members use `m_settings_` prefix (e.g., `m_settings_frame_sync`)
- **Methods**: camelCase (e.g., `process()`, `getTimeChannelComboBoxList()`)
- **Slots**: camelCase with descriptive names (e.g., `inputFileButtonPressed()`)
- **Struct fields**: `ProcessingParams` uses snake_case; the newer value types (`StreamConfig`, `StreamJob`, `ProcessedStreamData`, `CalibrationProfile`) use camelCase (Qt property style)

### Memory Management

- UI widgets created with `new` should specify parent widget for automatic cleanup
- Manual `delete` in destructor for widgets without parents
- Follow Qt's parent-child ownership model
- Use `const QString&` for all string parameters passed by reference

### Include Ordering (Google C++ Style)

Order includes in each `.cpp` / `.h` file as follows, with a blank line between groups:

1. Related header (e.g., `#include "mainview.h"` in `mainview.cpp`)
2. C system headers (e.g., `<time.h>`)
3. C++ standard library headers (e.g., `<fstream>`, `<string>`)
4. Third-party / Qt headers (e.g., `<QDebug>`, `<QMessageBox>`) — alphabetized
5. irig106 library headers — keep in dependency order, not alphabetized
6. Project headers (e.g., `"channeldata.h"`, `"constants.h"`) — alphabetized

### Signals and Slots

- Use Qt's signals/slots mechanism for event handling
- Connect signals in `setUpConnections()` method
- Uses new-style connect syntax (`&ClassName::signalName`)

## Build System

### qmake Project File (tmDataQualityAnalyzer.pro)

- Defines source files, headers, resources
- Configures Qt modules (core, gui, widgets)
- Sets C++17 standard
- Includes platform-specific libraries (ws2_32 for Windows sockets)

### Build Targets

- **Debug**: `nmake -f Makefile.Debug` → `debug/tmDataQualityAnalyzer.exe`
- **Release**: `nmake -f Makefile.Release` → `release/tmDataQualityAnalyzer.exe`

### VS Code Integration

Tasks are defined in `.vscode/tasks.json`:

- "qmake: Configure" - Runs qmake to generate Makefiles
- "Build (Debug)" - Compiles debug build
- "Build (Release)" - Compiles release build
- "Clean" - Cleans build artifacts
- "Rebuild" - Clean + Build

### clangd / IntelliSense (`compile_flags.txt`)

clangd and clang-tidy read a single project-root `compile_flags.txt` — one flag per
line, applied to every translation unit, which is sufficient here because every TU
shares the same includes and defines. It is **generated, not committed** (the Qt include
paths are machine-specific and absolute), and it is **gitignored**, so a fresh clone has
no IntelliSense until you run:

```powershell
py scripts/gen_compile_flags.py
```

Re-run it after a Qt version bump, or after changing `INCLUDEPATH` / `DEFINES` /
`QT +=` in the `.pro` — the generator mirrors those by hand (`PROJECT_INCLUDES`,
`QT_MODULES`, `DEFINES` in the script), so a `.pro` change silently leaves clangd stale
until it's regenerated on both sides.

It targets MSVC (`--target=x86_64-pc-windows-msvc`, `-fms-compatibility`), and clangd
then locates the MSVC and Windows SDK system headers itself — those paths are
deliberately **not** in the file. Verify with:

```powershell
clangd --check=src/view/plotwidget.cpp
```

which should end in `All checks completed, 0 errors`.

**Worktree trap.** The paths in `compile_flags.txt` are absolute into the checkout that
generated it, and `.claude/worktrees/<name>/` sits *inside* the main checkout — so
clangd walking up from a worktree source file finds the **main** tree's
`compile_flags.txt` and resolves every project header from the **main** tree. It still
reports `0 errors`, so nothing looks wrong while go-to-definition, diagnostics, and
completion all describe code you are not editing. **Run the generator inside each
worktree** (it writes relative to its own repo root), same as the `tests/data` junction
that worktrees also need.

### Deployment & Packaging

- **Build automation**: `deploy/build_release.ps1` — builds release, runs `windeployqt`, stages installer and portable layouts, signs exe, creates ZIP, compiles Inno Setup installer
- **Inno Setup installer**: `deploy/tmDataQualityAnalyzer.iss` — EXE installer with admin/non-admin support, TOML merge logic, `.ch10` file association, "What's New" page, Start Menu/desktop shortcuts
- **Portable ZIP**: Flat layout with `portable` marker file; QSettings redirected to app directory via `QSettings::setPath()` in `main.cpp`; includes LICENSE.txt and README.txt
- **Release notes**: `deploy/RELEASENOTES.txt` — shown as "What's New" page in installer (`InfoBeforeFile`)
- **Portable README**: `deploy/README_portable.txt` — copied as README.txt into portable ZIP
- **App root auto-detection** (`mainviewmodel.cpp`): Checks if `settings/` exists next to the exe (portable) or one level up (installed/dev/build)
- **Installed layout**: `{install}/bin/tmDataQualityAnalyzer.exe` + `{install}/settings/default.toml`
- **Portable layout**: `tmDataQualityAnalyzer.exe` + `settings/default.toml` + `portable` marker + `LICENSE.txt` + `README.txt` in same directory
- **Code signing**: Optional via `SIGN_CERT_SHA1` environment variable (SHA-1 thumbprint of certificate in Windows Certificate Store)

## Important Implementation Notes

### Chapter 10 File Handling

- Uses irig106utils library (C code, not C++)
- Be careful with C/C++ interop (no exceptions in C code)
- File handles managed through `m_file_handle`
- Buffer management for reading packets

### Time Handling

- Uses IRIG time format and standard time structures
- Time conversions between different formats (DOY/HMS ↔ uint64)
- UTC timezone enforced in Chapter10Reader and FrameProcessor constructors

### AGC / Stream Processing

- Central processing function: `FrameProcessor::process()`, driven per stream from its `ProcessingParams` + `FrameSetup`
- Drains the stream's `PacketQueue` (fed by the single `Ch10PacketReader`) rather than opening the file itself
- Accumulates results into an in-memory `ProcessedStreamData` (lock %, accumulated missed frames, and SNR channel series) — there is no CSV-on-disk intermediate
- One `FrameProcessor` per stream runs on its own `QThread`; `ProcessingCoordinator` owns the lifecycle and tears the threads down when each worker finishes
- Emits progress, completion, log, and error signals consumed by the coordinator

### Framesync Lock Statistics Calculation

- **Fundamental Metrics:**
  - `Frame_Length_Bits = Bits_per_word * Words_per_frame`
  - `Expected_FPS = Bitrate_bps / Frame_Length_Bits`
- **Window Processing:** The telemetry stream is processed in discrete time windows based on a user-specified time resolution (`T_res` in seconds).
- **Expected Frames per Window:** `Expected_Frames = Expected_FPS * T_res`
- **Calculation:**
  - During each `T_res` window, the stream is scanned for the valid framesync pattern.
  - A counter (`Locked_Frames_Count`) increments for each valid framesync found (optionally enforcing correct bit-spacing for strict locks).
  - At the window boundary, the lock percentage is calculated as `(Locked_Frames_Count / Expected_Frames) * 100` and clamped to a maximum of 100.0%.
  - The counter resets for the next time window.

## Common Development Tasks

### Adding a New UI Widget

1. Declare pointer in `mainview.h` private section
2. Create widget in appropriate `setUpSection()` method
3. Add to layout
4. Connect signals if needed in `setUpConnections()`
5. Handle cleanup in destructor if no parent
6. If the widget drives business logic, expose the action via a MainViewModel slot or property

### Modifying Build Configuration

1. Edit `tmDataQualityAnalyzer.pro`
2. Re-run qmake from the project root: `qmake tmDataQualityAnalyzer.pro -spec win32-msvc CONFIG+=debug`
3. Rebuild: `nmake -f Makefile.Debug`

### Adding a New Per-Stream Setting

1. Add the field to the `StreamConfig` struct in `include/dto/streamconfig.h` (with a default in `constants.h` if appropriate)
2. Surface it in the relevant gear sub-dialog in `StreamConfigDialog` (Frame Sync Lock or Receiver SNR setup), and include it in the "Apply to all" fan-out if it should propagate
3. If it must round-trip to a TOML file, add it to the matching `TomlConfigHelper` load/save helper (respecting the US5.1/US1.0 boundaries)
4. Carry it into `ProcessingParams` (and `FrameSetup` if it affects the word map) where `MainViewModel` builds each `StreamJob`
5. Consume it in `FrameProcessor::process()`

## Debugging

### Common Issues

1. **Build fails with PATH errors**
   - Dot-source `scripts\env.ps1` first — it imports the MSVC environment and puts the Qt
     `msvc2022_64` kit bin on PATH
   - Check paths in `.vscode/tasks.json` match your Qt installation
2. **qmake fails: "QMAKE_MSC_VER isn't set"**
   - A stale `.qmake.stash` (qmake caches the compiler's version detection and shares it up the
     directory tree) is masking MSVC detection — e.g. a leftover from a pre-switch MinGW build.
     Delete `.qmake.stash` (and any in parent dirs) and build in a clean dir.

3. **MOC errors**
   - Ensure Q_OBJECT macro is present in classes with signals/slots
   - Re-run qmake if header structure changed

### Build Warnings

- Build should produce 0 warnings. If new warnings appear, fix them before committing.

## Testing

Automated unit tests use the **Qt Test** framework. Test sources are in the `tests/` directory with a separate `tests/tests.pro` project file.

### Continuous Integration

`.github/workflows/ci.yml` builds the app and runs the full suite on every push to
`main` and every PR. It prefers a **self-hosted Windows runner** (local toolchain and
the full-size `.ch10` recordings on disk) and falls back to GitHub-hosted
`windows-latest` when that runner is offline or a contributor lacks access — both
paths run the same suite. The workflow's own constraints (ASCII-only, Windows
PowerShell 5.1, `QT_QPA_PLATFORM=offscreen`) are documented in its header comment;
the runner machine's setup and recovery procedure is in
[`docs/ci_runner.md`](ci_runner.md).

### Test Suites

The suites below are registered in `tests/main.cpp`, in a different order than they're
listed here: the fast, no-fixture suites run first, followed by the four heavy
`.ch10`-integration suites (`TestChapter10Reader`, `TestFrameProcessor`,
`TestProcessingCoordinator`, `TestCalibrationExtractor`), which `--fast`/`TMDQ_FAST_TESTS`
skip for quick local iteration. The source/header files are listed in `tests/tests.pro`.

- **TestChannelData** (`tst_channeldata`) — ChannelData model object tests
- **TestChapter10Reader** (`tst_chapter10reader`) — Chapter 10 metadata reader: channel discovery, time/PCM channel lists, channel ID resolution against real Ch10 test data
- **TestConstants** (`tst_constants`) — Verifies all PCMConstants, UIConstants, PlotConstants, AppVersion, and recent files constants (including kMaxPacketBufferSize, kFrameSyncHexPattern)
- **TestFrameProcessor** (`tst_frameprocessor`) — constructor defaults, abort flag, `derandomizeBitstream` (identity/short and changed/long), invalid time-channel/PCM-channel/file handling, and processing real Ch10 data (receiver-data accumulation, lock-only mode has no channels, monotonic frame-sync errors, slope affects values, shorter period → more samples, calibration round-trip clean steps, off-phase sync after lock-loss not extracted)
- **TestMainViewModelHelpers** (`tst_mainviewmodel_helpers`) — `MainViewModel::classifyLogMessage()`, the severity policy the View renders but does not decide. The channel-naming cases that used to live here moved to **TestFrameSetup** with the `MainViewModel` forwarders they called: `FrameSetup` is where that naming lives
- **TestFrameSetup** (`tst_framesetup`) — Frame parameter loading, word map, calibration, `channelPrefix()` naming the known channel positions then falling back to `CH<n+1>`, `receiverParameterName()` combining the two, `receiverIndexFromName()` inverting it (0 for a name with no parseable `_RCVR<N>`), and the receiver-parameters file as a whole: the scalars read from the `[Receivers]` block the shipped files actually use (and the older `[Parameters]` keys still accepted), a save/load round trip that carries the word map with them, the default-map fallback when no file was loaded, nothing written when there is no map to write, and the metadata-only file that parses to zero parameters — the contract `tryLoadingFile()`'s two callers depend on
- **TestPlotViewModel** (`tst_plotviewmodel`) — default state, CSV load/export (incl. header-only, malformed rows, async load signals), time conversion/formatting, color assignment, the computed Y range and the SNR max override that pins it (the minimum stays automatic, Reset returns it to the data), X time window, visibility, clear/title, in-memory `addStreamData` (lock/SNR/error series, multi-stream accumulation), the left-axis view toggle preserving per-stream selection, and stream-identity regression coverage: two streams sharing a TMATS-derived `streamLabel` but different `streamOrder` must stay independent through reprocess-replace and `renameSeries()`/`recolorSeries()` sibling-sync (pinned after a pre-v2.6.0 cross-contamination bug). Multi-file input: cross-source identity (two different `sourceId`s reusing the same `streamLabel`/`streamOrder` stay independent through reprocess-replace/rename/recolor), time-base re-basing (a later-added source starting earlier shifts existing series right; three successively-earlier sources compound correctly; a later source starting after triggers no shift), the non-overlap warning signal (fires once per newly-arriving non-overlapping source, not for an overlapping range), and `removeSource()` (drops only the target source, re-bases left only when the removed source held the earliest sample, clears all data when the last source is removed, no-ops for an unknown id). US1.1 source view: `setVisibleSource()` isolates a source via `effectiveVisible()` without mutating per-series `visible`, `sourceList()` lists distinct labeled sources, and `exportCsv(path, sourceId)` writes only that source's columns
- **TestProcessingCoordinator** (`tst_processingcoordinator`) — constructor defaults, `reset()` clears state, cancel-with-no-run no-op, `startProcessing()` empty-returns-false and processing-state emission, plus single-vs-multi-stream throughput benchmarks
- **TestMainView** (`tst_mainview`) — Main window construction, widget wiring, log routing, dock visibility behavior, and the CSV import routing (`openPath`/`importCsv`, valid/invalid). The batch-apply cases moved to **TestBatchController** with the state machine itself
- **TestPlotWidget** (`tst_plotwidget`) — Plot widget construction, null/valid ViewModel connection, dark/light theme application, the movable legend overlay populating from data (hidden until data loads, then one row per visible active-metric series), a shown/resized-window regression case asserting the overlay sizes correctly (not a collapsed frame-only box) after a second rebuild adds more rows — a QScrollArea `widgetResizable` sizeHint staleness bug reproduced and fixed post-review — the SNR legend row showing the short `"CH<id> <ch.name>"` form instead of the full TMATS stream title, the legend row layout reserving a right-side gutter matching the style's scrollbar extent, and each legend row carrying an objectName the overlay stylesheet can target to override the app's global `QWidget { background-color: ... }` theme rule (otherwise every row painted as an opaque chip); plus `exportImage()` writing a PNG/SVG/PDF headlessly (the parameterized image-export entry point extracted for batch apply); and the right-click context menu that replaced the external control rows: it lists every expected top-level item, its data-dependent entries are disabled until data loads, the Plot File submenu is disabled for a single source and switches `visibleSource` for a multi-source run, View Mode reflects and sets the active left-axis metric, `X Axis > Reset Span` / `Y Axes > Reset` clear the X window and both Y overrides, plus two structural guards - wheel-zoom/drag-pan stay enabled (horizontal-only) once data arrives, and **no QComboBox / QSpinBox / QLineEdit / QAbstractButton lives outside the chart** - on-chart overlays are allowed, external control rows are not (the machine-checkable form of "no external controls"); plus the legend toggle (shows/hides the overlay and survives a rebuild, appears only once data is loaded, is parented to the chart, and stays in sync with the context menu's checkable Show Legend item); plus Y axis occupancy - a lock-only plot hides the right axis, an SNR-only plot hides the left, an empty chart keeps the left (hiding both would leave a bare box), and hiding the last *visible* SNR series reclaims the right axis just as never loading one would
- **TestTmChart** (`tst_tmchart`) - the first-party chart replacing QCustomPlot: series bookkeeping across removals and out-of-range access, mismatched x/y lengths truncated (a draw-time overrun), degenerate/non-finite ranges rejected, X and Y coordinate transforms round-tripping with the correct screen orientation, wheel zoom anchoring the value under the cursor and reporting the new range, interactions inert until enabled, and the shared `renderTo()` export path painting real content while omitting the crosshair/zoom-band overlays (cursor state, not data), and that hiding a Y axis reclaims its margin (the measurable consequence) while leaving the coordinate transforms consistent with the new plot area
- **TestPlotCustomizationDialog** (`tst_plotcustomizationdialog`) — Customize Plot Series dialog: one Frame Sync Lock checkbox per stream, Select All/None, apply → per-stream lock/missed visibility round-trip to the ViewModel; Receiver SNR tree build (receiver grouping), tri-state group cascade, Select All/None, apply → per-channel SNR visibility round-trip, and the Expand/Collapse All button toggle; plus per-stream rename/recolor (lock tab) and per-channel rename/recolor via pending item roles (SNR tab) applied to the ViewModel on OK, and the single batched `seriesAppearanceChanged` emission (reaches private widgets/slots via a friend declaration, same pattern as TestFrameProcessor)
- **TestStreamConfigDialog** (`tst_streamconfigdialog`) — Per-stream Configure Streams dialog: stream rows, mode selection, gear setup dialogs, TOML load/save round-trips, "Apply to all" fan-out, the Channel column label (short names shown in full, long TMATS-derived names elided on the right with "..." , left-justified, styled via `channelNameCell` to mimic the Mode combo box's border/fill, and the full name always available via tooltip — which is now the only place a truncated name is recoverable), the Mode combo's left-justified closed-box text (via an editable-but-readonly internal line edit) while selection still tracks correctly, and the table header/separator using theme-QSS object names (`streamHeaderLabel` / `streamHeaderSeparator`) rather than hard-coded inline colors
- **TestExportDialog** (`tst_exportdialog`) — Export dialog checkbox-to-field enable logic, export-button validation, and the log-export row defaults/accessors and log-only validation
- **TestStepDetector** (`tst_stepdetector`) — Non-linear calibration (US5.3): `[[Step]]` TOML parsing (valid / empty-fails), plateau detection (clean, too-few-fails, extra-plateaus uses last of monotonic run, short-blip doesn't steal a pairing slot, long leading transient doesn't shift pairing, inverted-polarity sweep not reversed, non-monotonic pairing rejected, noisy, settling-at-plateau-start excluded, round-trip exact), the calibration-import resilience cases (the outcome code names the gate that rejected, a compressed out-of-tolerance receiver still calibrates via the relaxed threshold while a healthy one does not relax, noise-split plateaus coalesce instead of severing the sweep, a flat channel reports "flat / no signal" rather than a plateau shortfall, samples pinned at the floor or rail kept out of the noise estimate so live dwells neither split nor shatter, a split dwell far above the noise still paired as one step, railed top steps calibrating the range that stayed in scale while the rail clamps rather than extrapolates, an unconfirmed dwell measured from the dwell grid rather than bridged or shifted, the number of levels the sweep held reported so a short step file can be flagged, and which step file the Extract Calibration dialog preloads), and `interpolateCalibration()` (midpoint, below/above clamping, coincident-raw guard)
- **TestSeriesColumnSchema** (`tst_seriescolumnschema`) — the CSV column-header schema (`SeriesColumnSchema`): SNR name and `columnHeader()` formatting, `parseColumnHeader()` classification (lock/missed suffix vs. SNR `"<id> - "` prefix, multi-word stream labels split at the last space, SNR-shape-wins-over-suffix), the unknown→SNR fallback, and the format→parse round trip that keeps `exportCsv` and `CsvSeriesParser` inverses. Multi-file input: source 0 stays unqualified (byte-identical), source 1+ gets a leading `"S<n>| "` qualifier that round-trips through parse for both SNR and Lock/Missed shapes, an unqualified header still defaults to source 0, and a header merely starting with the letter `'S'` isn't misparsed as a qualifier
- **TestCalibrationExtractor** (`tst_calibrationextractor`) — US5.3 pipeline orchestration (complements TestStepDetector's pure logic): drives the async extraction end to end (reader + FrameProcessor workers → per-channel StepDetector). A bad file (with non-empty steps, so it clears the empty-steps guard) finishes unsuccessfully with a recorded error and no partial state; over `rnrz-l_testfile.ch10`, exactly words 6/7/8 (RCVR3 L/R/C, the only real stepped SNR sweep) build valid non-linear profiles while every other receiver word falls back to linear; over `step_cal, example.ch10` (receivers far out of alignment, its real 11-step 0–60 dB config, the shipped sequential word map), receivers 1, 3, 5 and 6 calibrate with every step paired to its own dwell (0 dB at the pre-sweep level, 60 dB at the top dwell, no split dwell taking a slot), receiver 1 over only the range that stayed in scale because its top two steps sit on the rail (readings hold at 48 dB), receiver 2 reports flat / no signal, and the report names the receivers; plus the pure `summarize()` report (receivers grouped, partial receivers spelled out with reasons, unnamed channels reported by name) and its two "check" prompts - a step file shorter than the recording and a word map that does not match it - each proven on the real recording with the wrong file, and silent on the correct setup; and clips that exceed the recording being reported rather than silently ignored
- **TestStreamConfigSchema** (`tst_streamconfigschema`) — `StreamConfigSchema` round trip for both `StreamMode` variants and the calibration input references (`calCh10Path`/`stepTomlPath`/`clipStartSec`/`clipEndSec`), confirms `calibrationByWord` itself never appears in the serialized JSON (and the `calibration` block is omitted entirely when no non-linear calibration was ever extracted), and `fromJson()` rejecting an object missing required fields while leaving in-class defaults for anything else omitted
- **TestProcessingTemplateSchema** (`tst_processingtemplateschema`) — processing templates / batch apply: `ProcessingTemplateSchema` round trip for both `StreamMode`s, series appearance, calibration input references, and `timeChannelIndex`; that appearance is omitted when empty; that `calibrationByWord` is never serialized (inherited from `StreamConfigSchema`); and `schemaVersion` accept/reject + malformed-document rejection — no `.ch10` fixture, runs in CI
- **TestBatchController** (`tst_batchcontroller`) — the Apply Template batch loop (US1.1): `buildTemplate()` capturing a source's configs + time channel + byte order + each series' name/color, `applyAppearance()` mapping a template's saved appearance onto the right series (both moved here from TestMainView, and no longer need a window to run), plus two cases the loop's old home made impractical — a queue whose files have all gone missing drains to `finished()` with both counted as skipped rather than stalling on a reader, and `start()` with nothing matched does not clear the session or announce a run
- **TestTemplateMatcher** (`tst_templatematcher`) — batch apply channel-ID matching: `templateChannelIds()` collection and `matchFile()` exact-set comparison (pass regardless of order; reject with the right `missing`/`extra` sets on a missing channel, an extra channel, and both) — pure logic, runs in CI

### Running Tests

```powershell
. .\scripts\env.ps1   # imports MSVC + puts the Qt msvc kit on PATH
cd tests
qmake tests.pro -spec win32-msvc
nmake -f Makefile.Debug
.\debug\tmDataQualityAnalyzer_tests.exe -o results.txt,txt
```

### Adding a New Test

1. Create `tst_newtest.h` with `Q_OBJECT` and private slots for each test case
2. Create `tst_newtest.cpp` with test implementations
3. Add both files to `tests/tests.pro` under `SOURCES +=` and `HEADERS +=`
4. Add `#include "tst_newtest.h"` and a `QTest::qExec()` block in `tests/main.cpp`

## Future Feature Candidates

All identified items have been implemented. No outstanding candidates at this time.

## Additional Resources

- [Qt 6 Documentation](https://doc.qt.io/qt-6/)
- [IRIG 106 Chapter 10 Standard](https://www.irig106.org/)
- [irig106utils GitHub](https://github.com/atac/irig106utils)
- [Qt Coding Conventions](https://wiki.qt.io/Qt_Coding_Style)
