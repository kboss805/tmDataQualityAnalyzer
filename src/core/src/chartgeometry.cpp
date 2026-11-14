/**
 * @file chartgeometry.cpp
 * @brief Implementation of the plot's geometry - transcribed expression for expression from the chart code it replaced,
 * which test_chartgeometry.cpp restates and compares it with, so the chart draws exactly the pixels it drew before.
 * The envelope looked up in a SeriesIndex is held to the walked one by the same tests.
 */

#include "chartgeometry.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <limits>

#include "constants.h"

namespace chart
{

bool usableRange(double lower, double upper)
{
    return std::isfinite(lower) && std::isfinite(upper) && (upper - lower) > 0.0;
}

double xToPixel(const View& view, double x)
{
    if (!usableRange(view.xLower, view.xUpper))
    {
        return view.left;
    }
    const double frac = (x - view.xLower) / (view.xUpper - view.xLower);
    return view.left + frac * view.width;
}

double pixelToX(const View& view, double px)
{
    if (view.width <= 0)
    {
        return view.xLower;
    }
    const double frac = (px - view.left) / view.width;
    return view.xLower + frac * (view.xUpper - view.xLower);
}

double yToPixel(const View& view, double y)
{
    const double bottom = view.top + view.height;
    if (!usableRange(view.yLower, view.yUpper))
    {
        return bottom;
    }
    const double frac = (y - view.yLower) / (view.yUpper - view.yLower);
    return bottom - frac * view.height;
}

double pixelToY(const View& view, double py)
{
    if (view.height <= 0)
    {
        return view.yLower;
    }
    // Screen Y grows downward, data Y upward.
    const double frac = ((view.top + view.height) - py) / view.height;
    return view.yLower + frac * (view.yUpper - view.yLower);
}

bool isSortedX(std::span<const double> xs)
{
    for (std::size_t i = 1; i < xs.size(); ++i)
    {
        if (!std::isfinite(xs[i]) || xs[i] < xs[i - 1])
        {
            return false;
        }
    }
    return true;
}

namespace
{

/// A series' line: each point joined to the one before, a gap breaking it.
struct LineBuilder
{
    std::vector<Segment>& segments;
    Point prev{};
    bool  have_prev = false;

    void add(const Point& pt)
    {
        if (have_prev)
        {
            segments.push_back(Segment{prev, pt});
        }
        prev      = pt;
        have_prev = true;
    }
    void breakLine() { have_prev = false; }
};

/// A series' band, built as two edges: tops left-to-right, bottoms right-to-left, so closing the polygon traces the
/// outline of the run.
struct BandBuilder
{
    std::vector<std::vector<Point>>& bands;
    std::vector<Point> tops;
    std::vector<Point> bottoms;

