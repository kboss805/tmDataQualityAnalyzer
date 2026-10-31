/**
 * @file chartgeometry.h
 * @brief Qt-free geometry of the plot: where a series' samples land in pixels, the line drawn through them, the band
 * that keeps a dense series honest, and where the ticks go.
 *
 * Moved out of the Qt chart, `TmChart` (plan step 4.3a, decision D1: the native core builds the geometry and each
 * application only strokes it), so the Qt chart and the Avalonia one draw from one implementation:
 *
 *  - the transforms between data values and the plot area's pixels;
 *  - a series' line, built from the samples in view only (with one on each side, so the line still enters and
 *    leaves along the segment the data describes), sample by sample while there are few enough per pixel column;
 *  - above that, its per-pixel-column envelope: a line through each column's MEAN and a band from its minimum to
 *    its maximum, so a one-sample dropout still reaches the edge of the band;
 *  - the X ticks, evenly spaced for a plain axis and snapped to round clock times for a time axis.
 *
 * Every value is a double in the coordinates the caller paints in; nothing here draws, holds a series or knows a
 * colour. A non-finite sample breaks the line (and the band) where it sits.
 */

#ifndef CHARTGEOMETRY_H
#define CHARTGEOMETRY_H

#include <span>
#include <vector>

namespace chart
{

/// What a series is drawn against: the X range shown, the range of the Y axis the series is measured on, and the
/// plot area in pixels (screen Y grows downward, so the plot area's bottom is `top + height`).
struct View
{
    double xLower = 0.0;
    double xUpper = 1.0;
    double yLower = 0.0;
    double yUpper = 1.0;
    double left   = 0.0;
    double top    = 0.0;
    double width  = 0.0;
    double height = 0.0;
};

struct Point
{
    double x = 0.0;
    double y = 0.0;
};

/// One stroke of a series' line. Laid out as four doubles (x0, y0, x1, y1), which the C ABI exposes as they are.
struct Segment
{
    Point from;
    Point to;
};

/// What to draw for one series, in pixels.
struct SeriesGeometry
{
    /// The line: every sample when the series is sparse enough to draw one by one, otherwise each pixel column's
    /// mean. A gap (a non-finite sample) has no segment across it.
    std::vector<Segment> segments;
    /// One closed outline per unbroken run of columns: the columns' minima left to right, then their maxima right to
    /// left. Empty unless the series is drawn from its envelope; a run of one column has no band.
    std::vector<std::vector<Point>> bands;
    /// True when the series was drawn from its envelope.
    bool decimated = false;
};

/// True when [@p lower, @p upper] is a usable, finite span. Every transform is guarded by it: a degenerate range
/// would divide by zero and a non-finite one would poison every coordinate derived from it.
bool usableRange(double lower, double upper);

/// @p x as a pixel column of the plot area; the area's left edge when the X range is not usable.
double xToPixel(const View& view, double x);
/// The X value at pixel column @p px; the X range's lower end when the plot area has no width.
double pixelToX(const View& view, double px);
/// @p y as a pixel row of the plot area, larger values higher up; the area's bottom when the Y range is not usable.
double yToPixel(const View& view, double y);
/// The Y value at pixel row @p py; the Y range's lower end when the plot area has no height.
double pixelToY(const View& view, double py);

/// True when every sample after the first is finite and none is smaller than the one before. Such a series lets the
/// geometry skip straight to the samples in view by binary search; computed once per series rather than assumed.
bool isSortedX(std::span<const double> xs);

/// The line and band of the series (@p xs, @p ys) in @p view. Only the paired prefix of the two is used. With
/// @p sortedX (see isSortedX) only the samples in view, and one on each side, are walked. Above
/// ChartConstants::kMaxSamplesPerPixelColumn samples per pixel column the series is drawn from its envelope.
SeriesGeometry buildSeriesGeometry(std::span<const double> xs, std::span<const double> ys, bool sortedX,
                                   const View& view);

/// @p count (at least 2) evenly spaced values from @p lower to @p upper, both ends included; none for a range that
/// is not usable.
std::vector<double> tickValues(double lower, double upper, int count);

/// Ticks for a time axis read as DDD:HH:MM:SS: a round step (1 s up to a day, then whole days) at least the span
/// divided evenly into @p count, on the round clock times inside [@p lower, @p upper]. @p origin is the clock time
/// of x = 0; snapping happens on the clock and the values returned are X values again. Below a second per tick the
/// span is divided evenly (tickValues). None for a range that is not usable.
std::vector<double> timeTickValues(double lower, double upper, int count, double origin = 0.0);

}  // namespace chart

#endif // CHARTGEOMETRY_H
