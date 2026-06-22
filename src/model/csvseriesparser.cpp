/**
 * @file csvseriesparser.cpp
 * @brief Implementation of CsvSeriesParser — FrameProcessor CSV -> PlotSeriesData.
 */

#include "csvseriesparser.h"

#include <QFile>
#include <QMap>
#include <QTextStream>
#include <QtMath>

#include "constants.h"

CsvParseResult CsvSeriesParser::parse(const QString& filepath)
{
    CsvParseResult result;

    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return result;
    }

    QTextStream stream(&file);

    // Parse header line: "Day,Time,param1,param2,..."
    QString header_line = stream.readLine();
    if (header_line.isEmpty())
    {
        return result;
    }

    QStringList columns = header_line.split(',');
    if (columns.size() < 3)
    {
        return result;
    }

    // Build series list from columns 2..N (skip Day, Time)
    int param_count = static_cast<int>(columns.size() - 2);
    QVector<PlotSeriesData> series(param_count);

    // Count channels per receiver for shade assignment (SNR series only)
    QMap<int, int> receiver_channel_count;

    static const QString kLockColumnName = "Framesync Lock (%)";

    for (int i = 0; i < param_count; i++)
    {
        PlotSeriesData& s = series[i];
        s.name = columns[i + 2].trimmed();

        if (s.name == kLockColumnName)
        {
            s.metricType = PlotSeriesData::MetricType::FrameSyncLock;
            s.receiverIndex = 0;
            s.channelIndex  = 0;
        }
        else
        {
            s.metricType = PlotSeriesData::MetricType::SNR;
            // Extract receiver index from "_RCVR<N>" suffix
            int rcvr_pos = static_cast<int>(s.name.lastIndexOf("_RCVR"));
            if (rcvr_pos >= 0)
            {
                bool ok = false;
                int rcvr_num = s.name.mid(rcvr_pos + 5).toInt(&ok);
                s.receiverIndex = ok ? rcvr_num : 0;
            }
            s.channelIndex = receiver_channel_count.value(s.receiverIndex, 0);
            receiver_channel_count[s.receiverIndex]++;
        }
    }

    // Estimate row count from remaining file size for pre-allocation
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

    // Parse data rows (writes base_day / base_time_offset into result directly)
    parseDataRows(stream, param_count, series, result.baseDay, result.baseTimeOffset);

    file.close();

    // Verify we got data
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

    // Compute xMax from data (cheap here while data is hot)
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
                                    int& out_base_day, double& out_base_time_offset)
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

        QStringList fields = line.split(',');
        if (fields.size() < param_count + 2)
        {
            continue;
        }

        int day = fields[0].toInt();
        double time_seconds = parseTimeToSeconds(fields[1]);

        // Convert DOY + time to elapsed seconds from first sample
        if (first_time < 0.0)
        {
            first_day         = day;
            first_time        = time_seconds;
            out_base_day      = day;
            out_base_time_offset = time_seconds;
        }

        double elapsed = ((day - first_day) * UIConstants::kSecondsPerDay)
                         + (time_seconds - first_time);

        for (int i = 0; i < param_count; i++)
        {
            bool ok = false;
            double value = fields[i + 2].toDouble(&ok);
            if (ok)
            {
                series[i].xValues.append(elapsed);
                series[i].yValues.append(value);
                series[i].yMinCached = qMin(series[i].yMinCached, value);
                series[i].yMaxCached = qMax(series[i].yMaxCached, value);
            }
        }
    }
}

double CsvSeriesParser::parseTimeToSeconds(const QString& time_str)
{
    // Format: "HH:MM:SS.mmm"
    QStringList parts = time_str.split(':');
    if (parts.size() != 3)
    {
        return 0.0;
    }

    int hours = parts[0].toInt();
    int minutes = parts[1].toInt();

    // Seconds may include milliseconds: "SS.mmm"
    double seconds = parts[2].toDouble();

    return (hours * UIConstants::kSecondsPerHour)
           + (minutes * UIConstants::kSecondsPerMinute)
           + seconds;
}
