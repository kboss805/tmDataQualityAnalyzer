/**
 * @file tomlconfighelper.h
 * @brief Custom QSettings format handler for TOML-style configuration files.
 *
 * Registers a QSettings::Format that reads and writes a simple TOML-compatible
 * subset: sections ([Header]), integer values, boolean values, and quoted
 * string values. This format is intentionally limited to the keys used by
 * tmDataQualityAnalyzer (no arrays, no inline tables, no multi-line strings).
 *
 * Usage:
 *   QSettings::Format fmt = TomlConfigHelper::format();
 *   QSettings s(path, fmt);
 */

#ifndef TOMLCONFIGHELPER_H
#define TOMLCONFIGHELPER_H

#include <QSettings>
/**
 * @brief Provides a QSettings-compatible TOML format for use throughout the application.
 *
 * Call TomlConfigHelper::format() once after QApplication is constructed (e.g. in main.cpp),
 * then pass the returned format token to any QSettings constructor that deals with .toml files.
 */
class TomlConfigHelper
{
public:
    /// Returns the registered custom QSettings format for TOML files.
    /// The format is registered lazily on first call.
    static QSettings::Format format();

    // Low-level read/write functions used by QSettings::registerFormat.
    // Public so they can be referenced as static function pointers.
    static bool readToml(QIODevice& device, QSettings::SettingsMap& map);
    static bool writeToml(QIODevice& device, const QSettings::SettingsMap& map);
};

#endif // TOMLCONFIGHELPER_H
