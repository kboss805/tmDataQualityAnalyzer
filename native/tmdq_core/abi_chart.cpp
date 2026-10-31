/**
 * @file abi_chart.cpp
 * @brief The chart's geometry through the C ABI (revision 2): the transforms, a series' line and band, and the ticks.
 *
 * Only translation of chartgeometry.h, which the Qt chart also calls, so a host and the Qt application draw the same
 * pixels. A series' segments and bands are handed out as views into the geometry, without a copy.
 */

#include "tmdq_abi.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

#include "abi_internal.h"
#include "chartgeometry.h"

namespace {

constexpr std::size_t kChartViewMinimum    = sizeof(tmdq_chart_view);
constexpr std::size_t kGeometryInfoMinimum = sizeof(tmdq_geometry_info);

// The segments and band points are handed out as the doubles they are made of.
static_assert(std::is_standard_layout_v<chart::Segment> && sizeof(chart::Segment) == 4 * sizeof(double));
static_assert(std::is_standard_layout_v<chart::Point> && sizeof(chart::Point) == 2 * sizeof(double));

bool readView(const tmdq_chart_view* in, chart::View& out)
{
    tmdq_chart_view view{};
    if (!tmdq::readStruct(in, kChartViewMinimum, view))
    {
        return false;
    }
    out.xLower = view.x_lower;
    out.xUpper = view.x_upper;
    out.yLower = view.y_lower;
    out.yUpper = view.y_upper;
    out.left   = view.left;
    out.top    = view.top;
    out.width  = view.width;
    out.height = view.height;
    return true;
}

bool badArray(const void* data, int32_t count) { return count < 0 || (count > 0 && !data); }

int32_t writeTicks(const std::vector<double>& values, double* ticks, int32_t capacity)
{
    if (capacity < 0 || (capacity > 0 && !ticks))
    {
        tmdq::setError("a tick buffer does not match its capacity");
        return -1;
    }
    const std::size_t written = std::min(values.size(), static_cast<std::size_t>(capacity));
    std::copy_n(values.begin(), written, ticks);
    return static_cast<int32_t>(values.size());
}

} // namespace

struct tmdq_geometry
{
    chart::SeriesGeometry geometry;
};

int32_t tmdq_chart_geometry(const double* xs, const double* ys, int32_t count, int32_t sorted_x,
                            const tmdq_chart_view* view, tmdq_geometry** out)
{
    tmdq::clearError();
    if (!out)
    {
        tmdq::setError("null output");
        return TMDQ_ERR_BADARG;
    }
    *out = nullptr;
    chart::View v;
    if (!readView(view, v))
    {
        tmdq::setError("null or short chart view");
        return TMDQ_ERR_BADARG;
    }
    if (badArray(xs, count) || badArray(ys, count))
    {
        tmdq::setError("a sample count does not match its arrays");
        return TMDQ_ERR_BADARG;
    }
    return tmdq::guarded("tmdq_chart_geometry", [&]() -> int32_t {
        const std::size_t n = static_cast<std::size_t>(count);
        auto result = std::make_unique<tmdq_geometry>();
        result->geometry = chart::buildSeriesGeometry(std::span<const double>(xs, n), std::span<const double>(ys, n),
                                                      sorted_x != 0, v);
        *out = result.release();
        return TMDQ_OK;
    });
}

int32_t tmdq_geometry_info_get(const tmdq_geometry* geometry, tmdq_geometry_info* out)
{
    tmdq::clearError();
    if (!geometry)
    {
        tmdq::setError("null geometry");
        return TMDQ_ERR_BADARG;
    }
    const chart::SeriesGeometry& g = geometry->geometry;
    tmdq_geometry_info info{};
    info.segment_count = static_cast<int32_t>(g.segments.size());
    info.segments      = g.segments.empty() ? nullptr : reinterpret_cast<const double*>(g.segments.data());
    info.band_count    = static_cast<int32_t>(g.bands.size());
    info.decimated     = g.decimated ? 1 : 0;
    if (!tmdq::writeStruct(info, kGeometryInfoMinimum, out))
    {
        tmdq::setError("null or short geometry info");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

int32_t tmdq_geometry_band(const tmdq_geometry* geometry, int32_t index, const double** points, int32_t* point_count)
{
    tmdq::clearError();
    if (!geometry || !points || !point_count)
    {
        tmdq::setError("null argument");
        return TMDQ_ERR_BADARG;
    }
    const auto& bands = geometry->geometry.bands;
    if (index < 0 || static_cast<std::size_t>(index) >= bands.size())
    {
        tmdq::setError("band index out of range");
        return TMDQ_ERR_BADARG;
    }
    const std::vector<chart::Point>& band = bands[static_cast<std::size_t>(index)];
    *points      = reinterpret_cast<const double*>(band.data());
    *point_count = static_cast<int32_t>(band.size());
    return TMDQ_OK;
}

void tmdq_geometry_free(tmdq_geometry* geometry) { delete geometry; }

int32_t tmdq_chart_sorted_x(const double* xs, int32_t count)
{
    tmdq::clearError();
    if (badArray(xs, count))
    {
        tmdq::setError("a sample count does not match its array");
        return -1;
    }
    return chart::isSortedX(std::span<const double>(xs, static_cast<std::size_t>(count))) ? 1 : 0;
}

int32_t tmdq_chart_usable_range(double lower, double upper) { return chart::usableRange(lower, upper) ? 1 : 0; }

int32_t tmdq_chart_transform(const tmdq_chart_view* view, int32_t kind, double value, double* out)
{
    tmdq::clearError();
    chart::View v;
    if (!out || !readView(view, v))
    {
        tmdq::setError("null output, or a null or short chart view");
        return TMDQ_ERR_BADARG;
    }
    switch (kind)
    {
        case TMDQ_X_TO_PIXEL: *out = chart::xToPixel(v, value); return TMDQ_OK;
        case TMDQ_PIXEL_TO_X: *out = chart::pixelToX(v, value); return TMDQ_OK;
        case TMDQ_Y_TO_PIXEL: *out = chart::yToPixel(v, value); return TMDQ_OK;
        case TMDQ_PIXEL_TO_Y: *out = chart::pixelToY(v, value); return TMDQ_OK;
        default:
            tmdq::setError("unknown transform");
            return TMDQ_ERR_BADARG;
    }
}

int32_t tmdq_chart_ticks(double lower, double upper, int32_t count, double* ticks, int32_t capacity)
{
    tmdq::clearError();
    return writeTicks(chart::tickValues(lower, upper, count), ticks, capacity);
}

int32_t tmdq_chart_time_ticks(double lower, double upper, int32_t count, double origin, double* ticks,
                              int32_t capacity)
{
    tmdq::clearError();
    return writeTicks(chart::timeTickValues(lower, upper, count, origin), ticks, capacity);
}
