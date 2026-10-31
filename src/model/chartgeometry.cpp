/**
 * @file chartgeometry.cpp
 * @brief Implementation of the plot's geometry - transcribed from TmChart, expression for expression, so the Qt chart
 * draws exactly the pixels it drew before it called this.
 */

#include "chartgeometry.h"

#include <algorithm>
#include <climits>
#include <cmath>

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

SeriesGeometry buildSeriesGeometry(std::span<const double> xs, std::span<const double> ys, bool sortedX,
                                   const View& view)
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

    auto pixelFor = [&](double vx, double vy) { return Point{xToPixel(view, vx), yToPixel(view, vy)}; };

    std::vector<Segment>& segments = out.segments;
    Point prev;
    bool  have_prev = false;

    auto addPoint = [&](const Point& pt) {
        if (have_prev)
        {
            segments.push_back(Segment{prev, pt});
        }
        prev      = pt;
        have_prev = true;
    };

    if (!decimate)
    {
        segments.reserve(static_cast<std::size_t>(std::max(0, end - begin - 1)));
        for (int i = begin; i < end; ++i)
        {
            const double vx = xs[static_cast<std::size_t>(i)];
            const double vy = ys[static_cast<std::size_t>(i)];
            if (!std::isfinite(vx) || !std::isfinite(vy))
            {
                have_prev = false;   // the gap breaks the line
                continue;
            }
            addPoint(pixelFor(vx, vy));
        }
        return out;
    }

    segments.reserve(static_cast<std::size_t>(columns));
    out.bands.reserve(1);

    int    column = INT_MIN;   ///< Pixel column being accumulated.
    double col_x  = 0.0;       ///< Its x in pixels.
    double lo     = 0.0;       ///< Lowest y (in pixels) in the column.
    double hi     = 0.0;       ///< Highest y (in pixels) in the column.
    double sum    = 0.0;       ///< Sum of the column's y pixels, for the mean.
    int    count  = 0;

    // The band is built as two edges: tops left-to-right, bottoms right-to-left, so closing the polygon traces the
    // outline of the run.
    std::vector<Point> tops;
    std::vector<Point> bottoms;

    auto closeBand = [&]() {
        if (tops.size() >= 2)
        {
            std::vector<Point> poly;
            poly.reserve(tops.size() + bottoms.size());
            poly.insert(poly.end(), tops.begin(), tops.end());
            poly.insert(poly.end(), bottoms.rbegin(), bottoms.rend());
            out.bands.push_back(std::move(poly));
        }
        tops.clear();
        bottoms.clear();
    };

    auto flushColumn = [&]() {
        if (column == INT_MIN)
        {
            return;
        }
        tops.push_back(Point{col_x, lo});
        bottoms.push_back(Point{col_x, hi});
        addPoint(Point{col_x, sum / count});
        column = INT_MIN;
    };

    for (int i = begin; i < end; ++i)
    {
        const double vx = xs[static_cast<std::size_t>(i)];
        const double vy = ys[static_cast<std::size_t>(i)];
        if (!std::isfinite(vx) || !std::isfinite(vy))
        {
            flushColumn();
            closeBand();
            have_prev = false;   // the gap breaks both the line and the band
            continue;
        }

        const Point pt  = pixelFor(vx, vy);
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
    closeBand();
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
