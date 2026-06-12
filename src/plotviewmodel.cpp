/**
 * @file plotviewmodel.cpp
 * @brief Implementation of PlotViewModel — CSV parsing, series data, axis state.
 */

#include "plotviewmodel.h"

#include <ctime>

#include <QtConcurrent/QtConcurrent>
#include <QtMath>

#include <QFile>
#include <QMap>
#include <QTextStream>

#include "constants.h"

PlotViewModel::PlotViewModel(QObject* parent)
    : QObject(parent)
    , m_plot_title(PlotConstants::kDefaultPlotTitle)
{
}

// ---------------------------------------------------------------------------
// Static parse helpers
// ---------------------------------------------------------------------------

CsvParseResult PlotViewModel::parseCsvData(const QString& filepath)
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
    parseCsvDataRows(stream, param_count, series, result.baseDay, result.baseTimeOffset);

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

void PlotViewModel::parseCsvDataRows(QTextStream& stream, int param_count,
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

// ---------------------------------------------------------------------------
// Commit helper — shared by loadCsvFile and onParseFinished
// ---------------------------------------------------------------------------

void PlotViewModel::commitParseResult(CsvParseResult&& result)
{
    m_series            = std::move(result.series);
    m_base_day          = result.baseDay;
    m_base_time_offset  = result.baseTimeOffset;
    m_x_min             = 0.0;
    m_x_max             = result.xMax;
    m_x_view_min        = m_x_min;
    m_x_view_max        = m_x_max;

    m_has_lock_series = false;
    m_has_missed_frames_series = false;
    for (const auto& s : m_series)
    {
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
        {
            m_has_lock_series = true;
        }
        else if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
        {
            m_has_missed_frames_series = true;
        }
    }

    assignColors();
    computeYRange();

    emit dataChanged();
}

// ---------------------------------------------------------------------------
// Public data loading API
// ---------------------------------------------------------------------------

bool PlotViewModel::loadCsvFile(const QString& filepath)
{
    CsvParseResult result = parseCsvData(filepath);
    if (!result.success)
    {
        return false;
    }
    commitParseResult(std::move(result));
    return true;
}

void PlotViewModel::loadCsvFileAsync(const QString& filepath)
{
    if (m_loading)
    {
        return;  // drop concurrent requests
    }
    m_loading = true;
    emit loadStarted();

    if (m_parse_watcher == nullptr)
    {
        m_parse_watcher = new QFutureWatcher<CsvParseResult>(this);
        connect(m_parse_watcher, &QFutureWatcher<CsvParseResult>::finished,
                this, &PlotViewModel::onParseFinished);
    }

    m_parse_watcher->setFuture(QtConcurrent::run(&PlotViewModel::parseCsvData, filepath));
}

void PlotViewModel::onParseFinished()
{
    CsvParseResult result = m_parse_watcher->result();
    m_loading = false;

    if (!result.success)
    {
        emit loadFailed();
        return;
    }

    commitParseResult(std::move(result));
}

// ---------------------------------------------------------------------------
// In-memory stream accumulation
// ---------------------------------------------------------------------------

void PlotViewModel::addStreamData(const ProcessedStreamData& data)
{
    if (!data.hasSamples())
    {
        return;
    }

    // Establish the shared time base from the first accumulated sample so that
    // streams added later line up on the same elapsed-seconds X axis.
    if (m_series.isEmpty())
    {
        m_base_abs_seconds = data.timesSec.first();
        auto epoch = static_cast<time_t>(m_base_abs_seconds);
        struct tm* t = gmtime(&epoch);
        if (t != nullptr)
        {
            m_base_day = t->tm_yday + 1;
            m_base_time_offset = (t->tm_hour * UIConstants::kSecondsPerHour)
                               + (t->tm_min * UIConstants::kSecondsPerMinute)
                               + t->tm_sec
                               + (m_base_abs_seconds - static_cast<double>(epoch));
        }
    }

    const int sample_count = static_cast<int>(data.timesSec.size());

    // Pre-compute elapsed seconds (shared by every series in this stream).
    QVector<double> elapsed(sample_count);
    for (int i = 0; i < sample_count; i++)
    {
        elapsed[i] = data.timesSec[i] - m_base_abs_seconds;
    }

    auto fillCaches = [](PlotSeriesData& s) {
        for (double v : s.yValues)
        {
            s.yMinCached = qMin(s.yMinCached, v);
            s.yMaxCached = qMax(s.yMaxCached, v);
        }
    };

    // Frame-sync lock series — present for both lock-only and receiver modes.
    if (!data.lockPercent.isEmpty())
    {
        PlotSeriesData lock;
        lock.name = data.streamLabel + " Lock (%)";
        lock.metricType = PlotSeriesData::MetricType::FrameSyncLock;
        lock.receiverIndex = 0;
        lock.channelIndex = 0;
        lock.streamOrder = data.pcmChannelId;
        lock.xValues = elapsed;
        lock.yValues = data.lockPercent;
        lock.visible = (m_lock_axis_view == LockAxisView::LockPercent);
        fillCaches(lock);
        m_series.push_back(lock);
    }

    // Missed frames accumulation series — left-axis sibling of the lock series,
    // shown only when the left-axis view is in MissedFrames mode.
    if (!data.accumulatedMissedFrames.isEmpty())
    {
        PlotSeriesData errors;
        errors.name = data.streamLabel + " Accumulated Missed Frames";
        errors.metricType = PlotSeriesData::MetricType::AccumulatedMissedFrames;
        errors.receiverIndex = 0;
        errors.channelIndex = 0;
        errors.streamOrder = data.pcmChannelId;
        errors.xValues = elapsed;
        errors.yValues = data.accumulatedMissedFrames;
        errors.visible = (m_lock_axis_view == LockAxisView::MissedFrames);
        fillCaches(errors);
        m_series.push_back(errors);
    }

    // Receiver-channel (SNR) series — one per processed parameter word.
    QMap<int, int> receiver_channel_count;
    for (const auto& ch : data.channels)
    {
        PlotSeriesData s;
        s.name = data.streamLabel + " " + ch.name;
        s.metricType = PlotSeriesData::MetricType::SNR;
        int rcvr_pos = static_cast<int>(ch.name.lastIndexOf("_RCVR"));
        if (rcvr_pos >= 0)
        {
            bool ok = false;
            int rcvr_num = ch.name.mid(rcvr_pos + 5).toInt(&ok);
            s.receiverIndex = ok ? rcvr_num : 0;
        }
        s.channelIndex = receiver_channel_count.value(s.receiverIndex, 0);
        receiver_channel_count[s.receiverIndex]++;
        s.streamOrder = data.pcmChannelId;
        s.xValues = elapsed;
        s.yValues = ch.values;
        fillCaches(s);
        m_series.push_back(s);
    }

    // Recompute global X range and refresh the viewport to show all data.
    m_x_min = 0.0;
    for (const auto& s : m_series)
    {
        if (!s.xValues.isEmpty())
        {
            m_x_max = qMax(m_x_max, s.xValues.last());
        }
    }
    m_x_view_min = m_x_min;
    m_x_view_max = m_x_max;

    m_has_lock_series = false;
    m_has_missed_frames_series = false;
    for (const auto& s : m_series)
    {
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
        {
            m_has_lock_series = true;
        }
        else if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
        {
            m_has_missed_frames_series = true;
        }
    }

    assignColors();
    computeYRange();

    emit dataChanged();
}

// ---------------------------------------------------------------------------
// clearData
// ---------------------------------------------------------------------------

void PlotViewModel::clearData()
{
    m_series.clear();
    m_x_min = m_x_max = 0.0;
    m_x_view_min = m_x_view_max = 0.0;
    m_data_y_min = m_data_y_max = 0.0;
    m_y_auto_scale = true;
    m_has_lock_series = false;
    m_has_missed_frames_series = false;
    m_lock_axis_view = LockAxisView::LockPercent;
    m_base_day = 0;
    m_base_time_offset = 0.0;
    m_base_abs_seconds = 0.0;
    m_plot_title = PlotConstants::kDefaultPlotTitle;
    emit dataChanged();
}

bool PlotViewModel::exportCsv(const QString& filepath) const
{
    if (m_series.isEmpty()) return false;

    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream out(&file);

    // Write header
    out << "Time (DOY:HH:MM:SS.mmm)";
    for (const auto& s : m_series)
    {
        out << "," << s.name;
    }
    out << "\n";

    // Map each rounded millisecond timestamp to a map of series index -> value
    QMap<qint64, QMap<int, double>> rows;

    for (int i = 0; i < m_series.size(); ++i)
    {
        const auto& s = m_series[i];
        for (int j = 0; j < s.xValues.size(); ++j)
        {
            qint64 ms = qRound(s.xValues[j] * 1000.0);
            rows[ms][i] = s.yValues[j];
        }
    }

    // Write rows
    for (auto it = rows.begin(); it != rows.end(); ++it)
    {
        qint64 ms = it.key();
        double elapsed = ms / 1000.0;
        
        // Format time
        double total_sec = (m_base_day * 86400.0) + m_base_time_offset + elapsed;
        int d = static_cast<int>(total_sec / 86400);
        double rem = total_sec - (d * 86400);
        int h = static_cast<int>(rem / 3600);
        rem -= h * 3600;
        int m = static_cast<int>(rem / 60);
        rem -= m * 60;
        int s = static_cast<int>(rem);
        int msec = static_cast<int>((rem - s) * 1000.0 + 0.5);

        out << QString("%1:%2:%3:%4.%5")
            .arg(d, 3, 10, QChar('0'))
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'))
            .arg(msec, 3, 10, QChar('0'));

        const auto& vals = it.value();
        for (int i = 0; i < m_series.size(); ++i)
        {
            if (vals.contains(i))
            {
                out << "," << QString::number(vals[i], 'f', 6);
            }
            else
            {
                out << ",";
            }
        }
        out << "\n";
    }

    file.close();
    return true;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool PlotViewModel::hasData() const
{
    return !m_series.isEmpty();
}

bool PlotViewModel::isLoading() const
{
    return m_loading;
}

int PlotViewModel::seriesCount() const
{
    return static_cast<int>(m_series.size());
}

const PlotSeriesData& PlotViewModel::seriesAt(int index) const
{
    return m_series.at(index);
}

const QVector<PlotSeriesData>& PlotViewModel::allSeries() const
{
    return m_series;
}

QString PlotViewModel::plotTitle() const
{
    return m_plot_title;
}

double PlotViewModel::xMin() const
{
    return m_x_min;
}

double PlotViewModel::xMax() const
{
    return m_x_max;
}

double PlotViewModel::yMin() const
{
    return m_y_auto_scale ? m_data_y_min : m_y_manual_min;
}

double PlotViewModel::yMax() const
{
    return m_y_auto_scale ? m_data_y_max : m_y_manual_max;
}

double PlotViewModel::dataYMin() const
{
    return m_data_y_min;
}

double PlotViewModel::dataYMax() const
{
    return m_data_y_max;
}

bool PlotViewModel::yAutoScale() const
{
    return m_y_auto_scale;
}

double PlotViewModel::xViewMin() const
{
    return m_x_view_min;
}

double PlotViewModel::xViewMax() const
{
    return m_x_view_max;
}

// ---------------------------------------------------------------------------
// Mutators
// ---------------------------------------------------------------------------

void PlotViewModel::setSeriesVisible(int index, bool visible)
{
    if (index < 0 || index >= m_series.size())
    {
        return;
    }
    if (m_series[index].visible == visible)
    {
        return;
    }

    m_series[index].visible = visible;
    emit seriesVisibilityChanged(index);

    if (m_y_auto_scale)
    {
        computeYRange();
        emit axisRangeChanged();
    }
}

void PlotViewModel::setPlotTitle(const QString& title)
{
    if (m_plot_title == title)
    {
        return;
    }
    m_plot_title = title;
    emit plotTitleChanged();
}

void PlotViewModel::setYManualRange(double min, double max)
{
    m_y_manual_min = min;
    m_y_manual_max = max;
    m_y_auto_scale = false;
    emit axisRangeChanged();
}

void PlotViewModel::setYAutoScale(bool enabled)
{
    if (m_y_auto_scale == enabled)
    {
        return;
    }
    m_y_auto_scale = enabled;
    if (enabled)
    {
        computeYRange();
    }
    emit axisRangeChanged();
}

void PlotViewModel::setXViewRange(double min, double max)
{
    m_x_view_min = min;
    m_x_view_max = max;
    emit axisRangeChanged();
}

void PlotViewModel::resetXRange()
{
    m_x_view_min = m_x_min;
    m_x_view_max = m_x_max;
    emit axisRangeChanged();
}

void PlotViewModel::resetYRange()
{
    m_y_auto_scale = true;
    computeYRange();
    emit axisRangeChanged();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void PlotViewModel::assignColors()
{
    // Left-axis metrics (lock % and frame-sync errors) share one color per stream,
    // keyed by source PCM channel, so a stream's lock and error curves match (only
    // one is ever visible at a time).
    QMap<int, int> left_axis_color_idx;
    int next_left_color = 0;

    for (auto& s : m_series)
    {
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock ||
            s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
        {
            auto it = left_axis_color_idx.find(s.streamOrder);
            int idx = 0;
            if (it != left_axis_color_idx.end())
            {
                idx = it.value();
            }
            else
            {
                idx = next_left_color++;
                left_axis_color_idx.insert(s.streamOrder, idx);
            }
            int num_primaries = PlotConstants::kNumFrameSyncLockPrimaryColors;
            QColor primary = PlotConstants::kFrameSyncLockPrimaryColors[idx % num_primaries];
            s.color = shadeOfColor(primary, idx / num_primaries);
            continue;
        }

        int num_primaries = PlotConstants::kNumSnrPrimaryColors;
        int receiver_idx = qMax(0, s.receiverIndex - 1);
        QColor primary = PlotConstants::kSnrPrimaryColors[receiver_idx % num_primaries];

        // Shade level combines the receiver's primary-color cycle with its channel index,
        // so additional receivers and additional channels both shift toward a new shade.
        int shade_level = (receiver_idx / num_primaries) + s.channelIndex;
        s.color = shadeOfColor(primary, shade_level);
    }
}

QColor PlotViewModel::shadeOfColor(const QColor& base, int shadeLevel)
{
    if (shadeLevel <= 0)
    {
        return base;
    }

    int h = 0;
    int sat = 0;
    int val = 0;
    base.getHsv(&h, &sat, &val);

    // Reduce saturation and increase value per shade level for a progressively lighter tint.
    constexpr int kMinSaturation = 40;
    constexpr int kSaturationStep = 60;
    constexpr int kMaxValue = 255;
    constexpr int kValueStep = 20;
    sat = qMax(kMinSaturation, sat - (shadeLevel * kSaturationStep));
    val = qMin(kMaxValue, val + (shadeLevel * kValueStep));

    QColor shaded;
    shaded.setHsv(h, sat, val);
    return shaded;
}

void PlotViewModel::computeYRange()
{
    double y_min = std::numeric_limits<double>::max();
    double y_max = std::numeric_limits<double>::lowest();
    bool has_visible = false;

    for (const auto& s : m_series)
    {
        if (!s.visible || s.yValues.isEmpty())
        {
            continue;
        }
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock ||
            s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
        {
            continue;  // Left-axis metrics use their own range, not the SNR range.
        }

        has_visible = true;
        y_min = qMin(y_min, s.yMinCached);
        y_max = qMax(y_max, s.yMaxCached);
    }

    if (!has_visible)
    {
        m_data_y_min = 0.0;
        m_data_y_max = 1.0;
        return;
    }

    // Round to nearest 5 dB, clip min at 0
    constexpr double kRoundingStep = 5.0;
    m_data_y_min = qMax(0.0, qFloor(y_min / kRoundingStep) * kRoundingStep);
    m_data_y_max = qCeil(y_max / kRoundingStep) * kRoundingStep;
    if (m_data_y_max <= m_data_y_min)
    {
        m_data_y_max = m_data_y_min + kRoundingStep;
    }
}

double PlotViewModel::parseTimeToSeconds(const QString& time_str)
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

int PlotViewModel::baseDay() const { return m_base_day; }
double PlotViewModel::baseTimeOffset() const { return m_base_time_offset; }
double PlotViewModel::lockYMin() const { return m_lock_y_min; }
double PlotViewModel::lockYMax() const { return m_lock_y_max; }
bool PlotViewModel::hasLockSeries() const { return m_has_lock_series; }
bool PlotViewModel::hasMissedFramesSeries() const { return m_has_missed_frames_series; }
PlotViewModel::LockAxisView PlotViewModel::lockAxisView() const { return m_lock_axis_view; }

double PlotViewModel::missedFramesMax() const
{
    double max_val = 0.0;
    for (const auto& s : m_series)
    {
        if (s.metricType != PlotSeriesData::MetricType::AccumulatedMissedFrames || !s.visible)
        {
            continue;
        }
        max_val = qMax(max_val, s.yMaxCached);
    }
    // Never return a degenerate range; a flat (no-error) stream still needs a
    // visible Y axis scale, e.g. [0, 10]
    if (max_val <= 0.0)
    {
        return 10.0;
    }
    return max_val;
}

void PlotViewModel::setLockAxisView(LockAxisView view)
{
    if (m_lock_axis_view == view)
    {
        return;
    }
    m_lock_axis_view = view;

    // Flip visibility: lock series shown in LockPercent mode, missed frames series in
    // MissedFrames mode. SNR series are unaffected.
    for (int i = 0; i < m_series.size(); i++)
    {
        PlotSeriesData& s = m_series[i];
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
        {
            s.visible = (view == LockAxisView::LockPercent);
        }
        else if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
        {
            s.visible = (view == LockAxisView::MissedFrames);
        }
    }

    emit lockAxisViewChanged();
    emit dataChanged();
}

QString PlotViewModel::formatTime(double elapsed) const
{
    double total = m_base_time_offset + elapsed;

    int day = m_base_day + static_cast<int>(total / UIConstants::kSecondsPerDay);
    total = fmod(total, static_cast<double>(UIConstants::kSecondsPerDay));
    if (total < 0.0)
    {
        total += UIConstants::kSecondsPerDay;
        day--;
    }

    int hours   = static_cast<int>(total) / UIConstants::kSecondsPerHour;
    int minutes = (static_cast<int>(total) % UIConstants::kSecondsPerHour) / UIConstants::kSecondsPerMinute;
    int seconds = static_cast<int>(total) % UIConstants::kSecondsPerMinute;

    constexpr int kBase10 = 10;
    return QString("%1:%2:%3:%4")
        .arg(day, 3, kBase10, QChar('0'))
        .arg(hours, 2, kBase10, QChar('0'))
        .arg(minutes, 2, kBase10, QChar('0'))
        .arg(seconds, 2, kBase10, QChar('0'));
}
