# Chapter 10 to CSV AGC Converter (tmDataQualityAnalyzer)

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
- **Drag-and-Drop**: Drop .ch10 files directly onto the application window or select via File > Open
- **Recent Files Menu**: File > Recent Files with up to 5 entries, persisted across sessions

### Frame Sync Lock
- **Lock Percentage Over Time**: Measures per-stream frame synchronization lock and plots it against actual file time
- **Acquire / Lock Scanner**: Bit-serial sync scanner that confirms a configurable number of in-phase syncs before declaring lock, then drops and re-acquires lock when a true sync is missed
- **PRN-Robust Matching**: While locked, off-phase sync matches produced by pseudo-random data words are rejected so they cannot corrupt the lock figure (eliminates spurious lock dips on clean randomized streams)
- **Bit-Span Lock Metric**: Lock % is computed from the actual number of frame-sized bit slots traversed versus frames detected; time is used only to delimit the averaging window, so the figure does not depend on an exact data rate
- **Configurable Frame Parameters**: Frame sync pattern (hex), sync mask (hex), bits per frame, RNRZ-L randomization on/off, data rate (Mbps, or Auto from TMATS), and averaging window (1/10/100 Hz)

### Receiver AGC (SNR) Processing
- **Time Range Filtering**: Specify start and stop times (Day of Year, Hour, Minute, Second)
- **Averaging Window**: 1 Hz, 10 Hz, or 100 Hz output sample rates
- **AGC Processing**: Extract calibrated receiver-channel AGC/SNR values with V-to-dB conversion
- **Calibration**: Configurable polarity, voltage slope/range, scale (dB/V), receiver count, and channels per receiver
- **Receiver Parameters TOML**: Word map and calibration loaded from a TOML configuration file

### Multi-Stream Processing
- **Per-Stream Configuration**: One row per PCM channel in the Configure Streams dialog, each independently set to Frame Sync Lock or Receiver SNR mode with its own setup
- **Concurrent Processing**: A single reader thread reads the file once and routes packets to per-stream queues; each stream is processed on its own worker thread in parallel
- **Single-Pass I/O**: The Chapter 10 file is read exactly once regardless of how many streams are selected
- **Accumulating Plot**: Each stream's results are added to the plot as it completes
- **Cancellation**: A cancel cleanly stops the reader and all worker threads

### Settings & Configuration
- **TOML Configuration**: Save and load frame sync fields and receiver parameters from TOML files
- **Per-File-Type Directory Persistence**: Independently remembers the last used directory for Ch10 and TOML file dialogs between sessions

### Plot & Visualization
- **Interactive Plot Window**: Plot showing frame sync lock (%) and/or receiver AGC (dB) series with mouse wheel zoom, click-drag pan, auto-scale axes, per-series visibility toggles, and auto-assigned color palette
- **Fixed-Layout Legend**: Series controls below the plot use a constant 3-column grid (1 column for Frame Sync Lock groups, 2 columns for receiver groups), so the layout doesn't shift between files; columns scroll vertically once they exceed the visible row count
- **X-Axis Time Display**: Actual file time (DDD:HH:MM:SS) on the X axis instead of elapsed seconds
- **Toolbar Export**: Export the current plot and its data (CSV plus PNG/SVG/PDF) via the Export action in the main toolbar
- **Hover Tooltip**: Shows series name, time (DDD:HH:MM:SS), and value (lock % or dB) on mouse hover

### Logging & Feedback
- **Inline Log Window**: Persistent, scrollable log with color-coded messages (green for success, yellow for warnings, red for errors)
- **Status Bar**: Displays file metadata summary (filename, size, channel counts, time range)
- **Pre-Process Summary**: Logs input file, channels, time range, sample rate, receiver count, and output path before processing
- **Startup & File Logging**: Logs default.ini settings at startup, channel/time/frame info when opening Ch10 files, and INI validation details when loading settings
- **Clickable Log Links**: Output file path and "Open Folder" links in the log window after processing completes

### Application & UI
- **Dark & Light Themes**: Windows 11 / WinUI 3 styled dark and light themes with runtime toggle
- **Keyboard Shortcuts**: Ctrl+O (Open file), Ctrl+R (Process), Ctrl+E (Expand/collapse all plot legend receivers)
- **Collapsible Panels**: Receivers and Time Controls sections collapse/expand to reduce visual clutter
- **Tooltips**: Descriptive tooltips on all plot controls (spinboxes, buttons, title field)
- **Busy Cursor**: Hourglass cursor shown during file processing

### Deployment
- **Installer & Portable Distribution**: Inno Setup EXE installer with admin/non-admin support, portable ZIP with local settings, INI upgrade logic, and optional `.ch10` file association

## System Requirements

### Software
- **Qt**: Version 6.0.0 or later (developed on 6.10.2)
- **Compiler**: GCC/MinGW 7.0+ (developed on MinGW 13.1.0 64-bit)
- **C++ Standard**: C++17 required
- **Operating System**: Windows (primary), Linux/macOS (may require adjustments)

