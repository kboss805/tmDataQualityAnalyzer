# tmDataQualityAnalyzer — AI Assistant Guide

This file provides context and guidelines for AI assistants working on the tmDataQualityAnalyzer project.

## Version Information

- **.NET**: .NET 10 SDK; the application is Avalonia 12 (`dotnet/TmDq.App`).
- **Compiler**: MSVC 2022 (Visual Studio 2022 C++ Build Tools, MSVC 14.44 / v143, pinned by `scripts\env.ps1`) for
  the native core. This is the only supported toolchain; CI builds with it too (`windows-2022`, checked by the job).
- **C++ Standard**: C++20 (`std::format`, `std::span`, `inline constexpr` throughout `constants.h`).
- **Shipping application** (from v3.0.0): the Avalonia 12 application on .NET 10 (`dotnet/TmDq.App`) over the native
  core (`tmdq_core.dll`), packaged by `deploy/avalonia/build_avalonia_package.ps1`. Up to v2.14.2 the application was
  Qt 6; `docs/changelog.md` records the move.
- **Project Version**: 3.0.0 — defined once in the `AppVersion` struct in `include/constants.h`; `dotnet/build/AppVersion.props` reads it for every assembly, and the package script and the manual build read it too, so no other file carries a duplicate version literal

## User Stories

The stories below follow the workflow a first-time user takes through the application: open a file and choose what to process (US1), define the per-stream parameters (US2), view the resulting data-quality metrics (US3), customize the plot (US4), manage configuration files and calibration (US5), export and re-import results (US6), and the cross-cutting concerns of input validation (US7), installation (US8), and theming (US9).

