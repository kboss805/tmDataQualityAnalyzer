# Porting Plan: tmDataQualityAnalyzer to WinUI 3

This document outlines the architecture, environment setup, and step-by-step development plan for porting the application from Qt/C++ to a native Windows 11 Fluent Design stack using **WinUI 3 (C#)** for the frontend and a **C++ DLL** for the high-performance backend.

## 1. Environment Setup & Prerequisites

Before starting the port, you need to configure the development environment.

### IDE & Workloads
Install **Visual Studio 2022** (Community, Professional, or Enterprise) and ensure the following workloads are checked in the Visual Studio Installer:
1. **.NET Desktop Development** (for C#)
2. **Desktop Development with C++** (for compiling the backend DLL)
3. **Windows Application Development** (Critical: ensure the optional "Windows App SDK C# Templates" are checked).

### NuGet Packages (Dependencies)
Once the WinUI 3 C# project is created, you will replace the Qt ecosystem with the following C# libraries via the NuGet Package Manager:
- **`ScottPlot.WinUI`**: High-performance scientific charting (Replaces `QCustomPlot`).
- **`Tomlyn`**: Extremely fast and reliable TOML parser (Replaces `TomlConfigHelper`).
- **`CommunityToolkit.Mvvm`**: Microsoft's official toolkit for MVVM architecture. It auto-generates boilerplate code for UI data-binding (Replaces Qt's `QObject` signals and slots).

---

## 2. Target Architecture

You cannot directly mix C# UI code and native C++ code in the same project easily without massive overhead. The new architecture will be split into two distinct projects within a single Visual Studio Solution:

1. **The Core Engine (C++ DLL):** You will strip all Qt dependencies (`QString`, `QVector`, `QObject`) out of `Chapter10Reader`, `FrameProcessor`, and the `irig106utils` library. You will compile this pure C/C++ code into a Windows DLL.
2. **The WinUI 3 App (C# EXE):** The frontend handles all UI, user input, TOML configuration, and plotting. It uses **P/Invoke** (`[DllImport]`) to call functions in the C++ DLL.

> [!TIP]
> **Callbacks:** To get progress bar updates from the background C++ processing thread to the C# UI, you will pass a C# `delegate` (function pointer) into the C++ DLL. The C++ code will call this pointer to report `(progress_percentage, current_status)`.

---

## 3. Step-by-Step Development Plan

Pass this checklist to Claude (or your AI assistant) to execute the port in a logical order.

### Phase 1: The C++ Backend (The Core Engine)
1. **Create the Project:** Create a new "Dynamic-Link Library (DLL) - C++" project in Visual Studio named `TmDataCore`.
2. **Port irig106utils:** Copy the `irig106utils` C files and headers into the project. Ensure they compile.
3. **Strip Qt:** Copy `chapter10reader.cpp/h` and `frameprocessor.cpp/h`. Replace all Qt types (`QString` → `std::string`, `QVector` → `std::vector`, `QFile` → `std::ifstream`). Remove all Qt Signal/Slot macros.
4. **Define the C-API:** Create a new header file `api.h` wrapped in `extern "C" __declspec(dllexport)`. Define clean C-style functions that C# can call:
   - `int OpenFile(const char* filepath)`
   - `int GetChannelCount()`
   - `void ProcessData(const char* filepath, const char* outpath, void (*progressCallback)(int))`

### Phase 2: The WinUI 3 Frontend Foundation
1. **Create the Project:** Create a new "Blank App, Packaged (WinUI 3 in Desktop)" project in C# named `TmDataAnalyzer`.
2. **Setup Dependencies:** Install `CommunityToolkit.Mvvm`, `ScottPlot.WinUI`, and `Tomlyn` via NuGet.
3. **P/Invoke Interop:** Create a `NativeMethods.cs` class. Write the `[DllImport("TmDataCore.dll")]` signatures to match the C-API you defined in Phase 1.
4. **MVVM Setup:** Create a `MainViewModel.cs` inheriting from `ObservableObject`. Set up basic commands using `[RelayCommand]` for actions like "Open File" and "Process".

### Phase 3: UI Layout & Configuration
1. **XAML Porting:** Translate the Qt UI layouts into XAML.
   - Replace Qt `QHBoxLayout`/`QVBoxLayout` with XAML `StackPanel` and `Grid`.
   - Replace `QTreeWidget` with WinUI `TreeView`.
   - Use the `ThemeResource` system to automatically support Dark/Light modes.
2. **TOML Configuration:** Write a new `ConfigManager.cs` using `Tomlyn` to load/save the `default_rcvr_params.toml` and frame sync settings. Bind these settings to the XAML UI.
3. **File Dialogs:** Implement the `Windows.Storage.Pickers.FileOpenPicker` to allow users to select `.ch10` files.

### Phase 4: Threading and Execution
1. **Async Processing:** When the user clicks "Process", launch a background task using `Task.Run()`. 
2. **Invoke the DLL:** Inside the task, call the P/Invoke `ProcessData()` function.
3. **UI Updates:** Ensure the progress callback passed to C++ marshals updates back to the UI thread using the `DispatcherQueue.TryEnqueue()` so the XAML Progress Bar updates smoothly without crashing.

### Phase 5: Charting & Polish
1. **Integrate ScottPlot:** Add the `<WinUI:WinUIPlot/>` control to the right-hand panel of your main XAML page.
2. **Load Data:** Once the C++ DLL finishes writing the processed CSV file, have C# read the CSV into `double[]` arrays.
3. **Plot Data:** Pass the arrays to ScottPlot using `Plot.Add.ScatterLine()`. Configure the left Y-axis for "Lock %" (0-100) and an additional right Y-axis for "SNR (dB)".
4. **Mica Backdrop:** Enable the native Windows 11 Mica material in the `MainWindow.xaml.cs` to give the app that authentic, translucent Windows feel.
