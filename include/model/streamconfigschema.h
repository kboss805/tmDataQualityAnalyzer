/**
 * @file streamconfigschema.h
 * @brief Single source of truth for StreamConfig's JSON representation (session
 *        save/load, Phase 6).
 *
 * The session writer and (eventual) reader must share one field list rather than
 * hand-mapping fields in two places -- the same lesson as SeriesColumnSchema for
 * CSV columns. `calibrationByWord` (the extracted, non-linear calibration
 * profiles) is intentionally never serialized here; only the calibration INPUT
 * references (calCh10Path/stepTomlPath/clipStartSec/clipEndSec) are, so a future
 * re-extraction pass has what it needs without persisting derived data.
 */

#ifndef STREAMCONFIGSCHEMA_H
#define STREAMCONFIGSCHEMA_H

class QJsonObject;
struct StreamConfig;

namespace StreamConfigSchema
{
    /// Serializes every persisted field of @p config to a JSON object. Excludes
    /// calibrationByWord (session-only; never serialized).
    QJsonObject toJson(const StreamConfig& config);

    /// Inverse of toJson(): populates @p out from @p json. Fields absent from
    /// @p json (e.g. an older schema version) are left at StreamConfig's own
    /// in-class defaults. @p out's calibrationByWord is not touched.
    /// @return true if @p json had the minimal shape of a StreamConfig object.
    bool fromJson(const QJsonObject& json, StreamConfig& out);
}

#endif // STREAMCONFIGSCHEMA_H
