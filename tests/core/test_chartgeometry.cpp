// The plot's geometry (plan step 4.3a): the transforms between data and pixels, a series' line and its per-pixel-column
// envelope, and the X ticks. The code came out of v2.x's chart, TmChart (its Qt implementation); that code is
// restated here over std types as it was (`Old`) and compared with the core over generated series, views and ranges,
// so the geometry is held to exactly what that chart drew. The cases after the comparison work their answers out by
// hand: the behaviours the chart's own tests pinned through a rendered widget (a dense series' dropout and spread, a
// line through a view no sample is in, the time ticks), here on the geometry itself.

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include "chartgeometry.h"
#include "constants.h"
#include "doctest.h"

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// ---------------------------------------------------------------------------
// The chart's code before it called the core (TmChart, at 02c214b), over std types.
// Its points, lines and polygons become plain structs; its rectangle the four doubles of the plot area; the series'
// axis picks which Y range is used, as it did.
// ---------------------------------------------------------------------------

struct OldPoint { double x; double y; };
struct OldLine { OldPoint p1; OldPoint p2; };

struct Old
{
    double x_lower = 0.0, x_upper = 1.0, left_lower = 0.0, left_upper = 1.0, right_lower = 0.0, right_upper = 1.0;
    double area_left = 0.0, area_top = 0.0, area_width = 0.0, area_height = 0.0;

    static bool usableRange(double lower, double upper)
    {
        return std::isfinite(lower) && std::isfinite(upper) && (upper - lower) > 0.0;
    }
    double bottom() const { return area_top + area_height; }

    double pixelToX(double px) const
    {
        if (area_width <= 0) return x_lower;
        const double frac = (px - area_left) / static_cast<double>(area_width);
        return x_lower + frac * (x_upper - x_lower);
    }
    double xToPixel(double x) const
    {
        if (!usableRange(x_lower, x_upper)) return area_left;
        const double frac = (x - x_lower) / (x_upper - x_lower);
        return area_left + frac * area_width;
    }
    double pixelToLeft(double py) const
    {
        if (area_height <= 0) return left_lower;
        const double frac = (bottom() - py) / static_cast<double>(area_height);
        return left_lower + frac * (left_upper - left_lower);
    }
    double leftToPixel(double value) const
    {
        if (!usableRange(left_lower, left_upper)) return bottom();
        const double frac = (value - left_lower) / (left_upper - left_lower);
        return bottom() - frac * area_height;
    }
    double rightToPixel(double value) const
    {
        if (!usableRange(right_lower, right_upper)) return bottom();
        const double frac = (value - right_lower) / (right_upper - right_lower);
        return bottom() - frac * area_height;
    }

    static bool sorted(const std::vector<double>& xs)
    {
        bool s = true;
        for (int i = 1; i < static_cast<int>(xs.size()); ++i)
        {
            if (!std::isfinite(xs[i]) || xs[i] < xs[i - 1])
            {
                s = false;
                break;
            }
        }
        return s;
    }

    // rebuildSeriesGeometry, with the decimation threshold it read from PlotConstants (2). Returns whether it decimated.
    bool rebuild(const std::vector<double>& xs, const std::vector<double>& ys, bool sortedX, bool leftAxis,
                 std::vector<OldLine>& out_segments, std::vector<std::vector<OldPoint>>& out_bands) const
    {
        std::vector<OldLine> segments;
        std::vector<std::vector<OldPoint>> bands;
        int begin = 0;
        int end = static_cast<int>(xs.size());
        if (sortedX && !xs.empty())
        {
            const auto lo_it = std::lower_bound(xs.cbegin(), xs.cend(), x_lower);
            const auto hi_it = std::upper_bound(xs.cbegin(), xs.cend(), x_upper);
            begin = std::max(0, static_cast<int>(lo_it - xs.cbegin()) - 1);
            end = std::min(static_cast<int>(xs.size()), static_cast<int>(hi_it - xs.cbegin()) + 1);
        }
        const int columns = static_cast<int>(area_width);
        const bool decimate = columns > 0 && (end - begin) > columns * 2;

        auto pixelFor = [&](double vx, double vy) {
            return OldPoint{xToPixel(vx), leftAxis ? leftToPixel(vy) : rightToPixel(vy)};
        };
        OldPoint prev{};
        bool have_prev = false;
        auto addPoint = [&](const OldPoint& pt) {
            if (have_prev) segments.push_back(OldLine{prev, pt});
            prev = pt;
            have_prev = true;
        };

        if (!decimate)
        {
            for (int i = begin; i < end; ++i)
            {
                const double vx = xs[i];
                const double vy = ys[i];
                if (!std::isfinite(vx) || !std::isfinite(vy))
                {
                    have_prev = false;
                    continue;
                }
                addPoint(pixelFor(vx, vy));
            }
            out_segments = segments;
            out_bands.clear();
            return false;
        }

        int column = INT_MIN;
        double col_x = 0.0, lo = 0.0, hi = 0.0, sum = 0.0;
        int count = 0;
        std::vector<OldPoint> tops;
        std::vector<OldPoint> bottoms;
        auto closeBand = [&]() {
            if (tops.size() >= 2)
            {
                std::vector<OldPoint> poly;
                for (const OldPoint& pt : tops) poly.push_back(pt);
                for (int i = static_cast<int>(bottoms.size()) - 1; i >= 0; --i) poly.push_back(bottoms[i]);
                bands.push_back(poly);
            }
            tops.clear();
            bottoms.clear();
        };
        auto flushColumn = [&]() {
            if (column == INT_MIN) return;
            tops.push_back(OldPoint{col_x, lo});
            bottoms.push_back(OldPoint{col_x, hi});
            addPoint(OldPoint{col_x, sum / count});
            column = INT_MIN;
        };
        for (int i = begin; i < end; ++i)
        {
            const double vx = xs[i];
            const double vy = ys[i];
            if (!std::isfinite(vx) || !std::isfinite(vy))
            {
                flushColumn();
                closeBand();
                have_prev = false;
                continue;
            }
            const OldPoint pt = pixelFor(vx, vy);
            const int col = static_cast<int>(std::floor(pt.x));
            if (col != column)
            {
                flushColumn();
                column = col;
                col_x = pt.x;
                lo = pt.y;
                hi = pt.y;
                sum = pt.y;
                count = 1;
            }
            else
            {
                lo = std::min(lo, pt.y);
                hi = std::max(hi, pt.y);
                sum += pt.y;
                ++count;
            }
        }
        flushColumn();
        closeBand();
        out_segments = segments;
        out_bands = bands;
        return true;
    }

