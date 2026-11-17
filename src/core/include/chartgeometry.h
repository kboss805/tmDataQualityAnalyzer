/**
 * @file chartgeometry.h
 * @brief Geometry of the plot: where a series' samples land in pixels, the line drawn through them, the band
 * that keeps a dense series honest, and where the ticks go.
 *
 * In the native core (plan step 4.3a, decision D1: the core builds the geometry and the application only strokes
 * it), so every chart that draws a series draws it from one implementation:
 *
 *  - the transforms between data values and the plot area's pixels;
 *  - a series' line, built from the samples in view only (with one on each side, so the line still enters and
 *    leaves along the segment the data describes), sample by sample while there are few enough per pixel column;
 *  - above that, its per-pixel-column envelope: a line through each column's MEAN and a band from its minimum to
 *    its maximum, so a one-sample dropout still reaches the edge of the band. With a SeriesIndex the envelope is
 *    looked up rather than walked: the same columns and the same band, the mean within rounding of the walked one;
 *  - the X ticks, evenly spaced for a plain axis and snapped to round clock times for a time axis.
 *
 * Every value is a double in the coordinates the caller paints in; nothing here draws, holds a series or knows a
 * colour. A non-finite sample breaks the line (and the band) where it sits.
 */

#ifndef CHARTGEOMETRY_H
#define CHARTGEOMETRY_H

#include <cstddef>
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

/// A series' values summarised once, so that its per-pixel-column envelope can be looked up instead of walked.
///
/// Zoomed out, every frame of a pan or a zoom used to visit every sample in view to find each column's minimum,
/// maximum and mean: 21 million samples, 280 ms, for 52 one-hour series at 10 ms. The index is a pyramid over the
/// values: the minimum, maximum and sum of each block of ChartConstants::kIndexBlockSamples samples, then of each
/// pair of blocks, and so on up. A run of samples is the few samples at its ends that do not fill a block, plus a
/// handful of blocks from the levels: about 2 x 32 samples and 2 x log2(run / 32) blocks, whatever the run's length.
///
/// It holds no samples, only what was summarised from them, so it is handed the values again with each question
/// (the caller's arrays may move between calls). It depends on the values alone, not on the times: a series whose
/// times shift keeps its index. Non-finite values are left out of every summary and listed, so that a run is never
/// asked across one.
class SeriesIndex
{
public:
    /// The least, the greatest and the sum of a run of values.
    struct Summary
    {
        double min;
        double max;
        double sum;
    };

    explicit SeriesIndex(std::span<const double> ys);

    /// How many values it was built over.
    std::size_t size() const { return size_; }

    /// The summary of @p ys[@p first, @p last), @p ys being the values the index was built over and every one in the
    /// run finite (nextNonFinite says where the first that is not lies). Requires @p first < @p last <= size().
    Summary summarize(std::span<const double> ys, std::size_t first, std::size_t last) const;

    /// The first value at or after @p from that is not finite; size() when there is none.
    std::size_t nextNonFinite(std::size_t from) const;

private:
    struct Node
    {
        double min;
        double max;
        double sum;
    };

    std::size_t size_ = 0;
    std::vector<std::vector<Node>> levels_;   ///< levels_[0]: one node a block; each level above, one a pair below.
    std::vector<std::size_t> nonFinite_;      ///< The non-finite values' indices, in order.
};

/// The line and band of the series (@p xs, @p ys) in @p view. Only the paired prefix of the two is used. With
/// @p sortedX (see isSortedX) only the samples in view, and one on each side, are walked. Above
/// ChartConstants::kMaxSamplesPerPixelColumn samples per pixel column the series is drawn from its envelope; with
/// @p sortedX and an @p index built over the paired values, the envelope is looked up in it rather than walked. An
/// index of another size is not used.
SeriesGeometry buildSeriesGeometry(std::span<const double> xs, std::span<const double> ys, bool sortedX,
                                   const View& view, const SeriesIndex* index = nullptr);

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
