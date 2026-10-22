/**
 * @file seriesbuilder.cpp
 * @brief Implementation of the series construction, shared time base and import ordering.
 */

#include "seriesbuilder.h"

#include <algorithm>
#include <ctime>
#include <map>

#include "constants.h"
#include "seriescolumnschema.h"
#include "textparse.h"

namespace {

/// `qMin` / `qMax` are `a < b ? a : b` and `a < b ? b : a`, not `std::min` / `std::max`: they treat NaN
/// differently, and a series' cached range depends on it.
double qtMin(double a, double b) { return (a < b) ? a : b; }
double qtMax(double a, double b) { return (a < b) ? b : a; }

void fillRange(SeriesSpec& spec)
{
    for (const double v : spec.yValues)
    {
        spec.yMin = qtMin(spec.yMin, v);
        spec.yMax = qtMax(spec.yMax, v);
    }
}

} // namespace

StreamKey streamKeyOf(const ProcessedStreamData& data)
{
    return StreamKey{data.sourceId, data.streamLabel, data.pcmChannelId};
}

StreamSeries buildStreamSeries(const ProcessedStreamData& data, double baseAbsSeconds)
{
    StreamSeries built;

    // Elapsed seconds, shared by every series of this stream.
    built.elapsed.reserve(data.timesSec.size());
    for (const double t : data.timesSec)
    {
        built.elapsed.push_back(t - baseAbsSeconds);
    }

    auto common = [&](SeriesSpec& spec, MetricType type, std::span<const double> y) {
        spec.metricType     = type;
        spec.streamLabel    = data.streamLabel;
        spec.streamOrder    = data.pcmChannelId;
        spec.streamSequence = data.jobIndex;
        spec.sourceId       = data.sourceId;
        spec.yValues        = y;
        fillRange(spec);
    };

    // Frame-sync lock series - present for both lock-only and receiver modes.
    if (!data.lockPercent.empty())
    {
        SeriesSpec lock;
        lock.name = data.streamLabel;
        common(lock, MetricType::FrameSyncLock, data.lockPercent);
        built.series.push_back(std::move(lock));
    }

    // Missed frames accumulation series - the left-axis sibling of the lock series.
    if (!data.accumulatedMissedFrames.empty())
    {
        SeriesSpec errors;
        errors.name = data.streamLabel;
        common(errors, MetricType::AccumulatedMissedFrames, data.accumulatedMissedFrames);
        built.series.push_back(std::move(errors));
    }

    // Receiver-channel (SNR) series - one per processed parameter word.
    std::map<int, int> receiver_channel_count;
    for (const ProcessedChannelSeries& ch : data.channels)
    {
        SeriesSpec s;
        // Receiver SNR series keep the TMATS channel number as a prefix so multiple receiver SNR streams
        // (whose per-receiver channel names like L_RCVR1 would otherwise collide in the legend) stay
        // distinct; the Configure Streams dialog and Frame Sync Lock series intentionally drop it. The exact
        // name format - and its inverse for CSV import - is owned by SeriesColumnSchema.
        s.name = SeriesColumnSchema::snrSeriesName(data.pcmChannelId, data.streamLabel, ch.name);
        const std::size_t rcvr_pos = ch.name.rfind("_RCVR");
        if (rcvr_pos != std::string::npos)
        {
            int rcvr_num = 0;
            s.receiverIndex = textparse::parseInteger(ch.name.substr(rcvr_pos + 5), rcvr_num) ? rcvr_num : 0;
        }
        int& count = receiver_channel_count[s.receiverIndex];
        s.channelIndex = count;
        ++count;
        common(s, MetricType::SNR, ch.values);
        built.series.push_back(std::move(s));
    }
    return built;
}

void SeriesTimeBase::reset()
{
    m_abs_seconds = 0.0;
    m_day = 0;
    m_offset = 0.0;
}

void SeriesTimeBase::setClock(int day, double offset)
{
    m_day = day;
    m_offset = offset;
}

bool SeriesTimeBase::overlaps(double newMinAbs, double newMaxAbs, double existingMaxElapsed) const
{
    const double existing_min_abs = m_abs_seconds;
    const double existing_max_abs = m_abs_seconds + existingMaxElapsed;
    return newMinAbs <= existing_max_abs && existing_min_abs <= newMaxAbs;
}

double SeriesTimeBase::adopt(double firstAbsSeconds, bool hasSeries)
{
    if (!hasSeries)
    {
        m_abs_seconds = firstAbsSeconds;
        recomputeClock();
        return 0.0;
    }
    if (firstAbsSeconds < m_abs_seconds)
    {
        // This source's earliest sample precedes everything already loaded: re-base, so elapsed stays >= 0
        // while every sample's absolute-time correlation across sources is preserved.
        const double delta = m_abs_seconds - firstAbsSeconds;
        m_abs_seconds = firstAbsSeconds;
        recomputeClock();
        return delta;
    }
    return 0.0;
}

double SeriesTimeBase::advance(std::optional<double> earliestElapsed)
{
    if (!earliestElapsed)
    {
        return 0.0;
    }
    const double new_base_abs = m_abs_seconds + *earliestElapsed;
    const double delta = new_base_abs - m_abs_seconds; // >= 0
    if (delta > 0.0)
    {
        m_abs_seconds = new_base_abs;
        recomputeClock();
        return delta;
    }
    return 0.0;
}

void SeriesTimeBase::recomputeClock()
{
    // Use a reentrant gmtime variant - plain gmtime() returns a pointer to a shared static tm and is not
    // thread-safe.
    std::time_t epoch = static_cast<std::time_t>(m_abs_seconds);
    std::tm tm_buf{};
#if defined(_WIN32)
    const bool ok = (gmtime_s(&tm_buf, &epoch) == 0);
#else
    const bool ok = (gmtime_r(&epoch, &tm_buf) != nullptr);
#endif
    if (ok)
    {
        m_day = tm_buf.tm_yday + 1;
        m_offset = (tm_buf.tm_hour * UIConstants::kSecondsPerHour)
                 + (tm_buf.tm_min * UIConstants::kSecondsPerMinute)
                 + tm_buf.tm_sec
                 + (m_abs_seconds - static_cast<double>(epoch));
    }
}

void assignImportedStreamOrders(std::vector<ImportedSeries>& series)
{
    // Receiver-SNR series carry the real PCM channel id (recovered from the CSV column name by the parser);
    // reuse it for a stream's lock/missed siblings so both tabs of the Customize Plot Series dialog show
    // the same "CH <id>" group. Lock-only streams have no SNR counterpart, so allocate fresh ids above any
    // SNR id to avoid collisions.
    std::map<int, std::map<std::string, int>> label_to_stream_order;
    int next_stream_order = 0;
    for (const ImportedSeries& s : series)
    {
        if (s.metricType == MetricType::SNR && !s.streamLabel.empty())
        {
            label_to_stream_order[s.sourceId][s.streamLabel] = s.streamOrder;
        }
        next_stream_order = std::max(next_stream_order, s.streamOrder + 1);
    }

    for (ImportedSeries& s : series)
    {
        if (s.metricType == MetricType::SNR)
        {
            continue;
        }
        std::map<std::string, int>& source_stream_order = label_to_stream_order[s.sourceId];
        auto it = source_stream_order.find(s.streamLabel);
        if (it == source_stream_order.end())
        {
            it = source_stream_order.emplace(s.streamLabel, next_stream_order++).first;
        }
        s.streamOrder = it->second;
    }
}