    static std::vector<double> tickValues(double lower, double upper, int count)
    {
        std::vector<double> ticks;
        if (!usableRange(lower, upper)) return ticks;
        const int n = std::max(2, count);
        const double step = (upper - lower) / (n - 1);
        for (int i = 0; i < n; ++i) ticks.push_back(lower + i * step);
        return ticks;
    }

    static std::vector<double> timeTickValues(double lower, double upper, int count, double origin)
    {
        std::vector<double> ticks;
        if (!usableRange(lower, upper)) return ticks;
        static const double kSteps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800,
                                        3600, 7200, 10800, 21600, 43200, 86400};
        const int n = std::max(2, count);
        const double raw = (upper - lower) / (n - 1);
        if (raw < 1.0) return tickValues(lower, upper, count);
        double step = 0.0;
        for (double s : kSteps)
        {
            if (s >= raw)
            {
                step = s;
                break;
            }
        }
        if (step == 0.0) step = std::ceil(raw / 86400.0) * 86400.0;
        const double first = std::ceil((lower + origin) / step) * step;
        for (double clock = first; clock <= upper + origin + step * 1e-9; clock += step) ticks.push_back(clock - origin);
        return ticks;
    }
};

chart::View viewOf(const Old& o, bool leftAxis)
{
    chart::View v;
    v.xLower = o.x_lower;
    v.xUpper = o.x_upper;
    v.yLower = leftAxis ? o.left_lower : o.right_lower;
    v.yUpper = leftAxis ? o.left_upper : o.right_upper;
    v.left = o.area_left;
    v.top = o.area_top;
    v.width = o.area_width;
    v.height = o.area_height;
    return v;
}

// Bit-for-bit: the geometry must place exactly the pixels the old code placed.
bool same(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }

bool sameGeometry(const chart::SeriesGeometry& g, const std::vector<OldLine>& segments,
                  const std::vector<std::vector<OldPoint>>& bands)
{
    if (g.segments.size() != segments.size() || g.bands.size() != bands.size()) return false;
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        const chart::Segment& s = g.segments[i];
        const OldLine& l = segments[i];
        if (!same(s.from.x, l.p1.x) || !same(s.from.y, l.p1.y) || !same(s.to.x, l.p2.x) || !same(s.to.y, l.p2.y))
            return false;
    }
    for (std::size_t b = 0; b < bands.size(); ++b)
    {
        if (g.bands[b].size() != bands[b].size()) return false;
        for (std::size_t i = 0; i < bands[b].size(); ++i)
            if (!same(g.bands[b][i].x, bands[b][i].x) || !same(g.bands[b][i].y, bands[b][i].y)) return false;
    }
    return true;
}

bool sameValues(const std::vector<double>& a, const std::vector<double>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!same(a[i], b[i])) return false;
    return true;
}

