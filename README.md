# TM Data Quality Analyzer (tmDataQualityAnalyzer)

A desktop application for analyzing the data quality of IRIG 106 Chapter 10 PCM telemetry streams and receiver Automatic Gain Control (AGC) samples contained in telemetry streams. It measures per-stream **frame sync lock** statistics and extracts multiplexed receiver **AGC** samples, plotting both against time. Multiple PCM streams can be configured and processed concurrently.

## Overview

This application reads IRIG 106 Chapter 10 (.ch10) files and processes one or more PCM streams, each independently configured for one of two modes:

- **Frame Sync Lock** — measures how reliably the frame sync pattern locks across the recording, reported as a lock percentage over time. No receiver/calibration parameters are required.
- **Receiver AGC (SNR)** — extracts calibrated receiver-channel AGC values from the minor frame using a Receiver Parameters configuration (word map + calibration), with voltage-to-dB conversion.

Results are plotted in an interactive chart. The UI allows the user to configure each stream, including channel selection, frame sync parameters and the averaging window.

## Features

### File Input

- **Chapter 10 File Support**: Read, parse, and process IRIG 106 Chapter 10 telemetry files
- **Channel Selection**: Choose the time channel and one or more PCM channels for processing
- **Drag-and-Drop**: Drop `.ch10` or exported `.csv` files directly onto the application window, or open them from the ☰ menu (**Open…** for recordings, **Import CSV…** for exports)
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
- **Folder Persistence**: Remembers, between sessions, the folder of the last recording, CSV or template, and the folder of the last calibration recording

### Plot & Visualization

- **Interactive Plot Window**: Plot showing frame sync lock (%) and/or receiver AGC (dB) series with mouse wheel zoom, click-drag pan (open/closed-hand grab cursor), auto-scale axes, per-series visibility toggles, and an auto-assigned color palette
- **Color Coding**: Frame Sync Lock series use purple/blue/green primaries (one per stream) and Receiver SNR series use red/orange/yellow primaries (one per receiver); additional streams, receivers, and channels are derived as progressively lighter shades so related series stay grouped
- **Customize Plot Series Dialog**: A tabbed dialog selects which series are visible — a Frame Sync Lock tab (one toggle per stream) and a Receiver SNR tab that presents each stream as a collapsible tree of receivers with L/R/C channel checkboxes, tri-state group toggles, and Expand/Collapse All; streams with many receivers split across two columns
- **Distraction-Free Plot**: The chart fills the entire plot area - there are no control rows around it. Every control lives in a **right-click context menu** on the chart (Set Plot Title, Plot File, View Mode, Customize View, Show Legend, Readout, X Axis, Y Axes, Reset View), in a single on-chart chip bar, or on a key while the chart has focus
- **On-Chart Chip Bar**: A compact overlay at the chart's top-left holding a one-click **View Mode** chip (labelled with the view it switches to) and a **Reset view** chip that appears only once the view is zoomed or an axis limit is set - so nothing sits there unused
- **Quick Plot Gestures**: Ctrl+drag or middle-drag rubber-bands a time range to zoom into it, double-click restores the full span, and a dashed crosshair follows the cursor for comparing series at the same instant
- **View Mode**: The context menu switches the left axis between Frame Sync Lock (%) and Accumulated Missed Frames; the per-stream visibility selection is preserved across the switch
- **Plot File Selector**: After a batch run, the context menu's **Plot File** submenu selects which processed file to view, or overlays them all (disabled when only one file is loaded)
- **Movable On-Plot Legend**: A translucent legend floats inside the chart (defaulting to the top-right, reset each session) and can be dragged clear of the data; it scrolls for dense plots (e.g. 48+ SNR channels) and is composited into exported images at its placed position. **Show Legend** on the right-click menu (or **L**) shows or hides it, and the choice persists between sessions
- **X-Axis Time Display**: Actual file time (DDD:HH:MM:SS) on the X axis instead of elapsed seconds
- **Export**: Export the current plot and its data from the ☰ menu (**Export…**) — choose any combination of CSV data (every series), a plot image (PNG/SVG/PDF, exactly what the chart shows, legend included), and the log window contents (text)
- **Hover Tooltip**: Shows series name, time (DDD:HH:MM:SS), and value (lock % or dB) on mouse hover

### Logging & Feedback

- **Inline Log Window**: Persistent, scrollable log with color-coded messages (green for success, goldenrod for warnings, red for errors)
- **Pre-Process Summary**: Logs the input file, the streams, the time range and each stream's averaging period before processing
- **File Logging**: Logs the file's size, time channels and PCM channels when a Chapter 10 file is opened, and each stream as it completes

### Application & UI

