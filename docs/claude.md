# tmDataQualityAnalyzer.md - AI Assistant Guide

This file provides context and guidelines for AI assistants working on the tmDataQualityAnalyzer project.

## Version Information

- **Qt Version**: 6.10.2 (minimum: Qt 6.0.0)
- **MinGW Version**: 13.1.0 (minimum: GCC/MinGW 7.0)
- **C++ Standard**: C++17 (required — `inline constexpr` used throughout constants.h)
- **Project Version**: 2.6.0 — defined once in the `AppVersion` struct in `include/constants.h`; qmake parses it from that header and propagates it to the Qt `VERSION` and the Windows resource file (`version_autogen.h`), so no other file carries a duplicate version literal

## User Stories

### US1.0: View SNR signal data from IRIG 106 formatted PCM telemetry streams contained in .ch10 files
**As a** telemetry engineer or data analyst
**I want to** to view SNR signal data (in dB) derived from telemetry receiver AGC samples (raw integer energy values from 0 to 65,535) included in PCM data streams within IRIG 106 formatted .ch10 files
**So that** I can view the SNR signal data for one or more receiver channels versus time.

**Acceptance Criteria:**
- [x] The user can view the SNR signal data (right hand y-axis) versus time (x-axis) in a plot window
- [x] The user can select which receiver/channels SNR signal data to view in the plot window
- [x] The user can select the time window to view the SNR signal data in the plot window
- [x] The user can select one of the following time periods to average SNR signal data over: 100 Hz, 10 Hz, 1 Hz
- [x] Samples are converted from raw integer energy values to Volts (V) using the voltage range and polarity, and then to decimal dB values using the scale (dB/V)
- [x] A progress bar updates as the application is processing the SNR signal data

### US1.1: Define the key parameters required to process the receiver AGC sample data
**As a** telemetry engineer or data analyst
**I want to** define the rules and key parameters (frame sync pattern, frame length, framesync mask, PCM code format, etc.) required to decommutate and process telemetry streams containing receiver AGC sample data
**So that** these parameters can be applied per-stream in the processing dialog window (defined in US5.0) and saved/loaded from configuration files (defined in US3.x).

**Acceptance Criteria:**
- [x] The user can define the frame sync pattern and frame length for the telemetry stream of interest
- [x] The application accepts frame sync pattern sizes up to 64 bits in length
- [x] The application accepts frame sync masks up to 64 bits in length
- [x] The application accepts frame lengths up to 65,536 bits in length
- [x] The user can select from one of the following PCM code formats: NRZ-L, RNRZ-L
- [x] The user can define the number of receivers and the number of channels per receiver in the telemetry stream of interest
- [x] The user can define the scale of the receiver AGC sample data in dB/V
- [x] The user can define the voltage range of the receiver AGC sample from one of the following values: 0-5V, 0-10V, +/-5V, +/-10V
- [x] The user can define the polarity of the receiver AGC sample data: positive or negative

### US1.2: Export user specified SNR signal data with timestamps to a file
**As an** As a telemetry engineer or data analyst
**I want to** to export SNR signal data of interest to a CSV file.
**So that** I can import the file into 3rd party applications such as Excel and Matlab for further analysis (the export settings and file location are configured via the export dialog window defined in US8.0).

**Acceptance Criteria:**
- [x] The current SNR signal data displayed in the plot window is exported to a CSV file
- [x] The CSV file includes at least the following columns: time stamps, receiver/channel identifiers, and SNR signal data
- [x] The CSV file includes headers describing the content of each column

### US1.3: Export user specified SNR signal data to an image file
**As an** As a telemetry engineer or data analyst
**I want to** to export SNR signal data to an image file (e.g. png, pdf, etc.) from the plot window.
**So that** I can import the image file into 3rd party applications such as PowerPoint to create presentations (the export format and location are configured via the export dialog window defined in US8.0).

**Acceptance Criteria:**
- [x] The current SNR signal data displayed in the plot window is exported to an image file
- [x] Images are exported to one of the following file formats as selected by the user: svg, png, pdf

### US2.0: View the framesync lock statistics for IRIG 106 formatted PCM telemetry streams contained in .ch10 files
**As a** telemetry engineer or data analyst
**I want to** to view the framesync lock statistics (percentage of time the telemetry stream is in sync) for PCM data streams in IRIG 106 formatted .ch10 files
**So that** I can view the framesync lock statistics for one or more telemetry streams versus time.

**Acceptance Criteria:**
- [x] The user can view the framesync lock statistics (left hand y-axis) versus time (x-axis) in a plot window
- [x] The user can select which telemetry stream(s) framesync lock statistics to view in the plot window
- [x] The user can select the time window to view the framesync lock statistics in the plot window
- [x] The user can select framesync lock statistics for the following sample windows: 10ms, 100ms, and 1s
- [x] A progress bar updates as the application is processing the framesync lock statistics

### US2.1: View Accumulated Missed Frames Over Time
**As a** telemetry data analyst
**I want to** quantify and plot the accumulated missed frames over time for my PCM streams
**So that** I can quantitatively evaluate how frame sync errors are accumulating over time

**Acceptance Criteria:**
- [x] The user can view the accumulated missed frames (left hand y-axis) versus time (x-axis) in a plot window
- [x] The user can switch the left-axis view between framesync lock (%) and accumulated missed frames modes
- [x] The user can select which telemetry stream's accumulated missed frames to view in the plot window
- [x] Missed frames are accumulated per telemetry stream against that stream's own frame parameters

  - **Scope:** A "missed frame" is a discrete loss-of-lock event — while in lock, the stream ran past the expected minor-frame boundary (`bits_in_frame`) without a sync match. The metric is a cumulative count of these events per stream, counted only within the selected `[start, stop]` processing window, and only ever increases. It is NOT a bit-level (Hamming/BER) error count. The user-facing label is "Accumulated Missed Frames" (`PlotConstants::kMissedFramesAxisLabel`); this is the canonical term across the UI, release notes, and code.

