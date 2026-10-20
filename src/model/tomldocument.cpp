/**
 * @file tomldocument.cpp
 * @brief Plain reader/writer for the TOML subset the settings files use.
 *
 * See tomldocument.h for why this is not a QSettings::Format any more, and for what the
 * port off QString reproduces. TestTomlDocument pins every behaviour below; it was
 * written against the Qt implementation first and passed there before this replaced it.
 */

#include "tomldocument.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>

#include "textfile.h"
#include "textparse.h"

namespace {

using textparse::parseDouble;
using textparse::parseInteger;
using textparse::trimmed;

bool startsWith(const std::string& s, char c)
{
    return !s.empty() && s.front() == c;
}

bool endsWith(const std::string& s, char c)
{
    return !s.empty() && s.back() == c;
}

std::string asciiLower(std::string s)
{
    for (char& c : s)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

/// Removes an unquoted trailing `# comment` from a value.
///
/// Quote-aware: a '#' inside a double-quoted string is part of the value, which
/// matters because sync patterns and scale values are quoted. Safe on UTF-8: the bytes
/// of a multi-byte sequence are never ASCII, so they cannot be mistaken for '"' or '#'.
std::string stripInlineComment(const std::string& value)
{
    bool in_quote = false;
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '"')
        {
            in_quote = !in_quote;
        }
        else if (!in_quote && value[i] == '#')
        {
            return trimmed(value.substr(0, i));
        }
    }
    return value;
}

/// Strips surrounding double quotes and unescapes \" and \\ inside them.
///
/// The unescape is new. The QSettings-format writer escaped both on the way out but
/// its reader only stripped the surrounding quotes, so a value containing either
/// character did not survive a round trip. No shipped key carries one - the TOML files
/// hold hex patterns, integers, booleans and a scale - so this corrects a latent
/// asymmetry rather than changing any real file's meaning. Writing fresh code that
/// reproduced it on purpose seemed worse.
std::string unquote(const std::string& value)
{
    if (value.size() < 2 || !startsWith(value, '"') || !endsWith(value, '"'))
    {
        return value;
    }

    const std::string inner = value.substr(1, value.size() - 2);
    std::string out;
    out.reserve(inner.size());
    for (std::size_t i = 0; i < inner.size(); ++i)
    {
        if (inner[i] == '\\' && i + 1 < inner.size() &&
            (inner[i + 1] == '"' || inner[i + 1] == '\\'))
        {
            out += inner[++i];
        }
        else
        {
            out += inner[i];
        }
    }
    return out;
}

/// Chooses the TOML representation for a stored value, mirroring what the shipped
/// files look like: bare for booleans and integers, quoted for anything else.
std::string formatValue(const std::string& raw)
{
    if (raw == "true" || raw == "false")
    {
        return raw;
    }

    long long as_int = 0;
    if (parseInteger(raw, as_int))
    {
        return raw;
    }

    std::string escaped;
    escaped.reserve(raw.size() + 2);
    escaped += '"';
    for (const char c : raw)
    {
        if (c == '\\' || c == '"')
        {
            escaped += '\\';
        }
        escaped += c;
    }
    escaped += '"';
    return escaped;
}

} // namespace

// ---------------------------------------------------------------------------
// key handling
// ---------------------------------------------------------------------------

void TomlDocument::splitKey(const std::string& key, std::string& section, std::string& bare)
{
    const std::size_t slash = key.find('/');
    if (slash == std::string::npos)
    {
        section.clear();
        bare = key;
        return;
    }
    section = key.substr(0, slash);
    bare    = key.substr(slash + 1);
}

