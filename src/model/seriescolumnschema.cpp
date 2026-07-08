/**
 * @file seriescolumnschema.cpp
 * @brief Implementation of the CSV column-header schema (format + parse).
 */

#include "seriescolumnschema.h"

#include <QPair>
#include <QStringView>

#include "constants.h"

namespace
{
    // The SNR column format is "<pcmChannelId> - <streamLabel> <channelName>", and
    // the receiver number lives in the channel name as "_RCVR<N>". These separators
    // are the single source of truth for both formatting and parsing below.
    constexpr QLatin1String kIdLabelSep(" - ");   // between "<id>" and "<streamLabel>"
    constexpr QChar         kLabelChannelSep(u' '); // between "<streamLabel>" and "<channelName>"
    constexpr QLatin1String kReceiverToken("_RCVR");

    // Multi-file input (docs/multi-file-input-design.md §7): a leading source
    // qualifier "S<n>| " on a source-1+ column. 'S' + digits can't be confused with
    // the SNR id-prefix shape (that starts with a bare digit, not a letter) or the
    // Lock/MissedFrames suffixes (those are suffixes, not a prefix).
    constexpr QChar         kSourceQualifierLead(u'S');
    constexpr QLatin1String kSourceQualifierSep("| ");

    /// @return True if @p h begins with "<one-or-more-digits> - ", the shape only an
    /// SNR column has (Lock/MissedFrames columns never lead with a digit-then-" - ").
    /// Checked before the suffix match so a header that happens to match both shapes
    /// (not reachable via this app's own export, but possible from a hand-crafted or
    /// third-party CSV) resolves as SNR rather than being misrouted to Lock/Missed.
    bool hasSnrIdPrefix(const QString& h)
    {
        int digits = 0;
        while (digits < h.size() && h.at(digits).isDigit())
            digits++;
        return digits > 0 && QStringView(h).mid(digits).startsWith(kIdLabelSep);
    }

    /// Strips a leading "S<n>| " source qualifier from @p header if present.
    /// @return {sourceId, headerWithQualifierRemoved}, or {0, header} unchanged if
    /// no qualifier is present -- source 0's header never carries one.
    QPair<int, QString> stripSourceQualifier(const QString& header)
    {
        if (header.isEmpty() || header.at(0) != kSourceQualifierLead)
            return { 0, header };

        int digits = 1; // scan starts just past the leading 'S'
        while (digits < header.size() && header.at(digits).isDigit())
            digits++;
        if (digits == 1) // no digit followed 'S' -- not a qualifier, e.g. a literal "Something"
            return { 0, header };
        if (!QStringView(header).mid(digits).startsWith(kSourceQualifierSep))
            return { 0, header };

        bool ok = false;
        const int source_id = header.mid(1, digits - 1).toInt(&ok);
        if (!ok)
            return { 0, header };

        return { source_id, header.mid(digits + kSourceQualifierSep.size()) };
    }
}

QString SeriesColumnSchema::snrSeriesName(int pcmChannelId, const QString& streamLabel,
                                          const QString& channelName)
{
    return QString::number(pcmChannelId) + kIdLabelSep + streamLabel
           + kLabelChannelSep + channelName;
}

QString SeriesColumnSchema::columnHeader(const PlotSeriesData& series)
{
    QString base;
    switch (series.metricType)
    {
    case PlotSeriesData::MetricType::FrameSyncLock:
        base = series.name + PlotConstants::kCsvLockSuffix;
        break;
    case PlotSeriesData::MetricType::AccumulatedMissedFrames:
        base = series.name + PlotConstants::kCsvMissedFramesSuffix;
        break;
    default: // SNR series names already carry the channel and are unique
        base = series.name;
        break;
    }

    // Source 0 keeps this exact, unqualified format -- byte-identical to every
    // prior release's single-source export.
    if (series.sourceId == 0)
        return base;

    QString qualifier;
    qualifier += kSourceQualifierLead;
    qualifier += QString::number(series.sourceId);
    qualifier += kSourceQualifierSep;
    return qualifier + base;
}

SeriesColumnSchema::ParsedColumn SeriesColumnSchema::parseColumnHeader(const QString& headerIn)
{
    ParsedColumn out;

    const auto [source_id, header] = stripSourceQualifier(headerIn);
    out.sourceId = source_id;

    const QString lock_suffix   = QLatin1String(PlotConstants::kCsvLockSuffix);
    const QString missed_suffix = QLatin1String(PlotConstants::kCsvMissedFramesSuffix);
    const bool    looks_like_snr = hasSnrIdPrefix(header);

    if (!looks_like_snr && header.endsWith(lock_suffix))
    {
        // Strip the metric suffix to recover the bare series name the legend shows;
        // keep it as streamLabel too so the lock/missed-frames pair re-links (rename
        // keeps paired siblings in sync by streamLabel).
        out.metricType  = PlotSeriesData::MetricType::FrameSyncLock;
        out.name        = header.left(header.size() - lock_suffix.size());
        out.streamLabel = out.name;
        return out;
    }
    if (!looks_like_snr && header.endsWith(missed_suffix))
    {
        out.metricType  = PlotSeriesData::MetricType::AccumulatedMissedFrames;
        out.name        = header.left(header.size() - missed_suffix.size());
        out.streamLabel = out.name;
        return out;
    }

    // SNR: "<pcmChannelId> - <streamLabel> <channelName>". Recover the stream id and
    // label so multiple receiver-SNR streams stay grouped/labelled on import (the
    // Customize Plot Series dialog keys SNR grouping on streamOrder + streamLabel).
    out.metricType = PlotSeriesData::MetricType::SNR;
    out.name       = header;

    const int dash = static_cast<int>(header.indexOf(kIdLabelSep));
    if (dash > 0)
    {
        bool id_ok = false;
        const int id = header.left(dash).toInt(&id_ok);
        if (id_ok)
            out.streamOrder = id;

        // "<streamLabel> <channelName>" — the label may itself contain spaces, so
        // split at the LAST space (the one before the channel name).
        const QString rest = header.mid(dash + kIdLabelSep.size());
        const int last_space = static_cast<int>(rest.lastIndexOf(kLabelChannelSep));
        out.streamLabel = (last_space > 0) ? rest.left(last_space) : rest;
    }

    const int rcvr_pos = static_cast<int>(header.lastIndexOf(kReceiverToken));
    if (rcvr_pos >= 0)
    {
        bool ok = false;
        const int rcvr_num = header.mid(rcvr_pos + kReceiverToken.size()).toInt(&ok);
        out.receiverIndex = ok ? rcvr_num : 0;
    }

    return out;
}