### Build Tools
- qmake (Qt build system)
- MinGW or compatible GCC toolchain

## Building the Project

### Using Qt Creator
1. Open `tmDataQualityAnalyzer.pro` in Qt Creator
2. Configure the project with your Qt kit
3. Build and run (Ctrl+R)

### Using Command Line (Windows with MinGW)
```bash
# Create and enter the build directory
mkdir build
cd build

# Configure the project (run from the build directory)
qmake ../tmDataQualityAnalyzer.pro -spec win32-g++

# Build debug version
mingw32-make -f Makefile.Debug

# Build release version
mingw32-make -f Makefile.Release

# Run the application
debug\tmDataQualityAnalyzer.exe
```

## Usage

1. **Load Input File**
   - Click the folder icon in the toolbar (or press **Ctrl+O**) to browse for a Chapter 10 (.ch10) file
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

4. **Set Time Range**
   - In the Time Controls section, check **All** to process the entire file
   - Or specify start/stop times (DDD HH:MM:SS format)

5. **Process**
   - Click **OK** to store the stream configuration
   - Click the play icon in the toolbar (or press **Ctrl+R**) to run all enabled streams concurrently
   - Monitor progress in the progress bar and log window; each stream is added to the plot as it completes

## Project Structure

```
tmDataQualityAnalyzer/
├── build/                      # Out-of-source build artifacts
├── deploy/                     # Build automation, installer, and release packaging
├── docs/                       # Project documentation and requirements
├── include/                    # Header files
│   ├── mainview.h
│   ├── receivergridwidget.h
│   ├── timeextractionwidget.h
│   ├── settingsdialog.h
│   ├── mainviewmodel.h
│   ├── chapter10reader.h
│   ├── frameprocessor.h
│   ├── framesetup.h
│   ├── channeldata.h
│   ├── settingsmanager.h
│   ├── settingsdata.h
│   ├── batchfileinfo.h
│   ├── plotviewmodel.h
│   ├── plotwidget.h
│   └── constants.h
├── lib/irig106/                # Third-party IRIG 106 library
│   ├── src/                   # irig106utils C source files
│   └── include/               # irig106utils C header files
├── lib/qcustomplot/            # Third-party QCustomPlot 2.1.1 charting library
├── deploy/                     # Build automation, installer, and release packaging
├── tests/                      # Qt Test framework unit tests
├── settings/                   # Default and user settings files
├── resources/                  # Resources (stylesheets, icons, rc)
│   ├── tmDataQualityAnalyzer_resource.rc # Windows resource file
│   ├── win11-dark.qss         # Dark theme stylesheet
│   ├── win11-light.qss        # Light theme stylesheet
│   ├── chevron-down-*.svg     # Combo box dropdown arrow icons (dark/light/disabled)
│   ├── chevron-right-*.svg    # Collapsible section arrow icons (dark/light)
│   ├── checkmark.svg          # Checkbox checkmark icon
│   ├── folder-open.svg        # Toolbar open icon
│   ├── play.svg               # Toolbar process icon
│   ├── stop.svg               # Toolbar cancel/stop icon
│   ├── gear.svg               # Toolbar settings icon
│   ├── retry.svg              # Toolbar retry-failed icon
│   ├── export.svg             # Toolbar plot export icon
│   └── icon.ico               # Application icon
├── scripts/                    # Build and utility scripts
│   ├── build_ide.ps1          # IDE/VS Code test build helper (reads QTDIR/MINGW_DIR from env)
│   ├── env.bat                # Developer environment PATH setup helper
│   └── setup-env.ps1          # One-time Windows user environment variable registration
├── src/                        # Source files
│   ├── main.cpp               # Application entry point
│   ├── mainview.cpp           # Main GUI window (View)
│   ├── receivergridwidget.cpp # Receiver/channel selection grid (View)
│   ├── timeextractionwidget.cpp # Time range and sample rate controls (View)
│   ├── settingsdialog.cpp     # Settings dialog (View)
│   ├── mainviewmodel.cpp      # Application logic (ViewModel)
│   ├── chapter10reader.cpp    # Chapter 10 file metadata (Model)
│   ├── frameprocessor.cpp     # PCM frame extraction and CSV output (Model)
│   ├── framesetup.cpp         # Frame configuration parameters (Model)
│   ├── channeldata.cpp        # Channel metadata (Model)
│   ├── settingsmanager.cpp    # Settings persistence (Model)
│   ├── plotviewmodel.cpp      # Plot data parsing and axis management (ViewModel)
│   └── plotwidget.cpp         # QCustomPlot chart widget (View)
├── tmDataQualityAnalyzer.pro   # Qt project file
└── README.md                   # This file
```

## Credits

This project incorporates code from the [irig106utils](https://github.com/atac/irig106utils) library for IRIG 106 Chapter 10 file handling and [QCustomPlot](https://www.qcustomplot.com/) 2.1.1 for interactive charting.

## License

GPL v3

## Contributing

[TBD]

## Support

Kevin Bossoletti
