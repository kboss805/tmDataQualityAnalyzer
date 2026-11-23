/**
 * @file csvseriesparser.cpp
 * @brief Implementation of CsvSeriesParser — reads the app's CSV export back into series.
 *        Matched pair with CsvSeriesWriter.
 */

#include "csvseriesparser.h"

#include <algorithm>
#include <map>

#include "constants.h"
#include "linesource.h"
#include "seriescolumnschema.h"
#include "seriesrange.h"
#include "textparse.h"

using csvlines::LineSource;
using csvlines::splitKeepEmpty;

CsvParseResult CsvSeriesParser::parse(const std::filesystem::path& path)
{
    CsvParseResult result;

    LineSource source(path);
    if (!source.isOpen())
    {
        return result;
    }

    // Parse header line: "Time (DOY:HH:MM:SS.mmm),series1,series2,..."
    std::string header_line;
    if (!source.next(header_line) || header_line.empty())
    {
        return result;
    }

    std::vector<std::string_view> columns;
    splitKeepEmpty(header_line, ',', columns);
    // Need the time column plus at least one series column.
    if (columns.size() < 2)
    {
        return result;
    }
    // Reject anything that isn't this app's export format (e.g. the legacy "Day,Time,..." layout), so
    // an unrecognized file fails cleanly rather than being mis-parsed into garbage series.
    if (textparse::trimmed(std::string(columns[0])) != SeriesColumnSchema::kTimeHeader)
    {
        return result;
    }

    // Build series list from columns 1..N (skip the time column).
    const int param_count = static_cast<int>(columns.size() - 1);
    std::vector<CsvSeries> series(static_cast<std::size_t>(param_count));

    // Count channels per (source, stream, receiver) for the channel index (SNR series only) - mirrors
    // the per-stream counting in buildStreamSeries (seriesbuilder.cpp), so multiple SNR streams that reuse
    // receiver numbers don't accumulate channel indices across the whole file. Keyed
    // [sourceId][streamOrder][receiverIndex]: a multi-source export can have two different sources
    // reuse the same streamOrder, so sourceId must be part of the key or their channel indices would
    // incorrectly accumulate together on import.
    std::map<int, std::map<int, std::map<int, int>>> stream_receiver_channel_count;

    for (int i = 0; i < param_count; ++i)
    {
        CsvSeries& s = series[static_cast<std::size_t>(i)];
        // Column identity (metric type, name, stream label/order, source, receiver) comes from the
        // shared schema - the exact inverse of the column headers CsvSeriesWriter writes.
        const SeriesColumnSchema::ParsedColumn col = SeriesColumnSchema::parseColumnHeader(
            textparse::trimmed(std::string(columns[static_cast<std::size_t>(i) + 1])));
        s.name          = col.name;
        s.streamLabel   = col.streamLabel;
        s.metricType    = col.metricType;
        s.streamOrder   = col.streamOrder;
        s.receiverIndex = col.receiverIndex;
        s.sourceId      = col.sourceId;

        // channelIndex is a per-(source, stream, receiver) running count assigned here as columns are
        // walked left to right (SNR series only).
        if (s.metricType == MetricType::SNR)
        {
            std::map<int, int>& receiver_channel_count = stream_receiver_channel_count[s.sourceId][s.streamOrder];
            int& count = receiver_channel_count[s.receiverIndex];
            s.channelIndex = count;
            ++count;
        }
    }

    // Estimate row count from the remaining file size for pre-allocation.
    constexpr std::size_t kBytesPerRowEstimate = 20;
    constexpr std::size_t kMinRowsEstimate = 100;
    const std::size_t estimated_rows =
        std::max(source.remaining() / (static_cast<std::size_t>(param_count) * 8 + kBytesPerRowEstimate), kMinRowsEstimate);
    for (CsvSeries& s : series)
    {
        s.xValues.reserve(estimated_rows);
        s.yValues.reserve(estimated_rows);
    }

    // Parse data rows.
    double first_time = -1.0;
    int first_day = 0;
    std::string line;
    std::vector<std::string_view> fields;
    while (source.next(line))
    {
        if (line.empty())
        {
            continue;
        }

        splitKeepEmpty(line, ',', fields);
        if (fields.size() < static_cast<std::size_t>(param_count) + 1)
        {
            ++result.skippedRows;
            continue;
        }

        int day = 0;
        double time_seconds = 0.0;
        if (!parseCombinedTimestamp(fields[0], day, time_seconds))
        {
            ++result.skippedRows;
            continue;
        }

        // Convert DOY + time to elapsed seconds from the first sample.
        if (first_time < 0.0)
        {
            first_day             = day;
            first_time            = time_seconds;
            result.baseDay        = day;
            result.baseTimeOffset = time_seconds;
        }

        const double elapsed = ((day - first_day) * TimeConstants::kSecondsPerDay) + (time_seconds - first_time);

        for (int i = 0; i < param_count; ++i)
        {
            double value = 0.0;
            if (textparse::parseDouble(std::string(fields[static_cast<std::size_t>(i) + 1]), value))
            {   // an empty cell (sparse series) parses as not-ok and is skipped
                CsvSeries& s = series[static_cast<std::size_t>(i)];
                s.xValues.push_back(elapsed);
                s.yValues.push_back(value);
                s.yMin = runningMin(s.yMin, value);
                s.yMax = runningMax(s.yMax, value);
            }
        }
    }

    // Verify we got data, and take xMax from it while it is hot.
    bool has_any_data = false;
    double x_max = 0.0;
    for (const CsvSeries& s : series)
    {
        if (!s.xValues.empty())
        {
            has_any_data = true;
            if (s.xValues.back() > x_max)
            {
                x_max = s.xValues.back();
            }
        }
    }
    if (!has_any_data)
    {
        return result;
    }

    result.series  = std::move(series);
    result.xMax    = x_max;
    result.success = true;
    return result;
}

bool CsvSeriesParser::parseCombinedTimestamp(std::string_view stamp, int& out_day, double& out_seconds)
{
    // Format: "DDD:HH:MM:SS.mmm"
    std::vector<std::string_view> parts;
    splitKeepEmpty(stamp, ':', parts);
    if (parts.size() != 4)
    {
        return false;
    }

    int    day = 0;
    int    hours = 0;
    int    minutes = 0;
    double seconds = 0.0;   // "SS.mmm"
    if (!textparse::parseInteger(std::string(parts[0]), day) || !textparse::parseInteger(std::string(parts[1]), hours) ||
        !textparse::parseInteger(std::string(parts[2]), minutes) || !textparse::parseDouble(std::string(parts[3]), seconds))
    {
        return false;
    }

    out_day     = day;
    out_seconds = (hours * TimeConstants::kSecondsPerHour) + (minutes * TimeConstants::kSecondsPerMinute) + seconds;
    return true;
}