### US2.2: Define the key parameters required to process the framesync lock statistics and frame sync error accumulation
**As a** telemetry engineer or data analyst
**I want to** define the rules and key parameters (frame sync pattern, frame length, framesync mask, PCM code format, etc.) required to properly decommutate and calculate frame lock statistics and frame sync error accumulation for telemetry streams
**So that** these parameters can be applied per-stream in the processing dialog window (defined in US5.0) and saved/loaded from configuration files (defined in US4.0).

**Acceptance Criteria:**
- [x] The user can define the frame sync pattern and frame length for each telemetry stream of interest
- [x] The application accepts frame sync pattern sizes up to 64 bits in length
- [x] The application accepts frame sync masks up to 64 bits in length
- [x] The application accepts frame lengths up to 65,536 bits in length
- [x] The user can select from one of the following PCM code formats: NRZ-L, RNRZ-L
- [x] The user can caculate frame sync lock statitics for up to 8 telemetry streams from the .ch10 file

### US2.3: Export user specified framesync lock statistics and accumulated missed frames with timestamps to a file
**As an** As a telemetry engineer or data analyst
**I want to** to export framesync lock statistics and accumulated missed frames of interest to a CSV file.
**So that** I can import the file into 3rd party applications such as Excel and Matlab for further analysis (the export settings and file location are configured via the export dialog window defined in US8.0).

**Acceptance Criteria:**
- [x] The current framesync lock statistics and accumulated missed frames displayed in the plot window are exported to a CSV file
- [x] The CSV file includes at least the following columns: time stamps, stream identifier, framesync lock statistics and accumulated missed frames
- [x] The CSV file includes headers describing the content of each column

### US2.4: Export user specified framesync lock statistics and accumulated missed frames to an image file
**As an** As a telemetry engineer or data analyst
**I want to** to export framesync lock statistics and accumulated missed frames to an image file (e.g. png, pdf, etc.) from the plot window.
**So that** I can import the image file into 3rd party applications such as PowerPoint to create presentations (the export format and location are configured via the export dialog window defined in US8.0).

**Acceptance Criteria:**
- [x] The current framesync lock statistics and accumulated missed frames displayed in the plot window are exported to an image file
- [x] Images are exported to one of the following file formats as selected by the user: svg, png, pdf

### US2.5: Apply one stream's configuration to all matching streams
**As a** telemetry engineer or data analyst
**I want to** configure one telemetry stream in the per-stream setup dialog and apply those settings to every other selected stream of the same mode in a single action
**So that** I don't have to re-enter identical parameters across many streams when a file contains several streams with the same format.

**Acceptance Criteria:**
- [x] The Frame Sync Lock and Receiver SNR setup dialogs each provide an "Apply to all <mode> streams" toggle next to the OK/Cancel buttons
- [x] When enabled and the user clicks OK, the entered settings are copied to every stream that is selected for processing and shares the same mode
- [x] Streams of a different mode are left unchanged
- [x] Each affected stream is marked configured/ready and its readiness indicator updates

  - **Scope:** Orthogonal to US2.1 (frame sync error accumulation); split out as its own story. Fan-out copies the same fields written by the gear dialog today (see `openGearDialog` in `streamconfigdialog.cpp`); it does not widen the Frame Sync Load/Save TOML boundary defined in US5.0.

### US3.0: Recall/store the default SNR signal data parameters from/to configuration files
**As an** As a telemetry engineer or data analyst
**I want to** to recall/store the parameters used to process SNR data in a configuration file
**So that** I don't have to re-enter the parameters every time I open the application.

**Acceptance Criteria:**
- [x] Configuration files include the default parameters used to process SNR data
- [x] User is able to save default parameters to a configuration file
- [x] User is able to recall default parameters from a configuration file
- [x] The configuration file is stored in a user specified location
- [x] The configuration file is stored in TOML format

### US3.1: Edit default SNR signal data parameters in configuration files via a dialog window
**As an** As a telemetry engineer or data analyst 
**I want to** to be able to edit the default key parameters used to process SNR signal data streams via a dialog window
**So that** I can update the default key parameters to meet the requirements of different telemetry streams formats.

**Acceptance Criteria:**
- [x] The dialog window includes the following default parameters used to process SNR signal data streams: 
	- number of receivers
	- number of channels per receiver
	- scale
	- voltage range
	- slope
- [x] The dialog window includes a "Reset" button to reset the key parameters to their default values
- [x] The dialog window includes a "Save" button to save the key parameters to the configuration file
- [x] The dialog window includes a "Cancel" button to cancel the operation

### US3.2: Non-Linear Receiver SNR Step Calibration
**As a** telemetry engineer or data analyst
**I want to** apply a non-linear step calibration to Receiver SNR streams using a Calibration CH10 file and a TOML step configuration
**So that** I can accurately groom out receiver non-linearities and plot true SNR values instead of relying on a simple linear slope/offset.

