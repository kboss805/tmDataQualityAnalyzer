# tmDataQualityAnalyzer.md - AI Assistant Guide

This file provides context and guidelines for AI assistants working on the tmDataQualityAnalyzer project.

## Version Information

- **Qt Version**: 6.10.2 (minimum: Qt 6.0.0)
- **MinGW Version**: 13.1.0 (minimum: GCC/MinGW 7.0)
- **C++ Standard**: C++17 (required — `inline constexpr` used throughout constants.h)
- **Project Version**: 2.2.0 — defined once in the `AppVersion` struct in `include/constants.h`; qmake parses it from that header and propagates it to the Qt `VERSION` and the Windows resource file (`version_autogen.h`), so no other file carries a duplicate version literal

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
**So that** I can import the file into 3rd party applications such as Excel and Matlab for further analysis (the export settings and file location are configured via the export dialog window defined in US7.0).

**Acceptance Criteria:**
- [x] The current SNR signal data displayed in the plot window is exported to a CSV file
- [x] The CSV file includes at least the following columns: time stamps, receiver/channel identifiers, and SNR signal data
- [x] The CSV file includes headers describing the content of each column

### US1.3: Export user specified SNR signal data to an image file
**As an** As a telemetry engineer or data analyst
**I want to** to export SNR signal data to an image file (e.g. png, pdf, etc.) from the plot window.
**So that** I can import the image file into 3rd party applications such as PowerPoint to create presentations (the export format and location are configured via the export dialog window defined in US7.0).

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
**So that** I can import the file into 3rd party applications such as Excel and Matlab for further analysis (the export settings and file location are configured via the export dialog window defined in US7.0).

**Acceptance Criteria:**
- [x] The current framesync lock statistics and accumulated missed frames displayed in the plot window are exported to a CSV file
- [x] The CSV file includes at least the following columns: time stamps, stream identifier, framesync lock statistics and accumulated missed frames
- [x] The CSV file includes headers describing the content of each column

### US2.4: Export user specified framesync lock statistics and accumulated missed frames to an image file
**As an** As a telemetry engineer or data analyst
**I want to** to export framesync lock statistics and accumulated missed frames to an image file (e.g. png, pdf, etc.) from the plot window.
**So that** I can import the image file into 3rd party applications such as PowerPoint to create presentations (the export format and location are configured via the export dialog window defined in US7.0).

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
- [x] The user can load a TOML file defining the expected step values (in dB) and duration (dwell time in seconds).
- [x] The user can load a Calibration CH10 file containing the recorded step data.
- [x] The application automatically extracts plateaus from the CAL file, using the expected dwell times to filter noise and trimming transition edges to only average the stable "core" of the step.
- [x] The application evaluates step extraction success independently for each receiver channel (up to 48).
- [x] Channels that successfully map the expected number of steps are assigned a non-linear calibration profile for the session.
- [x] Channels that fail step extraction (e.g., due to noise or no data) fall back to the standard linear (slope/offset) calibration.
- [x] A summary message box informs the user which channels succeeded and which fell back.
- [x] During main data processing, the application applies the non-linear profile using piece-wise linear interpolation between steps, and linear extrapolation for out-of-bounds values.

  - **Scope:** Plateaus are found by derivative/edge detection and paired with the TOML steps in time order (if more plateaus than steps are detected, the first N are used). "Success" requires at least the expected number of plateaus. Profiles are session-only (never serialized), keyed by minor-frame word index, and applied per word-map parameter. Implemented by `StepDetector`, `CalibrationExtractor`, `interpolateCalibration()` (in `calibrationprofile.h`), and the `FrameProcessor` sample-conversion branch; the extraction reuses the production `Ch10PacketReader` + `FrameProcessor` pipeline with unit slope / zero offset to recover raw counts.

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
- [x] An on-plot legend panel below the chart shows a color swatch and label for each visible series, scrolls vertically when entries exceed the visible rows, and is composited into exported PNG images
- [x] Auto set plot colors by default; framesync lock curves use purple/blue/green primaries (one per stream) and SNR curves use red/orange/yellow primaries (one per receiver); additional streams, receivers, and channels are progressively lighter shades of their primary so related series stay grouped
- [x] Framesync lock curves are highlighted; SNR curves are not

### US7.0: Export file settings
**As an** As a telemetry engineer or data analyst 
**I want to** configure export preferences via a unified export dialog window
**So that** I can specify the export types (CSV and/or images), file names, and directories for the data exports requested in US1.2, US1.3, US2.2, and US2.3 in a single action.

