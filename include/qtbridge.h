/**
 * @file qtbridge.h
 * @brief Conversions at the boundary between Qt types and the de-Qt'd core.
 *
 * The core uses UTF-8 `std::string` for text and `std::filesystem::path` for paths
 * (see docs/future_plans/dotnet-port-assessment.md). Code that holds a `QString`
 * converts at its own boundary:
 *
 *   - text:  `QString::toStdString()` / `QString::fromStdString()`, which are UTF-8 in
 *            Qt 6, so no helper is needed;
 *   - paths: `toFsPath()` below. A path goes through UTF-16 (`toStdWString()` is UTF-16
 *            on Windows), which is lossless for any name the file system can hold -
 *            unlike a trip through a narrow string in the ANSI code page.
 *
 * This header includes Qt. It is for the View, the ViewModel and tests - never for a
 * header the core is free of Qt in.
 */

#ifndef QTBRIDGE_H
#define QTBRIDGE_H

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

/// A QString path as a std::filesystem::path, losslessly.
inline std::filesystem::path toFsPath(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

/// A list of UTF-8 strings as a QStringList.
inline QStringList toQStringList(const std::vector<std::string>& values)
{
    QStringList list;
    list.reserve(static_cast<qsizetype>(values.size()));
    for (const std::string& value : values)
    {
        list.append(QString::fromStdString(value));
    }
    return list;
}

/// (id, UTF-8 name) pairs as the (id, QString) list the dialogs and the template matcher take.
inline QList<QPair<int, QString>> toQPairList(const std::vector<std::pair<int, std::string>>& values)
{
    QList<QPair<int, QString>> list;
    list.reserve(static_cast<qsizetype>(values.size()));
    for (const auto& value : values)
    {
        list.append({value.first, QString::fromStdString(value.second)});
    }
    return list;
}

#endif // QTBRIDGE_H