**Acceptance Criteria:**
- [x] The user can enable non-linear step calibration for a specific Receiver SNR stream in the per-stream setup dialog (via the "Extract Calibration…" action).
- [x] The user can load a TOML file defining the expected step values (in dB).
- [x] The user can load a Calibration CH10 file containing the recorded step data.
- [x] The application automatically extracts "ideal" steps from the CAL file.
- [x] The application evaluates step extraction success independently for each receiver channel (up to 48).
- [x] Channels that successfully map the expected number of steps are assigned a non-linear calibration profile for the session.
- [x] Channels that fail step extraction (e.g., due to noise or no data) fall back to the standard linear (slope/offset) calibration.
- [x] A summary message box informs the user which channels succeeded and which fell back.
- [x] During main data processing, the application applies the non-linear profile using piece-wise linear interpolation between steps, and clamps to the nearest end-step dB for out-of-range values (so receivers driven past the calibrated range read the ceiling/floor rather than diverging).

  - **Status (v2.2.5): COMPLETE.** Step selection is polarity-agnostic and robust to real recordings: it takes the first maximal monotonic plateau run and keeps its last `expected` plateaus, which excludes a signal-generator turn-on transient (leading) and the optional operator down-ramp (trailing) for both normal and inverted-polarity receivers — fixing the calibrated-staircase time skew. The extraction sample period is frame-rate-adaptive (100 ms floor) for fast frames, and the Apply Cal dialog adds optional **Clip Start / Clip End** controls so operators can trim leading/trailing seconds before detection. Out-of-range raw values clamp (not extrapolate) to the nearest end-step dB.

### US4.0: Recall/store framesync pattern and frame length parameters from/to configuration files
**As an** As a telemetry engineer or data analyst
**I want to** to recall/store the parameters used to process framesync pattern and frame length in a configuration file
**So that** I don't have to re-enter the parameters every time I open the application.

**Acceptance Criteria:**
- [x] Configuration files include the framesync pattern and frame length
- [x] User is able to save the framesync pattern and frame length to a configuration file
- [x] User is able to recall the framesync pattern and frame length from a configuration file
- [x] The configuration file is stored in a user specified location
- [x] The configuration file is stored in TOML format

### US5.0: Determine which streams are processed and the parameters to use for each stream
**As an** As a telemetry engineer or data analyst 
**I want to** select streams to process and configure their parameters in a single configuration/processing dialog window upon opening a Ch10 file
**So that** I can apply the specific parameters defined in US1.1 and US2.1 to each individual telemetry stream before starting the decommutation process.

**Acceptance Criteria:**
- [x] A dialog window is presented when the user opens a new ch10 file
- [x] The dialog window lists all streams identified in the ch10 file
- [x] The dialog window allows the user to select the streams to process for SNR data and/or framesync lock statistics
- [x] The dialog window allows the user to specify the framesync pattern, frame length, framesync mask, and bitrate parameters for each telemetry stream
- [x] The dialog window allows the user to recall/restore previous frame parameters for each stream using configuration files
  - **Scope:** Frame-sync Load/Save round-trips ONLY frame sync pattern, sync mask, and words/frame (bits per frame). Randomized, Data Rate, and Sample Rate are per-session operator inputs and are intentionally excluded — this is the meaning of the separator line in the setup-dialog wireframe. Keep `loadFrameSyncToml`/`saveFrameSyncToml` symmetric and do not widen them past that boundary.
- [x] The dialog window allows the user to specify the SNR signal data parameters to be used for SNR signal data streams
- [x] The dialog window allows the user to recall/restore previous SNR signal data parameters using configuration files
- [x] The dialog window closes when the user clicks the "Process" button
- [x] The dialog window closes when the user clicks the "Cancel" button
- [x] Once the user selects "Process", the dialog window closes and the main window is updated with the processed data

### US6.0: Configure plot window navigation and information
**As an** As a telemetry engineer or data analyst 
**I want to** Configure how the processed framesync lock statistics and SNR sample data is displayed in a plot window 
**So that** I can quickly analyze data in the plot window

**Acceptance Criteria:**
- [x] Dockable plot window
- [x] User specified plot title
- [x] Left Y axis is labeled and states the unit of measure; average framesync lock (%)
- [x] Right Y axis is labeled and states the unit of measure; SNR (dB)
- [x] Bottom X axis is labeled and states the unit of measure; time (seconds)
- [x] Auto set Y axis to min/max values
- [x] Auto set X axis to the max time span in the processed csv file
- [x] Controls to set a time window; auto zoom and move the x-axis so only this time window is visible
- [x] Contol to overide Y axis min/max
- [x] Mouse wheel zooms the x-axis
- [x] Mouse click and hold pans the x-axis
- [x] Select/Unselect which framesync lock statitics and receiver channel data is visible
- [x] A movable on-plot legend overlays the chart interior (not a separate panel below it), showing a line-swatch and label for each visible series. The user can click-drag it anywhere inside the plot area to reposition it so it doesn't obscure data of interest; it stays clamped within the plot. It defaults to the top-right corner and resets there each session. The legend background is translucent so data behind it stays faintly visible, and its height is capped with a vertical scrollbar so very dense plots (e.g. 48+ Receiver SNR series) stay compact; a permanent right-side gutter keeps the scrollbar from overlapping row text. Receiver SNR legend rows show a short "CH\<id\> \<L/R/C\>_RCVR\<n\>" label rather than the full TMATS-derived stream title (which can be long) — the full name is still used for CSV headers, renaming, and hover tooltips. It is composited into exported PNG and SVG images at its placed position. Per-series color and name are edited from the Customize Plot Series dialog
- [x] Auto set plot colors by default; framesync lock curves use purple/blue/green primaries (one per stream) and SNR curves use red/orange/yellow primaries (one per receiver); additional streams, receivers, and channels are progressively lighter shades of their primary so related series stay grouped
- [x] Framesync lock curves are highlighted; SNR curves are not

### US7.0: Import a previously exported CSV file to visualize historical data
**As a** telemetry engineer or data analyst
**I want to** open a CSV file that this application exported in the past (US1.2 / US2.3) and load it directly into the plot window
**So that** I can review and visualize old data sets without re-processing the original .ch10 file.