// A plot area where data and pixels are the same numbers: x 0..128 over 128 px, y 0..100 over 100 px from the top.
chart::View unitView()
{
    chart::View v;
    v.xLower = 0.0;
    v.xUpper = 128.0;
    v.yLower = 0.0;
    v.yUpper = 100.0;
    v.left = 0.0;
    v.top = 0.0;
    v.width = 128.0;
    v.height = 100.0;
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// Against the old chart's own code
// ---------------------------------------------------------------------------

TEST_CASE("ChartGeometry: a series' line and band are the code it replaced, over generated series and views")
{
    std::mt19937 rng(4031);
    auto uniform = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    auto chance = [&](double p) { return std::bernoulli_distribution(p)(rng); };
    auto pick = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };

    int decimated = 0, sparse = 0, culled = 0, unsorted = 0, gaps = 0, several_bands = 0, degenerate = 0, mismatched = 0;
    for (int trial = 0; trial < 3000; ++trial)
    {
        // The series: a size around and across the decimation threshold, mostly ordered in time.
        const int n = chance(0.05) ? pick(0, 2) : chance(0.5) ? pick(3, 120) : pick(200, 6000);
        std::vector<double> xs(static_cast<std::size_t>(n)), ys(static_cast<std::size_t>(n));
        double x = uniform(-50.0, 50.0);
        const double level = uniform(-20.0, 120.0);
        for (int i = 0; i < n; ++i)
        {
            x += chance(0.05) ? 0.0 : uniform(0.001, 2.0);   // repeated times now and then
            xs[i] = x;
            ys[i] = chance(0.02) ? uniform(-500.0, 500.0) : level + uniform(-10.0, 10.0);
        }
        if (n > 4 && chance(0.15))
        {
            std::swap(xs[pick(0, n - 1)], xs[pick(0, n - 1)]);
        }
        if (n > 4 && chance(0.2))
        {
            const int k = pick(1, 3);
            for (int j = 0; j < k; ++j)
            {
                const double bad = chance(0.5) ? kNaN : (chance(0.5) ? kInf : -kInf);
                (chance(0.5) ? xs : ys)[pick(0, n - 1)] = bad;
            }
        }
        if (n > 4 && chance(0.05))
        {
            ys.resize(static_cast<std::size_t>(pick(0, n - 1)));   // lengths that do not match
            ++mismatched;
        }

        // The view: the whole series, a slice of it, or somewhere beside it; now and then a range that cannot be used.
        Old o;
        const double data_lo = n > 0 && std::isfinite(xs.front()) ? xs.front() : 0.0;
        const double data_hi = n > 0 && std::isfinite(xs.back()) ? xs.back() : 100.0;
        const int how = pick(0, 3);
        if (how == 0)
        {
            o.x_lower = data_lo;
            o.x_upper = data_hi;
        }
        else if (how == 1)
        {
            const double a = uniform(std::min(data_lo, data_hi), std::max(data_lo, data_hi));
            const double b = uniform(std::min(data_lo, data_hi), std::max(data_lo, data_hi));
            o.x_lower = std::min(a, b);
            o.x_upper = std::max(a, b) + uniform(0.0, 3.0);
        }
        else if (how == 2)
        {
            o.x_lower = data_hi + uniform(0.0, 50.0);
            o.x_upper = o.x_lower + uniform(0.1, 50.0);
        }
        else
        {
            o.x_lower = uniform(-100.0, 100.0);
            o.x_upper = chance(0.5) ? o.x_lower : (chance(0.5) ? kNaN : o.x_lower - 1.0);
            ++degenerate;
        }
        o.left_lower = uniform(-50.0, 50.0);
        o.left_upper = chance(0.05) ? o.left_lower : o.left_lower + uniform(1.0, 200.0);
        o.right_lower = uniform(-60.0, 0.0);
        o.right_upper = chance(0.05) ? kInf : o.right_lower + uniform(1.0, 100.0);
        o.area_left = uniform(0.0, 120.0);
        o.area_top = uniform(0.0, 60.0);
        o.area_width = chance(0.03) ? 0.0 : uniform(1.0, 1500.0);
        o.area_height = chance(0.03) ? 0.0 : uniform(1.0, 900.0);
        const bool leftAxis = chance(0.5);

        const bool oldSorted = Old::sorted(xs);
        CHECK(chart::isSortedX(xs) == oldSorted);

        // The old chart truncated a mismatched pair to its common prefix before it stored the series.
        const std::size_t paired = std::min(xs.size(), ys.size());
        const std::vector<double> oxs(xs.begin(), xs.begin() + static_cast<std::ptrdiff_t>(paired));
        const std::vector<double> oys(ys.begin(), ys.begin() + static_cast<std::ptrdiff_t>(paired));
        const bool pairedSorted = Old::sorted(oxs);

        std::vector<OldLine> segments;
        std::vector<std::vector<OldPoint>> bands;
        const bool oldDecimated = o.rebuild(oxs, oys, pairedSorted, leftAxis, segments, bands);
        const chart::SeriesGeometry g = chart::buildSeriesGeometry(xs, ys, pairedSorted, viewOf(o, leftAxis));
        INFO("trial " << trial << ": " << paired << " samples, " << g.segments.size() << " segments vs " << segments.size()
                      << ", " << g.bands.size() << " bands vs " << bands.size());
        CHECK(sameGeometry(g, segments, bands));
        CHECK(g.decimated == oldDecimated);

        (g.decimated ? decimated : sparse)++;
        if (!pairedSorted) ++unsorted;
        if (std::any_of(oxs.begin(), oxs.end(), [](double v) { return !std::isfinite(v); }) ||
            std::any_of(oys.begin(), oys.end(), [](double v) { return !std::isfinite(v); }))
            ++gaps;
        if (bands.size() > 1) ++several_bands;
        if (pairedSorted && how == 1 && paired > 10) ++culled;
    }
    // Every path was reached, and often.
    CHECK(decimated > 600);
    CHECK(sparse > 600);
    CHECK(culled > 300);
    CHECK(unsorted > 300);
    CHECK(gaps > 300);
    CHECK(several_bands > 50);
    CHECK(degenerate > 300);
    CHECK(mismatched > 50);
}

TEST_CASE("ChartGeometry: the transforms are the code they replaced")
{
    std::mt19937 rng(77);
    auto uniform = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    for (int trial = 0; trial < 5000; ++trial)
    {
        Old o;
        o.x_lower = uniform(-1e6, 1e6);
        o.x_upper = trial % 17 == 0 ? o.x_lower : o.x_lower + uniform(1e-3, 1e6);
        o.left_lower = uniform(-100.0, 100.0);
        o.left_upper = trial % 19 == 0 ? kNaN : o.left_lower + uniform(1e-3, 1e3);
        o.area_left = uniform(0.0, 200.0);
        o.area_top = uniform(0.0, 200.0);
        o.area_width = trial % 23 == 0 ? 0.0 : uniform(1.0, 2000.0);
        o.area_height = trial % 29 == 0 ? -1.0 : uniform(1.0, 2000.0);
        const chart::View v = viewOf(o, true);
        const double x = uniform(-2e6, 2e6), px = uniform(-100.0, 2500.0), y = uniform(-500.0, 500.0);
        CHECK(same(chart::xToPixel(v, x), o.xToPixel(x)));
        CHECK(same(chart::pixelToX(v, px), o.pixelToX(px)));
        CHECK(same(chart::yToPixel(v, y), o.leftToPixel(y)));
        CHECK(same(chart::pixelToY(v, px), o.pixelToLeft(px)));
    }
}

TEST_CASE("ChartGeometry: the ticks are the code they replaced")
{
    std::mt19937 rng(9);
    auto uniform = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    int snapped = 0, even = 0, days = 0, none = 0;
    for (int trial = 0; trial < 20000; ++trial)
    {
        const double lower = uniform(-1e5, 1e6);
        // Spans from a fraction of a second to months, and now and then none at all.
        const double span = trial % 31 == 0 ? (trial % 2 ? 0.0 : -5.0) : std::pow(10.0, uniform(-2.0, 7.0));
        const double upper = trial % 97 == 0 ? kNaN : lower + span;
        const int count = static_cast<int>(uniform(-1.0, 25.0));
        const double origin = trial % 3 == 0 ? 0.0 : uniform(0.0, 86400.0 * 366);
        const std::vector<double> t = chart::timeTickValues(lower, upper, count, origin);
        CHECK(sameValues(t, Old::timeTickValues(lower, upper, count, origin)));
        CHECK(sameValues(chart::tickValues(lower, upper, count), Old::tickValues(lower, upper, count)));
        if (t.empty()) ++none;
        else if (span / (std::max(2, count) - 1) < 1.0) ++even;
        else if (span / (std::max(2, count) - 1) > 86400.0) ++days;
        else ++snapped;
    }
    CHECK(snapped > 5000);
    CHECK(even > 1000);
    CHECK(days > 300);
    CHECK(none > 300);
}

