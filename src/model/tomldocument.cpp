/**
 * @file tomldocument.cpp
 * @brief Plain reader/writer for the TOML subset the settings files use.
 *
 * See tomldocument.h for why this is not a QSettings::Format any more.
 */

#include "tomldocument.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {

/// Removes an unquoted trailing `# comment` from a value.
///
/// Quote-aware: a '#' inside a double-quoted string is part of the value, which
/// matters because sync patterns and scale values are quoted.
QString stripInlineComment(const QString& value)
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
            return value.left(i).trimmed();
        }
    }
    return value;
}

/// Strips surrounding double quotes and unescapes \\" and \\\\ inside them.
///
/// The unescape is new. The QSettings-format writer escaped both on the way out but
/// its reader only stripped the surrounding quotes, so a value containing either
/// character did not survive a round trip. No shipped key carries one - the TOML files
/// hold hex patterns, integers, booleans and a scale - so this corrects a latent
/// asymmetry rather than changing any real file's meaning. Writing fresh code that
/// reproduced it on purpose seemed worse.
QString unquote(const QString& value)
{
    if (value.length() < 2 || !value.startsWith('"') || !value.endsWith('"'))
    {
        return value;
    }

    const QString inner = value.mid(1, value.length() - 2);
    QString out;
    out.reserve(inner.length());
    for (int i = 0; i < inner.length(); ++i)
    {
        if (inner[i] == '\\' && i + 1 < inner.length() &&
            (inner[i + 1] == '"' || inner[i + 1] == '\\'))
        {
            out.append(inner[++i]);
        }
        else
        {
            out.append(inner[i]);
        }
    }
    return out;
}

/// Chooses the TOML representation for a stored value, mirroring what the shipped
/// files look like: bare for booleans and integers, quoted for anything else.
QString formatValue(const QString& raw)
{
    if (raw == QLatin1String("true") || raw == QLatin1String("false"))
    {
        return raw;
    }

    bool is_int = false;
    raw.toLongLong(&is_int);
    if (is_int)
    {
        return raw;
    }

    QString escaped = raw;
    escaped.replace('\\', QLatin1String("\\\\"));
    escaped.replace('"', QLatin1String("\\\""));
    return '"' + escaped + '"';
}

} // namespace

// ---------------------------------------------------------------------------
// key handling
// ---------------------------------------------------------------------------

void TomlDocument::splitKey(const QString& key, QString& section, QString& bare)
{
    const int slash = key.indexOf('/');
    if (slash < 0)
    {
        section.clear();
        bare = key;
        return;
    }
    section = key.left(slash);
    bare    = key.mid(slash + 1);
}

void TomlDocument::rememberOrder(const QString& section, const QString& bare)
{
    // Only real sections join the file-order list: sectionsInFileOrder() is walked by
    // FrameSetup to find word-map groups, and an unnamed bucket standing for
    // "top-level keys" is not a group anyone is looking for. save() emits that bucket
    // from m_section_keys directly instead.
    if (!section.isEmpty() && !m_sections.contains(section))
    {
        m_sections.append(section);
    }
    QStringList& keys = m_section_keys[section];
    if (!keys.contains(bare))
    {
        keys.append(bare);
    }
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------

TomlDocument TomlDocument::load(const QString& path, QString* error)
{
    TomlDocument doc;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        doc.m_ok = false;
        if (error)
        {
            *error = QStringLiteral("Could not open '%1'.").arg(QFileInfo(path).fileName());
        }
        return doc;
    }

    QTextStream in(&file);
    QString current_section;

    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();

        if (line.isEmpty() || line.startsWith('#'))
        {
            continue;
        }

        if (line.startsWith('[') && line.endsWith(']'))
        {
            // One bracket pair is stripped, exactly as the QSettings format did, so a
            // `[[Step]]` header reads as a section literally named "[Step]". This class
            // does NOT implement array-of-tables; StepDetector::parseStepConfig owns
            // that, and pretending otherwise here would invite a caller to rely on it.
            current_section = line.mid(1, line.length() - 2).trimmed();
            // A section with no keys still counts as declared: the receiver word map's
            // [EMPTY] placeholders exist precisely so the Word numbering reads as
            // contiguous, and tryLoadingFile() walks this list to find them.
            if (!current_section.isEmpty() && !doc.m_sections.contains(current_section))
            {
                doc.m_sections.append(current_section);
            }
            continue;
        }

        const int eq = line.indexOf('=');
        if (eq < 0)
        {
            continue;  // malformed line - skipped, as before
        }

        const QString bare  = line.left(eq).trimmed();
        const QString value = unquote(stripInlineComment(line.mid(eq + 1).trimmed()));
        const QString compound = current_section.isEmpty()
            ? bare
            : current_section + '/' + bare;

        // A repeated section's repeated key overwrites, which is what the QSettings
        // map did too. Only [EMPTY] repeats in practice, and tryLoadingFile() skips it
        // by name, so no value anyone reads is lost this way.
        doc.m_values.insert(compound, value);
        doc.rememberOrder(current_section, bare);
    }

    return doc;
}

