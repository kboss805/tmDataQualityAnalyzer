/**
 * @file processingtemplateschema.h
 * @brief Single source of truth for a ProcessingTemplate's JSON representation
 *        (Batch Apply, docs/processing-template-design.md).
 *
 * Each entry's processing settings are delegated to StreamConfigSchema so the
 * ~15-field StreamConfig list is never hand-mapped twice; this schema adds only
 * the per-entry series-appearance array and the top-level template envelope.
 * Like StreamConfigSchema, calibrationByWord is never serialized (only the
 * calibration input references are).
 */

#ifndef PROCESSINGTEMPLATESCHEMA_H
#define PROCESSINGTEMPLATESCHEMA_H

class QJsonDocument;
struct ProcessingTemplate;

namespace ProcessingTemplateSchema
{
    /// The schema version this build writes and accepts. A template with a
    /// newer/unrecognized version is rejected by fromJson() rather than
    /// silently mis-read.
    inline constexpr int kCurrentSchemaVersion = 1;

    enum class LoadStatus
    {
        Success,
        UnsupportedSchemaVersion, ///< schemaVersion is missing, or newer than kCurrentSchemaVersion.
        InvalidFormat,            ///< Malformed/missing required structure (e.g. no "entries" array).
    };

    /// Serializes @p tmpl to a JSON document (no file paths -- a template is
    /// location-independent by design).
    QJsonDocument toJson(const ProcessingTemplate& tmpl);

    /// Inverse of toJson(): parses @p doc into @p out. Fields absent from an
    /// entry's config are left at StreamConfig's own in-class defaults.
    LoadStatus fromJson(const QJsonDocument& doc, ProcessingTemplate& out);
}

#endif // PROCESSINGTEMPLATESCHEMA_H