- **Single Hamburger Menu**: Every command lives in one ☰ menu in the window's title bar, grouped into Process, Import/Export, Settings, and Help sections
- **Custom Title Bar**: The window's own title bar with minimize/maximize/close buttons; native Windows drag, resize, snap layouts and double-click-maximize are preserved
- **Toggleable Sidebar**: Show or hide the log sidebar from the title bar or with **Ctrl+B**; the choice persists across sessions
- **Dark & Light Themes**: Fluent dark and light themes with runtime toggle (Settings section); every window, dialog and the plot follow it, and the menu icons swap per theme
- **Built-in User Manual**: An HTML user manual opens in the default browser from ☰ menu > Help > **User Manual…**
- **Keyboard Shortcuts**: Ctrl+O (open a file), Ctrl+B (toggle the sidebar), and on the focused chart the arrows, +/-, Home, Backspace, R, V and L
- **Tooltips**: Descriptive tooltips on plot controls and dialog fields

### Deployment

- **Installer & Portable Distribution**: A signed Inno Setup installer with admin/non-admin support that upgrades a v2.x install in place (settings files kept, preferences imported on the first run), a portable ZIP that keeps its preferences beside itself, and an optional `.ch10` file association

## System Requirements

- **Running it:** Windows 10 (1809) or later, x64. The installer and the portable ZIP carry everything the application
  needs; nothing else is installed first.
- **Building it:**
  - **.NET 10 SDK** - the application (`src/TmDq.App`, Avalonia 12) and the C# library over the native core
    (`src/TmDq.Core`).
  - **MSVC 2022** (Visual Studio 2022 C++ Build Tools, workload "Desktop development with C++", with the Windows SDK) -
    the native core, `tmdq_core.dll` (`src/core`, C++20, MSBuild).
  - **Inno Setup 6** - only for the installer.

## Building the Project

Everything is in `tmdq.sln`. From a PowerShell prompt at the repository root:

```powershell
# The native core, then its doctest suite
powershell -ExecutionPolicy Bypass -File eng\build_core_dll.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File eng\test_native.ps1 -Configuration Release

# The C# library's tests and the application's tests, against that DLL
powershell -ExecutionPolicy Bypass -File eng\test_dotnet.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File eng\test_app.ps1 -Configuration Release

# The release packages (signed; pass -SignCertSha1 '' for an unsigned build)
powershell -ExecutionPolicy Bypass -File deploy\avalonia\build_avalonia_package.ps1
```

The scripts find MSVC themselves (`eng\env.ps1`). `.claude/CLAUDE.md` and `docs/claude.md` describe each one, the
test baselines and the release process.

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
   - A progress dialog shows the run, and its **Cancel** stops every stream; the log fills in beside the chart, and the results are plotted when the run completes

## Project Structure

```text
tmDataQualityAnalyzer/
├── .claude/                    # Claude Code: the session guide (CLAUDE.md), shared settings, project skills
├── assets/                     # The base user manual (compiled into the app), the icon, the UTF-8 manifest
├── deploy/                     # Release packaging and its clean-machine test
│   ├── avalonia/               # build_avalonia_package.ps1, the Inno Setup script, signing
│   ├── sandbox/                # Windows Sandbox install-and-upgrade test (Smart App Control)
│   └── RELEASENOTES.txt        # The installer's "What's New" page
├── docs/                       # Developer guide (claude.md), changelog, logic notes, records, manual sources
├── eng/                        # Build, test, manual and release-check scripts, and the MSBuild props and targets
├── settings/                   # The shipped TOML settings: frame sync patterns, receiver parameters, step files
├── src/                        # The product code
│   ├── core/                   # The native core, tmdq_core.dll (C++20, MSBuild; no Qt)
│   │   ├── include/            # Its headers: constants.h, dto/, model/
│   │   ├── src/                # Chapter 10 reading, decommutation, calibration, file formats
│   │   └── abi/                # The C ABI the .NET side calls (tmdq_abi.h)
│   ├── TmDq.App/               # The Avalonia 12 application (MVVM, CommunityToolkit.Mvvm)
│   └── TmDq.Core/              # The C# binding of the native core's C ABI
├── tests/                      # Every test suite, and what they read
│   ├── core/                   # The native core's doctest suite
│   ├── TmDq.App.Tests/         # The application's tests, on Avalonia's headless platform (xunit v3)
│   ├── TmDq.Core.Tests/        # The C# library's tests, running the committed recordings through it
│   ├── data/                   # The committed .ch10 recordings
│   └── fixtures/               # Small files the suites read
├── third_party/                # irig106 (do NOT edit - adapt in the core) and doctest
├── UserManual.html             # The full illustrated manual, shipped beside the application
└── tmdq.sln                    # The solution: the native core, its tests, and the .NET projects
```

## Credits

This project incorporates code from the [irig106utils](https://github.com/atac/irig106utils) library for IRIG 106 Chapter 10 file handling. The user interface is built on [Avalonia](https://avaloniaui.net/) (MIT); every library the application ships is listed with its licence in `THIRD-PARTY-NOTICES.txt` beside it. Charting is first-party.

## License

GPL v3

## Contributing

[TBD]

## Support

Kevin Bossoletti