// ---------------------------------------------------------------------------
// Worked by hand
// ---------------------------------------------------------------------------

TEST_CASE("ChartGeometry: a dense series still reaches a single-sample dropout, and its line is each column's mean")
{
    // 1024 samples at 100 over 128 columns, eight to a column: drawn from the envelope. One sample, in column 65,
    // drops to 0. The band must reach the bottom of the plot in that column, however dense the series; the line there
    // is the column's mean, (7 x 0 px + 100 px) / 8 = 12.5 px; everywhere else it is at the top.
    std::vector<double> xs(1024), ys(1024, 100.0);
    for (int i = 0; i < 1024; ++i) xs[i] = i * 0.125;
    ys[520] = 0.0;
    const chart::SeriesGeometry g = chart::buildSeriesGeometry(xs, ys, true, unitView());
    REQUIRE(g.decimated);
    REQUIRE(g.bands.size() == 1);
    REQUIRE(g.segments.size() == 127);   // one point per column, joined

    // The band: 128 tops (the columns' smallest pixel y) then 128 bottoms, right to left.
    const std::vector<chart::Point>& band = g.bands[0];
    REQUIRE(band.size() == 256);
    for (int c = 0; c < 128; ++c)
    {
        CHECK(band[c].x == c);
        CHECK(band[c].y == 0.0);
        CHECK(band[255 - c].x == c);
        CHECK(band[255 - c].y == (c == 65 ? 100.0 : 0.0));
    }
    for (int c = 0; c < 127; ++c)
    {
        CHECK(g.segments[c].from.x == c);
        CHECK(g.segments[c].from.y == (c == 65 ? 12.5 : 0.0));
        CHECK(g.segments[c].to.x == c + 1);
    }
}

TEST_CASE("ChartGeometry: a dense noisy series shows its spread, with its mean through the middle")
{
    // Alternating 40 and 60 (pixel rows 60 and 40): each column's band runs from 40 to 60 and its mean is 50.
    std::vector<double> xs(1024), ys(1024);
    for (int i = 0; i < 1024; ++i)
    {
        xs[i] = i * 0.125;
        ys[i] = i % 2 ? 40.0 : 60.0;
    }
    const chart::SeriesGeometry g = chart::buildSeriesGeometry(xs, ys, true, unitView());
    REQUIRE(g.bands.size() == 1);
    for (int c = 0; c < 128; ++c)
    {
        CHECK(g.bands[0][c].y == 40.0);
        CHECK(g.bands[0][255 - c].y == 60.0);
    }
    for (const chart::Segment& s : g.segments)
    {
        CHECK(s.from.y == 50.0);
        CHECK(s.to.y == 50.0);
    }
}

TEST_CASE("ChartGeometry: up to two samples a column are drawn one by one")
{
    // 256 samples over 128 columns is exactly the threshold: still sample by sample, no band.
    std::vector<double> xs(256), ys(256, 50.0);
    for (int i = 0; i < 256; ++i) xs[i] = i * 0.5;
    chart::SeriesGeometry g = chart::buildSeriesGeometry(xs, ys, true, unitView());
    CHECK_FALSE(g.decimated);
    CHECK(g.bands.empty());
    CHECK(g.segments.size() == 255);
    CHECK(g.segments[3].from.x == 1.5);
    CHECK(g.segments[3].from.y == 50.0);

    // One more sample than that (in a view that holds them all) and it is drawn from the envelope.
    xs.push_back(127.75);
    ys.push_back(50.0);
    g = chart::buildSeriesGeometry(xs, ys, true, unitView());
    CHECK(g.decimated);
    CHECK(ChartConstants::kMaxSamplesPerPixelColumn == 2);
}

TEST_CASE("ChartGeometry: a line persists through a view no sample is inside")
{
    // Two samples either side of the view: the one segment the data describes still crosses it.
    const std::vector<double> xs{0.0, 100.0}, ys{50.0, 50.0};
    chart::View v = unitView();
    v.xLower = 40.0;
    v.xUpper = 60.0;
    const chart::SeriesGeometry g = chart::buildSeriesGeometry(xs, ys, true, v);
    REQUIRE(g.segments.size() == 1);
    CHECK(g.segments[0].from.x == -256.0);   // (0 - 40) / 20 x 128
    CHECK(g.segments[0].to.x == 384.0);      // (100 - 40) / 20 x 128
    CHECK(g.segments[0].from.y == 50.0);
}

TEST_CASE("ChartGeometry: an ordered series is walked only where it is in view, plus one sample each side")
{
    // 10,000 samples a second apart; the view holds 10.5 to 20.5, so samples 11 to 20 and one each side: 12 points.
    std::vector<double> xs(10000), ys(10000, 1.0);
    for (int i = 0; i < 10000; ++i) xs[i] = i;
    chart::View v = unitView();
    v.xLower = 10.5;
    v.xUpper = 20.5;
    chart::SeriesGeometry g = chart::buildSeriesGeometry(xs, ys, true, v);
    CHECK_FALSE(g.decimated);   // 12 samples, not 10,000, over 128 columns
    REQUIRE(g.segments.size() == 11);
    CHECK(g.segments.front().from.x == chart::xToPixel(v, 10.0));
    CHECK(g.segments.back().to.x == chart::xToPixel(v, 21.0));

    // Unordered, the same series is walked whole, and so drawn from the envelope.
    g = chart::buildSeriesGeometry(xs, ys, false, v);
    CHECK(g.decimated);
}

