/**
 * @file sessionschema.h
 * @brief Single source of truth for a Session's JSON representation (session
 *        save/load, Phase 6, build order §12.2).
 *
 * Serializes/parses the top-level session document: schemaVersion, appVersion,
 * the source list (each stream via StreamConfigSchema), and coarse view state.
 * Also owns the relative/absolute path handling for each source's filepath
 * (docs/session-save-load-design.md §5) as pure, disk-free string logic so it
 * can be unit tested without a session file actually existing on disk.
 */

#ifndef SESSIONSCHEMA_H
#define SESSIONSCHEMA_H

#include <QString>

class QJsonDocument;
struct Session;

namespace SessionSchema
{
    /// The schema version this build writes and accepts. A saved session with
    /// a newer/unrecognized version is rejected by fromJson() rather than
    /// silently mis-read.
    inline constexpr int kCurrentSchemaVersion = 1;

    enum class LoadStatus
    {
        Success,
        UnsupportedSchemaVersion, ///< schemaVersion is missing, or newer than kCurrentSchemaVersion.
        InvalidFormat,            ///< Malformed/missing required structure (e.g. no "sources" array).
    };

    /// Serializes @p session to a JSON document. Each source's filepath is
    /// stored relative to @p sessionDir when possible (see sessionRelativePath()),
    /// else absolute, per §5.
    QJsonDocument toJson(const Session& session, const QString& sessionDir);

    /// Inverse of toJson(): parses @p doc into @p out. Source filepaths are left
    /// exactly as stored (relative or absolute) -- call resolveSessionPath() to
    /// resolve a relative one against the session file's directory before use.
    /// Source::sourceId is not populated (assigned fresh by the caller).
    LoadStatus fromJson(const QJsonDocument& doc, Session& out);

    /// @return @p filePath expressed relative to @p sessionDir, if that
    /// resolves to a genuine relative path (same drive/root); otherwise
    /// @p filePath unchanged (absolute).
    QString sessionRelativePath(const QString& filePath, const QString& sessionDir);

    /// Inverse of sessionRelativePath(): resolves @p storedPath against
    /// @p sessionDir if it is relative; returns it unchanged if already absolute.
    QString resolveSessionPath(const QString& storedPath, const QString& sessionDir);
}

#endif // SESSIONSCHEMA_H