**Acceptance Criteria:**
- [x] The user can open a previously exported CSV file via the toolbar **Import** button (left of Export; CSV-only dialog, disabled while processing), drag-and-drop of a `.csv`, and the Recent Files list. (File > Open and the toolbar Open button are Chapter-10-only; Import is the CSV entry point.)
- [x] Importing a CSV bypasses the Configure Streams dialog and the processing pipeline — the file is parsed straight into the plot
- [x] The importer accepts the application's own export format: a `Time (DOY:HH:MM:SS.mmm)` first column followed by one column per series
- [x] Frame Sync Lock (`Lock (%)` suffix), Accumulated Missed Frames (`Accumulated Missed Frames` suffix), and Receiver SNR (`_RCVR<N>`) series are recognized by their column headers and routed to the correct axis, metric type, and color
- [x] Imported series populate the plot, the on-plot legend, and the Customize Plot Series dialog the same way processed data does (axis auto-ranging, time window, visibility toggles all apply)
- [x] Imported data can be re-exported (CSV/image) and the recent-files list records the imported CSV
- [x] A CSV that is not in the application's export format (e.g. the legacy `Day,Time,…` layout, a wrong/missing header, or a file with no parseable data rows) is rejected with a clear error in the log rather than producing garbage series
- [x] Malformed or short data rows are skipped, and the user is informed how many rows were skipped

  - **Scope:** Reuses the existing Model/ViewModel import path (`CsvSeriesParser::parse` + `PlotViewModel::loadCsvFile` / `loadCsvFileAsync`), which is the matched partner of `exportCsv`; this story is primarily the View-layer wiring (menu/drag-drop entry, format routing on file open) plus user-facing error reporting. It does not add a CSV import dialog with column mapping, and it does not attempt to import arbitrary third-party CSVs — only files this application produced.

### US8.0: Export file settings
**As an** As a telemetry engineer or data analyst 
**I want to** configure export preferences via a unified export dialog window
**So that** I can specify the export types (CSV and/or images), file names, and directories for the data exports requested in US1.2, US1.3, US2.2, and US2.3 in a single action.

**Acceptance Criteria:**
- [x] A dialog window is presented to the user when the user clicks the export button
- [x] The dialog window allows the user to determine what data is exported; SNR sample data and framesync lock statistic data, plot images, or the log window contents
- [x] The dialog window allows the user to specify the filename and location for the exported data files
- [x] The dialog window allows the user to specify the filename and location for the exported plot images
- [x] The dialog window allows the user to export the log window contents to a text file, with its own filename and location

### US9.0: Error Checking
**As an** As a telemetry engineer or data analyst 
**I want to** ensure the values I enter into the application are valid
**So that** I can avoid errors and ensure the data I export is accurate

**Acceptance Criteria:**
- [x] Ensure all input fields are validated
- [x] Error messages are displayed to the user when invalid values are entered
- [x] Application ensures frame sync pattern only contains hexadecimal values
- [x] Application ensures frame sync pattern is no larger than the user specified frame length

### US10.0: Application Installer
**As a** developer
**I want to** create an application installer
**So that** I can quickly deploy the software/updates to users with all the necessary folders and settings files

**Acceptance Criteria:**
- [x] Signed application
- [x] Install to "Program Files" and to a user-selected directory for users without admin privileges
- [x] Installer shows install progress
- [x] Installer should not overwrite TOML files; if new fields are in the TOML file, alert the user that a new TOML file was saved as "new_x.toml" — try to use as many parameters from the old default TOML file as possible in the new TOML file

## Version History

### v2.6.0 — Import Exported CSV Files (US7.0)
- New: the application can open a CSV it previously exported and load it straight
  into the plot, so old data sets can be visualized without re-processing the
  source `.ch10` file (US7.0).
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
- Toolbar Export/Import icons are new Fluent bracket-and-arrow SVGs with
  per-theme variants (`export-{dark,light}.svg`, `import-{dark,light}.svg`),
  swapped by `MainView::applyToolbarIconsForTheme()` on startup and theme toggle.
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

