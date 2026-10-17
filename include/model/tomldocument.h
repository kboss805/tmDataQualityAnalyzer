/**
 * @file tomldocument.h
 * @brief Reads and writes the TOML subset this application's settings files use.
 *
 * Replaces the previous `TomlConfigHelper`, which implemented the same subset as a
 * custom `QSettings::Format`. QSettings is a settings STORE - scoped, cached, merged,
 * written through an atomic rename - and using it as a file-format parser cost more
 * than it saved:
 *
 *   - **It sorts.** Sections came back alphabetically, but a receiver word map is
 *     meaningful in file order, so `FrameSetup` opened and parsed the same file a
 *     SECOND time (`readGroupsInFileOrder`) just to recover that order.
 *   - **It merges.** Writing had to `QFile::remove()` the target first, or the old
 *     file's word map survived alongside the new one.
 *   - **It needs global registration.** `registerFormat()` is process-wide and lazy,
 *     so both `main()` and the test harness had to remember to call it before any
 *     config file could be read at all.
 *   - **It writes through a rename**, which on Windows silently does nothing when the
 *     target path is held by a live `QTemporaryFile` - a trap this project hit and had
 *     to write a memory note about.
 *   - **It cannot express arrays**, which is why `StepDetector::parseStepConfig` is a
 *     second, hand-written TOML parser for `[[Step]]` files. Unifying the two is the
 *     obvious next step and is deliberately NOT part of this change: that parser feeds
 *     calibration, and it deserves its own verifiable step rather than riding along.
 *
 * This class is plain file I/O over a `QString`-keyed map. It keeps Qt types (QString,
 * QVariant-free typed getters) - the point was never to drop Qt Core, it was to stop
 * using a settings store as a parser.
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

#include <QMap>
#include <QString>
#include <QStringList>

class TomlDocument
{
public:
    /// An empty, valid document, ready for setValue() + save().
    TomlDocument() = default;

    /// Parses @p path. On failure the document is !ok() and empty; @p error, when
    /// given, receives a human-readable reason. A file that parses but holds nothing
    /// is ok() with no keys - "unreadable" and "empty" are different answers, and
    /// callers here distinguish them.
    static TomlDocument load(const QString& path, QString* error = nullptr);

    /// False only when the file could not be read at all.
    bool ok() const { return m_ok; }

    bool contains(const QString& key) const;

    /// @p parsed, when given, reports whether the value was present AND converted -
    /// which is how a genuine 0 is told apart from a missing or malformed key.
    QString string (const QString& key, const QString& fallback = QString()) const;
    int     integer(const QString& key, int fallback = 0, bool* parsed = nullptr) const;
    double  number (const QString& key, double fallback = 0.0) const;
    bool    boolean(const QString& key, bool fallback = false) const;

    /// Section names in the order the file first declares each one.
    ///
    /// File order is the point: a receiver word map's sections carry the parameter
    /// order, and the QSettings implementation returned them SORTED - which is why
    /// FrameSetup had to parse the same file a second time to recover it.
    ///
    /// A repeated section appears once. The only name that repeats in practice is the
    /// [EMPTY] word-map placeholder, and `FrameSetup::tryLoadingFile()` skips that by
    /// name, so collapsing repeats loses nothing a caller reads. A section with no
    /// keys at all still appears, because its presence is what makes the word
    /// numbering read as contiguous.
    const QStringList& sectionsInFileOrder() const { return m_sections; }

    void setValue(const QString& key, const QString& value);
    void setValue(const QString& key, int value);
    void setValue(const QString& key, double value);
    void setValue(const QString& key, bool value);

    /// Writes the document to @p path, REPLACING whatever is there. Unlike QSettings
    /// this never merges, so no caller needs to delete the file first.
    ///
    /// Sections are emitted in the order they were first set, and keys in the order
    /// they were set within each section, so a file this writes reads back in the same
    /// order it was built. The QSettings implementation emitted both alphabetically.
    bool save(const QString& path, QString* error = nullptr) const;

private:
    /// Splits "Section/Key" into its parts; an unqualified key has an empty section.
    static void splitKey(const QString& key, QString& section, QString& bare);
    void rememberOrder(const QString& section, const QString& bare);

    QMap<QString, QString> m_values;    ///< compound key -> raw (unquoted) value
    QStringList            m_sections;  ///< section names, file/insertion order, with duplicates
    /// Per-section key order. Not a QMap of QStringList keyed by section alone,
    /// because a section name can repeat; the writer coalesces repeats, which only
    /// affects [EMPTY]-style placeholders that carry no distinct values.
    QMap<QString, QStringList> m_section_keys;
    bool                   m_ok = true;
};

#endif // TOMLDOCUMENT_H
