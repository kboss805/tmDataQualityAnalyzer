# TM Data Quality Analyzer (tmDataQualityAnalyzer)

A desktop application for analyzing the data quality of IRIG 106 Chapter 10 PCM telemetry streams and receiver Automatic Gain Control (AGC) samples contained in telemetry streams. It measures per-stream **frame sync lock** statistics and extracts multiplexed receiver **AGC** samples, plotting both against time. Multiple PCM streams can be configured and processed concurrently.

## Overview

This application reads IRIG 106 Chapter 10 (.ch10) files and processes one or more PCM streams, each independently configured for one of two modes:

- **Frame Sync Lock** — measures how reliably the frame sync pattern locks across the recording, reported as a lock percentage over time. No receiver/calibration parameters are required.
- **Receiver AGC (SNR)** — extracts calibrated receiver-channel AGC values from the minor frame using a Receiver Parameters configuration (word map + calibration), with voltage-to-dB conversion.

Results are plotted in an interactive chart. The UI allows the user to configure each stream, including channel selection, frame sync parameters, time range, and the averaging window.

## Features

### File Input

- **Chapter 10 File Support**: Read, parse, and process IRIG 106 Chapter 10 telemetry files
- **Channel Selection**: Choose the time channel and one or more PCM channels for processing
- **Drag-and-Drop**: Drop `.ch10` or exported `.csv` files directly onto the application window, or open them from the ☰ menu (**Open…**)
- **Recent Files**: Up to 5 entries under the ☰ menu (Process section), persisted across sessions
- **CSV Import**: Re-open a CSV this application previously exported straight into the plot — no re-processing of the source `.ch10` file (☰ menu > **Import CSV…**, or drag-and-drop a `.csv`)

### Frame Sync Lock

- **Lock Percentage Over Time**: Measures per-stream frame synchronization lock and plots it against actual file time
- **Acquire / Lock Scanner**: Bit-serial sync scanner that confirms a configurable number of in-phase syncs before declaring lock, then drops and re-acquires lock when a true sync is missed
- **PRN-Robust Matching**: While locked, off-phase sync matches produced by pseudo-random data words are rejected so they cannot corrupt the lock figure (eliminates spurious lock dips on clean randomized streams)
- **Bit-Span Lock Metric**: Lock % is computed from the actual number of frame-sized bit slots traversed versus frames detected; time is used only to delimit the averaging window, so the figure does not depend on an exact data rate
- **Configurable Frame Parameters**: Frame sync pattern (hex), sync mask (hex), bits per frame, RNRZ-L randomization on/off, data rate (Mbps, or Auto from TMATS), and averaging window (1/10/100 Hz)

### Receiver AGC (SNR) Processing

- **Averaging Window**: 1 Hz, 10 Hz, or 100 Hz output sample rates
- **AGC Processing**: Extract calibrated receiver-channel AGC/SNR values with V-to-dB conversion
- **Calibration**: Configurable polarity, voltage slope/range, scale (dB/V), receiver count, and channels per receiver
- **Receiver Parameters TOML**: Word map and calibration loaded from a TOML configuration file
- **Non-Linear Step Calibration**: Build a per-channel raw→dB calibration profile from a calibration .ch10 file plus a `[[Step]]` step-config TOML ("Extract Calibration…" in the Receiver SNR setup); applied during processing via piecewise-linear interpolation between steps, clamping to the nearest end-step dB for values past the calibrated range (so out-of-cal receivers read the ceiling/floor instead of diverging), and falling back to linear slope/offset for channels that don't calibrate. A receiver that saturates partway through the sweep is calibrated over the steps it could still measure, and the summary names each receiver with the reason any of them fell back. Plateau detection is polarity-agnostic and automatically excludes a signal-generator turn-on transient and any operator down-ramp; optional **Clip Start / Clip End** controls let you trim leading/trailing seconds before detection. Runtime-only; the extracted profile is not saved to disk.

### Multi-Stream Processing