void TomlDocument::rememberOrder(const std::string& section, const std::string& bare)
{
    // Only real sections join the file-order list: sectionsInFileOrder() is walked by
    // FrameSetup to find word-map groups, and an unnamed bucket standing for
    // "top-level keys" is not a group anyone is looking for. save() emits that bucket
    // from m_section_keys directly instead.
    if (!section.empty()
        && std::find(m_sections.begin(), m_sections.end(), section) == m_sections.end())
    {
        m_sections.push_back(section);
    }
    std::vector<std::string>& keys = m_section_keys[section];
    if (std::find(keys.begin(), keys.end(), bare) == keys.end())
    {
        keys.push_back(bare);
    }
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------

TomlDocument TomlDocument::load(const std::filesystem::path& path, std::string* error)
{
    TomlDocument doc;

    std::string text;
    if (!textfile::readText(path, text))
    {
        doc.m_ok = false;
        if (error)
        {
            *error = "Could not open '" + textfile::utf8FileName(path) + "'.";
        }
        return doc;
    }

    std::istringstream in(text);
    std::string raw_line;
    std::string current_section;

    while (std::getline(in, raw_line))
    {
        const std::string line = trimmed(raw_line);

        if (line.empty() || startsWith(line, '#'))
        {
            continue;
        }

        if (startsWith(line, '[') && endsWith(line, ']'))
        {
            // One bracket pair is stripped, exactly as the QSettings format did, so a
            // `[[Step]]` header reads as a section literally named "[Step]". This class
            // does NOT implement array-of-tables; StepDetector::parseStepConfig owns
            // that, and pretending otherwise here would invite a caller to rely on it.
            current_section = trimmed(line.substr(1, line.size() - 2));
            // A section with no keys still counts as declared: the receiver word map's
            // [EMPTY] placeholders exist precisely so the Word numbering reads as
            // contiguous, and tryLoadingFile() walks this list to find them.
            if (!current_section.empty()
                && std::find(doc.m_sections.begin(), doc.m_sections.end(), current_section)
                       == doc.m_sections.end())
            {
                doc.m_sections.push_back(current_section);
            }
            continue;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;  // malformed line - skipped, as before
        }

        const std::string bare  = trimmed(line.substr(0, eq));
        const std::string value = unquote(stripInlineComment(trimmed(line.substr(eq + 1))));
        const std::string compound = current_section.empty()
            ? bare
            : current_section + '/' + bare;

        // A repeated section's repeated key overwrites, which is what the QSettings
        // map did too. Only [EMPTY] repeats in practice, and tryLoadingFile() skips it
        // by name, so no value anyone reads is lost this way.
        doc.m_values[compound] = value;
        doc.rememberOrder(current_section, bare);
    }

    return doc;
}

// ---------------------------------------------------------------------------
// reading
// ---------------------------------------------------------------------------

bool TomlDocument::contains(const std::string& key) const
{
    return m_values.find(key) != m_values.end();
}

std::string TomlDocument::string(const std::string& key, const std::string& fallback) const
{
    const auto it = m_values.find(key);
    return it == m_values.end() ? fallback : it->second;
}

int TomlDocument::integer(const std::string& key, int fallback, bool* parsed) const
{
    const auto it = m_values.find(key);
    if (it == m_values.end())
    {
        if (parsed) { *parsed = false; }
        return fallback;
    }
    int v = 0;
    const bool ok = parseInteger(it->second, v);
    if (parsed) { *parsed = ok; }
    // One DELIBERATE difference from the QSettings implementation: a present but
    // unparseable value returns the caller's fallback, where QVariant::toInt() returned
    // 0. That silently turned a corrupt Polarity or Slope into index 0 rather than into
    // the default the caller passed, which is never what any call site here wants.
    return ok ? v : fallback;
}

double TomlDocument::number(const std::string& key, double fallback) const
{
    const auto it = m_values.find(key);
    if (it == m_values.end())
    {
        return fallback;
    }
    double v = 0.0;
    return parseDouble(it->second, v) ? v : fallback;
}

bool TomlDocument::boolean(const std::string& key, bool fallback) const
{
    const auto it = m_values.find(key);
    if (it == m_values.end())
    {
        return fallback;
    }
    // Deliberately QVariant::toBool()'s exact rule, which is what the QSettings
    // implementation went through: lowercase, then false only for empty, "0" or
    // "false" - anything else is true. Reproduced rather than tightened so this
    // refactor cannot change how any existing file reads. A stricter rule (unknown
    // text falls back instead of reading as true) would be an improvement, but it is
    // a separate decision from moving off QSettings.
    const std::string v = asciiLower(trimmed(it->second));
    return !(v.empty() || v == "0" || v == "false");
}

// ---------------------------------------------------------------------------
// writing
// ---------------------------------------------------------------------------

void TomlDocument::setValue(const std::string& key, const std::string& value)
{
    std::string section, bare;
    splitKey(key, section, bare);
    m_values[key] = value;
    rememberOrder(section, bare);
}

void TomlDocument::setValue(const std::string& key, int value)
{
    setValue(key, std::to_string(value));
}

void TomlDocument::setValue(const std::string& key, double value)
{
    // QString::number(double)'s spelling: %g with six significant digits, so 3.0 is
    // "3" (and is then written bare, like an integer), 0.00001 is "1e-05".
    char buf[64];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value,
                                         std::chars_format::general, 6);
    setValue(key, ec == std::errc() ? std::string(buf, ptr) : std::string("0"));
}

void TomlDocument::setValue(const std::string& key, bool value)
{
    setValue(key, std::string(value ? "true" : "false"));
}

bool TomlDocument::save(const std::filesystem::path& path, std::string* error) const
{
    // Truncate rather than merge. The QSettings implementation merged into whatever
    // the file already held, so its callers had to QFile::remove() first or an old
    // word map survived beside the new one.
    //
    // Text mode on purpose: the CRT writes each '\n' as CRLF, which is what the
    // QIODevice::Text writer produced and what the shipped files use.
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        if (error)
        {
            *error = "Could not write '" + textfile::utf8FileName(path) + "'.";
        }
        return false;
    }

    bool first = true;
    auto valueOf = [this](const std::string& compound) -> const std::string& {
        static const std::string empty;
        const auto it = m_values.find(compound);
        return it == m_values.end() ? empty : it->second;
    };

    // Top-level keys first, with no header, so they stay top-level on re-read.
    const auto top = m_section_keys.find(std::string());
    if (top != m_section_keys.end() && !top->second.empty())
    {
        for (const std::string& bare : top->second)
        {
            out << bare << " = " << formatValue(valueOf(bare)) << "\n";
        }
        first = false;
    }

    for (const std::string& section : m_sections)
    {
        if (!first)
        {
            out << "\n";
        }
        first = false;

        out << '[' << section << "]\n";
        const auto keys = m_section_keys.find(section);
        if (keys == m_section_keys.end())
        {
            continue;
        }
        for (const std::string& bare : keys->second)
        {
            out << bare << " = " << formatValue(valueOf(section + '/' + bare)) << "\n";
        }
    }

    out.close();
    if (out.fail())
    {
        if (error)
        {
            *error = "Error writing '" + textfile::utf8FileName(path) + "'.";
        }
        return false;
    }
    return true;
}
