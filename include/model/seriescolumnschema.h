/**
 * @file seriescolumnschema.h
 * @brief Single source of truth for the app's CSV column-header format.
 *
 * The application exports each plot series as a CSV column whose header encodes
 * the series' identity, and re-imports its own exports by parsing that header
 * back. Formatting (PlotViewModel::exportCsv / addStreamData) and parsing
 * (CsvSeriesParser) MUST stay inverses — historically they lived apart with a
 * "keep in lockstep" comment. This module owns both sides so they can't drift,
 * and is covered by a round-trip test (tst_seriescolumnschema).
 */

#ifndef SERIESCOLUMNSCHEMA_H
#define SERIESCOLUMNSCHEMA_H

#include <QString>

#include "plotseriesdata.h"

namespace SeriesColumnSchema
{
    /// Identity fields recovered from a data-column header. `channelIndex` is NOT
    /// here: it is a per-(stream, receiver) running count the parser assigns while
    /// walking columns left to right, not something a single header carries.
    struct ParsedColumn
    {
        PlotSeriesData::MetricType metricType = PlotSeriesData::MetricType::SNR;
        QString name;            ///< Presentation name (metric suffix stripped).
        QString streamLabel;     ///< Owning stream's label.
        int     streamOrder   = 0; ///< Source PCM channel id (SNR only; 0 otherwise).
        int     receiverIndex = 0; ///< 1-based receiver from "_RCVR<N>" (SNR only; 0 otherwise).
    };

    /// The in-memory display name for one Receiver-SNR channel series:
    /// "<pcmChannelId> - <streamLabel> <channelName>". The leading channel id keeps
    /// multiple SNR streams distinct in the legend and across a CSV round-trip.
    /// Used by PlotViewModel::addStreamData().
    QString snrSeriesName(int pcmChannelId, const QString& streamLabel,
                          const QString& channelName);

    /// The CSV column header for a series: its name, plus a metric suffix for the
    /// left-axis metrics (Frame Sync Lock / Accumulated Missed Frames) whose bare
    /// stream names would otherwise export as identical columns. SNR names already
    /// carry the channel and are unique, so they pass through unchanged.
    /// Used by PlotViewModel::exportCsv().
    QString columnHeader(const PlotSeriesData& series);

    /// Inverse of columnHeader(): classify a data-column header and recover its
    /// identity fields. An SNR-shaped header ("<digits> - …") always resolves as
    /// SNR; otherwise a recognized metric suffix picks Lock/MissedFrames; anything
    /// else resolves as SNR with a bare name. Used by CsvSeriesParser::parse().
    ParsedColumn parseColumnHeader(const QString& header);
}

#endif // SERIESCOLUMNSCHEMA_H
