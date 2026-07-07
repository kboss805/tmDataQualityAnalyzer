/**
 * @file seriescolumnschema.cpp
 * @brief Implementation of the CSV column-header schema (format + parse).
 */

#include "seriescolumnschema.h"

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
}

QString SeriesColumnSchema::snrSeriesName(int pcmChannelId, const QString& streamLabel,
                                          const QString& channelName)
{
    return QString::number(pcmChannelId) + kIdLabelSep + streamLabel
           + kLabelChannelSep + channelName;
}

QString SeriesColumnSchema::columnHeader(const PlotSeriesData& series)
{
    switch (series.metricType)
    {
    case PlotSeriesData::MetricType::FrameSyncLock:
        return series.name + PlotConstants::kCsvLockSuffix;
    case PlotSeriesData::MetricType::AccumulatedMissedFrames:
        return series.name + PlotConstants::kCsvMissedFramesSuffix;
    default: // SNR series names already carry the channel and are unique
        return series.name;
    }
}

SeriesColumnSchema::ParsedColumn SeriesColumnSchema::parseColumnHeader(const QString& header)
{
    ParsedColumn out;

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