TEST_CASE("ChartGeometry: a gap breaks the line, and the band")
{
    const std::vector<double> xs{0.0, 1.0, 2.0, 3.0, 4.0}, ys{10.0, 20.0, kNaN, 40.0, 50.0};
    chart::SeriesGeometry g = chart::buildSeriesGeometry(xs, ys, true, unitView());
    REQUIRE(g.segments.size() == 2);
    CHECK(g.segments[0].to.x == 1.0);
    CHECK(g.segments[1].from.x == 3.0);

    // Dense, with a non-finite time in the middle: two runs of columns, two bands, and no segment across the gap.
    std::vector<double> dx(1024), dy(1024, 50.0);
    for (int i = 0; i < 1024; ++i) dx[i] = i * 0.125;
    dx[512] = kNaN;
    g = chart::buildSeriesGeometry(dx, dy, false, unitView());
    REQUIRE(g.decimated);
    CHECK(g.bands.size() == 2);
    CHECK(g.segments.size() == 126);   // 64 column points each side of the gap, joined within each side only
    // A column's point is at its first sample: column 64 begins at 64.125, the sample after the gap. No segment
    // crosses from one side to the other.
    for (const chart::Segment& s : g.segments) CHECK((s.to.x < 64.0) == (s.from.x < 64.0));
    CHECK(g.segments[63].from.x == 64.125);
}

TEST_CASE("ChartGeometry: a run of one column has no band, and only the paired samples count")
{
    // Three samples in column 0, a gap, then three in column 1: drawn from the envelope (7 samples > 2 x 2 columns),
    // but each run is one column, so neither has a band, and the gap leaves no segment.
    chart::View v = unitView();
    v.width = 2.0;
    v.xUpper = 2.0;
    const std::vector<double> xs{0.0, 0.1, 0.2, kNaN, 1.1, 1.2, 1.3};
    const std::vector<double> ys{50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0};
    chart::SeriesGeometry g = chart::buildSeriesGeometry(xs, ys, false, v);
    CHECK(g.decimated);
    CHECK(g.bands.empty());
    CHECK(g.segments.empty());

    // A longer xs than ys: the extra times are not drawn.
    const std::vector<double> longer{0.0, 10.0, 20.0, 30.0};
    const std::vector<double> shorter{1.0, 2.0};
    g = chart::buildSeriesGeometry(longer, shorter, true, unitView());
    REQUIRE(g.segments.size() == 1);
    CHECK(g.segments[0].to.x == 10.0);
}

TEST_CASE("ChartGeometry: the transforms orient Y upward, round-trip, and fall back to an edge")
{
    chart::View v = unitView();
    v.left = 50.0;
    v.top = 20.0;
    CHECK(chart::xToPixel(v, 0.0) == 50.0);
    CHECK(chart::xToPixel(v, 128.0) == 178.0);
    CHECK(chart::yToPixel(v, 0.0) == 120.0);   // the bottom
    CHECK(chart::yToPixel(v, 100.0) == 20.0);  // the top: a larger value sits higher
    for (double x : {0.0, 25.0, 50.0, 99.9})
    {
        CHECK(std::abs(chart::pixelToX(v, chart::xToPixel(v, x)) - x) < 1e-9);
        CHECK(std::abs(chart::pixelToY(v, chart::yToPixel(v, x)) - x) < 1e-9);
    }

    chart::View bad = v;
    bad.xUpper = bad.xLower;
    bad.yUpper = kNaN;
    CHECK(chart::xToPixel(bad, 7.0) == 50.0);
    CHECK(chart::yToPixel(bad, 7.0) == 120.0);
    bad.width = 0.0;
    bad.height = 0.0;
    CHECK(chart::pixelToX(bad, 99.0) == bad.xLower);
    CHECK(chart::pixelToY(bad, 99.0) == bad.yLower);

    CHECK(chart::usableRange(0.0, 1e-300));
    CHECK_FALSE(chart::usableRange(1.0, 1.0));
    CHECK_FALSE(chart::usableRange(2.0, 1.0));
    CHECK_FALSE(chart::usableRange(kNaN, 1.0));
    CHECK_FALSE(chart::usableRange(0.0, kInf));
    CHECK_FALSE(chart::usableRange(-kInf, 0.0));
}

TEST_CASE("ChartGeometry: a series is sorted when every time after the first is finite and none goes back")
{
    using V = std::vector<double>;
    CHECK(chart::isSortedX(V{}));
    CHECK(chart::isSortedX(V{kNaN}));
    CHECK(chart::isSortedX(V{1.0, 1.0, 2.0}));
    CHECK(chart::isSortedX(V{kNaN, 1.0, 2.0}));   // the first is not checked, as the old chart did not check it
    CHECK_FALSE(chart::isSortedX(V{1.0, kNaN, 2.0}));
    CHECK_FALSE(chart::isSortedX(V{1.0, 2.0, kInf}));
    CHECK_FALSE(chart::isSortedX(V{2.0, 1.0}));
}