### v2.5.2 — Stream Config Dialog Polish
- Configure Streams dialog table background changed from near-black (#202020) to match the dialog background (#2C2C2C) in the dark theme QSS, removing the black-background appearance behind table row controls.
- Added an "All" toggle (QCheckBox) to the bottom-left of the Configure Streams dialog; toggling it on/off sets all stream Process checkboxes in one action.

### v2.5.1 — Frame Sync UX Polish and Resolution Fix
- Frame Sync Lock and Accumulated Missed Frames plot series names now use the bare stream label only (no metric suffix). `renameSeries` propagates a rename to the sibling series (same `streamLabel`, other frame-sync metric) so a custom name survives mode switching.
- Switching between Lock % and Missed Frames modes now clears `m_left_y_max_user_set` and emits `axisRangeChanged()` in `setLockAxisView`, resetting the left Y-axis to its automatic range.
- Main window opens at 1920×1080 (`UIConstants::kInitialWindowWidth/Height`); `adjustSize()` removed.
- Resolution fix: `kPlotDockMinHeight` removed; only the chart (`m_plot`) carries a 250 px floor (`PlotConstants::kPlotMinChartHeight`), so the bottom controls (Start, Stop, L/R Max) remain visible when maximized at high DPI.

### v2.5.0 — Cleaner Stream Names, Plot Axis Overrides, Dialog Polish
- Stream labels carry the **bare channel name** (no `<id> - ` TMATS prefix): drives
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
- US3.2 (Non-Linear Receiver SNR Step Calibration) is **complete and field-validated**.
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
- Non-linear step calibration for Receiver SNR (US3.2): an "Extract Calibration…"
  action in the Receiver SNR setup dialog builds a per-channel raw→dB profile from
  a calibration Chapter 10 file and a `[[Step]]` TOML, applied via piecewise-linear
  interpolation/extrapolation during processing; channels that fail fall back to
  linear math. New `StepDetector`, `CalibrationExtractor`, and
  `interpolateCalibration()`; session-only, keyed by word index. New
  `TestStepDetector` suite.
- New on-plot legend panel below the chart: a fixed-height, vertically
  scrolling 4-column grid of color swatch + series-name pairs that lists every
  visible series and updates as series are toggled or the left-axis view
  changes (US6.0)
- PNG export now composites the legend panel beneath the chart into a single
  image (US2.4); SVG/PDF export the chart as before
- Export dialog gains an "Export Log (Text)" option that writes the log window
  contents to a `.txt` file; the dialog now lists Image first and defaults to
  Image export (US8.0)
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
  "Accumulated Missed Frames" via a toolbar button (US2.1)
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
- US2.5 (apply one stream's configuration to all matching streams) tracked as a
  separate story; US2.2–2.4 terminology standardized on "missed frames"

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
- User stories US1.0–US10.0 complete
- Frame Sync Lock analysis with off-phase rejection and bit-span lock percentage
- Receiver SNR / AGC analysis with voltage-to-dB calibration
- Multi-stream concurrent processing with single-pass I/O (one reader + per-stream queues)
- Per-stream stream configuration dialog with TOML load/save
- Plot window with dual Y-axes, mouse zoom/pan, series visibility toggles, and light/dark theme
- CSV and image (SVG/PNG/PDF) export via unified export dialog
- Inno Setup installer (admin + non-admin) and portable ZIP packaging
- Automated unit tests (Qt Test framework)

### v0.8.0 — Internal Milestone
- User stories US1.0–US6.0 complete
- Initial automated unit test suite

## Tech Stack
- **Language**: C++
- **Framework**: Qt
- **Build System**: qmake
- **Platform**: Windows (primary target)
- **External Libraries**: irig106utils (embedded C library), QCustomPlot (embedded charting library, `lib/qcustomplot/`)

## ⚠️ CRITICAL: Protected Files - DO NOT MODIFY

The following files are third-party library code and **MUST NOT be modified** under any circumstances:

### Protected Source Files
- `lib/irig106/src/irig106*.c` - All IRIG 106 C source files
- `lib/irig106/src/i106_*.c` - All i106 prefixed C source files
- `lib/qcustomplot/qcustomplot.cpp` - QCustomPlot charting library

### Protected Header Files
- `lib/irig106/include/irig106*.h` - All IRIG 106 header files
- `lib/irig106/include/i106_*.h` - All i106 prefixed header files
- `lib/irig106/include/config.h` - IRIG 106 configuration
- `lib/qcustomplot/qcustomplot.h` - QCustomPlot charting library header

**File Patterns to Exclude**: Any file containing `i106` or `irig106` in its name; any file in `lib/qcustomplot/`

**Reason**: These files are from external libraries ([irig106utils](https://github.com/atac/irig106utils), [QCustomPlot](https://www.qcustomplot.com/)) and are maintained separately. Modifications would:
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
the plot as each stream finishes. There is **no batch / multi-file mode**.

### Core Components

#### View

1. **MainView** (`src/view/mainview.cpp`, `include/view/mainview.h`)
   - Thin GUI layer; creates and lays out all Qt widgets and the toolbar/menus
   - Binds to MainViewModel Q_PROPERTYs and connects signals/slots; contains no business logic
   - `logError()` / `logWarning()` / `logSuccess()` append colored HTML entries (red / #DAA520 / green) to the log window
   - Errors/warnings shown inline in the log (`QTextBrowser`, clickable links, persistent, auto-scroll); QMessageBox reserved for About and the calibration summary
   - Status bar shows the file metadata summary (filename, size, channel counts, time range)
   - Recent Files submenu under File menu with QSettings persistence
   - Drag-and-drop and File > Open of a single `.ch10` file (launches the StreamConfigDialog) or a previously exported `.csv` file (US7.0: routed by `openPath()`/`importCsv()` straight into the plot via `PlotViewModel::loadCsvFileAsync`, bypassing the dialog and processing pipeline)
   - Cancel toolbar button visible only during processing
   - Log window in a bottom QDockWidget; plot in a right QDockWidget (PlotWidget); View menu toggles each

2. **StreamConfigDialog** (`src/view/streamconfigdialog.cpp`, `include/view/streamconfigdialog.h`)
   - Modal "Configure Streams" dialog listing one row per PCM channel in the file
   - Five columns: Process, Channel, Mode, Configure (gear), Ready (status icon); a Time Channel combo at top
   - The gear opens a per-stream sub-dialog — Frame Sync Lock setup or Receiver SNR setup — keyed to the row's Mode
   - Sub-dialogs Load/Save frame-sync and receiver-parameter TOML files (US4.0/US3.0) and host the "Extract Calibration…" action (US3.2) and the "Apply to all <mode> streams" fan-out (US2.5)
   - Returns the configured `QVector<StreamConfig>` via `configs()` and the time channel via `timeChannelIndex()`

3. **PlotCustomizationDialog** (`src/view/plotcustomizationdialog.cpp`, `include/view/plotcustomizationdialog.h`)
   - "Customize Plot Series" dialog: a Frame Sync Lock tab (one row per stream — visibility toggle, color swatch, editable name) and a Receiver SNR tab (collapsible per-stream receiver/channel tree with tri-state group toggles and Expand/Collapse All; right-click a channel to rename or recolor it). Color/name edits are applied to the ViewModel on OK via `commitAppearanceChanges()`

4. **ExportDialog** (`src/view/exportdialog.cpp`, `include/view/exportdialog.h`)
   - Unified export dialog: any combination of CSV data, a plot image (PNG/SVG/PDF), and the log text, each with its own filename/location; checkbox-to-field enable logic and export-button validation

5. **PlotWidget** (`src/view/plotwidget.cpp`, `include/view/plotwidget.h`)
   - Self-contained QCustomPlot chart with title field, axis-control spinboxes, and a movable legend overlay (a translucent, draggable frame parented to the chart; single-column line-swatch + label rows with a vertical scrollbar for dense plots; composited into PNG/SVG exports)
   - Mouse wheel zoom and click-drag pan; `onSeriesVisibilityToggled()` toggles a graph without a full rebuild
   - All replots use `rpQueuedReplot`; controls disabled until data loads; `applyTheme(bool dark)` syncs colors with the app theme

#### ViewModel

6. **MainViewModel** (`src/viewmodel/mainviewmodel.cpp`, `include/viewmodel/mainviewmodel.h`)
   - Owns application state, validation, and the per-stream `StreamConfig` captured by StreamConfigDialog
   - Exposes Q_PROPERTYs (`inputFilename`, channel lists/indices, `fileLoaded`, `progressPercent`, `processing`) for the View to bind to
   - `openFile()` loads metadata via Chapter10Reader and logs channel/time/frame info
   - Builds a list of `StreamJob` objects (validated `ProcessingParams` + an owned `FrameSetup`) and hands them to ProcessingCoordinator
   - Receives each `ProcessedStreamData` and forwards it to PlotViewModel; manages recent files

7. **ProcessingCoordinator** (`src/viewmodel/processingcoordinator.cpp`, `include/viewmodel/processingcoordinator.h`)
   - Owns the reader + worker thread lifecycle for multi-stream processing
   - `startProcessing(QVector<StreamJob>)` takes ownership of each job's FrameSetup, spins up one `Ch10PacketReader` thread plus one `FrameProcessor` worker thread (and a `PacketQueue`) per stream
   - `cancelProcessing()` requests a cooperative abort of all workers and the reader; `reset()` clears transient state
   - Emits `progressChanged(int)`, `processingStateChanged(bool)`, `streamProcessed(ProcessedStreamData)`, `processingFinished(bool)`, `logMessageReceived(QString)`, `errorOccurred(QString)`

8. **PlotViewModel** (`src/viewmodel/plotviewmodel.cpp`, `include/viewmodel/plotviewmodel.h`)
   - Converts each `ProcessedStreamData` into in-memory `PlotSeriesData` vectors (name, receiver/channel indices, x/y values, cached Y min/max, color)
   - Converts absolute IRIG seconds to elapsed seconds; assigns the purple/blue/green (lock) and red/orange/yellow (SNR) palette
   - Manages axis ranges (auto Y with margin, manual Y override, X time window), the left-axis view toggle (lock % vs accumulated missed frames), and per-series visibility
   - Signals `dataChanged()`, `axisRangeChanged()`, `seriesVisibilityChanged()`; `computeYRange()` uses per-series cached min/max

#### Model

9. **Chapter10Reader** (`src/model/chapter10reader.cpp`, `include/model/chapter10reader.h`)
   - Reads Ch10 file **metadata** up front: scans TMATS to catalog time and PCM channels, provides channel lists, time accessors, and channel ID resolution. Wraps the irig106utils C library.

10. **Ch10PacketReader** (`src/model/ch10packetreader.cpp`, `include/model/ch10packetreader.h`)
    - The single-pass reader. `prepare()` opens the file, parses TMATS, resolves each stream's PCM attributes, and builds the channel-ID → `PacketQueue` routing; `run()` (on its own QThread) reads the file once, tracks IRIG time, and dispatches each PCM packet's payload to the matching queues, then posts end-of-stream sentinels
    - Owns the `SuChanInfo` per-channel bookkeeping table

11. **PacketQueue** (`include/model/packetqueue.h`) — bounded, thread-safe per-stream packet queue connecting the reader to one worker (header-only)

12. **FrameProcessor** (`src/model/frameprocessor.cpp`, `include/model/frameprocessor.h`)
    - Per-stream worker: drains its PacketQueue, runs the bit-serial frame-sync scanner (acquire/lock, off-phase rejection, bit-span lock %), accumulates lock %, missed frames, and (SNR mode) calibrated channel values into a `ProcessedStreamData`
    - Private helpers include `derandomizeBitstream()` and `hasSyncPattern()`; applies linear slope/offset or a non-linear `CalibrationProfile` per channel

13. **FrameSetup** (`src/model/framesetup.cpp`, `include/model/framesetup.h`) — frame configuration / word-map + calibration table built per job

14. **ChannelData** (`src/model/channeldata.cpp`, `include/model/channeldata.h`) — channel metadata value object

15. **StepDetector** (`src/model/stepdetector.cpp`, `include/model/stepdetector.h`) — *US3.2*; pure (UI-free) logic that parses the `[[Step]]` step-config TOML and detects step plateaus in a raw-count series via derivative/edge detection, building a per-channel `CalibrationProfile`

16. **CalibrationExtractor** (`src/model/calibrationextractor.cpp`, `include/model/calibrationextractor.h`) — *US3.2*; drives a raw extraction over a calibration Ch10 file (reusing the Ch10PacketReader + FrameProcessor pipeline with unit slope / zero offset) and runs StepDetector per channel to build session-only profiles

17. **TomlConfigHelper** (`src/model/tomlconfighelper.cpp`, `include/model/tomlconfighelper.h`) — registers a custom QSettings TOML format and provides the frame-sync / receiver-parameter load/save helpers

18. **CsvSeriesParser** (`src/model/csvseriesparser.cpp`, `include/model/csvseriesparser.h`) — pure (UI-free) static parser that turns a FrameProcessor `Day,Time,param…` CSV into `PlotSeriesData` (returned as a `CsvParseResult`). Extracted out of PlotViewModel so file parsing lives in the Model layer; thread-safe, so PlotViewModel runs `parse()` on a worker thread via `loadCsvFileAsync()`

19. **IRIG 106 Library** (`lib/irig106/`) — third-party C library for the Chapter 10 file format (see Protected Files)

### Constants and Data Structures

- **`AppVersion`** struct (`include/constants.h`) — version with `kMajor`, `kMinor`, `kPatch` and `toString()`
- **`PCMConstants`** namespace (`include/constants.h`) — PCM frame parameters (defaults for sync pattern/mask, bits per frame, scale, receiver counts, max raw sample value, buffer size, progress interval)
- **`UIConstants`** namespace (`include/constants.h`) — UI configuration (QSettings keys, theme identifiers, legend grid layout, sample-period/polarity/slope defaults, time validation limits, deployment/portable constants)
- **`PlotConstants`** namespace (`include/constants.h`) — plot dock dimensions, axis margin factor, default title, axis labels (`kSnrAxisLabel`, `kMissedFramesAxisLabel`), zoom factor, color palette
- **`CalibrationConstants`** namespace (`include/constants.h`) — non-linear calibration tuning (e.g. `kStepConfirmSeconds`)
- **`StreamConfig`** struct + **`StreamMode`** enum (`include/dto/streamconfig.h`) — per-stream configuration captured by StreamConfigDialog (frame params, SNR calibration fields, optional `calibrationByWord` profiles)
- **`StreamJob`** struct (`include/viewmodel/processingcoordinator.h`) — one unit of work: a `ProcessingParams` plus an owned `FrameSetup`
- **`ProcessingParams`** struct (`include/dto/processingparams.h`) — all inputs for processing one stream (filename, channel IDs, frame sync, time range, sample period, calibration, randomization)
- **`ProcessedStreamData`** + **`ProcessedChannelSeries`** structs (`include/dto/processedstreamdata.h`) — in-memory per-stream result (parallel `timesSec` / `lockPercent` / `accumulatedMissedFrames` vectors plus SNR channel series)
- **`PlotSeriesData`** struct (`include/dto/plotseriesdata.h`) — per-series plot data (name, receiver/channel indices, x/y vectors, visibility, color, cached Y min/max)
- **`CsvParseResult`** struct (`include/model/csvseriesparser.h`) — output of `CsvSeriesParser::parse()`: success flag, parsed `PlotSeriesData` vector, base day/time offset, and xMax; carried across the worker-thread boundary by PlotViewModel's `QFutureWatcher`
- **`CalibrationProfile`**, **`StepDefinition`**, **`CalibrationPoint`** (`include/dto/calibrationprofile.h`) — non-linear step-calibration data types (session-only); `interpolateCalibration()` does the piecewise-linear lookup
- **`TimeFields`** struct (`include/dto/timefields.h`) — groups DOY/HMS fields for start/stop times
- **`SuChanInfo`** typedef (`include/model/ch10packetreader.h`) — per-channel bookkeeping for the irig106 C helper layer

### Data Flow

```
User opens .ch10 ─► MainView ─► MainViewModel ─► Chapter10Reader (metadata)
                                      │
                            StreamConfigDialog (per-stream config)
                                      │
                      MainViewModel builds QVector<StreamJob>
                                      │
                             ProcessingCoordinator
                          ┌───────────┴───────────┐
                  Ch10PacketReader (one pass)      │
                          │  routes packets        │
                     PacketQueue ─► FrameProcessor (one worker per stream, parallel)
                                              │
                                   ProcessedStreamData (in memory)
                                              │
                                   PlotViewModel ─► PlotWidget (QCustomPlot)
```

## Qt-Specific Considerations

### Qt Version Compatibility

- **Target**: Qt 6.10.2
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
- **Member variables**: m_ prefix with snake_case (e.g., `m_frame_setup`, `m_reader`); widget members drop type suffixes when the declared type is clear (e.g., `m_input_file` not `m_input_file_lineedit`); buttons use `_btn` suffix (e.g., `m_process_btn`); settings members use `m_settings_` prefix (e.g., `m_settings_frame_sync`)
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
- **Debug**: `mingw32-make -f Makefile.Debug` → `debug/tmDataQualityAnalyzer.exe`
- **Release**: `mingw32-make -f Makefile.Release` → `release/tmDataQualityAnalyzer.exe`

### VS Code Integration
Tasks are defined in `.vscode/tasks.json`:
- "qmake: Configure" - Runs qmake to generate Makefiles
- "Build (Debug)" - Compiles debug build
- "Build (Release)" - Compiles release build
- "Clean" - Cleans build artifacts
- "Rebuild" - Clean + Build

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
2. Re-run qmake from the project root: `qmake tmDataQualityAnalyzer.pro -spec win32-g++ CONFIG+=debug`
3. Rebuild: `mingw32-make -f Makefile.Debug`

### Adding a New Per-Stream Setting
1. Add the field to the `StreamConfig` struct in `include/dto/streamconfig.h` (with a default in `constants.h` if appropriate)
2. Surface it in the relevant gear sub-dialog in `StreamConfigDialog` (Frame Sync Lock or Receiver SNR setup), and include it in the "Apply to all" fan-out if it should propagate
3. If it must round-trip to a TOML file, add it to the matching `TomlConfigHelper` load/save helper (respecting the US3.0/US5.0 boundaries)
4. Carry it into `ProcessingParams` (and `FrameSetup` if it affects the word map) where `MainViewModel` builds each `StreamJob`
5. Consume it in `FrameProcessor::process()`

## Debugging

### Common Issues

1. **Build fails with PATH errors**
   - Ensure Qt bin and MinGW bin are in PATH
   - Check paths in `.vscode/tasks.json` match your Qt installation

2. **MOC errors**
   - Ensure Q_OBJECT macro is present in classes with signals/slots
   - Re-run qmake if header structure changed

### Build Warnings
- Build should produce 0 warnings. If new warnings appear, fix them before committing.

## Testing

Automated unit tests use the **Qt Test** framework. Test sources are in the `tests/` directory with a separate `tests/tests.pro` project file.

### Test Suites
The suites below are registered (and run, in this order) in `tests/main.cpp`; the
source/header files are listed in `tests/tests.pro`.
- **TestChannelData** (`tst_channeldata`) — ChannelData model object tests
- **TestChapter10Reader** (`tst_chapter10reader`) — Chapter 10 metadata reader: channel discovery, time/PCM channel lists, channel ID resolution against real Ch10 test data
- **TestConstants** (`tst_constants`) — Verifies all PCMConstants, UIConstants, PlotConstants, AppVersion, and recent files constants (including kMaxPacketBufferSize, kFrameSyncHexPattern)
- **TestFrameProcessor** (`tst_frameprocessor`) — constructor defaults, abort flag, `derandomizeBitstream` (identity/short and changed/long), invalid time-channel/PCM-channel/file handling, and processing real Ch10 data (receiver-data accumulation, lock-only mode has no channels, monotonic frame-sync errors, slope affects values, shorter period → more samples, calibration round-trip clean steps, off-phase sync after lock-loss not extracted)
- **TestMainViewModelHelpers** (`tst_mainviewmodel_helpers`) — ViewModel helper methods (`channelPrefix` and `parameterName` over known/unknown/boundary indices)
- **TestFrameSetup** (`tst_framesetup`) — Frame parameter loading, word map, calibration
- **TestPlotViewModel** (`tst_plotviewmodel`) — default state, CSV load/export (incl. header-only, malformed rows, async load signals), time conversion/formatting, color assignment, Y auto/manual range, X time window, visibility, clear/title, in-memory `addStreamData` (lock/SNR/error series, multi-stream accumulation), and the left-axis view toggle preserving per-stream selection
- **TestProcessingCoordinator** (`tst_processingcoordinator`) — constructor defaults, `reset()` clears state, cancel-with-no-run no-op, `startProcessing()` empty-returns-false and processing-state emission, plus single-vs-multi-stream throughput benchmarks
- **TestMainView** (`tst_mainview`) — Main window construction, widget wiring, log routing, dock visibility behavior
- **TestPlotWidget** (`tst_plotwidget`) — Plot widget construction, null/valid ViewModel connection, dark/light theme application, the movable legend overlay populating from data (hidden until data loads, then one row per visible active-metric series), a shown/resized-window regression case asserting the overlay sizes correctly (not a collapsed frame-only box) after a second rebuild adds more rows — a QScrollArea `widgetResizable` sizeHint staleness bug reproduced and fixed post-review — the SNR legend row showing the short "CH\<id\> \<ch.name\>" form instead of the full TMATS stream title, the legend row layout reserving a right-side gutter matching the style's scrollbar extent, and each legend row carrying an objectName the overlay stylesheet can target to override the app's global `QWidget { background-color: ... }` theme rule (otherwise every row painted as an opaque chip)
- **TestPlotCustomizationDialog** (`tst_plotcustomizationdialog`) — Customize Plot Series dialog: one Frame Sync Lock checkbox per stream, Select All/None, apply → per-stream lock/missed visibility round-trip to the ViewModel; Receiver SNR tree build (receiver grouping), tri-state group cascade, Select All/None, apply → per-channel SNR visibility round-trip, and the Expand/Collapse All button toggle; plus per-stream rename/recolor (lock tab) and per-channel rename/recolor via pending item roles (SNR tab) applied to the ViewModel on OK, and the single batched `seriesAppearanceChanged` emission (reaches private widgets/slots via a friend declaration, same pattern as TestFrameProcessor)
- **TestStreamConfigDialog** (`tst_streamconfigdialog`) — Per-stream Configure Streams dialog: stream rows, mode selection, gear setup dialogs, TOML load/save round-trips, "Apply to all" fan-out, and the Channel column label (short names shown in full, long TMATS-derived names elided with "..." and the full name always available via tooltip)
- **TestExportDialog** (`tst_exportdialog`) — Export dialog checkbox-to-field enable logic, export-button validation, and the log-export row defaults/accessors and log-only validation
- **TestStepDetector** (`tst_stepdetector`) — Non-linear calibration (US3.2): `[[Step]]` TOML parsing (valid / empty-fails), plateau detection (clean, too-few-fails, extra-plateaus uses last of monotonic run, short-blip doesn't steal a pairing slot, long leading transient doesn't shift pairing, inverted-polarity sweep not reversed, non-monotonic pairing rejected, noisy, settling-at-plateau-start excluded, round-trip exact), and `interpolateCalibration()` (midpoint, below/above clamping, coincident-raw guard)
- **TestCalibrationExtractor** (`tst_calibrationextractor`) — US3.2 pipeline orchestration (complements TestStepDetector's pure logic): drives the async extraction end to end (reader + FrameProcessor workers → per-channel StepDetector). A bad file (with non-empty steps, so it clears the empty-steps guard) finishes unsuccessfully with a recorded error and no partial state; over `rnrz-l_testfile.ch10`, exactly words 6/7/8 (RCVR3 L/R/C, the only real stepped SNR sweep) build valid non-linear profiles while every other receiver word falls back to linear

### Running Tests
```bash
cd tests
qmake tests.pro -spec win32-g++
mingw32-make -f Makefile.Debug
./debug/tmDataQualityAnalyzer_tests.exe -o results.txt,txt
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