**Acceptance Criteria:**
- [x] A dialog window is presented to the user when the user clicks the export button
- [x] The dialog window allows the user to determine what data is exported; SNR sample data and framesync lock statistic data, plot images, or the log window contents
- [x] The dialog window allows the user to specify the filename and location for the exported data files
- [x] The dialog window allows the user to specify the filename and location for the exported plot images
- [x] The dialog window allows the user to export the log window contents to a text file, with its own filename and location

### US8.0: Error Checking
**As an** As a telemetry engineer or data analyst 
**I want to** ensure the values I enter into the application are valid
**So that** I can avoid errors and ensure the data I export is accurate

**Acceptance Criteria:**
- [x] Ensure all input fields are validated
- [x] Error messages are displayed to the user when invalid values are entered
- [x] Application ensures frame sync pattern only contains hexadecimal values
- [x] Application ensures frame sync pattern is no larger than the user specified frame length

### US9.0: Application Installer
**As a** developer
**I want to** create an application installer
**So that** I can quickly deploy the software/updates to users with all the necessary folders and settings files

**Acceptance Criteria:**
- [x] Signed application
- [x] Install to "Program Files" and to a user-selected directory for users without admin privileges
- [x] Installer shows install progress
- [x] Installer should not overwrite TOML files; if new fields are in the TOML file, alert the user that a new TOML file was saved as "new_x.toml" — try to use as many parameters from the old default TOML file as possible in the new TOML file

## Version History

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
  Image export (US7.0)
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
- User stories US1.0–US9.0 complete
- Frame Sync Lock analysis with off-phase rejection and bit-span lock percentage
- Receiver SNR / AGC analysis with voltage-to-dB calibration
- Multi-stream concurrent processing with single-pass I/O (one reader + per-stream queues)
- Per-stream stream configuration dialog with TOML load/save
- Plot window with dual Y-axes, mouse zoom/pan, series visibility toggles, and light/dark theme
- CSV and image (SVG/PNG/PDF) export via unified export dialog
- Inno Setup installer (admin + non-admin) and portable ZIP packaging
- Automated unit tests (Qt Test framework)
- US10.0 (Bit Error Rate) targeted for a future release

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

> ⚠️ **This section is partially out of date (as of v2.2.0).** It still describes a
> previous single-file + batch architecture and several classes that no longer
> exist (`SettingsDialog`, `SettingsManager`, `ReceiverGridWidget`) and structs
> that were removed (`SettingsData`, `BatchFileInfo`). The current design is the
> per-stream **Configure Streams** flow: `StreamConfigDialog` (per-stream setup),
> `Ch10PacketReader` (single reader → per-stream `PacketQueue`s), per-stream
> `FrameProcessor` workers, `ProcessingCoordinator` (worker lifecycle/batch),
> `TomlConfigHelper` (TOML I/O), `PlotCustomizationDialog`, and `ExportDialog`.
> See the README "Overview" / "Project Structure" for the accurate component
> roster until this section is rewritten.

### Core Components

The application follows the **MVVM (Model-View-ViewModel)** pattern:

1. **MainView** (`src/mainview.cpp`, `include/mainview.h`) — *View*
   - Thin GUI layer; creates and lays out all Qt widgets
   - Delegates receiver grid to ReceiverGridWidget, time controls to TimeExtractionWidget
   - Binds to MainViewModel Q_PROPERTYs and connects signals/slots
   - Contains no business logic; delegates all actions to the ViewModel
   - `logError()` / `logWarning()` / `logSuccess()` append colored HTML entries (red / #DAA520 / green) to the log window via `append()`
   - Errors and warnings are shown inline in the log; QMessageBox reserved for About dialog only
   - Log window uses `QTextBrowser` for clickable links; persistent (never cleared) with auto-scroll on new entries
   - Status bar displays file metadata summary (filename, size, channel counts, time range)
   - Read-only settings summary panel shows current frame sync, polarity, slope, scale, and receiver configuration
   - Recent Files submenu under File menu with persistence across sessions
   - Unified `QTreeWidget` file list for both single-file and batch modes with embedded per-file Time and PCM channel `QComboBox` selectors
   - Color-coded per-file status (Ready/Valid/Done/Skip/Error) and encoding column in batch mode
   - Multi-file selection via `QFileDialog::getOpenFileNames()` and multi-file drag-and-drop
   - Batch output directory prompt via `QFileDialog::getExistingDirectory()`
   - Dedicated cancel toolbar button (visible only during processing)
   - Log window in bottom QDockWidget; auto-hides when plot dock opens, restores when plot closes
   - Plot dock (right QDockWidget) with PlotWidget; auto-shown after single-file processing
   - View menu (Show Plot / Show Log) between File and Help menus

   a. **ReceiverGridWidget** (`src/receivergridwidget.cpp`, `include/receivergridwidget.h`) — *View*
      - Self-contained multi-column tree grid for receiver/channel selection
      - Manages expand/collapse, tri-state checkboxes, and synchronized scrollbars
      - Emits `receiverChecked()` when the user toggles a channel checkbox
      - Emits `selectAllRequested()` / `selectNoneRequested()` from dedicated buttons

   b. **TimeExtractionWidget** (`src/timeextractionwidget.cpp`, `include/timeextractionwidget.h`) — *View*
      - Widget with extract-all toggle, start/stop time inputs, and sample rate selector
      - `setSampleRateEnabled()` keeps sample rate active when other time controls are disabled (batch mode)
      - Emits `extractAllTimeChanged()` and `sampleRateIndexChanged()` signals

2. **SettingsDialog** (`src/settingsdialog.cpp`, `include/settingsdialog.h`) — *View*
   - Modal dialog for frame sync, polarity, scale, range, and receiver settings
   - Uses `setData()`/`getData()` with `SettingsData` struct for clean data transfer
   - Emits `loadRequested()` and `saveAsRequested()` for file I/O delegation

3. **MainViewModel** (`src/mainviewmodel.cpp`, `include/mainviewmodel.h`) — *ViewModel*
   - Owns all application state, validation, and processing orchestration
   - Exposes Q_PROPERTYs for the View to bind to
   - `validateProcessingInputs()`, `prepareFrameSetupParameters()`, `launchWorkerThread()` orchestrate the processing pipeline
   - `validateTimeFields()` shared by start/stop time validation; `generateOutputFilename()` shared by input-success and processing-finished flows
   - Creates a fresh `FrameProcessor` per processing run on a worker thread
   - `logStartupInfo()` emits default.ini settings at application startup (called after signal connections are established)
   - `openFile()` logs channel info, time range, and current frame settings when a Ch10 file is loaded
   - `runPreScan()` detects PCM encoding and verifies frame sync; runs on file open and on PCM channel change
   - `fileMetadataSummary()` returns formatted string for the status bar
   - `recentFiles()`, `addRecentFile()`, `clearRecentFiles()` manage recent file list with QSettings persistence
   - Emits pre-process summary log messages before launching worker thread
   - Batch processing: `openFiles()` loads multiple files, per-file channel discovery and validation
   - `setBatchFilePcmChannel()` / `setBatchFileTimeChannel()` for per-file channel selection
   - `startBatchProcessing(output_dir, sample_rate_index)` / `processNextBatchFile()` drive sequential batch execution with async continuation via `onProcessingFinished()`
   - `retryFailedFiles()` resets ERROR files' `processed` state and re-runs `processNextBatchFile()`; `processNextBatchFile()` skips `processed && processedOk` files so successful files are never re-run
   - `reorderBatchFile(from, to)` moves a file in `m_batch_files` and emits `batchFilesChanged()` to trigger a full list rebuild

4. **PlotViewModel** (`src/plotviewmodel.cpp`, `include/plotviewmodel.h`) — *ViewModel*
   - Parses CSV output files into in-memory `PlotSeriesData` vectors (name, receiver index, x/y values, cached Y min/max, color)
   - Pre-allocates data vectors from estimated file size for efficient CSV parsing
   - Converts DOY + HMS timestamps to elapsed seconds from first sample
   - Assigns colors from a 10-hue palette; channels within same receiver get varied saturation/value
   - Manages axis ranges (auto Y with margin, manual Y override, X time window)
   - Per-series visibility toggle; signals `dataChanged()`, `axisRangeChanged()`, `seriesVisibilityChanged()`
   - `computeYRange()` uses per-series cached min/max (O(series) not O(data points))

5. **ProcessingCoordinator** (`src/processingcoordinator.cpp`, `include/processingcoordinator.h`) — *ViewModel*
   - Owns all worker thread lifecycle and batch sequencing, extracted from MainViewModel
   - `startSingleProcessing()` and `startBatchProcessing()` are the two entry points
   - `retryFailedFiles()` resets ERROR files and re-invokes `processNextBatchFile()`; skips already-successful files
   - `cancelProcessing()` sets abort flag on current processor and sets `m_batch_cancelled`
   - `runPreScan()` (single file) and `preScanBatchFiles()` (batch) detect encoding and verify frame sync
   - `launchWorkerThread()` creates a fresh `FrameProcessor`, moves it to a `QThread`, connects signals, starts the thread
   - `onProcessingFinished()` tears down thread (`quit()`/`wait()`/`delete`), updates batch state, calls `processNextBatchFile()` or emits final `processingFinished()`
   - Receives `QVector<BatchFileInfo>*`, `FrameSetup*`, `Chapter10Reader*` via constructor injection
   - Emits: `processingStateChanged(bool)`, `progressChanged(int)`, `processingFinished(bool, QString)`, `logMessageReceived(QString)`, `errorOccurred(QString)`, `batchFilesChanged()`, `batchFileProcessing(int, int)`

6. **PlotWidget** (`src/plotwidget.cpp`, `include/plotwidget.h`) — *View*
   - Self-contained QCustomPlot chart widget with toolbar controls and legend panel
   - Top toolbar: title QLineEdit
   - Axis controls grid: X start/stop and Y min/max spinboxes in aligned columns, reset button
   - Legend: scrollable colored tree checkboxes for per-series visibility
   - Supports mouse wheel zoom (Y axis) and click-drag pan (both axes)
   - `onSeriesVisibilityToggled()` toggles individual graph visibility without full rebuild
   - All replots use `rpQueuedReplot` to coalesce redundant repaint requests
   - All plot controls disabled until data loads; enabled in `rebuildChart()`
   - `applyTheme(bool dark)` syncs chart colors with app dark/light theme
   - Placed inside a right QDockWidget by MainView

6. **Chapter10Reader** (`src/chapter10reader.cpp`, `include/chapter10reader.h`) — *Model*
   - Reads IRIG 106 Chapter 10 file metadata and manages channel selection
   - Scans TMATS records to catalog time and PCM channels
   - Provides channel lists, time accessors, and channel ID resolution
   - Wraps irig106utils C library for file I/O

7. **FrameProcessor** (`src/frameprocessor.cpp`, `include/frameprocessor.h`) — *Model*
   - Self-contained PCM frame extraction and CSV output processor
   - Created fresh per processing run, moved to a worker thread, auto-deleted via `deleteLater`
   - Owns its own irig106 file handle, buffers, and TMATS metadata
   - `process()` method takes channel IDs (not indices) and emits progress/completion signals
   - Private helper methods: `freeChanInfoTable()`, `assembleAttributesFromTMATS()`, `derandomizeBitstream()`, `hasSyncPattern()`

8. **SettingsManager** (`src/settingsmanager.cpp`, `include/settingsmanager.h`) — *Model*
   - Handles saving/loading user preferences using QSettings
   - Persists UI state between sessions via `MainViewModel*`
   - Validates all TOML values on load (FrameSync hex, Polarity, Slope, Scale, receiver count/channels)
   - Validates parameter section count against receiver x channel configuration
   - Emits `logMessage()` for load/save status, warnings, and errors routed to the log window

9. **FrameSetup** (`src/framesetup.cpp`, `include/framesetup.h`) — *Model*
   - Manages frame configuration parameters (word map, calibration)
   - Handles frame setup file loading and saving

10. **IRIG 106 Library** (`lib/irig106/src/irig106*.c`, `lib/irig106/include/i106*.h`)
   - Third-party C library for Chapter 10 file format
   - Handles low-level file parsing and data structures

### Constants and Data Structures

- **`AppVersion`** struct (in `include/constants.h`) — Version information with `kMajor`, `kMinor`, `kPatch` and `toString()`
- **`PCMConstants`** namespace (in `include/constants.h`) — Named constants for PCM frame parameters (word count, frame length, sync pattern length, time rounding, channel type identifiers, max raw sample value, default buffer size, progress report interval)
- **`UIConstants`** namespace (in `include/constants.h`) — Named constants for UI configuration (QSettings keys, theme identifiers, plot legend grid layout, time conversion, receiver count, default slope/scale, button text, time validation limits, sample rates, output filename format, deployment/portable mode constants)
- **`SettingsData`** struct (in `include/settingsdata.h`) — Value type used to transfer UI state between MainViewModel and SettingsManager without `friend class` coupling
- **`BatchFileInfo`** struct (in `include/batchfileinfo.h`) — Per-file metadata for batch processing (filepath, channel strings/IDs, resolved channel indices, validation state, encoding, processing result)
- **`PlotConstants`** namespace (in `include/constants.h`) — Named constants for plot dock dimensions, axis margin factor, default title, axis labels, zoom factor, and receiver color palette (10 hues)
- **`PlotSeriesData`** struct (in `include/plotviewmodel.h`) — Per-series data for plotting (name, receiver/channel indices, x/y value vectors, visibility, color, cached Y min/max)
- **`ProcessingParams`** struct (in `include/processingparams.h`) — All input parameters for a single processing run (filename, channel IDs, frame sync, time range, sample rate, calibration, output path, randomization flag)
- **`TimeFields`** struct (in `include/timefields.h`) — Groups DOY/hour/minute/second fields for start and stop times; used by MainViewModel and MainView for time range transfers
- **`SuChanInfo`** typedef (in `include/frameprocessor.h`) — Per-channel bookkeeping struct for the irig106 C helper layer

### Data Flow

```
User Input → MainView → MainViewModel → Chapter10Reader (metadata)
                              ↓                ↓
                        SettingsManager    FrameSetup
                              ↓
                        FrameProcessor → IRIG106 Library
                              ↓
                          CSV Output
                              ↓
                        PlotViewModel → PlotWidget (QCustomPlot)
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
- **Struct fields**: `ParameterInfo` and `ProcessingParams` use snake_case; `SettingsData` uses camelCase (Qt property style)

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
- **Build automation**: `deploy/build_release.cmd` — builds release, runs `windeployqt`, stages installer and portable layouts, signs exe, creates ZIP, compiles Inno Setup installer
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

### AGC Processing
- Central processing function: `FrameProcessor::process()`
- Takes parameters: input file, frame setup, output file, channel IDs, sync, sync length, time range, sample rate
- Returns bool indicating success/failure
- Created fresh per run by `MainViewModel::launchWorkerThread()`, moved to a background `QThread`, auto-deleted via `deleteLater` when the thread finishes
- Emits `progressUpdated(int)`, `processingFinished(bool)`, `logMessage(QString)`, and `errorOccurred(QString)` signals

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

### Adding a New Setting
1. Add field to `SettingsData` struct in `include/settingsdata.h`
2. Add to `MainViewModel::getSettingsData()` and `MainViewModel::applySettingsData()`
3. Add to save logic in `SettingsManager::saveFile()`
4. Add to load logic in `SettingsManager::loadFile()`
5. Update UI initialization in `MainViewModel::clearState()`

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
- **TestFrameProcessor** (`tst_frameprocessor`) — FrameProcessor constructor, abort flag, private static helpers (hasSyncPattern, derandomizeBitstream, writeTimeSample), preScan with valid/invalid files and encodings, process with real Ch10 test data
- **TestMainViewModelHelpers** (`tst_mainviewmodel_helpers`) — ViewModel helper methods (channelPrefix, parameterName, generateOutputFilename)
- **TestFrameSetup** (`tst_framesetup`) — Frame parameter loading, word map, calibration
- **TestPlotViewModel** (`tst_plotviewmodel`) — PlotViewModel default state, CSV loading, time conversion, series color assignment, Y auto/manual range, X time window, series visibility, clear data, plot title, invalid/empty file handling, missed-frames series creation, left-axis view toggle, and preservation of per-stream selection across the toggle
- **TestTimeExtractionWidget** (`tst_timeextractionwidget`) — Widget defaults, extractAllTime toggle, sampleRate setter/getter, fillTimes/clearTimes, enable/disable controls, sample rate options
- **TestProcessingCoordinator** (`tst_processingcoordinator`) — Worker-thread lifecycle, single vs. batch sequencing, pre-scan, retry/cancel state transitions
- **TestMainView** (`tst_mainview`) — Main window construction, widget wiring, log routing, dock visibility behavior
- **TestPlotWidget** (`tst_plotwidget`) — Plot widget construction, control enable/disable on data load, theme application, legend rebuild
- **TestStreamConfigDialog** (`tst_streamconfigdialog`) — Per-stream Configure Streams dialog: stream rows, mode selection, gear setup dialogs, TOML load/save round-trips, "Apply to all" fan-out
- **TestExportDialog** (`tst_exportdialog`) — Export dialog checkbox-to-field enable logic, export-button validation, and the log-export row defaults/accessors and log-only validation
- **TestStepDetector** (`tst_stepdetector`) — Non-linear calibration (US3.2): `[[Step]]` TOML parsing (valid / missing dwell / empty), plateau detection (clean, too-few-fails, extra-plateaus-uses-first-N, noisy, edge-trim), and `interpolateCalibration()` (midpoint, below/above extrapolation, coincident-raw guard)

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
