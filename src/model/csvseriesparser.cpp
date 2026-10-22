/**
 * @file csvseriesparser.cpp
 * @brief Implementation of CsvSeriesParser — reads the app's CSV export back into series.
 *        Matched pair with CsvSeriesWriter.
 */

#include "csvseriesparser.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include "constants.h"
#include "seriescolumnschema.h"
#include "textfile.h"
#include "textparse.h"

namespace {

/// What `qMin` / `qMax` do with a NaN: they are `a < b ? a : b`, not `std::min`, and the cached range
/// of a series with a NaN in it depends on it.
double qtMin(double a, double b) { return (a < b) ? a : b; }
double qtMax(double a, double b) { return (a < b) ? b : a; }

/// The lines of the file as UTF-8. A UTF-16 file (with a BOM) is read whole and transcoded; anything
/// else is streamed, with a UTF-8 BOM dropped.
class LineSource
{
public:
    explicit LineSource(const std::filesystem::path& path)
    {
        m_file.open(path, std::ios::binary);
        if (!m_file)
        {
            return;
        }
        m_file.seekg(0, std::ios::end);
        m_size = static_cast<std::size_t>(m_file.tellg());
        m_file.seekg(0, std::ios::beg);

        unsigned char head[3] = {0, 0, 0};
        m_file.read(reinterpret_cast<char*>(head), 3);
        const std::streamsize got = m_file.gcount();
        m_file.clear();
        m_file.seekg(0, std::ios::beg);

        const bool utf16 = got >= 2 && ((head[0] == 0xFF && head[1] == 0xFE) || (head[0] == 0xFE && head[1] == 0xFF));
        if (utf16)
        {
            std::ostringstream all;
            all << m_file.rdbuf();
            m_text = textfile::decodeText(all.str());
            m_in_memory = true;
            m_size = m_text.size();
        }
        else if (got >= 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF)
        {
            m_file.seekg(3, std::ios::beg);
            m_consumed = 3;
        }
    }

    bool isOpen() const { return m_in_memory || m_file.is_open(); }

    /// Bytes of the file not read yet (an estimate for pre-allocation only).
    std::size_t remaining() const { return m_size > m_consumed ? m_size - m_consumed : 0; }

    /// The next line without its terminator. @return false at the end of the file.
    bool next(std::string& line)
    {
        bool got = false;
        if (m_in_memory)
        {
            if (m_pos >= m_text.size())
            {
                return false;
            }
            const std::size_t end = m_text.find('\n', m_pos);
            if (end == std::string::npos)
            {
                line.assign(m_text, m_pos, std::string::npos);
                m_pos = m_text.size();
            }
            else
            {
                line.assign(m_text, m_pos, end - m_pos);
                m_pos = end + 1;
            }
            m_consumed = m_pos;
            got = true;
        }
        else
        {
            if (!std::getline(m_file, line, '\n'))
            {
                return false;
            }
            m_consumed += line.size() + 1;
            got = true;
        }
        // Every carriage return is ignored, wherever it is: the importer read through a Windows
        // text-mode QFile, which drops them all (not just the one before a line feed), so a lone one
        // neither ends a line nor stays in a name.
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        return got;
    }

private:
    std::ifstream m_file;
    std::string m_text;
    bool m_in_memory = false;
    std::size_t m_pos = 0;
    std::size_t m_size = 0;
    std::size_t m_consumed = 0;
};

/// Splits @p text at every @p sep, keeping empty parts (so "a,,b" is three).
void splitKeepEmpty(std::string_view text, char sep, std::vector<std::string_view>& out)
{
    out.clear();
    std::size_t start = 0;
    for (;;)
    {
        const std::size_t at = text.find(sep, start);
        if (at == std::string_view::npos)
        {
            out.push_back(text.substr(start));
            return;
        }
        out.push_back(text.substr(start, at - start));
        start = at + 1;
    }
}

} // namespace

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
    // the per-stream counting in PlotViewModel::addStreamData, so multiple SNR streams that reuse
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

        const double elapsed = ((day - first_day) * UIConstants::kSecondsPerDay) + (time_seconds - first_time);

        for (int i = 0; i < param_count; ++i)
        {
            double value = 0.0;
            if (textparse::parseDouble(std::string(fields[static_cast<std::size_t>(i) + 1]), value))
            {   // an empty cell (sparse series) parses as not-ok and is skipped
                CsvSeries& s = series[static_cast<std::size_t>(i)];
                s.xValues.push_back(elapsed);
                s.yValues.push_back(value);
                s.yMin = qtMin(s.yMin, value);
                s.yMax = qtMax(s.yMax, value);
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
    out_seconds = (hours * UIConstants::kSecondsPerHour) + (minutes * UIConstants::kSecondsPerMinute) + seconds;
    return true;
}
