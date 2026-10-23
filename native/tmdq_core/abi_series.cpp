/**
 * @file abi_series.cpp
 * @brief Series construction, the shared time base, stream identity and imported stream orders, through the C ABI.
 *
 * Only translation: `buildStreamSeries`, `SeriesTimeBase`, `streamKeyOf` and `assignImportedStreamOrders` are the
 * Qt-free core functions `PlotViewModel` calls, so a host and the application cannot disagree about which series
 * a stream yields or how the shared axis moves.
 */

#include "tmdq_abi.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "abi_events.h"
#include "abi_internal.h"
#include "seriesbuilder.h"

namespace {

constexpr std::size_t kSeriesSetInfoMinimum = sizeof(tmdq_series_set_info);
constexpr std::size_t kSeriesSpecMinimum    = sizeof(tmdq_series_spec);
constexpr std::size_t kStreamKeyMinimum     = sizeof(tmdq_stream_key);
constexpr std::size_t kTimeBaseInfoMinimum  = sizeof(tmdq_time_base_info);
constexpr std::size_t kImportedMinimum      = sizeof(tmdq_imported_series);

bool toMetricType(int32_t value, MetricType& out)
{
    switch (value)
    {
        case TMDQ_METRIC_SNR:                       out = MetricType::SNR; return true;
        case TMDQ_METRIC_FRAME_SYNC_LOCK:           out = MetricType::FrameSyncLock; return true;
        case TMDQ_METRIC_ACCUMULATED_MISSED_FRAMES: out = MetricType::AccumulatedMissedFrames; return true;
        default:                                    return false;
    }
}

} // namespace

/// A stream as series. It keeps the stream's data alive: the specs' value spans view it.
struct tmdq_series_set
{
    std::shared_ptr<const ProcessedStreamData> keep;
    StreamSeries                               series;
};

struct tmdq_time_base
{
    SeriesTimeBase base;
};

// --- a stream as series --------------------------------------------------------------------------------

int32_t tmdq_stream_result_series(const tmdq_stream_result* result, double base_abs_seconds, tmdq_series_set** out)
{
    tmdq::clearError();
    if (!out)
    {
        tmdq::setError("null output");
        return TMDQ_ERR_BADARG;
    }
    *out = nullptr;
    if (!result)
    {
        tmdq::setError("null result");
        return TMDQ_ERR_BADARG;
    }
    return tmdq::guarded("tmdq_stream_result_series", [&]() -> int32_t {
        auto set = std::make_unique<tmdq_series_set>();
        set->keep   = result->data;
        set->series = buildStreamSeries(*set->keep, base_abs_seconds);
        *out = set.release();
        return TMDQ_OK;
    });
}

