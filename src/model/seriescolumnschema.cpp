/**
 * @file seriescolumnschema.cpp
 * @brief Implementation of the CSV column-header schema (format + parse).
 */

#include "seriescolumnschema.h"

#include <string>
#include <string_view>
#include <utility>

#include "textparse.h"

namespace
{
    // The SNR column format is "<pcmChannelId> - <streamLabel> <channelName>", and
    // the receiver number lives in the channel name as "_RCVR<N>". These separators
    // are the single source of truth for both formatting and parsing below.
    constexpr std::string_view kIdLabelSep      = " - ";   // between "<id>" and "<streamLabel>"
    constexpr char             kLabelChannelSep = ' ';     // between "<streamLabel>" and "<channelName>"
    constexpr std::string_view kReceiverToken   = "_RCVR";

    // Multi-file input (docs/multi-file-input-design.md §7): a leading source
    // qualifier "S<n>| " on a source-1+ column. 'S' + digits can't be confused with
    // the SNR id-prefix shape (that starts with a bare digit, not a letter) or the
    // Lock/MissedFrames suffixes (those are suffixes, not a prefix).
    constexpr char             kSourceQualifierLead = 'S';
    constexpr std::string_view kSourceQualifierSep  = "| ";

    bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

    /// @return True if @p h begins with "<one-or-more-digits> - ", the shape only an
    /// SNR column has (Lock/MissedFrames columns never lead with a digit-then-" - ").
    /// Checked before the suffix match so a header that happens to match both shapes
    /// (not reachable via this app's own export, but possible from a hand-crafted or
    /// third-party CSV) resolves as SNR rather than being misrouted to Lock/Missed.
    bool hasSnrIdPrefix(const std::string& h)
    {
        std::size_t digits = 0;
        while (digits < h.size() && isAsciiDigit(h[digits]))
            digits++;
        return digits > 0 && std::string_view(h).substr(digits).starts_with(kIdLabelSep);
    }

    /// Strips a leading "S<n>| " source qualifier from @p header if present.
    /// @return {sourceId, headerWithQualifierRemoved}, or {0, header} unchanged if
    /// no qualifier is present -- source 0's header never carries one.
    std::pair<int, std::string> stripSourceQualifier(const std::string& header)
    {
        if (header.empty() || header[0] != kSourceQualifierLead)
            return { 0, header };

        std::size_t digits = 1; // scan starts just past the leading 'S'
        while (digits < header.size() && isAsciiDigit(header[digits]))
            digits++;
        if (digits == 1) // no digit followed 'S' -- not a qualifier, e.g. a literal "Something"
            return { 0, header };
        if (!std::string_view(header).substr(digits).starts_with(kSourceQualifierSep))
            return { 0, header };

        int source_id = 0;
        if (!textparse::parseInteger(header.substr(1, digits - 1), source_id))
            return { 0, header };

        return { source_id, header.substr(digits + kSourceQualifierSep.size()) };
    }
}

std::string SeriesColumnSchema::snrSeriesName(int pcmChannelId, const std::string& streamLabel,
                                              const std::string& channelName)
{
    std::string out = std::to_string(pcmChannelId);
    out.append(kIdLabelSep);
    out += streamLabel;
    out += kLabelChannelSep;
    out += channelName;
    return out;
}

std::string SeriesColumnSchema::columnHeader(MetricType metricType, const std::string& name, int sourceId)
{
    std::string base;
    switch (metricType)
    {
    case MetricType::FrameSyncLock:
        base = name + SeriesColumnSchema::kLockSuffix;
        break;
    case MetricType::AccumulatedMissedFrames:
        base = name + SeriesColumnSchema::kMissedFramesSuffix;
        break;
    default: // SNR series names already carry the channel and are unique
        base = name;
        break;
    }

    // Source 0 keeps this exact, unqualified format -- byte-identical to every
    // prior release's single-source export.
    if (sourceId == 0)
        return base;

    std::string qualifier;
    qualifier += kSourceQualifierLead;
    qualifier += std::to_string(sourceId);
    qualifier.append(kSourceQualifierSep);
    return qualifier + base;
}

SeriesColumnSchema::ParsedColumn SeriesColumnSchema::parseColumnHeader(const std::string& headerIn)
{
    ParsedColumn out;

    const auto [source_id, header] = stripSourceQualifier(headerIn);
    out.sourceId = source_id;

    const std::string_view lock_suffix   = SeriesColumnSchema::kLockSuffix;
    const std::string_view missed_suffix = SeriesColumnSchema::kMissedFramesSuffix;
    const bool             looks_like_snr = hasSnrIdPrefix(header);

    if (!looks_like_snr && std::string_view(header).ends_with(lock_suffix))
    {
        // Strip the metric suffix to recover the bare series name the legend shows;
        // keep it as streamLabel too so the lock/missed-frames pair re-links (rename
        // keeps paired siblings in sync by streamLabel).
        out.metricType  = MetricType::FrameSyncLock;
        out.name        = header.substr(0, header.size() - lock_suffix.size());
        out.streamLabel = out.name;
        return out;
    }
    if (!looks_like_snr && std::string_view(header).ends_with(missed_suffix))
    {
        out.metricType  = MetricType::AccumulatedMissedFrames;
        out.name        = header.substr(0, header.size() - missed_suffix.size());
        out.streamLabel = out.name;
        return out;
    }

    // SNR: "<pcmChannelId> - <streamLabel> <channelName>". Recover the stream id and
    // label so multiple receiver-SNR streams stay grouped/labelled on import (the
    // Customize Plot Series dialog keys SNR grouping on streamOrder + streamLabel).
    out.metricType = MetricType::SNR;
    out.name       = header;

    const std::size_t dash = header.find(kIdLabelSep);
    if (dash != std::string::npos && dash > 0)
    {
        int id = 0;
        if (textparse::parseInteger(header.substr(0, dash), id))
            out.streamOrder = id;

        // "<streamLabel> <channelName>" — the label may itself contain spaces, so
        // split at the LAST space (the one before the channel name).
        const std::string rest = header.substr(dash + kIdLabelSep.size());
        const std::size_t last_space = rest.rfind(kLabelChannelSep);
        out.streamLabel = (last_space != std::string::npos && last_space > 0) ? rest.substr(0, last_space) : rest;
    }

    const std::size_t rcvr_pos = header.rfind(kReceiverToken);
    if (rcvr_pos != std::string::npos)
    {
        int rcvr_num = 0;
        out.receiverIndex = textparse::parseInteger(header.substr(rcvr_pos + kReceiverToken.size()), rcvr_num)
                                ? rcvr_num : 0;
    }

    return out;
}