TEST_CASE("ChartGeometry: time ticks land on round times of the clock")
{
    // The old chart's own cases (time ticks on round times, and round on the clock rather than in
    // elapsed seconds), now on the core.
    auto multiplesOf = [](const std::vector<double>& ticks, double origin, double step) {
        return std::all_of(ticks.begin(), ticks.end(), [&](double t) {
            const double clock = t + origin;
            return std::abs(clock / step - std::round(clock / step)) <= 1e-6;
        });
    };

    std::vector<double> t = chart::timeTickValues(0.0, 116.0, 5);   // ~29 s wanted: 30 s
    CHECK(t == std::vector<double>{0.0, 30.0, 60.0, 90.0});
    t = chart::timeTickValues(1000.0, 1116.0, 5);                  // neither end round
    CHECK(t == std::vector<double>{1020.0, 1050.0, 1080.0, 1110.0});
    CHECK(multiplesOf(chart::timeTickValues(0.0, 5400.0, 5), 0.0, 1800.0));   // ~22 min wanted: 30 min

    const double kDay = 86400.0;
    t = chart::timeTickValues(0.0, 30.0 * kDay, 5);                 // ~7.5 days wanted: whole days, 8 at a time
    CHECK(t == std::vector<double>{0.0, 8 * kDay, 16 * kDay, 24 * kDay});

    t = chart::timeTickValues(0.0, 0.5, 5);                         // below a second: divided evenly
    CHECK(t == std::vector<double>{0.0, 0.125, 0.25, 0.375, 0.5});

    // On the clock: 10:30:17.046 plus 0..116 s gives ticks at :30, :00, :30 of the clock, inside the view.
    const double kOrigin = 37817.046;
    t = chart::timeTickValues(0.0, 116.0, 5, kOrigin);
    REQUIRE(t.size() == 4);
    CHECK(multiplesOf(t, kOrigin, 30.0));
    CHECK(std::abs(t[0] - 12.954) < 1e-9);
    CHECK(std::all_of(t.begin(), t.end(), [](double v) { return v >= 0.0 && v <= 116.0; }));
    CHECK(multiplesOf(chart::timeTickValues(0.0, 600.0, 5, 37812.0), 37812.0, 300.0));
    CHECK(chart::timeTickValues(0.0, 116.0, 5, 0.0) == chart::timeTickValues(0.0, 116.0, 5));

    // A span that is exactly a step of the ladder takes that step, not the next (120 s over 5 ticks is 30 s).
    CHECK(chart::timeTickValues(0.0, 120.0, 5) == std::vector<double>{0.0, 30.0, 60.0, 90.0, 120.0});
    // A tick a hair past the upper end (within a billionth of a step) still counts, so rounding cannot drop the last.
    CHECK(chart::timeTickValues(0.0, 120.0 - 1e-10, 5) == std::vector<double>{0.0, 30.0, 60.0, 90.0, 120.0});
    CHECK(chart::timeTickValues(0.0, 120.0 - 1e-6, 5) == std::vector<double>{0.0, 30.0, 60.0, 90.0});
    // Exactly a second a tick is snapped, not divided evenly: 0.5 to 4.5 s gives the whole seconds inside it.
    CHECK(chart::timeTickValues(0.5, 4.5, 5) == std::vector<double>{1.0, 2.0, 3.0, 4.0});
    CHECK(chart::timeTickValues(5.0, 5.0, 5).empty());
    CHECK(chart::timeTickValues(kNaN, 5.0, 5).empty());
}

TEST_CASE("ChartGeometry: plain ticks divide the range evenly, both ends included")
{
    CHECK(chart::tickValues(0.0, 100.0, 5) == std::vector<double>{0.0, 25.0, 50.0, 75.0, 100.0});
    CHECK(chart::tickValues(-1.0, 1.0, 0) == std::vector<double>{-1.0, 1.0});   // at least two
    CHECK(chart::tickValues(1.0, 1.0, 5).empty());
    CHECK(chart::tickValues(0.0, kInf, 5).empty());
}

// ---------------------------------------------------------------------------
// A series' index: the envelope looked up rather than walked
// ---------------------------------------------------------------------------

namespace {

// The geometry built with an index against the one walked: the same columns, so the same number of segments and
// bands; every band point bit for bit (the extremes of a column are the pixels of its extreme values); the line's x
// bit for bit and its y within rounding (the mean of the values is summed in another order). Reports the largest
// difference in a mean.
bool sameEnvelope(const chart::SeriesGeometry& looked, const chart::SeriesGeometry& walked, double& worst)
{
    if (looked.decimated != walked.decimated || looked.segments.size() != walked.segments.size() ||
        looked.bands.size() != walked.bands.size())
        return false;
    for (std::size_t b = 0; b < walked.bands.size(); ++b)
    {
        if (looked.bands[b].size() != walked.bands[b].size()) return false;
        for (std::size_t i = 0; i < walked.bands[b].size(); ++i)
            if (!same(looked.bands[b][i].x, walked.bands[b][i].x) || !same(looked.bands[b][i].y, walked.bands[b][i].y))
                return false;
    }
    for (std::size_t i = 0; i < walked.segments.size(); ++i)
    {
        const chart::Segment& l = looked.segments[i];
        const chart::Segment& w = walked.segments[i];
        if (!same(l.from.x, w.from.x) || !same(l.to.x, w.to.x)) return false;
        const double d = std::max(std::abs(l.from.y - w.from.y), std::abs(l.to.y - w.to.y));
        if (!(d <= 1e-6)) return false;   // false for a NaN on one side only, too
        worst = std::max(worst, d);
    }
    return true;
}

bool bitIdentical(const chart::SeriesGeometry& a, const chart::SeriesGeometry& b)
{
    if (a.decimated != b.decimated || a.segments.size() != b.segments.size() || a.bands.size() != b.bands.size())
        return false;
    for (std::size_t i = 0; i < a.segments.size(); ++i)
        if (!same(a.segments[i].from.x, b.segments[i].from.x) || !same(a.segments[i].from.y, b.segments[i].from.y) ||
            !same(a.segments[i].to.x, b.segments[i].to.x) || !same(a.segments[i].to.y, b.segments[i].to.y))
            return false;
    for (std::size_t k = 0; k < a.bands.size(); ++k)
    {
        if (a.bands[k].size() != b.bands[k].size()) return false;
        for (std::size_t i = 0; i < a.bands[k].size(); ++i)
            if (!same(a.bands[k][i].x, b.bands[k][i].x) || !same(a.bands[k][i].y, b.bands[k][i].y)) return false;
    }
    return true;
}

// The unit view squeezed to four columns: 1024 samples at 0.125 apart are 256 to a column, eight whole blocks each,
// so a column is looked up from the levels rather than walked at its ends.
chart::View fourColumns()
{
    chart::View v = unitView();
    v.width = 4.0;
    return v;
}

}  // namespace

