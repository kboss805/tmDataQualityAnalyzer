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
 *
 * Part of the Qt-free core: text is UTF-8 `std::string`, and `TestCoreQtFree`
 * keeps it so. The callers that hold QStrings convert at the call.
 */

#ifndef SERIESCOLUMNSCHEMA_H
#define SERIESCOLUMNSCHEMA_H

#include <string>

#include "metrictype.h"

namespace SeriesColumnSchema
{
    /// CSV export/import header tokens. The exporter (PlotViewModel::exportCsv) and
    /// the importer (CsvSeriesParser) MUST agree on these so an exported file can be
    /// re-loaded. The first column is the combined day+time stamp; frame-sync series
    /// names are qualified per metric (the bare name is shared by a stream's lock and
    /// missed-frames series and would otherwise export as two identical headers).
    ///
    /// These lived in PlotConstants until the de-Qt work. They are a serialization
    /// contract rather than plot presentation, and this file is already the documented
    /// single source of truth for the CSV column format - so they belong here, next to
    /// the code that formats and parses them. Keeping them in constants.h beside the
    /// QColor palettes is also what forced the MODEL layer to include a QtGui header.
    inline constexpr const char* kTimeHeader          = "Time (DOY:HH:MM:SS.mmm)";
    inline constexpr const char* kLockSuffix          = " Lock (%)";
    inline constexpr const char* kMissedFramesSuffix  = " Accumulated Missed Frames";

    /// Identity fields recovered from a data-column header. `channelIndex` is NOT
    /// here: it is a per-(stream, receiver) running count the parser assigns while
    /// walking columns left to right, not something a single header carries.
    struct ParsedColumn
    {
        MetricType  metricType = MetricType::SNR;
        std::string name;            ///< Presentation name (metric suffix AND source qualifier stripped).
        std::string streamLabel;     ///< Owning stream's label.
        int         streamOrder   = 0; ///< Source PCM channel id (SNR only; 0 otherwise).
        int         receiverIndex = 0; ///< 1-based receiver from "_RCVR<N>" (SNR only; 0 otherwise).
        int         sourceId      = 0; ///< Multi-file session source id recovered from a leading "S<n>| "
                                       ///< qualifier (see columnHeader()); 0 if the header carries none.
    };

    /// The in-memory display name for one Receiver-SNR channel series:
    /// "<pcmChannelId> - <streamLabel> <channelName>". The leading channel id keeps
    /// multiple SNR streams distinct in the legend and across a CSV round-trip.
    /// Used by PlotViewModel::addStreamData().
    std::string snrSeriesName(int pcmChannelId, const std::string& streamLabel,
                              const std::string& channelName);

    /// The CSV column header for a series: its name, plus a metric suffix for the
    /// left-axis metrics (Frame Sync Lock / Accumulated Missed Frames) whose bare
    /// stream names would otherwise export as identical columns. SNR names already
    /// carry the channel and are unique, so they pass through unchanged.
    ///
    /// Multi-file input (docs/multi-file-input-design.md §7): a series from source
    /// 0 (the first/only source) exports with EXACTLY this format, unchanged --
    /// a single-source export stays byte-identical to every prior release, and
    /// every previously exported CSV still imports. A series from source 1+ gets a
    /// leading "S<n>| " qualifier (e.g. "S1| 40 - RCVR Data L_RCVR1") so two
    /// sources sharing a channel id/label never export as identical, ambiguous
    /// columns; parseColumnHeader() strips it back off before recovering identity.
    ///
    /// Takes the three fields that make up a header rather than the plot's series
    /// struct, which carries Qt types. Used by PlotViewModel::exportCsv().
    std::string columnHeader(MetricType metricType, const std::string& name, int sourceId);

    /// Inverse of columnHeader(): classify a data-column header and recover its
    /// identity fields, including sourceId from a leading "S<n>| " qualifier if
    /// present (stripped before the shape detection below, so it composes with
    /// every other case). An SNR-shaped header ("<digits> - …") always resolves as
    /// SNR; otherwise a recognized metric suffix picks Lock/MissedFrames; anything
    /// else resolves as SNR with a bare name. Used by CsvSeriesParser::parse().
    ///
    /// Numbers are read with `QString::toInt()`'s rules (textparse.h): surrounding
    /// whitespace and a leading '+' are tolerated, junk or overflow reads as "not a
    /// number". Digits are ASCII only: Qt also treated Unicode digits as the start
    /// of an SNR id, which no export of this application produces.
    ParsedColumn parseColumnHeader(const std::string& header);
}

#endif // SERIESCOLUMNSCHEMA_H