- **Per-Stream Configuration**: One row per PCM channel in the Configure Streams dialog, each independently set to Frame Sync Lock or Receiver SNR mode with its own setup
- **Concurrent Processing**: A single reader thread reads the file once and routes packets to per-stream queues; each stream is processed on its own worker thread in parallel
- **Single-Pass I/O**: The Chapter 10 file is read exactly once regardless of how many streams are selected
- **Accumulating Plot**: Each stream's results are added to the plot as it completes
- **Cancellation**: A cancel cleanly stops the reader and all worker threads

### Batch Processing & Templates

- **Processing Templates**: Save one configured file's complete per-stream setup — modes, frame-sync/SNR parameters, calibration references, plus each series' custom name and color — as a file-path-independent template (☰ menu > **Save as Template…**)
- **Apply Template to Many Files**: Run a saved template against any number of `.ch10` files in one action (☰ menu > **Apply Template to Files…**). Each file's PCM channel-ID set must exactly match the template's; non-matching files are flagged (with the missing/extra channels) and skipped
- **Retain-All + Plot File Selector**: Every processed file stays in memory; the plot's **Plot File** selector switches between any single file's results and an "All files (overlaid)" comparison view
- **Optional Per-File Export**: A batch run can auto-write, per file, a CSV plus Frame Sync Lock and Missed Frames plot images to a chosen folder

### Settings & Configuration

- **TOML Configuration**: Save and load frame sync fields and receiver parameters from TOML files
- **Per-File-Type Directory Persistence**: Independently remembers the last used directory for Ch10 and TOML file dialogs between sessions

### Plot & Visualization

- **Interactive Plot Window**: Plot showing frame sync lock (%) and/or receiver AGC (dB) series with mouse wheel zoom, click-drag pan (open/closed-hand grab cursor), auto-scale axes, per-series visibility toggles, and an auto-assigned color palette
- **Color Coding**: Frame Sync Lock series use purple/blue/green primaries (one per stream) and Receiver SNR series use red/orange/yellow primaries (one per receiver); additional streams, receivers, and channels are derived as progressively lighter shades so related series stay grouped
- **Customize Plot Series Dialog**: A tabbed dialog selects which series are visible — a Frame Sync Lock tab (one toggle per stream) and a Receiver SNR tab that presents each stream as a collapsible tree of receivers with L/R/C channel checkboxes, tri-state group toggles, and Expand/Collapse All; streams with many receivers split across two columns
- **Distraction-Free Plot**: The chart fills the entire plot area - there are no control rows around it. Every control lives in a **right-click context menu** on the chart (Set Plot Title, Plot File, View Mode, Customize View, Show Legend, X Axis, Y Axes, Export) or in a single on-chart chip bar
- **On-Chart Chip Bar**: A compact overlay at the chart's top-left holding the legend toggle, a one-click **View Mode** chip (labelled with the view it switches to), and a **Reset view** chip that appears only once the view is zoomed or an axis maximum is pinned - so nothing sits there unused
- **Quick Plot Gestures**: Ctrl+drag or middle-drag rubber-bands a time range to zoom into it, double-click restores the full span, and a dashed crosshair follows the cursor for comparing series at the same instant
- **View Mode**: The context menu switches the left axis between Frame Sync Lock (%) and Accumulated Missed Frames; the per-stream visibility selection is preserved across the switch
- **Plot File Selector**: After a batch run, the context menu's **Plot File** submenu selects which processed file to view, or overlays them all (disabled when only one file is loaded)
- **Movable On-Plot Legend**: A translucent legend floats inside the chart (defaulting to the top-right, reset each session) and can be dragged clear of the data; it scrolls for dense plots (e.g. 48+ SNR channels) and is composited into exported images at its placed position. A small on-chart button (top-left) shows/hides it, and the choice persists between sessions
- **X-Axis Time Display**: Actual file time (DDD:HH:MM:SS) on the X axis instead of elapsed seconds
- **Export**: Export the current plot and its data from the ☰ menu or the plot's right-click menu (**Export…**) — choose any combination of CSV data, a plot image (PNG/SVG/PDF), and the log window contents (text); the image includes the legend
- **Hover Tooltip**: Shows series name, time (DDD:HH:MM:SS), and value (lock % or dB) on mouse hover

### Logging & Feedback

