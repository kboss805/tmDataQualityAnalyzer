/**
 * @file tomlconfighelper.cpp
 * @brief Custom QSettings read/write callbacks for a simple TOML-compatible format.
 *
 * Supported TOML subset (sufficient for tmDataQualityAnalyzer .toml files):
 *   - Section headers:     [SectionName]
 *   - Integer values:      Key = 42
 *   - Boolean values:      Key = true  /  Key = false
 *   - Quoted string vals:  Key = "some text"
 *   - Unquoted string vals for compatibility: Key = some text
 *   - Comments:            # comment
 *   - Blank lines:         ignored
 *
 * QSettings internally stores keys as "Section/Key" strings and retrieves them
 * via beginGroup/endGroup. The read callback builds a SettingsMap with those
 * compound keys. The write callback reconstructs sections from the map and
 * emits valid TOML syntax.
 */

#include "tomlconfighelper.h"

#include <QIODevice>
#include <QMap>
#include <QTextStream>

// ---------------------------------------------------------------------------
// format()
// ---------------------------------------------------------------------------

QSettings::Format TomlConfigHelper::format()
{
    static QSettings::Format s_format = QSettings::registerFormat(
        "toml",
        &TomlConfigHelper::readToml,
        &TomlConfigHelper::writeToml
    );
    return s_format;
}

// ---------------------------------------------------------------------------
// readToml()
// ---------------------------------------------------------------------------

bool TomlConfigHelper::readToml(QIODevice& device, QSettings::SettingsMap& map)
{
    QTextStream in(&device);
    QString current_section;

    while (!in.atEnd())
    {
        QString raw = in.readLine();
        QString line = raw.trimmed();

        // Skip blank lines and comments
        if (line.isEmpty() || line.startsWith('#'))
        {
            continue;
        }

        // Section header: [SectionName]
        if (line.startsWith('[') && line.endsWith(']'))
        {
            current_section = line.mid(1, line.length() - 2).trimmed();
            continue;
        }

        // Key = Value
        int eq_idx = line.indexOf('=');
        if (eq_idx < 0)
        {
            continue;  // Malformed line — skip
        }

        QString key   = line.left(eq_idx).trimmed();
        QString value = line.mid(eq_idx + 1).trimmed();

        // Strip inline comment (# after value, outside quotes)
        {
            bool in_quote = false;
            for (int i = 0; i < value.length(); ++i)
            {
                if (value[i] == '"')
                {
                    in_quote = !in_quote;
                }
                else if (!in_quote && value[i] == '#')
                {
                    value = value.left(i).trimmed();
                    break;
                }
            }
        }

        // Strip surrounding double quotes from string values
        if (value.length() >= 2 && value.startsWith('"') && value.endsWith('"'))
        {
            value = value.mid(1, value.length() - 2);
        }

        // Build compound key: "Section/Key" (QSettings convention)
        QString compound_key = current_section.isEmpty()
            ? key
            : (current_section + "/" + key);

        map.insert(compound_key, QVariant(value));
    }

    return true;
}

// ---------------------------------------------------------------------------
// writeToml()
// ---------------------------------------------------------------------------

bool TomlConfigHelper::writeToml(QIODevice& device, const QSettings::SettingsMap& map)
{
    QTextStream out(&device);

    // Group keys by section so we can emit one [Header] per section.
    // Keys with no section (top-level) go into an empty-section bucket.
    QMap<QString, QMap<QString, QVariant>> sections;

    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        const QString& compound = it.key();
        int slash = compound.indexOf('/');
        if (slash < 0)
        {
            // Top-level key (no section)
            sections[""][compound] = it.value();
        }
        else
        {
            QString section = compound.left(slash);
            QString key     = compound.mid(slash + 1);
            sections[section][key] = it.value();
        }
    }

    // Emit top-level keys first (if any), then sectioned groups
    bool first_section = true;
    for (auto sec_it = sections.constBegin(); sec_it != sections.constEnd(); ++sec_it)
    {
        const QString& section = sec_it.key();
        const QMap<QString, QVariant>& keys = sec_it.value();

        if (!first_section)
        {
            out << "\n";
        }
        first_section = false;

        if (!section.isEmpty())
        {
            out << "[" << section << "]\n";
        }

        for (auto key_it = keys.constBegin(); key_it != keys.constEnd(); ++key_it)
        {
            const QString& key = key_it.key();
            const QVariant& val = key_it.value();

            // Choose TOML value representation:
            //   - true/false booleans → unquoted
            //   - pure integer strings → unquoted integer
            //   - everything else → double-quoted string
            QString str_val = val.toString();

            if (str_val == "true" || str_val == "false")
            {
                // Boolean
                out << key << " = " << str_val << "\n";
            }
            else
            {
                bool is_int = false;
                str_val.toLongLong(&is_int);
                if (is_int)
                {
                    // Integer
                    out << key << " = " << str_val << "\n";
                }
                else
                {
                    // String — double-quote, escape any embedded quotes
                    str_val.replace("\\", "\\\\").replace("\"", "\\\"");
                    out << key << " = \"" << str_val << "\"\n";
                }
            }
        }
    }

    out.flush();
    return true;
}
