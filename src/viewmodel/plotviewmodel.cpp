/**
 * @file plotviewmodel.cpp
 * @brief Implementation of PlotViewModel — series state, axis ranges, colors.
 */

#include "plotviewmodel.h"

#include <algorithm>
#include <ctime>

#include <QtConcurrent/QtConcurrent>
#include <QtMath>

#include <QFile>
#include <QMap>
#include <QTextStream>

#include "constants.h"
#include "csvseriesparser.h"
#include "seriescolumnschema.h"

PlotViewModel::PlotViewModel(QObject* parent)
    : QObject(parent)
    , m_plot_title(PlotConstants::kDefaultPlotTitle)
{
}

// ---------------------------------------------------------------------------
// Commit helper — shared by loadCsvFile and onParseFinished
// ---------------------------------------------------------------------------

void PlotViewModel::commitParseResult(CsvParseResult&& result)
{
    const int skipped_rows = result.skippedRows;
    m_series            = std::move(result.series);
    m_base_day          = result.baseDay;
    m_base_time_offset  = result.baseTimeOffset;
    m_x_min             = 0.0;
    m_x_max             = result.xMax;
    m_x_view_min        = m_x_min;
    m_x_view_max        = m_x_max;

    m_has_lock_series = false;
    m_has_missed_frames_series = false;

    // The parser yields presentation-neutral series. Mirror what addStreamData()
    // does for processed data so an imported plot looks identical to the live one:
    //   - assign a per-stream order so each stream's left-axis curve gets its own
    //     color (assignColors() keys left-axis colors by streamOrder; without this
    //     every lock curve collapses onto color 0).
    //   - set visibility from the active left-axis view so a stream's lock and
    //     missed-frames siblings are not drawn on top of each other.
    //
    // Receiver-SNR series carry the real PCM channel id (recovered from the CSV
    // column name by the parser); reuse it for a stream's lock/missed siblings so
    // both tabs of the Customize Plot Series dialog show the same "CH <id>" group.
    // Lock-only streams have no SNR counterpart, so allocate fresh ids above any
    // SNR id to avoid collisions.
    QMap<QString, int> label_to_stream_order;
    int next_stream_order = 0;
    for (const auto& s : m_series)
    {
        if (s.metricType == PlotSeriesData::MetricType::SNR && !s.streamLabel.isEmpty())
        {
            label_to_stream_order.insert(s.streamLabel, s.streamOrder);
        }
        next_stream_order = qMax(next_stream_order, s.streamOrder + 1);
    }

    for (auto& s : m_series)
    {
        s.id = m_next_series_id++;
        switch (s.metricType)
        {
        case PlotSeriesData::MetricType::FrameSyncLock:
        case PlotSeriesData::MetricType::AccumulatedMissedFrames:
        {
            auto it = label_to_stream_order.find(s.streamLabel);
            if (it == label_to_stream_order.end())
            {
                it = label_to_stream_order.insert(s.streamLabel, next_stream_order++);
            }
            s.streamOrder = it.value();
            const bool is_lock = (s.metricType == PlotSeriesData::MetricType::FrameSyncLock);
            s.visible = is_lock ? (m_lock_axis_view == LockAxisView::LockPercent)
                                : (m_lock_axis_view == LockAxisView::MissedFrames);
            if (is_lock) { m_has_lock_series = true; }
            else         { m_has_missed_frames_series = true; }
            break;
        }
        default:
            s.visible = true;
            break;
        }
    }

    assignColors();
    computeYRange();

    emit dataChanged();

    // Surface malformed-row skips so a partially-loaded file isn't silently
    // presented as complete (the file is a data-quality artifact itself).
    if (skipped_rows > 0)
    {
        emit loadWarning(QString("Skipped %1 malformed row(s) while loading the CSV.")
                             .arg(skipped_rows));
    }
}

// ---------------------------------------------------------------------------
// Public data loading API
// ---------------------------------------------------------------------------