TEST_CASE("ChartGeometry: a series' index summarises any run of finite values as walking it would")
{
    // Every run of a short series, across every block boundary; then random runs of long ones. The least and the
    // greatest exactly, the sum to rounding, and where the next non-finite value lies.
    constexpr int kBlock = ChartConstants::kIndexBlockSamples;
    std::mt19937 rng(5501);
    auto uniform = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    auto chance = [&](double p) { return std::bernoulli_distribution(p)(rng); };
    auto pick = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };

    auto agrees = [&](const std::vector<double>& ys, const chart::SeriesIndex& index, std::size_t first, std::size_t last) {
        double mn = kInf, mx = -kInf, sum = 0.0, scale = 0.0;
        for (std::size_t k = first; k < last; ++k)
        {
            mn = std::min(mn, ys[k]);
            mx = std::max(mx, ys[k]);
            sum += ys[k];
            scale += std::abs(ys[k]);
        }
        const chart::SeriesIndex::Summary s = index.summarize(ys, first, last);
        return same(s.min, mn) && same(s.max, mx) && std::abs(s.sum - sum) <= 1e-12 * scale + 1e-300;
    };

    for (int n : {1, kBlock - 1, kBlock, kBlock + 1, 2 * kBlock, 5 * kBlock + 5})
    {
        std::vector<double> ys(static_cast<std::size_t>(n));
        for (double& v : ys) v = uniform(-100.0, 100.0);
        const chart::SeriesIndex index(ys);
        CHECK(index.size() == ys.size());
        CHECK(index.nextNonFinite(0) == ys.size());
        bool all = true;
        for (std::size_t first = 0; first < ys.size(); ++first)
            for (std::size_t last = first + 1; last <= ys.size(); ++last) all = all && agrees(ys, index, first, last);
        CHECK(all);
    }
    CHECK(chart::SeriesIndex(std::vector<double>{}).size() == 0);
    CHECK(chart::SeriesIndex(std::vector<double>{}).nextNonFinite(0) == 0);

    int runs = 0, long_runs = 0;
    for (int trial = 0; trial < 200; ++trial)
    {
        const int n = chance(0.5) ? pick(1, 3000) : pick(3000, 200000);
        std::vector<double> ys(static_cast<std::size_t>(n));
        for (double& v : ys) v = chance(0.001) ? uniform(-1e6, 1e6) : uniform(-50.0, 150.0);
        if (chance(0.5))
            for (int k = pick(1, 6); k > 0; --k) ys[static_cast<std::size_t>(pick(0, n - 1))] = chance(0.5) ? kNaN : -kInf;
        std::vector<std::size_t> bad;
        for (std::size_t i = 0; i < ys.size(); ++i)
            if (!std::isfinite(ys[i])) bad.push_back(i);
        const chart::SeriesIndex index(ys);
        for (int q = 0; q < 60; ++q)
        {
            const std::size_t from = static_cast<std::size_t>(pick(0, n));
            const auto next = std::lower_bound(bad.begin(), bad.end(), from);
            const std::size_t expected = next == bad.end() ? ys.size() : *next;
            REQUIRE(index.nextNonFinite(from) == expected);
            if (from >= ys.size() || expected == from) continue;
            const std::size_t last = from + 1 + static_cast<std::size_t>(pick(0, static_cast<int>(expected - from - 1)));
            CHECK(agrees(ys, index, from, last));
            ++runs;
            if (last - from > 64 * static_cast<std::size_t>(kBlock)) ++long_runs;
        }
    }
    CHECK(runs > 5000);
    CHECK(long_runs > 1000);
}

TEST_CASE("ChartGeometry: the envelope looked up in an index is the envelope walked, over generated series and views")
{
    std::mt19937 rng(9157);
    auto uniform = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    auto chance = [&](double p) { return std::bernoulli_distribution(p)(rng); };
    auto pick = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };

    int looked_up = 0, sparse = 0, with_gaps = 0, first_time_bad = 0, several_bands = 0, beside = 0, big = 0;
    int upside_down = 0;
    double worst = 0.0;
    for (int trial = 0; trial < 1500; ++trial)
    {
        // Ordered times, now and then repeated, now and then jumping (a recording that stopped and started again).
        const int n = chance(0.25) ? pick(3, 400) : chance(0.85) ? pick(400, 20000) : pick(20000, 200000);
        std::vector<double> xs(static_cast<std::size_t>(n)), ys(static_cast<std::size_t>(n));
        double x = uniform(-50.0, 50.0);
        const double level = uniform(-20.0, 120.0);
        const double step = std::pow(10.0, uniform(-3.0, 0.5));
        for (int i = 0; i < n; ++i)
        {
            x += chance(0.03) ? 0.0 : chance(0.0005) ? uniform(10.0, 5000.0) * step : step * uniform(0.5, 1.5);
            xs[static_cast<std::size_t>(i)] = x;
            ys[static_cast<std::size_t>(i)] = chance(0.003) ? uniform(-500.0, 500.0) : level + uniform(-10.0, 10.0);
        }
        if (chance(0.25))
        {
            // Dropouts: single non-finite values and runs of them.
            for (int k = pick(1, 8); k > 0; --k)
            {
                const int at = pick(0, n - 1), len = chance(0.7) ? 1 : pick(2, 60);
                for (int j = at; j < std::min(n, at + len); ++j) ys[static_cast<std::size_t>(j)] = chance(0.5) ? kNaN : kInf;
            }
            ++with_gaps;
        }
        if (chance(0.05))
        {
            xs[0] = chance(0.5) ? kNaN : -kInf;   // isSortedX does not look at the first time
            ++first_time_bad;
        }

        chart::View v;
        const double lo = std::isfinite(xs.front()) ? xs.front() : xs[std::min<std::size_t>(1, xs.size() - 1)];
        const double hi = xs.back();
        const int how = pick(0, 4);
        if (how == 0)
        {
            v.xLower = lo;
            v.xUpper = hi;
        }
        else if (how <= 2)
        {
            const double a = uniform(lo, hi), b = uniform(lo, hi);
            v.xLower = std::min(a, b);
            v.xUpper = std::max(a, b) + uniform(0.0, 3.0) * step;
        }
        else if (how == 3)
        {
            v.xLower = hi + uniform(0.0, 50.0);   // beside the data: only its last sample is walked
            v.xUpper = v.xLower + uniform(0.1, 50.0);
            ++beside;
        }
        else
        {
            v.xLower = uniform(lo, hi);
            v.xUpper = chance(0.5) ? v.xLower : kNaN;   // an X range that cannot be used: every sample in one column
        }
        v.yLower = uniform(-50.0, 50.0);
        v.yUpper = chance(0.05) ? v.yLower : v.yLower + uniform(1.0, 200.0);
        v.left = uniform(0.0, 120.0);
        v.top = uniform(0.0, 60.0);
        v.width = chance(0.03) ? 0.0 : uniform(1.0, 1600.0);
        // Now and then a plot area turned upside down: the pixel of a column's greatest value is then its lowest pixel.
        v.height = chance(0.03) ? 0.0 : chance(0.05) ? -uniform(1.0, 900.0) : uniform(1.0, 900.0);
        if (v.height < 0.0) ++upside_down;

        REQUIRE(chart::isSortedX(xs));
        const chart::SeriesIndex index(ys);
        const chart::SeriesGeometry walked = chart::buildSeriesGeometry(xs, ys, true, v);
        const chart::SeriesGeometry looked = chart::buildSeriesGeometry(xs, ys, true, v, &index);
        INFO("trial " << trial << ": " << n << " samples, view " << v.xLower << ".." << v.xUpper << " over " << v.width
                      << " px; " << looked.segments.size() << " segments vs " << walked.segments.size() << ", "
                      << looked.bands.size() << " bands vs " << walked.bands.size());
        CHECK(sameEnvelope(looked, walked, worst));
        (walked.decimated ? looked_up : sparse)++;
        if (walked.bands.size() > 1) ++several_bands;
        if (walked.decimated && n >= 20000) ++big;
    }
    INFO("largest difference in a mean: " << worst << " px");
    CHECK(worst < 1e-9);
    CHECK(looked_up > 500);
    CHECK(sparse > 150);
    CHECK(with_gaps > 300);
    CHECK(first_time_bad > 40);
    CHECK(several_bands > 60);
    CHECK(beside > 150);
    CHECK(big > 80);
    CHECK(upside_down > 40);
}