    void add(double x, double lo, double hi)
    {
        tops.push_back(Point{x, lo});
        bottoms.push_back(Point{x, hi});
    }
    void close()
    {
        if (tops.size() >= 2)
        {
            std::vector<Point> poly;
            poly.reserve(tops.size() + bottoms.size());
            poly.insert(poly.end(), tops.begin(), tops.end());
            poly.insert(poly.end(), bottoms.rbegin(), bottoms.rend());
            bands.push_back(std::move(poly));
        }
        tops.clear();
        bottoms.clear();
    }
};

/// The envelope of samples [begin, end), walked one by one: each sample transformed, and its column's extremes and
/// the sum of its pixels kept as it goes. Works whether or not the times are ordered.
void walkEnvelope(std::span<const double> xs, std::span<const double> ys, int begin, int end, const View& view,
                  LineBuilder& line, BandBuilder& band)
{
    int    column = INT_MIN;   ///< Pixel column being accumulated.
    double col_x  = 0.0;       ///< Its x in pixels.
    double lo     = 0.0;       ///< Lowest y (in pixels) in the column.
    double hi     = 0.0;       ///< Highest y (in pixels) in the column.
    double sum    = 0.0;       ///< Sum of the column's y pixels, for the mean.
    int    count  = 0;

    auto flushColumn = [&]() {
        if (column == INT_MIN)
        {
            return;
        }
        band.add(col_x, lo, hi);
        line.add(Point{col_x, sum / count});
        column = INT_MIN;
    };

    for (int i = begin; i < end; ++i)
    {
        const double vx = xs[static_cast<std::size_t>(i)];
        const double vy = ys[static_cast<std::size_t>(i)];
        if (!std::isfinite(vx) || !std::isfinite(vy))
        {
            flushColumn();
            band.close();
            line.breakLine();   // the gap breaks both the line and the band
            continue;
        }

        const Point pt{xToPixel(view, vx), yToPixel(view, vy)};
        const int   col = static_cast<int>(std::floor(pt.x));
        if (col != column)
        {
            flushColumn();
            column = col;
            col_x  = pt.x;
            lo     = pt.y;
            hi     = pt.y;
            sum    = pt.y;
            count  = 1;
        }
        else
        {
            lo   = std::min(lo, pt.y);
            hi   = std::max(hi, pt.y);
            sum += pt.y;
            ++count;
        }
    }
    flushColumn();
}

/// The same envelope of ordered samples [begin, end), looked up: each column's samples found by binary search on the
/// times and summarised by the index, so a column costs about the same whether it holds three samples or three
/// thousand.
///
/// It draws what walkEnvelope draws. The columns are the same: a column is the samples whose x lands in one pixel
/// column by the very transform walkEnvelope applies (the inverse transform only guesses where a column ends; the
/// forward one decides), cut short by a non-finite value as walkEnvelope cuts it. The band is the same, bit for bit:
/// the transform to pixels is monotonic, so the extreme pixels of a column are the pixels of its extreme values. The
/// mean is the pixel of the mean value, which is the mean of the pixels - equal to rounding (about 1e-12 px), since
/// the sum is taken in another order.
void lookUpEnvelope(std::span<const double> xs, std::span<const double> ys, std::size_t begin, std::size_t end,
                    const View& view, const SeriesIndex& index, LineBuilder& line, BandBuilder& band)
{
    std::size_t next_bad = index.nextNonFinite(begin);
    std::size_t i        = begin;
    while (i < end)
    {
        // A non-finite value breaks the line and the band, as in walkEnvelope. An ordered series has finite times
        // after the first; isSortedX does not look at the first.
        if (i == next_bad || !std::isfinite(xs[i]))
        {
            band.close();
            line.breakLine();
            if (i == next_bad)
            {
                next_bad = index.nextNonFinite(i + 1);
            }
            ++i;
            continue;
        }

        const double px     = xToPixel(view, xs[i]);
        const double col    = std::floor(px);
        auto         beyond = [&](double x) { return std::floor(xToPixel(view, x)) > col; };

        // Where the next column starts: guessed from the inverse transform, and kept only if the forward transform
        // agrees on both sides of it; otherwise searched for with the forward transform alone.
        const auto  first = xs.begin() + static_cast<std::ptrdiff_t>(i + 1);
        const auto  last  = xs.begin() + static_cast<std::ptrdiff_t>(end);
        std::size_t next  = static_cast<std::size_t>(std::lower_bound(first, last, pixelToX(view, col + 1.0)) - xs.begin());
        if (!((next == end || beyond(xs[next])) && !beyond(xs[next - 1])))
        {
            next = static_cast<std::size_t>(std::partition_point(first, last, [&](double x) { return !beyond(x); }) -
                                            xs.begin());
        }
        next = std::min(next, next_bad);

        const SeriesIndex::Summary s      = index.summarize(ys, i, next);
        const double               at_min = yToPixel(view, s.min);
        const double               at_max = yToPixel(view, s.max);
        band.add(px, std::min(at_min, at_max), std::max(at_min, at_max));
        line.add(Point{px, yToPixel(view, s.sum / static_cast<double>(next - i))});
        i = next;
    }
}

}  // namespace

SeriesIndex::SeriesIndex(std::span<const double> ys) : size_(ys.size())
{
    constexpr std::size_t kBlock = ChartConstants::kIndexBlockSamples;
    constexpr double      kInf   = std::numeric_limits<double>::infinity();
    std::vector<Node> level((size_ + kBlock - 1) / kBlock, Node{kInf, -kInf, 0.0});
    for (std::size_t i = 0; i < size_; ++i)
    {
        const double v = ys[i];
        if (!std::isfinite(v))
        {
            nonFinite_.push_back(i);
            continue;
        }
        Node& node = level[i / kBlock];
        node.min   = std::min(node.min, v);
        node.max   = std::max(node.max, v);
        node.sum  += v;
    }
    while (!level.empty())
    {
        const bool top = level.size() == 1;
        std::vector<Node> above((level.size() + 1) / 2);
        for (std::size_t k = 0; k < above.size(); ++k)
        {
            const Node& a = level[2 * k];
            above[k]      = a;
            if (2 * k + 1 < level.size())
            {
                const Node& b = level[2 * k + 1];
                above[k]      = Node{std::min(a.min, b.min), std::max(a.max, b.max), a.sum + b.sum};
            }
        }
        levels_.push_back(std::move(level));
        if (top)
        {
            break;
        }
        level = std::move(above);
    }
}

SeriesIndex::Summary SeriesIndex::summarize(std::span<const double> ys, std::size_t first, std::size_t last) const
{
    constexpr std::size_t kBlock = ChartConstants::kIndexBlockSamples;
    constexpr double      kInf   = std::numeric_limits<double>::infinity();
    Summary s{kInf, -kInf, 0.0};
    auto take = [&](double mn, double mx, double sum) {
        s.min  = std::min(s.min, mn);
        s.max  = std::max(s.max, mx);
        s.sum += sum;
    };

    // The samples before the first whole block; then, if the run reaches past it, the whole blocks, from the levels,
    // and the samples after the last whole block.
    const std::size_t head_end = std::min(last, (first + kBlock - 1) / kBlock * kBlock);
    for (std::size_t k = first; k < head_end; ++k)
    {
        take(ys[k], ys[k], ys[k]);
    }
    if (head_end == last)
    {
        return s;
    }
    const std::size_t tail_begin = last / kBlock * kBlock;   // at or after head_end, which is on a block boundary
    std::size_t lo = head_end / kBlock;
    std::size_t hi = tail_begin / kBlock;
    for (std::size_t level = 0; lo < hi; ++level)
    {
        const std::vector<Node>& nodes = levels_[level];
        if (lo & 1U)
        {
            const Node& n = nodes[lo++];
            take(n.min, n.max, n.sum);
        }
        if (hi & 1U)
        {
            const Node& n = nodes[--hi];
            take(n.min, n.max, n.sum);
        }
        lo >>= 1U;
        hi >>= 1U;
    }
    for (std::size_t k = tail_begin; k < last; ++k)
    {
        take(ys[k], ys[k], ys[k]);
    }
    return s;
}

std::size_t SeriesIndex::nextNonFinite(std::size_t from) const
{
    const auto it = std::lower_bound(nonFinite_.begin(), nonFinite_.end(), from);
    return it == nonFinite_.end() ? size_ : *it;
}

SeriesGeometry buildSeriesGeometry(std::span<const double> xs, std::span<const double> ys, bool sortedX,
                                   const View& view, const SeriesIndex* index)
{
    SeriesGeometry out;
    const std::size_t n = std::min(xs.size(), ys.size());
    xs = xs.first(n);
    ys = ys.first(n);

    // Only the samples in view can draw anything, so when the times are ordered the rest are skipped outright rather
    // than transformed and clipped. One sample is kept on each side so the line still enters and leaves the view along
    // the segment the data actually describes. Zoomed in, this is the difference between paying for the whole
    // recording and paying for what is on screen.
    int begin = 0;
    int end   = static_cast<int>(n);
    if (sortedX && n > 0)
    {
        const auto lo_it = std::lower_bound(xs.begin(), xs.end(), view.xLower);
        const auto hi_it = std::upper_bound(xs.begin(), xs.end(), view.xUpper);
        begin = std::max(0, static_cast<int>(lo_it - xs.begin()) - 1);
        end   = std::min(static_cast<int>(n), static_cast<int>(hi_it - xs.begin()) + 1);
    }

    // A plot only has as many pixel columns as it is wide, so a series with far more samples than that cannot show
    // them individually. Above kMaxSamplesPerPixelColumn samples per column the column is summarised instead: a BAND
    // from its minimum to its maximum, and a line through its MEAN.
    //
    // The band is what keeps this honest. A one-sample dropout is its column's minimum by definition, so the band
    // always reaches it - and on this plot that lone sample is the event an operator is hunting for. The mean is what
    // makes it readable: the outline alone drew dense noisy data as a solid blob with no indication of where the
    // signal actually sat inside it.
    //
    // The samples themselves are untouched: this is a drawing decision only.
    const int  columns  = static_cast<int>(view.width);
    const bool decimate = columns > 0 && (end - begin) > columns * ChartConstants::kMaxSamplesPerPixelColumn;
    out.decimated = decimate;

    LineBuilder line{out.segments};

    if (!decimate)
    {
        out.segments.reserve(static_cast<std::size_t>(std::max(0, end - begin - 1)));
        for (int i = begin; i < end; ++i)
        {
            const double vx = xs[static_cast<std::size_t>(i)];
            const double vy = ys[static_cast<std::size_t>(i)];
            if (!std::isfinite(vx) || !std::isfinite(vy))
            {
                line.breakLine();   // the gap breaks the line
                continue;
            }
            line.add(Point{xToPixel(view, vx), yToPixel(view, vy)});
        }
        return out;
    }

    out.segments.reserve(static_cast<std::size_t>(columns));
    out.bands.reserve(1);
    BandBuilder band{out.bands};
    // Zoomed out, walking is what a frame paid for: every sample in view, every frame. The index answers the same
    // question in time that depends on the columns, not the samples.
    if (sortedX && index && index->size() == n)
    {
        lookUpEnvelope(xs, ys, static_cast<std::size_t>(begin), static_cast<std::size_t>(end), view, *index, line,
                       band);
    }
    else
    {
        walkEnvelope(xs, ys, begin, end, view, line, band);
    }
    band.close();
    return out;
}

std::vector<double> tickValues(double lower, double upper, int count)
{
    std::vector<double> ticks;
    if (!usableRange(lower, upper))
    {
        return ticks;
    }
    const int n = std::max(2, count);
    ticks.reserve(static_cast<std::size_t>(n));
    const double step = (upper - lower) / (n - 1);
    for (int i = 0; i < n; ++i)
    {
        ticks.push_back(lower + i * step);
    }
    return ticks;
}

std::vector<double> timeTickValues(double lower, double upper, int count, double origin)
{
    std::vector<double> ticks;
    if (!usableRange(lower, upper))
    {
        return ticks;
    }

    // The X axis is read as DDD:HH:MM:SS, so a tick every 17 s is noise where a tick every 15 s is a landmark.
    // Dividing the span evenly put labels on arbitrary instants: a 116-second view produced ticks 29 seconds apart,
    // and nothing on screen fell on a round time.
    static constexpr double kSteps[] = {
        1, 2, 5, 10, 15, 30,                         // seconds
        60, 120, 300, 600, 900, 1800,                // 1, 2, 5, 10, 15, 30 minutes
        3600, 7200, 10800, 21600, 43200,             // 1, 2, 3, 6, 12 hours
        86400                                        // 1 day
    };

    const int    n   = std::max(2, count);
    const double raw = (upper - lower) / (n - 1);

    // Below a second there is no natural step to snap to - the format has no field finer than seconds - so an evenly
    // divided span is the honest answer.
    if (raw < 1.0)
    {
        return tickValues(lower, upper, count);
    }

    double step = 0.0;
    for (double s : kSteps)
    {
        if (s >= raw)
        {
            step = s;
            break;
        }
    }
    if (step == 0.0)
    {
        // Longer than a day per tick: whole days, which stay round however many the span needs. Without this the
        // ladder's top entry would be reused and a multi-week recording would grow ticks without bound.
        step = std::ceil(raw / 86400.0) * 86400.0;
    }

    // Start at the first round time at or after the range, not at the range itself - and round in the domain the
    // LABEL is read in, which is the clock, not the elapsed value. An X value is seconds since the recording's first
    // sample, while its label is a time of day; snapping the X value puts ticks a round interval apart but starting
    // from whenever the recording happened to begin (a file starting at 10:30:17.046 produced ticks reading :17, :47).
    const double first = std::ceil((lower + origin) / step) * step;
    for (double clock = first; clock <= upper + origin + step * 1e-9; clock += step)
    {
        ticks.push_back(clock - origin);
    }
    return ticks;
}

}  // namespace chart