- **Inline Log Window**: Persistent, scrollable log with color-coded messages (green for success, yellow for warnings, red for errors)
- **Status Bar**: Displays file metadata summary (filename, size, channel counts, time range)
- **Pre-Process Summary**: Logs input file, channels, time range, sample rate, receiver count, and output path before processing
- **Startup & File Logging**: Logs default.ini settings at startup, channel/time/frame info when opening Ch10 files, and INI validation details when loading settings
- **Clickable Log Links**: Output file path and "Open Folder" links in the log window after processing completes

### Application & UI

- **Single Hamburger Menu**: Every command lives in one ☰ menu in the window's title bar, grouped into Process, Import/Export, Settings, and Help sections
- **Frameless Window**: A custom title bar with its own minimize/maximize/close buttons; native Windows drag, resize, snap, and double-click-maximize are preserved
- **Toggleable Sidebar**: Show or hide the log sidebar from the title bar or with **Ctrl+B**; the choice persists across sessions
- **Dark & Light Themes**: Windows 11 / WinUI 3 styled dark and light themes with runtime toggle (Settings section); title-bar and menu icons re-render per theme
- **Built-in User Manual**: An HTML user manual opens in the default browser from ☰ menu > Help > **User Manual…**
- **Keyboard Shortcuts**: Ctrl+O (open a file), Ctrl+B (toggle the sidebar)
- **Tooltips**: Descriptive tooltips on plot controls and dialog fields

### Deployment

- **Installer & Portable Distribution**: Inno Setup EXE installer with admin/non-admin support, portable ZIP with local settings, INI upgrade logic, and optional `.ch10` file association

## System Requirements

### Software

- **Qt**: Version 6.0.0 or later (developed on 6.12.0), `msvc2022_64` kit
- **Compiler**: MSVC 2022 (Visual Studio 2022 C++ Build Tools — workload
  "Desktop development with C++", giving `cl` / `nmake` + the Windows SDK)
- **C++ Standard**: C++17 required
- **Operating System**: Windows

### Build Tools

- qmake (Qt build system)
- MSVC 2022 toolchain (`cl` / `nmake`)

## Building the Project

### Using Qt Creator

1. Open `tmDataQualityAnalyzer.pro` in Qt Creator
2. Configure the project with your Qt MSVC 2022 kit
3. Build and run (Ctrl+R)

### Using Command Line (Windows)

```powershell
# Set up the MSVC toolchain + Qt (imports vcvars, puts the Qt msvc kit on PATH)
. .\scripts\env.ps1

# Create and enter the build directory
mkdir build
cd build

# Configure the project (run from the build directory)
qmake ../tmDataQualityAnalyzer.pro -spec win32-msvc

# Build debug version
nmake -f Makefile.Debug

# Build release version
nmake -f Makefile.Release

# Run the application
debug\tmDataQualityAnalyzer.exe
```

## Usage

1. **Load Input File**
   - Open the ☰ menu and choose **Open…** (or press **Ctrl+O**) to browse for a Chapter 10 (.ch10) file
   - Or drag and drop a .ch10 file onto the application window
   - The **Configure Streams** dialog opens automatically, listing one row per PCM channel found in the file

2. **Select the Time Channel**
   - Choose the Time Channel from the dropdown at the top of the Configure Streams dialog

3. **Configure Each Stream**
   - For each PCM channel you want to analyze, check **Process**
   - Choose its **Mode**: *Frame Sync Lock* or *Receiver SNR*
   - Click the **gear** icon to open that stream's setup:
     - **Frame Sync Lock**: set the frame sync pattern, sync mask, bits per frame, randomized (RNRZ-L) on/off, data rate (or leave on Auto/TMATS), and the averaging window
     - **Receiver SNR**: set the same frame parameters plus polarity, slope, scale (dB/V), receiver count and channels, and load a Receiver Parameters TOML
   - Use the **Load/Save** buttons in the setup dialogs to reuse TOML configurations
   - The **Ready** indicator turns green when a stream is fully configured
   - The entire file is processed; output resolution is set per stream via the **averaging window** (1 s / 100 ms / 10 ms)

