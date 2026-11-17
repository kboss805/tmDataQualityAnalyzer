// The chart's geometry and a series' index through the C ABI (src/core/abi/abi_chart.cpp, revisions 2 and 3).

#include "test_abi_support.h"

// ---------------------------------------------------------------------------
// The chart's geometry (revision 2)
// ---------------------------------------------------------------------------

namespace {

tmdq_chart_view abiView(const chart::View& v)
{
    tmdq_chart_view out{};
    out.struct_size = sizeof(out);
    out.x_lower = v.xLower;
    out.x_upper = v.xUpper;
    out.y_lower = v.yLower;
    out.y_upper = v.yUpper;
    out.left = v.left;
    out.top = v.top;
    out.width = v.width;
    out.height = v.height;
    return out;
}

bool bitsEqual(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

} // namespace

TEST_CASE("Abi: a series geometry is the core's")
{
    // Generated series and views, through the ABI and straight through the core: the segments, every band's points
    // and the decimation flag, bit for bit, and each transform.
    std::mt19937 rng(426);
    auto uniform = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    int decimated = 0, banded = 0, sparse = 0;
    for (int trial = 0; trial < 400; ++trial)
    {
        const int n = trial % 5 == 0 ? trial % 3 : 3 + static_cast<int>(uniform(0.0, 4000.0));
        std::vector<double> xs(static_cast<std::size_t>(n)), ys(static_cast<std::size_t>(n));
        double x = 0.0;
        for (int i = 0; i < n; ++i)
        {
            x += uniform(0.01, 1.0);
            xs[i] = x;
            ys[i] = uniform(-5.0, 105.0);
        }
        if (n > 10 && trial % 4 == 0) ys[static_cast<std::size_t>(n / 2)] = std::numeric_limits<double>::quiet_NaN();
        if (n > 10 && trial % 7 == 0) std::swap(xs[1], xs[static_cast<std::size_t>(n - 2)]);
        chart::View v;
        v.xLower = uniform(-10.0, x * 0.5);
        v.xUpper = v.xLower + uniform(0.5, x + 10.0);
        v.yLower = uniform(-10.0, 10.0);
        v.yUpper = v.yLower + uniform(1.0, 120.0);
        v.left = uniform(0.0, 80.0);
        v.top = uniform(0.0, 40.0);
        v.width = uniform(10.0, 1400.0);
        v.height = uniform(10.0, 800.0);
        const bool sorted = chart::isSortedX(xs);
        CHECK(tmdq_chart_sorted_x(xs.data(), n) == (sorted ? 1 : 0));

        const chart::SeriesGeometry expected = chart::buildSeriesGeometry(xs, ys, sorted, v);
        const tmdq_chart_view view = abiView(v);
        tmdq_geometry* g = nullptr;
        REQUIRE(tmdq_chart_geometry(xs.data(), ys.data(), n, sorted ? 1 : 0, &view, &g) == int(TMDQ_OK));
        tmdq_geometry_info info{};
        info.struct_size = sizeof(info);
        REQUIRE(tmdq_geometry_info_get(g, &info) == int(TMDQ_OK));
        CHECK(info.struct_size == sizeof(info));
        CHECK(info.decimated == (expected.decimated ? 1 : 0));
        REQUIRE(info.segment_count == int(expected.segments.size()));
        bool same = true;
        for (std::size_t i = 0; i < expected.segments.size(); ++i)
        {
            const chart::Segment& e = expected.segments[i];
            const double* a = info.segments + 4 * i;
            same = same && bitsEqual(a[0], e.from.x) && bitsEqual(a[1], e.from.y) && bitsEqual(a[2], e.to.x) &&
                   bitsEqual(a[3], e.to.y);
        }
        CHECK(same);
        REQUIRE(info.band_count == int(expected.bands.size()));
        for (int b = 0; b < info.band_count; ++b)
        {
            const double* points = nullptr;
            int32_t count = -1;
            REQUIRE(tmdq_geometry_band(g, b, &points, &count) == int(TMDQ_OK));
            const std::vector<chart::Point>& e = expected.bands[static_cast<std::size_t>(b)];
            REQUIRE(count == int(e.size()));
            for (std::size_t i = 0; i < e.size(); ++i)
                CHECK((bitsEqual(points[2 * i], e[i].x) && bitsEqual(points[2 * i + 1], e[i].y)));
        }
        tmdq_geometry_free(g);

        const double value = uniform(-50.0, 1500.0);
        double out = 0.0;
        REQUIRE(tmdq_chart_transform(&view, TMDQ_X_TO_PIXEL, value, &out) == int(TMDQ_OK));
        CHECK(bitsEqual(out, chart::xToPixel(v, value)));
        REQUIRE(tmdq_chart_transform(&view, TMDQ_PIXEL_TO_X, value, &out) == int(TMDQ_OK));
        CHECK(bitsEqual(out, chart::pixelToX(v, value)));
        REQUIRE(tmdq_chart_transform(&view, TMDQ_Y_TO_PIXEL, value, &out) == int(TMDQ_OK));
        CHECK(bitsEqual(out, chart::yToPixel(v, value)));
        REQUIRE(tmdq_chart_transform(&view, TMDQ_PIXEL_TO_Y, value, &out) == int(TMDQ_OK));
        CHECK(bitsEqual(out, chart::pixelToY(v, value)));

        (expected.decimated ? decimated : sparse)++;
        if (!expected.bands.empty()) ++banded;
    }
    CHECK(decimated > 100);
    CHECK(sparse > 100);
    CHECK(banded > 100);
}

TEST_CASE("Abi: the ticks and the range check are the core's")
{
    std::mt19937 rng(17);
    auto uniform = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    for (int trial = 0; trial < 2000; ++trial)
    {
        const double lower = uniform(-1e4, 1e5);
        const double upper = trial % 25 == 0 ? lower : lower + std::pow(10.0, uniform(-2.0, 6.5));
        const int count = static_cast<int>(uniform(0.0, 20.0));
        const double origin = uniform(0.0, 86400.0);
        CHECK(tmdq_chart_usable_range(lower, upper) == (chart::usableRange(lower, upper) ? 1 : 0));

        const std::vector<double> plain = chart::tickValues(lower, upper, count);
        const std::vector<double> timed = chart::timeTickValues(lower, upper, count, origin);
        std::vector<double> got(64, -1.0);
        REQUIRE(tmdq_chart_ticks(lower, upper, count, got.data(), int32_t(got.size())) == int(plain.size()));
        CHECK(std::equal(plain.begin(), plain.end(), got.begin()));
        CHECK(got[plain.size()] == -1.0);   // nothing past what there is
        std::fill(got.begin(), got.end(), -1.0);
        REQUIRE(tmdq_chart_time_ticks(lower, upper, count, origin, got.data(), int32_t(got.size())) == int(timed.size()));
        CHECK(std::equal(timed.begin(), timed.end(), got.begin()));
        CHECK(got[timed.size()] == -1.0);
    }
    CHECK(tmdq_chart_usable_range(0.0, std::numeric_limits<double>::infinity()) == 0);

    // A buffer too small for them: how many there are, and only as many written as fit.
    double two[3] = {-1.0, -1.0, -1.0};
    CHECK(tmdq_chart_ticks(0.0, 100.0, 5, two, 2) == 5);
    CHECK(two[0] == 0.0);
    CHECK(two[1] == 25.0);
    CHECK(two[2] == -1.0);
    CHECK(tmdq_chart_time_ticks(0.0, 116.0, 5, 0.0, nullptr, 0) == 4);   // measuring only
}

TEST_CASE("Abi: a tick count no axis could have is refused")
{
    // The count sizes an allocation, so it is bounded before anything is built: a host's mistaken or hostile count
    // would otherwise ask for gigabytes (and an exception from that must not leave the library either).
    double ticks[4] = {-1.0, -1.0, -1.0, -1.0};
    CHECK(tmdq_chart_ticks(0.0, 100.0, 65536, nullptr, 0) == 65536);   // the most there can be, measured
    CHECK(std::string(tmdq_last_error()).empty());

    CHECK(tmdq_chart_ticks(0.0, 100.0, 65537, ticks, 4) == -1);
    CHECK(std::string(tmdq_last_error()).find("tick count") != std::string::npos);
    CHECK(ticks[0] == -1.0);   // nothing written
    CHECK(tmdq_chart_ticks(0.0, 100.0, std::numeric_limits<int32_t>::max(), nullptr, 0) == -1);

    CHECK(tmdq_chart_time_ticks(0.0, 100.0, 65537, 0.0, ticks, 4) == -1);
    CHECK(std::string(tmdq_last_error()).find("tick count") != std::string::npos);
    CHECK(tmdq_chart_time_ticks(0.0, 100.0, std::numeric_limits<int32_t>::max(), 0.0, nullptr, 0) == -1);
    CHECK(ticks[0] == -1.0);

    // An ordinary call after a refusal leaves no stale message.
    CHECK(tmdq_chart_ticks(0.0, 100.0, 5, ticks, 4) == 5);
    CHECK(std::string(tmdq_last_error()).empty());
}

TEST_CASE("Abi: bad arguments to the chart functions are refused")
{
    const double xs[] = {0.0, 1.0, 2.0};
    const double ys[] = {1.0, 2.0, 3.0};
    tmdq_chart_view view = abiView(chart::View{0.0, 2.0, 0.0, 3.0, 0.0, 0.0, 100.0, 100.0});
    tmdq_geometry* g = reinterpret_cast<tmdq_geometry*>(0x1);

    CHECK(tmdq_chart_geometry(xs, ys, 3, 1, &view, nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_chart_geometry(xs, ys, 3, 1, nullptr, &g) == int(TMDQ_ERR_BADARG));
    CHECK(g == nullptr);   // the output is cleared on a refusal
    tmdq_chart_view small = view;
    small.struct_size = sizeof(small) - 1;
    CHECK(tmdq_chart_geometry(xs, ys, 3, 1, &small, &g) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_chart_geometry(xs, ys, -1, 1, &view, &g) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_chart_geometry(nullptr, ys, 3, 1, &view, &g) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_chart_geometry(xs, nullptr, 3, 1, &view, &g) == int(TMDQ_ERR_BADARG));
    REQUIRE(tmdq_chart_geometry(nullptr, nullptr, 0, 1, &view, &g) == int(TMDQ_OK));   // no samples is no line
    tmdq_geometry_info info{};
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_geometry_info_get(g, &info) == int(TMDQ_OK));
    CHECK(info.segment_count == 0);
    CHECK(info.segments == nullptr);
    CHECK(info.band_count == 0);
    const double* points = nullptr;
    int32_t count = 0;
    CHECK(tmdq_geometry_band(g, 0, &points, &count) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_geometry_band(g, -1, &points, &count) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_geometry_band(g, 0, nullptr, &count) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_geometry_band(nullptr, 0, &points, &count) == int(TMDQ_ERR_BADARG));
    tmdq_geometry_info shortInfo{};
    shortInfo.struct_size = sizeof(shortInfo) - 1;
    CHECK(tmdq_geometry_info_get(g, &shortInfo) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_geometry_info_get(nullptr, &info) == int(TMDQ_ERR_BADARG));
    tmdq_geometry_free(g);
    tmdq_geometry_free(nullptr);

    double out = 0.0;
    CHECK(tmdq_chart_transform(&view, 4, 1.0, &out) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_chart_transform(&view, -1, 1.0, &out) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_chart_transform(nullptr, TMDQ_X_TO_PIXEL, 1.0, &out) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_chart_transform(&small, TMDQ_X_TO_PIXEL, 1.0, &out) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_chart_transform(&view, TMDQ_X_TO_PIXEL, 1.0, nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_chart_sorted_x(nullptr, 2) == -1);
    CHECK(tmdq_chart_sorted_x(xs, -1) == -1);
    CHECK(tmdq_chart_sorted_x(nullptr, 0) == 1);
    double buffer[4];
    CHECK(tmdq_chart_ticks(0.0, 1.0, 3, buffer, -1) == -1);
    CHECK(tmdq_chart_ticks(0.0, 1.0, 3, nullptr, 2) == -1);
    CHECK(tmdq_chart_time_ticks(0.0, 100.0, 3, 0.0, nullptr, 2) == -1);

    // A dense series of nothing but gaps is drawn from the envelope and has no line: no segments, and no pointer.
    const std::vector<double> gapsX(1000, std::numeric_limits<double>::quiet_NaN());
    const std::vector<double> gapsY(1000, 1.0);
    REQUIRE(tmdq_chart_geometry(gapsX.data(), gapsY.data(), 1000, 0, &view, &g) == int(TMDQ_OK));
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_geometry_info_get(g, &info) == int(TMDQ_OK));
    CHECK(info.decimated == 1);
    CHECK(info.segment_count == 0);
    CHECK(info.segments == nullptr);
    tmdq_geometry_free(g);

    // A view from a newer host (bigger) is read no further than this library knows.
    struct Bigger { tmdq_chart_view v; double extra[2]; };
    Bigger big{};
    big.v = view;
    big.v.struct_size = sizeof(Bigger);
    REQUIRE(tmdq_chart_transform(&big.v, TMDQ_X_TO_PIXEL, 1.0, &out) == int(TMDQ_OK));
    CHECK(out == 50.0);
    CHECK(std::string(tmdq_last_error()).empty());   // a successful call clears an earlier error
}

// ---------------------------------------------------------------------------
// A series' index (revision 3)
// ---------------------------------------------------------------------------

TEST_CASE("Abi: a series index gives the core's indexed geometry")
{
    // Generated ordered series, dense enough to be drawn from their envelope, through the ABI with an index and
    // straight through the core with one: bit for bit.
    std::mt19937 rng(8803);
    auto uniform = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    int decimated = 0;
    for (int trial = 0; trial < 120; ++trial)
    {
        const int n = 50 + static_cast<int>(uniform(0.0, 60000.0));
        std::vector<double> xs(static_cast<std::size_t>(n)), ys(static_cast<std::size_t>(n));
        double x = 0.0;
        for (int i = 0; i < n; ++i)
        {
            x += uniform(0.001, 0.02);
            xs[static_cast<std::size_t>(i)] = x;
            ys[static_cast<std::size_t>(i)] = uniform(-5.0, 105.0);
        }
        if (trial % 3 == 0) ys[static_cast<std::size_t>(n / 3)] = std::numeric_limits<double>::quiet_NaN();
        chart::View v;
        v.xLower = uniform(-1.0, x * 0.5);
        v.xUpper = v.xLower + uniform(0.5, x);
        v.yLower = -10.0;
        v.yUpper = 110.0;
        v.left = 60.0;
        v.top = 20.0;
        v.width = uniform(100.0, 1600.0);
        v.height = 800.0;

        tmdq_series_index* index = nullptr;
        REQUIRE(tmdq_series_index_build(ys.data(), n, &index) == int(TMDQ_OK));
        REQUIRE(index != nullptr);
        CHECK(tmdq_series_index_count(index) == n);
        const chart::SeriesIndex core(ys);
        const chart::SeriesGeometry expected = chart::buildSeriesGeometry(xs, ys, true, v, &core);

        const tmdq_chart_view view = abiView(v);
        tmdq_geometry* g = nullptr;
        REQUIRE(tmdq_chart_geometry_indexed(xs.data(), ys.data(), n, 1, index, &view, &g) == int(TMDQ_OK));
        tmdq_geometry_info info{};
        info.struct_size = sizeof(info);
        REQUIRE(tmdq_geometry_info_get(g, &info) == int(TMDQ_OK));
        CHECK(info.decimated == (expected.decimated ? 1 : 0));
        REQUIRE(info.segment_count == int(expected.segments.size()));
        bool same = true;
        for (std::size_t i = 0; i < expected.segments.size(); ++i)
        {
            const chart::Segment& e = expected.segments[i];
            const double* a = info.segments + 4 * i;
            same = same && bitsEqual(a[0], e.from.x) && bitsEqual(a[1], e.from.y) && bitsEqual(a[2], e.to.x) &&
                   bitsEqual(a[3], e.to.y);
        }
        CHECK(same);
        REQUIRE(info.band_count == int(expected.bands.size()));
        for (int b = 0; b < info.band_count; ++b)
        {
            const double* points = nullptr;
            int32_t count = -1;
            REQUIRE(tmdq_geometry_band(g, b, &points, &count) == int(TMDQ_OK));
            const std::vector<chart::Point>& e = expected.bands[static_cast<std::size_t>(b)];
            REQUIRE(count == int(e.size()));
            bool band = true;
            for (std::size_t i = 0; i < e.size(); ++i)
                band = band && bitsEqual(points[2 * i], e[i].x) && bitsEqual(points[2 * i + 1], e[i].y);
            CHECK(band);
        }
        tmdq_geometry_free(g);
        tmdq_series_index_free(index);
        if (expected.decimated) ++decimated;
    }
    CHECK(decimated > 100);
}

TEST_CASE("Abi: a series index is used only when it fits and bad arguments are refused")
{
    std::vector<double> xs(1024), ys(1024, 100.0), zeros(1024, 0.0);
    for (int i = 0; i < 1024; ++i) xs[static_cast<std::size_t>(i)] = i * 0.125;
    chart::View v;
    v.xLower = 0.0;
    v.xUpper = 128.0;
    v.yLower = 0.0;
    v.yUpper = 100.0;
    v.width = 4.0;    // four columns of 256 samples: looked up from whole blocks
    v.height = 100.0;
    const tmdq_chart_view view = abiView(v);

    // Built over other values of the same count, the index shows: its zeros are the bottom of the plot.
    tmdq_series_index* other = nullptr;
    REQUIRE(tmdq_series_index_build(zeros.data(), 1024, &other) == int(TMDQ_OK));
    tmdq_geometry* g = nullptr;
    REQUIRE(tmdq_chart_geometry_indexed(xs.data(), ys.data(), 1024, 1, other, &view, &g) == int(TMDQ_OK));
    const double* points = nullptr;
    int32_t count = 0;
    REQUIRE(tmdq_geometry_band(g, 0, &points, &count) == int(TMDQ_OK));
    REQUIRE(count == 8);
    CHECK(points[1] == 100.0);
    tmdq_geometry_free(g);

    // Not with times out of order, and not at all through tmdq_chart_geometry or with a NULL index.
    for (int pass = 0; pass < 3; ++pass)
    {
        g = nullptr;
        const int32_t status = pass == 0   ? tmdq_chart_geometry_indexed(xs.data(), ys.data(), 1024, 0, other, &view, &g)
                               : pass == 1 ? tmdq_chart_geometry(xs.data(), ys.data(), 1024, 1, &view, &g)
                                           : tmdq_chart_geometry_indexed(xs.data(), ys.data(), 1024, 1, nullptr, &view, &g);
        REQUIRE(status == int(TMDQ_OK));
        REQUIRE(tmdq_geometry_band(g, 0, &points, &count) == int(TMDQ_OK));
        CHECK(points[1] == 0.0);
        tmdq_geometry_free(g);
    }

    // An index over another count is refused, not silently ignored: the host has paired it with the wrong series.
    g = reinterpret_cast<tmdq_geometry*>(&count);
    CHECK(tmdq_chart_geometry_indexed(xs.data(), ys.data(), 1000, 1, other, &view, &g) == int(TMDQ_ERR_BADARG));
    CHECK(g == nullptr);
    CHECK(std::string(tmdq_last_error()).find("index") != std::string::npos);
    tmdq_series_index_free(other);

    // Building: a NULL output, a negative count, a NULL array with values; an empty series is an empty index.
    tmdq_series_index* index = nullptr;
    CHECK(tmdq_series_index_build(ys.data(), 4, nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_series_index_build(ys.data(), -1, &index) == int(TMDQ_ERR_BADARG));
    CHECK(index == nullptr);
    CHECK(tmdq_series_index_build(nullptr, 4, &index) == int(TMDQ_ERR_BADARG));
    REQUIRE(tmdq_series_index_build(nullptr, 0, &index) == int(TMDQ_OK));
    CHECK(tmdq_series_index_count(index) == 0);
    CHECK(std::string(tmdq_last_error()).empty());
    tmdq_series_index_free(index);
    CHECK(tmdq_series_index_count(nullptr) == -1);
    tmdq_series_index_free(nullptr);
}