int32_t tmdq_series_set_info_get(const tmdq_series_set* set, tmdq_series_set_info* out)
{
    tmdq::clearError();
    if (!set)
    {
        tmdq::setError("null series set");
        return TMDQ_ERR_BADARG;
    }
    tmdq_series_set_info info{};
    info.series_count  = static_cast<int32_t>(set->series.series.size());
    info.elapsed_count = static_cast<int32_t>(set->series.elapsed.size());
    info.elapsed       = set->series.elapsed.data();
    if (!tmdq::writeStruct(info, kSeriesSetInfoMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

int32_t tmdq_series_set_spec(const tmdq_series_set* set, int32_t index, tmdq_series_spec* out)
{
    tmdq::clearError();
    if (!set)
    {
        tmdq::setError("null series set");
        return TMDQ_ERR_BADARG;
    }
    // A negative index wraps to a huge size_t, so the one comparison refuses both ends.
    if (static_cast<std::size_t>(index) >= set->series.series.size())
    {
        tmdq::setError("series index out of range");
        return TMDQ_ERR_BADARG;
    }
    const SeriesSpec& s = set->series.series[static_cast<std::size_t>(index)];
    tmdq_series_spec spec{};
    spec.metric_type       = static_cast<int32_t>(s.metricType);
    spec.name_utf8         = s.name.c_str();
    spec.stream_label_utf8 = s.streamLabel.c_str();
    spec.y_values          = s.yValues.data();
    spec.y_min             = s.yMin;
    spec.y_max             = s.yMax;
    spec.receiver_index    = s.receiverIndex;
    spec.channel_index     = s.channelIndex;
    spec.stream_order      = s.streamOrder;
    spec.stream_sequence   = s.streamSequence;
    spec.source_id         = s.sourceId;
    spec.y_count           = static_cast<int32_t>(s.yValues.size());
    if (!tmdq::writeStruct(spec, kSeriesSpecMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

void tmdq_series_set_free(tmdq_series_set* set) { delete set; }

int32_t tmdq_stream_result_key(const tmdq_stream_result* result, tmdq_stream_key* out)
{
    tmdq::clearError();
    if (!result)
    {
        tmdq::setError("null result");
        return TMDQ_ERR_BADARG;
    }
    const ProcessedStreamData& d = *result->data;
    tmdq_stream_key key{};
    key.source_id  = d.sourceId;
    key.label_utf8 = d.streamLabel.c_str();
    key.channel    = d.pcmChannelId;
    if (!tmdq::writeStruct(key, kStreamKeyMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

int32_t tmdq_stream_key_equal(const tmdq_stream_key* a, const tmdq_stream_key* b)
{
    tmdq::clearError();
    tmdq_stream_key ka{}, kb{};
    if (!tmdq::readStruct(a, kStreamKeyMinimum, ka) || !tmdq::readStruct(b, kStreamKeyMinimum, kb))
    {
        tmdq::setError("a null key or a struct_size that is too small");
        return -1;
    }
    const StreamKey x{ka.source_id, tmdq::textOf(ka.label_utf8), ka.channel};
    const StreamKey y{kb.source_id, tmdq::textOf(kb.label_utf8), kb.channel};
    return x == y ? 1 : 0;
}

// --- the shared time base ------------------------------------------------------------------------------

int32_t tmdq_time_base_create(tmdq_time_base** out)
{
    tmdq::clearError();
    if (!out)
    {
        tmdq::setError("null output");
        return TMDQ_ERR_BADARG;
    }
    *out = nullptr;
    return tmdq::guarded("tmdq_time_base_create", [&]() -> int32_t {
        *out = new tmdq_time_base();
        return TMDQ_OK;
    });
}

void tmdq_time_base_free(tmdq_time_base* base) { delete base; }

int32_t tmdq_time_base_info_get(const tmdq_time_base* base, tmdq_time_base_info* out)
{
    tmdq::clearError();
    if (!base)
    {
        tmdq::setError("null time base");
        return TMDQ_ERR_BADARG;
    }
    tmdq_time_base_info info{};
    info.base_day         = base->base.baseDay();
    info.base_abs_seconds = base->base.baseAbsSeconds();
    info.base_time_offset = base->base.baseTimeOffset();
    if (!tmdq::writeStruct(info, kTimeBaseInfoMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

int32_t tmdq_time_base_reset(tmdq_time_base* base)
{
    tmdq::clearError();
    if (!base)
    {
        tmdq::setError("null time base");
        return TMDQ_ERR_BADARG;
    }
    base->base.reset();
    return TMDQ_OK;
}

int32_t tmdq_time_base_set_clock(tmdq_time_base* base, int32_t day, double offset)
{
    tmdq::clearError();
    if (!base)
    {
        tmdq::setError("null time base");
        return TMDQ_ERR_BADARG;
    }
    base->base.setClock(day, offset);
    return TMDQ_OK;
}

int32_t tmdq_time_base_overlaps(const tmdq_time_base* base, double new_min_abs, double new_max_abs,
                                double existing_max_elapsed, int32_t* out)
{
    tmdq::clearError();
    if (!base || !out)
    {
        tmdq::setError("null time base or output");
        return TMDQ_ERR_BADARG;
    }
    *out = base->base.overlaps(new_min_abs, new_max_abs, existing_max_elapsed) ? 1 : 0;
    return TMDQ_OK;
}

int32_t tmdq_time_base_adopt(tmdq_time_base* base, double first_abs_seconds, int32_t has_series, double* shift)
{
    tmdq::clearError();
    if (!base || !shift)
    {
        tmdq::setError("null time base or output");
        return TMDQ_ERR_BADARG;
    }
    *shift = base->base.adopt(first_abs_seconds, has_series != 0);
    return TMDQ_OK;
}

int32_t tmdq_time_base_advance(tmdq_time_base* base, int32_t has_earliest, double earliest_elapsed, double* shift)
{
    tmdq::clearError();
    if (!base || !shift)
    {
        tmdq::setError("null time base or output");
        return TMDQ_ERR_BADARG;
    }
    *shift = base->base.advance(has_earliest != 0 ? std::optional<double>(earliest_elapsed) : std::nullopt);
    return TMDQ_OK;
}

// --- imported CSV streams ------------------------------------------------------------------------------

int32_t tmdq_assign_imported_stream_orders(tmdq_imported_series* const* series, int32_t count)
{
    tmdq::clearError();
    if (count < 0 || (count > 0 && !series))
    {
        tmdq::setError("a series count that does not match its array");
        return TMDQ_ERR_BADARG;
    }
    return tmdq::guarded("tmdq_assign_imported_stream_orders", [&]() -> int32_t {
        std::vector<ImportedSeries> list;
        list.reserve(static_cast<std::size_t>(count));
        for (int32_t i = 0; i < count; ++i)
        {
            tmdq_imported_series wire{};
            if (!tmdq::readStruct(series[i], kImportedMinimum, wire))
            {
                tmdq::setError("series " + std::to_string(i) + " is null or its struct_size is too small");
                return TMDQ_ERR_BADARG;
            }
            ImportedSeries one;
            if (!toMetricType(wire.metric_type, one.metricType))
            {
                tmdq::setError("series " + std::to_string(i) + " has an unknown metric type");
                return TMDQ_ERR_BADARG;
            }
            one.sourceId    = wire.source_id;
            one.streamLabel = tmdq::textOf(wire.stream_label_utf8);
            one.streamOrder = wire.stream_order;
            list.push_back(std::move(one));
        }
        assignImportedStreamOrders(list);
        for (int32_t i = 0; i < count; ++i)
        {
            series[i]->stream_order = list[static_cast<std::size_t>(i)].streamOrder;
        }
        return TMDQ_OK;
    });
}
