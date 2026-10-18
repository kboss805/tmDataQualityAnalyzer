/**
 * @file qtbridge.h
 * @brief Conversions at the boundary between Qt types and the de-Qt'd core.
 *
 * The core is moving to UTF-8 `std::string` for text and `std::filesystem::path` for
 * paths (the de-Qt sweep; see docs/future_plans/dotnet-port-assessment.md). Code that
 * still holds a `QString` converts at its own boundary:
 *
 *   - text:  `QString::toStdString()` / `QString::fromStdString()`, which are UTF-8 in
 *            Qt 6, so no helper is needed;
 *   - paths: `toFsPath()` below. A path goes through UTF-16 (`toStdWString()` is UTF-16
 *            on Windows), which is lossless for any name the file system can hold -
 *            unlike a trip through a narrow string in the ANSI code page.
 *
 * This header includes Qt. It is for the View, the ViewModel and model code not yet
 * converted - never for a header the core is meant to be free of Qt in.
 */

#ifndef QTBRIDGE_H
#define QTBRIDGE_H

#include <filesystem>

#include <QString>

/// A QString path as a std::filesystem::path, losslessly.
inline std::filesystem::path toFsPath(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

#endif // QTBRIDGE_H
