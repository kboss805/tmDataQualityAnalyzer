# Chapter 10 to CSV AGC Converter (tmDataQualityAnalyzer)

A desktop application for extracting, processing, and exporting multiplexed receiver Automatic Gain Control (AGC) signal samples from IRIG 106 Chapter 10 telemetry files. The exported data can be imported into 3rd party applications like Microsoft Excel and MATLAB for analyzing and plotting AGC signal samples.

## Overview

This application reads IRIG 106 Chapter 10 (.ch10) files, extracts telemetry data from specified channels, and exports AGC information to CSV format. It provides a user-friendly GUI for configuring data extraction parameters including time ranges, channel selection, and frame synchronization settings.

## Features

### File Input
- **Chapter 10 File Support**: Read and parse IRIG 106 Chapter 10 telemetry files
- **Channel Selection**: Choose specific time and PCM channels for data extraction
- **Drag-and-Drop**: Drop .ch10 files directly onto the application window
- **Recent Files Menu**: File > Recent Files with up to 5 entries, persisted across sessions
- **Batch Processing**: Multi-file selection and sequential queue-based batch processing with per-file channel selection, encoding detection, and color-coded status tracking

### Data Processing & Export
- **Time Range Filtering**: Specify start and stop times (Day of Year, Hour, Minute, Second)
- **Sample Rate Options**: 1 Hz, 10 Hz, or 100 Hz output sample rates
- **Frame Configuration**: Configure frame synchronization, randomization, and setup parameters
- **Automatic Pre-Scan**: Detects PCM encoding and verifies frame sync on file open and PCM channel change
- **AGC Processing**: Extract and process Automatic Gain Control data with V-to-dB conversion
- **Receiver Selection**: Enable/disable specific receivers and channels per receiver; Select All/None shortcuts
- **CSV Export**: Output processed data in CSV format with auto-generated timestamped filenames

### Settings & Configuration
- **Settings Management**: Save and load processing configurations from INI files
- **INI Validation**: Warns when parameter section count in INI files does not match receiver/channel configuration
- **Settings Summary Panel**: Read-only display of current frame sync, polarity, slope, scale, and receiver configuration
- **Per-File-Type Directory Persistence**: Independently remembers the last used directory for Ch10, CSV, and INI file dialogs between sessions

### Plot & Visualization
- **AGC Signal Plot Window**: Interactive QCustomPlot chart with mouse wheel zoom, click-drag pan, auto-scale axes, per-receiver-channel visibility toggles, and auto-assigned color palette
- **X-Axis Time Display**: Actual file time (DDD:HH:MM:SS) on the X axis instead of elapsed seconds
- **Plot PDF Export**: Export current plot to high-quality PDF file via QCustomPlot's built-in `savePdf()` method
- **Hover Tooltip**: Shows series name, time (DDD:HH:MM:SS), and dB value on mouse hover
- **Copy Data to Clipboard**: Copies the visible plot range as comma-separated values

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

2. **Select Channels**
   - Choose a Time Channel from the dropdown
   - Choose a PCM Channel from the dropdown

3. **Configure Frame Settings**
   - Open Settings dialog from the File menu
   - Configure frame sync pattern, polarity, scale, and range
   - Or load settings from an INI configuration file

4. **Configure Receivers**
   - Select receivers and channels to include in the export
   - Use the tree checkboxes to enable/disable individual channels

5. **Set Time Range**
   - Check "All" to process entire file
   - Or specify start/stop times (DDD HH:MM:SS format)
   - Select sample rate (1 Hz, 10 Hz, or 100 Hz)

6. **Process**
   - Click the play icon in the toolbar (or press **Ctrl+R**) to begin conversion
   - Choose output CSV file location
   - Monitor progress in the progress bar and log window

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

Kevin
