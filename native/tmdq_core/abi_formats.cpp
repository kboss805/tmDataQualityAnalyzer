/**
 * @file abi_formats.cpp
 * @brief The file formats and small pure functions of the core, through the C ABI: the frame-sync pattern file,
 *        the receiver-parameters file, template matching, CSV import and export, and clock stamps.
 *
 * Only translation. Each of these is a Qt-free core function the Qt application also calls (the dialogs,
 * `BatchController::matchFile`, `PlotViewModel`), so a host and the application cannot disagree about a format.
 */

#include "tmdq_abi.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "abi_internal.h"
#include "clocktime.h"
#include "csvseriesparser.h"
#include "csvserieswriter.h"
#include "framesetup.h"
#include "framesyncfile.h"
#include "templatematcher.h"

namespace {

constexpr std::size_t kFrameSyncInfoMinimum = sizeof(tmdq_frame_sync_file_info);
constexpr std::size_t kMatchInfoMinimum     = sizeof(tmdq_match_info);
constexpr std::size_t kCsvInfoMinimum       = sizeof(tmdq_csv_info);
constexpr std::size_t kCsvSeriesMinimum     = sizeof(tmdq_csv_series_info);
constexpr std::size_t kCsvColumnMinimum     = sizeof(tmdq_csv_column);
constexpr std::size_t kReceiverParamsMinimum = sizeof(tmdq_receiver_params);

static_assert(sizeof(int) == sizeof(int32_t), "the ids are handed to the host as the core holds them");

// The core's MetricType is numbered as the ABI's.
static_assert(static_cast<int>(MetricType::SNR) == TMDQ_METRIC_SNR);
static_assert(static_cast<int>(MetricType::FrameSyncLock) == TMDQ_METRIC_FRAME_SYNC_LOCK);
static_assert(static_cast<int>(MetricType::AccumulatedMissedFrames) == TMDQ_METRIC_ACCUMULATED_MISSED_FRAMES);

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

struct tmdq_frame_sync_file
{
    FrameSyncFile file;
};

struct tmdq_match
{
    TemplateMatcher::MatchResult result;
};

struct tmdq_csv
{
    CsvParseResult result;
};

// --- the frame-sync pattern file -----------------------------------------------------------------------

int32_t tmdq_frame_sync_file_read(const char* path_utf8, tmdq_frame_sync_file** out)
{
    tmdq::clearError();
    if (!out)
    {
        tmdq::setError("null output");
        return TMDQ_ERR_BADARG;
    }
    *out = nullptr;
    if (!path_utf8)
    {
        tmdq::setError("null path");
        return TMDQ_ERR_BADARG;
    }
    return tmdq::guarded("tmdq_frame_sync_file_read", [&]() -> int32_t {
        auto file = std::make_unique<tmdq_frame_sync_file>();
        file->file = readFrameSyncFile(tmdq::pathFromUtf8(path_utf8));
        *out = file.release();
        return TMDQ_OK;
    });
}

int32_t tmdq_frame_sync_file_info_get(const tmdq_frame_sync_file* file, tmdq_frame_sync_file_info* out)
{
    tmdq::clearError();
    if (!file)
    {
        tmdq::setError("null file");
        return TMDQ_ERR_BADARG;
    }
    const FrameSyncFile& f = file->file;
    tmdq_frame_sync_file_info info{};
    info.has_bits_per_frame   = f.bitsPerFrame.has_value() ? 1 : 0;
    info.bits_per_frame       = f.bitsPerFrame.value_or(0);
    info.bits_from_legacy_key = f.bitsFromLegacyKey ? 1 : 0;
    info.inverted             = f.inverted ? 1 : 0;
    info.has_randomized       = f.randomized.has_value() ? 1 : 0;
    info.randomized           = f.randomized.value_or(false) ? 1 : 0;
    info.pattern_utf8         = f.pattern.c_str();
    info.mask_utf8            = f.mask.c_str();
    if (!tmdq::writeStruct(info, kFrameSyncInfoMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

void tmdq_frame_sync_file_free(tmdq_frame_sync_file* file) { delete file; }

int32_t tmdq_frame_sync_file_write(const char* path_utf8, const char* pattern_utf8, const char* mask_utf8,
                                   int32_t bits_per_frame, int32_t inverted, int32_t randomized)
{
    tmdq::clearError();
    if (!path_utf8)
    {
        tmdq::setError("null path");
        return TMDQ_ERR_BADARG;
    }
    return tmdq::guarded("tmdq_frame_sync_file_write", [&]() -> int32_t {
        if (!writeFrameSyncFile(tmdq::pathFromUtf8(path_utf8), tmdq::textOf(pattern_utf8), tmdq::textOf(mask_utf8),
                                bits_per_frame, inverted != 0, randomized != 0))
        {
            tmdq::setError("the pattern file could not be written");
            return TMDQ_ERR_IO;
        }
        return TMDQ_OK;
    });
}

// --- the receiver-parameters file (revision 1) ---------------------------------------------------------

namespace {

ReceiverParams toCore(const tmdq_receiver_params& p)
{
    ReceiverParams r;
    r.polarityIndex    = p.polarity_index;
    r.slopeIndex       = p.slope_index;
    r.scaleDdBPerV     = p.scale_db_per_v;
    r.numReceivers     = p.num_receivers;
    r.receiverChannels = p.receiver_channels;
    return r;
}

tmdq_receiver_params toAbi(const ReceiverParams& r)
{
    tmdq_receiver_params p{};
    p.polarity_index    = r.polarityIndex;
    p.slope_index       = r.slopeIndex;
    p.scale_db_per_v    = r.scaleDdBPerV;
    p.num_receivers     = r.numReceivers;
    p.receiver_channels = r.receiverChannels;
    return p;
}

} // namespace

int32_t tmdq_receiver_params_read(const char* path_utf8, const tmdq_receiver_params* defaults,
                                  tmdq_receiver_params* out)
{
    tmdq::clearError();
    if (!path_utf8)
    {
        tmdq::setError("null path");
        return TMDQ_ERR_BADARG;
    }
    if (!out || out->struct_size < kReceiverParamsMinimum)
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    // NULL defaults are the application's; given ones are read no further than the host's struct.
    ReceiverParams start;
    if (defaults)
    {
        tmdq_receiver_params given{};
        if (!tmdq::readStruct(defaults, kReceiverParamsMinimum, given))
        {
            tmdq::setError("defaults with a struct_size that is too small");
            return TMDQ_ERR_BADARG;
        }
        start = toCore(given);
    }
    return tmdq::guarded("tmdq_receiver_params_read", [&]() -> int32_t {
        const tmdq_receiver_params result = toAbi(FrameSetup::readReceiverParams(tmdq::pathFromUtf8(path_utf8), start));
        tmdq::writeStruct(result, kReceiverParamsMinimum, out);
        return TMDQ_OK;
    });
}

int32_t tmdq_receiver_params_save(const char* path_utf8, const tmdq_receiver_params* params,
                                  const char* word_map_source_utf8, int32_t num_words_in_minor_frame)
{
    tmdq::clearError();
    if (!path_utf8)
    {
        tmdq::setError("null path");
        return TMDQ_ERR_BADARG;
    }
    tmdq_receiver_params given{};
    if (!tmdq::readStruct(params, kReceiverParamsMinimum, given))
    {
        tmdq::setError("null params or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return tmdq::guarded("tmdq_receiver_params_save", [&]() -> int32_t {
        std::string error;
        if (!FrameSetup::saveReceiverParamsFile(tmdq::pathFromUtf8(path_utf8), toCore(given),
                                                word_map_source_utf8 ? tmdq::pathFromUtf8(word_map_source_utf8)
                                                                     : std::filesystem::path(),
                                                num_words_in_minor_frame, error))
        {
            tmdq::setError(error);
            return TMDQ_ERR_IO;
        }
        return TMDQ_OK;
    });
}

int32_t tmdq_words_in_minor_frame(int32_t bits_in_minor_frame)
{
    return FrameSetup::wordsInMinorFrame(bits_in_minor_frame);
}

// --- template matching ---------------------------------------------------------------------------------

int32_t tmdq_template_match(const int32_t* template_ids, int32_t template_count, const int32_t* file_ids,
                            int32_t file_count, tmdq_match** out)
{
    tmdq::clearError();
    if (!out)
    {
        tmdq::setError("null output");
        return TMDQ_ERR_BADARG;
    }
    *out = nullptr;
    if (template_count < 0 || file_count < 0 || (template_count > 0 && !template_ids) ||
        (file_count > 0 && !file_ids))
    {
        tmdq::setError("a channel count does not match its array");
        return TMDQ_ERR_BADARG;
    }
    return tmdq::guarded("tmdq_template_match", [&]() -> int32_t {
        const std::vector<int> templateIds(template_ids, template_ids + template_count);
        const std::vector<int> fileIds(file_ids, file_ids + file_count);
        auto match = std::make_unique<tmdq_match>();
        match->result = TemplateMatcher::match(templateIds, fileIds);
        *out = match.release();
        return TMDQ_OK;
    });
}

int32_t tmdq_match_info_get(const tmdq_match* match, tmdq_match_info* out)
{
    tmdq::clearError();
    if (!match)
    {
        tmdq::setError("null match");
        return TMDQ_ERR_BADARG;
    }
    const TemplateMatcher::MatchResult& r = match->result;
    tmdq_match_info info{};
    info.ok            = r.ok ? 1 : 0;
    info.missing_count = static_cast<int32_t>(r.missing.size());
    info.extra_count   = static_cast<int32_t>(r.extra.size());
    info.missing       = r.missing.data();
    info.extra         = r.extra.data();
    if (!tmdq::writeStruct(info, kMatchInfoMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

void tmdq_match_free(tmdq_match* match) { delete match; }

// --- CSV import and export -----------------------------------------------------------------------------

int32_t tmdq_csv_parse(const char* path_utf8, tmdq_csv** out)
{
    tmdq::clearError();
    if (!out)
    {
        tmdq::setError("null output");
        return TMDQ_ERR_BADARG;
    }
    *out = nullptr;
    if (!path_utf8)
    {
        tmdq::setError("null path");
        return TMDQ_ERR_BADARG;
    }
    return tmdq::guarded("tmdq_csv_parse", [&]() -> int32_t {
        auto csv = std::make_unique<tmdq_csv>();
        csv->result = CsvSeriesParser::parse(tmdq::pathFromUtf8(path_utf8));
        if (!csv->result.success)
        {
            tmdq::setError("the file is not a readable series CSV (unreadable, wrong header, or no parseable sample)");
            return TMDQ_ERR_PREPARE;
        }
        *out = csv.release();
        return TMDQ_OK;
    });
}

int32_t tmdq_csv_info_get(const tmdq_csv* csv, tmdq_csv_info* out)
{
    tmdq::clearError();
    if (!csv)
    {
        tmdq::setError("null csv");
        return TMDQ_ERR_BADARG;
    }
    const CsvParseResult& r = csv->result;
    tmdq_csv_info info{};
    info.series_count      = static_cast<int32_t>(r.series.size());
    info.base_day          = r.baseDay;
    info.skipped_rows      = r.skippedRows;
    info.base_time_offset  = r.baseTimeOffset;
    info.x_max             = r.xMax;
    if (!tmdq::writeStruct(info, kCsvInfoMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

int32_t tmdq_csv_series_get(const tmdq_csv* csv, int32_t index, tmdq_csv_series_info* out)
{
    tmdq::clearError();
    if (!csv)
    {
        tmdq::setError("null csv");
        return TMDQ_ERR_BADARG;
    }
    // A negative index wraps to a huge size_t, so the one comparison refuses both ends.
    if (static_cast<std::size_t>(index) >= csv->result.series.size())
    {
        tmdq::setError("series index out of range");
        return TMDQ_ERR_BADARG;
    }
    const CsvSeries& s = csv->result.series[static_cast<std::size_t>(index)];
    tmdq_csv_series_info info{};
    info.metric_type       = static_cast<int32_t>(s.metricType);
    info.name_utf8         = s.name.c_str();
    info.stream_label_utf8 = s.streamLabel.c_str();
    info.x_values          = s.xValues.data();
    info.y_values          = s.yValues.data();
    info.y_min             = s.yMin;
    info.y_max             = s.yMax;
    info.receiver_index    = s.receiverIndex;
    info.channel_index     = s.channelIndex;
    info.stream_order      = s.streamOrder;
    info.source_id         = s.sourceId;
    info.point_count       = static_cast<int32_t>(s.xValues.size());
    if (!tmdq::writeStruct(info, kCsvSeriesMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

void tmdq_csv_free(tmdq_csv* csv) { delete csv; }

int32_t tmdq_csv_write(const char* path_utf8, const tmdq_csv_column* const* columns, int32_t count, int32_t base_day,
                       double base_time_offset)
{
    tmdq::clearError();
    if (!path_utf8)
    {
        tmdq::setError("null path");
        return TMDQ_ERR_BADARG;
    }
    if (count <= 0 || !columns)
    {
        tmdq::setError("no columns to write");
        return TMDQ_ERR_BADARG;
    }
    return tmdq::guarded("tmdq_csv_write", [&]() -> int32_t {
        std::vector<CsvColumn> list;
        list.reserve(static_cast<std::size_t>(count));
        for (int32_t i = 0; i < count; ++i)
        {
            tmdq_csv_column wire{};
            if (!tmdq::readStruct(columns[i], kCsvColumnMinimum, wire))
            {
                tmdq::setError("column " + std::to_string(i) + " is null or its struct_size is too small");
                return TMDQ_ERR_BADARG;
            }
            CsvColumn column;
            if (!toMetricType(wire.metric_type, column.metricType))
            {
                tmdq::setError("column " + std::to_string(i) + " has an unknown metric type");
                return TMDQ_ERR_BADARG;
            }
            if (wire.x_count < 0 || wire.y_count < 0 || (wire.x_count > 0 && !wire.x_values) ||
                (wire.y_count > 0 && !wire.y_values))
            {
                tmdq::setError("column " + std::to_string(i) + " has a sample count that does not match its array");
                return TMDQ_ERR_BADARG;
            }
            column.name      = tmdq::textOf(wire.name_utf8);
            column.sourceId  = wire.source_id;
            column.xValues   = std::span<const double>(wire.x_values, static_cast<std::size_t>(wire.x_count));
            column.yValues   = std::span<const double>(wire.y_values, static_cast<std::size_t>(wire.y_count));
            list.push_back(std::move(column));
        }
        const CsvTimeBase base{base_day, base_time_offset};
        if (!CsvSeriesWriter::write(tmdq::pathFromUtf8(path_utf8), list, base))
        {
            tmdq::setError("the CSV file could not be created or fully written");
            return TMDQ_ERR_IO;
        }
        return TMDQ_OK;
    });
}

// --- clock stamps --------------------------------------------------------------------------------------

int32_t tmdq_clock_time_format(int32_t base_day, double base_time_offset, double elapsed, int32_t include_milliseconds,
                               char* buffer, int32_t capacity)
{
    tmdq::clearError();
    int32_t length = -1;
    const int32_t status = tmdq::guarded("tmdq_clock_time_format", [&]() -> int32_t {
        length = tmdq::copyText(clocktime::format(base_day, base_time_offset, elapsed, include_milliseconds != 0), buffer,
                                capacity);
        if (length < 0)
        {
            tmdq::setError("a buffer and capacity that do not agree");
        }
        return TMDQ_OK;
    });
    return status == TMDQ_OK ? length : -1;
}