4. **Process**
   - Click **Process** (the dialog's accept button) to read the file and run all enabled streams concurrently
   - Monitor progress in the progress bar and log window; each stream is added to the plot as it completes

## Project Structure

```text
tmDataQualityAnalyzer/
├── build/                      # Out-of-source build artifacts
├── deploy/                     # Release packaging: build_release.ps1, Inno Setup .iss, release notes
├── docs/                       # Design notes, logic docs (SNR_logic, framesync_logic), AI guide (claude.md)
├── include/                    # Header files, organized by MVVM layer
│   ├── dto/                    # Plain value types passed between layers
│   │   ├── streamconfig.h · source.h · processingparams.h · processedstreamdata.h
│   │   ├── plotseriesdata.h · seriesappearance.h · processingtemplate.h
│   │   └── framesyncparams.h · calibrationprofile.h (US5.3, runtime-only)
│   ├── model/                 # File I/O, decommutation, schemas (no Qt UI)
│   │   ├── chapter10reader.h · ch10packetreader.h · packetqueue.h · frameprocessor.h
│   │   ├── stepdetector.h · calibrationextractor.h        # Non-linear step calibration (US5.3)
│   │   ├── framesetup.h · channeldata.h · tomlconfighelper.h
│   │   ├── csvseriesparser.h · seriescolumnschema.h       # CSV import/export (US6.x)
│   │   └── streamconfigschema.h · processingtemplateschema.h · templatematcher.h  # Templates (US1.1)
│   ├── view/                  # Qt widgets and dialogs
│   │   ├── mainview.h          # Main window: hamburger menu + frameless title bar + log sidebar
│   │   ├── plotwidget.h        # chart host + on-plot legend + right-click context menu
│   │   ├── tmchart.h           # first-party 2D line chart (axes, series, gestures, export)
│   │   ├── streamconfigdialog.h · streamsubdialogs.h · plotcustomizationdialog.h
│   │   └── exportdialog.h · batchapplydialog.h · processingprogressdialog.h
│   ├── viewmodel/             # Application logic bound to the views
│   │   └── mainviewmodel.h · processingcoordinator.h · plotviewmodel.h
│   └── constants.h            # App version (single source), UI/plot constants, QSettings keys
├── src/                        # Implementations mirroring include/ (main.cpp, model/, view/, viewmodel/)
├── lib/irig106/                # Third-party IRIG 106 library — do NOT edit (adapt in app code)
│   ├── src/                   # irig106utils C source files
│   └── include/               # irig106utils C header files
├── tests/                      # Qt Test framework unit tests (19 suites)
├── settings/                   # Default and user TOML settings files
├── resources/                  # Stylesheets, icons, embedded manual, Windows .rc
│   ├── win11-dark.qss · win11-light.qss       # Theme stylesheets
│   ├── usermanual.html         # Embedded HTML user manual (Help > User Manual…)
│   ├── export-{dark,light}.svg · import-{dark,light}.svg   # Menu icons (per-theme variants)
│   ├── chevron-*.svg · checkmark.svg · gear.svg · folder-open.svg
│   ├── play.svg · stop.svg · retry.svg · floppy-save.svg · toggle-*.svg
│   ├── tmDataQualityAnalyzer_resource.rc      # Windows resource file
│   └── icon.ico               # Application icon
├── scripts/                    # Developer helper scripts (the signed release build is deploy/build_release.ps1)
│   ├── env.ps1                # MSVC + Qt environment setup — dot-source before building
│   ├── build_ide.ps1          # IDE/VS Code test build helper
│   ├── setup-env.ps1          # One-time Windows user environment variable registration
│   └── gen_compile_flags.py   # Generate compile_flags.txt for clangd/clang-tidy IntelliSense
├── tmDataQualityAnalyzer.pro   # Qt project file (parses the version from constants.h)
└── README.md                   # This file
```

## Credits

This project incorporates code from the [irig106utils](https://github.com/atac/irig106utils) library for IRIG 106 Chapter 10 file handling. Charting is first-party (`TmChart`); the previously vendored QCustomPlot library has been removed.

## License

GPL v3

## Contributing

[TBD]

## Support

Kevin Bossoletti
