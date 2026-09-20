/**
 * @file csvseriesparser.cpp
 * @brief Implementation of CsvSeriesParser — reads the app's CSV export back into
 *        PlotSeriesData. Matched pair with PlotViewModel::exportCsv.
 */

#include "csvseriesparser.h"

#include <QFile>
#include <QMap>
#include <QTextStream>
#include <QVarLengthArray>

#include "constants.h"
#include "seriescolumnschema.h"

CsvParseResult CsvSeriesParser::parse(const QString& filepath)
{
    CsvParseResult result;

    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return result;
    }

    QTextStream stream(&file);

    // Parse header line: "Time (DOY:HH:MM:SS.mmm),series1,series2,..."
    QString header_line = stream.readLine();
    if (header_line.isEmpty())
    {
        return result;
    }

    QStringList columns = header_line.split(',');
    // Need the time column plus at least one series column.
    if (columns.size() < 2)
    {
        return result;
    }
    // Reject anything that isn't this app's export format (e.g. the legacy
    // "Day,Time,..." layout), so an unrecognized file fails cleanly rather than
    // being mis-parsed into garbage series.
    if (columns[0].trimmed() != QLatin1String(PlotConstants::kCsvTimeHeader))
    {
        return result;
    }

    // Build series list from columns 1..N (skip the time column).
    int param_count = static_cast<int>(columns.size() - 1);
    QVector<PlotSeriesData> series(param_count);

    // Count channels per (stream, receiver) for shade/channel-index assignment
    // (SNR series only) — mirrors the per-stream counting in addStreamData so
    // multiple SNR streams that reuse receiver numbers don't accumulate channel
    // indices across the whole file.
    // Keyed [sourceId][streamOrder][receiverIndex]: a multi-source export (Phase 1
    // multi-file input) can have two different sources reuse the same streamOrder,
    // so sourceId must be part of the key or their channel indices would
    // incorrectly accumulate together on import.
    QMap<int, QMap<int, QMap<int, int>>> stream_receiver_channel_count;

    for (int i = 0; i < param_count; i++)
    {
        PlotSeriesData& s = series[i];
        // Column identity (metric type, name, stream label/order, source, receiver)
        // comes from the shared schema — the exact inverse of the column headers
        // written by PlotViewModel::exportCsv().
        const SeriesColumnSchema::ParsedColumn col =
            SeriesColumnSchema::parseColumnHeader(columns[i + 1].trimmed());
        s.name          = col.name;
        s.streamLabel   = col.streamLabel;
        s.metricType    = col.metricType;
        s.streamOrder   = col.streamOrder;
        s.receiverIndex = col.receiverIndex;
        s.sourceId      = col.sourceId;

        // channelIndex is a per-(source, stream, receiver) running count assigned
        // here as columns are walked left to right (SNR series only), so multiple
        // SNR streams that reuse receiver numbers don't accumulate indices across
        // the whole file (or across sources).
        if (s.metricType == PlotSeriesData::MetricType::SNR)
        {
            QMap<int, int>& receiver_channel_count =
                stream_receiver_channel_count[s.sourceId][s.streamOrder];
            s.channelIndex = receiver_channel_count.value(s.receiverIndex, 0);
            receiver_channel_count[s.receiverIndex]++;
        }
    }

    // Estimate row count from remaining file size for pre-allocation.
    constexpr int kBytesPerRowEstimate = 20;
    constexpr int kMinRowsEstimate = 100;
    qint64 remaining_bytes = file.size() - stream.pos();
    int estimated_rows = static_cast<int>(remaining_bytes / (param_count * 8 + kBytesPerRowEstimate));
    estimated_rows = qMax(estimated_rows, kMinRowsEstimate);
    for (int i = 0; i < param_count; i++)
    {
        series[i].xValues.reserve(estimated_rows);
        series[i].yValues.reserve(estimated_rows);
    }

    // Parse data rows (writes base_day / base_time_offset / skipped count directly).
    parseDataRows(stream, param_count, series, result.baseDay, result.baseTimeOffset,
                  result.skippedRows);

    file.close();

    // Verify we got data.
    bool has_any_data = false;
    for (const auto& s : series)
    {
        if (!s.xValues.isEmpty())
        {
            has_any_data = true;
            break;
        }
    }

    if (!has_any_data)
    {
        return result;
    }

    // Compute xMax from data (cheap here while data is hot).
    double x_max = 0.0;
    for (const auto& s : series)
    {
        if (!s.xValues.isEmpty() && s.xValues.last() > x_max)
        {
            x_max = s.xValues.last();
        }
    }

    result.series  = std::move(series);
    result.xMax    = x_max;
    result.success = true;
    return result;
}

void CsvSeriesParser::parseDataRows(QTextStream& stream, int param_count,
                                    QVector<PlotSeriesData>& series,
                                    int& out_base_day, double& out_base_time_offset,
                                    int& out_skipped_rows)
{
    double first_time = -1.0;
    int first_day = 0;

    while (!stream.atEnd())
    {
        QString line = stream.readLine();
        if (line.isEmpty())
        {
            continue;
        }

        // Tokenize into QStringViews over `line` — no per-field QString allocation.
        QVarLengthArray<QStringView, 64> fields;
        for (QStringView token : qTokenize(QStringView{line}, u',', Qt::KeepEmptyParts))
        {
            fields.append(token);
        }
        if (fields.size() < param_count + 1)
        {
            out_skipped_rows++;
            continue;
        }

        int day = 0;
        double time_seconds = 0.0;
        if (!parseCombinedTimestamp(fields[0], day, time_seconds))
        {
            out_skipped_rows++;
            continue;
        }

        // Convert DOY + time to elapsed seconds from the first sample.
        if (first_time < 0.0)
        {
            first_day            = day;
            first_time           = time_seconds;
            out_base_day         = day;
            out_base_time_offset = time_seconds;
        }

        double elapsed = ((day - first_day) * UIConstants::kSecondsPerDay)
                         + (time_seconds - first_time);

        for (int i = 0; i < param_count; i++)
        {
            bool ok = false;
            double value = fields[i + 1].toDouble(&ok);
            if (ok)  // an empty cell (sparse series) parses as not-ok and is skipped
            {
                series[i].xValues.append(elapsed);
                series[i].yValues.append(value);
                series[i].yMinCached = qMin(series[i].yMinCached, value);
                series[i].yMaxCached = qMax(series[i].yMaxCached, value);
            }
        }
    }
}

bool CsvSeriesParser::parseCombinedTimestamp(QStringView stamp, int& out_day, double& out_seconds)
{
    // Format: "DDD:HH:MM:SS.mmm"
    QVarLengthArray<QStringView, 4> parts;
    for (QStringView token : qTokenize(stamp, u':', Qt::KeepEmptyParts))
    {
        parts.append(token);
    }
    if (parts.size() != 4)
    {
        return false;
    }

    bool ok_d = false;
    bool ok_h = false;
    bool ok_m = false;
    bool ok_s = false;
    int    day     = parts[0].toInt(&ok_d);
    int    hours   = parts[1].toInt(&ok_h);
    int    minutes = parts[2].toInt(&ok_m);
    double seconds = parts[3].toDouble(&ok_s);  // "SS.mmm"
    if (!ok_d || !ok_h || !ok_m || !ok_s)
    {
        return false;
    }

    out_day     = day;
    out_seconds = (hours * UIConstants::kSecondsPerHour)
                  + (minutes * UIConstants::kSecondsPerMinute)
                  + seconds;
    return true;
}