**Status: all user stories are Complete** — implemented, tested and shipped. Every acceptance criterion below is delivered functionality (`[x]`), each held by a test or a recorded manual check (`docs/records/parity.md` maps them), and each story carries a **Complete** marker in its heading. New work is tracked as new stories appended after US9.0.

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
  - **Scope:** Frame-sync Load/Save round-trips the frame sync pattern, sync mask, words/frame (bits per frame), and the two stream-format flags — **Invert Data** and **Derandomize**. Those two describe how the recording's bits are encoded (US2.0 calls NRZ-L / RNRZ-L a PCM code format), so a saved pattern file is only usable if it carries them: loading PRN-15 into a randomized stream and having to tick Derandomize by hand every time is the bug this fixes. **Data Rate** and **Sample Rate** stay out — the first is usually TMATS-derived and the second is an output resolution the operator chooses per session, so neither belongs to the pattern. Keep the core's `FrameSyncFile` read and write symmetric (the app's `StreamFiles.LoadFrameSync` / `SaveFrameSync` call them).
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
  - **Config application:** each file's run feeds the template's `StreamConfig`s through unchanged (`TemplateBatch.ToRunRequest`); the reader dispatches each stream by `pcmChannelId`, which is safe precisely because of the exact-set match. **Non-linear calibration:** a template stores only the calibration *input references* (not the extracted profile), so **all batch files process with the linear slope/offset fallback**. Non-linear step calibration is available for single-file processing only; re-extraction on template apply is a possible future add.
  - **Custom names/colors:** captured only when **saving a template** from an already-processed, customized plot (`TemplateBatch.Build`), and reapplied per file after each run (`TemplateBatch.ApplyAppearance`, matched by channel id + metric/receiver/channel). Every file's copy of a channel intentionally shares the same name/color (they are the same channel across recordings); use the **Plot File** selector to view one file at a time when per-file distinction matters.
  - **Retain-all + file selector:** a batch always keeps every processed file in memory; the plot's right-click **Plot File** submenu chooses which file to view, defaulting to every source overlaid, with "All files (overlaid)" for comparison. The filter is a `PlotViewModel` source gate (`SetVisibleSource`) orthogonal to per-series visibility and the **View Mode** (lock %/accumulation) selector. Optional per-file export runs as a post-pass (`BatchRunner` shows each source alone, then writes its CSV, and a `_framesync_lock.png` and `_missed_frames.png`, through `SessionExport`).
  - **Status: implemented** (Save/Apply Template, one `BatchRunner` batch loop, retain-all + Plot File selector), every part tested in `TemplateTests` and `TemplateBatchTests`, the batch over copies of the committed PRN sample. See `docs/processing-template-design.md`.

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
  - **Scope:** Orthogonal to US3.1 (frame sync error accumulation); split out as its own story. Fan-out copies the same fields the setup dialog writes (`StreamRules.ApplyToAll`, called by `ConfigureStreamsViewModel`); it does not widen the Frame Sync Load/Save TOML boundary defined in US1.0.

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
  - **Scope:** A "missed frame" is a discrete loss-of-lock event — while in lock, the stream ran past the expected minor-frame boundary (`bits_in_frame`) without a sync match. The metric is a cumulative count of these events per stream, counted only within the selected `[start, stop]` processing window, and only ever increases. It is NOT a bit-level (Hamming/BER) error count. The user-facing label is "Accumulated Missed Frames" (`PlotViewModel.MissedFramesAxisLabel`); this is the canonical term across the UI, release notes, and code.

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
  - **A criterion removed (plan step 5.1):** "Frame Sync Lock curves are drawn with a visually highlighted style, while Receiver SNR curves are not." Neither application has ever done it: every curve has had the same 1.5 px pen since the QCustomPlot days, and the colours above are what tell the two kinds apart. The parity review found it ticked but untrue, and the story now says what ships (`docs/records/parity.md`).

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
  - **The keys are the chart's, not the window's:** they live in `PlotView.OnKeyDown`. A
    bare-letter window shortcut would be handled ahead of the focused control, so it would
    swallow that character everywhere the user types (the plot-title prompt, the time-window
    fields, any setup dialog). A click on the chart focuses it, which is what makes them
    reachable (`TheKeysAreTheChartsAndNotTheWindows`, `AClickOnTheChartGivesItTheKeys`).
  - **Any further plot control** goes to the context menu or an on-chart overlay, never to a
    control around the chart: that is the standing rule this story sets, and
    `ThePlotViewIsTheChartWithItsControlsOverItAndNothingAround` holds it.
  - **The menu advertises each key** as its item's gesture, `V` on the View Mode items
    (`TheMenuAdvertisesEveryKeyOfThePlot`).
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
- [x] The dialog window includes a "Save" button to save the key parameters to the configuration file.
- [x] The dialog window includes a "Cancel" button to cancel the operation.
  - **A criterion removed (plan step 5.1):** "The dialog window includes a "Reset" button to reset the key parameters to their default values." Neither application's Receiver SNR dialog has one. Loading the shipped `settings/receiver_params/default.toml` puts the shipped defaults back, which is what the button would have done. The parity review found it ticked but untrue, and the story now says what ships (`docs/records/parity.md`).

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

- [x] The current plot is exported as a single image of exactly what the chart shows: the series shown, the left axis's metric in view, the time window and axis limits as they are set, and the legend where it sits (or none when hidden).
  - **Scope:** the image is the view, not the data (the CSV is the data, US6.1), so a chart is made by setting up the screen. The interactive overlays are not part of the chart and are left out: the cursor's crosshair, a zoom band being dragged, the on-chart chips and "Loading...".
- [x] The user selects the image format from one of the following: svg, png, pdf.
- [x] The user specifies the image filename and location via the export dialog.
- [x] The movable legend is rendered into the exported image at its on-plot position.
- [x] A single export dialog lets the user select any combination of image, data, and log exports and run them in one action.

### US6.1: Export the plotted data to a CSV file — Complete

**As a** telemetry engineer or data analyst
**I want to** export the plotted data (framesync lock statistics, accumulated missed frames, and SNR signal data) to a CSV file
**So that** I can import the file into third-party applications such as Excel and MATLAB for further analysis.

**Acceptance Criteria:**

- [x] The current plot data is exported to a single CSV file covering every Frame Sync Lock, Accumulated Missed Frames, and Receiver SNR series, every time: shown or hidden, whichever metric the view is on.
  - **Scope:** the CSV is the data, not the view. Hiding a series, switching the left axis's metric or zooming changes what is drawn, never what is exported; only the batch run's per-file export narrows it, to one source.
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
  - **Scope:** The import is the core's CSV parser (`CsvSeriesParser`, through `PlotCsv.Parse`) behind `PlotViewModel.LoadCsv` / `LoadCsvAsync`, the matched partner of the export (`PlotViewModel.ExportCsv`, the core's `CsvSeriesWriter`); the app adds the entry points (the menu, drag and drop, Recent Files) and the error reporting. It does not add a CSV import dialog with column mapping, and it does not attempt to import arbitrary third-party CSVs — only files this application produced.

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
  - **Replaces two criteria that described a merge this project never built.** They claimed the installer saved a conflicting default as `new_x.toml` and carried the user's old values into it. Nothing implements that: the installer script (`deploy/avalonia/tmDataQualityAnalyzer.iss`) is declarative, with no `[Code]` section, and there is no `new_x`, migration, schema-version or merge logic in the core, `deploy/` or `scripts/`. Confirmed on a clean machine by `deploy/sandbox/` - a first run wrote nothing under `settings\` and no registry key.
  - **The guarantee above is weaker but true, and the defensive readers are what make it safe** - `FrameSetup::readReceiverParams()` accepts both the current `[Receivers]` keys and the older `[Parameters]` spelling, an absent `Randomized` leaves the operator's choice alone rather than clearing it, and the template reader (`TemplateFile`) keeps each key's default for anything omitted. A change that removed that tolerance would look harmless and would not be: it is load-bearing for this criterion, not incidental.
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

## Tech Stack

- **The application**: C# on .NET 10, Avalonia 12 (MIT) with CommunityToolkit.Mvvm - `dotnet/TmDq.App`.
- **The native core**: C++20, built by MSBuild into `tmdq_core.dll` (`native/tmdq_core`), with a C ABI (`tmdq_abi.h`)
  that `dotnet/TmDq.Core` binds.
- **Platform**: Windows x64.
- **External libraries**: irig106utils (a vendored C library, compiled into the core) and doctest (the native tests'
  single header, `lib/doctest`). Charting is first-party: the core builds the geometry (`chartgeometry`), the app draws
  it (`PlotChart`) with Skia. Every .NET package the app ships is open source and on the licence allow-list
  (`scripts/check_package_licences.ps1`); no licensed tooling is used, and the project stays GPL-3.0.
- **History**: up to v2.14.2 the application was Qt 6 (Qt Widgets, qmake) over the same core; v3.0.0 is the first
  release of this one. `docs/changelog.md` records every release and every step of the move.

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

