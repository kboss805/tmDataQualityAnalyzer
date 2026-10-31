# tmDataQualityAnalyzer — AI Assistant Guide

This file provides context and guidelines for AI assistants working on the tmDataQualityAnalyzer project.

## Version Information

- **Qt Version**: 6.12.0 (minimum: Qt 6.0.0)
- **Compiler**: MSVC 2022 (Visual Studio 2022 C++ Build Tools, `cl` / `nmake`), Qt `msvc2022_64` kit.
  This is the only supported toolchain (and what CI uses).
- **C++ Standard**: C++20 (moved from C++17 for `std::format`, which the de-Qt string work needs in place of `QString::arg`; `inline constexpr` is used throughout constants.h)
- **Project Version**: 2.14.2 — defined once in the `AppVersion` struct in `include/constants.h`; qmake parses it from that header and propagates it to the Qt `VERSION` and the Windows resource file (`version_autogen.h`), so no other file carries a duplicate version literal

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
- [x] The context menu provides: **Plot File** (which processed file to view), **View Mode** (Lock Percentage / Accumulation), **Customize View…**, **Set Plot Title…**, **Show Legend** (checkable), **X Axis** (Set Time Window… / Zoom Back / Reset Span), **Y Axes** (Set Left Min… / Set Left Max… / Set Right Min… / Set Right Max… / Reset), and **Reset View**. Export is deliberately not here — it writes a CSV, an image and the log, which is a file operation on the session rather than a plot control, so it lives only in the main menu beside Import CSV.
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
- [x] The user can show or hide the legend from the context menu's **Show Legend** item (or `L`). The choice persists across sessions, and a hidden legend is excluded from exported images.
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
- [x] The legend can be shown or hidden from the context menu (or `L`), and the choice persists across sessions.
- [x] The persistent on-chart controls are gathered into a single chip bar at the chart's top-left (View Mode chip, Reset view chip) rather than scattered around the plot; each chip hides itself when it would be meaningless, and with neither showing the bar collapses to nothing - so it costs no chart space at rest.
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
- [x] The installer never overwrites an existing user TOML settings file, on upgrade or reinstall. A settings file is written only when it is absent (`onlyifdoesntexist`), and is left behind by uninstall (`uninsneveruninstall`).
- [x] A settings file written by any earlier version stays usable: every reader supplies its own default for a key the file does not carry, so an older file degrades to "missing keys fall back to defaults" rather than to a wrong result.
- [x] New fields added to a shipped default therefore reach a **fresh install**, not an existing one. An upgraded install keeps the operator's file exactly as it stands.

  - **Replaces two criteria that described a merge this project never built.** They claimed the installer saved a conflicting default as `new_x.toml` and carried the user's old values into it. Nothing implements that: `tmDataQualityAnalyzer.iss` is 95 declarative lines with no `[Code]` section, and there is no `new_x`, migration, schema-version or merge logic in `src/`, `deploy/` or `scripts/`. Confirmed on a clean machine by `deploy/sandbox/` - a first run wrote nothing under `settings\` and no registry key.
  - **The guarantee above is weaker but true, and the defensive readers are what make it safe** - `FrameSetup::readReceiverParams()` accepts both the current `[Receivers]` keys and the older `[Parameters]` spelling, an absent `Randomized` leaves the operator's choice alone rather than clearing it, and `StreamConfigSchema::fromJson()` keeps in-class defaults for anything omitted. A change that removed that tolerance would look harmless and would not be: it is load-bearing for this criterion, not incidental.
  - **When a merge would become necessary:** a shipped default gaining a field whose absence an old file cannot express - where "missing" and a deliberate choice are indistinguishable. `Randomized` is exactly that shape and was only safe because the reader was written to notice. Option B - building the merge in the installer's `[Code]` section - was scoped and deliberately not taken; see the "Settings on upgrade" row in `docs/future_plans/README.md` for why, and rebuild the sketch from this criterion if that day comes.

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

### Unreleased

Items from `docs/future_plans/dotnet-port-assessment.md` that are worth doing whether or not
that port ever happens - the throughput guard, the TOML move off `QSettings`, and now the
de-Qt of the native core in small steps (QtGui out of the model's headers, then
`PacketQueue`, then the `QElapsedTimer`s, then the packet bytes, then the first `QVector`s, the processing result and the pipeline's pointer lists) - plus the clean-build regression the TOML change
caused, a generated Visual Studio solution, and C++20. The first `QString` steps move `TomlDocument`,
`FrameSetup` and the step-file parser to UTF-8 `std::string`, and the `QObject` steps take
Qt out of `FrameProcessor` and `Ch10PacketReader`; the DTOs follow, starting with
`ProcessingParams` and `ProcessedStreamData`, and `FrameSetup`, `Chapter10Reader` and `ChannelData` lose their `QObject`s and `QString`s, the calibration report becomes `std::string`, and `CalibrationExtractor` becomes a plain class (the last `QObject` in the core); the last Qt header the core reached, in `constants.h`, goes, and a test now guards it. One user-facing fix, found during the
`QString` survey: recordings under a folder with a non-ASCII name can be processed.

#### A Visual Studio solution, generated rather than committed

`scripts/gen_vs_project.ps1` produces `tmDataQualityAnalyzer.sln` for developers who want
Visual Studio Community instead of VS Code + the `env.ps1` shell. It runs `qmake -tp vc`
on the app and test `.pro` files (so the `.pro` files stay the single source of truth for
sources, defines and libraries) and writes the solution around the two projects. The
output is **gitignored**: it carries absolute Qt and repo paths, exactly like
`compile_flags.txt`, and a committed copy would rot at the next Qt bump. Regenerate after
a `.pro` change, a Qt bump or a Visual Studio update. Regenerating keeps build products,
so the build that follows is incremental (0 files recompiled, 3 s, against about 170 s
for a clean one).

Raw `qmake -tp vc` output does **not** build usefully in Visual Studio. Six things are
corrected after the fact, each found by building and reading the failure:

1. **Executable location.** The app finds `settings/` relative to its own directory and
   the tests find `data/` one level above theirs, so the exes must land in
   `build/<cfg>/` and `tests/<cfg>/`, not under `vs/`. `OutDir` is rewritten.
2. **Vendored C warnings.** `lib/irig106` raises C4100/C4267 at the project warning
   level, and the zero-warning gate applies. The library may not be edited, so its
   `ClCompile` items get `WarningLevel=TurnOffAllWarnings`. The script throws if it finds
   none, so a layout change cannot silently turn this into a no-op.
3. **Qt's own deprecation warnings.** `thirdparty.pri` passes `/external:anglebrackets
   /external:W0`; MSBuild adds `/external:W3` itself, which then collides (D9025,
   "overriding /external:W3 with /external:W0"). The raw options are stripped and the
   equivalent properties `TreatAngleIncludeAsExternal` and `ExternalWarningLevel` are
   set instead.
4. **Parallel PDB writes.** A solution build compiles both projects at once and fails
   intermittently with `C1090: PDB API call failed, error code '3'` (about one build in
   four) because the two compilers race on the PDB service. `ForceSynchronousPDBWrites`
   (`/FS`) fixes it; three consecutive full `/t:Rebuild` runs were clean after the change.
5. **Qt runtime.** A post-build step runs `scripts/deploy_qt_local.ps1`, so F5 needs no Qt
   on `PATH` (the same reason that script exists for IDE launches in general).
6. **Solution file.** qmake writes only projects. The `.sln` is written directly (UTF-8
   with BOM, CRLF), app project first so it is the startup project, with the project GUIDs
   read back out of the generated `.vcxproj` files.

Verified: Debug|x64 and Release|x64 both build with 0 warnings and 0 errors, and the
Visual Studio-built test exe gives 426 passed / 0 failed / 1 skipped, identical to the
`jom` baseline. No Qt VS Tools extension is required.

**Toolchain drift, and the pin.** Since Visual Studio Community 2026 was installed,
`env.ps1` resolved `cl` to its MSVC 14.51 (toolset v145) rather than the Build Tools'
14.44 (v143): `vswhere -latest` takes whatever is newest, so the "MSVC 2022 only"
statement in these docs, the Qt `msvc2022_64` kit and CI no longer described what a local
build used, and a tree built across the install held objects from both compilers.
`env.ps1` now asks vswhere for `[17.0,18.0)` (override: `TMDQ_VS_RANGE`), and skips its
2022 path-probe fallback when a range was requested so a failed 2026 lookup cannot quietly
return 2022. `gen_vs_project.ps1` requests `[18.0,19.0)`, because the projects it writes
carry the compiler's `PlatformToolset` and VS 2026 builds v145; it warns if the shell
already holds an older environment. Verified: default resolves 14.44, the override
resolves 14.51, an unsatisfiable range throws, and a clean Debug rebuild of the app and
tests under 14.44 gave 0 warnings and 435 passed / 0 failed / 1 skipped.

#### A deleted header broke the clean build, and an incremental one could not see it

CI went red on `main` for one reason: the TOML change below deleted
`include/model/tomlconfighelper.h` and left a dangling `#include` in
`src/view/streamconfigdialog.cpp`. The include was **dead** - nothing in that file used
it - so nothing functional was wrong, but a clean build cannot compile the file.

**Every verification build had been incremental, and that is why it passed.** jom does not
notice that a header has been *deleted*, so the stale `streamconfigdialog.obj` was relinked
and the build reported success. The full suite ran **414 / 0 / 0 with zero warnings** against
that stale object. CI builds clean and failed immediately.

This is the stale-object trap `CLAUDE.md` already documents, arriving from the opposite
direction: not a crash at a meaningless address, a **false pass**. And grepping for the class
name `TomlConfigHelper` could not have found it, because what survived the refactor was the
*filename*. `CLAUDE.md` now says plainly that a change deleting a header is only verified by a
clean build.

A second stale-artifact trap on the way to fixing it, also now recorded: wiping `tests/debug/`
removes the Qt runtime that `deploy_qt_local.ps1` had put beside the test exe, so the suite
dies before its first test with exit **127** and `Qt6Cored.dll: cannot open shared object
file`. Meanwhile `output/results.txt` still holds the *previous* run's numbers, so reading it
reports a result for a run that never happened.

#### Qt's own deprecation warnings are suppressed, in the one way v2.9.0 left open

With the compile error gone, a clean build surfaced **13 `warning C4996`** - all from Qt 6.12
itself, at `qguiapplication.h(169)`, where `QGuiApplication` overrides the deprecated
`QCoreApplication::compressEvent`. Every translation unit that includes `<QGuiApplication>`
emits it, this application never calls `compressEvent`, and there is no fix available in our
code. CI's gate matches `warning [A-Z]\d+`, so a clean build could not go green while they
were visible.

`thirdparty.pri` now sets **`/external:anglebrackets /external:W0`**.

**Deliberately not `/wd4996`.** That flag used to live in this very file, suppressed
deprecation warnings for *application* code as collateral, and was removed on purpose in
v2.9.0 - the file carries a note saying so. `/external:` suppresses warnings whose *location*
is a non-first-party header while leaving our own files fully warned, and a deprecated API
called from our code is reported at **our** call site, so those keep warning exactly as
v2.9.0 intended. Every first-party header here is quote-included, so `anglebrackets` covers
Qt, the CRT and the Windows SDK in one rule. Stable since VS2019 16.10; this toolchain is
19.51.

#### The session guide moves into `.claude/`

The short guide Claude Code loads at the start of every session moves from the repository root to **`.claude/CLAUDE.md`**, beside `settings.json` and the project skills, so everything Claude-specific lives in one folder. Claude Code loads it from there exactly as it did from the root. This file (`docs/claude.md`), the full developer guide, stays in `docs/`. No code changed.

- **The root `.gitignore` re-includes it** (`!.claude/CLAUDE.md`). It ignores everything under `.claude/` except the files it lists, so without that line the moved file would have dropped out of git without a word, the same way `dotnet/build/` and `TmDq.App/` hid in step 3.1.
- **`scripts/check_release_consistency.ps1` reads the new path** for its "Current version" check, which CI runs on every push; it found the line there.
- **Every reference to where the guide lives now points at `.claude/CLAUDE.md`**: the skills (`build-and-test`, `code-cleanup`, `cut-release`), the plan, the release and Visual Studio scripts, a project-file comment, the Markdown lint config. The README's tree gains a `.claude/` line. References that record history (plan step 0.1, the future-plans table of what PR #40 touched) keep the name they had then.
- **References to `docs/CLAUDE.md` now spell it `docs/claude.md`**, the name git actually tracks. Windows ignores the difference, so it never failed here, but a case-sensitive checkout and GitHub's links do not find the upper-case name (`git add docs/CLAUDE.md` once staged nothing). The changelog entries below keep their original spelling.

#### The Avalonia app's projects and conventions (plan step 3.1)

The first step of the Avalonia application. Nothing user-facing changed: the Qt application is still the one that ships, and the new app is a window that shows its version. What this step settles is how every later step's code is built.

- **`dotnet/TmDq.App`** (Avalonia 12.1.4, Fluent theme, CommunityToolkit.Mvvm 8.4.2; the exe is `tmDataQualityAnalyzer.exe`) and **`dotnet/TmDq.App.Tests`** (xunit v3 on `Avalonia.Headless.XUnit`, with the app's own `App` class on the headless platform and Skia drawing), both in `tmdq.sln` next to `TmDq.Core` and its tests.
- **VS Code opens `tmdq.sln` by default**: `.vscode/settings.json` sets `"dotnet.defaultSolution": "tmdq.sln"`, so the C# extension loads the native projects and all four .NET projects. One consequence: C# Dev Kit then builds those projects in the background, into the same `obj\` and `bin\` as a command-line build, and the two can collide (a locked `.pdb`, a build task that "failed unexpectedly"). Normal work is unaffected; a long loop of command-line builds, such as a mutation run, belongs in a git worktree.
- **The version is written once.** `dotnet/build/AppVersion.props` reads `kMajor`, `kMinor` and `kPatch` from `include/constants.h` at build time, as qmake and the release scripts already do, and sets the assembly, file and product versions (the exe's version resource reads 2.14.2). `TmDq.Core` and both test projects import it as well. A header it cannot read stops the build with that message; it sets a version only when all three numbers were read, because a partial one (`7.81.`) fails restore first with a message about the version string.
- **The UTF-8 code-page manifest is in the exe.** As in the spike, the SDK's `ApplicationManifest` reaches the managed assembly, not the apphost that is the process, so `mt.exe` merges `dotnet/TmDq.App/app.manifest` into the exe after the build. It replaces the apphost's own manifest, so it also declares Windows 10 compatibility.
- **The licence allow-list (plan decision D5).** `dotnet/build/PackageLicences.targets`, imported by all four projects, runs `scripts/check_package_licences.ps1` after restore over every package in `project.assets.json`, transitive ones included. Allowed: MIT, Apache-2.0, BSD-2-Clause, BSD-3-Clause; an SPDX expression passes if one `OR` alternative is wholly allowed. A licence URL passes only from licenses.nuget.org naming an allowed id, or when reviewed by hand. A licence **file** passes only when its SHA-256 is in the script's `$Reviewed` table, so a later version that changes its licence fails again. One so far: `Avalonia.Angle.Windows.Natives`, ANGLE's standard BSD 3-clause text, which `Avalonia.Desktop` brings and which stopped the very first build. A failure is an MSBuild error naming every offending package. The check re-runs when the assets file changes, so a package added to a project that already built is checked too.
- **Not in CI yet:** the app and its tests build and run locally; CI for them is step 3.2. CI does already exercise the licence check, through `TmDq.Core.Tests`' build.

**Two things git was hiding.** The root `.gitignore`'s `build/` (the Qt build folder) matched `dotnet/build/`, and its `*.app` (a macOS bundle) matched `dotnet/TmDq.App/`, because git on Windows ignores case. `git status` did not list either folder at all, so a commit would have left out the app and its build files without a word. `dotnet/.gitignore` re-includes both, keeping the change in that folder as its header asks.

**Two build traps, now handled in the code:**
- An imported `.targets` file sees `$(IntermediateOutputPath)` empty in its property groups (the SDK sets it later), so the licence check's stamp first landed beside each project file. Its path is now written in the target, which expands when it runs; a test checks that the stamp is under `obj\`.
- `scripts\env.ps1` runs `vcvars64`, which puts `Platform=x64` in the environment, and MSBuild reads environment variables as properties: a throwaway build started from a test put its output in `bin\x64\` and the test, intermittently, found nothing. The tests' build helper strips `Platform` and `Configuration`. The script also avoids `Get-FileHash`: a Windows PowerShell 5.1 started by a build from PowerShell 7 inherits 7's module path and cannot load it.

**Tests** (`TmDq.App.Tests`, 35): the window's title, its version label laid out by the real theme, and its default and minimum sizes; the version, read from `constants.h` with the tests' own pattern, in both assemblies and in the exe's version resource; the manifest read back out of the built exe (the code page, Windows 10, and that it is the project's own file); the licence script against packages folders the tests write (each allowed licence, a proprietary and a copyleft id, `OR` / `AND` / parentheses, an unreviewed or altered licence file, a missing one, a nuget.org and another licence URL, an unknown type, no licence, a missing nuspec, every offender named, a missing assets file or packages folder, the app's real packages); and real builds with the SDK: a project with a fake commercial package fails, the same project with an MIT package succeeds, and a project that built clean fails once it gains the commercial package. `AppVersion.props` is built against three made-up headers, so each number is read from its own definition and a missing number or header stops the build.

**Mutation check:** 46 mutants of the licence script, its target, the version props and the app (the allow-list widened, each rule of an expression, each verdict, the stamp, the target's hook, condition, failure and output, each version property and check, each project's import, the manifest merge and its contents, the title, the label and the window's sizes). **43 caught by a test, 2 by the build itself** (counting a project reference as a package, and a wrong reviewed hash, each fail the test project's own build), **1 survived**: the guard that a `$Reviewed` entry's own licence is on the allow-list, which only an edit to the script's table can reach. The first run was spoiled twice and is not counted: C# Dev Kit's background builds broke half its builds, and a re-run in a worktree found the baseline itself red (a test read `TmDq.Core.Tests`' restore output, which a build of the app tests never produces; it now reads only what its own build restores). The runner now refuses to start on a red baseline.

Managed baseline **300 / 0 / 1**, unchanged. App baseline **35 / 0 / 0** (new). Qt and native baselines unchanged (no native or Qt code changed).

#### `TestAbi` moves to doctest, the last core suite to leave Qt Test (plan step 2.4 done)

`native/tmdq_core_tests/test_abi.cpp` holds all 48 cases of `TestAbi` as `Abi: <old name>`; the Qt version is deleted from `tests.pro`, `main.cpp` and the tree. The suite tests the C ABI, which is core, and used Qt only as its harness (`QtTest`, `QDir`, `QFileInfo`), so nothing in it needed a recorded oracle: every comparison was already with the core called directly. No product code changed. With it, **every core suite is ported or split, and step 2.4 is ticked.**

- **The ABI is compiled into the native test executable**, as it was into the Qt one: the project compiles the six `native/tmdq_core/*.cpp` sources with `TMDQ_ABI_STATIC` and includes `native/tmdq_core`, and the cases call `tmdq_*` exactly as a host would.
- **The guard moved with it.** `TestCoreQtFree::nativeProjectProblems` checked that `tests/tests.pro` compiled every ABI source; it now reads the native tests' project instead, and `theNativeProjectGuardSeesEveryKindOfDrift` builds that project in its temporary tree (a forgotten ABI source, an invented one, an empty project, and a test file that is not an ABI source and must not count as one). `CLAUDE.md` now says a new ABI source goes in `tmdq_core.vcxproj` and `tmdq_core_tests.vcxproj`.
- **QCOMPARE on doubles was fuzzy; the port compares exactly**, and every expected number still matched. Paths go through `std::filesystem` and `generic_u8string`; the recordings through `repoFile()`, which fails rather than skips when one is missing.

**Mutation check: the mutants the Qt suite was validated against in step 2.2, against the native `Abi` cases alone.** 307 distinct mutants of the ABI sources, the header, `calibrationrequest.cpp` and `calibrationextractor.cpp`; eight of step 2.2's had code that has moved since and were re-aimed where it lives now (the callbacks are in `abi_events.h`, the missed-frames view and the channel bound in `abi_run.cpp`, and the revision is 0 under version 2). **294 caught, 2 by the compiler, 11 survived.** Each survivor was compared with what the Qt suite did with it:
- **Six survived the Qt suite too**, so the port lost nothing on them: a freed result never released, the calibration's time channel not carried, the CSV writer's x count taken as the y count (it writes the shorter), the clock stamp's returned length, a format struct reporting a size that padding makes the same, and a time base's output not cleared on create.
- **One is equivalent now and was not then:** "the revision is not reported" returns 0, and the revision is 0.
- **Two belong to another suite:** they mutate `calibrationrequest.cpp`, which the Qt run caught through `TestCalibrationRequest`. That suite is native now; all 22 of that file's mutants, run against its cases, are caught.
- **Two were real gaps, both closed:**
  - **A series set that does not keep its stream's data alive.** The case freed the result and read the values, which caught the mutant under the Qt suite's Debug heap (it overwrites freed memory) and not under the native suite's Release one (it leaves the values there). The case now holds a `weak_ptr` to the data: alive after the result is freed, gone once the set is.
  - **A CSV column with times but no time array.** Only the value array had a refusal case; the Qt suite caught the mutant by crashing. It has its own case now.

The check's own five mutants of the moved guard (reading `tests.pro` again, the wrong folder, a forgotten or an invented source not reported, a test file counted as an ABI source) are all caught by `TestCoreQtFree`.

Qt baseline **457 / 0 / 0** (was 507 / 0 / 0: the suite's 50 results leave), **19 suites**, from a clean `tests/` rebuild (a header was deleted) with zero warnings. Native baseline **477 cases / 181,336 assertions** (1 skipped, the opt-in benchmark). Managed baseline unchanged (**300 / 0 / 1**).

#### Two dead constants go, and a test helper that was never called gets its case

Cleanup after the splits; no behaviour changed.

- **`PCMConstants::kSecondsPerMinute` and `UIConstants::kHexBase` are deleted.** The `TestConstants` mutation check found them: nothing in the app, the core, the tests or the C# library reads either (the one in use is `UIConstants::kSecondsPerMinute`). The app, the tests, the DLL and the native tests all build without them.
- **`loadWithoutFlagBoxesDigest` in `tst_framesyncfile.cpp` had been dead since it was written** (#177, `0b71c85`): defined, never called. It was meant to cover `loadFrameSyncFromToml` with its two optional boxes (Invert Data, Derandomize) left out, and both production callers pass them, so the two null checks in that function had no test. The new case `loadingWithoutTheFlagBoxesFillsTheEditorsTheSame` runs every recorded load row without the boxes and requires the editors filled exactly as the recorded row has them and the boxes left alone (at least three rows carry a flag that would otherwise have changed a box). **Mutation check:** removing either null check crashes the suite (access violation) in the new case; both survived before it.

Qt baseline **507 / 0 / 0** (was 506: one case), **20 suites**. Native baseline unchanged (**429 cases / 90,223 assertions**).

#### `TestConstants` is split: `constants.h` moves to doctest, `plotconstants.h` stays (plan step 2.4)

The fifth split, and one of the two core suites the 2.4 note had wrongly called "deliberately Qt" (the other, `TestAbi`, is the last suite of the step). `TestConstants` pinned two headers: `constants.h`, which the whole core includes and which has had no Qt since the de-Qt sweep, and `plotconstants.h`, which is the view's (`QColor`, `QString`). No product code changed.

**The native half** (`native/tmdq_core_tests/test_constants.cpp`, 40 cases): the 30 Qt cases on `constants.h` under their old names, the sync-pattern length relation moved out of the Qt `plotConstants` case, and 9 new cases. The Qt half keeps `plotConstants`, the one case on `plotconstants.h`.

**Mutation check: every constant in the header, in three rounds.**
- **Round 1, against the constants cases:** each of the header's 104 definitions altered (a number plus one, a string with a letter added, an array's first element), plus the version string's separator and its patch. **64 caught, 42 survived.** Three are the version numbers, left alone on purpose: the version case builds its expectation from them, and `check_release_consistency.ps1` pins the version across the shipped files.
- **Round 2, the other 39 against the whole native suite**, to see which some behaviour test already holds: **16 caught** (the receiver defaults by `FrameSetup`; the calibration tuning by the extractor and the step detector), **23 survived**.
- **Two of the 23 are dead constants**, used nowhere: `PCMConstants::kSecondsPerMinute` (UIConstants has its own) and `UIConstants::kHexBase`. Reported, not pinned; the entry above deletes them.
- **The other 21 are now pinned**, by the new cases. Where a shipped file carries the same value the case reads the file, so the two cannot drift apart unseen:
  - the Receiver SNR fallback constants (`FE6B2840`, 800 bits, the mask) are what `settings/framesync_patterns/framesync_rcvr_default.toml` says - the dialog falls back to them when the file is missing, so a difference would change the defaults silently - and the Frame Sync Lock default is what `framesync_PRN15.toml` says;
  - the receiver-parameter and step-calibration folders ship `default.toml` under the default name;
  - the full manual's name is the one `deploy/tmDataQualityAnalyzer.iss` installs;
  - the sidebar and legend settings keys (a renamed key forgets every user's choice), the status glyph colours, the progress maximum, the window and layout sizes, the byte units;
  - four calibration tuning values that survived a small change for reasons in the fixtures, not the constant (the start-up sample period is replaced once the bit rate is known, the relaxation search reaches its floor in four attempts, no relaxed channel needs less than 2 sigma, the sample floor binds only at periods coarser than any test uses), with the relations their comments state - including that the attempt cap lets the search get from 6 sigma down past the 1.5 floor.
- **Round 3, the 21 again plus 8 aimed at the new cases** (two tuning values that break a stated relation, five changes to the two shipped pattern files, the installer shipping the manual under another name): **29 of 29 caught.**

Qt baseline **506 / 0 / 0** (was 536 / 0 / 0: 30 cases leave), **20 suites**, from a clean `tests/` rebuild with zero warnings. Native baseline **429 cases / 90,223 assertions** (1 skipped, the opt-in benchmark).

#### The core is built and tested on a machine with no Qt at all (plan step 2.4)

Step 2.4 is done when "the core and its tests build and pass with no Qt installed, and CI runs them". Both build jobs have Qt on the machine (the self-hosted runner's kit; `install-qt-action` on hosted), so all they could show was that the core does not *reach* for it: no Qt include path, no Qt import.

**A third CI job, `core-without-qt`,** runs on every push and PR on a plain `windows-latest` runner whichever runner the Qt build picked. It never installs Qt, sets up only MSVC (`msvc-dev-cmd`) and .NET 10, and builds `tmdq_core.dll` (Debug and Release), the doctest suite and the managed library, and runs both test suites. Its first step **fails if it finds Qt**: `qmake`, `qmake6`, `qtpaths` or `windeployqt` on PATH, a `Qt6Core*.dll` in any PATH folder, a `QTDIR` / `QT_ROOT` / `QT_ROOT_DIR` / `QT_PLUGIN_PATH` / `Qt6_DIR` variable, or an install folder in the usual places, so the proof cannot quietly lapse if the image ever gains a Qt. The `ci` status now needs this job **and** the selected Qt build. The scripts dot-source `env.ps1` only when `msbuild` or `mt` is missing, so nothing in this job touches its Qt half.

**Its first run found a real problem, and not a Qt one.** The hosted image is now Visual Studio 2026 with **MSVC 14.51**, where `env.ps1` pins this machine to 14.44, and 14.51 rejects the vendored `doctest.h` with **C5285** ("cannot declare a specialization for `std::tuple`"): in its default mode doctest forward-declares a few `std` templates to save compile time, and the new compiler forbids that. The DLL built clean; the doctest suite stopped at the zero-warning gate. The `build-hosted` fallback would have failed the same way the next time CI routed to it. The fix is doctest's own switch, **`DOCTEST_CONFIG_USE_STD_HEADERS`**, set for the whole `tmdq_core_tests` project (the vendored header is not edited, and every translation unit must see the same doctest): it includes the standard headers instead. Reproduced locally with `TMDQ_VS_RANGE='[18.0,19.0)'` (C5285 without the switch), and with it both 14.51 and 14.44 build with **0 warnings** and give the same **389 cases / 90,104 assertions**.

The check was run locally both ways: on this machine it finds the Qt kit (2 traces in a plain shell, 12 after `env.ps1`), and with the Qt folder, variables and PATH entries hidden it passes. Running it first found one bug of its own: an unset `RUNNER_WORKSPACE` made the candidate `\Qt`, which resolves to the root of the current drive.

**2.4 is not ticked, and the note that said the core's suites were all ported was wrong.** It listed `TestConstants` and `TestAbi` as "deliberately Qt". `TestConstants` is mixed (the Qt-free `constants.h` beside the Qt `plotconstants.h`), so it is split like the four before it; `TestAbi` tests the C ABI, which is core, with Qt only as its harness, so it is ported. The plan note now says so. Qt on purpose: `TestStreamConfigSchema`, `TestProcessingTemplateSchema` (template JSON goes to C#, D3), `TestQtProcessingEvents` and `TestCoreQtFree`.

No product code changed; the baselines are unchanged.

#### `TestStreamJob` is split: its core half moves to doctest, the wrapper half stays (plan step 2.4)

The fourth and last of the suites that drove the core through a Qt object (#207 `28b8e5c`, #208 `af48cb9`, #209 `73719cc` before it); **every core suite is now either ported or deliberately Qt**. `TestStreamJob` pinned the job builder through `MainViewModel::buildStreamJob`, which converts the user's Qt settings (`QString`, `QHash`, the sync fields) into `StreamJobConfig`, reads the application root and byte order it holds, and turns the builder's report into a `QString` and signals. No core code changed; the project now also compiles `streamjobbuilder.cpp`.

**The native half** (`native/tmdq_core_tests/test_streamjob.cpp`, 4 cases): the builder is called with its own plain settings.
- **All 59 rows**, each lambda that adjusted a `StreamConfig` rewritten to adjust the `StreamJobConfig` (`c.sync.pattern` is `c.syncPattern`, `calibrationByWord.insert` is an assignment, a `QString` file name is a path); every recorded expectation is unchanged and matched at once. The recordings are the core's own words (the messages, the log with its em dash, the whole table).
- **The oracle is restated.** The old `MainViewModel::buildStreamJob` (over `QString` and `QFileInfo`) is restated over `std::string` and `std::filesystem`, with its one marked line (the fix for an appended default map), and compared over the same 1,500 generated configurations with the same reach thresholds. It shares with the builder the frame-sync decode, the acquisition flags and `FrameSetup`, which have their own suites. One generator detail: `QHash::insert` replaces a word's profile and `unordered_map::emplace` does not, so the generator assigns.
- **Two direct cases:** the default file read under a non-ASCII application root, and the error and log as plain UTF-8 text.

**The Qt half**, trimmed to 15 of the 59 rows, one for each thing the wrapper does; the generated comparison and the two direct cases left.

**Mutation check:** 102 mutants of `streamjobbuilder.cpp`, run against the `streamjob` cases alone: the stream's description in every message, each frame-sync error and its text, the frame-length bound and words, each acquisition flag, the job's parameters, every sample period, the default file (its path, its use, the missing-file and partial-map rules), the default map's arguments and failure, a named file's missing and failed-load cases and messages, the empty-table check, the scale and slope bounds and messages, the calibration arguments, and the profile and linear log lines. **101 caught, 1 equivalent, and one real gap closed:** the default file was loaded with a word count one too high and nothing noticed, because no default file had a word at the frame's edge (50 words in an 800-bit frame is outside it, in an 801-bit one inside). Two rows now pin both sides. The equivalent one loads the default file without first checking that it exists: a missing file fails to load and falls back the same way.

Qt baseline **536 / 0 / 0** (was 583 / 0 / 0: 44 rows and 3 cases leave), **536 / 0 / 0** at release. **20 suites.** Native baseline **389 cases / 90,104 assertions** (1 skipped, the opt-in benchmark). The DLL and managed builds were not rerun (no code changed).

#### `TestSeriesBuild` is split: its core half moves to doctest, the plot half stays (plan step 2.4)

The third of the four suites that drive the core through a Qt object (#207 `28b8e5c`, #208 `af48cb9`). `TestSeriesBuild` pinned the series logic through `PlotViewModel`, whose data half is orchestration over three core functions (`buildStreamSeries`, `SeriesTimeBase`, `assignImportedStreamOrders`): drop a stream's earlier series, judge overlap, move the base, build, sort, range, remove a source and move the base back, take an import in. No core code changed; the project needed no new source (`seriesbuilder.cpp` came with `TestCsvSeries`).

**The native half** (`native/tmdq_core_tests/test_seriesbuild.cpp`, 5 cases).
- **A host over std types.** `Plot` does what the plot does with those functions, in about 150 lines with no Qt: the stream-key dedupe, the overlap warning, adopt-and-shift, build, the stable sort by job, the span, removal with advance-and-shift, clear, and an import (parse, set the clock, assign orders). All 77 scenarios run on it with their recorded expectations, unchanged. The scenarios pin the core's answers in the plot's sequence; the host's own wiring is what a Qt scenario still checks.
- **The oracle is restated, not recorded.** The Qt suite compared the plot with the old data code over 700 generated add/remove/clear sequences, kept as a class over `QVector` / `QString` / `QMap`. That class is restated over std types (it builds each series itself, moves the base with its own arithmetic, reads the clock with `gmtime_s`, and shares with the core only the column schema's naming) and compared with `Plot` the same way, whole state after every step, with the same generator and thresholds. The import ordering is compared with its old `QMap` code over 4,000 generated lists. **No digest was needed, and both matched at once** except for one slip of mine: `QMap::insert` replaces an existing key and `std::map::insert` does not, so the first port of the ordering oracle disagreed with the core until it assigned instead.
- **Direct:** a spec views the processed data without copying it, and `SeriesTimeBase`'s adopt / overlaps / advance / setClock / reset.

**The Qt half**, trimmed to 16 of the 77 scenarios, one for each thing the plot does itself. The generated sequences, the ordering comparison and the two direct cases left.

**Mutation check:** 113 mutants of `seriesbuilder.cpp`, run against the `seriesbuild` cases alone: the cached range, the stream key, the elapsed axis and every field a series carries, the lock and missed-frames series (their conditions, names, types and values), the receiver channels (the name, the `_RCVR` search and offset, a refused number, the per-receiver count), each step of the time base (reset, clock, overlap at each boundary, adopt, advance) and of the clock (day, hours, minutes, seconds, fraction), and the import ordering (registration, sources, fresh orders). **108 caught, 5 equivalent, and no new case was needed**: the 77 scenarios and the generated sequences caught everything a mutant could change. The five equivalent: `std::max` is the range's own `(a < b) ? b : a`; a refused receiver number leaves its output at 0 either way (the number reader leaves `out` alone on a refusal, which `csvseries` pins); and re-basing or advancing on a zero shift, and advancing with no earliest sample (`value_or(0)`), all return 0 and recompute the same clock.

Qt baseline **583 / 0 / 0** (was 648 / 0 / 0: 61 scenarios and 4 cases leave), **583 / 0 / 0** at release. **20 suites.** Native baseline **385 cases / 88,483 assertions** (1 skipped, the opt-in benchmark). The DLL and managed builds were not rerun (no code changed).

#### `TestCsvSeries` is split: its core half moves to doctest, the plot half stays (plan step 2.4)

The second of the four suites that drive the core through a Qt object (#207 `28b8e5c` was the first, and the rule: **split them**). `TestCsvSeries` read the CSV format through `PlotViewModel` (`loadCsvFile`, `exportCsv`), so an expectation was the core's answer as the plot stored it. The native half calls `CsvSeriesParser::parse`, `CsvSeriesWriter::write` and `clocktime::format` directly; the trimmed Qt suite keeps what the plot adds. The project now also compiles `clocktime.cpp`, `csvseriesparser.cpp`, `csvserieswriter.cpp` and `seriesbuilder.cpp`. The only core change is a comment in `textparse.h` that named the Qt case.

**The native half** (`native/tmdq_core_tests/test_csvseries.cpp`, 13 cases).
- **All the rows.** The 64 import rows and 17 export rows move with their recorded expectations, plus two new import rows. An import row's `order=` is what the plot shows, which is the parser's result run through `assignImportedStreamOrders` (the parser leaves a frame-sync series with no order), so the helper applies it, as the plot does on commit. An export row goes through what a host does between a processed stream and the writer: the time base is the earliest first sample (`SeriesTimeBase`), each stream becomes its series on that base (`buildStreamSeries`), and the writer gets a column per series. No row adds a source that starts earlier than one already there, so the helper requires that the base never moves.
- **The oracles are digests.** The Qt suite compared the core with the QString / QTextStream / QMap code it replaced over generated input, and the number reader with `QString::toDouble` / `toInt`. The doctest binary has no Qt, so what each answered was recorded once, with a throwaway diagnostic in the Qt suite (removed again), as one FNV-1a digest per comparison plus the counters the reach checks read: 400 generated files (350 loaded, 50 failed, with BOMs, UTF-16, skipped rows, SNR and lock series), 500 generated series sets (346 written, 154 declined, ties, non-finite values, sparse cells, over 400 bytes in 202), 8,000 clock stamps in both forms, 20,095 number texts (double and int), and one 1.46 MB export. Every generator is the Qt suite's, token for token, on the same seeds. **Every digest matched on the first run.**
- **Beside the digests:** a UTF-16 file with CR LF line ends; an export replacing the file that was there; a non-ASCII folder; writing nothing writes no file and an unwritable path fails; export, import and export again byte for byte; and, new, a column whose times and values differ in number writes the shorter (the Qt suite's columns always came from one series).

**The Qt half**, trimmed: 10 of the 64 import rows and 9 of the 17 export rows (the load warning's count, the clock taken from the first row, the order each stream is given, every series, one source's, none), and the four plot-level cases (a non-ASCII folder, an export of nothing, an unwritable path, the round trip). The two directions of the format, the clock stamp and the number reader left; their 54 + 8 rows and 7 cases are native now.

**Mutation check:** 239 mutants over `csvseriesparser.cpp`, `csvserieswriter.cpp`, `clocktime.cpp` and the two number readers in `textparse.h`, run against the `csvseries` cases alone: the cached range, the byte-order marks and both line sources, the splitter, the header checks, each series' identity, the channel counting, every data-row rule (short and blank rows, the base, the elapsed time, each cell), the span, the stamp; the writer's open mode, header, sample selection, sort, rounding, row assembly, flush and result; each step of the clock stamp; each rule of both number readers. **215 caught, 24 equivalent.** Four were real gaps, each closed by a new case:
- **A look-alike of the UTF-8 mark** (`EF BB 20`, not `EF BB BF`) was skipped as a mark by a check that looked at two of its three bytes, and the file then loaded. It is a row now (the file must fail).
- **A UTF-16 file whose last line is one character long**, with no line end, was dropped by a one-off end test. It is a row now (the line is a short row, counted as skipped).
- **A refused number overwrote its output.** `parseDouble` and `parseInteger` promise to leave `out` untouched on failure; nothing checked it. Every text in the sweep now starts from a sentinel and a refusal must leave it.
- **Two of my mutants were wrong** (one compared a string with `.size()` and did not compile, one could never differ) and were replaced by ones that can: the base reset on every row, and the time header compared by length only. Both caught.

**24 equivalent**, each reasoned:
- **A second guard stops the same case:** a file that cannot be opened is refused anyway at the header read (twice), an empty first line is one column, a time column alone has no data, and a writer that carries on past an unopened file reports false at the end.
- **Another call decides:** `from_chars` rejects a leading `+` and an empty string, so the `++5` and empty checks before it only choose which refusal.
- **Capacity only:** the reserved row estimate, the writer's reserve, and the flush size and cadence (the bytes written are the same; the large export checks them).
- **Same behaviour:** `std::max` is the cached range's own `(a < b) ? b : a`; a 2-byte file is neither a UTF-16 mark nor a header; a stable sort on time alone keeps the columns in order; `std::ofstream` truncates by default; `ms > 0` for `ms >= 0` (the negative branch gives 0 for 0); and `std::llround` for the writer's rounding, which differs only for a value of exactly 0.49999999999999994 ms, which no time can produce (checked: no double times 1000 gives it).
- **Needs an I/O error:** the flush and the final success flag (a full disk).

Qt baseline **648 / 0 / 0** (was 717 / 0 / 0: 54 + 8 rows and 7 cases leave), **648 / 0 / 0** at release. **20 suites.** Native baseline **380 cases / 67,659 assertions** (1 skipped, the opt-in benchmark). The DLL and managed builds were not rerun (no code changed beyond a comment).

#### `TestFrameSyncFile` is split: its core half moves to doctest, the dialog half stays (plan step 2.4)

Eighteenth suite touched, and the first of the four that drive the core through a Qt object. `TestFrameSyncFile` read the pattern-file format through the dialog's own `loadFrameSyncFromToml` / `saveFrameSyncToToml` and `QLineEdit` / `QSpinBox` / `QCheckBox`, so each expected value was what the dialog does with the file (a pattern upper-cased, a length clamped to the spin box, a missing `Randomized` key leaving the box alone), not what the core says. The rule for these four: **split them.** The core's cases move to native with expected values worked out for the core; the Qt-bound cases stay in a trimmed Qt suite until the Avalonia switch-over. No core code changed.

**The native half.** `native/tmdq_core_tests/test_framesyncfile.cpp` (9 cases) calls `readFrameSyncFile` and `writeFrameSyncFile` directly. The project now also compiles `framesyncfile.cpp`.
- **What each file says.** The Qt suite's 57 load rows are all here, each restated as what the core returns: the pattern and mask as written (not upper-cased), the length only if usable and **not clamped** (70,000 reads 70,000; `WordsInMinorFrame = 100000` reads 1,600,000, the dialog's 65,536 being its spin box's), whether it came from the legacy key, Invert Data, and `Randomized` as present-true, present-false or absent. Every expected value was worked out by hand from the rows and matched on the first run.
- **What writing puts in the file.** The 15 save rows as exact bytes (CRLF, quoting, a newline inside a value, non-ASCII passed through), plus: writing replaces the whole file and carries no Data Rate or Sample Rate; a folder with a non-ASCII name; a round trip for every combination of the flags and four lengths; a length the reader will not use is still written.
- **The oracle** is the load and save code the core replaced, restated over `TomlDocument` and std types (the Qt suite kept it over `QString` and widgets), run beside the core over 300 generated files and 300 generated saves. The generator is the Qt suite's, state for state; the reach thresholds are the same, with `Randomized` present-true, present-false and absent each asserted reached.

**The Qt half**, trimmed. The dialog-level behaviour stays: 23 of the 57 load rows (the upper-casing, the spin box's range, a field left alone, Invert Data cleared when absent, Derandomize left alone when absent), 6 of the 15 save rows (the widgets' values reaching the file, no case-folding, no trimming, non-ASCII through the `QString`), the 16 Receiver SNR defaults rows (`loadReceiverFrameDefaults` is the view's), the two generated comparisons of the dialog with its old code, and the dialog round trip. The two direct core cases (`theReaderReportsWhatAFileSaysAndNothingElse`, `writingToAnUnwritablePathFails`) and the 34 + 9 rows that only restated the file format left; every one of them is a native row now.

**Mutation check:** 66 mutants of `framesyncfile.cpp`: each of the six keys and the section renamed; each field read from another's key or never read; every fallback value; the legacy key tried when the length equals the minimum, only for zero, only for non-positive, always, never; the legacy word threshold at 4, 5 and 20 words; the word width; the legacy marker never set, kept, or set when no length results; the length threshold at the minimum, above it, positive-only, doubled, always, never; `Inverted` defaulted on, negated, never read, read from the randomized key; `Randomized` reported when absent, never, from the inverted key, negated, always false; the file not loaded; every value of the writer dropped, swapped, offset, clamped, upper-cased or trimmed; and the save reported always true, always false, never made, or made to another name. **65 caught, 1 equivalent.** The equivalent one defaults `Randomized` to on: `TomlDocument::boolean` uses its fallback only when the key is absent, and the call sits behind `contains()`. No gap needed a new case: the generated comparisons and the 57 + 15 tables caught every mutant at once, which is what porting the rows first was for.

Qt baseline **717 / 0 / 0** (was 762 / 0 / 0: 2 direct cases, 34 load rows and 9 save rows leave), **717 / 0 / 0** at release. **20 suites** (the suite is trimmed, not deleted). Native baseline **367 cases / 66,562 assertions** (1 skipped, the opt-in benchmark). Managed baseline unchanged.

#### `TestSeriesColumnSchema` moves to doctest (plan step 2.4)

Seventeenth suite ported, the third of "the suites for the code moved in step 1". `native/tmdq_core_tests/test_seriescolumnschema.cpp` holds all 22 cases of `TestSeriesColumnSchema` (its two data-driven cases are tables walked in a loop) and 1 new one; the Qt version is deleted. The project now also compiles `seriescolumnschema.cpp`. No core code changed.

**The oracle problem, solved the way `TestTextFormat` and `TestFrameSync` solved it.** The Qt suite kept the `QString` implementation the schema replaced and compared the Qt-free parser with it over 40,000 generated headers. The doctest binary has no Qt, so what that implementation answered was recorded once, with a throwaway diagnostic in the Qt suite (removed again): one FNV-1a digest over its answer for every header (metric, name, label, order, receiver, source), and the eight counters the reach check reads. The generator is the Qt suite's token for token and on the same `mt19937` seed, so it makes the same headers; the digest matched on the first run, which is the port's proof. The formatters have no parsing in them, so for those a second way of writing the grammar (`std::string` concatenation, sharing nothing with the schema) stands in for the old implementation over every combination of name, metric and source id, and every id and channel name.

**Mutation check:** 69 mutants of `seriescolumnschema.cpp`: the separators and tokens (each one altered), the digit test (each end, and one character past each), the SNR-shape test, every step of the source qualifier (its lead, digits, separator, number, remainder), each part of `snrSeriesName`, each suffix and the qualifier in `columnHeader`, and every step of `parseColumnHeader` (the order of the SNR test and the suffix tests, the names and labels each branch keeps, the dash, the id, the split at the last space, the receiver token and number). The ported cases caught **62; 7 survived**. Two were real gaps, closed with parse rows:
- **A receiver token without its underscore** (`5 - A LRCVR3`) read the receiver number as the real token would. Nothing generated or recorded had `RCVR` without `_`.
- **The characters either side of `'0'..'9'`.** A digit test that also took `:` read `5: - A Lock (%)` as an SNR header; nothing had a digit-adjacent character that is not a digit.

The five rows added are worked out from the code and not recorded from Qt. **64 caught; 5 equivalent:** the digit scan stopping one character early (a separator cannot follow a digit that is the last character); `header[0]` read on an empty `std::string` (well-defined, a `'\0'`); a qualifier with no digits accepted (the number parse of nothing fails and returns the same); and the id and receiver number keeping a partial value on a refused parse (a refused parse leaves its output alone, which `textparse` pins).

Qt baseline **762 / 0 / 0** (was 827 / 0 / 0: the suite's 65 results leave, data rows counted each), **762 / 0 / 0** at release. **20 suites.** Native baseline **358 cases / 64,111 assertions** (1 skipped, the opt-in benchmark). Managed baseline unchanged.

#### `TestCalibrationRequest` moves to doctest (plan step 2.4)

Sixteenth suite ported, the second of "the suites for the code moved in step 1". `native/tmdq_core_tests/test_calibrationrequest.cpp` holds the 6 cases of `TestCalibrationRequest` and 2 new ones; the Qt version is deleted. The project now also compiles `calibrationrequest.cpp`. No core code changed.

**The oracle is the same rule over standard types.** The Qt suite kept the dialog's old inline code (`QString`, `QFileInfo`) beside `buildCalibrationRequest` and compared the two over 3,000 generated inputs. The native test keeps that comparison with the old code rewritten over `std::string` and `std::filesystem`, with the same reach thresholds (over 300 accepted, over 300 refused, over 30 using the default word map). Both sides share `resolveFrameSync`, as they did in the Qt suite.

**New cases:** an application root whose name has non-ASCII text in it still finds `default.toml`, and one typical request is read out in full as text, so a field carried to the wrong place shows by its value.

**Mutation check:** 40 mutants of `calibrationrequest.cpp`: each of the 14 carried fields dropped, swapped or inverted (the two channel ids, the receiver count and channels, the clips, randomized and inverted, the byte order), each refusal (accepted instead, the wrong field named in the message, the text altered), the word map (not carried, a named one overridden by the default, the default never looked for, looked for in the wrong folder or under the wrong name, used without checking it exists, used only if it does not), the application root ignored, and the request never delivered, delivered on a refusal, or a success not reported. **39 caught, 1 equivalent.** One real gap: the "failed build leaves the request alone" case refused only for the pattern, never for the mask, so a refusal on the mask path that still delivered the request was not seen. Both are now checked. The equivalent one builds the default path with `std::filesystem::path(std::string)` instead of `pathFromUtf8`: the test executable runs in the UTF-8 code page (the same manifest the application has), where the two give the same path.

Qt baseline **827 / 0 / 0** (was 835 / 0 / 0: the suite's 8 results leave), **827 / 0 / 0** at release. **21 suites.** Native baseline **335 cases / 62,924 assertions** (1 skipped, the opt-in benchmark). Managed baseline unchanged.

#### `TestTemplateMatcher` moves to doctest (plan step 2.4)

Fifteenth suite ported, and the first of "the suites for the code moved in step 1". `native/tmdq_core_tests/test_templatematcher.cpp` holds 13 cases; the Qt version is deleted. The project now also compiles `templatematcher.cpp`. No core code changed.

**The Qt suite tested two things at once.** It read the matcher through `BatchController::matchFile`, which is the ViewModel's (it reads the ids out of a `ProcessingTemplate` and a reader's channel list) and so cannot move to the native project. The ported cases call `TemplateMatcher::match` directly on two id lists. The two cases that were about the wiring itself (every template entry and every file channel counts; a channel's name plays no part) are now `TestBatchController::matchFileComparesChannelIdsAndNothingElse`.

**The oracle changed.** The Qt suite compared the matcher with the `QSet` implementation it replaced. The native one compares it with the same rule written another way: element by element over `std::set`, sharing nothing with the matcher's sort / unique / set_difference. 4,000 generated id-list pairs, each outcome (same, only missing, only extra, both, repeats) asserted reached in volume. One new check: repeats that are not next to each other (`9, 7, 9, 8, 9`), which a "collapse neighbours only" unique would leave in.

**Mutation check:** 20 mutants, **all caught, none equivalent**. 14 of the matcher: the sort dropped or reversed, repeats not collapsed (or `unique` run without the erase), missing and extra swapped or dropped, extra as the symmetric difference, `ok` ignoring either side or true when either is empty or never set, the template and the file lists read as each other. 6 of the wiring: template ids off by one or only the first entry, file ids off by one or only the first channel, file ids read from the template, the name mixed into the id. The matcher's own mutants run in the native suite; the wiring ones run in `TestBatchController`, where they are caught by the new case as well as by `validateComparesEachFilesChannelsToTheTemplate`, which skips when the PRN sample is missing.

Qt baseline **835 / 0 / 0** (was 850 / 0 / 0: the suite's 16 results leave, the wiring case adds one), **835 / 0 / 0** at release. **22 suites.** Native baseline **327 cases / 56,853 assertions** (1 skipped, the opt-in benchmark). Managed baseline unchanged.

#### `TestCalibrationExtractor` moves to doctest, and the report and lifecycle finally have tests of their own (plan step 2.4)

Fourteenth suite ported. `native/tmdq_core_tests/test_calibrationextractor.cpp` holds all 25 cases of `TestCalibrationExtractor` (its two data-driven cases are loops); the Qt version is deleted. No core code changed, and the project needed no new source: `calibrationextractor.cpp` was already compiled for the frame-processor suite.

**The event loop is gone.** The Qt suite received the extractor's reports through `QtProcessingEvents` on its own thread's event loop. The native `Recorder` is the sink itself: reports arrive on the extractor's threads (and `start()`'s own failures on the caller's), so it records them under a lock and a condition variable replaces the nested loop. Two consequences:
- The end really is last, so the old wait for the reader's final progress report is gone.
- "Cancel at the first report" used to mean the first one to reach the loop, which was always mid-extraction. It now means the first report **off the calling thread**; cancelling inside `start()` is its own case (below).

**The mutation check found the report and the lifecycle far less covered than the suite looked.** 190 mutants of `calibrationextractor.cpp`: the report's grouping, counting and every line of its text; `start()`'s guards, resets, window sizing and word-map checks; `cancel()`, the driver and the end; the clips and the per-channel notes; and the word-map builder. The ported cases caught **121; 69 survived**. They were written around the scenarios that once failed in the field and say little about the rules between them.

**New cases close the gaps.** Seven more report scenarios, each worked out by hand:
- the lead-in boundary (two levels over the step file warns; one does not) and the MOST levels any channel of a receiver held;
- the HIGHER of two railed tops, and an inverted receiver, whose highest dB is its first profile point;
- a flat channel that reports no level, and an unnamed flat channel beside a calibrated one (the word-map hint is about numbered receivers only);
- a plateau count of zero, and a count with no data to put against the steps.

And in the extraction itself:
- **The clips are real.** A start or an end alone that takes the whole recording is reported and ignored (the report names the total typed); 20 s off the start leaves only receiver 1 calibrated and 20 s off the end none; the recording's length is the recording's (65.4 s of 6,539,016 bytes at 800 kbit/s) whatever the clips.
- **The whole extraction against recorded numbers.** All 48 channels of step_cal, unclipped and clipped (outcome, plateaus, points, edge threshold, profile sum). The Qt suite pinned twelve calibrated channels and nothing about the other thirty-six, or about any number.
- **Every per-channel log note**, checked per channel: the MISMATCH marker present exactly when the plateaus differ from the steps, a failed channel's outcome and threshold, a railed channel's partial calibration, a relaxed threshold (a four-step request provokes it), and none on an ordinary success. Also the tag on the worker's and the reader's lines.
- **The window is sized from the frame rate.** Ten frames, between 100 and 200 ms: 800 bits at 0.8 Mbps and at 0.1 Mbps (12.5 frames, printed as 13), 8000 bits at 0.8, 0.5, 0.4 and 0.2 Mbps; and a recording at 40 kbit/s is measured as 1,308 s.
- **Cancelling.** From the very first report, before the worker exists; with the reader parked and the worker waiting on an empty queue (the cancel has to wake it, and the reader sees it too); and after the worker finished but before it said so (the run is still a cancel). A destroyed running extractor was cancelled, not waited out.
- **A clean start.** One extractor through a run with ignored clips, an ordinary run, a run that fails before it starts, a good one and a cancelled one: nothing of one is left in the next (flag, length, error, results, cancel).
- **The word map.** 49 channels fit an 800-bit frame and 50 do not; a word just past the frame is refused; the default grid names the words `L/R/C_RCVR<n>` in order.
- `running()` is already false while the end is reported.

Two re-runs: **166 caught, 24 equivalent.** Each of the 24 survived the new cases too:
- **Five redundant ways of stopping a worker.** `cancel()` aborts the reader and the worker and closes the queue, and the driver aborts the reader and closes the queue again, so removing any one of these changes only how soon the run ends. (Closing the queue in `cancel()` is the exception, and is caught by the parked-reader case.)
- **Three cancel guards** reachable only when a cancel races a `start()`: the early return when nothing is running, and the null checks on the reader and the queue.
- **Nine values overwritten before they are used**: the worker flag and the summary a run resets (the end overwrites both), the two clips clamped (the sample clamp downstream already bounds them), the parameters' file name and time channel (`prepare()` reads them from the request), the window set a second time in the parameters, and a frame setup's sums and profile (a fresh object, and the worker resets them).
- **Six that no recording here can reach**: the default window when a recording states no rate (every one does), the window guard's `&&` for `||` (both operands are true together), the generic failure text (a failed worker always has a reason), a channel with no samples, an over-aggressive clip with neither clip set, and the top dB of a railed channel read from its last point (the committed railed receiver is not inverted).
- **A redundant ternary** around `channelPartOfName`, which already returns the whole name when there is no receiver number.

**No defect found.** Unlike the previous ports, this one turned up nothing in the core; what it found was test gaps.

**Harness note.** The runner now retries `C1083` (a compiler file the tracker could not open) like the other file-lock flakes; a `FileTracker` error on one mutant was also a flake and was re-run. The 190 mutants run for about three hours, because the full suite is real extractions, so the report mutants run against the three fast report cases alone (50 of them, under half an hour).

Qt baseline **850 / 0 / 0** (was 893 / 0 / 0: the suite's 43 results leave), **850 / 0 / 0** at release. **23 suites.** Native baseline **314 cases / 44,811 assertions** (1 skipped, the opt-in benchmark). Managed baseline unchanged.

#### `TestPipelineRunner` moves to doctest, and the throughput guard moves with it (plan step 2.4)

Thirteenth suite ported. `native/tmdq_core_tests/test_pipelinerunner.cpp` holds all 15 cases of `TestPipelineRunner`; the Qt version is deleted. The project now also compiles `pipelinerunner.cpp`. The runner's friend, which the Qt suite used to reach one stream's processor, is now `friend struct PipelineRunnerTestAccess` (core change, test-only). One ordering fix in the port: the helper that releases a parked thread is declared after the runner, so on a failed check it releases before the runner's destructor joins that thread. The Qt order would have hung there.

**The throughput benchmark moves with it, and the Qt suite loses its one standing skip.** It was `TestProcessingCoordinator::benchmarkHeavyWorkloadSingleVsMultiStream`; it measures `PipelineRunner`, which has no Qt, so it is now the native case "PipelineRunner throughput: four streams on one recording cost about what the slowest costs". On the full recording it reproduces the recorded numbers: **1.01x the slowest solo and 0.64x all-sequential** (limits 1.50 and 0.85).
- **Opt-in, and unable to skip once asked for.** Without `TMDQA_RUN_HEAVY_BENCH` the case is skipped and listed as skipped. With it, a recording that cannot satisfy it (the committed sample holds 2 of the 4 channels), a timeout or a failed run is a **failure**, where the Qt version called each a skip.
- **`scripts\run_throughput_bench.ps1` no longer builds Qt.** It builds the native tests in Release and runs them all, then runs that one case pinned to the performance cores. It fails unless doctest says exactly that case ran and passed (a case that did not run reads `0 passed`, and the script says why), and still fails on "NOT GUARDED" (too few cores). A full run takes about 100 s. The full Qt suite is now a separate release step; the `cut-release` skill says so.
- **Both CI skip gates now expect no skips** (they expected exactly the benchmark). `build-and-test` and the root `CLAUDE.md` no longer describe a standing skip.
- `TestProcessingCoordinator` keeps its other throughput test, a diagnostic that reports and asserts nothing. The PRN helpers, thresholds and the benchmark left with the guard.

**Mutation check:** 58 mutants of `pipelinerunner.cpp` (the relay, every step of `start()` and its failure path, `cancel()`, the driver, each stream's thread and the end), then 5 of the guard's limits and checks:
- the relay routing a call to another method, dropping it or forwarding the end; the busy and empty-job refusals and their messages; the previous run's driver, flags, queues and workers; the reader, queues and workers wired and cleaned up; `prepare` fed the wrong job;
- cancel setting its flag and reaching the reader, every worker and every queue; the streams joined; each stream closing its own queue; a result gated on success and on no cancel, stamped with its job index and source; the end reported once, last, with the right verdict and with `running()` already false.

The ported 15 cases caught most; four real gaps were closed:
- **Competing consumers.** Several identical streams only had their sample counts compared, so one queue shared between them (each taking a share of the packets) passed. Their results are now compared whole.
- **Which job names the recording.** Every test used identical jobs. The first job's recording and time channel are the ones used (new case, both directions).
- **Cancel reaching every queue.** The parked-reader case now has two workers and waits for both, and checks the reader saw the cancel.
- **A stale success flag.** A stream that fails *after* another delivered must not undo the run's success. Which finishes first is up to the scheduler, so the failing stream is parked on its report until the good one has delivered.

Two cases are new for what no mutant needed but the header promises: nothing of one run is left over in the next (success, failure and cancel in turn), and a run lets go of its frame setups however it ends. **52 caught; 6 left, all equivalent:**
- the stale queue, worker and reader pointers kept after a run: `finish()` and the failed-start path clear them, and the start clears them again, so removing any one is covered by another and costs only memory until then;
- `cancel()` aborting only the first worker: every queue is still closed and the reader still posts its end markers, so every worker ends anyway, only later;
- the null check on the reader in `cancel()`, which matters only when a cancel races a start that is failing, a window no test can hold open.

The throughput guard's five mutants (each limit tightened, each check inverted, the sequential sum replaced by the maximum) are all caught: the real benchmark fails each time.

**Harness traps, both of them mine.** The mutation runner's retry for `LNK1201`, a PDB write flake that hit in streaks, deleted the *compile* PDB too, which left every already-built irig106 object pointing at a PDB that no longer existed and made the next build report 13 `LNK4099` warnings (the baseline gate refused to start, correctly). It now deletes only the link PDB. A clean rebuild clears it. Separately, a stray carriage return from a mangled backslash in a skill file made git treat the whole file as binary-ish and show every line changed; a diff much larger than the edit is the symptom.

Qt baseline **893 / 0 / 0** (was 910 / 0 / 1: the suite's 17 results and the benchmark's skip leave), **893 / 0 / 0** at release too: there is nothing left to skip. **24 suites.** Native baseline **277 cases / 43,590 assertions** (1 skipped, the benchmark). Managed baseline unchanged.

#### `TestFrameProcessor` moves to doctest, and the windowing arithmetic finally has tests (plan step 2.4)

Twelfth suite ported. `native/tmdq_core_tests/test_frameprocessor.cpp` holds all 22 cases of `TestFrameProcessor`; the Qt version is deleted. The data-driven cases became loops, the three invalid-input cases one loop. `QtConcurrent` became a `std::thread`, and the calibration round trip waits on `CalibrationExtractor::wait()` instead of a `QEventLoop`. The project now also compiles `frameprocessor.cpp` and `calibrationextractor.cpp`. Every recording these cases read is committed, so a missing one fails rather than skips.

**The test executable now runs in the UTF-8 code page.** The non-ASCII path case needs it: both readers pass irig106 the path in the process code page. `tmdq_core_tests.vcxproj` merges `resources/utf8codepage.manifest`, the same manifest the application uses. The case first asserts `GetACP() == 65001`, so a lost manifest fails loudly instead of skipping the check.

**The one core change is test-only.** `friend class TestFrameProcessor` (the Qt suite reached `derandomizeBitstream` through it) is now `friend struct FrameProcessorTestAccess`.

**The mutation check found the processor's arithmetic almost untested.** 129 mutants of `frameprocessor.cpp`, covering:
- the descrambler, inversion and byte swap;
- the run's setup: the result fields, the sum reset, the minimum syncs and the window bounds;
- both clocks and their seeding; the abort checks;
- window closing, the flush and the lock percentage;
- gap extrapolation (the operator's rate, the bit-period fallback, the lock drop);
- the frame counter's overrun; the sync match and the boundary test;
- extraction and word collection;
- the enabled words, the tag and the sync spec; the reports;
- calibration and the reset of each window's sum.

The ported cases caught **69; 60 survived**. They check that results have the right shape: vector sizes, counts that never decrease, a value that doubles with the slope. Almost nothing checked a value. The windowing, the gaps, the lock percentages, the minimum-sync rule, the offset, disabled words, inversion and the descrambler's state across packets could all change unnoticed.

**New cases close the gaps.** They run short synthetic streams timed at one second per bit, with every expected value worked out by hand (and each matched the implementation on its first run):
- **Windows, on both clocks:** a bit on a window's end belongs to that window, and each window averages its own frames. Times, values, lock percent and missed frames are all exact, and the completion line counts every window. The IRIG run uses 7-bit packets, so packet edges cross every frame and window boundary.
- **A bounded window:** a frame's sync lies on each end, and both count; nothing outside the window counts, and no window closes after the stop. On the IRIG clock a recording that starts before the window starts its windows at the window's start.
- **A lost lock:** missed frames are counted bit by bit and never extrapolated as well. A window with bits but no frame reads 0, not the average of nothing.
- **Unsynced bits:** one missed frame per frame length, counted only inside the window.
- **A recording gap:** the missed frames are extrapolated at the bit period's rate, and an operator's data rate wins over it. The gap drops lock.
- **The minimum syncs:** at 2 and at 3, after two overlapping syncs part-way through a word.
- **Channels:** only enabled words become channels, calibrated as `(raw + offset) x slope`. A word outside the frame stays 0, a lock-only stream has no channels even with a word map, and a sum left from an earlier run does not leak in.
- **The descrambler:** checked against an independently written IRIG 106 randomizer, run in pieces of every size and from a wrong starting state (it resynchronizes after 15 bits). A randomized stream decodes across packet boundaries.
- **Smaller cases:** an inverted stream; a packet with no payload; aborting before the first packet; the elapsed time printed in seconds and no longer than the run took.
- **Three new message rows:** a window bounded only at its stop, a pattern with a leading zero not matched before four bits have arrived, and four words too many for the frame extracting nothing.

Two re-runs: **127 caught, 2 left.** Neither is a gap the tests could close:
- **The abort check inside the bit loop:** with the check removed, a run aborted mid-packet still ends at the next packet's check. The only difference is how quickly the abort lands, which no test can observe without a hook inside the scan.
- **The "bounded" test that skips the start:** a window bounded only at its START prints the unset stop's raw value (`processing (1s to 18446744073709551615s)...`). That text is left unpinned rather than locked in.

**One behaviour noted and not changed:** a final window that had bits but no frame is not recorded, while the same window mid-run is (with lock 0 and value 0). So a stream that ends out of lock loses its last sample.

Qt baseline **910 / 0 / 1** (was 944 / 0 / 1: the suite's 34 results leave), **911 / 0 / 0** at release, **25 suites**, from a clean build of `tests/` (a header was deleted) with zero warnings. Native baseline **258 cases / 43,338 assertions**. Managed baseline unchanged.

#### `TestChapter10Reader` and `TestChannelData` move to doctest (plan step 2.4)

Tenth and eleventh suites ported, together, because `ChannelData` is the value the reader stores per channel. `native/tmdq_core_tests/test_chapter10reader.cpp` holds all 20 cases of `TestChapter10Reader` and all 10 of `TestChannelData`, the ChannelData ones folded into 5. Both Qt suites are deleted. The Qt suite's data-driven cases became loops. The TMATS edits are the same as before, same-length so no packet length or checksum moves. The project already compiled `chapter10reader.cpp` and `channeldata.cpp` for the packet-reader suite.

**The one core change is test-only.** The Qt suite read the reader's PCM selection, which no production caller reads back, as `friend class TestChapter10Reader`. That friend is now `friend struct Chapter10ReaderTestAccess`, defined in the native test.

**One new TMATS row:** the PCM channel retyped as `11` makes a second time channel. Both are listed in id order, and the default selection is the FIRST: every committed recording has one time channel, so nothing showed that before. A `ChannelData` copy assignment is checked too, beside the copy and the move.

**Mutation check:** 44 mutants of `chapter10reader.cpp` and `channeldata.cpp`:
- the enabled flag (upper-casing, trimming, ignored, a missing flag, Y and 1), the name priority (three ways), the type codes (01, 09, 11, trimming, case);
- the combo-box separator, each list built from the wrong channels, the bare name;
- both selection offsets and bounds, keeping a stale selection, the default being the last channel, the first-PCM accessor;
- the reload, the error cleared or not, each message, the time sync, the TMATS-first check, the TMATS decode;
- the data rate (zero, negative, the wrong channel, `atof`), and every `ChannelData` field.

**42 caught; the one real gap is closed** (the default time channel, by the row above). **The 2 left are equivalent:** the reader's own "first packet is not TMATS" check sits behind the library's identical check at open, as the suite has always said; and taking a zero rate as a rate returns 0 either way.

Qt baseline **944 / 0 / 1** (was 1022 / 0 / 1: the two suites' 78 results leave), **945 / 0 / 0** at release, **26 suites**. Native baseline **227 cases**.

#### A turn-on transient before a saturated sweep no longer shifts its step numbering (US5.3)

The open finding the `TestStepDetector` port recorded, now fixed. When a receiver rails at the top of its range, the detector falls back to calibrating the steps that stayed in range, and finds which step each surviving level is by counting dwells on a grid of transitions. The grid counted from the first transition in the RECORDING. A turn-on transient above the 0 dB level, before the sweep reverses into it, adds a transition of its own, so every step came out one too high: levels 5000, 1000, 2000, 3000 and two railed dwells, against a 0 to 24 dB step file, calibrated 1000 as 6 dB and reported 0 dB as unresolved. That contradicted the code's own comment that the run's first level is step 0. The full-sweep path, for a receiver that does not rail, was never affected.

**The fix** (`calibrateSaturatedTop` in `stepdetector.cpp`): every slot is taken relative to the slot of the run's first level, both for the levels themselves and for the grid that `measureUnconfirmedDwells` measures missing dwells on. A transition before the run then has a negative slot, and the unconfirmed-dwell measurement skips it. A recording with no lead-in, which is every recording the suite had and the committed step_cal recording, gets exactly the numbers it got before.

**Three new cases**, each failing without the fix by exactly one step (6 dB where 0 dB belongs):
- the transient above a normal sweep;
- its mirror on an inverted receiver, a transient below a sweep that rails at the floor;
- an unconfirmed dwell after a transient, still measured at its own 18 dB, with the step after it keeping 24 dB.

**Mutants of the fix:** four. Three are caught: no shift at all, the levels shifted but not the grid (caught only by the unconfirmed-dwell case), and the shift off by one. One survives, and it is not equivalent: dropping the negative-slot guard. Without the guard the measurement reads one element before the start of its `paired` array. It then discards that value, because the next check finds there is no lower neighbour. An out-of-range read that changes nothing visible is undefined behaviour no test can observe, so the guard stays and is recorded here rather than covered.

The `TestStepDetector` entry below describes this as open; it is closed by this entry. Native baseline **207 cases** (3 more); the Qt and managed baselines are unchanged.

#### `TestCh10PacketReader` moves to doctest, the first native suite to read recordings, and a sync mask no longer clips receiver words (plan step 2.4)

Ninth suite ported, and the first that opens a recording. `native/tmdq_core_tests/test_ch10packetreader.cpp` holds all 9 cases of `TestCh10PacketReader`; the Qt version is deleted. The project now compiles `ch10packetreader.cpp`, `chapter10reader.cpp` and `channeldata.cpp`. It also compiles the 13 vendored irig106 C files exactly as `tmdq_core.vcxproj` does: warnings off, compiled as C, `/utf-8`. It links `ws2_32.lib`. One core line changed: the word-mask fix below.

The recordings are reached through `repoFile()` under `tests/data/test files/`. A missing one fails the case rather than skipping it, because they are committed. The Qt suite's data-driven cases became loops. The exclusive Windows file handle that shows where the irig106 lock is taken is the same `CreateFileW` call as before.

**New cases**, for what the Qt suite counted but did not check:
- **Every field `prepare()` resolves for a worker:** the pattern and mask (given, and defaulted to every bit of a 24-bit pattern), the bits and words of a frame (790 bits against 50 words, so each is read from its own field), the word length, at least one sync, the operator's byte order, and the bit period from TMATS (12.5 x 100 ns for step_cal's declared 0.8 Mbps) or from an operator's rate (25 for 400 kbps).
- **Every packet's own fields:** its bit count is eight times its bytes, and its base time is positive and strictly later than the last.
- **A queue two streams share:** each packet arrives twice, with one end marker and nothing after it.
- **Two more ways setup can fail:** a file cut inside its first header, and one whose first packet is not TMATS. irig106 only warns on these; the reader's own checks say which.
- **Routing on the committed 77 MB PRN sample:** a stream on channel 13 receives exactly channel 13's packets, counted by walking the headers, and none of channel 15's.

**A defect, found by the first new case, and fixed.** `Set_Attributes_Ext_PcmF1` (vendored irig106) ends with `ullCommonWordMask &= ullMinorFrameSyncMask`. The reader copied that word mask into `ResolvedPcmAttrs::wordMask`, and `FrameProcessor` masks every receiver word with it (`frame_words[word] & word_mask`).

- **Who was affected:** only a Receiver SNR stream given a sync mask whose low 16 bits are not all ones. The default mask, and every mask that only ignores bits above the low 16, left the word mask at `0xFFFF`.
- **What happened:** such a stream's AGC values silently lost bits. A mask of `0x00FF00FF` gave a word mask of `0xFF`, so every receiver word kept only its low byte. A mask of `0xFFFF0000` would have zeroed every value. Frame Sync Lock streams were not affected, because they read no words.
- **The fix:** one line in `Ch10PacketReader::prepare()`. The mask is now built from the word length (`(1 << wordLen) - 1`) instead of being read back from the library, which may not be edited. The calibration extractor runs through the same reader, so its profiles are fixed too.
- **The regression test:** `TestFrameProcessor::aSyncMaskDoesNotMaskReceiverWords` runs the TRC recording twice, once with every sync bit significant and once ignoring sync bits 8-15. It requires every receiver value to be identical between the two runs. Without the fix it fails: channel 6's first sample reads 129.283 with the mask and 178.533 without. It passes with the fix. The native suite now also requires the masked stream's word mask to be `0xFFFF`.
- **Two things learned on the way:**
  - The first version of the regression test ignored only the lowest sync bit and passed against the broken code: this recording's raw words never set their lowest bit.
  - The matcher compares the MASKED stream with the pattern as written (`(word & mask) == pattern`), exactly as irig106's own decoder does, so a pattern must carry zeros where its mask ignores bits. With ones there it can never match. That convention is unchanged: it fails loudly ("frame sync pattern was not found") rather than silently.

**Mutation check:** 54 mutants:
- `prepare()`'s failures and their messages, the irig106 open warning, the channel check, every resolved field, the rate override, and the routing table and its de-duplication;
- `run()`'s start line, the header, time and PCM read errors, abort, progress, time sync, the time channel, the channel header strip, empty packets, the bit count and base time, a per-queue copy, the first queue only, the end markers, the final progress, and routing an unasked channel;
- and the irig106 lock: unlocked, released at once, taken after the open, per thread, per call and deferred, in both readers.

First round: **46 caught, 8 survived**. Four were gaps closed by the cases above: a frame of exactly 16 x words bits, base times checked only as non-decreasing, the open warning, and routing on a one-channel fixture. **Four remain, and no committed recording can reach them:**
- a progress percentage scaled wrongly (even the 77 MB sample reads before the 100 ms progress gate first fires);
- time packets on a second time channel (every fixture has one);
- a time packet that changes the relative-time reference (no fixture has a time discontinuity);
- a PCM packet with no data after its channel header (none in any fixture).

Totals **50 caught, 4 unreachable with the fixtures**.

Qt baseline **1022 / 0 / 1** (was 1041 / 0 / 1: the suite's 20 results leave and the regression test adds one), **1023 / 0 / 0** at release, **28 suites**. Native baseline **204 cases / 41,442 assertions**.

#### `TestStepDetector` moves to doctest, and the detection's inner paths finally have tests of their own (plan step 2.4)

Eighth suite ported. `native/tmdq_core_tests/test_stepdetector.cpp` holds all 39 cases of `TestStepDetector` (its two data-driven cases are loops with `INFO`, and `qFuzzyCompare` is reproduced by a small `fuzzy()`); the Qt version is deleted. The project now also compiles `stepdetector.cpp`. No core code changed.

No oracle problem: the Qt suite's expected values were already its own (hand-built series, and the old hand-written step-file parser kept verbatim as an oracle, which moves across unchanged). `initialStepConfigPath` finds the shipped `settings/rcvr_cals/default.toml` through `repoRoot()`.

**The mutation check found the detection far less covered than the suite looked.** 96 mutants over `stepdetector.cpp` and `interpolateCalibration` (`calibrationprofile.h`): the step-file parser, the initial path, the median and noise estimate, stability marking, plateau confirmation, level averaging and both merges, sweep selection, and every part of the saturated-top path - the dwell grid, measuring an unconfirmed dwell, and its guards. The ported 39 cases, plus two new ones (several interpolation segments; a non-ASCII application root), caught **55. 41 survived.** The Qt suite's cases each pin a scenario that once failed in the field, and they pass through the inner rules without isolating any of them. The real recording in `TestCalibrationExtractor` reaches some of these rules, but only as one scenario.

**17 new cases** close the real gaps, and each has its expected value worked out by hand:
- **The noise estimate.** The edge threshold is six robust sigmas of the live derivative: 71.1648 for a dwell pattern whose derivative is 0, +8 and -8 in thirds. Runs pinned at the floor *and* at full scale are left out.
- **Plateau confirmation.** A plateau needs exactly 100 stable samples at 10 ms: a 102-sample dwell confirms and a 101-sample one does not.
- **The 2-count edge floor.** Steps of 2.5 counts are steps, and relaxing the threshold below the floor never rescues steps of 1.5.
- **Sweep selection.** A sweep that starts where a reversal ended is found.
- **Merging.** An 80-count split (between half and all of the 10% merge tolerance) heals, and so does an inverted receiver's split dwell.
- **Railing at the floor.** An inverted receiver railing at zero is calibrated over the steps it kept. "Railed" is within 2% of either end of the converter's range.
- **The dwell grid.** A transition smeared over two samples is one boundary, and short dwells at a coarse sample period keep their own. The grid tolerates dwells of 150, 140 and 155 samples and refuses 150 and 260, including when the irregular dwell is the last one. A level held through two dwells is skipped in the numbering, not shifted.
- **Unconfirmed dwells.** One is measured by the median of its settled tail, not from its start and not by the mean, and one that is not between its neighbours is left unresolved.
- **The saturated-top guards.** The path needs a railed top, at least three measured steps and at least half the step file. Two levels inside one dwell keep the fallback.
- **Counts reported.** `sweepLevels` counts the railed level, and `detectedPlateaus` describes the nominal pass rather than the retry.

Two re-runs later: **82 caught, 13 equivalent, 1 open.** The equivalent ones:
- **Sign changes that cannot matter.** The derivative's sign only ever reaches `fabs` and a median absolute deviation. A level change smaller than the threshold cannot reach direction-finding, because the merges remove it first.
- **Duplicate and unreachable guards.** The one-sample guard duplicates the two-sample one. The noise-threshold merge is covered by the step merge, whose tolerance is never smaller. The grid's at-least-two-boundaries check is repeated by its empty-spacings check. An unconfirmed dwell at the top slot is already paired, and the neighbours it is measured between always exist.
- **Fields nothing reads.** A level's end is never read, and a level's begin and its averaging window's start fall in the same dwell.
- **Interpolation.** The clamp above the last point is also the loop's fall-through. The coincident-points guard is unreachable, because a value can never lie strictly between two equal raw counts. So neither its removal nor its return value can be seen.

**The open one is a finding, recorded and not changed here.** The saturated-top path numbers the steps one too high when a turn-on transient ABOVE the 0 dB level comes before the sweep. A recording of 5000, 1000, 2000, 3000, then two railed dwells, against 0-24 dB, calibrates 1000 as 6 dB and reports 0 dB as unresolved. The cause is that the dwell grid counts the transient's transition. That contradicts the code's own comment that the run's first level is step 0. The full-sweep path handles the same transient correctly (the new reversal case). The mutant that survived only changes which run the saturated path takes, and that cannot be seen without a lead-in this path already mishandles. Whether and how to fix it is a calibration decision, so it is left as it is.

Qt baseline **1041 / 0 / 1** (was 1099 / 0 / 1: the suite's 58 results leave), **1042 / 0 / 0** at release, **29 suites**. Native baseline **191 cases / 33,229 assertions**.

#### `TestFrameSetup` moves to doctest, and a default word map can no longer be built from two negative counts (plan step 2.4)

Seventh suite ported. `native/tmdq_core_tests/test_framesetup.cpp` holds all 27 cases of `TestFrameSetup`; the Qt version is deleted. It is the first ported suite that reads committed files from the repository (`tests/fixtures/test_framesetup*.toml`, `settings/receiver_params/RASA.toml`): `test_support.h` gained `repoRoot()`, which walks up from the working directory or from the header's own path (MSBuild passes absolute paths, so `__FILE__` is one) to the folder holding `tmdq.sln`, and `repoFile()`, which **requires** the file. The Qt suite skipped when a fixture was missing; these are committed, so a missing one is a broken checkout and fails.

**15 new cases**, for what the Qt suite did not reach: the linear calibration's arithmetic (worked by hand for every voltage range and both polarities, and checked end to end: the lowest and highest raw counts land on the two ends of the dB range in the order polarity says; nothing in the Qt suite tested `applyLinearCalibration` at all), the reserved sections (each of the seven names, repeated `[EMPTY]`s, and that a name only LIKE one is a parameter), file order kept rather than sorted, a word that is not a number, a missing file, a load replacing the previous one (and a failed load keeping what it read before the bad entry), `addParameter` and both `getParameter` overloads, the default map's word numbering and its boundaries, `readReceiverParams` preferring `[Receivers]` and ignoring counts and scales that are not positive, the exact keys `writeReceiverParams` writes and that it replaces the old file, a source map that fails to load being replaced by the default one rather than mixed with it, and the message when the file cannot be written.

**A defect, found by the new default-map case and fixed.** `buildDefaultReceiverMap` checked only that the PRODUCT of the counts was positive, so `-2 x -2` passed, the loops built nothing, and the call reported success with an empty map. The dialog's spin boxes start at 1 and `readReceiverParams` ignores a count that is not positive, so the only way in is a hand-edited processing template (`StreamConfigSchema` reads the counts as they stand) on an installation whose shipped `default.toml` is missing; even then the job builder's own check refused the stream ("Receiver Parameters file contains no parameters"), so no run produced wrong data. The guard now checks each count as well, and such a stream is refused with the map's own reason. `src/model/framesetup.cpp` is compiled by both builds, so the Qt suite was rerun against it.

**Mutation check:** 90 mutants of `framesetup.cpp` (every reserved name, every load check and its bound, both `getParameter` bounds, `addParameter`, every term of the linear calibration, the profile attach, the word rounding, every key and guard of the parameter reader and writer, the save's source, clear and error paths, the names and their parsing, the default map's bounds, stride and names, the word map's 1-based numbering). First round: **85 caught, 5 survived, and two were real gaps in the port**: the round-trip test's source map was exactly the default map for its counts, so a save that ignored the source passed (the source now has an `[EMPTY]` gap and a fourth parameter); and the never-written test checked only that some error was reported, so a save that ignored the default map's failure and reported a write error instead passed (it now checks the text). Both caught on the re-run. **The three left are equivalent**: an unparseable `Word` already reads as -1 through its fallback of 0, so ignoring the parse flag changes nothing; and receiver number 0 or an unparsed one both come out as 0 either way, because the parser leaves its output alone on failure. Totals **87 caught, 3 equivalent**.

Qt baseline **1099 / 0 / 1** (was 1128 / 0 / 1: the suite's 29 results leave), **1100 / 0 / 0** at release, **30 suites**. Native baseline **133 cases / 9,748 assertions**.

#### `TestIntervalGate` moves to doctest (plan step 2.4)

Sixth suite ported. `native/tmdq_core_tests/test_intervalgate.cpp` holds all 7 cases of `TestIntervalGate`; the Qt version is deleted. No core code changed, and no new core source was needed (`IntervalGate` is header-only).

No oracle problem: the Qt suite already used injected time points and Qt only for its assertions. Two cases are new, both found by asking what a wrong gate could get away with. **Every original case used a 100 ms interval**, the reader's own figure, so a gate that ignored its argument and used 100 ms would have passed them all; the new case checks 1, 7, 250 and 5,000 ms each at its boundary. And a zero interval is always due, while a reading from before the start (a negative elapsed time, which a steady clock cannot produce but the comparison must not wrap on) never is. The reset case now resets twice, so a reset that adds to the start instead of replacing it shows.

**Mutation check:** 17 mutants (the seven the Qt suite was validated against, plus: the interval hard-wired to 100 ms or never stored, the start never stored, rounding the elapsed time up or truncating it, comparing the absolute difference, defaulting `due()` or `reset()` to the clock's epoch, the comparison inverted, and a zero interval never due). **15 caught by a test, 1 by the compiler, 1 equivalent.** The compiler's catch was `due()` restarting the interval itself, which needs `due()` to stop being `const`; a version that keeps it `const` with a `mutable` start was then run separately and caught by two cases, so the guarantee is held by a test and not only by the signature. The equivalent one truncates the elapsed time to whole milliseconds, which is exactly what `QElapsedTimer` did: for a whole-millisecond interval a truncated time reaches N ms when the real one does (the header says so), and the two differ only for a zero interval and a negative elapsed time under a millisecond, which a steady clock never produces.

Qt baseline **1128 / 0 / 1** (was 1137 / 0 / 1: the suite's 9 results leave), **1129 / 0 / 0** at release, **31 suites**. Native baseline **91 cases / 9,189 assertions**.

#### `TestPacketQueue` moves to doctest (plan step 2.4)

Fifth suite ported. `native/tmdq_core_tests/test_packetqueue.cpp` holds all 12 cases of `TestPacketQueue`; the Qt version is deleted. No core code changed, and no new core source was needed (`PacketQueue` is header-only).

No oracle problem here either: the queue was already `std`, and the Qt suite only used Qt for its assertions and for `qFatal`. The two safety nets it learned it needed carry over unchanged in shape: the helper-thread guard closes the queue and joins with a 5-second bound, and the 20,000-item stress test has a watchdog, so a broken `close()` aborts the run with a message instead of hanging it (CI has no job timeout). `qFatal` became a small `abortRun()` (message to stderr, then `std::abort`).

Three cases are new: an end-of-stream item the **producer** sends travels like any other and does not close the queue; the sentinel a **closed** queue makes carries no payload, no bits and no time; and a full queue closed under a blocked producer refuses that producer's item but keeps the ones it already holds (the reader's cancel path). The depth-bound case now runs at depths 1, 3 and 7 instead of 3 alone.

**Mutation check:** 26 mutants (the eight the Qt suite was validated against, plus: the bound at depth minus one, pushing to the front, each side of `close()` not waking, the flag never set, notifying the wrong side, the default depth off by one, a non-positive depth taken literally or any depth ignored, a closed queue discarding, ignoring its items, accepting or storing after close, the predicates not seeing close, the consumer waiting for close, the sentinel not flagged, an item moved out field by field, the payload deep-copied, and `close()` taking no lock). **25 caught** (two by a hang the runner kills, the rest by a failing case or by the guard's abort), **one survived and it is not equivalent**: `close()` without the lock is a data race whose symptom, a lost wakeup, needs the close to land between a waiter's predicate check and its wait. No test can make that window happen on demand; it is recorded here as a known blind spot, the same kind `irig106lock.h` has for a lock released too early.

Qt baseline **1137 / 0 / 1** (was 1151 / 0 / 1: the suite's 14 results leave), **1138 / 0 / 0** at release, **32 suites**. Native baseline **82 cases / 9,154 assertions**.

#### `TestEventReporter` moves to doctest (plan step 2.4)

Fourth suite ported. `native/tmdq_core_tests/test_eventreporter.cpp` holds all 6 cases of `TestEventReporter`; the Qt version is deleted. No core code changed, and the project needed no new core source (`EventReporter` is header-only in `processingevents.h`).

This one had no oracle problem: the Qt suite already compared with a plain recording sink, not with Qt. Two cases are new: a sink that does not override `progressUpdated` (the interface makes it optional) still hears every other call, and arguments pass through unchanged (UTF-8, an empty message, an embedded NUL, progress of 0, 100 and -1). The clearing case now also checks that `finished` and `progress` go quiet, not only `log` and `error`.

**Mutation check:** 24 mutants of `EventReporter` and the interface (each call routed to another method, dropped, doubled, its argument lost or altered; each null guard removed; `setSink` ignored, first-wins, ignoring a null, or borrowing instead of sharing; `progressUpdated` made pure virtual). **23 caught by a test, 1 by the compiler** (making `progressUpdated` pure virtual stops the sink that relies on its default from compiling, which is the guarantee that case states), none survived. The first run reported one mutant as a compile error that was a `LNK1201` PDB write failure; it was re-run and caught, and the runner now treats `LNK1201` and `C1090` as inconclusive like the other file-lock flakes, so a link failure can never count as a catch.

Qt baseline **1151 / 0 / 1** (was 1159 / 0 / 1: the suite's 8 results leave), **1152 / 0 / 0** at release, **33 suites**. Native baseline **67 cases / 9,043 assertions**.

#### `TestFrameSync` moves to doctest (plan step 2.4)

Third suite ported. `native/tmdq_core_tests/test_framesync.cpp` holds all 7 cases of `TestFrameSync`; the Qt version is deleted. The project now also compiles `framesetup.cpp` (`framesync.h` includes `framesetup.h` for `FrameSetup::wordsInMinorFrame`); no core code changed.

**The oracle problem again, solved the same way as `TestTextFormat`.** The Qt suite compared `textparse::parseUnsignedHex` with `QString::toULongLong(16)`, on a list of 54 awkward inputs and on 40,000 generated strings. The doctest binary has no Qt, so:

- **What Qt answered**, recorded once with a throwaway diagnostic (removed again): a table of 78 inputs with Qt's accept / refuse and value (the original 54 plus 24 new edges: the widest values, `0x` with nothing after, a prefix after a sign, a control character, a no-break space, `+` before a prefix), and the sweep as one FNV-1a digest of all 40,000 answers, 7,753 of them accepted. A refused parse must also leave the output alone, which the table now checks with a sentinel.
- **A round trip that shares no code with the parser**: 4,000 random values of every width from 1 to 64 bits are written with `std::format("{:x}")` / `{:X}` and read back through every spelling the parser accepts (a leading `+`, `0x` / `0X`, up to 24 leading zeros, ASCII whitespace of every kind on either side). Plus the refusals: one bit more than 64, a sign that is not a leading `+`, a second prefix, a space inside.

**New cases beyond the original seven**, from the questions the mutation check would otherwise have asked: `applyAcquisition`'s three flags independently over all eight combinations (so none is read from another), megabits to bits per second (and zero or a negative rate to zero), the frame rounded up to whole words at 0, 1, 15, 16, 17, 32, 33, 800, 801 and 65,536 bits, and a mask that is refused on overflow.

**Mutation check:** 77 mutants over `framesync.h` (the decode, its error codes and order, the default mask, what is written on failure, every line of `applyAcquisition`), the hex parser in `textparse.h`, `fullSyncMask` and `wordsInMinorFrame`. **74 caught, 3 equivalent, none a gap.** (The extensions above were written before the first run, so this is the result for the extended suite, not a count of what the original seven would have missed.) The three equivalent: a rate of exactly zero taken as a rate (zero times a million is zero); a prefix test that needs one character instead of two (it reads the string's terminator, which is not an `x`); and one that needs three (`0x` alone is refused with or without the prefix stripped).

Qt baseline **1159 / 0 / 1** (was 1168 / 0 / 1: the suite's 9 results leave), **1160 / 0 / 0** at release, **34 suites**. Native baseline **59 cases / 8,993 assertions**.

#### `TestTextFormat` moves to doctest, and a not-a-number prints the way Qt prints it (plan step 2.4)

Second suite ported (the framework came with #190). `native/tmdq_core_tests/test_textformat.cpp` holds all 7 cases of `TestTextFormat`; the Qt version is deleted from `tests.pro`, `main.cpp` and the tree.

**The Qt suite used Qt as its oracle, and the doctest binary has none.** Each assertion that compared the helper with `QString::number(v, 'f', n)` needed an expected value from somewhere else, and there are two sources, neither of them the helper:

- **What Qt printed**, recorded once with a throwaway diagnostic in the Qt suite (removed again): the values that are not ties (13 values, 0 to 7 decimals), the signs and zeros, the large and small magnitudes (`5e-324`, `4503599627370496.5`, ...), and the 20,000-value sweep as one FNV-1a digest of all its answers (the sweep's powers of ten are now a table, since `std::pow(10, -3)` is not guaranteed to equal the literal).
- **Exact integer arithmetic**, for every `n/64` (-4200 to 4200) at 0 to 6 decimals: the value is an integer quotient and a remainder in units of 10^-d, so "half away from zero" is a comparison on integers with no formatting rule in it. Over a thousand of those are true ties.

New cases beyond the original seven: **one representable step either side of every tie** (above it rounds up, below it down; the helper nudges a tie up by one step, so the neighbours are where a wrong nudge shows), the **neighbours of a tie are not ties** when asked of `isExactTie` directly, and **infinity and not-a-number**, which no test had.

**A real difference from Qt, found by the last of those.** For a negative not-a-number the helper printed `-nan` (it fell back to `std::format("{}", value)` with the sign bit still set), where Qt prints `nan` for either sign. Recorded from Qt: `inf`, `-inf`, `nan`, `nan`. Nothing in the application can print one (a calibration value comes from a 16-bit count), so it never showed, but the helper's own statement of purpose is "reproduces Qt's output". The fix removed code instead of adding it: the `!isfinite` branch is gone, because the magnitude has no sign bit left (`fabs`) and an infinity takes its sign from the comparison below, so every non-finite value goes through the ordinary path. The new case fails against the old helper (2 assertions) and passes against the new one; every other ported case passed against the unchanged helper on the first run.

**Mutation check:** 39 mutants of `textformat.h` against the ported suite, then 33 against the simplified helper (the 7 that mutated the removed branch dropped, one added). Final: **26 caught, 7 equivalent**, and one real gap found by the first round and closed: the digit-5-then-zeros test in `isExactTie` could lose its "then zeros" half (`0.25` plus one step is not a tie, but still has a 5 in the next place) and nothing noticed, because rounding such a value up needs no nudge. The equivalent seven, each reasoned and not just survived: `kExactPlaces` at 17, 100 or 1075 (the exact expansion of any value this can be asked about shows its first nonzero digit within about 17 places of the next one; 1075 is past a double's longest expansion anyway); dropping the `== '5'` half (a digit that is not 5 cannot be followed by only zeros if the rounded digit was a 5); `!= '0'` for `== '5'` (the same argument); asking the tie of the signed value instead of the magnitude (the digits are the same); and a mutant that tried to put the sign back on a not-a-number through a multiplication, which the hardware leaves alone.

Qt baseline **1168 / 0 / 1** (was 1177 / 0 / 1: the suite's 9 results leave), **1169 / 0 / 0** at release, **35 suites**, from a clean build of `tests/` and an incremental one of the app (a header was deleted, so `tests/` was wiped) with zero warnings in both. Native baseline **48 cases / 522 assertions** (the previous 39 plus 9 cases).

#### The core's tests start moving off Qt Test: doctest, and `TestTomlDocument` first (plan step 2.4)

First of the PRs porting the core's suites to **doctest** (plan decision D2: one vendored header), one suite per PR, old and new side by side until the new one holds every case, a mutation check against the same mutants, then the Qt version is deleted. This one brings the framework and the first suite.

- **`lib/doctest`** is doctest 2.4.12, the single header, with its licence and a README giving the source URL and SHA-256 and saying not to edit it.
- **`native/tmdq_core_tests/`** is an MSBuild project (in `tmdq.sln`) that builds `tmdq_core_tests.exe` from `main.cpp`, one `test_<suite>.cpp` per ported suite and **only the core sources those suites need** (so far `textfile.cpp` and `tomldocument.cpp`). It has no Qt include path and no Qt library, and the same compiler switches as `tmdq_core.vcxproj`. `test_support.h` holds the helpers (`TempDir`, `writeBytes`, `readBytes`, `utf8`).
- **`scripts/test_native.ps1`** builds it, requires **zero compiler warnings** and an import list with no Qt, runs it, and fails on a non-zero exit. Arguments after the switches go to doctest (`-tc="TomlDocument*"`). **CI runs it in both jobs** before the managed tests.
- **`TestCoreQtFree` guards the new project** the way it guards the DLL project (`nativeTestsProblems`): only core sources, none from another layer, no Qt outside comments, every `test_*.cpp` in the project and the project listing no file that is not on disk, no Qt or View/ViewModel header reached through the test files' includes, and the solution builds it. Two tests (the real tree, and a temporary tree with each kind of drift).
- **`TestTomlDocument` is ported and deleted** (`tst_tomldocument.{h,cpp}` gone from `tests.pro` and `main.cpp`). All 18 cases of the original are in `test_tomldocument.cpp` under the same descriptions. The expected values did not change; what changed is the helpers and `CHECK` for `QCOMPARE`. 39 cases and 300 assertions in all.

**Mutation check, the old method on the new suite.** 161 single-defect mutants of `tomldocument.cpp`, `textfile.cpp` and the number readers in `textparse.h` (every comparison, boundary, key name, branch and return of the load, the typed reads, the array-of-tables reader, the writer, the encoding detection and the number rules). The ported suite alone caught 111; **50 survived, and they were real**: the ported suite is the Qt one, and the Qt one had never been mutation checked. Closing them added 12 cases to `test_tomldocument.cpp` and the whole of `test_textfile.cpp` (9 cases; `textfile` had been tested only through `TomlDocument`; a UTF-8 length boundary, an unpaired surrogate or an odd trailing byte could change without a test noticing). Second round, the 50 again with a mutant whose pattern had matched nothing rewritten: **42 caught, 8 survive and all 8 are equivalent**: reading `inner[size]` (the string's terminator) in an unescape that already checks the next character, clearing a section name that is already empty, an empty line falling through to a branch that skips it, the `size() >= 4` guard on a `[[...]]` line (a line of three or fewer characters cannot both start with `[[` and end with `]]`), the `"0"` fallback text of a number write that cannot fail, the failure of `close()` on a file that wrote (needs a full disk) and its message, and the empty-input check before `from_chars` (which refuses empty input anyway). Totals: **153 caught, 8 equivalent**, 161. The runner is the one from the earlier steps (pristine byte copies verified, a green baseline before the first mutant, the exe deleted and required newer than the source before each result, a hang counts as a catch, an `mt.exe` flake is inconclusive rather than a compile error).

**Two corrections to my own checks, found by running them:** a value ending in a backslash before the closing quote reads back as the value with its backslash (`Q/Trailing` is `abc\`; the first draft of the test expected `abc`), and one mutant's text matched nothing so it was "run" unmutated and survived for the wrong reason (rewritten to its context).

**Traps worth knowing:**

- doctest's `CHECK(a == b)` on two `std::string`s prints both on failure, but a `char8_t` literal does not convert to `std::string`; `utf8(u8"...")` in `test_support.h` is the one place that does it.
- `msbuild` of this project writes to `native\out\<config>\` beside the DLL, so `scripts\build_core_dll.ps1`'s "delete the DLL first" does not touch the test exe; `test_native.ps1` deletes the exe first for the same reason.
- Run it from a PowerShell that has `env.ps1` loaded, or let the script dot-source it (it does when `msbuild` is not on PATH).

Qt baseline **1177 / 0 / 1** (was 1196 / 0 / 1: `TestTomlDocument`'s 19 results leave, `TestCoreQtFree` gains 2), **1178 / 0 / 0** at release, **36 suites**, from a clean build of both trees with zero warnings. Native baseline **39 cases / 300 assertions / 0 failed**. Managed baseline unchanged.

#### Series construction in the C# library - the managed library covers the whole ABI (plan step 2.3d)

Last of the four PRs wrapping the C ABI in `dotnet/TmDq.Core`. With the files and runs (2.3a), the step files and calibration (2.3b) and the formats (2.3c), the library now has a managed face for every group of the ABI. No native code changed; the ABI is still version 2, revision 0.

- **`StreamResult.BuildSeries(baseAbsSeconds)`** returns a **`SeriesSet`**: the series a stream yields (lock %, then accumulated missed frames, then one per receiver channel, each with its metric, name, stream label, receiver, channel index, stream order, job index, source and range) on the **shared elapsed axis**. The values and the elapsed times are **views** of the stream's data, not copies: the samples stay where the host keeps them (plan decision D3). The set keeps that data alive, so the result may be disposed first, and it refuses reads once it is disposed itself.
- **`StreamResult.Key`** and **`StreamKey.IsSameStream`**: which stream a series belongs to; the same stream run again is the same only if source, label and channel all agree. The rule is decided by the core; the managed `==` is the same three-field comparison, and a test holds the two together.
- **`TimeBase`**: the clock the shared axis hangs from (`Info`, `Reset`, `SetClock`, `Overlaps`, `Adopt`, `Advance`). It says how far every existing series must shift when a stream arrives or a source goes; applying the shift to stored samples is the host's.
- **`ImportedStreams.AssignOrders`** gives an imported CSV's frame-sync series the order of their streams (from the SNR series of the same label in the same source, else a fresh order above every one in the file), for a list of `ImportedSeriesInfo` or straight from a parsed CSV's series.
- The five new native structs are mirrored and size-checked against `tmdq_sizeof` like the rest, and pinned field by field to the offsets `TestAbi` pins.

**Tests** (`SeriesTests`, `SeriesDigest`; the suite grew from 257 to 301): the expected answers are what the core's own functions give, recorded with a throwaway diagnostic in `TestAbi` (removed again): `buildStreamSeries` and `streamKeyOf` over the `step_cal` recording as a lock stream labelled with a non-ASCII name and a receiver stream, in source 3, at three time bases (every series' identity and range, and the sum, first and last of every array, exactly); a **90-step random sequence of time-base operations** (adopt, advance, overlaps, set clock, reset) compared **step by step** on the result and on the base after each; fourteen stream-key pairs; and fourteen import lists. Checks that need no recording at all: the elapsed time is the sample's time less the base for every sample, the values are the stream's own data, a set outlives its result, the key of a result, a hand-worked first / earlier / later / removed sequence on a time base (touching at one instant overlaps), the orders the Qt CSV tests pin for three CSVs (`41, 40, 40, 41`; `1, 2, 2`; `41, 41, 40`), an unknown metric refused, and every access after dispose. The existing memory test now also builds and disposes a series set per result, so a set that is never freed (which would keep a 2.5 MB stream alive) fails it.

**Two corrections to my own checks, found by running them:** a receiver series is named `26 - rx L_RCVR1` (channel, label, name - the way the CSV spells it), not the bare channel name; and the first random sequence of time-base operations never produced an overlap that came out true, which the reach test caught, so the generator was biased and the digest re-recorded.

**Validated against 108 single-defect mutants** of the new code. 89 were caught at once, 2 by the compiler, and the 16 that survived were looked at: **two were real gaps** (an unknown metric reaching the import ordering was refused by the core and nothing checked it; a series set never freed - both closed and caught) and **fourteen are equivalent**: native calls that can fail only for a null handle (creating a time base, reading its info, reset, overlaps, adopt, advance, a series set's info and spec, a result's key and series), the small time-base handle never freed, `Advance` with no earliest sample passing a zero in either case, a key comparison's negative return that only a null key produces, and a trailing `Reserved` field that padding makes the same size. One mutant's text matched two structs and was rewritten with its context and re-run.

**With this, plan step 2.3 is complete**: `TmDq.Core` wraps files, runs, calibration, the formats and series construction; its 301 tests run in CI against `tmdq_core.dll`.

#### The file formats in the C# library (plan step 2.3c)

Third of four PRs wrapping the C ABI in `dotnet/TmDq.Core` (2.3d, series construction, follows). It carries the formats stage 1 moved into the core, so the C# application reads and writes the same bytes the Qt one does (plan rule 5: one implementation of every format). No native code changed; the ABI is still version 2, revision 0.

- **`FrameSyncFile`**: `Read(path)` gives the pattern and mask as written, the frame length only if the file gives a usable one (and whether it came from the older `WordsInMinorFrame` key), `Inverted`, and `Randomized` as **null when the file has no such key** (older files, the shipped PRN patterns among them, must leave the operator's choice alone); a missing, empty or unreadable file reads as one that says nothing. `Write(...)` writes the pattern, mask, length and the two flags (data rate and sample rate stay out); a file that cannot be written is an `Io` error.
- **`TemplateMatcher.Match(templateIds, fileIds)`**: exact-set comparison, order and repeats ignored, two empty sets match; the missing and extra ids come back sorted, each once.
- **`CsvFile.Parse(path)` / `CsvFile.Write(path, columns, baseDay, baseTimeOffset)`**: the application's CSV in both directions, as managed copies (series with their metric, name, stream label, receiver, channel index, stream order, source, range and samples; the base day and time of day, the skipped rows and the last elapsed time). A file the parser refuses is a `Prepare` error.
- **`ClockTime.Format(baseDay, baseTimeOffset, elapsed, includeMilliseconds)`**: the plot's `DDD:HH:MM:SS[.mmm]` stamp.
- The five new native structs are mirrored and size-checked against `tmdq_sizeof` like the rest, and pinned field by field to the offsets `TestAbi` pins.

**Tests** (`FormatsTests`, `FormatsDigest`; the suite grew from 195 to 257): the expected answers are what the core's own functions give for the same inputs, recorded with a throwaway diagnostic in `TestAbi` (removed again) that ran `readFrameSyncFile`, `writeFrameSyncFile`, `CsvSeriesParser::parse`, `CsvSeriesWriter::write`, `TemplateMatcher::match` and `clocktime::format` over a corpus the generator also wrote into the test: **58 pattern files** (every key and spelling, the legacy length key at each boundary, both flags in every spelling, byte-order marks, UTF-16, padding, comments, the three shipped files), **56 CSV files** (line endings, byte-order marks, headers and cells in every awkward form, malformed rows and times, all three metrics, source qualifiers, channel counts), eight pattern writes, twelve template matches, seventeen clock stamps and fourteen CSV writes (ties, rounding, NaN and infinity, sources, empty and non-ASCII names, more times than values, an empty column). Pattern and CSV writes are compared **byte for byte**, doubles exactly (17 significant digits), and a reach test asserts the corpus really produced every kind of answer. Hand-written checks cover a written CSV's header and rows and its round trip, every refusal and its status, a non-ASCII folder, and a parsed CSV giving its native memory back (thirty parses of a 100,000-row file).

**Two traps the comparison found, both in the test and not the library:** a NaN made in C# has the sign bit set where the C library's `strtod("nan")` does not, and the writer prints the two differently (`-nan(ind)` against `nan`), so the corpus NaN is built with the C library's bits; and the `Text.Snprintf` helper reads the library's last error, so its tests need the library loaded, which other test classes had been doing for them.

**Validated against 125 single-defect mutants** of the new code (one failed to compile). 109 were caught by a test; **15 survived, one a real gap and fourteen equivalent**. The gap: a parsed CSV that was never freed natively, invisible until a test measured the process (it now does, and fails the mutant). The equivalent ones: calls that cannot fail for a valid handle (reading a pattern file or a match, the info calls), the small pattern-file and match handles never freed, an empty array passed as null or as an empty block, the writer's x and y counts swapped (it writes the shorter), the clock's base offset and elapsed seconds swapped (they are added), and two `Reserved` fields dropped from structs that padding makes the same size. The runner's `mt.exe` retry from the previous step meant no build was mistaken for a compile error.

#### Step files and calibration extraction in the C# library (plan step 2.3b)

Second of three PRs wrapping the C ABI in `dotnet/TmDq.Core` (2.3c, the formats and series construction, follows). This one carries the Extract Calibration workflow across: read a step file, run an extraction over a calibration recording, read each channel's outcome, and hand the profiles it found to a run. No native code changed; the ABI is still version 2, revision 0.

- **`StepFile.Parse(path)`** returns the expected steps in file order; an unreadable file or one with no step is a `Prepare` error carrying the parser's own message. **`StepFile.InitialPath(appRoot, preferred)`** is the file the dialog opens with (the preferred one if it exists, else the shipped default under the application root, else empty), through a small `Text.Snprintf` helper that does the library's two-call text convention (ask for the length, then for the text).
- **`CalibrationRun.Start(request, observer)`** takes a `CalibrationRequest` (the recording, the receiver stream's settings, the steps, the clips) and returns at once; `IsRunning`, `Cancel`, `Wait`, `Dispose` (cancels, waits, releases) are as for a processing run, and it reports to the same `IRunObserver` (`OnStreamCompleted` is never called). A sync pattern or mask that cannot be read is a `Job` error before anything starts; a failure the extractor sees first (no steps, a recording it cannot open) is **not** an exception: the observer is told `OnError` then `OnFinished(false)` before `Start` returns and the run is returned already finished, as the ABI documents.
- **`GetResult()`** is a managed copy valid after the run is gone: the summary line, the error, the operator-facing report, the recording length, whether the clips were ignored, and a `CalibrationChannel` per word (name, word, points, plateau levels, unresolved dB, edge threshold, outcome and the flags). It throws a `State` error while the extraction is still running. `CalibrationResult.Profiles()` is the usable profiles, ready for `StreamConfig.Calibration`.
- **One bridge for both runs' callbacks.** The static callbacks, the `GCHandle` that finds the observer again and the first-exception-wins rule moved out of `ProcessingRun` into `ObserverBridge`, which both runs use; `ProcessingRun`'s behaviour is unchanged (its tests are the proof, plus the mutation check below).
- The three new native structs are mirrored and size-checked against `tmdq_sizeof` like the rest, and pinned field by field to the offsets `TestAbi` pins.

**Tests** (`CalibrationTests`, `CalibrationDigest`; the suite grew from 124 to 195 tests): the expected numbers are what the core's own `CalibrationExtractor` makes when run directly on `step_cal`, recorded with a throwaway diagnostic in `TestAbi` (removed again) that printed 17 significant digits, so the comparison is exact: the summary, the whole report text, the recording length, and **all 48 channels field by field** (name, word, validity, outcome, plateaus, the saturated and relaxed flags, the edge threshold, the points, the plateau levels, the unresolved steps); seven variations (asymmetric clips, derandomize, invert, data rate, sync mask, frame length, a shorter step file), three of which (derandomize, invert, a mask that matches nothing) find no sync at all, which is a result too; the receiver grid by the names it produces (`L_RCVR1, L_RCVR2` against `L_RCVR1, R_RCVR1`); the application root's own word map and a named one; the file-scoped byte order and the PCM channel; clips that leave nothing; the profiles it found fed to a receiver stream, compared with the core's own run of the same (66 samples, 48 channels, the sum of every value); the observer's order; every failure with its status and the core's message; state errors while running; cancel; dispose in flight; a throwing observer.

**Validated against 138 single-defect mutants** of the new and changed code, with the managed runner of the previous step (now also retrying a build whose `mt.exe` step fails to write the manifest into the exe, which eleven times looked like a compile error and was nothing of the kind, and reporting a build that still fails that way as inconclusive). 108 were caught by a test and 3 by the compiler; the 11 inconclusive ones were re-run and caught; **16 survived**, of which **five were real gaps** in `Text.Snprintf` (its error and truncation paths cannot be reached through `InitialPath`, so it is now tested directly with fake native functions that write more, less and nothing, and fail on either call) and **eleven are equivalent**: a native struct, a GC handle or a bridge that is never freed (a leak the API cannot show), the time channel of a calibration, a zero-length step array passed as null or as empty, a callback table filled for a run with no observer, a check that cannot fail at that point, and a struct's trailing `Reserved` field (padding makes the size the same). A redundant clear of the callback table, whose every field is set straight after, was deleted rather than tested.

**The false "compile errors" also showed that 2.3a's own round had hidden eleven**, never re-run. Re-running them found **two real gaps in the code merged as #186**: a word-map file named on a stream (`ReceiverParamsToml`) was never exercised by a managed run, and nothing checked that the default observer releases a completed result. Both have tests now.

#### A C# library over the C ABI, with tests that run the recordings through it (plan step 2.3a)

First half of plan step 2.3 (the second, 2.3b, wraps the rest of the ABI). `dotnet/TmDq.Core` is the managed side of `tmdq_core.dll`: what the Avalonia application will call. It is a net10.0 class library with no package dependencies, written with `[LibraryImport]` source-generated stubs. This PR covers what the walking skeleton (plan step 3.4) needs: loading and verifying the DLL, listing a recording's channels, and running N streams with callbacks, cancel and results. Calibration, step files, the formats and series construction follow in 2.3b.

- **`Library.Load(path)`** loads the DLL by full path (the runtime's resolver is installed once and reads the candidate path, so a file that is not a library can be refused and another tried), then checks the **ABI version and revision** and that **every mirrored struct is exactly as large as the DLL says its twin is** (`tmdq_sizeof`), because a wrong offset reads plausible garbage rather than failing. Loading a second DLL beside the first is refused; the same path in another spelling (case, relative) is a no-op. Every other entry point refuses with a message naming `Library.Load` if nothing is loaded.
- **`Ch10File.Open(path)`** gives the time and PCM channels (id, TMATS name, declared rate) and the default time channel, as plain values; nothing native is held afterwards.
- **`ProcessingRun.Start(request, observer)`** takes the settings records (`StreamConfig`, `RunRequest`, `CalibrationProfile`, with the application's own defaults) and returns at once. An `IRunObserver` (every method optional) is told `OnLog`, `OnError`, `OnProgress`, `OnStreamCompleted(StreamResult)` and `OnFinished`, on the native threads, in the documented order. The native callbacks are `[UnmanagedCallersOnly]` static methods with a `GCHandle` context; an exception thrown by an observer is **caught and kept** (`ObserverException`, the first one) rather than allowed to unwind a native thread. `Dispose` cancels, waits and releases, so an observer may be torn down straight after.
- **`StreamResult`** views the native arrays without copying (`TimesSec`, `LockPercent`, `MissedFrames`, `ChannelValues`) until disposed; `ToProcessed()` takes a managed copy that outlives it. Every accessor refuses after `Dispose`.
- **Memory.** An `Arena` owns the UTF-8 strings and arrays of one call and frees them when the call returns (the library copies what it keeps).

**The ABI is now version 2.** The spike's two version-1 calls (`tmdq_probe_file`, `tmdq_run_lock_stream`), kept until this step replaced their only user, are removed, along with `dotnet/hybrid-spike/driver` and the spike's own `tmdq_core.cpp`/`build_core.ps1` (its README says so). `tmdq_core.cpp` now holds only the error text, the version and `tmdq_sizeof`. The revision starts again at 0. Header and tests also now pin a rule the managed side depends on: `lock_percent` and `missed_frames` of a stream result are **NULL, not an empty array**, when the stream has none.

**`dotnet/TmDq.Core.Tests`** (xunit v3, **124 tests, 1 skipped by design**) runs the committed recordings through the library:

- **The numbers are the application's.** The PRN figures are what the interop spike recorded before the de-Qt sweep (116 samples and 1,100 missed frames at one second, 11,554 and 838 at ten milliseconds); the others (step_cal, the randomized TRC recording, both modes) were taken from `PipelineRunner`, the code behind both applications, with a digest recomputed in the same order so equal inputs give equal floating-point results. Channel lists for all five recordings are the ones `TestChapter10Reader` pins.
- **The layout is pinned twice**: against the DLL's own `tmdq_sizeof`, and field by field to the same offsets `TestAbi` pins, so neither side can drift alone.
- **Every setting of a stream reaches the job** (sync mask, derandomize, invert, data rate, sample period, frame length, label, scale, polarity, slope, receiver count and channels), one at a time: the changed run must differ from the unchanged one. So must the file-scoped byte order and the application root's own word map. Calibration profiles are checked word for word (two profiles do what each does alone, a profiled word stays inside its profile's dB range and keeps the order of the samples).
- **The failure paths**: a rejected stream is a `Job` error with the core's own message, an unusable recording a `Prepare` error, an incomplete request `BadArgument`; an observer that throws does not break the run; cancel, dispose in flight, a result outliving its run, two runs at once, 30 disposed results not growing the process by their native size.
- **The state before anything is loaded** can be seen only in a process nothing has been loaded into, so `IsolatedLoadTests` runs a probe test in a **child process of the same executable** and requires it to pass: every call refuses with a message naming `Library.Load`, a text file is refused as "not a usable tmdq_core.dll" and leaves nothing loaded, and the real DLL can still be loaded afterwards. The probe is skipped in the parent run, which is the one skip.
- **Non-ASCII paths** work only because the test executable carries the UTF-8 active-code-page manifest (the vendored irig106 opens files with a narrow `open()`); a test asserts the code page, so a missing manifest fails loudly. **The SDK's `ApplicationManifest` embeds a manifest in the managed dll, not in the apphost exe that is the actual process**, so the build merges `app.manifest` into the exe with `mt.exe` after the build. And `AppendPlatformToOutputPath=false`, because with the default the SDK wrote to `bin\x64\...` while the script ran a stale `bin\Release\net10.0` exe built before the manifest step existed.

`scripts/test_dotnet.ps1` builds (warnings are errors) and runs it; CI runs it in both jobs after the DLL build (the hosted job installs the .NET 10 SDK with `actions/setup-dotnet`; the self-hosted runner already has it). **On the self-hosted runner the "Overlay full-size PRN fixture" step now runs after the managed tests, not before the build**: it replaces `prn_testfile, sample.ch10` with the 640 MB recording, and the managed tests pin the committed sample's numbers; the first CI run of this step failed two tests (184 samples instead of 116) before the step was moved. `tmdq.sln` now holds the two C# projects.

**Validated against 197 single-defect mutants of the library** (a runner that builds, runs the test exe and reads the result, restoring each file from a pristine copy and refusing to start unless the baseline is green; a watchdog kills a hung run, which is a catch). The first round caught 118, 16 failed to compile (also a catch) and **59 survived**, and they were real gaps, not noise: the library's load checks could not be tested in a process that has loaded the real DLL (so the checks became pure functions of plain values, tested with a DLL that reports the wrong thing, and the not-loaded behaviour moved into the child process above); **six settings of a stream (mask, invert, data rate, polarity, receiver count and channels) and the calibration profile pass-through were never varied**; the byte order and the application root were never exercised; a progress callback that always said 100 passed; the dispose checks covered only one accessor; the first-exception-wins rule was not pinned; and a result that was never freed natively was invisible. Closing them found **one real defect**: because the runtime allows one DLL resolver per assembly and the first `Load` bound it to its path, **a failed first `Load` made every later one throw** from the runtime itself. The resolver is now installed once and reads the candidate path, and the child-process test pins the retry. The second round re-ran the 60 that had not been caught plus three mutants of the new load code: 40 caught, 6 compile errors, 16 left, of which **one was a real gap** (the missing free, now caught by the memory test) **and 15 are equivalent**: freeing the GC handle or the native file struct (a leak the API cannot show, and the finalizer covers a result nobody disposed); a zero-count array passed as null or as an empty block (the native side ignores both); three struct-padding changes (a `Reserved` field and `Pack = 1` that leave every offset and size as they were); a zeroed allocation also zeroing its terminator and being non-null at size 0; and a last-error pointer the native side never returns as null.

**Left as it was, worth knowing:** a DLL that loads but does not match the binding cannot be unloaded and replaced (the runtime keeps the module it bound), so the process should report it and exit; `StreamConfig.ScaleDdBPerV` keeps the native field's name (`scale_d_db_per_v`); the `ObserverException` property exposes only the first exception.

Qt baseline unchanged: **1196 / 0 / 1** (`TestAbi` keeps its results; its bodies lost the version-1 size checks and gained the NULL-pointer assertions), **1197 / 0 / 0** at release, **37 suites**, from a clean build of both trees with zero warnings. Managed baseline **124 / 0 / 1** (123 run; the skip is the child-process probe).

#### The C ABI, part four: series construction - the ABI is complete (plan step 2.2d)

Last of the four PRs growing `native/tmdq_core/tmdq_abi.h` (revision 4). The data half of what `PlotViewModel` does with a processed stream now crosses the boundary (`abi_series.cpp`); the samples stay where the host keeps them (D3 and step 1.6: the core describes and decides, the host stores and presents). Translation only; no core logic changed.

- **A stream as series** - `tmdq_stream_result_series(result, base_abs_seconds, &set)` calls `buildStreamSeries`; `tmdq_series_set_info_get` gives the series count and the shared **elapsed axis** (seconds from the base), `tmdq_series_set_spec` each series: metric, name, stream label, receiver and channel index, stream order and sequence, source, the value array and its cached range (as Qt's `qMin`/`qMax` see a NaN), all as views. To make a view safe, **a result now shares its data** (`std::shared_ptr<const ProcessedStreamData>`): the series set keeps the stream's data alive, so the host may free the result first (pinned).
- **Stream identity** - `tmdq_stream_result_key` and `tmdq_stream_key_equal`: the same stream run again is the same only if source, label and channel all agree.
- **The shared time base** - an opaque `tmdq_time_base` (create / free / info / reset / set_clock) with `overlaps`, `adopt` and `advance`, which return how far every existing series must shift (right when an earlier source arrives, left when the earliest goes), exactly `SeriesTimeBase`.
- **Imported CSV streams** - `tmdq_assign_imported_stream_orders` gives each lock and missed-frames series of an imported CSV the order of its stream; it validates the whole list first and writes only `stream_order`, so a refused list is left alone.

**Tests** (`TestAbi` grew by 4, all against the core called directly): the five new structs' sizes and offsets and their id numbers; 1,500 random processed streams (lock only, with missed frames, with receiver channels named in the awkward ways step 1.6 found, NaN and infinity, empty arrays, non-ASCII labels, several sources) built through both, every spec field and array compared, with reach thresholds, plus a known stream with hand-checked numbers and the free-the-result-first case; 700 random sequences of 14 time-base operations compared after every step (state and return values); stream keys differing in each field; 1,500 random imported-stream lists compared, including whether each lock series has an SNR sibling in its source.

**Validated against 57 single-defect mutants.** 55 caught at once; one survivor was a real gap (only two of the five new struct-id numbers were pinned) and is closed; the other cannot be observed: `tmdq_time_base_create` clears its output before allocating, and the only way that matters is an allocation failure. (The first run of the new tests also failed its own reach thresholds - the generators rarely made the time base overlap and counted lock series without a sibling wrongly - and were re-aimed; the comparisons themselves matched the core from the first run.)

**With this step the C ABI covers everything plan step 2.2 listed** - probe a file, run N streams of either mode with calibration profiles, progress/log/error callbacks, cancel, results, calibration extraction and step files, the TOML helpers, CSV import and export, job building (inside the run), series construction - at 5 source files (`abi_file`, `abi_run`, `abi_calibration`, `abi_formats`, `abi_series`) over `abi_internal.h` / `abi_events.h`, and 67 exports. The spike's version-1 functions remained until plan step 2.3a replaced their driver.

Baseline **1196 / 0 / 1** (was 1192 / 0 / 1; `TestAbi` adds 4 results), **1197 / 0 / 0** at release. **37 suites.**

#### The C ABI, part three: the formats (plan step 2.2c)

Third of four PRs growing `native/tmdq_core/tmdq_abi.h` (revision 3). The file formats and small pure functions stage 1 moved into the core now cross the boundary, so the C# application reads and writes exactly the bytes the Qt one does (plan rule 5: one implementation of every format). No core logic changed; this is translation (`abi_formats.cpp`) and tests.

- **The frame-sync pattern file** - `tmdq_frame_sync_file_read` / `_info_get` / `_free` and `tmdq_frame_sync_file_write`. A missing, empty or unreadable file reads as a file that says nothing (the application's behaviour), so reading fails only for a NULL argument. The info carries the pattern and mask text (views into the handle), the frame length with its has-flag and whether it came from the older `WordsInMinorFrame` key, `Inverted`, and `Randomized` with its has-flag (older files, the shipped PRN patterns included, have no key and must not clear the operator's choice). An absent optional reads as 0 beside a has-flag of 0, pinned. Writing refuses a NULL path and reports a path that cannot be written as the new **`TMDQ_ERR_IO`** (8).
- **Template matching** - `tmdq_template_match` / `_info_get` / `_free`: exact-set comparison of a template's channel ids with a file's (order and repeats do not matter, two empty sets match), returning `ok` and the sorted `missing` and `extra` ids as views.
- **CSV import** - `tmdq_csv_parse` returns a handle (`tmdq_csv_info_get`: series count, base day, base time of day, skipped rows, x max; `tmdq_csv_series_get`: metric, name, stream label, receiver, channel index, stream order, source, y range and the x and y arrays as zero-copy views). A file the parser refuses (unreadable, wrong header, no parseable sample) is `TMDQ_ERR_PREPARE`, since the core gives no finer reason.
- **CSV export** - `tmdq_csv_write(path, columns, count, base_day, base_time_offset)` takes `count` pointers to versioned `tmdq_csv_column` structs (metric, name, source, and x and y arrays with their own counts) and the clock origin, and calls `CsvSeriesWriter::write`: CR LF, UTF-8 without a BOM, six decimals, one row per distinct millisecond. A NULL name reads as empty; an unknown metric, a negative count or an array missing for a positive count is refused naming the column.
- **Clock stamps** - `tmdq_clock_time_format(base_day, base_time_offset, elapsed, include_ms, buffer, capacity)`, snprintf-style, for the plot's axis and readouts.

**Tests** (`TestAbi` grew by 6, every one against the core called directly): the five new structs' sizes and every offset, `TMDQ_ERR_IO`, the metric numbers (also `static_assert`ed against `MetricType` in the source); the three shipped pattern files and eleven odd ones (legacy key, too-small lengths, both keys, no `Randomized`, a BOM, non-ASCII text, empty values, an empty file, junk, a missing file) read the same through both, and the same arguments written through both give **byte-identical files** that read back alike; template matching over eight fixed cases and 2,000 random sets with repeats (with reach thresholds); the four committed CSV fixtures plus a generated file and one with malformed rows parsed through both field for field (and the files the parser refuses); a CSV written through both from columns that exercise the writer (every metric, two sources, non-ASCII, several times in one millisecond, a column with more times than values, an empty column), **byte-identical**, then parsed back; 3,000 random clock stamps and a known one; every refusal and bad argument.

**Validated against 73 single-defect mutants** of `abi_formats.cpp`, the header and the size table. First run: 66 caught, 6 survived, 1 failed to compile (a swapped metric number, which the source's `static_assert` rejects - also a catch). Three survivors were gaps in the fixtures and are closed: no committed CSV had a malformed row (so `skipped_rows` was never nonzero) and none had an SNR header carrying a stream id (so `stream_order` was always 0) - both now in the generated files, with a check that the files really contain them - and the raw value of an absent `Randomized` was not pinned. **Three are not defects**: the writer uses the shorter of a column's two sample counts, so swapping the counts is invisible in the output (an equivalent mutant); one "mutant" was a deliberate no-op control; and one chose two structs of the same size (replaced by a pair that differ, caught).

**Left as it was, worth knowing:** the CSV format has no quoting, so a series name containing a comma cannot be written and read back (the first version of the test used one; the format is unchanged and the test avoids it). `tmdq_csv_parse` cannot say *why* a file was refused - the core does not.

Baseline **1192 / 0 / 1** (was 1186 / 0 / 1; `TestAbi` adds 6 results), **1193 / 0 / 0** at release. **37 suites.**

#### The C ABI, part two: step files and calibration extraction (plan step 2.2b)

Second of four PRs growing `native/tmdq_core/tmdq_abi.h` (revision 2). It carries the Extract Calibration workflow across the boundary: read a step file, assemble the extractor's request, run the extraction with callbacks and cancel, and read each channel's outcome and profile.

**Step-file parsing was already in the core** (`StepDetector::parseStepConfig`); what the plan took for view logic was the *request assembly* the dialog did inline. That is a rule about the data, so it moved too:

- **`buildCalibrationRequest(CalibrationInputs, appRoot, Request&, error)`** (`include/model/calibrationrequest.h`, `src/model/calibrationrequest.cpp`, Qt-free, in `kCoreSources` and the DLL project). It decodes the sync text exactly as the main run does (`resolveFrameSync`: " FE6B " is 24 bits), refuses an unreadable pattern or mask with the dialog's own words ("Invalid frame sync pattern '...'." / "...mask..."), carries every other field, and, when no word-map file is named, uses `<appRoot>/settings/receiver_params/default.toml` if it exists. The two passes (main run, extraction) must agree on word indices or profiles attach to the wrong channels - that is the reason the rule is written once. A failed build leaves the request alone. `CalibrationSetupDialog::maybeRunExtraction` now calls it.
- **Pinned against the old code.** `TestCalibrationRequest` keeps the dialog's inline code verbatim (QString and `QFileInfo` included) and runs it beside the new function over 3,000 generated inputs (patterns, masks, flags, rates, named and default word maps, four application roots, steps, clips), requiring the same acceptance, the same message and the same request field for field, and that the generator really reached the accepted, refused and default-map branches. **No difference.** `TestStreamConfigDialog`'s extraction tests (the dialog end to end) pass unchanged.
- **`CalibrationExtractor::wait()`** (the one method the ABI needed that the extractor lacked; `PipelineRunner` already had it) joins the driver thread; pinned in `TestCalibrationExtractor` for an idle extractor, one that failed before starting, a full run and a cancelled one.

**The ABI** (`abi_calibration.cpp`; `CallbackEvents` moved to a shared `abi_events.h`):

- `tmdq_steps_parse` / `_count` / `_db` / `_free`: the step list from a file (a missing or empty file is `TMDQ_ERR_PREPARE` carrying the parser's message). `tmdq_step_config_initial_path` writes the dialog's preloaded step file into a caller buffer, **snprintf-style** (the length needed is returned, a short buffer gets a terminated truncation) - the convention for text with no handle to own it, recorded at the top of the revision-1 conventions.
- `tmdq_calibration_start` takes the request (the dialog's fields, plus the application root), checks it through `buildCalibrationRequest` (an unreadable sync is `TMDQ_ERR_JOB` with the dialog's message) and starts the extractor. The existing `tmdq_callbacks` is reused with `stream_done` never called. A failure the extractor sees before any thread starts (no steps, a recording it cannot open) arrives as `error` then `finished(0)` **before `start` returns**, and the handle is returned already finished - the extractor's own contract, kept rather than hidden. `cancel` / `running` / `wait` / `free` as for runs (free cancels and waits).
- Results: `tmdq_calibration_result` (summary line, error, the operator-facing **report** from `CalibrationExtractor::summarize`, recording length, whether the clips were ignored) and `tmdq_calibration_channel_result` (name, word, validity, had-data, outcome, plateau counts, thresholds, the profile's points and the plateau and unresolved arrays as zero-copy views). Both return the new **`TMDQ_ERR_STATE`** while the extraction runs, since the extractor's results are only valid once it has finished. A profile's points are handed out exactly as they lie in memory, which `static_assert`s on `CalibrationPoint` / `tmdq_cal_point` enforce, so the host passes `valid` channels straight on to `tmdq_run_start`.

**Tests** (`TestAbi` grew by 11): the three new structs' sizes and every offset, the new enum numbers; the step file against the core and each failure; the initial path measured, written exactly, truncated and refused; a full calibration against the extractor run directly (all 48 channels, every field, the report, the callbacks' order) and its profiles fed back into a run, compared with the core's; **every setting of a calibration varied one at a time** (randomized, inverted, byte order, frame length, data rate, sync mask, PCM channel, both clips, clips beyond the recording, step count, receiver count and channels with no word map, a named word map, an application root with its own default) with the requirement that the core's result changes first; the 4-step run that makes channels need a **relaxed threshold** (the one flag the ordinary run never sets); refusals and bad arguments; a calibration that cannot start; results refused while running and cancel; free in flight; `wait` alone.

**Validated against 91 single-defect mutants** of the new core function, the ABI, the text helper, `wait()` and the header. First run: 86 caught, **3 survived**. Two were real (the clip-ignored flag, which no row set; and the relaxed flag, which no fixture run ever raised) and are closed by the two cases above. **One is equivalent**: the PCM recording's *time channel* does not affect a calibration - the extractor runs on the data-rate clock, so even a nonexistent id gives identical results - and so "the ABI forgot to carry it" cannot be seen in any output (it is carried).

**Left as it was, worth knowing:** `TMDQ_ERR_STATE` and the snprintf convention are new vocabulary the managed side must mirror in step 2.3; the host-callback exception swallowing of `CallbackEvents` applies to calibration too.

Baseline **1186 / 0 / 1** (was 1166 / 0 / 1; `TestAbi` adds 11 results, `TestCalibrationRequest` 8, `TestCalibrationExtractor` 1), **1187 / 0 / 0** at release. **37 suites.**

#### The C ABI, part one: conventions, the file, and runs (plan step 2.2a)

First of four PRs that grow `native/tmdq_core/tmdq_abi.h` from the spike's two calls to everything the application needs (2.2b calibration, 2.2c the formats, 2.2d series construction). This one settles the conventions every later part follows, and carries the two capabilities a host cannot do without: reading a recording's channels and running streams.

**Conventions** (written at the top of the header's revision-1 section; each is pinned by `TestAbi`):

- **Opaque handles** (`tmdq_file`, `tmdq_run`, `tmdq_stream_result`), returned through an out parameter, released by the matching `_close` / `_free`. Never two owners.
- **Versioned structs.** A struct the host fills in starts with `uint32_t struct_size`; fields are only ever appended. A library newer than the host reads the fields the host knew and defaults the rest; a struct *shorter* than the first revision is refused (`TMDQ_ERR_BADARG`, naming `struct_size`). A struct the library fills in is written no further than the host's own `struct_size`. `abi_internal.h`'s `readStruct` / `writeStruct` do this once, for every struct.
- **Version and revision.** `TMDQ_ABI_VERSION` stays 1 (nothing existing changed meaning, so the spike's v1 calls still work); the new `TMDQ_ABI_REVISION` (1) counts additions. `tmdq_sizeof(id)` reports every struct's size for the managed side to assert against `Marshal.SizeOf`.
- **Strings** in are UTF-8, copied before the call returns; NULL means "not given". Strings and arrays out are **views** into the handle they came from (no copy), valid until it is released.
- **One error per thread** (`tmdq_last_error`), cleared by every call that can set it, and **no exception crosses the boundary**: `tmdq::guarded` turns one into `TMDQ_ERR_INTERNAL` with the reason.

**The file.** `tmdq_file_open` reads the TMATS (through `Chapter10Reader`, which serialises its own setup) and the handle lists time and PCM channels (`id`, bare TMATS name, and for PCM the declared data rate) in the order the application lists them, plus the default time channel. This needed one reader accessor, `Chapter10Reader::getTimeChannelList()` (the combo list without its "<id> - " spelling), pinned in `TestChapter10Reader`. There is **no time range**: the application has none (its summary line says "Time range: all"), so the plan's mention of one is not exposed; the request struct can grow it.

**Runs.** `tmdq_run_start(request, callbacks, &run)` takes the recording, the time channel, the file-scoped byte order, the application root and N stream configs (the fields of the core's `StreamJobConfig`, calibration profiles included), builds each job with the core's own `buildStreamJob` (so every rejection message is the Qt application's, and arrives as `TMDQ_ERR_JOB`), and starts the core's own `PipelineRunner`. It reports through function-pointer callbacks: `log` (the job builder's lines first, on the calling thread, before `start` returns), `error`, `progress` (reader thread only), `stream_done` (each stream's thread; ownership of the result passes to the host) and `finished` (once, last, after every thread has ended). `tmdq_run_cancel` / `_wait` / `_free` (free cancels and waits, so a callback target can be destroyed straight after). A result is read with `tmdq_stream_result_info` / `_channel`, which point into it. A callback that throws is swallowed rather than allowed to unwind through the core's threads.

**Tests first.** `TestAbi` compiles the ABI sources into the test executable (`TMDQ_ABI_STATIC`, so `TMDQ_API` is empty) and calls them as a host would. It pins every struct's size and field offsets, the numeric value of every enum, the version and revision; the file listing against `Chapter10Reader`; and runs against an **oracle**: the same configs built and run directly through `buildStreamJob` + `PipelineRunner`, compared sample for sample over a lock stream and a receiver stream together. It then varies each setting of a stream one at a time (sync mask, randomized, inverted, data rate, sample period, scale, polarity, slope, receiver count and channels) and requires first that the core's result *changes* (so the row proves something) and then that the ABI's equals it; does the same for the file-scoped byte order, a named word-map file, the application root present and absent, duplicate and invalid calibration profiles, NULL optional text and the `log` lines. It checks the documented order and threads of the callbacks, cancel, free-while-running, a result outliving its run and freed from another thread, a throwing callback, two runs at once, every bad argument and each struct-size case. `TestCoreQtFree` gained the ABI sources: Qt-free and layer-clean through their includes (`abiSourceProblems`), listed in the project, in `tests.pro` and on disk alike (three-way), and `native/` is searched for unlisted Qt like `src/` and `include/`.

**Validated against 106 single-defect mutants** of the ABI sources, the header and the guards. The first run had 18 survivors and they were real: ten fields of a stream config that no test varied, the job builder's log lines, the reader's failure reason (only checked for being non-empty), the exception guard's messages (a test could not see them), and a stale-dependency artefact (`tst_abi.cpp` was missing its header dependency until `qmake` was re-run, so header mutants did not rebuild it - the new-header trap again). They were closed with the per-setting test, the log test, an exact comparison with the reader's message, a direct test of `guarded`, and a test of `readStruct` with guard bytes. Two checks were redundant (`index < 0` is covered by the unsigned comparison that follows) and were removed rather than tested. Accepted as equivalent: freeing a result or a file does nothing observable through the API (a leak cannot be told from a free); one header mutant fails to compile, which is also a catch.

**Left as it was, worth knowing:** the spike's v1 functions (`tmdq_probe_file`, `tmdq_run_lock_stream`) remain, untouched, for the spike's driver; they go with the next incompatible version, when 2.3's C# library has replaced that driver. The static test build cannot tell a UTF-8-aware path conversion from the process code page (the test executable carries the UTF-8 manifest, a managed host does not unless it merges one), so non-ASCII paths are checked by the hybrid driver and, from 2.3, by the C# tests.

Baseline **1166 / 0 / 1** (was 1134 / 0 / 1; `TestAbi` adds 29 results, `TestCoreQtFree` 2, `TestChapter10Reader` 1), **1167 / 0 / 0** at release. **36 suites.**

#### The core builds as `tmdq_core.dll` with no Qt, in CI (plan step 2.1)

First step of stage 2 of `docs/dotnet-migration/avalonia-plan.md`: the Qt-free core, which stage 1 finished, is now
also built as a product, by a build that has no Qt to reach for.

- **`native/tmdq_core/tmdq_core.vcxproj`** (MSBuild, in the new committed **`tmdq.sln`** at the repository root)
  compiles every `src/model` source in `TestCoreQtFree`'s `kCoreSources` (18 files), the ABI
  (`native/tmdq_core/tmdq_core.cpp`, `tmdq_abi.h`) and the 13 vendored irig106 C files, into a DLL. It has no Qt
  include path and no Qt library, so a core source that reached for a Qt header would not compile - the thing
  `TestCoreQtFree` infers from the include closure is here a property of the build. The compiler switches mirror
  what qmake gives the app (C++20, `/utf-8`, `/permissive-`, level 3 with the same promoted warnings, `UNICODE`;
  irig106 with warnings off, as `thirdparty.pri` does), plus `/FS`, which parallel compiles into one PDB need
  (without it the build failed intermittently with C1041). The toolset is whatever MSBuild brings (v143 from the
  VS 2022 tools `env.ps1` pins, v145 from VS 2026); intermediates go in `native/obj/<toolset>/<config>/` so two
  compilers never share objects. Output is `native/out/<config>/tmdq_core.dll` (both gitignored).
- **`scripts/build_core_dll.ps1 [-Configuration Debug|Release] [-Rebuild]`** builds the solution and then checks
  it: **zero compiler warnings**, and the DLL's imports (`dumpbin /dependents`) must all be on an **allow-list**
  (the C runtime, `KERNEL32`, `WS2_32`, `USER32`). An allow-list rather than "no Qt", so any new dependency has to
  be added on purpose. It deletes the DLL first, so a stale one cannot pass. Release is 377,344 bytes importing
  `MSVCP140`, `VCRUNTIME140`, `VCRUNTIME140_1`, `KERNEL32` and the UCRT forwarders; Debug is 2.5 MB. Works under
  Windows PowerShell 5.1 (CI's shell) and 7; dot-sources `env.ps1` itself when `msbuild` is not on PATH.
- **CI builds both** (`ci.yml`, both the self-hosted and the hosted job, after the app and test builds): Debug and
  Release through that script. The Qt app still compiles the same sources through qmake until step 6.
- **The two source lists cannot drift.** `TestCoreQtFree::theNativeProjectCompilesExactlyTheCoreSources` compares the
  project's `src/model` sources with `kCoreSources`, its irig106 files with the ones `tmDataQualityAnalyzer.pro`
  compiles, rejects any file from another layer, requires that the project never says "Qt" outside its comments
  (in any case: `qt6core.lib`, `QT_CORE_LIB`, `C:\Qt\...`), and that `tmdq.sln` builds it. The check is one function
  (`nativeProjectProblems`), proved on temporary trees by `theNativeProjectGuardSeesEveryKindOfDrift` (a missing
  core source, an extra one, three other layers, a missing/extra/absent irig106 file, Qt three ways, a solution
  without the project, an unreadable project) and its readers by `theNativeProjectGuardReadsProjectFilesCorrectly`.
- **The ABI is the spike's, unchanged** (version 1, one Frame Sync Lock stream per call), moved here from
  `dotnet/hybrid-spike/native/tmdq_core.cpp` and `dotnet/interop-spike/tmdq_abi.h`; step 2.2 grows it. The spike
  directories keep their own copies and are frozen as the record of what was measured. **Their driver is the
  numeric check for this step:** `dotnet/hybrid-spike/driver` (20 checks against the numbers recorded before the
  de-Qt sweep: 116 samples / 1,100 missed at 1 s, 11,554 / 838 at 10 ms, two streams at once, a non-ASCII folder)
  passes 20 of 20 against the MSBuild-built DLL in both Debug and Release. Step 2.3 makes that check permanent.

**Why not the plan's `tmDataQualityAnalyzer.sln`.** That name belongs to the gitignored, generated Visual Studio
solution (`scripts\gen_vs_project.ps1`, which regenerates it on demand and would overwrite a committed one), so the
long-term solution is **`tmdq.sln`**. Later steps add the C# projects to it.

**Validated against 26 single-defect mutants, all caught:** nine plant drift in the project or solution (a core source
dropped, a UI-session source and a ViewModel source added, an irig106 file dropped and one added, a Qt include
directory, a lower-case `qt6core.lib`, a Qt define, the project dropped from the solution) and seventeen break the
guard itself (an `Update=` item counted, backslashes not read, extension or folder not checked, comments not
stripped, Qt matched case-sensitively, `.cc` taken for `.c`, each report removed, each layer, the irig106 list,
the solution check, an unreadable project passing). The first run had **five survivors, all the same kind**: a check
that no test exercised on its own (the `.pro` comparison, the core-list comparison, the solution, the Qt-free
rule, and the case-insensitive match), because the only tests ran on a tree that was already correct. Folding the
checks into one function and driving it over a tree with each fault fixed all of them. The build script's gates were
checked by hand: an allow-list missing `KERNEL32` fails, a planted C4101 warning fails with "1 compiler warning(s)".

**Left as it was, worth knowing:**

- The DLL is built `/MD`, so a host needs the Visual C++ runtime installed (or beside it). Whether to link it
  statically is a deployment decision for step 5.
- A managed host must run with the **UTF-8 active code page** or a non-ASCII recording path cannot reach irig106
  (the vendored library opens files with a narrow `fopen`). `dotnet.exe` and the test host do not carry that
  manifest; the Qt app's exe does (`codepage.pri`). The hybrid driver merges one into its exe; step 2.3's test host and
  step 3's app must do the same.
- The hosted CI job builds with whatever Visual Studio `windows-latest` has; the toolset follows it.

Baseline **1134 / 0 / 1** (was 1131 / 0 / 1; `TestCoreQtFree` adds 3 results), **1135 / 0 / 0** at release. **35 suites.**

#### Outside the View, Qt is confined to a named list, and the guard enforces it (plan step 1.7, end of stage 1)

Last step of stage 1 of `docs/dotnet-migration/avalonia-plan.md`: "DTO cleanup". Steps 1.1-1.6 had already
moved every piece of logic the DTOs fed - the core takes plain copies (`StreamJobConfig`, `CsvSeries`,
`ImportedSeries`, `SeriesSpec` ...), so nothing in it reaches `StreamConfig`, `FrameSyncParams`, `SeriesAppearance`
or `PlotSeriesData`. Converting those DTOs themselves to `std` would change nothing the core depends on and would
mean rewriting every dialog and the plot twice, once now and once in C# in step 4. **They stay Qt on purpose**: they
are the UI's own types, and step 4 replaces them. (The plan item's wording, "`SeriesAppearance` colour as a plain RGB
value", would also have left `QString`/`QHash` in the same header.)

What the step does instead is make the end state of stage 1 **a thing the suite enforces rather than a thing
that happens to be true.** `TestCoreQtFree` grew three tests:

- **`outsideTheViewQtIsConfinedToTheListedFiles`** - every `.cpp` and `.h` under `src/` and `include/` outside
  `src/view` and `include/view` is searched for a Qt include (`<Q...>`, `<Qt...>`) **or a forward declaration of a Qt
  class** (`class QThing;` / `struct QThing;` - the two schema headers reach Qt that way, with no include). A file that
  does it must be on `kQtOutsideTheView`; a file on the list that no longer does fails as stale, so **the list can only
  shrink.** It holds `main.cpp`, the four ViewModels, the two JSON serialisers (`processingtemplateschema`,
  `streamconfigschema`), six UI-form DTOs and three Qt bridges (`plotconstants.h`, `qtbridge.h`,
  `qtprocessingevents.h`).
- **`everyOtherHeaderIsQtFreeThroughAndThrough`** - a header can reach Qt through a DTO it includes, which the direct
  search cannot see. Every header outside the View that is not on the list must have no Qt and no ViewModel or View
  header anywhere in its include closure.
- **`theConfinementGuardSeesAnUnlistedAndAStaleFile`** - proves the guard on a temporary tree: an unlisted file, a
  stale entry, a View file (exempt), a look-alike (`<Quux.h>`, `class Quark;`), a comment, a forward declaration in a
  header, and a header that has no Qt but reaches up into the View.

The existing shared-header self-test also gained class and struct forward declarations.

**Where this differs from the plan's "Done when".** The plan says the only Qt left outside `src/view` is the four
ViewModels and the two schemas. The list is longer: the UI-form DTOs and the bridges are Qt too, because the dialogs,
the plot and the schemas are written in terms of them. They are presentation types; nothing below them depends on them.

**Validated against 25 single-defect mutants.** Six plant a violation in the real tree (a Qt include in a model header,
Qt reached through a DTO, a forward declaration in a header and in a core source, a plain DTO that starts using Qt, a
top-level helper header that includes Qt); fourteen break the guard (a weakened Qt-header test, the View's exemptions,
headers, sources or subfolders not searched, forward declarations or structs ignored, an unlisted or stale file not
reported, the closure test skipped or neutered, layer violations not looked for); five edit the list. **22 were caught
at once; the three survivors were holes in the guard's own self-test** (struct forward declarations, forward
declarations in the walk, the header layer check - each unproven because no test tree contained one) and are closed.
It also corrected two stale header comments (`framesyncparams.h` and `plotseriesdata.h` named users that moved off them
in earlier steps).

Stage 1 is complete: the core is Qt-free, the list of what is not is short, named and cannot grow unnoticed.

Baseline **1131 / 0 / 1** (was 1128 / 0 / 1; `TestCoreQtFree` adds 3 results), **1132 / 0 / 0** at release. **35 suites.**

#### Series construction and the shared time axis are in the Qt-free core (plan step 1.6)

Sixth step of stage 1 of `docs/dotnet-migration/avalonia-plan.md`: the data half of `PlotViewModel::addStreamData`,
`removeSource` and the CSV import's commit.

**What moved** (`include/model/seriesbuilder.h`, `src/model/seriesbuilder.cpp`, no Qt, listed in `TestCoreQtFree`):

- **`buildStreamSeries(data, baseAbsSeconds)`** turns a processed stream into the series it yields: lock %, then
  accumulated missed frames, then one per receiver channel, each with its name (the stream label, or the SNR name),
  its receiver number (the number after the last `_RCVR`, 0 when there is none or it is not a number), its channel
  index within its receiver, its stream order, job index and source, and its cached value range (with `qMin`'s
  treatment of NaN). It also returns the elapsed axis. The series' values are `std::span` views of the processed
  stream, not copies.
- **`SeriesTimeBase`** is the clock the shared elapsed axis hangs from: `adopt` (a stream arrives: the base is its
  first sample if nothing is loaded, else it moves only earlier, and the call returns how far existing series must
  shift right), `advance` (a source was removed: the base moves up to the earliest remaining sample, and the call
  returns how far to shift left), `overlaps` (whether a new source touches what is loaded; touching at one instant
  counts), `setClock` (a CSV states its own day and time of day), and the day and time of day of the base.
- **`assignImportedStreamOrders`** gives an imported CSV's lock and missed-frames series the order of their stream:
  that of the SNR series with the same label in the same source, else a fresh one above every order in the file.
- **`StreamKey`** (`streamKeyOf`) is the identity rule for "this stream run again": source, label and channel
  together.

**What did not move, and why.** The sample storage stays in `PlotViewModel`'s `QVector<PlotSeriesData>`. The plot
and the chart rely on `QVector`'s implicit sharing - one elapsed array per stream, referenced by each of its series
and by the chart - and putting the samples in the core would copy every one. So the core works on views and returns
values; the plot converts them once, building ONE elapsed buffer per stream and sharing it, and keeps colours,
visibility, axis ranges, sorting by job index and the signals. How the C# application holds its series is for
stage 3 to decide; this is the logic it will call. The plan's wording ("building series ... into the core") is met
for the logic, not for the container.

**Tests first, on the old code.** A new suite, `TestSeriesBuild`, pins 77 scenarios through `PlotViewModel`, each as
one line: the time base, the span, the flags, the warnings, and every series' identity, indices, cached range and
samples. They cover what a stream becomes (lock only, with missed frames, a receiver stream with and without lock
series, empty and non-ASCII labels); the receiver number in every spelling that has come up (`X_RCVR2_RCVR3`,
`L_RCVR 7`, `L_RCVR+4`, `L_RCVR-2`, no number, overflow, non-ASCII); channel index counting; sample arrays that do not
line up; NaN and infinity; unsorted and repeated times; legend order by job index; running a stream again (what
replaces what, and that the base can move later when everything is replaced); re-basing across sources, including
by fractions of a second; the overlap rule at every boundary; the base day and time of day at the epoch, before it,
across a year and a leap day and far in the future; removing the earliest, a later and the only source; ids
continuing after a removal or a clear; and the order given to an imported CSV's streams. They passed on the old code
before anything was ported, and **all 77 held after the port with no change.**

**An oracle for the rest.** The old data logic is kept in the test as a small class, verbatim, and run beside the new
`PlotViewModel` over 700 random sequences of adds, removals and clears (streams from a pool of labels, channels,
sources and job indices; times at the epoch, a day boundary, a leap day, far in the future and in fractions;
mismatched array lengths; the awkward receiver names; NaN and infinity in the frame-sync series), comparing the whole
state after every step. A second oracle compares the old import ordering with the core's over 4,000 generated series
lists. **No difference.** Direct tests check that a spec's values are views of the processed data (the same pointer),
and walk the time base through a first stream, a later one, an earlier one, a replacement, the overlap boundary, a
removal and a reset.

**A trap the generator found.** Qt 6.12 asserts in a debug build when `qFloor` gets a non-finite or enormous number, and
`computeYRange` rounds an SNR range to whole decibels. Infinite or 1e300 values in a receiver channel therefore crash a
debug test run. Nothing real produces them (a calibrated value comes from a 16-bit count), so the generator keeps
channel values finite and puts the odd ones in the frame-sync series, whose range that code skips. The code is as it was.

**Validated against 101 single-defect mutants; 100 were caught at once and the survivor is closed.** They cover the
elapsed axis, which series exist, every field of a series, the metric swaps, the range, the SNR name, the receiver
parsing (first or last token, the offset, the failure value), the channel index, every branch of the clock (the
overlap boundaries and the `&&`, adopting later or earlier or never, the shift's sign, the clock recomputed or not,
advancing, each term of the day and time of day, reset), the import ordering (SNR series left alone, unlabelled series,
the source in each key, first or last label winning, how fresh orders start and advance), the stream key, the plot's
side (what a re-run replaces, the base judged before or after the replaced stream goes, the warning, the shift,
sharing the elapsed buffer, each field copied, the visible metric, removal, clearing, the import) and the Qt-free guard.
**The survivor** was `SeriesSpec::streamLabel`: the plot set a series' label from the stream data directly, so the
core's copy of it was never read. The plot now takes it from the spec, which left a local variable unused and removed.

**Left as it was, worth knowing:** the base never moves later when a stream is replaced - only when every series is
gone, or a source is removed - so a re-run that starts later leaves the axis starting at the old base (pinned as
`reprocessing-the-first-stream-can-move-the-base-later`); an import sets the day and time of day but leaves the absolute
base alone; and sample arrays that do not line up with the times are kept as they are, not trimmed.

Throughput is untouched (the plot is not on the decode path), so the Release gate was not rerun.

Baseline **1128 / 0 / 1** (was 1045 / 0 / 1; `TestSeriesBuild` adds 83 results), **1129 / 0 / 0** at release. **35 suites.**

#### Turning a stream's settings into a job is in the Qt-free core (plan step 1.5)

Fifth step of stage 1 of `docs/dotnet-migration/avalonia-plan.md`, and the largest: the validation and
construction that `MainViewModel::buildStreamJob` did, between the user's per-stream settings and the job the
pipeline runs.

- **`buildStreamJob(StreamJobConfig, base, StreamJobContext, StreamJob&, StreamJobReport&)`**
  (`include/model/streamjobbuilder.h`, `src/model/streamjobbuilder.cpp`) takes plain values - UTF-8 text, a
  `std::filesystem::path`, a `std::unordered_map` of calibration profiles - and returns the job, or the reason for
  rejecting it, plus the lines for the processing log. It reports instead of emitting, so it needs no signal. The
  context carries what is true of the whole recording and installation rather than of one stream: the application
  root (where `settings/receiver_params/default.toml` is looked for) and the file-scoped byte order. Listed in
  `TestCoreQtFree`'s core list.
- **`MainViewModel::buildStreamJob` is the adapter**: it copies a `StreamConfig` into a `StreamJobConfig`,
  calls the core, and relays the error and log lines. Everything else stays where it was; the plan's wording
  ("taking a plain `StreamConfig` value") is met by a plain copy of it, because `StreamConfig` itself still
  carries `QString` and `QHash` for the dialogs and templates.
- **The checks and their order are unchanged:** the sync pattern, then the mask, then the frame length; for a
  Receiver SNR stream the receiver-parameters file (named, else the shipped default, else a default word map),
  then the scale, then the slope index. Every message leads with the stream's label, or `Ch <id>`.

**A defect found and fixed on purpose.** When no receiver-parameters file is named, a job uses the shipped
`default.toml`, and falls back to a default word map if that file will not load. A rejected load can leave the
words it did read in the table, and the fallback **appended** to them: a `default.toml` that was readable up to a
word outside the frame produced a job with seven parameters, `L_RCVR1` twice. It would run, with a word map nobody
wrote. `FrameSetup::saveReceiverParamsFile` already cleared the table first (its comment says why); building a job
did not. It now does. The pin that recorded the old behaviour (`default-file-partly-outside-the-frame`) carries a
comment naming the change, and the oracle in the test is the old code with that one line marked.

**Tests first, on the old code.** A new suite, `TestStreamJob`, pins 59 cases through
`MainViewModel::buildStreamJob`, each as one line saying whether a job was built, the message, the log, every
worker parameter the settings decide, and the whole frame-parameter table with its calibration. The existing
`TestMainViewModelHelpers` cases covered the lock-only path and one explicit receiver file; these cover what they
did not: every valid and invalid slope index and the arithmetic each gives, both polarities, the scale at its
edges, an explicit file that is missing, a folder, outside the frame, empty, metadata-only or not TOML, the word
boundary and the rounding up of the frame's word count (800 bits rejects word 50, 801 bits accepts it), the default
file and the default word map in each state (good, partly usable, metadata-only, garbage, absent, too big, no
receivers, a product that overflows), the profiles attached, unattached and invalid, the order the checks run
in, and non-ASCII labels and file names. They passed on the old code before anything moved, and **58 of the 59
held after the port unchanged**; the other is the fix above. The 51 existing cases were not touched.

**An oracle for the rest.** The old function is kept in the test, verbatim apart from its inputs and the one marked
line, and compared with the adapter over 1,500 generated configurations (modes, patterns, masks and frame lengths
good and bad, every receiver-file fixture, every default-file state, counts that do and do not make a map, profiles,
non-ASCII labels), the generator asserted to reach each outcome in volume. **No difference.** Two direct tests of the
core check that the application root, which arrives as UTF-8, finds the default file under a non-ASCII folder, and
that a rejection leaves the log empty and a success the error.

**Validated against 76 single-defect mutants; all 76 were caught.** They cover the description and every message, the
frame-length boundary and the word count, every acquisition field and the byte order (taken as on, and inverted),
each of the four sample periods, lock-only taking the receiver path or having no setup, every wrong default path,
the partial map kept (the defect above), a named file ignored or its absence not noticed, the wrong frame size to the
loaders, an empty map accepted, the default map's arguments swapped, the scale and slope boundaries, the calibration
arguments, the profile attach, test and log lines, every field the adapter copies, the error and log relayed or decoded
as Latin-1, and the Qt-free guard.

**Left as it was, worth knowing:** a scale of NaN passes the `<= 0` check and fills the table with NaN slopes (pinned as
`scale-not-a-number`); no setting can produce one today, since the dialog's spin box cannot and the template format cannot
encode it. A slope index outside the table is rejected here, but `applyLinearCalibration` itself
indexes by it unchecked, so a new caller has to validate first.

Throughput is untouched (building a job is not on the decode path), so the Release gate was not rerun.

Baseline **1045 / 0 / 1** (was 981 / 0 / 1; `TestStreamJob` adds 64 results), **1046 / 0 / 0** at release. **34 suites.**

#### The frame-sync pattern file is in the Qt-free core (plan step 1.4)

Fourth step of stage 1 of `docs/dotnet-migration/avalonia-plan.md`. The plan named the "frame-sync and
receiver-parameter TOML helpers"; the receiver-parameters file was already in the core (`FrameSetup`, v2.12.1), so
what was left in the view file `streamsubdialogs.cpp` was the frame-sync pattern file and the Receiver SNR defaults
file that has the same layout. Both mixed the file's format rules with updating widgets.

- **`FrameSyncFile`** (`include/model/framesyncfile.h`, `src/model/framesyncfile.cpp`) is the format, with no Qt:
  `readFrameSyncFile(path)` returns the pattern and mask as written, the frame length only if the file gives a
  usable one (`BitsPerFrame`, else the older `WordsInMinorFrame` times 16 bits), whether it came from the older key,
  `Inverted` (false when absent), and `Randomized` only when the file carries the key; `writeFrameSyncFile` writes
  the five keys and returns whether it succeeded. A file that is missing, empty or unreadable reads as one that says
  nothing. Listed in `TestCoreQtFree`'s core list.
- **The dialog functions are thin glue** (`loadFrameSyncFromToml`, `saveFrameSyncToToml`,
  `loadReceiverFrameDefaults` keep their names and signatures): they apply the result to the editors, normalising
  the pattern and mask to upper case (the dialog's presentation, so it stayed Qt). The Receiver SNR defaults use
  the same reader but ignore the older key, which is a pattern-file fallback only.
- **Boundaries unchanged:** the US1.0 scope (the pattern, mask, frame length and the two stream-format flags
  round-trip; Data Rate and Sample Rate stay out) and the "no `Randomized` key leaves the operator's choice alone"
  rule are exactly as they were, and now sit in one place.

**Tests first, on the old code.** A new suite, `TestFrameSyncFile`, pins 86 cases through the dialog's own functions,
so the same table holds on both sides of the move: 56 load rows (missing and empty files, every key alone and
together, case folding including a non-ASCII letter, padded and unquoted values, comments, duplicate keys, other
sections, key case, line endings and byte-order marks; the frame length at, below and above the minimum and the
spin box's maximum; the older key at every boundary; both flags in every spelling), each run twice with Derandomize
ticked and unticked so that "left alone" shows; 15 save rows (the exact bytes, including quotes, backslashes,
non-ASCII text and an embedded newline); and 15 Receiver SNR defaults rows. They were recorded from the old code,
read through, and passed on it before anything was ported; **all 86 held after the port with no change** (the mutation check added two more, below).

**An oracle for the rest.** The old load, save and defaults functions are kept in the test, verbatim, and compared
with the new ones over 300 generated pattern files (keys present or not, in the right section or another, values
from pools of the ordinary and the awkward, repeated, commented, either line ending, sometimes a byte-order mark)
and 300 generated saves, each generator asserted to reach every branch in volume. **No difference.**

**Validated against 41 single-defect mutants; 38 were caught at once, two are equivalent and one was a real gap.**
They cover every key name, the length rules (the minimum itself, the older key's word count and width, the marker
that says a length came from it), the `Inverted` default, `Randomized` always reported, every writer decision, the
glue (case folding, empty values clearing a field, off-by-one, flipped flags, a stale folder, narrowed text) and the
defaults reader (reading the older key, not reading the length), plus the Qt-free guard. **The gap:** a mutant that
let the exact minimum length fall back to the older key survived, because no file had `BitsPerFrame = 64` beside a
larger `WordsInMinorFrame`; that case is now pinned in both tables. The equivalent pair, `words >= 1` and `words > 2`,
cannot differ: one or two words are 16 or 32 bits, below the 64-bit minimum either way.

**Not changed, and worth knowing:** saving does not check the result (it never did), so a pattern file that cannot be
written is silently not written; `writeFrameSyncFile` now returns whether it worked, for the day the dialog is
allowed to say so. And a pattern containing a newline is written raw and breaks the file: pinned as it is
(`a-newline`), since the editor's validator only allows hex digits.

Throughput is untouched (settings files are on no decode path), so the Release gate was not rerun.

Baseline **981 / 0 / 1** (was 886 / 0 / 1; `TestFrameSyncFile` adds 95 results), **982 / 0 / 0** at release.
**33 suites.**

#### `TemplateMatcher` is in the Qt-free core (plan step 1.3)

Third step of stage 1 of `docs/dotnet-migration/avalonia-plan.md`, and the smallest: the exact-set comparison of a
template's PCM channel ids with a file's.

- **`TemplateMatcher::match(templateIds, fileIds)`** takes two `std::vector<int>`s and returns
  `MatchResult{ok, missing, extra}` with `std::vector<int>`s, sorted and each id once. Order and repeats in either
  list do not matter; two empty sets are the same set and match. No Qt, and `templatematcher` moved from the
  UI-session list to the core list in `TestCoreQtFree`.
- **Reading the ids stays with the Qt side.** `BatchController::matchFile(tmpl, fileChannels)` takes a
  `ProcessingTemplate` and the reader's `getPCMChannelList()` as it is (`std::vector<std::pair<int, std::string>>`)
  and calls the core. That drops the conversion to `QList<QPair<int, QString>>` at both call sites, and
  `TemplateMatcher::templateChannelIds()` and `fileMatchesTemplate()` are gone: neither had a caller outside the
  tests, since `MatchResult::ok` is the predicate.

**Tests first, on the old code.** Seven edge cases were added to `TestTemplateMatcher` and passed on the `QSet`
implementation before it moved: two empty sets match, an empty template finds every file channel extra, repeated
entries or channels count once (and are reported once), missing and extra come back sorted, any `int` can be an id
(including the extremes and negatives), and the channel names play no part. Every test reads the matcher through
one helper, so only that function's body changed with the move. The old implementation is kept in the test as an
oracle and compared over 4,000 generated pairs of id lists (the same set reshuffled and with repeats, less some,
plus some, unrelated), with the generator asserted to reach every outcome in volume. **No difference.**

**Validated against 20 single-defect mutants; 19 were caught at once and the survivor is closed.** They cover
repeats not collapsed, ids not sorted, missing and extra swapped or never reported, `ok` ignoring either list or
using `||`, both ids read off by one or only the first of each counted, validation accepting every file or giving
no reason, the mismatch text for each side, and the Qt-free guard (Qt reaching the header, the matcher including the
Qt template, the source left unclassified). **The survivor was the re-check in `applySourceConfig`**, which compares
a file with the template again once it has loaded, in case it changed on disk since the up-front check: nothing
tested it, and a mutant that skipped it passed everything. `start()` does not validate, so
`aFileWhoseChannelsNoLongerMatchIsSkippedAtItsTurn` hands the loop a file the template does not describe, calls
`onSourceReady()` as the View does, and requires the skip, its warning and the summary; the mutant now fails it.
That test was not run against the pre-port code, which is the one place this step departs from the method: it
exercises logic the port did not change, and it is validated by the mutant instead.

Throughput is untouched (the matcher is on no decode path), so the Release gate was not rerun.

Baseline **886 / 0 / 1** (was 878 / 0 / 1; seven new matcher cases and the re-check test), **887 / 0 / 0** at
release. **32 suites.**

#### The CSV format is in the Qt-free core, in both directions (plan step 1.2)

Second step of stage 1 of `docs/dotnet-migration/avalonia-plan.md`: reading and writing the application's
CSV now happen in the core, so the Qt application and the future C# one share one implementation of the
format, beside `SeriesColumnSchema` (step 1.1).

- **`CsvSeriesParser` is plain C++.** It takes a `std::filesystem::path` and returns `CsvSeries` /
  `CsvParseResult` (UTF-8 `std::string`, `std::vector`). It reads the file as UTF-8 with a UTF-8 or UTF-16
  byte-order mark honoured, ignores carriage returns, and no longer includes Qt.
- **`CsvSeriesWriter` is new** (`include/model/csvserieswriter.h`): the export that was
  `PlotViewModel::exportCsv`. It takes `CsvColumn`s (a series' identity plus `std::span`s over its samples,
  so the plot copies nothing) and a `CsvTimeBase`, and writes the same bytes as before: CR LF, UTF-8 with no
  BOM, six decimals, a row per distinct millisecond, empty cells where a column has no sample.
- **`clocktime::format`** (`include/model/clocktime.h`) is the `DDD:HH:MM:SS[.mmm]` stamp the writer needs;
  `PlotViewModel::formatTime` is a one-line wrapper. Its `includeMilliseconds` parameter is gone from the
  plot: the export was its only caller and it now calls the core directly (found by mutation, below).
- **The plot converts at the boundary.** `loadCsv()` runs on the worker thread (`QtConcurrent`), so the copy
  into `QVector` and `QString` is not paid on the UI thread; `exportCsv` builds the column list from the
  series it holds. All three files are in `TestCoreQtFree`'s core list (`csvseriesparser` moved over from
  the UI-session list).

**Tests first, on the old code.** A new suite, `TestCsvSeries`, pins 81 import and export cases through the
`PlotViewModel` API, so the same table holds on both sides of the move: 64 import rows (line endings, byte-order
marks, blank and short rows, header padding, cell number forms, special values, every timestamp shape) and 17
export rows (millisecond rounding, six-decimal ties, out-of-order and repeated samples, day boundaries, sources,
non-ASCII labels). They were recorded from the old implementation, read through for sense, and passed on it
before anything was ported. The exact figures: 81 rows recorded, all 81 green on the unchanged code.

**Oracles for what a table cannot reach.** The old importer (QFile + QTextStream), the old exporter and the old
`formatTime` are kept in the test, verbatim apart from their types, and compared with the core over generated
input: 400 files (headers and cells from pools of the awkward and ordinary, short and long rows, the three line
endings, UTF-8 / BOM / UTF-16), 500 sets of series (exact millisecond ties, repeated and out-of-order and negative
times, ties at six decimals, huge, tiny, infinite and not-a-number values), and 8,000 clock stamps. Each
generator is asserted to reach every outcome in volume. The counts are modest because the Qt importer is slow
in a debug build (about 6 ms a file against 0.4 ms for the core's), which is also why `TestCsvSeries` takes a
few seconds.

**Things the comparison turned up, each kept as Qt had it or changed on purpose:**

- **`qRound` rounds halves away from zero.** -62.5 ms is -63, not -62. The writer rounds the way `qRound`
  does (add or subtract a half and truncate), and `milliseconds-that-are-exact-ties-either-side-of-the-base`
  pins it at x/16 s, the only places a millisecond tie is exactly representable.
- **`std::from_chars` takes numbers Qt refuses.** "Infinity", "nan(1)", "+nan" and a signed "nan" are numbers to
  the standard library and not to `QString::toDouble`, which takes only `nan`, `inf`, `+inf` and `-inf` in any
  case. `textparse::parseDouble` now refuses the others, compared with Qt over 20,000 generated strings
  (`numbersReadLikeQt`). It is shared with the settings and step-file readers, which gain the same strictness;
  none of their shipped files contains such a value.
- **All carriage returns are ignored,** not only the one before a line feed. The old reader dropped them
  wherever they were, so a lone one neither ended a line nor stayed in a name; the core does the same.
- **Changed on purpose, each pinned with the old value beside it:** a byte that is not valid UTF-8 no longer
  makes the whole file fail to load (the name shows the replacement character); a UTF-32 byte-order mark is no
  longer recognised (nothing writes one; UTF-8 and UTF-16 are); and trimming is ASCII only, so a no-break space
  around a header or a cell is no longer trimmed - the same narrowing `textparse.h` documents for the settings
  files. The application never writes any of them.
- **A failed write is now reported.** The writer returns false if the file cannot be created or fully
  written; the old code returned true after a write error.

**Validated against 77 single-defect mutants: 74 caught, 3 survived.** They cover the header (trimming, the
column count, the time header), the channel counts (per source, per stream, from one, for lock series), blank
and short rows and their counts, the base time, day arithmetic, five-field stamps, both byte-order marks and
both UTF-16 endians, carriage returns, the cached range, the file with no samples, every writer decision (the
empty case, line ends, the header's source and name, rounding both ways, sort stability, last-wins, empty
cells before and after a value, decimals, tie rounding, the flush, the base offset, truncation, appending),
every field of the clock, the number reader (infinity, its sign, case, `+nan`), every field the plot copies
across on import and export, and the Qt-free guard (Qt reaching the header, the clock or the parser, the parser
reaching the view model, an unclassified source). Two survivors are **equivalent**: `size() < 2` to `< 1`
(a file with no series column has no samples and fails the same way) and `std::max` for `qMax` (the same
expression). The third was a **dead parameter**: `formatTime`'s milliseconds flag no longer had a caller, so a
mutant that ignored it passed; it was removed rather than tested. Two tests were added before the run for
properties no small test sees and which mutants then confirmed: `aLargeExportCrossesTheFlushThresholdIntact`
(the writer flushes every mebibyte; 30,000 rows compared with the Qt export byte for byte) and
`anExportReplacesTheFileThatWasThere` (an append mode is the one mutant only that test can see).

**A harness note:** a suite filter, a temporary `TMDQ_ONLY` check in `runSuite`, made the loop run only the four
relevant suites (about ten seconds instead of forty-five); it was removed before the commit.

Throughput is untouched (CSV is on no decode path), so the Release gate was not rerun.

Baseline **878 / 0 / 1** (was 784 / 0 / 1; `TestCsvSeries` adds 94 results), **879 / 0 / 0** at release.
**32 suites.**

#### The CSV column schema is in the Qt-free core (plan step 1.1)

First step of stage 1 of `docs/dotnet-migration/avalonia-plan.md`: the code that defines and parses the
CSV column-header format moves into the core, so the future C# application and the Qt one read and write
the same format through one implementation.

- **`SeriesColumnSchema` is plain C++.** UTF-8 `std::string` in and out, no Qt include, listed as core in
  `TestCoreQtFree`. `columnHeader()` now takes the three fields a header is made of (metric, name, source
  id) rather than a `PlotSeriesData`, which carries Qt types. Numbers are read with `textparse::parseInteger`,
  which already reproduces `QString::toInt` (whitespace and a leading `+` tolerated, junk and overflow read
  as "not a number").
- **`MetricType` has its own header,** `include/dto/metrictype.h`, so the schema can name it;
  `PlotSeriesData::MetricType` is an alias of it, so the 43 existing spellings compile unchanged.
- **Callers convert at the call.** `CsvSeriesParser` decodes the header it parses and `PlotViewModel`
  encodes the headers it exports and the SNR series names it builds; nothing else changed in them.
- **One deliberate narrowing:** digits are ASCII only. `QChar::isDigit()` also accepted Unicode digits as the
  start of an SNR id, so a header such as `<Arabic digits> - A B Lock (%)` would have read as SNR where it now
  reads as a lock series. No export of this application contains one; the same narrowing `textparse.h`
  already documents.

**Tests first, on the old code.** `TestSeriesColumnSchema` had 17 cases. Before anything moved, a
data-driven table of 35 parse and 8 format edge cases was written against the `QString` implementation and
passed there on its first run (empty headers; ids and receiver numbers with signs, spaces and overflow; a
qualifier with no space or with an overflowing number; non-ASCII labels). Every call in the file then went
through four helpers, so only their bodies changed with the schema's string type.

**A differential oracle.** The old `QString` implementation is kept in the test, verbatim apart from its
names, and compared with the Qt-free one over 40,000 generated headers and every combination of formatter
inputs. The generator is asserted to reach each outcome in volume (lock, missed, SNR; a source, an id, a
receiver, a label), so "no mismatch" cannot be vacuous. **No header read differently.** Each pinned row
also asserts that the oracle gives the recorded answer, which ties the oracle to the old behaviour and not
only to my copy of it.

**A limitation of the format, found on the way and not caused by it:** source 0 is written with no
qualifier, so a series whose own NAME begins with `S<n>| ` reads back as source `<n>`. The old code did the
same. It needs a recording whose stream label looks like that, so it is pinned
(`aNameThatLooksLikeASourceQualifierDoesNotRoundTrip`) rather than fixed here; a change to the format is a
decision.

**Validated against 31 single-defect mutants; 30 are caught and one is equivalent.** They cover the digit
test, the SNR-shape test, every branch of the qualifier parser, both suffixes, the label split (first space
against last, position zero), the receiver token (first against last) and its number, the stream order, the
source id, the formatters (separator, suffix swap, qualifier for source 0, field order), the Qt-free guard
(a Qt include in the header, the Qt DTO included by the source), the enum order, and each of the three
callers. **Three survived the first complete run, and they were a real gap:** import decoding a non-ASCII
name as Latin-1, export encoding one as Latin-1, and export dropping the source id all passed, because the
schema's tests cannot see its callers. Three `TestPlotViewModel` cases now pin them
(`csvImportKeepsNonAsciiSeriesNames`, `csvExportWritesNonAsciiNamesAsUtf8`,
`csvExportQualifiesEverySeriesOfALaterSource`), **run first against a checkout of the old code, where they
pass**, and each mutant is now caught by exactly the test written for it. The one survivor, removing the
`digits == 1` early return in the qualifier parser, is **equivalent**: with no digits the number parse fails
and returns the same `{0, header}`. It mirrors the original's guard and is kept for that reason.

**Two build traps this step found, worth knowing:**

- **A new header needs a qmake re-run, or jom cannot see changes to it.** `metrictype.h` was new, and the
  generated Makefile's dependency lists predate it. A mutation of that header was neither rebuilt nor, worse,
  un-rebuilt on restore, and the "restored" tree stayed broken (66 failing tests) until qmake was re-run and
  the tree rebuilt clean. Re-run qmake whenever a change adds a header or a new `#include` relationship.
- **The compiler and linker PDB locks fail far more often right after the test exe has run** (`C1041`,
  `LNK1201`): after running it, the next build failed seven times in a row, and without running it about one
  in four. A pause and a retry cure it, and the loop needed both.

**The mutation loop itself had to be made trustworthy first.** A restore that silently failed once left a
mutation in the file, so every later verdict was measured against a broken baseline: three caller mutants were
"caught" only by a test that never touches callers. The loop now restores from pristine byte copies and
verifies the bytes, self-heals before each mutant, refuses to start unless the baseline is completely green,
stops outright if a restore fails, and counts a crash exit (an unhandled exception) as a catch.

Throughput is untouched (the schema is on no decode path), so the Release gate was not rerun.

Baseline **784 / 0 / 1** (was 735 / 0 / 1; the schema suite goes from 19 results to 65 and
`TestPlotViewModel` gains 3), **785 / 0 / 0** at release. **31 suites.**

#### A stream that ends early closes its own queue, so it cannot strand the reader

The hazard `PipelineRunner` carried over from the coordinator. The reader hands every packet to every stream's
bounded queue (64 deep) and blocks while one is full. A stream whose thread stopped early - its processor
aborted, with the run not cancelled - stops draining its queue; the queue fills and stays full, the reader waits
on it forever, and every OTHER stream, perfectly healthy, starves. The coordinator had the same property.
Cancelling the whole run was the only early exit anyone knew of, and it closes every queue, which is why it never
showed.

The fix is one line in `PipelineRunner::runStream`: when a stream's `process()` returns, however it ended, the
thread closes its own queue (`enqueue` on a closed queue discards and returns at once). Closing a drained queue
after a normal end changes nothing.

**It could be reached after all, and tested.** I had said nothing could trigger it, because nothing in the
runner's API can end one stream on its own. The test is a friend of the runner and aborts ONE stream's processor
mid-run (`aStreamThatEndsEarlyDoesNotLeaveTheReaderBlockedOnItsQueue`): job 1 stops, its queue fills, and the
healthy job 0 has to finish, be delivered and make the run succeed. **Against the unfixed code the run never
ended** (the test waits 30 s, then fails); with the fix it passes. No timing is involved: the dead stream's queue
fills whenever the file has more than 64 packets, which the committed recording has.

**Validated against two single-defect mutants; both caught**: the close removed (the failure above), and the
close aimed at job 0's queue instead of the stream's own.

Baseline **735 / 0 / 1** (was 734 / 0 / 1), **736 / 0 / 0** at release. **31 suites.**

#### The step-file parser reads through `TomlDocument`: one TOML reader, not two

The follow-up v2.14.2 left open on purpose. `StepDetector::parseStepConfig` was a second, hand-written TOML
parser for `[[Step]]` calibration files, because `TomlDocument` could not express arrays of tables; it
duplicated the line loop, the trimming, the comment stripping and the lower-casing, and each copy could drift.
Because it feeds calibration, the aim was to change **no behaviour at all**.

- **`TomlDocument` reads arrays of tables** (read only; `save()` does not write them). `arrayTables()` lists
  every `[[Name]]` entry in file order; `tables(name)` picks the ones named so, ignoring ASCII case. An entry
  is its header's name (as written) and its keys in file order with repeats, each value **as written** - quotes
  kept, an unquoted trailing comment stripped - so a caller can tell `"6"` from `6`. A `[[` line that is not
  just `[[Name]]` (a comment after it, a missing bracket) still opens an entry, so its keys are not taken for the
  previous entry's, but one no name matches. A section header, or a half-header such as `[Meta] # note`, ends an
  entry. Entries never reach `contains()` or the sections, and sections read as they always did.
- **`parseStepConfig` is now a loop over `tables("step")`**: a step per entry, `db` matched without regard to
  case, the last value that reads as a number winning, an entry with no usable `db` still a step at 0. Its two
  messages are unchanged. The helpers it carried (a comment stripper, a line loop) are gone.
- **One pin was changed on purpose.** `doubleBracketHeaderIsALiteralSectionName` pinned `[[Step]]` reading as a
  section literally named "[Step]", "so nobody starts relying on something it does not do". It now does, so it
  is replaced by `doubleBracketHeaderOpensAnArrayTableNotASection`. No shipped file other than the step files
  uses `[[`.

**Tests first, on the old parser.** `parseStepConfig` already had 13 characterization cases (BOM, CRLF, UTF-16,
header exactness, value rules, last-wins, error messages). To go beyond cases anyone thought of, a verbatim copy
of the hand-written parser was kept **in the test as an oracle** and run beside the new one over 4,000 generated
files - headers with comments, spaces, other names and missing brackets; keys in every case, quoted, commented,
empty, repeated and outside any step - requiring the same success, the same dB to the bit and the same message.
It passed against the unchanged parser (proving the generator reaches both outcomes in volume) and against the
new one.

One difference between the two comment strippers was checked rather than assumed: the old one was not
quote-aware and the document's is. They cannot disagree on whether a value parses, because a value the naive one
can parse has no quote before its first `#`, so the quote-aware one cuts in the same place.

**Validated against 13 single-defect mutants; all caught**: a header name trimmed; a header with a trailing
comment still named; entry keys also becoming section values; a section header, or a half-header, leaving the
entry open; entry values unquoted; names matched case-sensitively; entry comments not stripped; the first
`db` winning; the key matched case-sensitively; a step with no `db` dropped; a bad value resetting an earlier
good one; an unreadable file reading as "no steps".

Baseline **734 / 0 / 1** (was 729 / 0 / 1; the replaced pin and four new `TomlDocument` cases, and the
differential test), **735 / 0 / 0** at release. **31 suites.**

#### The pipeline is a Qt-free `PipelineRunner`; the coordinator is a thin wrapper around it

The last piece of non-UI logic that still sat in the Qt layer. `ProcessingCoordinator` owned the whole
single-reader / parallel-worker arrangement - a `PacketQueue` per stream, the reader's `prepare()`, a
`QThread` per worker and one for the reader, the start and stop order, and what each stream's result is
stamped with - in `QVector`, `QThread::create` and signals. The interop spike's `tmdq_core.cpp` had to
reimplement a single-stream copy of it by hand to put the core behind a C ABI.

- **`PipelineRunner`** (`include/model/pipelinerunner.h`, `src/model/pipelinerunner.cpp`; no Qt, listed in
  `TestCoreQtFree`'s core sources). `StreamJob` moved into its header. `start(jobs, events, error)` resolves
  the recording synchronously and returns at once; a failure before anything runs comes back as `false` plus
  the reason, reporting nothing to the sink. Everything after runs on `std::thread`s: one per stream, and the
  reader's thread, which also waits for the others and reports the end. A stream's result is stamped with its
  `jobIndex` and `sourceId` and delivered from that stream's own thread, only for a stream that succeeded in a
  run that was not cancelled; `processingFinished` is last, after every thread has ended and the pipeline has
  been released. `cancel()` is safe from any thread. The sink is a `PipelineEvents`: `ProcessingEvents` plus
  `streamProcessed`.
- **`ProcessingCoordinator` keeps its public API, its signals and its state flags**, and now only adapts: its
  private `Bridge` posts each of the runner's reports to the coordinator's thread as a queued call (the four
  ordinary ones through `QtProcessingEvents`), so the delivery order the UI relied on is the same. It still
  emits the opening "--- Processing N stream(s) ---" line and the final progress of 100 itself, which is why the
  progress bar still reads 100 twice - the reader's, and the coordinator's on finish.
- **No extra thread.** The reader's thread is the runner's driver; the thread count per run is unchanged.

**Tests first, on the old implementation.** The coordinator suite was the oracle, and reading it showed that
`cancelProcessing()` and destroying a coordinator mid-run - both rewired by this change - had no test (the suite
only cancelled to clean up after a timeout). Two cases were added and **run against the unchanged coordinator
first, where they pass**: `cancelProcessingEndsTheRunWithoutAResult` and `destroyingTheCoordinatorMidRunIsSafe`.
`TestPipelineRunner` (14 cases) pins the runner directly, with a plain recording sink and no event loop.

**Validated against 24 single-defect mutants; the first run caught 19 and five survived**, all of them cancel
and end-of-run rules that the obvious test cannot see, because cancelling aborts the workers and so a stream
mostly just fails to finish: a result still delivered for a cancelled run, success reported despite a cancel,
the cancel not aborting the workers, the cancel not closing the queues, and the running flag cleared after the
end was reported rather than before. Each was closed with a case that holds the sink at a chosen moment of a
real run - a thread parked on a log line - instead of racing for it (see `TestPipelineRunner` in the catalog).
All 24 are now caught.

Carried over from the coordinator unchanged when this landed, and fixed straight after (the entry above): a
stream whose thread ended early without a cancel would leave the reader blocked on that stream's full queue.

Baseline **729 / 0 / 1** (was 711 / 0 / 1; the runner's 16 results and the coordinator's 2), **730 / 0 / 0** at
release. **31 suites.**

#### One event reporter, one result type, one fewer thread (the review pass, part two)

The simplifications the review pass found that were too large to ride along with its mechanical fixes. No
behaviour changes.

- **`EventReporter`** (`processingevents.h`). `Ch10PacketReader`, `FrameProcessor` and `CalibrationExtractor`
  each carried their own "if a sink is set, forward to it" - three copies of the same nine-line functions for
  log, error, finished or progress, plus a private `m_events` and null checks. They now hold one
  `EventReporter` and call `m_report.log(...)`; `setEvents()` and `start()` keep their signatures, so no caller
  and no test changed. With no sink every call does nothing, as before. A new suite, `TestEventReporter`, pins
  that, the routing of each call, the order, replacing and clearing the sink, and that the reporter (not a
  borrowed pointer) keeps a shared sink alive.
- **The calibration extractor's worker runs on its driver thread.** The driver started a worker thread and a
  reader thread and then only waited for the first; it now starts the reader and runs the worker itself. One
  thread fewer per extraction; the worker's log lines now arrive from the driver thread (the header says so),
  and the sink was already required to be callable from any of them.
- **`CalibrationChannelResult` derives from `StepDetector::Result`** instead of repeating its ten fields and
  copying them one by one. It adds only the channel's name, word and whether it had data; every field's name
  is unchanged, so the dialog and the tests needed no edit, and a field added to the detector's result now
  reaches the report without a second edit.

**Validated against eleven single-defect mutants; all caught**: the reporter routing a log to the error call,
inverting `finished`, dropping `progress`, losing its null guard, or ignoring `setSink` (the first three are
caught by `TestEventReporter` itself, the last two crash the run); the reader's last progress report, the extractor's
error and its progress relay, and the processor's success report each dropped; the driver skipping the worker;
and the detection never copied into the result.

Baseline **711 / 0 / 1** (was 703 / 0 / 1; the new suite's six cases and its init/cleanup), **712 / 0 / 0** at
release. **30 suites.**

#### A review pass over the de-Qt'd core: dead diagnostics, drifted comments, one mask definition

A read of everything the sweep changed, with fresh eyes, found no defect and nothing in the wrong layer -
the Qt-free guard holds - but some debris. Nothing here changes behaviour.

- **Dead diagnostics in `FrameProcessor`.** When the log was made concise (#103) the "longest run" and
  related fields stopped being printed, but the scan kept maintaining them bit by bit. Removed:
  `max_sync_run` and `current_sync_run` (updated on every sync, read by nothing), `buffer_ever_filled`,
  `first_data_time` / `last_data_time` (a pair of stores on every bit), and the `ScanDiagnostics` copies
  of `words_in_frame` and `min_syncs`. What the report still prints is untouched: `reportsExactlyTheseMessages`
  pins its text.
- **Doc comments that had drifted.** `frameprocessor.h`'s three `report*` declarations sat between the
  comments of the functions around them; the relaxed-threshold paragraph in the calibration summary sat on
  top of the saturated-top section's; field names the DTO rewrite renamed (`resolved_attrs`, `packet_queue`,
  `is_randomized`) survived in comments; `ProcessingEvents::errorOccurred` claimed to be always followed by
  `processingFinished(false)`, which the packet reader never raises; several headers still described the
  sweep as in progress; and `ProcessingParams::swapBytes` said "Defaults false" over a `true` that #101
  deliberately restored.
- **Seven headers were not in `tmDataQualityAnalyzer.pro`'s `HEADERS`** (`processingevents.h`, `streammode.h`,
  `containerutil.h`, `qtbridge.h`, `qtprocessingevents.h`, `textformat.h`, `textparse.h`). The build did not
  need them - none has `Q_OBJECT` - but a generated Visual Studio solution would not list them.
- **One definition of "every bit of the pattern".** The default sync mask was computed three times - in
  `resolveFrameSync`, in `Ch10PacketReader::prepare` and in the failure report - and the third differed for a
  pattern of no length. `fullSyncMask()` (in `processingparams.h`) is now the only one; a zero-length pattern
  gets a full mask in all three, which cannot occur from a real setup (`resolveFrameSync` refuses an empty
  pattern).

**Validated against five single-defect mutants of the mask; all caught**: a zero length giving an empty mask; 64
bits shifting by 64; the off-by-one; the reader ignoring the default; and the report inverting its mask test.
The dead-diagnostics removal cannot be mutation-checked - it removes work nothing observed - so its evidence is
the unchanged suite and the Release throughput gate.

Baseline **703 / 0 / 1** (was 702 / 0 / 1; the mask helper's test), **704 / 0 / 0** at release. **29 suites.**

#### irig106 setup is serialised: two threads opening recordings at once no longer crash

The vendored library keeps process-wide state it never locks. `GetNextHandle()` claims a slot in a global
handle table with a check-then-set, so two simultaneous opens can be given the **same** slot;
`InitHandles()` resets that table the first time it runs; and `i106_decode_tmats.c` decodes through static
pointers. Found by driving the core from a bare C# host, where the first concurrent run died with
`0xC0000005`. The application opens each file once from one thread, so nothing was failing - but batch work,
calibration beside a main run, or a second window would have met it, as an access violation rather than an
error. `lib/irig106` may not be edited, so the lock is ours.

- `include/model/irig106lock.h`: one process-wide mutex (`irig106SetupMutex()`, an inline function-local
  static, so one instance across translation units) and `lockIrig106Setup()`, which returns the held
  `std::unique_lock`. Qt-free, so the core guard stays satisfied.
- `Chapter10Reader::loadChannels` holds it for the whole load, and `Ch10PacketReader::prepare` from just
  before the open to its return, by an object, so every early return releases it. That is the whole of
  setup: open, time sync, TMATS read and decode. **Not** the packet reads afterwards - a handle that is already
  open touches only itself, so the parallel decode is untouched.

**Tests first.** `manyThreadsSettingUpAtOnceAllSucceedAndAgree` releases eight threads together to do 40
setups each, alternating the two readers, and requires every one to succeed and to list the same PCM channels
as a single-threaded load. **Against the unfixed code the whole test executable crashed** with `0xC0000005`
inside it. A race is only sometimes visible, so two deterministic cases pin the lock itself, once per reader:
`setupWaitsForTheLockAnotherThreadHolds` (while the test thread holds the lock, a setup on another thread must
not finish within half a second, and must succeed once it is released) and `aFailedSetupStillReleasesTheLock`
(a setup on a missing file must leave the lock free and the next setup working).

**The deterministic case also sees where the lock is taken.** The recording it hands the worker is a copy the
test holds open with no sharing, so any attempt to open it fails at once: a setup that reached `open()` while
it should still be waiting finishes - with a failure - while the lock is still held. The handle is closed
before the lock is released, so the open then succeeds. The first version of this test could not tell a lock
taken before the open from one taken after it, and two mutants survived; this is what killed them.

**Validated against eight single-defect mutants; all caught.** Either reader unlocked (two); a mutex that is
per-thread instead of shared; the lock released the instant it is taken (two); the lock taken after the open
(two, the survivors above); and the lock leaked when the open fails. The first three families and the early
release are caught by the stress test crashing the run, which it did on every run of the mutation loop; the
after-open pair by the deterministic case; the leak by a later suite re-locking on the same thread and
throwing. **The remaining blind spot is a lock released too early *after* the open**, which only the
stress test can see, and that is probabilistic.

Baseline **702 / 0 / 1** (was 697 / 0 / 1; five new results: the stress test and two cases per reader),
**703 / 0 / 0** at release. **29 suites.**

#### The core includes no Qt header, and a test says so (de-Qt, `constants.h`)

With the last `QObject` gone, the question was what Qt the core still touched. A compile probe of every core
translation unit with no Qt include path answered it: **one header, `include/constants.h`**, which included
`<QString>` for `AppVersion::toString()` and used `qsizetype` for `kMaxPacketBufferSize`. Every core source
reached it through `include/` or a DTO.

- `AppVersion::toString()` returns `std::string`, built with `std::to_string`; the three UI call sites
  (`mainviewchrome.cpp`, `batchcontroller.cpp`, `mainviewmodel.cpp`) wrap it in `QString::fromStdString`. The
  `.pro` file and `deploy/build_release.ps1` read only the `kMajor = <n>` lines, so neither is affected.
- `kMaxPacketBufferSize` is a `std::ptrdiff_t`, the same 100 MiB.

**A guard, not a one-off.** `TestCoreQtFree` walks the `#include "..."` closure of every core source (the
`.cpp` and everything it reaches through `include/`, `include/dto/` and `include/model/`, recursively, ignoring
commented-out includes) and fails on two things: any `<Q...>` header (`Q` plus a capital, or `Qt`), and any
reach into `include/viewmodel` or `include/view`. Every `src/model` source must be classified as core or
UI-session, so a new file cannot join the model silently unchecked. Its fourth test runs the walker over a
temporary tree to prove it sees a Qt include hidden behind a shared header, a layering violation, and a
comment that must not count.

**Proved at link level, on the exploration branch.** Built with plain `cl`/`link` and no Qt include path or
library, the decode pipeline plus a flat C ABI is a 361 KB DLL whose `dumpbin /dependents` lists the C runtime
and nothing from Qt; a C# host drives it and reproduces the numbers recorded before the sweep exactly (116
samples, 93.532 % mean lock and 1,100 missed frames at a 1 s sample period; 11,554 samples and 838 missed at
10 ms). That lives in `dotnet/hybrid-spike/` on `explore/dotnet-migration`, not here.

**Validated against ten single-defect mutants.** The first run caught nine; **one survived**: a walker that
recognised only `<QString>`, because the self-test planted exactly one include, a `<QString>`. The self-test now
plants `<QString>`, `<QtGlobal>` and `<QObject>` plus a look-alike `<Quux.h>` that must not count, and the
mutant fails. The ten: `<QString>` returning to `constants.h`; the version separator changed; the buffer cap
changed; a Qt include in a nested DTO; the core including a view-model header; a walker that does not recurse;
a walker that knows only `<QString>`; a model source left unclassified; the forbidden directories unwatched;
and commented-out includes counted.

Baseline **697 / 0 / 1** (was 691 / 0 / 1; the new suite's 4 tests and its init/cleanup), **698 / 0 / 0** at
release. **29 suites.**

#### A fully calibrated group of unnamed channels is listed as calibrated (US5.3)

The calibration report groups channels by receiver and lists the receivers whose channels all
calibrated on one line, `Calibrated: receivers 1, 3.` Channels whose names carry no `_RCVR<N>` (a
hand-written receiver-parameters file may name them anything) have no receiver number, so they are one
group of their own, reported by name. When **every** channel in that group calibrated, the report put it
under the heading **"Fell back to linear calibration:"**, as `Unnamed receiver (AGC_LEFT, AGC_RIGHT) —
calibrated`. It is the only fully-calibrated case that did not go on the calibrated line, and the heading
is wrong for channels that did not fall back. The manual already describes the intended report
("Calibrated - the receivers whose channels all calibrated", "Fell back - every other receiver"), so it was
the report that was wrong. All three shipped word maps name every channel `_RCVR<N>`, so only a custom map
could show it.

Found while pinning the report text for the move to `std::string`, where it was left alone on purpose (a
refactor does not change wording) and noted. The wording was then chosen: the unnamed channels go on their
own line beside the calibrated receivers, so a numbered-only report - every shipped map, and the manual's
figure - is **byte-for-byte unchanged**:

```
Calibrated: receivers 1, 3.
Calibrated (unnamed channels): AGC_LEFT, AGC_RIGHT.
```

With no numbered receivers the second line stands alone. A group where some unnamed channels *did* fall back
still reads as a mixed receiver under the fallback heading, which is correct.

**Tests first.** The pinned expectation for the unnamed, all-calibrated group was changed to the corrected
text and three scenarios were added (unnamed beside a numbered receiver; beside a receiver that failed; with
a saturated top, which appears in both its sections). Against the unfixed code exactly five tests failed -
the four report rows and the non-ASCII word-map extraction test - and the other seven report scenarios did
not move; the fix turned them green.

**Validated against nine single-defect mutants; all caught**: the names dropped; the group also left under
the fallback heading; the lines in the other order; no blank line before a lone unnamed line; the heading
reworded; the closing period dropped; a different separator; the line printed for a *partly* calibrated
group; and the line gated on numbered receivers only.

The manual's calibration walkthrough gains one sentence, in the source (`docs/manual/walkthroughs.html`) and
the generated `UserManual.html` identically, so the generator splices no change. The end-user release notes
are written at release time and should mention it then.

Baseline **691 / 0 / 1** (was 688 / 0 / 1; 3 new report scenarios), **692 / 0 / 0** at release. **28 suites.**

#### `CalibrationExtractor` is a plain class on one driver thread, and the sync decode is shared with the main run (de-Qt, `CalibrationExtractor` step B)

The second half of taking Qt out of `CalibrationExtractor`, and the last `QObject` in the processing
core. It was a `QObject` with three signals and two `QThread`s; it is now a plain class that
reports to a `ProcessingEvents` sink (`start(Request, events)`), with no Qt in its header or its
source. One **driver thread** starts the reader and the worker, waits for both, runs step detection,
and reports. The Extract Calibration dialog builds a `QtProcessingEvents` adapter around lambdas, as
the coordinator does; no wrapper class was needed.

`Request` is plain too: paths are `std::filesystem::path`, and how the bits are recovered is an
**`Acquisition`** (the resolved sync pattern and mask, frame length, the two code-format flags, the
data rate **and the file-level byte order**). The dialogs still hold the sync as hex text
(`FrameSyncParams` and `StreamConfig` stay `QString`, as scoped), so `CalibrationSetupDialog` now takes
a small `Base` struct and decodes the text when the extraction starts - an unreadable field is shown on
its status line exactly as before.

**One decode, one assignment, for both runs (`include/model/framesync.h`).** The main run
(`MainViewModel::buildStreamJob`) and the extraction each decoded the hex pattern and mask and each set
the raw-affecting fields of their `ProcessingParams` by hand. The calibration extractor must read the
same bitstream the main run will, and that invariant was broken twice by one site setting a field the
other forgot (the inversion flag, then the byte order). Both now call `resolveFrameSync()` and
`applyAcquisition()`, so a new raw-affecting field has one place to go. Hex is read by a Qt-free
`textparse::parseUnsignedHex`, which reproduces `QString::toULongLong(16)`: surrounding whitespace
ignored, a leading `+` and a `0x` prefix accepted, any `-` refused (even `-0`), 64-bit overflow refused.
Two quirks are kept on purpose: the pattern's length in bits is its **character** count times four
(so `" FE6B "` is 24 bits), and an empty mask means every bit of the pattern. Whitespace trimming is
ASCII only, as the other parsers in `textparse.h` are.

**Behaviour that changes, deliberately, and why:**

- **Step detection runs on the driver thread, not the GUI thread.** It used to run inside the queued
  completion handler; the progress dialog now stays responsive while 48 channels are detected.
- **`processingFinished` is truly last.** The old class finished while the reader's last progress report
  was still queued behind it (a test had to wait for it); the driver now joins both threads before it
  reports, so every progress and log report precedes the end.
- **A failure is an error, then the end; a cancel is only the end.** `errorOccurred(reason)` then
  `processingFinished(false)`. The worker's own reason (e.g. "pattern was not found") is still what a
  failed run reports, and still appears as a log line.
- **Completion is always delivered through the event loop, even when a run fails before it starts.** The
  old shortcut (finish synchronously inside `start()`) is gone; the dialog's `finished` flag remains
  because a modal progress dialog pumps events itself and can deliver the end before the dialog waits.
- **`cancel()` can be called from any thread, and the destructor cancels and waits** for a run in flight.

**Tests first.** Two commits pin the QObject version before anything was ported, all passing on it:

- `buildStreamJob`, which had no test of its own: 43 rows of hex decoding (the quirks above), every field
  a config contributes to the worker's parameters, and each rejection message in the order the checks run;
- the extractor's lifecycle through one `Recorder` helper: the failures that happen before anything
  starts (no steps; a word map that is missing, outside the frame, empty, or does not fit; a recording
  that cannot be opened), a cancel from the first report, a start while running being ignored, a finished
  extractor running again, the first log line and the success summary;
- the dialog: an unreadable sync pattern or mask, or an unopenable recording, shown on the status line
  without extracting, and a real `step_cal` extraction through the progress dialog (twelve profiles).

Written after the port, and labelled as such: `TestFrameSync` (the hex parser against Qt on 40,000
random strings and a list of awkward ones; the decode; `applyAcquisition` setting every field and no
other); that the end is last and only a failure is an error; a worker failure reporting its own reason; a
cancel from another thread; destroying a running extractor; and, at the dialog, that **byte order,
inversion and derandomization each reach the extraction** (step_cal needs the swap and neither of the
others, so the wrong value must visibly fail) and that the **data rate** does (at half the rate the same
bits take twice as long, which the clip warning shows as exactly twice the recording length).

**A crash on the way, and it was the test.** The destruction test deleted the event target before the
extractor's threads had stopped, so they posted to a dead object. The adapter's contract is that the
target outlives the events; the test now honours it.

**Validated against 36 single-defect mutants; 34 were caught at once and the two survivors are closed.**
They cover the decode (all-ones mask, length times eight, empty pattern unchecked, the result written
before the mask is checked), `applyAcquisition` (each field dropped, the rate in kHz, words truncated),
the parser (no `+`, no prefix, no trim, wrapping on overflow), the extractor (cancel ignored, running
never cleared, error not an event, summary arguments swapped, a worker failure ignored, progress not
relayed, the worker's reason lost, the acquisition not applied, the steps guard, the params-file check,
the previous driver not joined, a cancel reported as an error), and both callers (raw-affecting fields
dropped or fixed in the main run and in the dialog, two error texts). The survivors: **the clip-ignored log line**
(nothing tested the line, only the flags; now it is) and **the dialog dropping the data rate**
(step_cal states the same 0.8 Mbps itself, so it changed nothing there; the half-rate test above makes it
visible).

The suite was run five times with threads in play (the clean build, three more, and the release gate),
all green. Throughput is untouched: pinned release gate 15299 ms parallel, 1.04x the slowest solo
stream and 0.67x all-sequential.

Baseline **688 / 0 / 1** (was 606 / 0 / 1; 82 new: 51 for `buildStreamJob`, 9 extractor lifecycle, 4 for the
plain extractor's guarantees, 10 at the dialog, 8 in `TestFrameSync`), **689 / 0 / 0** at release.
**28 suites.**

#### The calibration report is UTF-8 `std::string`, and Qt's rounding is reproduced (de-Qt, `CalibrationExtractor` step A)

The first half of taking Qt out of `CalibrationExtractor`: the text the operator reads after an
extraction. `CalibrationExtractor::summarize()` now returns a UTF-8 `std::string` built with
`std::format` (it was a `QString` built with `QString::arg`, a `QMap` and `QStringList`s), and
`CalibrationChannelResult::name` is a `std::string`. The Extract Calibration dialog decodes the report
where it shows it. The class itself is still a `QObject`, and its log lines and start-up errors are still
`QString` - that is the second half, because they leave through the signals that become the event sink.

**`std::format` does not round like Qt, and that was measured before it was assumed.** A probe printed
both for the same values: an exact tie is rounded away from zero by `QString::number(v, 'f', n)` and to
the even digit by `std::format`. `0.5` with no decimals is "1" in Qt and "0" in `std::format`; `2.5` is
"3" and "2"; `0.25` with one decimal is "0.3" and "0.2"; `1234.5` is "1235" and "1234". Every other value
agrees, because it is decided by the exact binary value, not by a tie. The report prints dB values and raw
levels with 0 decimals, so a step file with half-dB steps would have printed "2 dB" where it printed
"3 dB". New `include/textformat.h` has `formatFixed(value, decimals)`, which detects an exact tie from the
double's exact decimal expansion and nudges it up one step so the correctly-rounded formatter takes it
away from zero, and keeps the sign Qt keeps on a small negative that rounds to zero.

**New suite `TestTextFormat` checks the helper against Qt itself**: the oracle is `QString::number`, over
the ties the report prints, every `n/64` for `n` in -4200..4200 at 0 to 6 decimals (exact binary
fractions, so hundreds of true ties), 20,000 arbitrary doubles from a fixed generator, and the extremes.
**It found a real difference on its first run**: the helper kept the sign of negative zero ("-0") where Qt
prints "0", while Qt does print "-0" for `-0.4` rounded to nothing. The sign now comes from `value < 0`,
not the sign bit, and both facts are pinned.

**Tests first, as before.** Twelve scenarios (eleven at first, and one added below) compare the **whole** report text, where
the old tests looked for phrases inside it: empty, one receiver, mixed, unnamed, non-ASCII names, both "check" sections, a
saturated top with exact-half dB values, a relaxed threshold, flat levels, several failure reasons and
twelve receivers. They were captured from the `QString` implementation, read through for sense, and pass
on it before anything was ported. Reads and writes of a result's name and the report go through test
helpers, so the port changed those and nothing else.

**One thing the capture showed, fixed in the next change.** When every channel in an *unnamed* group
calibrated, the report listed the group under the heading "Fell back to linear calibration:"
(`Unnamed receiver (AGC_LEFT, AGC_RIGHT) — calibrated`), which is wrong for channels that did not fall
back. It was pinned as it was while the report moved to `std::string`, because a refactor does not change
wording; see "A fully calibrated group of unnamed channels is listed as calibrated" above.

**Validated against 21 single-defect mutants; 18 were caught at once, and the three survivors are
accounted for.**

- A non-ASCII channel name decoded wrongly in the per-channel log line: the fixtures' names are ASCII,
  so no test could see it. Closed by a real extraction of `step_cal` with a hand-written word map naming
  three live words `R<e-acute>cepteur <omega>`, `Gain <sigma>` and `Plain`, checked through the result, the log
  line and the report.
- A fully-calibrated unnamed group listed as a numbered receiver: no scenario had such a group. Closed by
  the `unnamed-all-calibrated` scenario above.
- The helper looking at only 17 places of the exact expansion instead of 1100: **equivalent over any value
  the report can print.** A search over 800,000 decimal ties at 0 to 3 decimals found none whose nearest
  double separates from the tie beyond 17 places. 1100 stays because it is the proven bound (a double's
  expansion is at most 1074 places), not because anything reachable needs it.

The tests that closed those gaps (the extra scenario, the non-ASCII word-map extraction and the
per-channel log-line check) were run on the unmodified code first, as were the exact-text scenarios.

**A harness slip, caught by the compiler.** Routing the tests through helpers with a regular expression
turned two helper bodies into calls to themselves; MSVC warned (C4717), and the run crashed instead of
failing a test. The helpers were rewritten by hand.

Throughput is unaffected (nothing here is on the decode path); the pinned release gate gave 13846 ms
parallel, 1.02x the slowest solo stream and 0.65x all-sequential.

Baseline **606 / 0 / 1** (was 583 / 0 / 1; 23 new: 12 exact-report scenarios, the per-channel log line, the
non-ASCII word-map extraction, and the 9 results of `TestTextFormat`), **607 / 0 / 0** at release.
**27 suites.**

#### `Chapter10Reader` and `ChannelData` are plain classes (de-Qt, `QObject` step 4)

The metadata reader, the one that lists a recording's channels in the Configure Streams dialog, was a
`QObject` with a `displayErrorMessage` signal, `QMap` / `QList` of `new`'d `ChannelData*`, and
`QString` for the path and every TMATS string. It is now a plain class and `chapter10reader.h` and
`channeldata.h` contain no Qt: the path is a `std::filesystem::path`, text is UTF-8 `std::string`,
channels live in a `std::map<int, ChannelData>` in id order, and the time and PCM lists hold channel
**ids** rather than pointers. The getters keep their names and return `std::vector<std::string>` /
`std::vector<std::pair<int, std::string>>`; `MainViewModel` and `BatchController` convert at about
eight sites with two small helpers in `qtbridge.h` (`toQStringList`, `toQPairList`). `TemplateMatcher`
and `StreamConfig` stay Qt, as scoped.

**Errors are read, not signalled.** Every error path emitted once and then returned false, and loading
is synchronous on the caller's thread, so the signal carried nothing a return value and a string do
not. `lastError()` holds the reason of the last `loadChannels()`; `MainViewModel::loadFileMetadata`
emits `errorOccurred` from it right after a failed load, which keeps "one error, in the same call".
A `ProcessingEvents`-style sink would have added nothing here.

**Three things changed that are not a pure port, each deliberate:**

- **A latent defect is fixed.** `loadChannels` cleared the channel map but not the time and PCM lists,
  which held raw pointers into it. Loading a second time on one reader left dangling pointers and
  appended duplicates. Production never does this (a fresh reader per load), so it never showed; it
  was undefined behaviour, so there was nothing to pin on the old code. `loadChannels` now replaces
  everything - channels, lists and selection - once the new file has opened, and a file that fails to
  open still leaves the old contents alone (`aSecondLoadReplacesWhatTheFirstLeftBehind`,
  `aFileThatFailsToOpenLeavesThePreviousChannelsAlone`).
- **Dead code is removed.** `tryLoadingFile()` and the public `closeFile()` had no caller in `src/` or
  `tests/`, and `m_filename` was set and read only to build one error message.
- **`path.string()` replaces `toLocal8Bit()`.** Both readers now pass irig106 the path in the process
  code page the same way; the UTF-8 manifest makes that UTF-8.

**One branch is unreachable and left as it is.** "Failed to find TMATS message (expected as the first
packet)" sits behind a check irig106's `enI106Ch10Open` has already made: a recording whose first
packet is not TMATS is refused at open ("Error opening file"), so no file reaches it. Pinned by
the `no-tmats-first` row.

**Tests first, in four commits, all passing on the `QObject` reader before the port.** The old suite
asserted "not empty". The new one pins:

- exact channel ids, names and TMATS data rates for each of the five committed recordings (5 rows);
- the combo-box index to channel mapping, including the placeholder at 0 and indices past the end;
- each way a file fails to load: message and an empty result (7 rows: missing, empty, text, short
  header, first packet not TMATS, only TMATS, cut inside TMATS - the last two fail at "Error
  establishing time sync.");
- the view model: the combo-box lists, the default stream rows (id, bare label, TMATS rate), the
  selection through the property setters, and a failed open reporting exactly one error and leaving no
  channels; `BatchController::validate` against a real file (it had no test with one);
- a non-ASCII channel name, through the reader and through the view model.

**Reaching the TMATS branches the fixtures don't.** The five recordings have real disabled sources
(`CHE-2:F`) but no whitespace, numeric type codes, name fallbacks or odd rates. `step_cal` has no data
checksum and its first packet's header is untouched by a same-length edit, so the tests copy it and
patch its TMATS text in place (24 rows): the enabled flag (T / Y / 1 either case enable; F / N / 0 /
anything else disable; missing enables; whitespace ignored), the type (`PCMIN` / `01` / `09`, `TIMEIN` /
`11`, case-sensitive, anything else unlisted), the name priority (data source id, then channel data
link name, then PCM link name), an accented letter in a name, and the bit rate read like
`QString::toDouble` (12 rows: exponent, signs, spaces and a fraction accepted; negative, zero, junk, a
thousands separator and `nan` mean "not described"). The text-based `QString::toDouble` rules are
`textparse::parseDouble`'s, which already reproduced them for settings files.

**Validated against 23 single-defect mutants; all caught, after two survivors were closed.** They
cover the enabled flag (ignored, no upper-casing, no trim), name priority (two orders), type codes
(`09`, `11`, no trim), the combo-box separator, both selection offsets, the reload, `lastError()`
(not cleared, wrong text, time-sync text lost), the data-rate guard, the first-PCM accessor, and the
conversion boundaries (the view model's relay, the label decoding, the path, both `qtbridge.h`
helpers, the batch check). The first pass left two uncaught: **trimming around the enabled flag** and
**the `> 0` guard on the data rate** (a negative rate would have been reported as a rate). Neither is
equivalent. Each was closed with rows run on the unmodified reader first, and both mutants then fail.

**A harness trap, once more, and a new one.** A mutation run started as `cmd &` inside a tool call
was believed dead after it applied one mutant and was relaunched, which left **two loops running at
once** colliding on the same files ("Device or resource busy"); the output was garbage (almost every
mutant "no result"). The sources were byte-compared with saved copies, one loop was run to completion
alone, and the earlier results discarded. A runner now compares its sources against saved copies at
the end, and a background job is started as the tool's own command rather than as a detached `&`.
Separately, a run's `tests exit 0` after a failed build is the previous exe's code; the runners delete
the exe and results first and refuse a result unless both exist.

Also corrected: `tst_chapter10reader.cpp` said the `.ch10` fixtures are gitignored; they are
committed, and the suite's skip now says what it actually guards.

Throughput is not touched (one TMATS packet is read, not the stream); the pinned release gate gave
13754 ms parallel, 1.01x the slowest solo stream and 0.65x all-sequential.

Baseline **583 / 0 / 1** (was 525 / 0 / 1; 58 new: 52 in `TestChapter10Reader`, 2 in
`TestChannelData`, 3 in `TestMainViewModelHelpers`, 1 in `TestBatchController`), **584 / 0 / 0** at
release. **26 suites.**

#### `FrameSetup` is a plain class: no `QObject`, and a `std::unordered_map` for profiles (de-Qt, `QObject` step 3)

The word map and calibration table every stream job owns was a `QObject` for no reason anyone used.
It had no signals, no properties and no connections; the base class was only for parent
ownership, and every owner either holds it by `shared_ptr` (the view model's jobs, the tests) or
deletes it itself (the calibration extractor), always constructed with a null parent. It is now a
plain class with a defaulted constructor, and `framesetup.h` includes no Qt header.

The one remaining Qt type in its interface went with it: `attachCalibrationProfiles()` took a
`QHash<int, CalibrationProfile>` and now takes a `std::unordered_map`. The hash it was fed from,
`StreamConfig::calibrationByWord`, stays a `QHash`: that struct is the dialogs' own form, and
nothing in the processing pipeline reads one. `MainViewModel::buildStreamJob` copies it across,
once per run and at most one profile per receiver channel, which is the only conversion.

**`StreamConfig` itself is deliberately not converted.** Scoping this step showed it is the UI form
(combo-box indices, hex strings for the sync pattern and mask, `QString` paths shown in line edits);
`buildStreamJob` already turns it into the `std` `ProcessingParams`. Converting it would add a
`toStdString()` at about 45 dialog and view-model sites and 55 test sites and remove no Qt from the
pipeline. What is left in the core is `ChannelData`, `Chapter10Reader`, and `CalibrationExtractor`
with `FrameSyncParams`.

**Tests first, for a function that had none.** `attachCalibrationProfiles` was called from one
place and tested nowhere. Two new `TestFrameSetup` cases were written against the `QObject` /
`QHash` version and passed there before the port: only a profile that is **valid** and **matches a
parameter's word** attaches (an invalid profile stays linear on purpose; a profile for a word the
map lacks, and a parameter nobody supplied a profile for, are left alone), and nothing to attach
returns zero. Only the one helper that builds the container changed across the port.

**One test was written after the port, and says so.** `buildStreamJob` (private, and with no test)
is where the profiles actually reach the word map, so `TestMainViewModelHelpers` is now a friend of
`MainViewModel` and `buildStreamJobAttachesCalibrationByWord` runs a receiver stream with three
profiles (one valid and matching, one invalid, one for a word the map lacks) through it, checking
which parameter received what and the "provided 3 profile(s), attached 1" log line. It could not be
run against the old code without reverting the port, so it is validated by mutation.

**Validated against five single-defect mutants; each fails the test written for it.** `attach`
ignoring `valid`; matching the wrong word; not counting what it attached; the view model shifting
every key by one; and the view model copying nothing across. The first three fail both the unit
case and the `buildStreamJob` case; the last two only the latter, which is why it exists. Each
build was checked to have relinked.

**A harness trap, again.** The first run of the new `buildStreamJob` test had two compile errors,
and the script printed `tests exit 0` and 524 passed: the previous exe, run after a failed build.
The runner now deletes the exe and results first, and a number is only believed if both exist
afterwards.

Throughput, pinned release gate: 4-stream parallel 13681 ms, 1.01x the slowest solo stream and
0.64x all-sequential. That is about 10% under the last two PRs' readings; pinned numbers drift
between sessions, so it is recorded as not slower rather than as a gain.

Baseline **525 / 0 / 1** (was 522 / 0 / 1; 3 new = the two attach cases and the `buildStreamJob`
case), **526 / 0 / 0** at release. **26 suites.**

#### `ProcessedStreamData` is plain C++ too (de-Qt, DTOs step 2)

The core's output struct no longer holds a Qt type: `streamLabel` is a UTF-8 `std::string`, and
`processedstreamdata.h` includes no Qt header (it now takes `StreamMode` from `streammode.h`
rather than the whole of `streamconfig.h`). The core is now `std` end to end on both sides: the
label goes in as `ProcessingParams::streamLabel` and comes out unchanged, so `FrameProcessor` copies
it instead of converting it.

The conversion sits where the result meets Qt, in two places. `PlotViewModel::addStreamData` decodes
the label once into a local and uses it for the three things the label does there: it names the
lock and missed-frames series, prefixes each SNR series' name, and is what reprocessing matches on
to replace a stream instead of stacking a second copy. `MainView::onStreamProcessed` decodes it for
the log line. `PlotSeriesData::streamLabel` stays `QString` with the rest of the plot's data.

**Tests first, for the plot half.** New `TestPlotViewModel::aNonAsciiStreamLabelReachesEverySeries`
was written against the `QString` field and passed there before the port: a label with an accented
letter and a Greek capital omega names the lock and missed-frames series, appears in the SNR series'
name, and a second run of the same stream replaces its series (4 stay 4) while a second stream that
shares the label but not the channel is left alone. (Its first draft expected 3 series; a receiver
stream also contributes a lock series, and the code was right.) The four test helpers that build a
`ProcessedStreamData` from a `QString` label now convert with `toStdString()`; no assertion changed
except two that decode the result's label to compare it.

**One test was written after the port, and says so.** `TestMainView::aFinishedStreamIsLoggedUnderItsLabel`
covers the log line, which nothing tested. It could not be run against the `QString` version without
reverting the port, so it is validated by mutation instead: it fails when the label is decoded as
Latin-1.

**Validated against five single-defect mutants; each fails the test written for it.** The label
decoded as Latin-1 in the plot (the new plot case); reprocess matching never equal (the new case plus
two existing reprocess cases); the SNR series losing its label (the new case); `FrameProcessor` not
copying the label into the result (the non-ASCII message row and `processAccumulatesReceiverData`);
and the log line decoded as Latin-1 (the new `MainView` case). Each build was checked to have relinked.

Throughput, pinned release gate: 4-stream parallel 15306 ms, 1.04x the slowest solo stream and
0.67x all-sequential. Nothing here is on the per-bit path.

Baseline **522 / 0 / 1** (was 520 / 0 / 1; 2 new = the plot case and the log-line case),
**523 / 0 / 0** at release. **26 suites.**

#### `ProcessingParams` is plain C++: UTF-8 label and a `std::filesystem::path` (de-Qt, DTOs step 1)

The core's input struct no longer holds Qt types: `filename` is a `std::filesystem::path` and
`streamLabel` a UTF-8 `std::string`. `processingparams.h` includes no Qt header at all. The layers
that still hold `QString` convert where they fill the struct in (`MainViewModel` building a job,
`CalibrationExtractor` building its request) and where they read it back (`FrameProcessor` copies the
label into `ProcessedStreamData`, which is still a `QString` until the next slice; the coordinator
passes the path straight to the reader, which already took one).

**`StreamMode` moved to its own header, `include/dto/streammode.h`.** `ProcessingParams` names the
mode, and the enum used to live in `streamconfig.h`, which carries Qt types for the dialogs. Pulling
that header in for one enum would have kept the struct Qt-bound, so the enum moved and
`streamconfig.h` includes it. Nothing else changed about it. That include had also been the only way
`calibrationextractor.h` got `FrameSyncParams`; it now includes `framesyncparams.h` itself.

**Tests first, as before, and two of them are new behaviour pinned against the old types.**

- `reportsExactlyTheseMessages` gained a tenth row, **`non-ascii-label`**: a stream labelled
  `R<e-acute>cepteur <omega>` is quoted in every tag (`[CH 7 R<e-acute>cepteur <omega>] `) and comes
  back unchanged in the result's `streamLabel`. It passed against the `QString` field before the
  port, and only the one helper that assigns the label changed afterwards.
- `TestProcessingCoordinator::aRecordingUnderANonAsciiFolderProcessesEndToEnd` copies the committed
  `step_cal` recording under a folder named `caf<e-acute>-<omega>`, lists its channels with the
  metadata reader, then runs the whole pipeline on it through the coordinator and expects samples.
  The earlier non-ASCII path fix proved each reader opens such a path on its own; this proves the
  path survives the trip from a job's file name to the reader, which is the hand-off this change
  rewrote.

**Validated against three single-defect mutants; each fails the test written for it.** The label
decoded as Latin-1 on its way into the result (the new row, which compares the result's label);
the label dropped on that path (the new row and `processAccumulatesReceiverData`); and the
coordinator handing the reader an empty path (the end-to-end test, the throughput benchmark and the
ordering test). Each build was checked to have relinked.

Throughput, pinned release gate: 4-stream parallel 15561 ms, 1.00x the slowest solo stream and
0.66x all-sequential. `main` read 15646 ms in the previous session; pinned numbers drift between
sessions, so this shows nothing slower rather than a gain. Nothing here touches the per-bit path:
the label is read once per stream and the path once per run.

Baseline **520 / 0 / 1** (was 518 / 0 / 1; 2 new = the label row and the end-to-end case),
**521 / 0 / 0** at release. **26 suites.**

#### `Ch10PacketReader` has no Qt: the same event sink, plus progress (de-Qt, `QObject` step 2)

The single-pass reader was a `QObject` emitting `progressUpdated`, `logMessage`, `errorOccurred`
and `finished`. It is now a plain class reporting to the same `ProcessingEvents` interface as
`FrameProcessor`, which gained one optional method for it, `progressUpdated(int)` (a no-op by
default, so a sink that shows no progress ignores it). `ch10packetreader.h` and `.cpp` contain no
Qt: the path is a `std::filesystem::path`, errors are `std::string`, the routing table is a
`std::unordered_map`, the file size comes from `std::filesystem::file_size`, and messages are
built with `std::format`. The Qt adapter, `QtProcessingEvents`, takes a fifth optional handler for
progress and delivers it like the rest: queued to the target, in order.

**`finished()` is gone, not ported.** Nothing connected to it: the coordinator and the extractor
learn that the reader is done by joining its thread at teardown, and they learn a stream is done
from its worker. The reader therefore never calls `processingFinished`, and the interface now says
so instead of implying every source does.

Both consumers changed the way the worker side did: the reader is `shared_ptr`-owned and runs on a
`QThread::create` thread, the adapter relays its reports into the coordinator's and the
extractor's existing signals, and the explicit `delete` at teardown becomes a `reset()` once the
thread is joined. `prepare()` is still synchronous on the caller's thread, before the thread starts.

**The reader had no tests of its own, so the characterisation suite was written from nothing, and
against the signal-based reader first.** New `TestCh10PacketReader` (6 methods, 15 results) pins what
the reader reports and what it hands to the queues, on the committed `step_cal` recording:

- each way `prepare()` can fail has its own exact message (a missing file, a text file, an empty
  file and an unknown PCM channel each read differently, and a stub too short to sync time says so);
- a full run reports "Reading Chapter 10 file...", never an error, progress that never goes
  backwards and ends at exactly 100, and delivers all 1598 packets (6,539,016 payload bytes, the
  4-byte channel header stripped) followed by exactly one end marker;
- an abort is announced, still reports 100, and still posts the end marker, which is what releases
  the worker that would otherwise wait on its queue forever;
- two streams on one channel each get every packet, and **the payload is one shared buffer**: the
  reason the fan-out is cheap, which nothing could previously see;
- and the ways a file can go wrong mid-read, with the cut points found by walking the packet
  headers rather than guessed: a cut **inside a PCM packet's data** and **inside a time packet's data**
  both report "File read error; aborting parsing." and deliver exactly the packets before the cut; a
  **damaged header checksum** reports "File read error during data collection" and stops there, with
  everything after lost.

**Two behaviours this turned up, pinned as they are rather than changed.** A file that ends **inside
a packet header** is read as a clean end of file: no error, so a copy cut off there looks complete.
And a damaged **sync pattern** is skipped silently: the library scans on to the next sync, no error
is reported, and exactly one packet is missing. Either may deserve a warning; that is a separate
decision from a refactor.

The suite passes unchanged across the port; only three helper functions (listen to the reader,
prepare it, run it) changed. Its `Drain` consumer gives up after a timeout and closes the queue,
because a reader that never posts its end marker used to make the test wait for ever instead of
failing.

Two things it cannot reach, stated rather than glossed: the "N% read..." lines (the fixture reads
faster than the 100 ms progress throttle, so only the throttle's own suite sees them) and the
"time gap(s) detected" warning, which needs a recording with a gap in its IRIG time packets. There
is none in the repository, and building one means hand-writing Chapter 10 packets with checksums.

`TestQtProcessingEvents` gained a progress case (101 values from a worker thread arrive on the
target's thread in order). The coordinator's end-to-end test now checks the progress bar: 0 at the
start, then 100 reported **twice**, once by the reader and once by the coordinator when it
finishes, so a reader whose progress never reached the coordinator would leave exactly one. A new
`TestCalibrationExtractor::extractionReportsItsProgress` does the same for the Extract Calibration
dialog's progress, which nothing observed before.

**Validated against eleven single-defect mutants; all caught.**

| mutant | caught by |
|---|---|
| final progress (100) never reported | 4 reader cases and the coordinator end-to-end test |
| abort not announced | `cancelReportsItAndStillEndsTheStream` |
| only the first queue of a channel gets packets | the fan-out case |
| each queue gets its own deep copy of the payload | the fan-out case (the pointers differ) |
| end marker never posted | every reader case, each naming the cause; the pipeline suites then abort |
| progress delivered directly, not queued | the run crashes |
| coordinator drops the reader's progress | the coordinator end-to-end test (100 reported once, not twice) |
| extractor drops the reader's progress | `extractionReportsItsProgress` |
| header read error not reported | the damaged-checksum row |
| time-packet read error not reported | the cut-inside-a-time-packet row |
| PCM-packet read error not reported | the cut-inside-a-PCM-packet row |

**The first pass left one of those uncaught, and it is why the truncation test cuts in three places.**
The original test cut the file at its midpoint, which always lands in a PCM packet, so the identical
error on the time-packet read could be silenced without any test noticing. Finding where to cut meant
walking the file's packet headers in the test (sync `0xEB25`, channel at byte 2, length at byte 4,
data type at byte 15), with a check that the walk tiles the file exactly. Separately, the first
mutation run also ran against a mutation script that had been corrupted by shell escaping, which
would have reported every mutant as a survivor; it was stopped, the script rebuilt, and the runner
now reports a script failure as "not a result" instead of running the unmutated code.

Throughput, pinned release gate and interleaved with `main` because pinned numbers drift between
sessions: this branch 14546 / 15277 / 14955 ms, `main` 15646 ms in the same stretch. Not slower. The
only per-packet cost added is the routing lookup, now a `std::unordered_map`.

Baseline **518 / 0 / 1** (was 501 / 0 / 1; 17 new = 15 reader results + 1 hand-off case + 1 extractor
case; the coordinator test gained assertions but no result), **519 / 0 / 0** at release. **26 suites.**

#### `FrameProcessor` has no Qt: it reports through an event sink (de-Qt, `QObject` step 1)

`FrameProcessor` was a `QObject` that emitted `logMessage`, `errorOccurred` and
`processingFinished`. It is now a plain class: what it has to say goes to a
**`ProcessingEvents`** sink (`include/model/processingevents.h`: three methods, UTF-8
`std::string` messages) set with `setEvents()`. `frameprocessor.h` and `.cpp` contain no Qt, the
messages are built with `std::format` (C++20, the reason for that change), and the class needs
neither `moc` nor a thread with an event loop.

**The Qt side stays Qt.** `include/qtprocessingevents.h` is a `ProcessingEvents` that posts each call
to a target `QObject` with a queued invocation, which is the delivery the signals gave: events from
one source arrive on the target's thread in the order they were raised. `ProcessingCoordinator` and
`CalibrationExtractor` keep their own signals and create the adapter to forward into them. The
adapter is Qt, so the C# shell will implement the same three-method interface over its own
mechanism rather than reuse it.

Two consequences in the consumers, both small but not mechanical:

- **The worker thread is a `QThread::create`, not an event-loop thread.** It runs `process()` and
  ends when that returns. The processor is owned by `shared_ptr` (held by `Worker` and by the thread's
  body) instead of `deleteLater` on `QThread::finished`, so it is freed when the coordinator clears
  its workers after the join.
- **The finish handler looks the processor up by job index at delivery time** rather than holding a
  pointer to it. An event delivered after teardown then finds nothing, instead of a dangling object.
  (Qt drops events still queued for a destroyed target, which `TestQtProcessingEvents` checks.)

**Tests first, as before.** `reportsExactlyTheseMessages` (9 rows) was written against the signals
implementation and passed there before anything was ported. It pins the full text and order of
everything `FrameProcessor` reports: success, a bounded window (named; an unbounded one is not), a
masked pattern, sync-but-no-frames, cancel, both "internal error" cases, and the two byte-swap
warnings. The mixed-byte-order warning had **no test at all** before this. Only its capture function
changed across the port, and all nine rows pass unchanged.

New suite **`TestQtProcessingEvents`** (4 cases) pins the hand-off itself: 300 mixed events from a
worker thread arrive on the target's thread in order with completion last; UTF-8 decodes; missing
handlers are skipped; and events for a destroyed target are dropped. New
`TestProcessingCoordinator::workerReportsPrecedeTheResultAndFinishComesLast` checks the same
property through a real run: the worker's completion line is relayed before the stream's result,
and `processingFinished` is last.

**Validated against eight single-defect mutants; all caught.** Delivered directly instead of queued
(the run crashes with an access violation: handlers run on the worker thread); completion alone
delivered directly (it overtakes the log lines; also crashes); the message read as Latin-1; the cancel
path not reporting completion; elapsed time with two decimals; the label always appended; the pattern
in lower case (caught only by the real-file test, since the synthetic rows use `0x9`, which has no
letter); and error reports dropped (11 cases). **Two of them abort the run rather than fail a case,
which still counts: the race they create is exactly what the queued hand-off exists to prevent.**

Throughput: pinned release gate 13866 ms, 1.01x the slowest solo stream, 0.65x sequential; the
only per-event cost is one null-checked virtual call.

Baseline **501 / 0 / 1** (was 485 / 0 / 1; 16 new = 9 message rows + 4 hand-off cases + 1
coordinator case + the new suite's `initTestCase` / `cleanupTestCase`), **502 / 0 / 0** at release.
**25 suites.**

#### The step-file parser is `std::string` and `std::filesystem::path`, and shares its file reading (de-Qt, `QString` step 3)

`StepDetector::parseStepConfig` (the `[[Step]]` calibration file) and `initialStepConfigPath` no longer
use Qt: `QFile` / `QTextStream` / `QFileInfo` are gone, the error is a UTF-8 `std::string`, and the
path is a `std::filesystem::path`. `initialStepConfigPath` stays string-in, string-out on purpose: its
result is shown in the Extract Calibration dialog, and `path / "x"` would put a backslash in the
middle of `<root>/settings/rcvr_cals/default.toml`. `stepdetector.h` and `.cpp` now contain no Qt.

**This changed course from "fold it into `TomlDocument`", and the reason is in the tests.** Writing
down what the parser actually does showed it is not the duplicate it looked like. It matches `[[Step]]`
and its keys **case-insensitively**, where `TomlDocument` is case-sensitive; it does **not** strip
quotes, so `db = "6"` is skipped and the step keeps 0, where `TomlDocument` would read 6; and it
compares the whole header line, so `[[Step]] # first` is not a step header at all. A fold would have
changed what an existing calibration file means, for no gain in the de-Qt goal. So it is ported in
place with behaviour unchanged, and what it genuinely shares with `TomlDocument` is shared instead.

**Characterisation tests first, again**: 9 methods, 26 results, written against the `QString`
implementation and passing there before anything was ported. They pin the quirks above, plus: a key or
header before any step or inside another section belongs to no step; a step with no parseable `db`
still exists at 0; the file order and last-`db`-wins; the output cleared on every call, success or not;
`QString::toDouble`'s rules on the value (`+6`, `.5`, `5.`, `1e1` read; `"6"`, `6x`, `1,5`, `0x10`,
`1e999`, `+-6`, empty skipped); UTF-8 BOM with CRLF and UTF-16 LE and BE read; a non-ASCII path
opens; and both error messages name the file. Only a two-line `parseSteps()` helper changed across
the port.

**One reader, not two.** The BOM / UTF-16 decoding that `TomlDocument` had written moved out into
**`include/model/textfile.h`** (`readText`, `decodeText`, `utf8FileName`, `pathFromUtf8`), and the
number rules live in `include/textparse.h` from the previous step. `TomlDocument` and the step
parser both use them, so a Notepad-saved file behaves the same in either.

**Validated against ten single-defect mutants**; eight fail the test written for them: header
matched case-sensitively; key matched case-sensitively; a comment allowed on the header; the output
not cleared; a bad value dropping the step (6 cases fail); no leading `+`; no BOM / UTF-16 handling (6
cases across two suites); and `exists()` for `is_regular_file()` so a directory counts as a step file
(caught by a new `initialStepConfigPathIgnoresADirectory`, added because nothing covered it).
**Two survived, and both are equivalent rather than missed.** Ignoring keys outside a step has no
observable effect: a stray `db` sets the pending step, and `flush()` discards it unless a step is
open. And `pathFromUtf8` reading its bytes in the ANSI code page is unobservable in this process,
because the UTF-8 code-page manifest (the non-ASCII path fix) makes the ANSI page UTF-8; the
`char8_t` route stays as the correct spelling for any host without that manifest.

**A build-harness trap found along the way, worth knowing**: several mutant runs reported
`tests exit 0` with no failures when the test exe had not been produced at all. The link was
failing intermittently with `LNK1201` (error writing the program database), and the runner script
prints the exit status of the previous exe when `.\debug\...exe` does not exist. Disk space was fine
(195 GB free). Deleting `tests/debug/*.pdb` and relinking cleared it. Such runs were discarded, the
runner now deletes the exe, PDB and results first and refuses to report a result unless both exist
afterwards, and the affected mutants were redone.

Baseline **485 / 0 / 1** (was 458 / 0 / 1; 27 new = 26 step-parser results + the directory case),
**486 / 0 / 0** at release. 24 suites.

#### `FrameSetup` is UTF-8 `std::string` and `std::filesystem::path` (de-Qt, `QString` step 2)

The word map and receiver-parameters file reader/writer: `ParameterInfo::name`, parameter names, the
reserved-section list, `channelPrefix()` / `receiverParameterName()` / `receiverIndexFromName()` /
`channelPartOfName()`, error out-params, and every path are now `std::string` /
`std::filesystem::path`, and `m_parameters` is a `std::vector`. It is still a `QObject` and still takes
its calibration profiles in a `QHash`: the QObject step and the associative containers are later.

`ProcessedChannelSeries::name` follows (it is `param->name` copied out), so the core's output is
`std::string` end to end. The seam moves up a layer: `PlotViewModel` and `CalibrationExtractor` convert
the name to `QString` where they build a plot series and a report line. `StreamConfig`'s path fields and
`CalibrationChannelResult::name` stay `QString`, since the report text they feed is built with
`QString::arg` and is its own step.

**Tests first, again.** `receiverNameParsingMatchesQStringToInt` was written against the `QString`
implementation and passed there before anything changed. It pins what `QString::toInt` quietly did to a
name typed by hand into a receiver-parameters TOML: the token is the **last** `_RCVR`; whitespace and a
leading `+` are tolerated (`"L_RCVR 3"`, `"L_RCVR+4"`); a second sign, a negative, trailing junk and an
overflow read as "no receiver"; a name that *starts* with the token has no channel part to split off
(`"_RCVR2"` returns the whole name though its number parses); non-ASCII names pass through.

**One parser, not two.** Those rules are exactly what `TomlDocument` already reproduced for its numbers,
so they moved out of `tomldocument.cpp` into **`include/textparse.h`** (`trimmed`, `parseInteger`,
`parseDouble`) and both use it. A second hand-rolled copy would have been a place for them to drift
apart. `PlotViewModel` has its *own* receiver-number parse with slightly different rules (it accepts 0
and negatives); it was left alone and now reads a converted copy of the name.

**Validated against four single-defect mutants; each fails the test written for it**: the first
`_RCVR` instead of the last, a channel part split at index 0, an accepted receiver 0, and `trimmed()`
disabled. The last also fails 26 other cases across `TestFrameSetup`, `TestStreamConfigDialog` and
`TestTomlDocument`, which is what sharing the parser buys. The first attempt at the first mutant did
not apply (a pattern that missed on line endings) and reported a clean pass; it was caught because the
run printed no failing test, redone, and the source confirmed byte-identical afterwards.

**Throughput, checked rather than assumed**, since `ParameterInfo` is on the decode path and
`std::string` is 32 bytes against `QString`'s 24: pinned release gate, branch **14035 / 14030 / 14058
ms** against `main`'s **14989 / 14301 ms** straight after. Not slower. Pinned numbers are stable
*within* a session but drift about 5% *between* them (the same code read 13.6-13.7 s earlier the same day),
so only interleaved runs mean anything.

Baseline **458 / 0 / 1** (was 457 / 0 / 1), **459 / 0 / 0** at release. 24 suites.

#### `TomlDocument` is UTF-8 `std::string` and `std::filesystem::path` (de-Qt, `QString` step 1)

The reader/writer behind every settings file no longer uses Qt: keys and values are UTF-8
`std::string`, paths are `std::filesystem::path`, and `QFile`/`QTextStream` are `std::ifstream` /
`std::ofstream`. Callers still holding a `QString` convert at their own boundary: the new
**`include/qtbridge.h`** supplies `toFsPath()` for paths (via UTF-16, lossless for any name the file
system can hold), and text uses Qt 6's `toStdString()` / `fromStdString()`, which are UTF-8. It is
C++17 only (`from_chars` / `to_chars`), so it does not depend on the C++20 change.

**The order matters: the suite came first.** `TomlDocument` had no tests of its own. The new
`TestTomlDocument` (14 cases, 15 results) was written against the **Qt implementation** and passed
there before anything was ported, so it encodes what the code did, not what the rewrite happens to
do. Its four helpers are the only lines that touch `TomlDocument`'s string types; across the port only
those helpers changed, and every assertion passed unchanged against the new code.

What `QFile` + `QTextStream` + `QString` supplied for free, now reproduced and pinned:

- **Encoding:** a UTF-8 BOM is skipped, and UTF-16 LE or BE with a BOM is decoded. That is Qt's
  Unicode auto-detection, and older Notepad's "Unicode" setting writes UTF-16.
- **Numbers:** integers and doubles tolerate surrounding whitespace and a leading `+`, which
  `std::from_chars` does not. Out of range fails rather than saturating.
- **Writing:** doubles are `%g` with six significant digits (`QString::number`'s spelling: `3.0` writes
  as a bare `3`, `0.00001` as `"1e-05"`), and the file is written with CRLF and no BOM.
- **A trap the old API had too:** `setValue("K", "text")` binds a string literal to the `bool` overload,
  because a pointer-to-bool conversion beats the one to `std::string`. A `const char*` overload now
  catches it.

Two narrowings, neither reachable from a shipped file: whitespace trimming is ASCII only (Qt also
trimmed Unicode spaces such as U+00A0), and invalid UTF-8 is kept as bytes rather than replaced.

**Validated against four single-defect mutants of the port; each fails the test written for it**: BOM
handling removed (the BOM case and both UTF-16 rows), LF instead of CRLF (the exact-file-format case),
shortest-round-trip doubles instead of `%g` (the doubles case), and no whitespace / `+` tolerance (the
integer case). **The first mutation run was wrong, and was caught.** Run in a loop, two mutants
reported exactly the failures of the mutant before them: the loop checked that the source recompiled
but not that the test exe was relinked, and a stale binary ran. Each mutant was rerun on its own with
the build checked, and the table above comes from those runs.

Consumers: `FrameSetup` (word map, receiver parameters) and the frame-sync dialog helpers in
`streamsubdialogs.cpp` convert at the call; both are next in the `QString` sweep. Baseline **457 / 0 / 1**
(was 440 / 0 / 1; 17 new = 15 results + `initTestCase` / `cleanupTestCase`), **458 / 0 / 0** at release.
24 suites.

#### C++20 (ahead of the de-Qt string work)

The language standard moves from C++17 to **C++20**: `CONFIG += c++20` in both `.pro` files (MSVC
`/std:c++20`), plus `gen_compile_flags.py` for clangd and `.vscode/c_cpp_properties.json` for
cpptools. It is its own change so that a standard-related problem cannot hide inside a refactor.

**Why now.** The core builds its log lines and the calibration report with `QString::arg` and
`QString::number`, and C++17 has no standard replacement. C++20's `std::format` is one, and it is
Microsoft's own implementation in MSVC's standard library. The `QString` step will hold core
strings as UTF-8 `std::string`, converting at the Qt boundary, which is consistent with the UTF-8
process code page that the non-ASCII path fix set.

**Verified on a clean build, because an incremental one cannot verify this.** jom recompiles an
object when its source or headers change, not when a compiler flag does, so after a standard bump
an incremental build would relink every C++17 object and report success: the same false pass as
the deleted-header trap above. Every object in `build/` and `tests/`, Debug and Release, was deleted
first. Then:

- Debug: app 63 translation units, tests 109, **0 warnings, 0 errors**; suite **440 / 0 / 1**.
- Release: app 63, tests 109, **0 warnings**; the pinned throughput gate **441 / 0 / 0**, parallel
  13703 ms against 13596-13614 ms for `main`. No change.

Nothing in the code needed to change. The usual C++20 breakages - aggregates with user-declared
constructors, `u8` literals becoming `char8_t`, rewritten comparison operators - do not occur here.

#### A recording under a non-ASCII folder could be listed but not processed (US1.0)

Found while surveying `QString` for the de-Qt sweep, not reported. Both Chapter 10 readers hand the
path to irig106's `enI106Ch10Open()`, which opens it with the C runtime's narrow `open(char*)` - and
they encoded it **differently**: `Chapter10Reader` with `toLocal8Bit()` (the ANSI code page, which is
what that `open()` reads), `Ch10PacketReader` with `toUtf8()`. So a file under `C:\Users\Jos<e-acute>\`
- users' own folders being exactly where recordings get copied - opened in the Configure Streams
dialog, listed its channels, and failed with "Error opening data file." the moment Process was
pressed. A folder name the ANSI code page cannot spell at all (any non-Latin script on a Western
system) could not be opened by either reader.

**Reproduced before it was fixed.** New `TestFrameProcessor::nonAsciiPathOpensInBothReaders` copies a
committed fixture into a `caf<e-acute>` folder and an `omega-<Greek capital omega>` folder and opens each with both readers.
Against the old code: the Latin row failed in the packet reader only, the Greek row in the metadata
reader already.

**The fix is the process code page, not the string.** `lib/irig106` may not be edited, so its narrow
`open()` stays; the new **`codepage.pri`** merges an `activeCodePage` = `UTF-8` manifest
(`resources/utf8codepage.manifest`, Windows 10 1903+) into both executables with `/MANIFESTINPUT`.
Under it the CRT's narrow file functions take UTF-8, and Qt's `toLocal8Bit()` produces UTF-8, so both
readers now pass `toLocal8Bit()`: any path opens, and on an older Windows that ignores the setting the
two readers still agree, which they did not before. Verified with `mt.exe` that both the app and the
test exe embed the setting alongside the existing Common Controls dependency, and that the generated
Visual Studio projects carry it. The whole widget suite now runs under the UTF-8 code page; nothing
else in the application noticed.

Not addressed: a path longer than `MAX_PATH` (260). irig106 stores the name in a `char[MAX_PATH]`
and the CRT's `open()` needs long-path support enabled machine-wide - a separate change.

Baseline **440 / 0 / 1** (the test is data-driven: two rows), **441 / 0 / 0** at release.

#### The pipeline's internal lists are `std::vector`, and the decoder got 7% faster (de-Qt, step 7)

`FrameProcessor`'s enabled-parameter list and per-frame word buffer, `Ch10PacketReader`'s channel
table and queue lists, and the `ProcessingParams*` list handed to `Ch10PacketReader::prepare()` move
from `QVector` to `std::vector`. `ch10packetreader.h` and `frameprocessor.h` no longer include
`<QVector>` at all. The routing table stays a `QHash` (now of `std::vector`s): replacing Qt's
associative containers is its own step.

**It is the first step of the sweep to measurably change throughput, and it is faster.** Release
gate, pinned to the performance cores (previous entry), interleaved with unmodified `main`:

| run | binary | 4-stream parallel | slowest solo |
|---|---|---|---|
| main (PR #147's two runs) | main | 14696 / 14594 ms | 14531 / 14535 ms |
| branch | branch | 13614 ms | 13529 ms |
| branch | same binary | 13596 ms | 13484 ms |
| main, straight after | main | 14690 ms | 14455 ms |

About **7%** on every pinned run, reproducible in both directions. The only part of this change on
the per-bit path is `ScanState::frame_words`: the scanner writes it once per word and reads it once
per enabled parameter per frame, through a non-const reference. A non-const `QVector` subscript
checks whether the buffer is shared, and detaches if so, on **every** access; `std::vector`'s is a
plain index. That is the likely source of the gain, inferred from the code rather than profiled.

Nothing else here touches a hot path - the queue lists are walked once per packet, the channel
table once at `prepare()` - and the one logic change, `QVector::contains` -> `std::find` when
de-duplicating the end-of-stream queue list, is covered by the pipeline suites: inverted, it would
post no sentinels and every worker would wait forever.

Baseline unchanged, **438 / 0 / 1**, **439 / 0 / 0** at release.

#### The throughput gate runs on the performance cores, so its milliseconds mean something

`scripts/run_throughput_bench.ps1` now pins the suite to the CPU's highest efficiency class before it
runs, restores the affinity afterwards, and prints the mask it used. `-AllCores` opts out.

The cause of the bimodal timings in the entry below was **tested, not assumed**. The same executable,
pinned each way:

| affinity | 4-stream parallel | slowest solo |
|---|---|---|
| efficiency cores only (`0xFFF0000`) | 26112 ms | 24965 ms |
| performance cores only (`0xFFFF`) | 14189 ms | 14178 ms |

The efficiency-core run reproduces the slow mode exactly, so Windows was sometimes scheduling the
single-threaded decoder onto efficiency cores. Two pinned runs back to back afterwards - the pattern
that had produced 14962 ms and then 26386 ms from one binary - measured **14696 ms and 14594 ms**
(slowest solo 14531 and 14535 ms).

- **The mask is discovered, not hard-coded.** `GetLogicalProcessorInformationEx` reports each
  physical core's `EfficiencyClass`; the cores in the highest class are the performance cores. On
  this i7-14700KF that is 8 cores / 16 logical processors (`0xFFFF`), and on another machine it is
  whatever that machine reports. A CPU with one core class, or more than one processor group, is
  left unpinned and says so.
- **It relies on inheritance, not on `Start-Process`.** The script already runs the test exe
  directly, to avoid Windows PowerShell 5.1's empty-`ExitCode` trap, and a child process inherits
  its parent's affinity. So the script pins itself and the exe follows.
- The ratios were never at risk - they held at 0.97-1.01x in every run, pinned or not - but the gate
  is also the only place this project measures throughput at all, and a measurement that halves
  depending on the scheduler is not one anybody can compare against.

#### The processing result is `std::vector`; the plot deliberately stays `QVector` (de-Qt, step 6)

`ProcessedStreamData` and `ProcessedChannelSeries` - what `FrameProcessor` produces and the
coordinator hands over - are now `std::vector`. That removes the seam step 5 left in
`CalibrationExtractor` and its test: the detector now reads the channel's values directly.

**`PlotSeriesData` and `TmChart` keep `QVector`, and that is the boundary, not unfinished work.**
The plot relies on implicit sharing in three places. `PlotViewModel::addStreamData` builds one
`elapsed` time axis per stream and every series of that stream shares it; `TmChart::setSeriesData`
keeps its own reference to each series' buffers, so a full chart rebuild copies nothing (its comment
says so, with 48 series over a long recording in mind); and pushing a series onto the list copies a
reference count. A `std::vector` turns each into a deep copy - every series its own time axis and
every rebuild a copy of every sample. The view model and the chart are the layers the port replaces
wholesale, so they keep Qt's containers until then. `toPlotVector()` in `plotviewmodel.cpp` pays
the one copy, once per series per stream; steady-state memory is unchanged, because the result is
dropped as soon as the signal returns.

The `streamProcessed` signal chain (coordinator -> main view model -> view) was checked first, because
a queued hop would copy the whole result: all three hops are direct, on the main thread, and
`ProcessedStreamData` has no metatype registration, so a queued hop could not have worked anyway.

New case **`seriesOfOneStreamShareOneTimeAxis`** pins the sharing, which no value-comparing test can
see. Mutation-checked: giving one series its own copy of the axis fails it.

**The release gate's absolute timings are bimodal on this machine, and that was nearly misread as a
regression.** The first branch run read 26364 ms against about 14000 ms before, with every channel,
solo runs included, slower by the same ~1.9x and every ratio unchanged. Two A/B pairs followed:

| run | binary | 4-stream parallel | slowest solo |
|---|---|---|---|
| branch 1 | branch | 26364 ms | 26710 ms |
| main 1 | main | 14343 ms | 14193 ms |
| branch 2 | branch | 14962 ms | 14926 ms |
| branch 3 | **same binary as branch 2** (0 files compiled) | 26386 ms | 27182 ms |
| main 2 | main | 14123 ms | 14025 ms |

Branch 2 and branch 3 are one executable, 14962 ms and then 26386 ms, so the slow mode is the machine
and not the code. The change cannot account for it anyway: the benchmark samples at 1 s, so the
touched code runs a few hundred times per run against billions of bit operations in the unchanged
decoder. The cause - the i7-14700KF's efficiency cores - was confirmed afterwards and the gate now
pins itself to the performance cores; see the entry above.
The guard asserts ratios, which held at 0.97-1.01x in every run, so it is unaffected; **absolute
milliseconds quoted from single runs in the entries above are less trustworthy than they were
presented**, and comparisons between runs should be A/B pairs or pinned to the performance cores.

Baseline **438 / 0 / 1** (was 437 / 0 / 1), **439 / 0 / 0** at release. 23 suites.

#### The calibration path uses `std::vector`, not `QVector` (de-Qt, step 5)

`QVector` is about 500 occurrences across the model, the DTOs, the view model, the view and 12
test files, and it cannot be swapped in one move: the data flows through it. So this is the first
of several steps, taken leaf-first like the earlier ones. It covers `StepDetector`,
`CalibrationExtractor`, `CalibrationProfile::points`, `StepDefinition` lists, and the dialog and
tests that touch them. The view-only uses (`QTreeWidget*`, `QPolygonF`, `QLineF`, widget lists)
are Qt-API lists and stay as they are.

**`size()` is the hazard, not `append`.** `QVector::size()` is signed; `std::vector::size()` is
unsigned. The code indexes with `int` everywhere, and MSVC at `/W3` does **not** warn on
`int i < v.size()` (it did flag `size_t` -> `int` truncations, C4267, which is how most sites were
found). Left alone, those comparisons are correct only while the index is non-negative. So every
`.size()` on a converted container goes through a new `intSize()` (`include/containerutil.h`),
which returns an `int` and keeps the signed idiom, rather than rewriting each loop to unsigned and
auditing each subtraction. One site genuinely needed more: `deriv.reserve(size - 1)` on an empty
series passes -1, which `QVector` ignored and `std::vector` reads as 18 quintillion elements and
throws. It is guarded, and `detect()` also rejects fewer than two samples before reaching it.

**`QVector<bool>` became `std::vector<char>`, not `std::vector<bool>`.** The latter is a
bit-packed proxy container (no real references, no `data()`), a trap rather than a saving for
per-sample flags.

Other `QVector` conveniences with no `std::vector` equivalent were replaced explicitly:
`first()/last()` -> `front()/back()`, `mid(pos, n)` -> an iterator-range copy, `contains()` ->
`std::find` (same exact-`==` semantics), `<<` and `append` -> `push_back` or a braced list.

**A temporary seam.** `ProcessedChannelSeries::values` is still a `QVector<double>` (the DTOs convert
in a later step), so the extractor builds the detector's `std::vector` from it with an iterator
range, and one test does the same. Both are marked, and both disappear when the DTO converts.

New case **`detectOnATooShortSeriesFailsCleanly`**: an empty and a one-sample series come back
"not calibrated". Checked by mutation: with `detect()`'s early return and the `reserve` guard both
removed, the suite dies with an unhandled `std::length_error` (exit `0xE06D7363`); restored, it passes.

Release throughput gate, full 640 MB recording: 4-stream parallel **13987 ms** against 14011 ms
(unmodified `main`) and 14201 ms (previous step), 1.01x vs the slowest solo stream, 0.64x vs
sequential. `interpolateCalibration` runs per sample and now reads a `std::vector`; no change
beyond the earlier runs' spread. Single runs; variance not measured.

Still `QVector`, in the order they will be converted: the DTO series (`ProcessedStreamData`,
`PlotSeriesData`, which reach the view model and the chart), the pointer lists inside
`FrameProcessor` / `Ch10PacketReader`, and the configuration lists (`StreamConfig`, `Source`,
`StreamJob`) in the view model.

Baseline **437 / 0 / 1** (was 436 / 0 / 1), **438 / 0 / 0** at release. 23 suites.

#### Packet bytes are a shared `std::vector`, not a `QByteArray` (de-Qt, step 4)

`PacketItem::payload` and the two Chapter 10 readers' read buffers were `QByteArray`; they
are now `std::vector<uint8_t>`. The readers' buffers are a straight swap (the library takes
a `void*`, and a `uint8_t` buffer removes a `reinterpret_cast` at the worker).

The payload is the part that needed thought, because **`QByteArray` was doing a job nobody
had written down**. The reader hands the *same* packet to every stream configured on a
channel with `q->enqueue(item)` in a loop, and it is the pipeline's single bottleneck. With
`QByteArray` that loop copied a reference count; each worker detached on its first write. A
`std::vector` member would have turned it into one full deep copy per queue, on the reader
thread - a throughput regression that no test in the suite would notice, since every copy
holds the same bytes. So the sharing is now explicit: `PacketPayload` is a
`std::shared_ptr<const std::vector<uint8_t>>`. The `const` is the point - nothing may modify
bytes another worker is reading - and a worker copies into its own buffer before it swaps,
inverts or derandomizes. That buffer lives outside the loop and is reused, so it stops
allocating once it has seen the largest packet. The cost against the old behaviour is one
memcpy per packet per worker, including the single-stream case where `QByteArray` never had
to copy at all.

Measured with the release throughput gate on the full 640 MB PRN recording, unmodified `main`
against this change: 4-stream parallel **14011 ms -> 14201 ms** (1.3%), ratio against the
slowest solo stream **1.01x** both times, against all-sequential **0.64x -> 0.65x**; the
solo runs moved by under 1%. One run each, and run-to-run variance was not measured, so
this shows no regression large enough to see, not that the cost is zero; the decode work
dwarfs a packet copy, which is why none was expected.

New case in `TestPacketQueue`, **`oneItemOnSeveralQueuesSharesItsBytes`**: one item enqueued
to three queues must come out of all three pointing at the same bytes. It guards the
property above, which is invisible to every value-comparing test. (Changing `payload` back to
a by-value vector also stops the test compiling, so the guard cannot be defeated quietly.)

Not done, deliberately: `Chapter10Reader` still builds a `QByteArray` from
`QString::toLocal8Bit()` for a filename. That is the `QString` step, not this one.

Baseline **436 / 0 / 1** (was 435 / 0 / 1), **437 / 0 / 0** at release. 23 suites.

#### `PacketQueue` is `std`, and finally has tests of its own (de-Qt, step 2)

`include/model/packetqueue.h` - the bounded hand-off between the single reader and each
worker - moves from `QMutex` / `QWaitCondition` / `QQueue` to `std::mutex` /
`std::condition_variable` / `std::deque`. It was the right next step: header-only, pure
infrastructure with no Qt semantics to preserve, and on the hot path, where the interop
spike had already shown `std::thread` driving the reader fine. `PacketItem::payload` stays a
`QByteArray` for now - converting it reaches into both the producer and the consumer, so it
is its own step.

**The queue had no direct test.** It was only exercised incidentally, through the whole
reader + `FrameProcessor` pipeline - tolerable on Qt's primitives, not tolerable the moment
they are swapped, because a lost wakeup or a wrong fullness test shows up there as a hang or
a silently shortened stream, far from its cause. New suite **`TestPacketQueue`** (11 cases):
FIFO order; every `PacketItem` field surviving, including an embedded NUL in the payload;
the producer blocking at *exactly* the configured depth and resuming after one dequeue; the
consumer blocking when empty and resuming after one enqueue; `close()` releasing a blocked
producer and a blocked consumer; a closed queue draining what it holds before reporting
end-of-stream; a non-positive depth falling back to 64; and 20,000 items through a depth-4
queue in order under constant hand-off.

**The tests were validated, not assumed.** Run in an isolated harness against the original
Qt-based queue and the rewrite, plus eight single-defect mutants of the rewrite:

| variant | result | caught by |
| --- | --- | --- |
| original Qt queue | **pass 13/13** | - (the tests encode existing behaviour) |
| `std` rewrite | **pass 13/13** | - |
| full at `depth + 1` | fail | the producer-blocks, depth-bound and fallback tests |
| LIFO instead of FIFO | fail | ordering and the 20,000-item stress test |
| `close()` never notifies | fail | `closeReleasesABlockedProducer` |
| `enqueue()` never notifies | fail | consumer-resumes and the stress test |
| `dequeue()` never notifies | fail | producer-resumes and the stress test |
| non-positive depth taken literally | fail | the fallback test |
| `close()` discards queued items | fail | the drain test |

Passing against the *original* is the stronger half: it shows the suite pins the behaviour
the pipeline already depended on rather than whatever the rewrite happens to do.

**Two things the mutation run found about the tests themselves.**

- A `close()` that records the flag but never wakes anyone **hung** the validation run past
  150 s instead of failing. The helper-thread guard closes the queue and then joins - and if
  `close()` is the broken part, that join waits forever. CI sets no `timeout-minutes`, so a
  hung suite would hold the self-hosted runner for GitHub's six-hour default. The join is now
  bounded (5 s) and aborts the process with a message naming the cause; that mutant now fails
  in about 10 s. The 30 s stress-test watchdog got the same treatment.
- The first matrix was meaningless: **every variant, including the original, "failed" with
  empty output.** Qt Test on Windows writes nothing to a piped stdout, so the harness had read
  nothing and a missing result counted as a failure. It now reads the `-o file,txt` results
  file, as the project's own harness does. A column of FAILs would have looked like the tests
  working.

Baseline **426 / 0 / 1** (was 413 / 0 / 1; 13 new lines = 11 cases + the framework's
`initTestCase` / `cleanupTestCase`), **427 / 0 / 0** at release. 22 suites.

#### The progress throttle is `std::chrono`, behind a tested gate (de-Qt, step 3)

The two `QElapsedTimer`s in the native core are gone. `FrameProcessor` keeps a plain
`steady_clock` start point and `duration_cast<milliseconds>` for the one-shot duration that
becomes the completion log's one-decimal seconds (both truncate to whole milliseconds, so the
printed figure is unchanged). `Ch10PacketReader::run()` gets a small new type,
**`IntervalGate`** (`include/model/intervalgate.h`, `<chrono>` only).

**Why a type rather than four inline lines.** The throttle is the one place in this change
that can go wrong *silently*: seconds where milliseconds were meant makes it never fire,
microseconds makes it fire on every packet, `>` where `>=` was drifts it late - and none of
those is caught by a build, or by the pipeline tests, because the recordings in the suite
read faster than the 100 ms interval and a time-based check inside a per-packet loop cannot
be observed from outside. With the time point injectable, the whole contract is pinned
deterministically with no sleeps. Same lesson as `PacketQueue`: a swap with no test under it
is a swap you are trusting rather than checking.

New suite **`TestIntervalGate`** (7 cases): not due before the interval, due at *exactly* the
interval, `due()` reporting without restarting the interval, milliseconds rather than seconds
or microseconds, `reset()` measuring from the reset, sub-millisecond agreement on every side
of the boundary, and a default-constructed gate starting at *now* rather than the clock's
epoch (which would make it due the instant it exists).

**Validated against seven single-defect mutants; every one fails**, each caught by the test
written for it: `>=` weakened to `>`; the interval read as seconds; read as microseconds;
`reset()` a no-op; `due()` restarting the interval itself; an epoch default start; and
rounding the elapsed time up so it fires up to a millisecond early. The real gate passes 9/9.

**Equivalence with `QElapsedTimer` was measured, not argued.** The injected-time tests pin the
gate's own contract but cannot show it matches the timer it replaced, because `QElapsedTimer`
has no injectable clock. So both were run against the real clock side by side, twelve runs,
reading the timer first and the gate second: the gate was **never late** (zero iterations with
the timer due and the gate not), and the first-due instants differed by **at most 1.3 us,
always on the early side**. That residue is the harness constructing the gate a moment before
calling `timer.start()`, not a property of the gate; at a 100 ms interval it is a relative
difference of about 1e-5. Stated as measured rather than called identical.

Baseline **435 / 0 / 1** (was 426 / 0 / 1; 9 new lines = 7 cases + the framework's
`initTestCase` / `cleanupTestCase`), **436 / 0 / 0** at release. 23 suites.

#### The model layer no longer pulls in QtGui (de-Qt, step 1)

`include/constants.h` held `PlotConstants`' `QColor` palettes alongside `PCMConstants`,
`AppVersion`, `UIConstants` and `CalibrationConstants` - so **every model translation unit
included `<QColor>`** and pulled in QtGui. Nothing in the model references a colour.

Found by trying to build the model as a standalone native DLL with `QT -= gui` (the interop
spike on `explore/dotnet-migration`), which died on `constants.h(11): fatal error C1083:
Cannot open include file: 'QColor'`. Compile-time coupling only - the palettes are
`inline constexpr` and unreferenced there, so no QtGui code was ever emitted into the model.

- `PlotConstants` moves to the new **`include/plotconstants.h`**, which owns the QtGui
  dependency and is included only by the View, the ViewModel and their tests.
- `constants.h` is now QtGui-free: `<array>`, `<cstdint>`, `<QString>`.
- The three CSV header tokens move into **`SeriesColumnSchema`** as `kTimeHeader`,
  `kLockSuffix` and `kMissedFramesSuffix`. They are a serialization contract rather than plot
  presentation, this file already documents itself as the single source of truth for the CSV
  column format, and their presence in `PlotConstants` is specifically what made the model
  depend on a QtGui-bearing header. They now sit beside the code that formats and parses them.

**Not finished by this.** Eight `QColor` references remain in the model and DTO layer - a hex
string round trip in `processingtemplateschema.cpp` and value fields in `plotseriesdata.h` and
`seriesappearance.h`. The transitive *header* coupling is gone, which is what blocked a
gui-free build; naming the type in three files is the next step.

#### The decommutation pipeline has a performance guard, and it had none before

`benchmarkHeavyWorkloadSingleVsMultiStream` existed, and measured the right thing, and was
**three separate kinds of inert**:

- **It asserted nothing.** It printed four solo timings and a parallel timing through
  `qWarning()` and then ended. A throughput regression would have passed, in a log nobody
  reads. Reporting is not guarding, and this is the one that mattered most.
- **It was opt-in behind `TMDQA_RUN_HEAVY_BENCH`, and nothing set it.** Not CI, not the
  release, not any script.
- **It was pointed at a fixture that cannot satisfy it.** The path moved from
  `prn_testfile.ch10` to `test files/prn_testfile, sample.ch10` in PR #105's fixture
  reorganization (v2.11.1), which read as a mechanical rename and was not: the committed
  77 MB sample carries PCM packets for channels **13 and 15 only**, while 14 and 16 appear
  in its TMATS with no data behind them. The benchmark times four channels, so enabling the
  variable produced no number - it probed four, recognized two, and skipped with a message
  blaming channel identification. Established with `scripts/ch10_triage.py`, which reports
  `no PCM packets on channel 14; available: [13, 15]`.

What changed:

- **The ratios are asserted**, not the milliseconds. Absolute times belong to the machine;
  the ratios belong to the architecture, and four historical runs held them to within 0.01.
  Limits are `parallel / slowest-solo <= 1.50` and `parallel / all-sequential <= 0.85`,
  against a measured 0.92 and 0.59 - deliberate headroom, because a reader serialized behind
  worker back-pressure (the regression this catches) would drive the first toward 4.0, not up
  a few percent. Below 4 cores the ratios describe the machine instead, so they are reported
  and explicitly NOT asserted.
- **It runs at every release**, via the new `scripts/run_throughput_bench.ps1`, which the
  `cut-release` skill's Step 5 now invokes in place of the by-hand suite run. It builds
  Release, enables the benchmark, fails on a skip or a missing fixture rather than letting
  either pass quietly, and prints the ratio block.
- **Not in CI.** CI was cut 286s -> 93s deliberately (PR #48); this adds minutes for a
  property that is not correctness, and the full recording is not committed anyway. CI still
  leaves the benchmark skipped, so its "exactly one skip" gate is unchanged.
- **Release baseline is 414 / 0 / 0**, where the ordinary baseline stays 413 / 0 / 1.

The `640 MB` figure in both `CLAUDE.md` files was **correct** and is unchanged - it describes
`prn_testfile, full.ch10`, measured at 640.1 MB. It was briefly "corrected" during this work
on the reasoning that the test read the sample; the test did read the sample, which was the
bug, not the documentation.

#### TOML parsing is plain file I/O, not a QSettings format

`TomlConfigHelper` implemented the settings-file subset as a custom `QSettings::Format`.
Replaced by `TomlDocument` (`include/model/tomldocument.h`), which reads and writes the same
subset directly. `QSettings` is a settings *store* - scoped, cached, merged, written through
an atomic rename - and using it as a file-format parser cost real work at every call site:

- **It sorted.** `childGroups()` came back alphabetically, but a receiver word map is
  meaningful in file order, so `FrameSetup::tryLoadingFile()` opened and parsed the same file
  a **second** time through a `readGroupsInFileOrder()` helper just to recover that order.
  Both are gone: one parse, and `sectionsInFileOrder()` is the primary interface.
- **It merged.** `writeReceiverParams()` had to `QFile::remove()` the target first or the old
  file's word map survived beside the new one. `TomlDocument::save()` truncates.
- **It needed global registration.** `registerFormat()` is process-wide and lazy, so `main()`
  *and* the test harness each had to call it before any config file could be read. Both calls
  are gone.
- **It cannot express arrays**, which is why `StepDetector::parseStepConfig` is a second,
  hand-written TOML parser for `[[Step]]` files. Unifying the two is the obvious follow-up and
  is deliberately **not** in this change: that parser feeds calibration, and it deserves its
  own verifiable step rather than riding along on this one.

`FrameSetup::saveToSettings(QSettings&)` becomes `writeWordMap(TomlDocument&)`, which is what
it always did. Scope note: this does **not** remove the Qt Core dependency and was not meant
to - `TomlDocument` still uses `QString`. The `QString`/`QVector` sweep that the assessment
also listed is not done, because the hybrid port it recommends keeps the model as a native DLL
that links Qt Core regardless, so that sweep buys nothing until a full C# rewrite.

**Value semantics are reproduced, not quietly tightened**, because a refactor whose claim is
"same subset" must not change how an existing file reads. `boolean()` is
`QVariant::toBool()`'s exact rule (lowercase; false only for empty, `0` or `false`; anything
else true), which is what the QSettings path went through. A stricter rule would arguably be
better and is left as its own decision.

Two differences are deliberate and named:

- **`integer()` returns the caller's fallback for a present-but-unparseable value**, where
  `QVariant::toInt()` returned 0 - silently turning a corrupt `Polarity` or `Slope` into
  index 0 rather than the default the caller passed. No call site wanted the old behaviour.
- **Escaping is symmetric.** The old writer escaped `\` and `"` on the way out but its reader
  only stripped the surrounding quotes, so neither character survived a round trip. No
  shipped key carries either, so this corrects an asymmetry rather than any real file's
  meaning - but reproducing it on purpose in fresh code seemed worse.

Application preference storage is untouched and still `QSettings` (theme, recent files,
sidebar and legend visibility), which is what that class is for.

### v2.14.2 — Shipped Defaults That Say What They Mean, and an Installer Test That Can Run

The application binary is unchanged from v2.14.1 apart from its version. Everything here
is a shipped data file, a document, or the tooling that checks them - which is precisely
the category this project keeps finding stale, because nothing in CI reads any of it.

Three of the four entries exist because something claimed to be true and was not: three
pattern files left a setting unstated, two acceptance criteria described an installer
feature that was never built, and the clean-machine install had been listed as untestable
since v2.13.1.

#### The shipped pattern files state Derandomize explicitly (US1.0, US2.0)

`framesync_PRN11.toml`, `framesync_PRN15.toml` and `framesync_rcvr_default.toml` now
carry `Randomized = false`.

**This changes what loading one does, deliberately.** v2.13.0 left the key out so a
pattern file would leave the operator's Derandomize choice alone - the reader gates on
`contains("Frame/Randomized")`, and an absent key means "not mentioned, do not touch".
With the key present, loading any of these three files now **sets** the box, so the
state after a load is always explicit rather than partly inherited from whatever was
ticked before.

**The value is a judgement about the recordings, not about the sync pattern.**
`Randomized` describes the recording's PCM code format, which the fixtures show plainly:
the RNRZ-L recording runs with it on, the Safran one with it off. The same PRN-15 pattern
appears in both. So `false` here is a statement that these shipped defaults target
non-randomized streams, and an operator whose stream is RNRZ-L still ticks the box - the
difference is that the file no longer silently leaves the previous answer standing.

A pattern file the operator saved before this is unaffected: it has no `Randomized` key,
so it keeps the leave-it-alone behaviour.

Verified through the real parser rather than by reading the syntax: all three report
`contains=yes` with `toBool=false`, and their sync patterns and bit counts are unchanged.

#### US8.0 now describes the installer that exists (US8.0)

Two acceptance criteria claimed the installer saved a conflicting shipped default as
`new_x.toml` and carried the user's old values into it. **Nothing implements that** -
`tmDataQualityAnalyzer.iss` is 95 declarative lines with no `[Code]` section, and there
is no `new_x`, migration, schema-version or merge logic in `src/`, `deploy/` or
`scripts/`. The clean-machine run above confirmed it from the other side: a first run
wrote nothing under `settings\` and no registry key.

They are **replaced, not deleted**, by the guarantee that is real: a settings file is
never overwritten, and a file written by any earlier version stays usable because every
reader supplies its own default for a key the file does not carry. The consequence is
stated plainly rather than left to be inferred - a field added to a shipped default
reaches a **fresh** install, not an existing one.

**The defensive readers are named in the criteria**, because they are what make the
weaker guarantee safe and a change removing that tolerance would otherwise look
harmless: `readReceiverParams()` accepts both the current and older count keys, an
absent `Randomized` leaves the operator's choice alone, `fromJson()` keeps in-class
defaults. The condition that would force a real merge is also written down - a shipped
default gaining a field whose absence an old file cannot express, which is exactly the
`Randomized` shape.

`UserGuide.txt` gains a section saying what an upgrade does to `settings\`, and loses a
stale claim while it is there: it still said Derandomize was "deliberately not saved",
which v2.13.0 made untrue when the flag started round-tripping through the pattern file.

Option B - actually building the merge - is not taken. It moves the installer from zero
custom code to scripted, which is the thing that currently makes it cheap to maintain,
and nobody has asked for it in fourteen releases.

#### The installer is tested on a machine that has never seen it (US8.0)

`deploy/sandbox/` makes the clean-machine install a command rather than an intention:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\sandbox\run_sandbox_test.ps1
```

It stages the newest `setup.exe`, generates a `.wsb`, launches **Windows Sandbox** - a
disposable VM that has never held this application - installs silently, launches, reads
what the first run did, uninstalls, and prints the results back on the host. Exit code
follows the result. `cut-release` gains it as Step 9.

**v2.13.1 named this gap and could not close it**: the first-run path "needs a genuinely
clean Windows target; Windows 11 Home offers neither Sandbox nor Hyper-V." The machine is
now on Pro, so it can be closed.

**Run against v2.14.1: all checks passed.** The result worth having is the DLL closure -
every `Qt6*`, `qwindows.dll`, `MSVCP140*` and `VCRUNTIME140*` loaded **from the install
directory** on a machine with no Qt at all. v2.14.1 was the first build on **Qt 6.12.0**
and that closure had last been verified on 6.11.1, so it was the one real untested risk in
the release. It also confirms, from the other direction, what the settings-upgrade
scoping argued from the code (its plan file was deleted on landing, per the
`docs/future_plans/` convention): a first run wrote **nothing** under `settings\` and no
registry key, so there is no `new_x.toml` merge to find.

- The `.wsb` is **generated, not committed**. Sandbox requires absolute host paths and does
  not expand environment variables in them, so a checked-in one would carry one
  developer's directory layout.
- **The harness refuses to start while a Sandbox is running, and says not to kill it.**
  Force-killing leaves the container half torn down and the next launch fails *silently* -
  no error, no window, nothing in the results directory.
- **A red result is not automatically a product defect.** The first run reported three
  failures and all three were the harness: `msvcp_win.dll` is a Windows UCRT component
  that correctly loads from System32 and is not in our payload, but matched a `^msvcp`
  filter; the `.ch10` registry KEY is deliberately left behind by `uninsdeletevalue` while
  the VALUE is the actual contract; and `Randomized = false` was asserted against a build
  that predates it. Each was checked against the payload and the `.iss` rather than taken
  at face value, and the corrected harness states this where a future reader will meet it.

**Two more of its own bugs surfaced the first time it met a real release, and both are
the same shape as the signing pre-flight's:** exercised once under the conditions that
happened to hold, then wrong on the next release.

- **It hardcoded the expected version.** `Check "exe version" ($v -like '2.14.1*')` passed
  for exactly one release and then called a correct `2.14.2.0` install a failure. This
  project keeps ONE version source and treats a literal anywhere else as a bug to remove,
  not a place to update - a rule this file states and the harness broke. It now derives
  the expectation from the `setup.exe` under test, which is the only version it has any
  business asserting. The remaining `2.14.1` literal in the `Randomized` gate stays: that
  is a historical boundary - the release the key landed after - not a moving target.
- **Its "a Sandbox is already running" guard matched the wrong process name.** It checked
  `-Name 'WindowsSandbox'` exactly, but the window process exits before
  `WindowsSandboxServer` and `WindowsSandboxRemoteSession` do - so it reported "clear"
  while a session was still alive, let a second launch collide with it, and that run
  produced no results at all. Measured: three processes alive with no `WindowsSandbox.exe`
  among them. The guard now matches `WindowsSandbox*` and names what it found.

The substantive result was unaffected: every product check passed against v2.14.2,
including `PRN15 carries Randomized = false` firing as a real assertion for the first time
rather than reporting INFO.

### v2.14.1 — Ticks That Match the Clock, and a Manual That Matches the Build

A corrective release. The processing core is untouched: nothing about how streams are
read, synced, calibrated or exported has changed, and the binary differs from v2.14.0
only in the plot's tick placement, the theme's reach into window frames, and the removal
of the on-chart legend toggle.

Three of the six entries below are repairs to v2.14.0 itself - the tick snapping it
introduced measured from the wrong origin, and the figures that shipped with it were
already stale. The other three are build and release tooling that had been costing time
quietly.

#### The portable README pointed at a repository that is not this one (US8.0)

`deploy/README_portable.txt` ships in every portable ZIP and gave the source URL as
`github.com/kevinbossoletti/tmDataQualityAnalyzer`; the remote is `kboss805`. Nothing
reads that file in CI, which is why it sat wrong - the same reason `UserGuide.txt` once
went eighteen months stale. Found by reading every shipped artifact during packaging,
not by a check, because there is no check that could know.

#### The manual's figures are re-captured, and the spares are gone (US8.0)

Thirteen captures re-taken against a clean 6.12.0 build: the six plot figures and
`walk-theme-01` / `walk-err-01` / `walk-batch-03` for the removed on-chart legend toggle
and the clock-rounded X ticks, the three menu figures for Export leaving the plot menu,
the icons on Open / Save as Template and Exit moving to the bottom, and
`walk-fs-04-processing` - which still carried a **v2.9.3** banner and the verbose
`stop=18446744073709551615s` log line that v2.11.0 removed, so it had been stale for four
releases. 32/32 captures placed; both manuals rebuilt.

**`$Spares` is now empty.** It held three - two unused halves of with/without-legend
pairs and a sidebar shot superseded by `walk-err-01-log-error.png`. All three predated the
legend-toggle removal, so none could have been placed without re-capturing first. A stale
spare looks like an option and is not one. The mechanism stays for the next deliberate
one.

**Two traps worth recording, because neither announces itself:**

- **A `$true` crop is size-validated; an explicit rectangle is not.** `walk-fs-04`'s
  `@(14, 14, 1936, 1119)` assumes a 1962x1142 full-screen grab, and two window-sized
  re-captures (1946x1130, then 1950x1129) fell under it. The failure is
  `System.Drawing`'s `"possibly due to a lack of memory"`, which actually means an
  out-of-bounds rectangle and sends the reader somewhere else entirely. Every rectangle
  is now checked against its image before a build, not just the strict crops.
- **The crosshair is cursor state and lands wherever the pointer was.** Four plot figures
  came back with a readout box sitting over the data, and the with/without-legend pair -
  the one place the manual puts two figures side by side so the legend is the only
  difference - also disagreed on whether the status bar was visible. Move the pointer off
  the chart before capturing.

#### env.ps1 stops printing a message nobody can act on

Every script that dot-sources `scripts/env.ps1` printed

    'vswhere.exe' is not recognized as an internal or external command

before doing its actual work. It came from inside Microsoft's `vcvars64.bat`, which
invokes `vswhere.exe` by **bare name**: vswhere is installed at the standard path - so
`env.ps1`'s own lookup finds it - but it is not on `PATH`, so vcvars falls through to its
own fallbacks and the import succeeds anyway. Harmless, not ours to fix, and printed on
every single run, which is how output stops being read at all.

- vcvars' stderr is discarded (`2>nul` inside the `cmd /c`, so only that call is
  silenced and `set` still flows).
- **The import is now judged by its result instead of its chatter.** A PARTIAL import -
  the failure that actually matters - was silent either way: it surfaces much later as
  qmake's `QMAKE_MSC_VER isn't set`, which names neither vcvars nor this script. If
  `INCLUDE` or `LIB` is unset after the import, `env.ps1` re-runs vcvars **with its
  output shown** and throws naming the path it ran.

Verified both directions: a fresh shell is silent and still gets `INCLUDE`, `LIB` and
`cl.exe`, an incremental build through it exits 0, and pointing `TMDQ_VCVARS` at a batch
file that sets nothing produces the re-run, the script's own output, and the throw.

#### X ticks land on round CLOCK times, not round elapsed values (US4.0)

v2.14.0 snapped the tick step to a natural interval, which fixed the spacing but not
where the ticks sit. A tick's VALUE is seconds since the recording's first sample,
while its LABEL is a time of day - and those are the same thing only when the
recording began exactly on a round second.

- Reported as "SNR files are not snapping to even numbers like 5 and 10 in the time,
  like the frame sync does". **It was never about SNR.** Frame-sync runs tend to start
  on a whole second, so their ticks landed on round clock times by luck; SNR runs
  sample from an arbitrary packet time and did not. Measured on three bases: a start at
  `:30:00.000` gives ticks reading `:00, :30, :00`, a start at `:30:17.046` gives
  `:17, :47, :17`, and a start at `:30:12.000` - no fraction involved at all - gives
  `:12, :42, :12`. Evenly spaced every time, round only in the first case.
- `TmChart::setTimeOrigin()` carries the clock time of x = 0, and `timeTickValues()`
  takes it as a fourth argument, snapping in origin-shifted space and shifting back.
  The default of 0 preserves the existing behaviour exactly, which is what the
  no-formatter and sub-second paths assume.
- **Set in `updateAxes()`, not once beside `setTimeFormatter()`.** `PlotViewModel`
  re-bases the origin whenever a source with an earlier first sample arrives, so a
  one-time copy would snap to the previous recording's start.

`timeTicksAreRoundOnTheClockNotInElapsedSeconds` asserts the CLOCK time of each tick is
the round number, across a mid-second start and a whole-second-but-not-round start, and
pins that omitting the origin is unchanged. Verified to fail against the elapsed-domain
snapping it replaces.

#### The light theme reaches dialog title bars (US9.0)

- Reported as the progress dialog keeping a dark title bar in the light theme - true of
  every ordinary dialog, not just that one. A stylesheet paints widgets and has no reach
  into a window's frame, which Windows draws from **Qt's colour scheme**. The
  application set the stylesheet and never the scheme. The main window hides this,
  being frameless with a title bar of its own; every dialog shows it.
- `MainView::applyApplicationTheme()` now sets both, and **startup and the theme toggle
  share it**. They previously loaded the stylesheet separately, which is how the two
  could disagree in the first place - adding the scheme call to one and not the other
  would have reproduced the bug in a different half of the application.

`lightThemeReachesNativeWindowFrames` asserts the stylesheet unconditionally and the
colour scheme wherever the platform supports one. **The offscreen plugin - which CI
uses - has no platform theme for the override to reach**: `setColorScheme` is inert
there, `colorScheme()` stays `Unknown` and no signal is emitted, so the test probes
whether the override is live rather than asserting into a vacuum. The scheme half
therefore runs on a developer machine and not in CI; it was verified to fail against a
build that applies only the stylesheet.

#### Signing fails in the first seconds, not the tenth minute

The release script's most fragile dependency was `signtool`, and all three of its
problems had already cost time.

- **The SDK path was pinned to `10.0.26100.0`.** An SDK update would silently demote
  it to the bare name on `PATH` - still working, until it did not. It is now
  discovered from the newest installed Windows SDK.
- **A missing signtool surfaced at step 5 of 8.** Packaging is a ten-minute run that
  signs near the end, so the check now runs as **step 0** and aborts before the build.
- **The certificate being present proves nothing.** It is token-backed (Certum
  SimplySign) and with the token locked it still reports valid with
  `HasPrivateKey = True`, so only an actual signature settles it. `build_release.ps1`
  now signs a throwaway copy under a timeout before building - the smoke test that was
  previously a manual ritual in the `cut-release` skill, and that cost a whole cycle
  the one time it was skipped.

Verified both ways against the real function: a bad thumbprint throws in **0.1 s**
carrying signtool's own message, and the real certificate passes, leaving no temp files
behind.

**That verification was run under the wrong shell, and the pre-flight was broken on
arrival.** It was exercised with `pwsh`, while `build_release.ps1` is invoked with
`powershell` - Windows PowerShell 5.1 - whose `Start-Process -PassThru` returns a
`Process` that has **not cached its handle**. Once the process exits the handle can no
longer be obtained, so `$p.ExitCode` reads back **empty**; empty is not 0, so the very
first real release run reported a *successful* signing as a failure, printing `exit `
and signtool's own `Successfully signed` in one message, and aborted before the build.

The fix is one line - touch `$p.Handle` **before** waiting - and it is measured rather
than assumed: against a deliberately bad thumbprint the exit code is `''` without it and
`1` with it. Re-verified under 5.1 in both directions: the real certificate passes, and a
bad thumbprint throws carrying `SignTool Error: No certificates were found...`. A null
exit code no longer fails the build either, because guessing in either direction is worse
than saying so - treating it as success would defeat the pre-flight, and treating it as
failure was this bug.

Second time in this release that something was verified under `pwsh` and shipped broken
for 5.1 (see `setup-env.ps1` below). **Release and setup scripts must be exercised with
`powershell`, not `pwsh`.**

That 36 s is why the timeout is **90 s rather than 45**: nearly all of it is the
timestamp-server round trip, so the shorter timeout would have reported a locked token
on a slow network - a confident, wrong diagnosis. The timeout message now names both
possible causes instead of asserting one.

The `cut-release` skill's Step 6 drops its hand-run snippet and says the script does
it, which is the point: a ritual nobody can forget.

#### Qt 6.12.0, and a deploy check that survives a version bump

- `QT_VERSION` in `scripts/env.ps1` and `.github/workflows/ci.yml` moves to **6.12.0**,
  which is the only Qt now installed - 6.11.1 is gone, so the build was broken until
  this landed. `compile_flags.txt` regenerated (its Qt paths are absolute and
  version-pinned), both `.qmake.stash` files wiped, and `gen_compile_flags.py`'s own
  fallback default moved with them, since a stale one there silently points clangd at
  a Qt that no longer exists. **411 / 0 / 1, zero warnings** on 6.12.0.

- **`deploy_qt_local.ps1` skipped re-deploying after the bump, and that is a bug worth
  the fix rather than the workaround.** Its "already deployed" check compared the
  deployed `Qt6Core.dll`'s LastWriteTime against the kit's - which answers the wrong
  question after a version change. Qt 6.12.0's DLLs carry an *older* build date than
  the 6.11.1 copies already sitting beside the exe, so the check skipped, the freshly
  linked test binary loaded the previous runtime, and it died with
  **`0xC0000139` (entry point not found)** and no results file - no message, nothing on
  screen to explain it.
  The check now compares the **Qt version** of the deployed DLL against the kit's, so a
  bump always re-deploys. This is the same family as the `0xC0000135` that followed the
  6.11.1 upgrade: local DLLs shadow `PATH`, so a stale copy beside the exe wins over a
  correct one on the search path.

- Not changed here, because it is a machine setting rather than a repository one: the
  **persistent user `QTDIR`** still points at 6.11.1. Builds are unaffected (`env.ps1`
  sets its own), but cpptools and Qt Creator read it. `env.ps1` warns on every run and
  names the fix - `scripts\setup-env.ps1`.

#### The on-chart legend toggle is gone; Show Legend stays in the menu (US4.1)

A control with two homes. `Show Legend` has been in the right-click menu since the
menu replaced the external control rows, and `L` has toggled it since the keyboard
shortcuts landed - so the chip was a third way to do the same thing, permanently
occupying chart space to duplicate a menu item one click away.

- The chip, its click handler, its event-filter registration, its two tooltip
  refreshes and its stylesheet rules are removed. The legend itself, the persisted
  `LegendVisible` preference, the menu item and the `L` key are untouched.
- **Two helpers were named after the button while actually operating on the whole
  bar**, so they are renamed to what they do: `positionLegendToggle()` →
  `positionOverlayBar()`, `styleLegendToggle()` → `styleOverlayBar()`. The latter's
  null-guard moved from the button to the bar, which is what it dereferences.
- The `:checked` stylesheet rule went with it - the two remaining chips are
  momentary actions, not states.

**A state the chip used to mask.** With both remaining chips self-hiding, a plot with
one left-axis metric and no zoom now shows a bar with NO children. Measured: it
collapses to 0x0 rather than painting an empty translucent box over the chart, and
`overlayBarAppearsOnlyWithData` now pins that.

`legendChipIsLabelledAndDescribed` is deleted - it tested only the removed button.
The three other legend tests keep their assertions and lose the chip ones;
`contextMenuShowLegendMirrorsToggle` becomes
`contextMenuShowLegendDrivesTheLegend`, since there is no longer a second control
to mirror.

**The manual's figures still show the chip.** They were already due for re-capture
after v2.14.0's tick change, so this folds into that round rather than adding one.

### v2.14.0 — Ideas From Qwt, Without Qwt

#### The X axis remembers where you zoomed from (US4.0, US4.1)

The idea is Qwt's `QwtPlotZoomer`, the last of the four taken from that review.

You could drill into an event but not step back out of it: the only way back was
Reset Span, which discards every level at once and returns the whole recording. An
operator working down through a loss-of-lock had to re-zoom from the top each time.

- **`X Axis > Zoom Back`** (and `Backspace`) returns to the view before the last
  zoom. It sits above Reset Span, which is the same action taken to its limit, so the
  two read as a pair from least to most drastic. Disabled until there is somewhere to
  go back to, so it never offers an action that would do nothing.
- **Only DISCRETE zooms are recorded** - a rubber-band drag or the Set Time Window
  dialog, which both arrive at `applyTimeWindow()`. Wheel zoom and drag-pan go
  through `handlePlotXRangeChanged()` and are deliberately not: they are continuous,
  and one history entry per mouse notch would bury the levels the user actually chose
  under a notch-by-notch trail until "back" stopped meaning anything.
- **Reset clears the history.** Once the full span is back on screen, stepping "back"
  into a window just discarded would be the opposite of what Reset means.
- Bounded at `PlotConstants::kMaxZoomDepth` (32), dropping the OLDEST when full: the
  levels nearest the current view are the ones about to be stepped through.

`zoomStackRemembersDiscreteZoomsOnly` covers the lot - two levels stepped back in
order, a continuous change in between that must not add one, stepping back from empty
as a no-op, Reset clearing it, and the depth cap.
`contextMenuOffersZoomBack` covers the entry's disabled-until-useful state and that
triggering it actually steps back. **Both verified to fail** against a build that
records continuous changes too, which is the mistake worth guarding.

#### X ticks land on round times (US4.0)

The idea is Qwt's `QwtDateScaleEngine`, from the same review.

The ticks divided the visible span into N equal parts, so their VALUES were whatever
the span happened to be over four - a 116-second view produced ticks 29 seconds
apart, and an axis read as `DDD:HH:MM:SS` had nothing on it a person would name.

- `TmChart::timeTickValues()` snaps the step to the next natural interval at or above
  the evenly divided one, from a ladder of 1/2/5/10/15/30 s, 1/2/5/10/15/30 min,
  1/2/3/6/12 h and a day, then starts at the first round time at or after the range.
  Ticks no longer sit on the range ends, which is the point: they sit on round times
  inside it.
- **Beyond a day per tick the step becomes a whole number of days.** Without that the
  ladder's top entry would be reused and a multi-week recording would grow ticks
  without bound.
- **Below a second it falls back to even division** - the format has no field finer
  than seconds, so there is nothing to snap to and pretending otherwise would give
  one tick for the whole view.
- Snapping applies only when a **time formatter** is set. That is what makes the axis
  a time axis; without one `TmChart` is a general chart whose X values are numbers,
  and snapping them to 15 s would be meaningless. The Y axes are untouched - lock %
  and dB are not times.
- Grid lines and labels both go through one `xTickValues()`, so they cannot disagree
  about where a tick is.

`timeTicksLandOnRoundTimes` covers all five cases - half-minutes, an offset range
where neither end is round, half-hours, whole days, and the sub-second fallback -
asserting the values rather than the rendered labels, because offscreen rendering has
no fonts to read a label back with and the property claimed is arithmetic. Verified to
fail against the even division it replaces.

`timeTickValues()` is public, unlike the evenly spaced `tickValues()`: it is pure,
stateless and worth asserting directly, and `TmChart` exposes observation points
rather than befriending its tests.
#### A decimated series shows its spread, not just a line through it (US3.0, US3.2)

The idea is Qwt's `QwtPlotIntervalCurve`, from the same review that rejected the
library itself.

The decimation has always computed each pixel column's minimum and maximum, then
drawn them as an opaque outline - which on dense noisy data reads as one solid blob.
The spread was on screen but not legible, and there was nothing to say where inside
it the signal actually sat.

- **The column's min-max range is now a translucent band, and the line runs through
  the column's MEAN** rather than whichever sample happened to land first. On a noisy
  receiver that spread is the measurement, and the mean is what makes it readable.
- **The band is outlined, not merely filled.** The polygon's top and bottom edges ARE
  the extremes, so stroking them is what keeps a one-sample excursion as sharp as it
  was before the band existed. Rendering the fill alone first - and looking at it -
  showed the dropout degraded from a solid line to a faint smudge, on the plot whose
  whole purpose is spotting dropouts. The outline uses a cosmetic pen, which draws
  crisply and skips Qt's stroker.
- Both come out of one column walk, so the extra geometry costs no extra pass. With
  the backing store above, it is also built once per view change rather than per paint.
- Sparse series are unchanged: below the decimation threshold a column holds a sample
  or two and there is no spread to show, so there is no band.

`denseSeriesShowsItsSpreadNotJustItsMean` alternates a dense series between two
extremes, so its mean sits mid-axis and only the band can put ink near the edges;
it asserts ink at both extremes and in the middle. Verified to fail without the band -
as, more tellingly, does the existing `denseSeriesStillDrawsASingleSampleDropout`,
which now depends on the band to reach the dropout at all.

#### A repaint that changed nothing costs a blit (US4.0)

The idea is Qwt's `QwtPlotCanvas::BackingStore`, taken from a review of that library
rather than from adopting it - see the note at the end.

v2.13.0 cached each series' *geometry*, which stopped the segments being rebuilt but
still re-rasterized them on every paint. So moving the crosshair across twelve
channels cost the full 34 ms draw, every mouse move, for a picture that had not
changed. The data layer - background, title, grid, series, axes - is now painted once
into a `QPixmap` and blitted; the crosshair and zoom band are painted on top of the
blit and are deliberately never part of it.

Release build, per paint:

| samples/series | series | first paint | repaint | crosshair move |
| --- | --- | --- | --- | --- |
| 360,000 | 3 | 35 ms | **0 ms** | **0 ms** |
| 360,000 | 12 | 127 ms | **1 ms** | **0 ms** |
| 1,000,000 | 3 | 81 ms | **0 ms** | **0 ms** |

Where the twelve-channel repaint has been across three changes: **102 s → 1,483 ms
→ 34 ms → under 1 ms.**

- `render()` splits into `renderData()` (everything cacheable) and
  `renderOverlays()` (cursor state). `renderTo()` is untouched and never uses the
  pixmap: an export renders at its own size and must not be a rescaled screen grab.
- **Every setter that changes the data layer now calls `updateData()`** rather than
  `update()`; only `setCrosshair()` and `setZoomBand()` call `update()` directly, and
  that difference is the whole optimisation. 18 call sites moved, 2 deliberately did
  not. `invalidateSeriesGeometry()` drops the pixmap too - anything that makes the
  segments stale makes the picture drawn from them stale.
- The pixmap carries the widget's device pixel ratio and is rebuilt when either the
  size or the ratio changes, so moving the window to a differently-scaled monitor
  re-renders rather than upscaling.

Two tests, each verified to fail against the defect it describes. Both go through
`grab()` rather than `renderTo()`, because `renderTo()` paints straight to its target
and never touches the pixmap - the existing rendering tests could not have caught
either of these:

- `cachedPaintStillFollowsTheData` - two identical paints must match, and a range
  change must not. A stale pixmap shows the previous view, which still looks like a
  plausible plot.
- `crosshairDrawsOverTheCachedPlot` - the crosshair must change the image and hiding
  it must restore it exactly. The opposite failure to a stale plot: an overlay that
  never appears because the blit is all that reaches the screen.

#### Why not Qwt itself

Reviewed at the same time, and **not adopted**. `QwtPlotCurve::drawLines` ends in
`QwtPainter::drawPolyline`, a wrapper over `QPainter::drawPolyline`, with the pen used
as-is and no cosmetic-pen handling - which is the call this project measured at
**1,725 ms** against `drawLines()`'s 27 ms for the same geometry and the same 1.5 px
pen. Adopting it would have cost roughly 60x the paint time it now has.

Worth recording from the review:

- **`QwtPlotCurve::FilterPointsAggressive` reduces each pixel column to "first, min,
  max, last"** - the same envelope v2.13.0 arrived at independently. Two
  implementations converging on it is reasonable evidence it is right. It is off by
  default in Qwt and limited to integer-coordinate devices.
- Qwt's licence would not have been the obstacle (LGPL with a static-linking
  exception, no attribution required). The scale was: ~200 classes, ~40 of them plot
  classes, against `TmChart`'s ~700 lines - and v2.9.0 deliberately deleted 44,700
  lines of QCustomPlot to be rid of exactly that.
- Still on the list as ideas rather than dependencies: a zoom stack with history
  (`QwtPlotZoomer`), an interval band drawn from the per-column min/max the decimation
  already computes (`QwtPlotIntervalCurve`), and tick steps snapped to natural time
  boundaries (`QwtDateScaleEngine`) instead of the current linear spacing in seconds.

### v2.13.1 — A Third of the Installer Was a Redistributable Nothing Ran

The application binary is functionally identical to v2.13.0; this release exists to
fix what was in the installer around it.

#### The installer no longer ships an unused Visual C++ redistributable (US8.0)

- **`vc_redist.x64.exe` (25.6 MB) was inside every installer, and nothing ever ran it.**
  `windeployqt` stages it into `bin\` unless told otherwise, and the `.iss` ships
  `bin\*` wholesale, so it travelled into the setup and then sat in the installed
  `{app}\bin` directory forever.
- The redist was never the mechanism anyway: `build_release.ps1` copies the CRT DLLs
  (`vcruntime140*`, `msvcp140*`) next to the exe, because that is what serves the
  **portable** layout too - a ZIP cannot run an installer. The script's own comment
  said so; the flag to stop staging it was simply never passed.
- `--no-compiler-runtime` on the windeployqt call. Installer payload **77 MB → 51.8 MB**,
  setup.exe **41.0 MB → 18.4 MB** - a bigger drop than the raw 25.6 MB suggests,
  because an already-compressed executable does not compress again inside the setup,
  so it was contributing close to its full size. The portable ZIP is unchanged, because its layout
  never included the redist.
- **Found by testing the installer rather than by reading it.** A dependency-closure
  walk over the payload - every import of every DLL resolved against the payload plus
  the known Windows set - turned it up as the one entry whose own dependencies pointed
  at an installer's world (`msi.dll`, `wintrust.dll`, `cabinet.dll`) rather than the
  application's.

#### Verified on an installed copy, not just in the staging directory

The same checks that found it were re-run against the rebuilt installer:

- Silent per-user install to a scratch directory, with the `.ch10` file association
  task deliberately off.
- **Launched with Qt absent from `PATH` and `QTDIR` cleared** - the condition a clean
  machine puts the app in. Every `Qt6*.dll`, the CRT, and `platforms\qwindows.dll`
  loaded from the install directory rather than the system. That plugin is the classic
  omission: without it the app dies at startup with no window and no useful error.
- Uninstall removes the program files, the Start Menu entry and the uninstall registry
  entry; `settings\` is deliberately left behind (`uninsneveruninstall`, US8.0).

**What this still does not prove:** first-run behaviour on a machine that has never
held this application's settings or registry state - in particular the `new_x.toml`
merge path when a shipped default gains fields a user's file lacks. That needs a
genuinely clean Windows target; Windows 11 Home offers neither Sandbox nor Hyper-V.

### v2.13.0 — The Plot Stops Choking, and Both Y Axes Take Limits

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
  **X Axis** (Set Time Window… / Zoom Back / Reset Span), **Y Axes** (Set Left Max… / Set
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
   - The Qt face of multi-stream processing; the pipeline itself is `PipelineRunner` (below), which it wraps and reports as signals
   - `startProcessing(QVector<StreamJob>)` hands the jobs to the runner, which takes ownership of each job's FrameSetup and spins up one `Ch10PacketReader` thread plus one `FrameProcessor` worker thread (and a `PacketQueue`) per stream
   - `cancelProcessing()` asks the runner for a cooperative abort of all workers and the reader; `reset()` clears transient state
   - Keeps the opening "--- Processing N stream(s) ---" log line and the final progress of 100 for itself (the reader also reports 100, so the bar reads it twice)
   - Emits `progressChanged(int)`, `processingStateChanged(bool)`, `streamProcessed(ProcessedStreamData)`, `processingFinished(bool)`, `logMessageReceived(QString)`, `errorOccurred(QString)`

3. **BatchController** (`src/viewmodel/batchcontroller.cpp`, `include/viewmodel/batchcontroller.h`)
   - *Processing templates / batch apply (US1.1)*; owns the sequential Apply Template run: the file queue, which file is in flight, the per-file config application, the appearance reuse, and the export post-pass
   - `validate()` checks each file's channel set against the template (the slow part of starting a batch, since it reads every file's channels) through `matchFile()`, which reads the ids out of the template and the reader's list for the Qt-free `TemplateMatcher`; `buildTemplate()` captures a processed source as a template; `applyAppearance()` reapplies a template's saved names/colors. All three are static and free of dialog code, which is what makes them testable
   - Reports through `message(LogLevel, QString)` and `finished()` rather than calling the View, and renders per-file images through an injected `ImageExporter` callback — the widget draws, the controller decides when and where
   - Lived in `MainView` until v2.12.1; the View kept a state machine, which is the layering leak this removes

4. **PlotViewModel** (`src/viewmodel/plotviewmodel.cpp`, `include/viewmodel/plotviewmodel.h`)
   - Converts each `ProcessedStreamData` into in-memory `PlotSeriesData` vectors (name, receiver/channel indices, x/y values, cached Y min/max, color), carrying its `sourceId` through so cross-source identity checks (reprocess-replace, rename/recolor sibling-sync) never cross a source boundary
   - (The logic - the series a stream yields, the shared base and its re-basing, the overlap rule, the import's stream ordering - is the core's `seriesbuilder.h`; this class keeps the storage and the presentation.) Converts absolute IRIG seconds to elapsed seconds against a shared base that tracks the **earliest absolute sample across every source** (not just the first-added one): `addStreamData()` re-bases — shifting every existing series right — when a later-added source started earlier; `removeSource()` is the mirror, shifting left only if the removed source held the earliest sample. Emits `nonOverlappingSourceWarning(sourceId)` (informational) the first time a newly added source's absolute range doesn't intersect what's already loaded
   - Assigns the purple/blue/green (lock) and red/orange/yellow (SNR) palette
   - Manages axis ranges (auto Y with margin, manual Y override, X time window), the left-axis view toggle (lock % vs accumulated missed frames), and per-series visibility
   - Signals `dataChanged()`, `axisRangeChanged()`, `seriesVisibilityChanged()`, `nonOverlappingSourceWarning(int)`; `computeYRange()` uses per-series cached min/max
   - `hasLeftYMaxOverride()`/`leftYMaxOverrideValue()` and the right-axis counterparts expose whether/what the user overrode the axis maxima

#### Model

1. **Chapter10Reader** (`src/model/chapter10reader.cpp`, `include/model/chapter10reader.h`)
   - Reads Ch10 file **metadata** up front: scans TMATS to catalog time and PCM channels, provides channel lists, time accessors, and channel ID resolution. Wraps the irig106utils C library.
   - Plain C++, no Qt: the path is a `std::filesystem::path`, text is UTF-8 `std::string`, channels are `ChannelData` values in a `std::map` by id, and a failed load leaves its reason in `lastError()` (loading is synchronous, so there is no signal). The view model converts the lists at its boundary (`qtbridge.h`)

2. **Ch10PacketReader** (`src/model/ch10packetreader.cpp`, `include/model/ch10packetreader.h`)
   - The single-pass reader. `prepare()` opens the file, parses TMATS, resolves each stream's PCM attributes, and builds the channel-ID → `PacketQueue` routing; `run()` (on its own thread) reads the file once, tracks IRIG time, and dispatches each PCM packet's payload to the matching queues, then posts end-of-stream sentinels
   - Plain C++, no Qt: it reports progress, log lines and errors to a `ProcessingEvents` sink (`include/model/processingevents.h`) on the reading thread, relayed to the UI thread by `qtprocessingevents.h`
   - Owns the `SuChanInfo` per-channel bookkeeping table

3. **PacketQueue** (`include/model/packetqueue.h`) — bounded, thread-safe per-stream packet queue connecting the reader to one worker (header-only)

4. **FrameProcessor** (`src/model/frameprocessor.cpp`, `include/model/frameprocessor.h`)
   - Per-stream worker: drains its PacketQueue, runs the bit-serial frame-sync scanner (acquire/lock, off-phase rejection, bit-span lock %), accumulates lock %, missed frames, and (SNR mode) calibrated channel values into a `ProcessedStreamData`
   - Plain C++, no Qt: it reports log lines, errors and completion to a `ProcessingEvents` sink (`include/model/processingevents.h`) on the processing thread; `qtprocessingevents.h` forwards them to a `QObject`'s thread as queued calls
   - Private helpers include `derandomizeBitstream()` and `hasSyncPattern()`; applies linear slope/offset or a non-linear `CalibrationProfile` per channel

5. **FrameSetup** (`src/model/framesetup.cpp`, `include/model/framesetup.h`) — frame configuration / word-map + calibration table built per job. Plain C++ (no `QObject`, no Qt in the header); calibration profiles are attached from a `std::unordered_map`

6. **ChannelData** (`src/model/channeldata.cpp`, `include/model/channeldata.h`) — channel metadata value object

7. **StepDetector** (`src/model/stepdetector.cpp`, `include/model/stepdetector.h`) — *US5.3*; pure (UI-free) logic that parses the `[[Step]]` step-config TOML and detects step plateaus in a raw-count series via derivative/edge detection, building a per-channel `CalibrationProfile`

8. **CalibrationExtractor** (`src/model/calibrationextractor.cpp`, `include/model/calibrationextractor.h`) — *US5.3*; drives a raw extraction over a calibration Ch10 file (reusing the Ch10PacketReader + FrameProcessor pipeline with unit slope / zero offset) and runs StepDetector per channel to build session-only profiles. Plain C++ (no Qt in header or source): one driver thread runs the reader and worker, waits for both, detects and reports through a `ProcessingEvents` sink; its `Request` carries an `Acquisition` built through `framesync.h`, the same description the main run uses

9. **TomlDocument** (`src/model/tomldocument.cpp`, `include/model/tomldocument.h`) — reads and writes the TOML subset every settings file uses, as plain file I/O. Replaced `TomlConfigHelper`, which implemented the same subset as a custom `QSettings::Format`: QSettings sorts its sections (so `FrameSetup` had to re-parse each file to recover word-map order), merges on write (so callers had to delete the file first), and needs process-wide `registerFormat()` before anything can be read. It is the format only: the frame-sync pattern file is `FrameSyncFile` (step 1.4), and the receiver-parameters file (scalars + word map) is read and written by `FrameSetup` via `writeWordMap()`. It also reads **arrays of tables** (`[[Name]]`; `arrayTables()` / `tables(name)`, read only), and `StepDetector::parseStepConfig` is a thin reader over that - it used to be a second hand-written TOML parser

10. **CsvSeriesParser** and **CsvSeriesWriter** (`src/model/csvseriesparser.cpp`, `src/model/csvserieswriter.cpp`, with `clocktime.cpp`; headers in `include/model/`) - the two directions of the application's CSV format, matched inverses. **Part of the Qt-free core** (UTF-8 `std::string`, `std::filesystem::path`, `std::vector`, listed in `TestCoreQtFree`). `CsvSeriesParser::parse(path)` returns `CsvSeries` / `CsvParseResult`: UTF-8 or UTF-16 with a byte-order mark, carriage returns ignored, a single `Time (DOY:HH:MM:SS.mmm)` column then one column per series; thread-safe, so `PlotViewModel::loadCsvFileAsync` runs it (and the conversion to `PlotSeriesData`) on a worker thread. `CsvSeriesWriter::write(path, columns, timeBase)` writes what `PlotViewModel::exportCsv` used to: CR LF, UTF-8 without a BOM, six decimals, one row per distinct millisecond (rounded half away from zero, the last sample in a millisecond winning), empty cells where a column has no sample; its `CsvColumn`s view the plot's buffers through `std::span`. `clocktime::format` is the `DDD:HH:MM:SS[.mmm]` stamp both the writer and `PlotViewModel::formatTime` use. Pinned by `TestCsvSeries`, which keeps the Qt implementations they replaced as oracles
11. **SeriesColumnSchema** (`src/model/seriescolumnschema.cpp`, `include/model/seriescolumnschema.h`) — single source of truth for the CSV column-header format: builds each series' column header and the SNR series name, and parses a header back to its identity fields (metric type, name, stream label/order, receiver, source id). Shared by `CsvSeriesWriter`, `PlotViewModel::addStreamData` and `CsvSeriesParser` so export and import stay inverses (pinned by `tst_seriescolumnschema`'s round-trip). Multi-file input: a series from source 0 exports with the unqualified, byte-identical-since-v2.6 header format; source 1+ gets a leading `"S<n>| "` qualifier that `parseColumnHeader()` strips back off before recovering identity. **Part of the Qt-free core** (UTF-8 `std::string`, listed in `TestCoreQtFree`): `columnHeader()` takes the metric, name and source id rather than a `PlotSeriesData`, and the callers that hold `QString`s convert at the call. `MetricType` lives in `include/dto/metrictype.h` so the schema can name it; `PlotSeriesData::MetricType` is an alias of it, so every existing spelling still compiles

12. **StreamConfigSchema** (`src/model/streamconfigschema.cpp`, `include/model/streamconfigschema.h`) — single source of truth for `StreamConfig`'s JSON representation (`toJson()`/`fromJson()`), so no field list is hand-mapped twice. Excludes `calibrationByWord` (extracted profiles are session-only, never serialized) but includes the calibration *input references* (`calCh10Path`, `stepTomlPath`, `clipStartSec`, `clipEndSec`). Used by `ProcessingTemplateSchema`
13. **ProcessingTemplateSchema** (`src/model/processingtemplateschema.cpp`, `include/model/processingtemplateschema.h`) — *processing templates / batch apply*; serializes/parses a `ProcessingTemplate` (`schemaVersion`, `appVersion`, `name`, `timeChannelIndex`, and a list of `TemplateStreamEntry`). Delegates each entry's config to `StreamConfigSchema` and adds the per-entry `SeriesAppearance` array; no file paths (a template is location-independent); a `schemaVersion` newer than `kCurrentSchemaVersion` is rejected
14. **TemplateMatcher** (`src/model/templatematcher.cpp`, `include/model/templatematcher.h`) - *processing templates / batch apply*; pure, disk-free validator that a target file's PCM channel-ID set matches a template's **exactly** (`match(templateIds, fileIds)` -> `MatchResult{ok, missing, extra}`, sorted, each id once). **Part of the Qt-free core** (`std::vector<int>` in and out, listed in `TestCoreQtFree`); `BatchController::matchFile` reads the ids out of a `ProcessingTemplate` and the reader's channel list and calls it. Exact-set matching is what lets the batch loop feed the template's stored `pcmChannelId`s straight to processing
15. **StreamJobBuilder** (`src/model/streamjobbuilder.cpp`, `include/model/streamjobbuilder.h`) - turns one stream's settings into a processing job: `buildStreamJob(StreamJobConfig, ProcessingParams base, StreamJobContext, StreamJob&, StreamJobReport&)` validates the sync pattern, mask and frame length, then (Receiver SNR) builds the frame-parameter table from the named receiver-parameters file, else the shipped `settings/receiver_params/default.toml`, else a default word map, applies the linear calibration and attaches any step-calibration profiles, returning the job or the reason for refusing it plus the log lines. **Part of the Qt-free core** (UTF-8 text, `std::filesystem::path`, `std::unordered_map`; listed in `TestCoreQtFree`); `MainViewModel::buildStreamJob` copies a `StreamConfig` into a `StreamJobConfig`, supplies the application root and the file-scoped byte order, and relays the report as `logMessageReceived`. Pinned by `TestStreamJob`
16. **SeriesBuilder** (`src/model/seriesbuilder.cpp`, `include/model/seriesbuilder.h`) - the data logic behind the plot's series: `buildStreamSeries` turns a `ProcessedStreamData` into the series it yields (lock %, missed frames, one per receiver channel, with names, receiver numbers, channel indices, cached ranges and the elapsed axis; values are views, not copies); `SeriesTimeBase` is the clock the shared elapsed axis hangs from (earliest absolute sample across every source, its day and time of day, `adopt` / `advance` for re-basing, `overlaps` for the non-overlap warning); `assignImportedStreamOrders` orders an imported CSV's streams; `StreamKey` is the "same stream run again" identity. **Part of the Qt-free core** (listed in `TestCoreQtFree`); `PlotViewModel` keeps the sample storage (`QVector`s the chart shares) and the presentation, and calls it. Pinned by `TestSeriesBuild`
17. **IRIG 106 Library** (`lib/irig106/`) — third-party C library for the Chapter 10 file format (see Protected Files)

### Data Flow

```text
User opens .ch10 ─► MainView ─► MainViewModel ─► Chapter10Reader (metadata)
     (or Add Source)                  │           [assigns/reuses sourceId]
                            StreamConfigDialog (per-stream config)
                                      │
                      MainViewModel builds QVector<StreamJob>
                              [ProcessingParams.sourceId stamped]
                                      │
                ProcessingCoordinator (Qt) ─► PipelineRunner (no Qt)
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

- **Target**: Qt 6.12.0
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
- Sets C++20 standard
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
- One `FrameProcessor` per stream runs on its own `std::thread` (no event loop); `PipelineRunner` owns the lifecycle - the queues, the reader and the threads - and joins them all before it reports the end
- Reports log, error, and completion through `ProcessingEvents`; the coordinator's adapter relays them as its own signals, on the UI thread and in order

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
3. If it must round-trip to a TOML file, add it to the matching `loadFrameSyncFromToml` / `saveFrameSyncToToml` helper in `streamsubdialogs.cpp`, or to `FrameSetup`'s receiver-parameters read/write, which both go through `TomlDocument` (respecting the US5.1/US1.0 boundaries)
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
listed here: the fast, no-fixture suites run first, followed by the heavy
`.ch10`-integration suite (`TestProcessingCoordinator`), which `--fast`/`TMDQ_FAST_TESTS`
skip for quick local iteration. The source/header files are listed in `tests/tests.pro`.

- **ChannelData** (doctest, `native/tmdq_core_tests/test_chapter10reader.cpp`; no Qt; ported from the Qt `TestChannelData`, plan step 2.4, whose Qt version is deleted) — ChannelData model object tests, including a non-ASCII name round-tripping and copies keeping every field Also copy assignment.
- **Chapter10Reader** (doctest, `native/tmdq_core_tests/test_chapter10reader.cpp`; no Qt; ported from the Qt `TestChapter10Reader`, plan step 2.4, whose Qt version is deleted) — Chapter 10 metadata reader, on the five committed recordings: exact channel ids, names and TMATS data rates for each; the combo-box index to channel mapping (placeholder at 0, past the end); each way a file fails to load (missing, empty, text, short header, first packet not TMATS, only TMATS, cut inside TMATS) with its exact message and no channels left; a second load replacing the first and a failed open leaving the old channels alone; and a copy of `step_cal` with same-length edits to its TMATS text (no data checksum, header untouched) that reaches what the fixtures do not - the channel-enabled flag (T / Y / 1 either case enable, F / N / 0 / other disable, missing enables, whitespace ignored), the type codes (`PCMIN` / `01` / `09`, `TIMEIN` / `11`, case-sensitive), the name priority (data source id, channel data link name, PCM link name), a non-ASCII name, and the bit rate read like `QString::toDouble`. Written against the `QObject` reader first; validated against 23 mutants Also a second time channel (the PCM channel retyped as 11), listed in id order with the FIRST as the default. Validated against 44 mutants with ChannelData (42 caught, 2 equivalent).
- **TestConstants** (`tst_constants`) — `plotconstants.h` (Qt: `QColor`, `QString`), pinned by value in one case. `constants.h` is Qt-free and pinned by the native suite below.
- **Constants** (doctest, `native/tmdq_core_tests/test_constants.cpp`; no Qt; the core half of the Qt `TestConstants`, plan step 2.4) — every constant in `constants.h` that something outside the code depends on: the version (built from its three numbers), the PCM frame and file constants, the UI defaults and settings keys, and the calibration tuning that the detector's own suites cannot tell from a neighbouring value. Where a shipped file carries the same value, the case reads the file: the Receiver SNR fallback constants against `settings/framesync_patterns/framesync_rcvr_default.toml`, the Frame Sync Lock default against `framesync_PRN15.toml`, each settings folder's `default.toml`, and the full manual's name against the installer script.
- **Ch10PacketReader** (doctest, `native/tmdq_core_tests/test_ch10packetreader.cpp`; no Qt; ported from the Qt `TestCh10PacketReader`, plan step 2.4, whose Qt version is deleted; the first native suite to read recordings, compiling the vendored irig106 as the DLL does) — the single-pass reader, tested directly on the committed `step_cal` recording (it had no tests of its own): each way `prepare()` fails has its own exact message; a full run reports its start, no error, progress that never goes backwards and ends at 100, all 1598 packets (6,539,016 payload bytes) and exactly one end marker; an abort is announced, reports 100 and still posts the end marker; a file cut inside a PCM packet, inside a time packet, or with a damaged header checksum reports the right read error and delivers exactly the packets before the damage; a file that ends inside a packet header is a clean end of file and a damaged sync pattern silently costs one packet (both pinned as the behaviour they are); and two streams on one channel each get every packet sharing one payload buffer. Cut points are found by walking the packet headers. Written against the signal-based reader first; validated against eleven mutants Setup is serialised across threads (the library's own state is not thread-safe): while one thread holds `lockIrig106Setup()` a setup on another cannot even open the file (a copy held open with no sharing makes an early open fail visibly), a failed setup releases the lock, and eight threads setting up 40 times each all succeed and agree Also: every attribute `prepare()` resolves for a worker (mask given and defaulted, bits and words separately, byte order, the bit period from TMATS or an operator rate), each packet's bit count and strictly increasing base time, a queue shared by two streams (every packet twice, one end marker), a file cut inside its first header and one whose first packet is not TMATS, and routing on the multi-channel PRN sample (exactly channel 13's packets). Validated against 54 mutants (50 caught; 4 need recordings the repository does not have). The masked stream's word mask is every bit of a word, `0xFFFF` (it was cut by the sync mask before the fix).
- **FrameProcessor** (doctest, `native/tmdq_core_tests/test_frameprocessor.cpp`; no Qt; ported from the Qt `TestFrameProcessor`, plan step 2.4, whose Qt version is deleted; the test executable carries the UTF-8 code-page manifest for its non-ASCII path case) — constructor defaults, abort flag, `derandomizeBitstream` (identity/short and changed/long), invalid time-channel/PCM-channel/file handling, and processing real Ch10 data (receiver-data accumulation, lock-only mode has no channels, monotonic frame-sync errors, slope affects values, shorter period → more samples, calibration round-trip clean steps, off-phase sync after lock-loss not extracted); and `reportsExactlyTheseMessages` (10 rows), the exact text and order of everything it reports - success, a non-ASCII stream label, a bounded window, a masked pattern, sync-but-no-frames, cancel, both internal errors, and the all-odd and mixed byte-swap warnings - written against the signals implementation first; and `aSyncMaskDoesNotMaskReceiverWords`, a receiver stream processed with and without a sync mask that ignores sync bits 8-15 giving identical values (the word mask is every bit of a word, not cut by the sync mask). Also, on synthetic streams timed at one second per bit with every value worked by hand: windows on both clocks (a bit on a window's end in it, each window averaging its own frames, the completion count), a bounded window (both ends in, nothing outside counted, windows starting at the start), lost lock (missed frames counted bit by bit, never also extrapolated; a window with no frame at 0), one missed frame per frame length of unsynced bits inside the window only, gap extrapolation at the bit period's rate or the operator's, the minimum syncs, only enabled words as channels with `(raw + offset) x slope`, the descrambler against an independent IRIG 106 randomizer (in pieces, resynchronizing, across packets), inversion and a packet with no payload; three more message rows. Validated against 129 mutants (127 caught; an abort check whose absence only delays a mid-packet abort, and the unpinned text of a start-only bound)
- **TestMainViewModelHelpers** (`tst_mainviewmodel_helpers`) — `MainViewModel::classifyLogMessage()`, the severity policy the View renders but does not decide. The channel-naming cases that used to live here moved to **TestFrameSetup** with the `MainViewModel` forwarders they called: `FrameSetup` is where that naming lives; and `buildStreamJobAttachesCalibrationByWord`, which runs a receiver stream through the private `buildStreamJob` (the test is a friend) and checks which parameters received which profile and the log line that says so; and `buildStreamJob` as a whole: 43 rows of how the sync pattern and mask are decoded (whitespace trimmed, `+` and `0x` accepted, any `-` refused, 64-bit overflow refused, the pattern's length counted in characters), every field a config contributes to the worker's parameters, and each rejection message in the order the checks run
- **TestStreamJob** (`tst_streamjob`) - what `MainViewModel::buildStreamJob` does around the core's job builder: 15 cases, each one line of the outcome (built or not, the message, the log, every worker parameter, the whole frame-parameter table with its calibration), one for each thing the wrapper does (it carries the user's Qt settings across - label, sync fields, file names, profiles - reads the application root and byte order it holds, and reports what the builder found as a QString and as signals). The builder itself (`buildStreamJob` in `streamjobbuilder.h`) is the core's and is tested without Qt in `native/tmdq_core_tests/test_streamjob.cpp`: all 59 cases (the slope indices and their arithmetic, polarity, the scale at its edges, an explicit receiver file that is missing, a folder, outside the frame, empty, metadata-only or not TOML, the word-count rounding, the default file and the default word map in each state, profiles attached and not, the order of the checks, non-ASCII labels and names) plus two for the default file's word at the frame's edge; the old view-model code restated over std types (with one marked line, the fix for an appended default map) and compared over 1,500 generated configurations; and two direct tests (a UTF-8 application root with non-ASCII folder names; error and log as plain text). 102 mutants of `streamjobbuilder.cpp`: 101 caught, 1 equivalent
- **FrameSetup** (doctest, `native/tmdq_core_tests/test_framesetup.cpp`; no Qt; ported from the Qt `TestFrameSetup`, plan step 2.4, whose Qt version is deleted; reads `tests/fixtures` and `settings/receiver_params` through `repoFile()`) — Frame parameter loading, word map, calibration, `channelPrefix()` naming the known channel positions then falling back to `CH<n+1>`, `receiverParameterName()` combining the two, `receiverIndexFromName()` inverting it (0 for a name with no parseable `_RCVR<N>`), and the receiver-parameters file as a whole: the scalars read from the `[Receivers]` block the shipped files actually use (and the older `[Parameters]` keys still accepted), a save/load round trip that carries the word map with them, the default-map fallback when no file was loaded, nothing written when there is no map to write, and the metadata-only file that parses to zero parameters — the contract `tryLoadingFile()`'s two callers depend on; and `receiverNameParsingMatchesQStringToInt`, which pins how a hand-typed parameter name parses (the last `_RCVR` token wins, whitespace and a leading `+` tolerated, a second sign / negative / trailing junk / overflow reads as no receiver, a name starting with the token has no channel part) - written against the `QString` implementation first; and `attachCalibrationProfiles`, which attaches a step-calibration profile only when it is valid and its word matches a parameter (an invalid one stays linear; a word the map lacks is ignored), written against the `QObject`/`QHash` version first Also: the linear calibration worked by hand for every voltage range and polarity, the reserved sections, file order, a reload, the default map's numbering and bounds (each count must be positive, not only their product), `readReceiverParams` precedence and guards, the exact file `writeReceiverParams` writes, a failed source map replaced by the default one, and the write-failure message. Validated against 90 mutants (87 caught, 3 equivalent).
- **TestPlotViewModel** (`tst_plotviewmodel`) — default state, CSV load/export (incl. header-only, malformed rows, async load signals), time conversion/formatting, color assignment, the computed Y range and the SNR max override that pins it (the minimum stays automatic, Reset returns it to the data), X time window, visibility, clear/title, in-memory `addStreamData` (lock/SNR/error series, multi-stream accumulation), the left-axis view toggle preserving per-stream selection, and stream-identity regression coverage: two streams sharing a TMATS-derived `streamLabel` but different `streamOrder` must stay independent through reprocess-replace and `renameSeries()`/`recolorSeries()` sibling-sync (pinned after a pre-v2.6.0 cross-contamination bug). Multi-file input: cross-source identity (two different `sourceId`s reusing the same `streamLabel`/`streamOrder` stay independent through reprocess-replace/rename/recolor), time-base re-basing (a later-added source starting earlier shifts existing series right; three successively-earlier sources compound correctly; a later source starting after triggers no shift), the non-overlap warning signal (fires once per newly-arriving non-overlapping source, not for an overlapping range), and `removeSource()` (drops only the target source, re-bases left only when the removed source held the earliest sample, clears all data when the last source is removed, no-ops for an unknown id). US1.1 source view: `setVisibleSource()` isolates a source via `effectiveVisible()` without mutating per-series `visible`, `sourceList()` lists distinct labeled sources, and `exportCsv(path, sourceId)` writes only that source's columns; and `aNonAsciiStreamLabelReachesEverySeries`, which pins that a non-ASCII stream label (arriving as UTF-8 from the core) names the lock and missed-frames series, prefixes the SNR series' name, and is what a re-run matches on to replace the stream
- **TestSeriesBuild** (`tst_seriesbuild`) - what the `PlotViewModel` does with processed streams and imported files, as one line of its data layer per scenario (time base, span, flags, warnings, every series' identity, indices, cached range and samples): 16 scenarios, one for each thing the plot does (it drops a stream's earlier series, judges overlap and raises the warning once per source, moves the base for an earlier source, orders its legend by job, removes a source and moves the base back, clears, keeps counting ids, takes an import in and replaces what was there). The functions behind it (`buildStreamSeries`, `SeriesTimeBase`, `assignImportedStreamOrders`) are the core's and are tested without Qt in `native/tmdq_core_tests/test_seriesbuild.cpp`: all 77 scenarios (what a stream becomes, the receiver number in every spelling, channel indices, mismatched arrays, NaN and infinity, legend order, running a stream again, re-basing across sources, the overlap rule at every boundary, the base day and time of day across the epoch, a year, a leap day and the far future, removing sources, ids across removals and clears, and the order given to an imported CSV's streams) run on `Plot`, a small host written over std types that does what the plot does with those functions; the old data logic restated over std types and compared with `Plot` over 700 random add/remove/clear sequences (whole state after every step) and the old import ordering over 4,000 generated lists; direct tests of views-not-copies and of `SeriesTimeBase`. 113 mutants of `seriesbuilder.cpp`: 108 caught, 5 equivalent
- **TestProcessingCoordinator** (`tst_processingcoordinator`) — constructor defaults, `reset()` clears state, cancel-with-no-run no-op, `cancelProcessingEndsTheRunWithoutAResult` (Cancel ends the run unsuccessfully, delivers no result, and the state goes true then false) and `destroyingTheCoordinatorMidRunIsSafe` (nothing queued for the dead object is delivered), `startProcessing()` empty-returns-false and processing-state emission, plus a single-vs-multi-stream throughput diagnostic that reports and asserts nothing (the throughput GUARD is the native `PipelineRunner throughput` case); and `workerReportsPrecedeTheResultAndFinishComesLast`, which checks through a real run that the worker's completion line is relayed before the stream's result and `processingFinished` is last; its end-to-end test also checks the progress bar (0 at the start, then 100 reported twice: once by the reader, once on finish); and `aRecordingUnderANonAsciiFolderProcessesEndToEnd`, which runs the committed `step_cal` recording copied under a non-ASCII folder through the whole pipeline
- **TestMainView** (`tst_mainview`) — Main window construction, widget wiring, log routing, dock visibility behavior, and the CSV import routing (`openPath`/`importCsv`, valid/invalid). The batch-apply cases moved to **TestBatchController** with the state machine itself; and `aFinishedStreamIsLoggedUnderItsLabel`, the log line quoting a finished stream's (non-ASCII) label
- **TestPlotWidget** (`tst_plotwidget`) — Plot widget construction, null/valid ViewModel connection, dark/light theme application, the movable legend overlay populating from data (hidden until data loads, then one row per visible active-metric series), a shown/resized-window regression case asserting the overlay sizes correctly (not a collapsed frame-only box) after a second rebuild adds more rows — a QScrollArea `widgetResizable` sizeHint staleness bug reproduced and fixed post-review — the SNR legend row showing the short `"CH<id> <ch.name>"` form instead of the full TMATS stream title, the legend row layout reserving a right-side gutter matching the style's scrollbar extent, and each legend row carrying an objectName the overlay stylesheet can target to override the app's global `QWidget { background-color: ... }` theme rule (otherwise every row painted as an opaque chip); plus `exportImage()` writing a PNG/SVG/PDF headlessly (the parameterized image-export entry point extracted for batch apply); and the right-click context menu that replaced the external control rows: it lists every expected top-level item, its data-dependent entries are disabled until data loads, the Plot File submenu is disabled for a single source and switches `visibleSource` for a multi-source run, View Mode reflects and sets the active left-axis metric, `X Axis > Reset Span` / `Y Axes > Reset` clear the X window and both Y overrides, plus two structural guards - wheel-zoom/drag-pan stay enabled (horizontal-only) once data arrives, and **no QComboBox / QSpinBox / QLineEdit / QAbstractButton lives outside the chart** - on-chart overlays are allowed, external control rows are not (the machine-checkable form of "no external controls"); plus the legend toggle (shows/hides the overlay and survives a rebuild, appears only once data is loaded, is parented to the chart, and stays in sync with the context menu's checkable Show Legend item); plus Y axis occupancy - a lock-only plot hides the right axis, an SNR-only plot hides the left, an empty chart keeps the left (hiding both would leave a bare box), and hiding the last *visible* SNR series reclaims the right axis just as never loading one would
- **TestTmChart** (`tst_tmchart`) - the first-party chart replacing QCustomPlot: series bookkeeping across removals and out-of-range access, mismatched x/y lengths truncated (a draw-time overrun), degenerate/non-finite ranges rejected, X and Y coordinate transforms round-tripping with the correct screen orientation, wheel zoom anchoring the value under the cursor and reporting the new range, interactions inert until enabled, and the shared `renderTo()` export path painting real content while omitting the crosshair/zoom-band overlays (cursor state, not data), and that hiding a Y axis reclaims its margin (the measurable consequence) while leaving the coordinate transforms consistent with the new plot area
- **TestPlotCustomizationDialog** (`tst_plotcustomizationdialog`) — Customize Plot Series dialog: one Frame Sync Lock checkbox per stream, Select All/None, apply → per-stream lock/missed visibility round-trip to the ViewModel; Receiver SNR tree build (receiver grouping), tri-state group cascade, Select All/None, apply → per-channel SNR visibility round-trip, and the Expand/Collapse All button toggle; plus per-stream rename/recolor (lock tab) and per-channel rename/recolor via pending item roles (SNR tab) applied to the ViewModel on OK, and the single batched `seriesAppearanceChanged` emission (reaches private widgets/slots via a friend declaration, same pattern as TestFrameProcessor)
- **TestStreamConfigDialog** (`tst_streamconfigdialog`) — Per-stream Configure Streams dialog: stream rows, mode selection, gear setup dialogs, TOML load/save round-trips, "Apply to all" fan-out, the Channel column label (short names shown in full, long TMATS-derived names elided on the right with "..." , left-justified, styled via `channelNameCell` to mimic the Mode combo box's border/fill, and the full name always available via tooltip — which is now the only place a truncated name is recoverable), the Mode combo's left-justified closed-box text (via an editable-but-readonly internal line edit) while selection still tracks correctly, and the table header/separator using theme-QSS object names (`streamHeaderLabel` / `streamHeaderSeparator`) rather than hard-coded inline colors; the Extract Calibration dialog: an unreadable sync or an unopenable recording shown on the status line without extracting, a real `step_cal` extraction through the progress dialog (twelve profiles), byte order / inversion / derandomization each reaching the extraction (the wrong value must visibly fail), and the data rate reaching it (half the rate shows exactly twice the recording length)
- **TestFrameSyncFile** (`tst_framesyncfile`) - what the dialogs do with a frame-sync pattern file (US1.0 / US2.0 / US5.0), through the dialog's own load/save functions. The file format itself (what a file says, what writing puts in it) is the core's and is tested without Qt in `native/tmdq_core_tests/test_framesyncfile.cpp` (all 57 load rows and 15 save rows as the core's values, a round trip for every flag combination, 300 generated files and saves against the code the core replaced; 65 of 66 mutants caught, 1 equivalent). This suite keeps the dialog-level behaviour: 23 load rows (each run with Derandomize ticked and unticked) covering upper-casing, the spin box's range, a field left alone, Invert Data cleared when absent and Derandomize left alone when absent; 6 save rows (the widgets' values reach the file unchanged); 16 Receiver SNR defaults rows (`loadReceiverFrameDefaults`); the old dialog code kept as an oracle and compared over 300 generated files and 300 generated saves, each branch asserted reached; save-then-load round trip through the dialog
- **TestExportDialog** (`tst_exportdialog`) — Export dialog checkbox-to-field enable logic, export-button validation, and the log-export row defaults/accessors and log-only validation
- **StepDetector** (doctest, `native/tmdq_core_tests/test_stepdetector.cpp`; no Qt; ported from the Qt `TestStepDetector`, plan step 2.4, whose Qt version is deleted) — Non-linear calibration (US5.3): `[[Step]]` TOML parsing (valid / empty-fails), plateau detection (clean, too-few-fails, extra-plateaus uses last of monotonic run, short-blip doesn't steal a pairing slot, long leading transient doesn't shift pairing, inverted-polarity sweep not reversed, non-monotonic pairing rejected, noisy, settling-at-plateau-start excluded, round-trip exact), the calibration-import resilience cases (the outcome code names the gate that rejected, a compressed out-of-tolerance receiver still calibrates via the relaxed threshold while a healthy one does not relax, noise-split plateaus coalesce instead of severing the sweep, a flat channel reports "flat / no signal" rather than a plateau shortfall, samples pinned at the floor or rail kept out of the noise estimate so live dwells neither split nor shatter, a split dwell far above the noise still paired as one step, railed top steps calibrating the range that stayed in scale while the rail clamps rather than extrapolates, an unconfirmed dwell measured from the dwell grid rather than bridged or shifted, the number of levels the sweep held reported so a short step file can be flagged, and which step file the Extract Calibration dialog preloads), and `interpolateCalibration()` (midpoint, below/above clamping, coincident-raw guard); and the step-file parser pinned directly, written against the `QString` implementation first: case-insensitive `[[Step]]` header and `db` key, a header with a trailing comment or inner spaces not counting as a step, keys outside a step or in another section ignored, a step with no parseable `db` kept at 0, `QString::toDouble`'s value rules (quotes are NOT stripped), file order with the last `db` winning, the output cleared on every call, UTF-8 BOM / UTF-16 files, a non-ASCII path, both error messages, and `initialStepConfigPath` ignoring a directory The step-file parser (`parseStepConfig`, now a reader over `TomlDocument`'s arrays of tables) is also run against a verbatim copy of the hand-written parser it replaced, kept in the test as an oracle, over 4,000 generated files - headers with comments, spaces, other names and missing brackets; keys in every case, quoted, commented, empty, repeated and outside any step - and must agree on success, on every step's dB to the bit, and on the message Also, cases for the inner rules the field scenarios pass through without isolating: the edge threshold's exact value (six robust sigmas of the live derivative, floor and ceiling runs excluded), the confirmation window's boundary, the 2-count floor and no relaxing below it, a sweep starting where a reversal ended, a split up to a tenth of a step, an inverted receiver's split dwell and floor rail, and the saturated-top path's dwell grid (smeared transitions, coarse sample periods, tolerated and refused irregularity, a skipped slot), unconfirmed-dwell measurement (settled tail, median, between its neighbours) and guards. Validated against 96 mutants (82 caught, 13 equivalent; the last was tied to a lead-in before a saturated sweep, since fixed). Also: a turn-on transient before a saturated sweep, on a normal and an inverted receiver, and before an unconfirmed dwell, does not shift the step numbering.
- **SeriesColumnSchema** (doctest, `native/tmdq_core_tests/test_seriescolumnschema.cpp`; no Qt; ported from the Qt `TestSeriesColumnSchema`, plan step 2.4, whose Qt version is deleted) — the CSV column-header schema (`SeriesColumnSchema`): SNR name and `columnHeader()` formatting, `parseColumnHeader()` classification (lock/missed suffix vs. SNR `"<id> - "` prefix, multi-word stream labels split at the last space, SNR-shape-wins-over-suffix), the unknown→SNR fallback, and the format→parse round trip that keeps `exportCsv` and `CsvSeriesParser` inverses; the three header strings (`kTimeHeader`, the two suffixes) pinned as the file format they are. Multi-file input: source 0 stays unqualified (byte-identical), source 1+ gets a leading `"S<n>| "` qualifier that round-trips through parse for both SNR and Lock/Missed shapes, an unqualified header still defaults to source 0, and a header merely starting with the letter `'S'` isn't misparsed as a qualifier. Written in layers: the original round-trip cases; a table of 40 parse and 8 format edge cases (the 35 and 8 recorded against the `QString` implementation BEFORE it moved - empty headers, ids and receiver numbers with signs, spaces, overflow, a qualifier with no space or a literal overflow, non-ASCII labels - and 5 parse rows added since, worked out from the code and not recorded from Qt: a receiver token without its underscore or in the wrong case, and the characters either side of `'0'..'9'` not being digits); the **40,000 generated headers**, the Qt suite's generator token for token on the same seed, checked against one FNV-1a digest of what the `QString` implementation answered for each (recorded once with a throwaway diagnostic, removed again) and the eight reach counters; and every combination of formatter inputs against the grammar written a second way. It also pins a limitation of the FORMAT: a series NAMED `S1| A` at source 0 reads back as source 1. Validated against 69 mutants (64 caught, 5 equivalent)
- **TestCsvSeries** (`tst_csvseries`) - what the plot does with the CSV series format, through `PlotViewModel` (`loadCsvFile`, `exportCsv`): 10 import rows (the load warning's count, the clock taken from the first row, the order each stream is given) and 9 export rows (every series, one source's, none), plus a non-ASCII folder, an export of nothing, an unwritable path, and export-import-export being byte-identical. The format itself (the parser, the writer, the clock stamp and the number reader) is the core's and is tested without Qt in `native/tmdq_core_tests/test_csvseries.cpp`: all 66 import rows (line endings, byte-order marks, blank and short rows, padded headers, cell number forms, special values, every timestamp shape, source qualifiers, channel counts; four record behaviour changed on purpose, with the old value in a comment) and 17 export rows (millisecond rounding, six-decimal ties, out-of-order and repeated samples, day boundaries, sources, non-ASCII labels), a file replaced rather than appended to, a non-ASCII folder, uneven time and value arrays, an export crossing the 1 MiB flush, and export-import-export being byte-identical. **Oracles** are what the replaced Qt code answered, recorded as digests: 400 generated files, 500 generated series sets, 8,000 clock stamps and 20,095 number texts (`QString::toDouble` / `toInt`) plus a 1.46 MB export, each generator asserted to reach every outcome in volume. 239 mutants of the parser, writer, clock stamp and number readers: 215 caught, 24 equivalent
- **CalibrationExtractor** (doctest, `native/tmdq_core_tests/test_calibrationextractor.cpp`; no Qt; ported from the Qt `TestCalibrationExtractor`, plan step 2.4, whose Qt version is deleted; reads the committed recordings through `repoFile()`) — US5.3 pipeline orchestration (complements TestStepDetector's pure logic): drives the async extraction end to end (reader + FrameProcessor workers → per-channel StepDetector). A bad file (with non-empty steps, so it clears the empty-steps guard) finishes unsuccessfully with a recorded error and no partial state; over `rnrz-l_testfile.ch10`, exactly words 6/7/8 (RCVR3 L/R/C, the only real stepped SNR sweep) build valid non-linear profiles while every other receiver word falls back to linear; over `step_cal, example.ch10` (receivers far out of alignment, its real 11-step 0–60 dB config, the shipped sequential word map), receivers 1, 3, 5 and 6 calibrate with every step paired to its own dwell (0 dB at the pre-sweep level, 60 dB at the top dwell, no split dwell taking a slot), receiver 1 over only the range that stayed in scale because its top two steps sit on the rail (readings hold at 48 dB), receiver 2 reports flat / no signal, and the report names the receivers; plus the pure `summarize()` report (receivers grouped, partial receivers spelled out with reasons, unnamed channels reported by name) and its two "check" prompts - a step file shorter than the recording and a word map that does not match it - each proven on the real recording with the wrong file, and silent on the correct setup; and clips that exceed the recording being reported rather than silently ignored; and `extractionReportsItsProgress`, which checks the extraction's progress reaches the dialog (non-decreasing, ending at 100); and `summarizeProducesExactlyThisReport` (15 scenarios, the **whole** report text each - empty, one receiver, mixed, unnamed, an all-calibrated unnamed group alone, beside a numbered receiver, beside a failure and with a saturated top, non-ASCII names, both "check" sections, a saturated top with exact-half dB values, a relaxed threshold, flat levels, several failure reasons, twelve receivers), `extractionLogsOneLinePerChannelWithData`, and `aNonAsciiChannelNameSurvivesExtractionItsLogAndItsReport` (a real `step_cal` extraction with a word map naming channels with non-ASCII text); the extractor's lifecycle through one `Recorder` helper: failures before anything starts (no steps; a word map that is missing, outside the frame, empty, or too large; an unopenable recording), a cancel from the first report and from another thread, a start while running being ignored, a finished extractor running again, destroying a running one, the end being the last report, a failure being an error and a cancel not, a worker failure reporting its own reason, and the clip-ignored log line Also: a clip that takes the whole recording from either end alone is reported and ignored, and a clip really removes the seconds it names (20 s off the start leaves only receiver 1 calibrated, off the end none); the whole step_cal extraction, channel by channel, against recorded numbers (outcome, plateaus, points, edge threshold, profile sum) unclipped and clipped; every per-channel log note (the MISMATCH marker, a failed channel's outcome and threshold, a railed channel's partial calibration, a relaxed threshold) and the tag on the worker's and reader's lines; the window sized from the frame rate (100 ms floor, 200 ms ceiling, ten frames, 12.5 printing as 13) and a slow recording measured in its own time; the default word map taking 49 channels of a 50-word frame but not 50; a word just past the frame refused; a cancel from the very first report (before the worker exists), a cancel that wakes a worker waiting for packets (the reader parked), and a cancel that lands after the worker finished (the worker parked on its last line); an extractor starting clean after a clipped, failed or cancelled run; the extractor no longer running while it reports the end; a destroyed extractor having been CANCELLED rather than waited out; and seven more report scenarios (the lead-in boundary and the most levels any channel held, the higher of two railed tops, an inverted receiver's top at the first point, a flat channel with no level, an unnamed flat channel beside a calibrated one, a plateau count of zero, a count with no data). Validated against 190 mutants (166 caught, 24 equivalent)
- **TestStreamConfigSchema** (`tst_streamconfigschema`) — `StreamConfigSchema` round trip for both `StreamMode` variants and the calibration input references (`calCh10Path`/`stepTomlPath`/`clipStartSec`/`clipEndSec`), confirms `calibrationByWord` itself never appears in the serialized JSON (and the `calibration` block is omitted entirely when no non-linear calibration was ever extracted), and `fromJson()` rejecting an object missing required fields while leaving in-class defaults for anything else omitted
- **TestProcessingTemplateSchema** (`tst_processingtemplateschema`) — processing templates / batch apply: `ProcessingTemplateSchema` round trip for both `StreamMode`s, series appearance, calibration input references, and `timeChannelIndex`; that appearance is omitted when empty; that `calibrationByWord` is never serialized (inherited from `StreamConfigSchema`); and `schemaVersion` accept/reject + malformed-document rejection — no `.ch10` fixture, runs in CI
- **TestBatchController** (`tst_batchcontroller`) — the Apply Template batch loop (US1.1): `buildTemplate()` capturing a source's configs + time channel + byte order + each series' name/color, `applyAppearance()` mapping a template's saved appearance onto the right series (both moved here from TestMainView, and no longer need a window to run), plus two cases the loop's old home made impractical — a queue whose files have all gone missing drains to `finished()` with both counted as skipped rather than stalling on a reader, and `start()` with nothing matched does not clear the session or announce a run; and `aFileWhoseChannelsNoLongerMatchIsSkippedAtItsTurn`, the second channel check made once a file has loaded (the first can be stale), which skips a file the template does not describe with a warning and counts it in the summary; and `matchFileComparesChannelIdsAndNothingElse` (from `TestTemplateMatcher`), the ids `matchFile` reads: every template entry and every file channel counts, and a channel's name plays no part
- **TemplateMatcher** (doctest, `native/tmdq_core_tests/test_templatematcher.cpp`; no Qt; ported from the Qt `TestTemplateMatcher`, plan step 2.4, whose Qt version is deleted) - batch apply channel-ID matching, called directly on two id lists: pass regardless of order; reject with the right `missing`/`extra` sets on a missing channel, an extra channel, and both; a file with no channels lacks every template channel; two empty sets match and an empty template finds every file channel extra; repeats count once and are reported once (including repeats that are not neighbours); results sorted; any `int` is an id. A second way of writing the comparison (element by element over `std::set`, sharing nothing with the matcher's sort / unique / set_difference) is compared over 4,000 generated id-list pairs (each outcome asserted reached). The two cases that were about `BatchController::matchFile` (every template entry and every file channel counts; a channel's name plays no part) moved to `TestBatchController::matchFileComparesChannelIdsAndNothingElse`. Validated against 20 mutants (the matcher and the ViewModel's wiring of it)
- **PacketQueue** (doctest, `native/tmdq_core_tests/test_packetqueue.cpp`; no Qt; ported from the Qt `TestPacketQueue`, plan step 2.4, whose Qt version is deleted) — the bounded reader-to-worker hand-off, tested directly rather than only through the pipeline: FIFO order, every `PacketItem` field surviving (including an embedded NUL in the payload), the producer blocking at exactly the configured depth and resuming after one dequeue, the consumer blocking when empty and resuming after one enqueue, `close()` releasing a blocked producer (`enqueue` returns false) and a blocked consumer (end-of-stream sentinel), a closed queue draining what it already holds before reporting end-of-stream, a non-positive depth falling back to the default of 64, and 20,000 items through a depth-4 queue in order under constant hand-off with a watchdog so a lost wakeup fails instead of hanging the suite. Validated against the original Qt-based queue (must pass) and eight single-defect mutants (every one must fail) - see the changelog. Pure logic, runs in CI Also: a producer-sent end-of-stream item does not close the queue, a closed queue's sentinel carries nothing, a full queue closed under a blocked producer keeps what it holds, and the bound holds at depths 1, 3 and 7. Re-validated against 26 mutants (25 caught; `close()` without its lock is a race no test can force).
- **IntervalGate** (doctest, `native/tmdq_core_tests/test_intervalgate.cpp`; no Qt; ported from the Qt `TestIntervalGate`, plan step 2.4, whose Qt version is deleted) — the progress throttle that replaced `QElapsedTimer` in `Ch10PacketReader::run()`, pinned with injected time points so nothing depends on a sleep: not due before the interval, due at *exactly* the interval (`>=`, not `>`), due long after without `due()` restarting the interval itself, the interval read as milliseconds rather than seconds or microseconds, `reset()` measuring from the reset, sub-millisecond precision agreeing with `QElapsedTimer`'s truncation on every side of the boundary, and a default-constructed gate starting at *now* rather than the clock's epoch (otherwise it is due the instant it exists and throttles nothing). Validated against seven single-defect mutants, every one of which must fail. Pure logic, runs in CI Also: intervals other than 100 ms (1, 7, 250, 5,000) each at its boundary, a zero interval always due, a time before the start never due, and a second reset replacing the first. Re-validated against 17 mutants.
- **TomlDocument and textfile** (doctest, `native/tmdq_core_tests/test_tomldocument.cpp` and `test_textfile.cpp`; no Qt, run by `scripts\test_native.ps1`, not part of the Qt executable) — ported from the Qt `TestTomlDocument` (plan step 2.4, the first suite moved; the Qt version is deleted). Every case of the original is there under the same description, plus the cases the mutation check showed it lacked. the settings-file reader/writer, pinned directly rather than only through `FrameSetup` and the dialogs: sections and keys in file order (not sorted), an empty `[EMPTY]` section still declared, `[[Name]]` opening an array-of-tables entry rather than a section (keys in file order with values as written - quotes kept, an unquoted comment stripped; names matched without case but otherwise exactly, so `[[ Step ]]` and `[[Steps]]` are not `[[Step]]`; a malformed `[[` line opening an entry no name matches; a section header or a half-header ending an entry; entries never leaking into sections), quote-aware `#` comments, `\"` and `\\` unescaped, a repeated key overwriting, a UTF-8 BOM with CRLF, UTF-16 LE and BE with a BOM, a non-ASCII value saved to and loaded from a folder outside every Western code page, the integer rule (whitespace and `+` tolerated, junk / fraction / overflow / empty return the caller's fallback with `parsed` false), the double rule (including `inf`), `QVariant::toBool()`'s boolean rule (even `no` is true), the exact bytes `save()` writes (top-level keys first, a blank line between sections, CRLF, no BOM), doubles as `%g` with six digits, `save()` truncating rather than merging, and both error paths naming the file. Written against the Qt implementation first; validated against four mutants of the `std` port. Pure logic, runs in CI `test_textfile.cpp` holds `textfile` directly: the UTF-8 mark (and two bytes of it not being one), UTF-16 in either byte order with and without a mark, UTF-8 lengths at 0x7F / 0x80 / 0x7FF / 0x800 / 0xD7FF / 0xE000 / 0xFFFF, surrogate pairs and unpaired surrogates (U+FFFD), an odd trailing byte, `readText` as binary (a Ctrl-Z and carriage returns come through), `utf8FileName` and `pathFromUtf8`. 39 cases, 300 assertions.
- **TestQtProcessingEvents** (`tst_qtprocessingevents`) — the hand-off from a processing thread to Qt that replaced `FrameProcessor`'s signals: 300 mixed log / error events raised on a worker thread arrive on the target's thread in the order raised with completion last (and the test really did cross threads); UTF-8 messages decode (an accented letter and an em dash); any handler may be missing; events still queued for a destroyed target are dropped, which is what lets the coordinator and extractor die with events in flight. Validated against mutants that deliver directly instead of queued (the run crashes) and that decode as Latin-1. Pure logic, runs in CI; and progress events (101 values from a worker thread arrive on the target's thread in order)
- **TextFormat** (doctest, `native/tmdq_core_tests/test_textformat.cpp`; no Qt; ported from the Qt `TestTextFormat`, plan step 2.4, whose Qt version is deleted) — `formatFixed()`, which reproduces `QString::number(v, 'f', n)` without Qt, checked against what Qt printed (recorded when the suite was ported) and against exact integer arithmetic: the exact ties the report prints (Qt rounds them away from zero, `std::format` to even), every n/64 at 0 to 6 decimals, 20,000 arbitrary doubles from a fixed generator, signs and negative zero, the extremes, and `isExactTie()` seeing only real ties. Pure logic, runs in CI Also: one representable step either side of every tie, the neighbours of a tie not being ties, and infinity and not-a-number printing as Qt prints them (`inf`, `-inf`, `nan` for either sign of NaN).
- **FrameSync** (doctest, `native/tmdq_core_tests/test_framesync.cpp`; no Qt; ported from the Qt `TestFrameSync`, plan step 2.4, whose Qt version is deleted) — the shared decode in `framesync.h`: `parseUnsignedHex` against what `QString::toULongLong(16)` answered (a recorded table of 78 inputs and a digest of 40,000 random strings) and a `std::format` round trip over 4,000 values in every accepted spelling, `resolveFrameSync` (the pattern's length counted in characters, an empty mask meaning every bit, the pattern checked before the mask and the result never half-written), `applyAcquisition` setting every raw-affecting field and no other, and `fullSyncMask` (every bit of the pattern, no shift by 64, a full mask for a pattern of no length, exactly N bits set for 1 to 63). Pure logic, runs in CI Also: every flag of `applyAcquisition` independently, megabits to bits per second, and the frame rounded up to whole words.
- **CalibrationRequest** (doctest, `native/tmdq_core_tests/test_calibrationrequest.cpp`; no Qt; ported from the Qt `TestCalibrationRequest`, plan step 2.4, whose Qt version is deleted) - building the Extract Calibration dialog's request in the Qt-free core (`buildCalibrationRequest`, plan step 2.2b): the sync text decoded like the main run (a length of four bits per character, whatever the characters), each refusal with the dialog's own words (empty and invalid pattern, invalid mask, the pattern checked first), a failed build leaving the request alone (refused for the pattern and for the mask, which return from different places), every other field carried, the default word map used only when none is named and the application root has one (also under a root with non-ASCII text in its name); one typical request read out in full; and the code the dialog used to run inline, kept in the test over `std::string` and `std::filesystem` as the oracle over 3,000 generated inputs with reach thresholds. Validated against 40 mutants (39 caught, 1 equivalent)
- **Abi** (doctest, `native/tmdq_core_tests/test_abi.cpp`; no Qt; ported from the Qt `TestAbi`, plan step 2.4, whose Qt version is deleted; the last core suite to move) - the C ABI of `tmdq_core.dll` (`native/tmdq_core/tmdq_abi.h`, revisions 1 to 4), called in-process: the ABI sources are compiled into the native test executable (`TMDQ_ABI_STATIC`) and used exactly as a host would use them. Pins every struct's size and field offsets (what the managed side must reproduce), every enum's number, the version and revision, and `tmdq_sizeof`; struct versioning both ways (a longer host struct accepted and read no further than known, a shorter one refused, a library-filled struct written no further than the host's own size, `readStruct` proved with guard bytes); the file handle against `Chapter10Reader` (ids, names, TMATS rates, default time channel, out-of-range and null arguments, the reader's own failure reason word for word); and runs against an oracle that builds and runs the same configs directly through `buildStreamJob` and `PipelineRunner` (a lock stream and a receiver stream together, sample for sample), each stream setting varied one at a time with the requirement that the core's result changes, the file-scoped byte order, a named word-map file, the application root present and absent, non-linear profiles (valid, invalid, duplicate), NULL optional text, the job builder's log lines arriving first on the calling thread, the documented order and threads of every callback, cancel, free while running, a result outliving its run and freed from another thread, a callback that throws, two runs at once, `wait` as the only synchronisation, every bad argument, the per-thread last error and the exception guard; and, from revision 2, the step file and its initial path, calibration extraction against the extractor run directly (every channel field, the report, the callbacks), each calibration setting varied one at a time, results refused while running, cancel, free in flight and `wait` alone; and, from revision 3, the formats (pattern file, template matching, CSV in both directions, clock stamps) each against the core called directly with byte-identical files; and, from revision 4, series construction, the shared time base, stream keys and imported stream orders over random streams, operation sequences and lists. Written against the core first; validated against 106 single-defect mutants of the ABI sources, the header and the guards (step 2.2a), 91 for the calibration side (step 2.2b) 73 for the formats (step 2.2c) and 57 for series construction (step 2.2d, with 5,700 random streams, time-base steps and CSV lists compared against the core). Ported, run against the same 307 distinct mutants: everything the Qt suite caught is caught, after two cases were tightened (a series set's data lifetime watched through a `weak_ptr`, since a Release heap does not overwrite freed memory; a CSV column with no time array refused)
- **TestCoreQtFree** (`tst_coreqtfree`) — the guard that keeps the processing core free of Qt: it walks the `#include "..."` closure of every core source and fails on any `<Q...>` header or any reach into `include/viewmodel` or `include/view`, requires every `src/model` source to be classified as core or UI-session, and proves its own walker on a temporary tree (several spellings of a Qt header and class and struct forward declarations seen through a shared header, a look-alike not counted, a layering violation, a commented-out include ignored). Since plan step 1.7 it also **confines Qt outside the View**: every file under `src/` and `include/` other than `src/view` / `include/view` that includes a Qt header or forward-declares a Qt class must be on `kQtOutsideTheView` (and a listed file that no longer does fails as stale, so the list only shrinks), and every other header must be Qt-free and layer-clean through its whole include closure; that guard is proven on a second temporary tree (unlisted, stale, exempt, look-alike, forward declaration, a header reaching up into the View). Since plan step 2.1 it also **keeps the MSBuild project that builds `tmdq_core.dll` (`native/tmdq_core/tmdq_core.vcxproj`) in step with the Qt build**: the same `src/model` sources as `kCoreSources`, the same irig106 files as the `.pro`, nothing from another layer, no mention of Qt outside its comments, and `tmdq.sln` builds it (one function, `nativeProjectProblems`, proved on a temporary tree with each kind of drift). Since plan step 2.2a it also **holds the C ABI's own sources** (`native/tmdq_core/*.cpp`) to the core's rule (no Qt, no ViewModel or View header, through their includes; `abiSourceProblems`, proved on a temporary tree) and checks that they are the same set in the project, in the native tests' project (which compiles them in for `test_abi.cpp`; `tests/tests.pro` did until `TestAbi` moved to doctest) and on disk. Pure file reading, runs in CI Since plan step 2.4 it also keeps the doctest project (`native/tmdq_core_tests/tmdq_core_tests.vcxproj`) honest: it compiles only core sources (each one in `kCoreSources`) and no file of another layer, never mentions Qt outside its comments, every `test_*.cpp` on disk is in the project and the other way round, the test files reach no Qt or View/ViewModel header through their includes, and `tmdq.sln` builds it (`nativeTestsProblems`, proved on a temporary tree by `theNativeTestsGuardSeesEveryKindOfDrift`).
- **TmDq.Core.Tests** (xunit v3, `dotnet/TmDq.Core.Tests`; not part of the Qt executable - run by `scripts/test_dotnet.ps1`, 301 tests) - the C# binding of the C ABI against the real `tmdq_core.dll`: the struct layout pinned against `tmdq_sizeof` and to the offsets the native `Abi` cases pin (`LayoutTests`); the channel lists of the five recordings (`FileTests`); runs of the committed recordings in both modes against the numbers the Qt pipeline produces, every stream setting reaching the job one at a time, the byte order and application root, a named word-map file, calibration profiles, failures with their statuses and messages, observer order and exceptions, cancel, dispose, results outliving runs and giving their memory back (`RunTests`); step files and calibration extraction against the numbers the core's own `CalibrationExtractor` makes - all 48 channels, the report, seven variations, the profiles fed to a run (`CalibrationTests`, expected values in `CalibrationDigest`); the file formats - pattern files, template matching, CSV in both directions, clock stamps - over a corpus against what the core's own functions answer, byte for byte (`FormatsTests`, `FormatsDigest`); what `Library.Load` checks and how a second load behaves (`LibraryTests`); series construction - the series of real streams, the shared time base over a 90-step operation sequence, stream keys, the order of imported streams (`SeriesTests`, `SeriesDigest`); the arena and the snprintf-style text helper (`ArenaTests`); and the not-yet-loaded state, seen only in a child process of the same executable (`IsolatedLoadTests`, whose probe is the one skip). Validated against 197 single-defect mutants of the library (plan step 2.3a) 138 more (step 2.3b), 125 more (step 2.3c) and 108 more (step 2.3d)
- **TmDq.App.Tests** (xunit v3 on `Avalonia.Headless.XUnit`, `dotnet/TmDq.App.Tests`; not part of the Qt executable, 35 tests; plan step 3.1) - the Avalonia application on the headless platform with its own `App` class and Skia drawing: the main window's title, version label (laid out by the real theme) and sizes (`MainWindowTests`); the version read from `include/constants.h` with the tests' own pattern in both assemblies and the exe's version resource (`VersionTests`), and `dotnet/build/AppVersion.props` built against made-up headers (`VersionBuildTests`); the UTF-8 code-page manifest read back out of the built exe (`ManifestTests`); `scripts/check_package_licences.ps1` against packages folders the tests write (`PackageLicenceTests`) and `dotnet/build/PackageLicences.targets` in real SDK builds against a local feed with a fake commercial package (`LicenceBuildTests`). Validated against 46 mutants (43 caught by a test, 2 by the build, 1 unreachable guard)
- **EventReporter** (doctest, `native/tmdq_core_tests/test_eventreporter.cpp`; no Qt; ported from the Qt `TestEventReporter`, plan step 2.4, whose Qt version is deleted) — `EventReporter`, the null-safe forwarder every processing source reports through: every call harmless with no sink, each of log / error / finished / progress reaching its own sink method with its argument, calls arriving in order, a replaced sink receiving later calls and the old one none, a cleared sink going silent, and the reporter (not a borrowed pointer) keeping a shared sink alive. Pure logic, runs in CI Also: a sink that does not override the optional `progressUpdated` still hears everything else, and arguments pass through unchanged (UTF-8, empty, an embedded NUL, any progress value).
- **PipelineRunner** (doctest, `native/tmdq_core_tests/test_pipelinerunner.cpp`; no Qt; ported from the Qt `TestPipelineRunner`, plan step 2.4, whose Qt version is deleted; it also holds the throughput guard, see below) — the Qt-free pipeline, with a plain recording sink and no event loop, on the committed `step_cal` recording: a single stream delivers its result (stamped with `jobIndex` and the job's `sourceId`) and then the end, with every worker line before the result, the end last, and exactly one progress of 100 (the runner adds none of its own); several streams on one channel each deliver their own result; a stream that finds no sync does not stop the others, and says why; a run in which every stream fails ends unsuccessfully; a recording that cannot be opened (or no jobs) fails before anything runs, with a reason and nothing reported, and leaves the runner usable; a second start while running is refused and does not disturb the first; cancel ends the run unsuccessfully with no result and is a no-op with nothing running, and four cases hold the sink at a chosen moment of a real run (a thread parked on a log line) to pin what a race cannot: a stream that finished after the cancel was asked for delivers nothing and the run fails even though another stream had delivered, a worker waiting on an empty queue is woken by the cancel while the reader is still stuck, a worker that has not started reading is stopped by its own abort flag, and `running()` is already false inside the end report; the runner runs again after a run ends, without waiting for the thread that reported it; a stream that ends early without a cancel (its processor aborted, reached through the test being a friend) closes its own queue so the reader is not left blocked on it and the healthy stream still finishes, and destroying a running runner stops it and has reported the end by the time the destructor returns Also: several streams on one channel each get every packet (their results are identical, not merely the same length), a stream that fails after another delivered does not undo the run's success (the failing stream is parked on its report until the good one has delivered), the first job names the recording, a cancel wakes EVERY worker waiting on an empty queue and the reader sees it too, nothing of one run is left over in the next (success, failure and cancel in turn), a run lets go of its frame setups however it ends, and the two refusal messages are exact. Validated against 58 mutants (52 caught, 6 equivalent). **The throughput guard** is the case "PipelineRunner throughput: four streams on one recording cost about what the slowest costs": opt-in (`TMDQA_RUN_HEAVY_BENCH`, skipped and listed as skipped without it), run at release by `scripts\run_throughput_bench.ps1`, and a FAILURE rather than a skip once asked for if the recording cannot satisfy it. It asserts ratios (parallel over the slowest solo at most 1.50, over all-sequential at most 0.85; measured 1.01 and 0.64), never milliseconds. Validated against 5 mutants of its limits and checks, all caught.

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

All identified feature items have been implemented. The one initiative in progress is the move off Qt
to Avalonia; its work plan is `docs/dotnet-migration/avalonia-plan.md`.

## Additional Resources

- [Qt 6 Documentation](https://doc.qt.io/qt-6/)
- [IRIG 106 Chapter 10 Standard](https://www.irig106.org/)
- [irig106utils GitHub](https://github.com/atac/irig106utils)
- [Qt Coding Conventions](https://wiki.qt.io/Qt_Coding_Style)