bool PlotViewModel::loadCsvFile(const QString& filepath)
{
    CsvParseResult result = CsvSeriesParser::parse(filepath);
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

    m_parse_watcher->setFuture(QtConcurrent::run(&CsvSeriesParser::parse, filepath));
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

    commitParseResult(std::move(result));  // emits dataChanged()
    // Distinct from dataChanged() (which also fires for in-memory stream
    // processing and clearData()): a dedicated success signal lets the View
    // finalize an async import without guessing which dataChanged() it was.
    emit loadSucceeded();
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

    // Reprocessing a stream (e.g. clicking "Process" again after switching a
    // channel's mode) should replace its prior result, not stack a second copy
    // on top of it. Drop any existing series from a previous run on this same
    // stream before adding the new ones — otherwise a stale series from an
    // earlier mode (e.g. Frame Sync Lock) lingers on the plot indefinitely
    // alongside the new one, since addStreamData() only ever accumulates and
    // the plot is only cleared when a new file is opened, not on reprocess.
    // Matched on sourceId, streamLabel, AND pcmChannelId together: label alone
    // isn't a unique stream key (two configured streams can share a
    // TMATS-derived name), pcmChannelId alone isn't either (it defaults to 0
    // for streams without a real channel id, e.g. synthetic/test data), and in
    // a multi-file session two different .ch10 sources can legitimately reuse
    // the same channel id/label — requiring sourceId too ensures reprocessing
    // one source's stream never erases another source's identically-keyed one.
    m_series.erase(std::remove_if(m_series.begin(), m_series.end(),
                                   [&](const PlotSeriesData& s) {
                                       return s.sourceId == data.sourceId &&
                                              s.streamLabel == data.streamLabel &&
                                              s.streamOrder == data.pcmChannelId;
                                   }),
                   m_series.end());

    // Detect a source whose recording doesn't overlap the data already loaded
    // (e.g. two .ch10 files recorded on different days) -- informational only,
    // since the re-basing below keeps elapsed correct regardless (multi-file
    // input, docs/multi-file-input-design.md §3). Checked once, on the first
    // stream we see from this source in THIS add, using the range already at
    // hand (no extra file access): existing = [m_base_abs_seconds, m_base_abs_seconds
    // + m_x_max], since m_x_min is always 0.
    if (!m_series.isEmpty())
    {
        const bool source_already_present = std::any_of(m_series.cbegin(), m_series.cend(),
            [&](const PlotSeriesData& s) { return s.sourceId == data.sourceId; });
        if (!source_already_present)
        {
            const double existing_min_abs = m_base_abs_seconds;
            const double existing_max_abs = m_base_abs_seconds + m_x_max;
            const double new_min_abs = data.timesSec.first();
            const double new_max_abs = data.timesSec.last();
            const bool overlaps = new_min_abs <= existing_max_abs && existing_min_abs <= new_max_abs;
            if (!overlaps)
            {
                emit nonOverlappingSourceWarning(data.sourceId);
            }
        }
    }

    // Establish/maintain the shared time base so every source's samples line up
    // on the same elapsed-seconds X axis (multi-file input,
    // docs/multi-file-input-design.md §3). The base must be the EARLIEST
    // absolute sample across every source ever added, not just the first-added
    // one -- a later-added source (e.g. a second .ch10 file) can easily start
    // earlier than the first.
    if (m_series.isEmpty())
    {
        m_base_abs_seconds = data.timesSec.first();
        recomputeBaseDayAndOffset();
    }
    else if (data.timesSec.first() < m_base_abs_seconds)
    {
        // This source's earliest sample precedes everything already loaded.
        // Re-base: shift every existing series' elapsed seconds right by the
        // delta so elapsed stays >= 0 (all existing X-axis/formatTime() code
        // keeps working unchanged) while every sample's absolute-time
        // correlation across sources is preserved. O(total samples), a
        // one-time cost per add that starts a new earliest source.
        const double delta = m_base_abs_seconds - data.timesSec.first();
        for (PlotSeriesData& s : m_series)
        {
            for (double& x : s.xValues)
            {
                x += delta;
            }
        }
        m_base_abs_seconds = data.timesSec.first();
        recomputeBaseDayAndOffset();
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
        lock.name = data.streamLabel;
        lock.streamLabel = data.streamLabel;
        lock.metricType = PlotSeriesData::MetricType::FrameSyncLock;
        lock.receiverIndex = 0;
        lock.channelIndex = 0;
        lock.streamOrder = data.pcmChannelId;
        lock.streamSequence = data.jobIndex;
        lock.sourceId = data.sourceId;
        lock.id = m_next_series_id++;
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
        errors.name = data.streamLabel;
        errors.streamLabel = data.streamLabel;
        errors.metricType = PlotSeriesData::MetricType::AccumulatedMissedFrames;
        errors.receiverIndex = 0;
        errors.channelIndex = 0;
        errors.streamOrder = data.pcmChannelId;
        errors.streamSequence = data.jobIndex;
        errors.sourceId = data.sourceId;
        errors.id = m_next_series_id++;
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
        // Receiver SNR series keep the TMATS channel number as a prefix so multiple
        // receiver SNR streams (whose per-receiver channel names like L_RCVR1 would
        // otherwise collide in the legend) stay distinct; the Configure Streams dialog
        // and Frame Sync Lock series intentionally drop it. The exact name format —
        // and its inverse for CSV import — is owned by SeriesColumnSchema.
        s.name = SeriesColumnSchema::snrSeriesName(data.pcmChannelId, data.streamLabel, ch.name);
        s.streamLabel = data.streamLabel;
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
        s.streamSequence = data.jobIndex;
        s.sourceId = data.sourceId;
        s.id = m_next_series_id++;
        s.xValues = elapsed;
        s.yValues = ch.values;
        fillCaches(s);
        m_series.push_back(s);
    }

    // Keep legend order stable: sort by job submission index so streams always
    // appear as Stream 1, 2, 3 ... regardless of which parallel worker finished first.
    std::stable_sort(m_series.begin(), m_series.end(),
                     [](const PlotSeriesData& a, const PlotSeriesData& b) {
                         return a.streamSequence < b.streamSequence;
                     });

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

void PlotViewModel::removeSource(int sourceId)
{
    const bool had_match = std::any_of(m_series.cbegin(), m_series.cend(),
        [sourceId](const PlotSeriesData& s) { return s.sourceId == sourceId; });
    if (!had_match)
    {
        return;
    }

    m_series.erase(std::remove_if(m_series.begin(), m_series.end(),
                                   [sourceId](const PlotSeriesData& s) {
                                       return s.sourceId == sourceId;
                                   }),
                   m_series.end());

    if (m_series.isEmpty())
    {
        clearData();
        return;
    }

    // Mirror of the add-time re-base in addStreamData() (§3): if the removed
    // source held the earliest sample, the base must move forward to the new
    // earliest among what remains, shifting every remaining series LEFT so
    // elapsed stays correct relative to the new (later) base. Each series'
    // first sample is at elapsed xValues.first() against the OLD base, so its
    // absolute time is m_base_abs_seconds + xValues.first(); the new base is
    // the minimum of those across all remaining series.
    double new_base_abs = m_base_abs_seconds + m_series.first().xValues.first();
    for (const PlotSeriesData& s : m_series)
    {
        if (!s.xValues.isEmpty())
        {
            new_base_abs = qMin(new_base_abs, m_base_abs_seconds + s.xValues.first());
        }
    }
    const double delta = new_base_abs - m_base_abs_seconds; // >= 0
    if (delta > 0.0)
    {
        for (PlotSeriesData& s : m_series)
        {
            for (double& x : s.xValues)
            {
                x -= delta;
            }
        }
        m_base_abs_seconds = new_base_abs;
        recomputeBaseDayAndOffset();
    }

    // Recompute global X range/viewport, lock/missed-frames flags, and colors
    // from scratch (unlike addStreamData()'s incremental qMax, m_x_max must be
    // able to SHRINK here since data was removed, not just added).
    m_x_min = 0.0;
    m_x_max = 0.0;
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

    // Write header. Frame-sync series names are stored bare (no metric suffix) for
    // the plot legend; SeriesColumnSchema::columnHeader() qualifies them so the CSV
    // columns are self-describing (a stream's lock and missed-frames series share the
    // same bare name and would otherwise export as two identical headers) and stays
    // the inverse of the import parser.
    out << PlotConstants::kCsvTimeHeader;
    for (const auto& s : m_series)
    {
        out << "," << SeriesColumnSchema::columnHeader(s);
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

        out << formatTime(elapsed, /*includeMilliseconds=*/true);

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

int PlotViewModel::indexOfSeriesId(int id) const
{
    for (int i = 0; i < m_series.size(); ++i)
    {
        if (m_series.at(i).id == id)
            return i;
    }
    return -1;
}

const PlotSeriesData* PlotViewModel::seriesById(int id) const
{
    const int index = indexOfSeriesId(id);
    return index < 0 ? nullptr : &m_series.at(index);
}

// Id-based appearance edits — the public API. Each resolves the stable id to a
// current index and delegates to the (private, bounds-guarded) index-based method,
// which no-ops on the -1 returned for a missing id.
void PlotViewModel::renameSeriesById(int id, const QString& name)
{
    renameSeries(indexOfSeriesId(id), name);
}

void PlotViewModel::recolorSeriesById(int id, const QColor& color)
{
    recolorSeries(indexOfSeriesId(id), color);
}

void PlotViewModel::setSeriesVisibleById(int id, bool visible)
{
    setSeriesVisible(indexOfSeriesId(id), visible);
}

void PlotViewModel::setSeriesVisibleQuietById(int id, bool visible)
{
    setSeriesVisibleQuiet(indexOfSeriesId(id), visible);
}

const QVector<PlotSeriesData>& PlotViewModel::allSeries() const
{
    return m_series;
}

void PlotViewModel::renameSeries(int index, const QString& name)
{
    if (index < 0 || index >= m_series.size())
        return;

    m_series[index].name = name;

    // For frame sync series, keep the paired sibling (same stream, other metric)
    // in sync so a custom name survives switching between lock/missed-frames modes.
    // Capture just the small identity fields (not a full PlotSeriesData copy —
    // xValues/yValues can be large) before the loop mutates m_series.
    const QString renamed_label = m_series[index].streamLabel;
    const int renamed_order = m_series[index].streamOrder;
    const int renamed_source = m_series[index].sourceId;
    if (isLeftAxisMetric(m_series[index].metricType))
    {
        for (PlotSeriesData& s : m_series)
        {
            if (isFrameSyncSibling(renamed_label, renamed_order, renamed_source, s))
            {
                s.name = name;
            }
        }
    }
}

void PlotViewModel::commitAppearanceChanges()
{
    // Mirrors the per-call Y-range recompute that setSeriesVisible() does — a
    // batched setSeriesVisibleQuiet() edit skips that per-call, so it must happen
    // once here before views react to the appearance/visibility signal.
    if (m_y_auto_scale)
    {
        computeYRange();
        emit axisRangeChanged();
    }
    emit seriesAppearanceChanged();
}

void PlotViewModel::recolorSeries(int index, const QColor& color)
{
    if (index < 0 || index >= m_series.size())
        return;

    m_series[index].color = color;

    // For frame sync series, keep the paired sibling (same stream, other metric) in
    // sync so a custom color survives switching between lock/missed-frames modes —
    // mirrors renameSeries(). Capture just the identity fields, not a full copy.
    const QString recolored_label = m_series[index].streamLabel;
    const int recolored_order = m_series[index].streamOrder;
    const int recolored_source = m_series[index].sourceId;
    if (isLeftAxisMetric(m_series[index].metricType))
    {
        for (PlotSeriesData& s : m_series)
        {
            if (isFrameSyncSibling(recolored_label, recolored_order, recolored_source, s))
            {
                s.color = color;
            }
        }
    }
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
    if (m_right_y_max_user_set)
    {
        // Never let a user override drop below the current min — that would hand
        // QCPRange an inverted (max < min) range. Keep it at least a minimum span above.
        return qMax(m_right_y_max_user, yMin() + PlotConstants::kMinAxisSpan);
    }
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

void PlotViewModel::setSeriesVisibleQuiet(int index, bool visible)
{
    if (index < 0 || index >= m_series.size())
    {
        return;
    }
    m_series[index].visible = visible;
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
    m_left_y_max_user_set = false;
    m_right_y_max_user_set = false;
    computeYRange();
    emit axisRangeChanged();
}

void PlotViewModel::setLeftYMaxOverride(double max)
{
    m_left_y_max_user_set = true;
    m_left_y_max_user = max;
    emit axisRangeChanged();
}

void PlotViewModel::setRightYMaxOverride(double max)
{
    m_right_y_max_user_set = true;
    m_right_y_max_user = max;
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
        if (isLeftAxisMetric(s.metricType))
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

bool PlotViewModel::isLeftAxisMetric(PlotSeriesData::MetricType type)
{
    return type == PlotSeriesData::MetricType::FrameSyncLock ||
           type == PlotSeriesData::MetricType::AccumulatedMissedFrames;
}

bool PlotViewModel::isFrameSyncSibling(const QString& streamLabel, int streamOrder,
                                       int sourceId, const PlotSeriesData& candidate)
{
    return isLeftAxisMetric(candidate.metricType) &&
           candidate.streamLabel == streamLabel &&
           candidate.streamOrder == streamOrder &&
           candidate.sourceId == sourceId;
}

void PlotViewModel::recomputeBaseDayAndOffset()
{
    // Use a reentrant gmtime variant — plain gmtime() returns a pointer to a
    // shared static tm and is not thread-safe.
    std::time_t epoch = static_cast<std::time_t>(m_base_abs_seconds);
    std::tm tm_buf{};
#if defined(_WIN32)
    const bool ok = (gmtime_s(&tm_buf, &epoch) == 0);
#else
    const bool ok = (gmtime_r(&epoch, &tm_buf) != nullptr);
#endif
    if (ok)
    {
        m_base_day = tm_buf.tm_yday + 1;
        m_base_time_offset = (tm_buf.tm_hour * UIConstants::kSecondsPerHour)
                           + (tm_buf.tm_min * UIConstants::kSecondsPerMinute)
                           + tm_buf.tm_sec
                           + (m_base_abs_seconds - static_cast<double>(epoch));
    }
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
        if (isLeftAxisMetric(s.metricType))
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

int PlotViewModel::baseDay() const { return m_base_day; }
double PlotViewModel::baseTimeOffset() const { return m_base_time_offset; }
double PlotViewModel::lockYMin() const { return m_lock_y_min; }
double PlotViewModel::lockYMax() const { return m_lock_y_max; }
bool PlotViewModel::hasLockSeries() const { return m_has_lock_series; }
bool PlotViewModel::hasMissedFramesSeries() const { return m_has_missed_frames_series; }
PlotViewModel::LockAxisView PlotViewModel::lockAxisView() const { return m_lock_axis_view; }

double PlotViewModel::leftYMax() const
{
    if (m_left_y_max_user_set)
        return m_left_y_max_user;
    if (m_lock_axis_view == LockAxisView::MissedFrames)
        return missedFramesMax() * (1.0 + PlotConstants::kAxisMarginFactor);
    return 100.0;
}

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

    const LockAxisView old_view = m_lock_axis_view;
    m_lock_axis_view = view;
    m_left_y_max_user_set = false;

    auto metricMatchesView = [](PlotSeriesData::MetricType type, LockAxisView v) {
        return (v == LockAxisView::LockPercent)
            ? type == PlotSeriesData::MetricType::FrameSyncLock
            : type == PlotSeriesData::MetricType::AccumulatedMissedFrames;
    };

    // A stream is "selected" if its currently-active metric series is visible. Capture
    // that per-stream selection from the outgoing view so it can be carried over to the
    // incoming metric — otherwise switching modes would make every stream visible again
    // and discard the user's stream selection.
    QMap<int, bool> selected;
    for (const PlotSeriesData& s : m_series)
    {
        if (metricMatchesView(s.metricType, old_view))
        {
            selected.insert(s.streamOrder, s.visible);
        }
    }

    // Flip visibility: lock series shown in LockPercent mode, missed frames series in
    // MissedFrames mode — but only for streams that were selected. SNR series are
    // unaffected.
    for (int i = 0; i < m_series.size(); i++)
    {
        PlotSeriesData& s = m_series[i];
        if (isLeftAxisMetric(s.metricType))
        {
            s.visible = metricMatchesView(s.metricType, view)
                && selected.value(s.streamOrder, true);
        }
    }

    // No dataChanged(): the series and their data are unchanged — only per-series
    // visibility and the left-axis metric flip. The View reacts to lockAxisViewChanged
    // by syncing graph visibility in place instead of rebuilding the whole chart.
    emit axisRangeChanged();
    emit lockAxisViewChanged();
}

QString PlotViewModel::formatTime(double elapsed, bool includeMilliseconds) const
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
    QString result = QString("%1:%2:%3:%4")
        .arg(day, 3, kBase10, QChar('0'))
        .arg(hours, 2, kBase10, QChar('0'))
        .arg(minutes, 2, kBase10, QChar('0'))
        .arg(seconds, 2, kBase10, QChar('0'));

    if (includeMilliseconds)
    {
        // total's fractional part (below one second) survived the day/hour/minute/
        // second extraction above untouched, so it can be pulled out independently
        // without disturbing that (tested) integer-field logic.
        const int msec = static_cast<int>((total - qFloor(total)) * 1000.0 + 0.5);
        result += QString(".%1").arg(msec, 3, kBase10, QChar('0'));
    }
    return result;
}

double PlotViewModel::parseTime(const QString& text) const
{
    const auto parts = text.split(':');
    if (parts.size() != 4)
    {
        return 0.0;
    }
    const int day     = parts[0].toInt();
    const int hours   = parts[1].toInt();
    const int minutes = parts[2].toInt();
    const int seconds = parts[3].toInt();

    const double entered_absolute =
        day * static_cast<double>(UIConstants::kSecondsPerDay) +
        hours * static_cast<double>(UIConstants::kSecondsPerHour) +
        minutes * static_cast<double>(UIConstants::kSecondsPerMinute) + seconds;
    const double base_absolute =
        m_base_day * static_cast<double>(UIConstants::kSecondsPerDay) + m_base_time_offset;
    return entered_absolute - base_absolute;
}