// ---------------------------------------------------------------------------
// reading
// ---------------------------------------------------------------------------

bool TomlDocument::contains(const QString& key) const
{
    return m_values.contains(key);
}

QString TomlDocument::string(const QString& key, const QString& fallback) const
{
    const auto it = m_values.constFind(key);
    return it == m_values.constEnd() ? fallback : *it;
}

int TomlDocument::integer(const QString& key, int fallback, bool* parsed) const
{
    const auto it = m_values.constFind(key);
    if (it == m_values.constEnd())
    {
        if (parsed) { *parsed = false; }
        return fallback;
    }
    bool ok = false;
    const int v = it->toInt(&ok);
    if (parsed) { *parsed = ok; }
    // One DELIBERATE difference from the QSettings implementation: a present but
    // unparseable value returns the caller's fallback, where QVariant::toInt() returned
    // 0. That silently turned a corrupt Polarity or Slope into index 0 rather than into
    // the default the caller passed, which is never what any call site here wants.
    return ok ? v : fallback;
}

double TomlDocument::number(const QString& key, double fallback) const
{
    const auto it = m_values.constFind(key);
    if (it == m_values.constEnd())
    {
        return fallback;
    }
    bool ok = false;
    const double v = it->toDouble(&ok);
    return ok ? v : fallback;
}

bool TomlDocument::boolean(const QString& key, bool fallback) const
{
    const auto it = m_values.constFind(key);
    if (it == m_values.constEnd())
    {
        return fallback;
    }
    // Deliberately QVariant::toBool()'s exact rule, which is what the QSettings
    // implementation went through: lowercase, then false only for empty, "0" or
    // "false" - anything else is true. Reproduced rather than tightened so this
    // refactor cannot change how any existing file reads. A stricter rule (unknown
    // text falls back instead of reading as true) would be an improvement, but it is
    // a separate decision from moving off QSettings.
    const QString v = it->trimmed().toLower();
    return !(v.isEmpty() || v == QLatin1String("0") || v == QLatin1String("false"));
}

// ---------------------------------------------------------------------------
// writing
// ---------------------------------------------------------------------------

void TomlDocument::setValue(const QString& key, const QString& value)
{
    QString section, bare;
    splitKey(key, section, bare);
    m_values.insert(key, value);
    rememberOrder(section, bare);
}

void TomlDocument::setValue(const QString& key, int value)
{
    setValue(key, QString::number(value));
}

void TomlDocument::setValue(const QString& key, double value)
{
    setValue(key, QString::number(value));
}

void TomlDocument::setValue(const QString& key, bool value)
{
    setValue(key, value ? QStringLiteral("true") : QStringLiteral("false"));
}

bool TomlDocument::save(const QString& path, QString* error) const
{
    // Truncate rather than merge. The QSettings implementation merged into whatever
    // the file already held, so its callers had to QFile::remove() first or an old
    // word map survived beside the new one.
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (error)
        {
            *error = QStringLiteral("Could not write '%1'.").arg(QFileInfo(path).fileName());
        }
        return false;
    }

    QTextStream out(&file);
    bool first = true;

    // Top-level keys first, with no header, so they stay top-level on re-read.
    const QStringList top_level = m_section_keys.value(QString());
    if (!top_level.isEmpty())
    {
        for (const QString& bare : top_level)
        {
            out << bare << " = " << formatValue(m_values.value(bare)) << "\n";
        }
        first = false;
    }

    for (const QString& section : m_sections)
    {
        if (!first)
        {
            out << "\n";
        }
        first = false;

        out << '[' << section << "]\n";
        for (const QString& bare : m_section_keys.value(section))
        {
            out << bare << " = " << formatValue(m_values.value(section + '/' + bare)) << "\n";
        }
    }

    out.flush();
    file.close();

    if (file.error() != QFile::NoError)
    {
        if (error)
        {
            *error = QStringLiteral("Error writing '%1': %2")
                        .arg(QFileInfo(path).fileName(), file.errorString());
        }
        return false;
    }
    return true;
}