**If changes are needed**: They should be made by wrapping/adapting the library in the core (e.g., `chapter10reader.cpp`, `ch10packetreader.cpp`, `frameprocessor.cpp`), NOT by modifying the library files directly.

## Architecture

Three layers, each tested on its own:

1. **The native core** (`include/`, `src/model/`): reading Chapter 10 recordings, decommutation, calibration, the file
   formats (TOML settings, the CSV export, the frame-sync pattern file) and the plot's series and geometry. Plain
   C++20 with no UI. The processing pipeline is a **single-reader / parallel-worker** one: a run reads the file exactly
   once while each selected stream is processed on its own worker thread, and the results stay in memory.
2. **The C ABI and the C# library**: `native/tmdq_core` builds the core into `tmdq_core.dll` behind `tmdq_abi.h`
   (opaque handles, plain structs, a version and revision, every error as a code and a message); `dotnet/TmDq.Core` is
   its C# binding, and nothing above it touches a pointer.
3. **The application** (`dotnet/TmDq.App`, MVVM): Avalonia views over CommunityToolkit.Mvvm view models. The
   presentation state (what is shown, the axes, the legend, the dialogs' rules, preferences, the template JSON) is C#;
   the data and every file format the core reads or writes are the core's. Views hold no rules: code-behind does only
   what a control must (focus, pointer gestures, popups), and view models reach no control.

**Multi-source plots:** the plot can hold several recordings' series at once, and only the Apply Template batch puts
them there. Every recording is a source with its own id, so two recordings that reuse a PCM channel id never mix, and
all share one elapsed-seconds X axis based at the earliest sample of any of them (the core's `seriesbuilder`). There is
one processing run at a time: a batch is N runs into one accumulating plot. The plot's **Plot File** selector shows one
recording or all (`PlotViewModel.SetVisibleSource`), orthogonal to per-series visibility and the View Mode.

**Processing templates** (US1.1, `docs/processing-template-design.md`): **Save as Template…** captures one processed
recording's rows of Configure Streams, its time channel's place and byte order, and each series' name and colour
(`TemplateFile`, the same JSON v2.x wrote); **Apply Template to Files…** checks each chosen file's PCM channel ids
against the template's (the core's `TemplateMatcher`, exact-set match), then runs every match in turn (`BatchRunner`),
optionally exporting a CSV and the plot images per file. A template stores only a calibration's inputs, never its
profile, so a batch processes with the linear calibration.

### Core Components

#### The application (`dotnet/TmDq.App`)

- **`MainWindow` / `MainWindowViewModel`** (`ViewModels`, `Views`): the shell - its own title bar (snap layouts on the
  maximize button), the hamburger menu (Process, Import/Export, Settings, Help, Exit), Recent Files, drag and drop, the
  log sidebar (Ctrl+B), the themes, and the commands behind them: Open, Import CSV, Export, Save as Template, Apply
  Template. Three jobs are types of their own: `ShellPreferences` (Recent Files, the theme, the sidebar, the last
  folder, the legend's choice), `SessionExport` (the data, the image and the log written out) and `BatchRunner` (Apply
  Template's batch and each file's export). Its dialogs, pickers, configurator, clock and preferences are handed in as
  one `MainWindowServices`; each service left out has the effect its property states.
- **Configure Streams** (`ConfigureStreamsViewModel` and its window) and the setup dialog (`StreamSetupViewModel`, in
  Frame Sync Lock or Receiver SNR mode), over the rules in `Streams` (`StreamRules`, `StreamDefaults`, `StreamFiles`):
  one row per PCM channel, each problem shown as it arises, OK and Process held until every checked row is ready,
  Apply to all (US2.2), the pattern and receiver-parameter files' Load and Save.
- **Extract Calibration** (`ExtractCalibrationViewModel`) behind Apply Cal (US5.3).
- **The run** (`Processing`: `StreamsRun`, `ProcessingViewModel` and its window; `Logging`: `SessionLog`, `LogView`):
  the core's run on its own threads, the modal progress dialog whose Cancel stops every stream, and the log.
- **The plot** (`Plot`): `PlotViewModel` (the series built by the core, the axes and overrides, the view mode, the
  recordings and Plot File, the readout's pin), with four jobs in types of their own: `PlotTimeWindow` (the span, the
  window and its zoom history, the gestures and keys that move it), `PlotReadout` (what the readout says), `PlotCsv`
  (the series to and from the core's CSV) and `PlotColors` (the colours). `PlotChart` draws it (from the core's
  geometry, a cached data layer, the gestures), and `PlotView` holds what is over it (the legend, the chip bar, the
  right-click menu, the keys, Customize View).
- **Export and import** (`ExportViewModel`, `PlotImageExport`): the CSV through the core's writer (every series), the
  image through one Skia renderer for PNG, SVG and PDF (exactly what the chart shows), the log as text; Import CSV
  through the core's parser.
- **Templates** (`Templates`): `TemplateFile` (the JSON), `TemplateBatch` (matching, building a template from a
  recording, putting its looks back, a file's run), the batch confirmation.
- **Preferences** (`Settings`: `Preferences`, `PreferencesStore`, `QtSettingsImport`, `QtIni`): `preferences.json` in
  `%APPDATA%\tmDataQualityAnalyzer` (beside the exe for a portable copy), filled once from v2.x's INI file on the first
  run. The import stays through the v3.0.x releases, then goes.
- **Help**: the manual compiled in (`resources/usermanual.html`), or the full one beside the exe.

#### The C# library (`dotnet/TmDq.Core`)

The binding of every ABI function: `Ch10File` (a recording's channels), `ProcessingRun` and `StreamResult` (a run and
what it produced, with progress, log and cancel), `CalibrationRun` and `StepFile`, the file formats (`FrameSyncFile`,
`ReceiverParamsFile`, `CsvFile`, `TemplateMatcher`, `ClockTime`), series construction (`SeriesSet`, `TimeBase`,
`ImportedStreams`) and `ChartGeometry`. `Library` loads `tmdq_core.dll` (the app passes the one beside it, or
`TMDQ_CORE_DLL`: `NativeCore`) and refuses one whose ABI version or revision it does not know.

#### The native core (`include/`, `src/model/`)

1. **Chapter10Reader** (`chapter10reader`) - reads a recording's **metadata** up front: scans TMATS to catalogue its
   time and PCM channels (`ChannelData` values in a map by id, names and data rates), with a failed load's reason in
   `lastError()`. Wraps the irig106utils C library.
2. **Ch10PacketReader** (`ch10packetreader`) - the single-pass reader: `prepare()` opens the file, parses TMATS,
   resolves each stream's PCM attributes and routes channel ids to `PacketQueue`s; `run()`, on its own thread, reads
   the file once, tracks IRIG time, dispatches each PCM packet's payload to its queues, and ends each with a sentinel.
   It owns the per-channel `SuChanInfo` table.
3. **PacketQueue** (`packetqueue.h`) - the bounded, thread-safe hand-off from the reader to one worker.
4. **FrameProcessor** (`frameprocessor`) - the per-stream worker: drains its queue, runs the bit-serial frame-sync
   scanner (acquire and lock, off-phase rejection, bit-span lock %), and accumulates lock %, missed frames and (Receiver
   SNR) calibrated channel values into a `ProcessedStreamData`; derandomizes and inverts as told, and applies the linear
   slope and offset or a non-linear `CalibrationProfile` per channel.
5. **PipelineRunner** (`pipelinerunner`) - owns a run's lifecycle: the queues, the reader and one worker thread per
   stream, joined before it reports the end.
6. **ProcessingEvents / EventReporter** (`processingevents.h`) - how the reader, the workers and the extractor report
   log lines, errors, progress and completion; the ABI relays them to the host.
7. **FrameSetup** (`framesetup`) - the frame's word map and calibration table, built per job; reads and writes the
   receiver-parameters file.
8. **framesync** (`framesync.h`) - the shared decode of a pattern and mask, and `applyAcquisition`, which builds every
   raw-affecting field of a run's parameters (the main run and the calibration extractor both go through it).
9. **StreamJobBuilder** (`streamjobbuilder`) - turns one stream's settings into a processing job: validates the pattern,
   mask and frame length, builds the word map (the named receiver-parameters file, else the shipped default, else the
   default map), applies the linear calibration and attaches step-calibration profiles, or says why it refuses.
10. **StepDetector** (`stepdetector`) - *US5.3*: parses the `[[Step]]` step file and finds the step plateaus in a raw
    series, building a per-channel `CalibrationProfile`.
11. **CalibrationExtractor** (`calibrationextractor`, with `calibrationrequest`) - *US5.3*: a raw extraction over a
    calibration recording through the same reader and worker, then the step detector per channel, and the report.
12. **TomlDocument** (`tomldocument`, with `textfile`) - the TOML subset every settings file uses, sections in file
    order, arrays of tables (`[[Name]]`) read only; `FrameSyncFile` (`framesyncfile`) is the pattern file over it.
13. **CsvSeriesParser / CsvSeriesWriter** (`csvseriesparser`, `csvserieswriter`, `clocktime`) - the two directions of
    the application's CSV, matched inverses, with the `DDD:HH:MM:SS.mmm` stamp; **SeriesColumnSchema**
    (`seriescolumnschema`) is the single source of the column headers both use.
14. **SeriesBuilder** (`seriesbuilder`, `seriesrange.h`) - a processed stream's plot series, the shared time base
    across sources, imported streams' order, and stream identity; **ChartGeometry** (`chartgeometry`) - the plot's
    envelope, culling, min-max band, transforms and time ticks.
15. **TemplateMatcher** (`templatematcher`) - a file's PCM channel ids against a template's, exact-set.
16. **IRIG 106 Library** (`lib/irig106/`) - the third-party C library for the Chapter 10 format (see Protected Files).
    Its setup is serialised (`irig106lock.h`).

### Data Flow

```text
User opens .ch10 ─► MainWindowViewModel ─► Ch10File (TmDq.Core) ─► Chapter10Reader (metadata)
                                │
                   Configure Streams (per-stream settings)
                                │
                StreamsRun builds a RunRequest ─► ProcessingRun (TmDq.Core, through the C ABI)
                                                        │
                                       PipelineRunner (the core, its own threads)
                                  ┌─────────────────────┴───────────┐
                          Ch10PacketReader (one pass)               │
                                  │  routes packets                 │
                             PacketQueue ─► FrameProcessor (one worker per stream, parallel)
                                                        │
                                  ProcessedStreamData (in memory, carries its source id)
                                                        │
                       StreamResult ─► the core's series builder ─► PlotViewModel ─► PlotChart
                                      [one shared elapsed axis, based at the earliest
                                       sample across every loaded source]
```

## Coding Conventions

### File Organization

- **The core**: headers in `include/` (`dto/`, `model/`, `constants.h`), sources in `src/model/`; the ABI in
  `native/tmdq_core/`; the doctest suite in `native/tmdq_core_tests/`. A new core source goes in `tmdq_core.vcxproj`,
  and a new ABI source in both native projects; `NativeProjectTests` fails until they list it.
- **.NET**: `dotnet/TmDq.Core` (the binding) and `dotnet/TmDq.App` (folders by role: `ViewModels`, `Views`, `Plot`,
  `Streams`, `Processing`, `Settings`, `Templates`, `Services`, `Logging`), each with a `.Tests` project beside it;
  `dotnet/build` holds the props and targets every project imports.
- **Resources**: `resources/` (the base manual, the icon, the UTF-8 manifest); shipped settings in `settings/`.

### Naming Conventions

- **C++ (the core)**: classes PascalCase (`Chapter10Reader`, `FrameProcessor`); constants `kPascalCase` in namespaces
  (`PCMConstants::kWordsInMinorFrame`); members `m_` with snake_case (`m_frame_setup`); methods camelCase;
  `ProcessingParams` fields snake_case, the newer value types (`StreamJob`, `ProcessedStreamData`,
  `CalibrationProfile`) camelCase.
- **C#**: the .NET conventions - PascalCase types, members and properties, `_camelCase` private fields; tests named
  for what they hold, as sentences (`ABatchRunsEachFileInTurnKeepsEachAndExportsEachAsV214Did`).
- **Comments say what the code does and why**, on their own: a rule is stated, not explained by another library's
  behaviour. A test whose expected answer was recorded from v2.14.2 says so once ("as v2.14").

### Working Method

The rules the move off Qt ran on (the plan's rules 2 to 5, which code comments still cite), and how every change is
made now:

1. **Tests first, on the code as it is.** Before changing or reimplementing anything, pin what it does today, run the
   pins against the unchanged code, and only then change it. For a reimplementation, keep the old implementation in
   the test as an oracle and compare the two over generated input.
2. **Mutation-check every change** (`scripts\mutate.ps1`). A test that cannot fail proves nothing: single-defect
   mutants per rule, and a survivor is a missing test or a line that does nothing, not a shrug.
3. **A clean build before believing a result**, zero warnings, and the Release throughput gate
   (`scripts\run_throughput_bench.ps1`) for anything on the decode path.
4. **One implementation of every file format.** The CSV, the TOML settings files and the pattern file are the core's;
   the template JSON and the preferences are the app's. Never two hand-written copies of one format.
5. **A refactor that needs a test changed has changed behaviour**: say so, or do not do it.
6. **One PR per change, with its changelog entry** (`docs/changelog.md`, under Unreleased).

### Include Ordering (the core, Google C++ Style)

Order includes in each `.cpp` / `.h` file as follows, with a blank line between groups:

1. Related header (e.g., `#include "frameprocessor.h"` in `frameprocessor.cpp`)
2. C system headers (e.g., `<time.h>`)
3. C++ standard library headers (e.g., `<fstream>`, `<string>`)
4. irig106 library headers — keep in dependency order, not alphabetized
5. Project headers (e.g., `"channeldata.h"`, `"constants.h"`) — alphabetized

## Build System

Everything builds from `tmdq.sln`, which VS Code opens by default; `.claude/CLAUDE.md` gives each script's switches:

- **`tmdq_core.dll`**: `scripts\build_core_dll.ps1 [-Configuration Debug|Release]` (MSBuild over
  `native/tmdq_core/tmdq_core.vcxproj`, into `native/out/<config>/`), then fails on any compiler warning or on an import
  outside a short allow-list.
- **The .NET projects**: built by the test scripts; every project imports `dotnet/build/AppVersion.props` (the version,
  read from `AppVersion`) and `dotnet/build/PackageLicences.targets` (the licence allow-list).
- **The manuals**: `scripts\build_manual.ps1` renders the figures from the app (`ManualFiguresTests`) and embeds them.
- **The release**: `deploy\avalonia\build_avalonia_package.ps1` (single-file publish, signing, the portable ZIP and the
  Inno installer); `.claude/skills/cut-release` is the procedure.

### clangd / IntelliSense (`compile_flags.txt`)

clangd and clang-tidy read a single project-root `compile_flags.txt` — one flag per
line, applied to every translation unit. It is **generated, not committed** (its paths
are machine-specific and absolute), and it is **gitignored**, so a fresh clone has
no IntelliSense until you run:

```powershell
py scripts/gen_compile_flags.py
```

It targets MSVC (`--target=x86_64-pc-windows-msvc`, `-fms-compatibility`), and clangd
then locates the MSVC and Windows SDK system headers itself — those paths are
deliberately **not** in the file. Verify with:

```powershell
clangd --check=src/model/frameprocessor.cpp
```

which should report no diagnostics. (clangd 22 can still end in `1 errors` on a file where one of its own
refactoring self-tests, a "tweak", fails: that line is not a problem in the code.)

**Worktree trap.** The paths in `compile_flags.txt` are absolute into the checkout that
generated it, and `.claude/worktrees/<name>/` sits *inside* the main checkout — so
clangd walking up from a worktree source file finds the **main** tree's
`compile_flags.txt` and resolves every project header from the **main** tree. It still
reports `0 errors`, so nothing looks wrong while go-to-definition, diagnostics, and
completion all describe code you are not editing. **Run the generator inside each
worktree** (it writes relative to its own repo root), same as the `tests/data` junction
that worktrees also need.

### Deployment & Packaging

- **Build automation**: `deploy/avalonia/build_avalonia_package.ps1` — the consistency check, a signing pre-flight,
  the core in Release, a self-contained single-file publish, staging (settings, the VC++ runtime the core imports, the
  licence, `THIRD-PARTY-NOTICES.txt`), signing our two binaries only, a smoke run on the PRN sample, the ZIP and the
  installer.
- **Inno Setup installer**: `deploy/avalonia/tmDataQualityAnalyzer.iss` — v2.x's AppId and folder, so it upgrades a
  v2.x install in place (its `bin\`, `UserGuide.txt` and old manual deleted, `settings\` kept), admin or per-user,
  the `.ch10` association, the "What's New" page, the optional full manual.
- **Portable ZIP**: the same files flat, with a `portable` marker (preferences in a `tmDataQualityAnalyzer` folder beside
  the exe), `UserManual.html`, `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt` and `README.txt`.
- **Release notes**: `deploy/RELEASENOTES.txt` — the installer's "What's New" page (`InfoBeforeFile`) and the GitHub
  release's text. The developer's record is `docs/changelog.md`.
- **Portable README**: `deploy/README_portable.txt` — copied as `README.txt` into the ZIP.
- **Clean-machine test**: `deploy/sandbox/run_sandbox_test.ps1` with `verify_avalonia_install.ps1` — Smart App Control
  on, an unsigned control blocked, v2.14.2 installed and upgraded.
- **Code signing**: the certificate's SHA-1 in `SIGN_CERT_SHA1` (falls back to the Certum OSS certificate), token-backed
  (SimplySign): unlock it first.

## Important Implementation Notes

### Chapter 10 File Handling

- Uses irig106utils library (C code, not C++)
- Be careful with C/C++ interop (no exceptions in C code)
- File handles managed through `m_file_handle`
- Buffer management for reading packets
- irig106 setup is serialised (`lockIrig106Setup()`), and the path is passed in the process code page, which the UTF-8
  manifest makes UTF-8 (`.claude/CLAUDE.md`, Domain invariants)

### Time Handling

- Uses IRIG time format and standard time structures
- Time conversions between different formats (DOY/HMS ↔ uint64)
- UTC timezone enforced in Chapter10Reader and FrameProcessor constructors

### AGC / Stream Processing

- Central processing function: `FrameProcessor::process()`, driven per stream from its `ProcessingParams` + `FrameSetup`
- Drains the stream's `PacketQueue` (fed by the single `Ch10PacketReader`) rather than opening the file itself
- Accumulates results into an in-memory `ProcessedStreamData` (lock %, accumulated missed frames, and SNR channel series) — there is no CSV-on-disk intermediate
- One `FrameProcessor` per stream runs on its own `std::thread` (no event loop); `PipelineRunner` owns the lifecycle - the queues, the reader and the threads - and joins them all before it reports the end
- Reports log, error, and completion through `ProcessingEvents`; the ABI's run relays them to the host's observer, and the app posts them to the UI thread, in order (`SessionLog`)

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
- **What the code really measures** (`.claude/CLAUDE.md`, Domain invariants): lock % is a **bit-span** quantity, the
  bits spanned by locked frames over the bits in the window, not `time × bitrate`; off-phase syncs are rejected while
  locked; missed frames are extrapolated across a gap only when a window has no bits at all.

## Common Development Tasks

### Adding a Control or a Dialog

1. Put the rule in a view model (`dotnet/TmDq.App/ViewModels`), where a plain `[Fact]` can test it without a window.
2. Bind it from the view's XAML; keep the code-behind to what only a control can do (focus, pointer gestures, popups).
3. Test the window on the headless platform (`[AvaloniaFact]`), never a control in a plain `[Fact]`, and never await a
   modal dialog in a test. The `verify-ui-change` skill covers measuring and rendering.

### Changing What the Core Does

1. Pin the current behaviour with a doctest case first, then change it.
2. A new function a host needs goes through the ABI (`native/tmdq_core/tmdq_abi.h`, a new revision) and its binding in
   `dotnet/TmDq.Core`, with a native case in `test_abi.cpp` and a managed test.
3. Mutation-check the change (`scripts\mutate.ps1`), and after a change to processing, the plot's series or the CSV
   writer, run the soak (`scripts\run_soak.ps1`).

### Adding a New Per-Stream Setting

The `add-stream-setting` skill is the checklist. In short:

1. Add the field to the app's stream setup (`Streams/StreamSetup`, its defaults and rules) and to `StreamConfig`
   (`dotnet/TmDq.Core/RunTypes.cs`), and to the core's job input it travels in, through the ABI.
2. Surface it in the setup dialog's view model, in Apply to all if it should propagate, and in the template file.
3. If it must round-trip to a TOML file, add it to the core's format (`framesyncfile` or the receiver-parameters file,
   through `TomlDocument`), respecting the TOML boundary in `.claude/CLAUDE.md`.
4. Carry it into `ProcessingParams` (and `FrameSetup` if it affects the word map) in `StreamJobBuilder`, and through
   `applyAcquisition` if it affects raw values (the calibration extractor must see it too).
5. Consume it in `FrameProcessor::process()`.

## Debugging

### Common Issues

1. **A build tool is not found** (`msbuild`, `mt`, `dumpbin`): the scripts dot-source `scripts\env.ps1` when they are
   missing, which imports the Visual Studio 2022 environment; run them from PowerShell, not a bare `cmd`.
2. **`dotnet build` fails on `Platform=x64`**: `vcvars` sets it, and a .NET build then looks for an x64 configuration
   that does not exist. The scripts clear it; clear it yourself in a shell that ran `env.ps1`.
3. **A test sees an old `tmdq_core.dll`**: the managed and app tests load `native\out\<config>\tmdq_core.dll` (or
   `TMDQ_CORE_DLL`), so rebuild the core after changing it.
4. **A deleted or renamed header still "builds"**: an incremental build relinks stale objects; only `-Rebuild`
   verifies it.

### Build Warnings

- Every build is warning-free: the core's (`build_core_dll.ps1` and `test_native.ps1` fail on one) and the .NET
  projects' (warnings are errors). Fix a new one before committing.

## Testing

Three suites, each run by its own script; the baselines are in `.claude/CLAUDE.md`, and the `build-and-test` and
`write-test` skills say how to run and extend them:

- **The native core** (doctest, `native/tmdq_core_tests`): `scripts\test_native.ps1`, filtered with
  `-tc="<name>*"` while iterating.
- **The C# library** (xunit v3, `dotnet/TmDq.Core.Tests`): `scripts\test_dotnet.ps1`.
- **The application** (xunit v3 on `Avalonia.Headless.XUnit`, `dotnet/TmDq.App.Tests`): `scripts\test_app.ps1`.

The suites read the committed recordings under `tests/data/test files` and the fixtures under `tests/fixtures`. Two
more checks run by hand:

- **The side-by-side soak** (`scripts\run_soak.ps1`, `SoakTests`): 34 runs over every recording on the development
  machine, each CSV held byte for byte to what v2.14.2 exported (`Soak/soak.json`). Most recordings are not committed,
  so CI never runs it. `docs/records/soak.md` is the record of how it was set up.
- **Mutation checks** (`scripts\mutate.ps1`, in a worktree): every behaviour change is checked against single-defect
  mutants, and a surviving mutant is a test to write or a line to remove.

A new native test file goes in `native/tmdq_core_tests/tmdq_core_tests.vcxproj` (`NativeProjectTests` fails until it
is); a new .NET test needs nothing but its class.

### Continuous Integration

`.github/workflows/ci.yml` is one job, `ci`, on GitHub-hosted `windows-2022` (Visual Studio 2022, as releases are
built, which the job checks), on every push to `main` and every PR: the release consistency check, a check that the
machine has no Qt, the core built Debug and Release, then the native, managed and app suites. The workflow's own
constraints (ASCII-only, Windows PowerShell 5.1) are in its header comment. The self-hosted runner CI used until plan
step 6.4 is retired; [`docs/ci_runner.md`](ci_runner.md) says how to take it off the machine.

### Test Suites

**The native core** (`native/tmdq_core_tests`, one `test_<suite>.cpp` each):

- **Chapter10Reader and ChannelData** (`test_chapter10reader`) - the five committed recordings' channel ids, names and
  TMATS data rates; every way a load fails, with its message; and edited copies of `step_cal` for what the recordings do
  not reach (the enabled flag, the type codes, the name priority, a non-ASCII name, a second time channel).
- **Ch10PacketReader** (`test_ch10packetreader`) - the single pass over `step_cal`: every packet once with one end
  marker, progress, abort, files cut or damaged at each kind of place, routing and shared payloads, the attributes
  `prepare()` resolves, and serialised setup across threads.
- **PacketQueue** (`test_packetqueue`) - order, the bound, blocking on either side, `close()`, draining, and 20,000
  items through a small queue under a watchdog.
- **FrameProcessor** (`test_frameprocessor`) - real recordings in both modes, and synthetic streams at one second per
  bit with every value worked by hand: the windows on both clocks, a bounded window, lost lock and missed frames, gap
  extrapolation, the word map and calibration, the descrambler against an independent randomizer, inversion, and the
  exact text of everything it reports.
- **PipelineRunner** (`test_pipelinerunner`) - a run's results and end on `step_cal`, a failing stream beside good ones,
  refusals, cancel at chosen moments (a thread parked on a log line), runs one after another, and destruction while
  running. It also holds **the throughput guard**, opt-in (`TMDQA_RUN_HEAVY_BENCH`) and run at release by
  `scripts\run_throughput_bench.ps1` on the full PRN recording: four streams in parallel must cost about what the
  slowest costs alone, asserted as ratios, never milliseconds.
- **EventReporter** (`test_eventreporter`) and **IntervalGate** (`test_intervalgate`) - the null-safe forwarder every
  source reports through, and the progress throttle, with injected time.
- **FrameSetup** (`test_framesetup`) - the word map, the linear calibration for every range and polarity, channel and
  receiver names, the receiver-parameters file (both key spellings, round trips, the exact file written, the default
  map), and which step-calibration profiles attach.
- **FrameSync** (`test_framesync`) - hex parsing, the pattern and mask decode, `applyAcquisition` setting every
  raw-affecting field and no other, and the full sync mask.
- **StreamJob** (`test_streamjob`) - one stream's settings into a job: validation and its messages, the word-map
  sources, the calibration and its profiles.
- **StepDetector** (`test_stepdetector`) - the step file (against the parser it replaced, over 4,000 generated files),
  plateau detection on clean, noisy, inverted, split, railed and saturated sweeps, and the interpolation and its clamps.
- **CalibrationExtractor** and **CalibrationRequest** (`test_calibrationextractor`, `test_calibrationrequest`) - whole
  extractions of `step_cal` and `rnrz-l_testfile` against recorded numbers, clips, the report's exact text in every
  scenario, the lifecycle and cancel; and the request built from the dialog's fields with its refusals.
- **TomlDocument and textfile** (`test_tomldocument`, `test_textfile`) - the TOML subset (sections in order, arrays of
  tables, comments, escapes, the number and boolean rules, the exact bytes written) and the text encodings.
- **FrameSyncFile** (`test_framesyncfile`) - the pattern file both ways, Randomized only when the file has the key.
- **CsvSeries and SeriesColumnSchema** (`test_csvseries`, `test_seriescolumnschema`) - the CSV both ways and its clock
  stamp; every column header formatted and parsed back, source qualifiers, edge cases, and 40,000 generated headers.
- **SeriesBuild** (`test_seriesbuild`) - a stream's series, the shared time base across sources, imported streams'
  order.
- **ChartGeometry** (`test_chartgeometry`) - the envelope, band, transforms and ticks, against the code they replaced
  over thousands of generated series and views, and worked by hand.
- **TemplateMatcher** (`test_templatematcher`) - exact-set matching and its missing and extra lists.
- **TextFormat** (`test_textformat`) - fixed-point formatting with ties rounded away from zero, as v2.14 printed it.
- **Constants** (`test_constants`) - every constant something outside the code depends on, read against the shipped
  files that carry the same value.
- **Abi** (`test_abi`) - the C ABI compiled into the test executable and called as a host would: every struct's size
  and offsets, versioning both ways, each function against the core called directly (runs, calibration, the formats,
  series construction, the receiver-parameters file), the callbacks' order and threads, cancel and free in flight, bad
  arguments and the last error.

**The C# library** (`dotnet/TmDq.Core.Tests`): the struct layout against the native side (`LayoutTests`); the native
projects' source lists against the files on disk (`NativeProjectTests`); the recordings' channels (`FileTests`); runs of
the committed recordings against numbers v2.14.2 produced, every setting reaching the job, failures, observers, cancel
and lifetimes (`RunTests`); calibration against the core's extractor (`CalibrationTests`); the file formats byte for
byte against the core (`FormatsTests`); loading the DLL (`LibraryTests`, `IsolatedLoadTests` in a child process, the
suite's one skip); `ChartGeometryTests`, `ReceiverParamsTests`, `SeriesTests`, `ArenaTests`; and `TempFilesTests`, the
helper every test cleans up and reads back through, which waits out a virus scanner's moment's hold.

**The application** (`dotnet/TmDq.App.Tests`, on the headless platform with Skia drawing):

- **The run:** `WalkingSkeletonTests` (Open to the drawn chart on the PRN sample, failures, the smoke report),
  `ProcessingTests` (the log, the progress dialog and its Cancel, the run's lines in order), `SeveralStreamsTests`.
- **Configure Streams:** `StreamRulesTests` and `StreamFilesTests` (the rules and files without a window),
  `StreamSetupViewModelTests`, `ConfigureStreamsTests`, `StreamWindowsTests`; `CalibrationDialogTests` (Extract
  Calibration and Apply Cal).
- **The plot:** `PlotTests` (series, axes, view mode, the time window and zoom, the readout, the chart's drawing and
  gestures), `AroundTheChartTests` (legend, chips, menu, keys, Customize View, prompts).
- **Files:** `ExportImportTests` (seven runs' CSVs byte for byte against v2.14.2's, the image pixel by pixel, Import
  CSV), `TemplateTests` and `TemplateBatchTests` (v2.x's templates byte for byte, the batch and its exports).
- **The shell:** `ShellTests` (title bar, menu, Recent Files, keys, drag and drop, themes, Help), `PreferencesTests`
  (the preferences file and v2.x's INI import), `MainWindowTests`.
- **The manual:** `ManualFiguresTests` renders every figure and checks each caption's claims.
- **The build and the release:** `VersionTests`, `VersionBuildTests`, `ManifestTests`, `ThirdPartyNoticesTests`,
  `PackageLicenceTests`, `LicenceBuildTests`.
- **The soak:** `SoakTests`, explicit, run by `scripts\run_soak.ps1` (above).

## Future Feature Candidates

All identified feature items have been implemented. New work starts as a user story here, or as a note in
`docs/future_plans/`.

## Additional Resources

- [Avalonia documentation](https://docs.avaloniaui.net/)
- [IRIG 106 Chapter 10 Standard](https://www.irig106.org/)