TEST_CASE("ChartGeometry: the envelope comes from the index when there is one, and only an index that fits is used")
{
    // The index is built over values the series does not have - zero where the series is at 100 - so the columns it
    // answers for show the zeros: the bottom of the plot, pixel row 100, where the series itself is at row 0.
    std::vector<double> xs(1024), ys(1024, 100.0);
    for (int i = 0; i < 1024; ++i) xs[i] = i * 0.125;
    const chart::SeriesIndex other(std::vector<double>(1024, 0.0));
    const chart::SeriesGeometry g = chart::buildSeriesGeometry(xs, ys, true, fourColumns(), &other);
    REQUIRE(g.decimated);
    REQUIRE(g.bands.size() == 1);
    REQUIRE(g.bands[0].size() == 8);
    for (const chart::Point& p : g.bands[0]) CHECK(p.y == 100.0);
    REQUIRE(g.segments.size() == 3);
    for (const chart::Segment& s : g.segments) CHECK(s.from.y == 100.0);
    // Walked, the same columns are at the top.
    for (const chart::Point& p : chart::buildSeriesGeometry(xs, ys, true, fourColumns()).bands[0]) CHECK(p.y == 0.0);

    // Its own index gives what walking gives.
    const chart::SeriesIndex own(ys);
    double worst = 0.0;
    CHECK(sameEnvelope(chart::buildSeriesGeometry(xs, ys, true, fourColumns(), &own),
                       chart::buildSeriesGeometry(xs, ys, true, fourColumns()), worst));

    // An index built over another number of values is not used; nor is any index when the times are not ordered.
    const chart::SeriesIndex misfit(std::vector<double>(1023, 0.0));
    CHECK(bitIdentical(chart::buildSeriesGeometry(xs, ys, true, fourColumns(), &misfit),
                       chart::buildSeriesGeometry(xs, ys, true, fourColumns())));
    CHECK(bitIdentical(chart::buildSeriesGeometry(xs, ys, false, fourColumns(), &other),
                       chart::buildSeriesGeometry(xs, ys, false, fourColumns())));
    // It is the paired prefix that has to fit: with one more value than times, an index over the paired values is used.
    std::vector<double> longer = ys;
    longer.push_back(5.0);
    CHECK(chart::buildSeriesGeometry(xs, longer, true, fourColumns(), &other).bands[0][0].y == 100.0);
}

TEST_CASE("ChartGeometry: looked up, a dense series still reaches a single-sample dropout, and its line is each column's mean")
{
    // The walked case above, with an index: 1024 samples at 100, one dropping to 0. Four columns of 256 samples, so the
    // dropout sits inside a block that is looked up, not walked.
    std::vector<double> xs(1024), ys(1024, 100.0);
    for (int i = 0; i < 1024; ++i) xs[i] = i * 0.125;
    ys[520] = 0.0;
    const chart::SeriesIndex index(ys);
    const chart::SeriesGeometry g = chart::buildSeriesGeometry(xs, ys, true, fourColumns(), &index);
    REQUIRE(g.decimated);
    REQUIRE(g.bands.size() == 1);
    const std::vector<chart::Point>& band = g.bands[0];
    REQUIRE(band.size() == 8);
    for (int c = 0; c < 4; ++c)
    {
        CHECK(band[static_cast<std::size_t>(c)].x == c);
        CHECK(band[static_cast<std::size_t>(c)].y == 0.0);                              // the tops: 100, the top of the plot
        CHECK(band[static_cast<std::size_t>(7 - c)].y == (c == 2 ? 100.0 : 0.0));       // sample 520 is in column 2
    }
    REQUIRE(g.segments.size() == 3);
    CHECK(g.segments[0].from.y == 0.0);
    CHECK(std::abs(g.segments[1].to.y - 0.390625) < 1e-12);   // column 2's mean: 255 at 100 and one at 0, 99.609375
}
