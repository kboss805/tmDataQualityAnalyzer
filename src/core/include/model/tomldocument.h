/**
 * @file tomldocument.h
 * @brief Reads and writes the TOML subset this application's settings files use.
 *
 * A file-format parser and writer, not a settings store:
 *
 *   - **It keeps file order.** A receiver word map is meaningful in file order, so
 *     sections and keys come back in the order the file declares them, from one parse.
 *   - **It replaces, never merges.** save() truncates the target, so an old file's word
 *     map cannot survive alongside the new one and no caller deletes the file first.
 *   - **It needs no registration.** Nothing process-wide has to be set up before a
 *     config file can be read.
 *   - **It writes the file in place**, not through a rename: a path it cannot open for
 *     writing is reported as an error, never silently skipped.
 *   - **It reads arrays of tables** (`arrayTables()`), so `StepDetector::parseStepConfig`
 *     reads `[[Step]]` files through it rather than through a parser of its own - see below.
 *
 * This class is plain file I/O over a map of UTF-8 `std::string`s, with paths as
 * `std::filesystem::path`.
 *
 * tests/core/test_tomldocument.cpp pins the file handling, against what
 * v2.14 read and wrote: a UTF-8 BOM is skipped, UTF-16 (LE or BE, with a BOM) is
 * decoded, integers and doubles tolerate surrounding whitespace and a leading '+',
 * doubles are written `%g` with six significant digits, and the file is written with
 * CRLF line endings and no BOM. Whitespace trimming is ASCII only (U+00A0 is not
 * trimmed), and invalid UTF-8 is kept as bytes rather than replaced with U+FFFD;
 * neither is reachable from a shipped file.
 *
 * Supported subset, unchanged from what the shipped files use:
 *   - Section headers:    [SectionName]
 *   - Integers:           Key = 42
 *   - Floats:             Key = 1.5
 *   - Booleans:           Key = true / false
 *   - Quoted strings:     Key = "some text"
 *   - Bare strings:       Key = some text        (tolerated for compatibility)
 *   - Comments:           # to end of line, honoured outside quotes
 *   - Blank lines
 *   - Arrays of tables:   [[Name]] opens an entry; the keys under it, up to the next header,
 *                         belong to it (READ ONLY: save() does not write them)
 *
 * Keys are addressed as "Section/Key", the same compound spelling the call sites
 * already used, so this is a drop-in at every one of them. A key outside any section
 * is addressed by its bare name.
 *
 * Usage:
 *   TomlDocument doc = TomlDocument::load(path);
 *   if (!doc.ok()) { ... }
 *   int bits = doc.integer("Frame/BitsPerFrame", 0);
 */

#ifndef TOMLDOCUMENT_H
#define TOMLDOCUMENT_H

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

class TomlDocument
{
public:
    /// An empty, valid document, ready for setValue() + save().
    TomlDocument() = default;

    /// Parses @p path. On failure the document is !ok() and empty; @p error, when
    /// given, receives a human-readable reason (UTF-8). A file that parses but holds
    /// nothing is ok() with no keys - "unreadable" and "empty" are different answers,
    /// and callers here distinguish them.
    static TomlDocument load(const std::filesystem::path& path, std::string* error = nullptr);

    /// False only when the file could not be read at all.
    bool ok() const { return m_ok; }

    bool contains(const std::string& key) const;

    /// @p parsed, when given, reports whether the value was present AND converted -
    /// which is how a genuine 0 is told apart from a missing or malformed key.
    std::string string (const std::string& key, const std::string& fallback = {}) const;
    int         integer(const std::string& key, int fallback = 0, bool* parsed = nullptr) const;
    double      number (const std::string& key, double fallback = 0.0) const;
    bool        boolean(const std::string& key, bool fallback = false) const;

    /// Section names in the order the file first declares each one.
    ///
    /// File order is the point: a receiver word map's sections carry the parameter
    /// order, so they must not come back sorted.
    ///
    /// A repeated section appears once. The only name that repeats in practice is the
    /// [EMPTY] word-map placeholder, and `FrameSetup::tryLoadingFile()` skips that by
    /// name, so collapsing repeats loses nothing a caller reads. A section with no
    /// keys at all still appears, because its presence is what makes the word
    /// numbering read as contiguous.
    const std::vector<std::string>& sectionsInFileOrder() const { return m_sections; }

    /// One entry of an array of tables: the keys under a `[[Name]]` header, up to the next header.
    ///
    /// Read from a file only; `save()` does not write them, and `contains()` / `string()` and the
    /// other lookups do not see them (a `[[Step]]` file has no "Step/db").
    struct Table
    {
        /// The text between the brackets exactly as written, so `[[ Step ]]` is " Step ". Empty for a
        /// `[[` line that is not just `[[Name]]` on its own - a trailing comment, a missing bracket.
        /// Such a line still OPENS an entry, so the keys under it are not taken for the previous
        /// entry's, but one that no name matches.
        std::string name;
        /// (key, value) in file order, repeats included. The key is trimmed. The value is as written
        /// after the '=': an unquoted trailing `# comment` and the surrounding whitespace removed, and
        /// the quotes KEPT - unlike a section's values - so a caller can tell "6" from 6.
        std::vector<std::pair<std::string, std::string>> entries;
    };

    /// Every `[[...]]` entry in the file, in order, whatever it is named.
    const std::vector<Table>& arrayTables() const { return m_tables; }

    /// The entries whose header names @p name, in file order. ASCII case is ignored; nothing else is,
    /// so `[[Step]]` and `[[STEP]]` match "step" but `[[Steps]]` and `[[ Step ]]` do not.
    std::vector<const Table*> tables(const std::string& name) const;

    void setValue(const std::string& key, const std::string& value);
    /// Without this, a string literal would bind to setValue(key, bool): a pointer
    /// converts to bool by a standard conversion, which beats the user-defined one to
    /// std::string - so `setValue("K", "text")` would silently write `K = true`.
    void setValue(const std::string& key, const char* value) { setValue(key, std::string(value)); }
    void setValue(const std::string& key, int value);
    void setValue(const std::string& key, double value);
    void setValue(const std::string& key, bool value);

    /// Writes the document to @p path, REPLACING whatever is there. It never merges,
    /// so no caller needs to delete the file first.
    ///
    /// Sections are emitted in the order they were first set, and keys in the order
    /// they were set within each section, so a file this writes reads back in the same
    /// order it was built, not alphabetically.
    bool save(const std::filesystem::path& path, std::string* error = nullptr) const;

private:
    /// Splits "Section/Key" into its parts; an unqualified key has an empty section.
    static void splitKey(const std::string& key, std::string& section, std::string& bare);
    void rememberOrder(const std::string& section, const std::string& bare);

    std::map<std::string, std::string> m_values;   ///< compound key -> raw (unquoted) value
    std::vector<std::string>           m_sections; ///< section names, file/insertion order, each once
    /// Per-section key order. Keyed by section name; the writer coalesces a repeated
    /// section, which only affects [EMPTY]-style placeholders with no distinct values.
    std::map<std::string, std::vector<std::string>> m_section_keys;
    std::vector<Table>                              m_tables;   ///< `[[...]]` entries, file order
    bool m_ok = true;
};

#endif // TOMLDOCUMENT_H
