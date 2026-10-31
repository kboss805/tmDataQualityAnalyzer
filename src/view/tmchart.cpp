#include "tmchart.h"


#include <algorithm>
#include <cmath>
#include <span>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QLineF>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include "chartgeometry.h"
#include "constants.h"
#include "plotconstants.h"

namespace
{
/// Gap between the plot area and the widget edge on a side with no axis text.
constexpr int kOuterMargin   = 10;
/// Extra room past the tick labels for an axis title.
constexpr int kAxisLabelGap  = 6;
/// Tick mark length, in pixels, drawn outward from the axis line.
constexpr int kTickLength    = 4;
/// Minimum clear space between adjacent X tick labels before one is dropped.
constexpr int kTickLabelGapPx = 12;
/// Wheel notch -> zoom factor. Matches the feel of the previous implementation:
/// one notch changes the visible span by ~15%.
constexpr double kWheelZoomStep = 0.85;

/// Every range setter is guarded by the core's check (chart::usableRange): a degenerate range would divide by zero and
/// a non-finite one would poison every coordinate derived from it.
using chart::usableRange;
}  // namespace

TmChart::TmChart(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
    setMouseTracking(true);   // the crosshair follows the cursor with no button held
}

// ---------------------------------------------------------------- series ----

int TmChart::addSeries(Axis axis)
{
    Series s;
    s.axis = axis;
    m_series.append(s);
    return m_series.size() - 1;
}

void TmChart::removeSeries(int index)
{
    if (indexValid(index))
    {
        m_series.remove(index);
        updateData();
    }
}

void TmChart::clearSeries()
{
    m_series.clear();
    updateData();
}

void TmChart::setSeriesData(int index, const QVector<double>& xs, const QVector<double>& ys)
{
    if (!indexValid(index))
    {
        return;
    }
    if (xs.size() == ys.size())
    {
        // Equal lengths is the normal case, and QVector is implicitly shared, so
        // assigning is a refcount bump rather than copying the samples. That keeps
        // a full rebuild cheap even with 48 series over a long recording.
        m_series[index].xs = xs;
        m_series[index].ys = ys;
    }
    else
    {
        // Mismatched lengths would read past the end of the shorter vector while
        // drawing; keep only the paired prefix.
        const int n = std::min(xs.size(), ys.size());
        m_series[index].xs = xs.mid(0, n);
        m_series[index].ys = ys.mid(0, n);
    }
    Series& s = m_series[index];
    s.segmentsValid = false;
    // Sorted once here rather than per draw: the draw only needs the answer, and
    // the data is already being copied at this point.
    s.sortedX = chart::isSortedX(std::span<const double>(s.xs.constData(), static_cast<std::size_t>(s.xs.size())));

    updateData();
}

void TmChart::setSeriesPen(int index, const QPen& pen)
{
    if (indexValid(index))
    {
        m_series[index].pen = pen;
        updateData();
    }
}

void TmChart::setSeriesVisible(int index, bool visible)
{
    if (indexValid(index))
    {
        m_series[index].visible = visible;
        updateData();
    }
}

void TmChart::setSeriesName(int index, const QString& name)
{
    if (indexValid(index))
    {
        m_series[index].name = name;
    }
}

bool TmChart::seriesVisible(int index) const
{
    return indexValid(index) ? m_series[index].visible : false;
}

QPen TmChart::seriesPen(int index) const
{
    return indexValid(index) ? m_series[index].pen : QPen();
}

QString TmChart::seriesName(int index) const
{
    return indexValid(index) ? m_series[index].name : QString();
}

// ---------------------------------------------------------------- ranges ----

void TmChart::invalidateSeriesGeometry() const
{
    // The painted pixmap goes with it: anything that makes the segments stale makes
    // the picture drawn from them stale too, and a stale picture still looks like a
    // plausible plot.
    m_backing_valid = false;
    for (const Series& s : m_series)
    {
        s.segmentsValid = false;
        s.cachedSegments.clear();
        s.cachedBands.clear();
    }
}

void TmChart::setXRange(double lower, double upper)
{
    if (!usableRange(lower, upper))
    {
        return;
    }
    m_x_lower = lower;
    m_x_upper = upper;
    invalidateSeriesGeometry();
    updateData();
}

void TmChart::setLeftRange(double lower, double upper)
{
    if (!usableRange(lower, upper))
    {
        return;
    }
    m_left_lower = lower;
    m_left_upper = upper;
    invalidateSeriesGeometry();
    updateData();
}

void TmChart::setRightRange(double lower, double upper)
{
    if (!usableRange(lower, upper))
    {
        return;
    }
    m_right_lower = lower;
    m_right_upper = upper;
    invalidateSeriesGeometry();
    updateData();
}

// ---------------------------------------------------------------- chrome ----

void TmChart::setTitle(const QString& title)
{
    m_title = title;
    recalcPlotArea();   // a title appearing/vanishing changes the top margin
    updateData();
}

void TmChart::setXLabel(const QString& label)
{
    m_x_label = label;
    recalcPlotArea();
    updateData();
}

void TmChart::setLeftLabel(const QString& label)
{
    m_left_label = label;
    recalcPlotArea();
    updateData();
}

void TmChart::setRightLabel(const QString& label)
{
    m_right_label = label;
    recalcPlotArea();
    updateData();
}

void TmChart::setLeftAxisVisible(bool visible)
{
    if (m_left_visible == visible)
    {
        return;
    }
    m_left_visible = visible;
    recalcPlotArea();   // reclaims the left margin when hidden
    updateData();
}

void TmChart::setRightAxisVisible(bool visible)
{
    if (m_right_visible == visible)
    {
        return;
    }
    m_right_visible = visible;
    recalcPlotArea();   // reclaims the right margin when hidden
    updateData();
}

void TmChart::setTopInset(int px)
{
    const int clamped = std::max(0, px);
    if (m_top_inset == clamped)
    {
        return;
    }
    m_top_inset = clamped;
    recalcPlotArea();
    updateData();
}

void TmChart::setXTickCount(int count)
{
    m_x_tick_count = std::max(2, count);
    updateData();
}

void TmChart::setTimeFormatter(std::function<QString(double)> formatter)
{
    m_formatter = std::move(formatter);
    updateData();
}

void TmChart::setTimeOrigin(double seconds)
{
    if (qFuzzyCompare(m_time_origin, seconds))
    {
        return;
    }
    m_time_origin = seconds;
    // Moves where every tick lands, and ticks are grid lines and labels - both in
    // the cached data layer, so the backing store has to go.
    updateData();
}

void TmChart::setThemeColors(const QColor& background, const QColor& foreground,
                             const QColor& grid, const QColor& title)
{
    m_background  = background;
    m_foreground  = foreground;
    m_grid        = grid;
    m_title_color = title;
    updateData();
}

void TmChart::setInteractionsEnabled(bool enabled)
{
    m_interactions = enabled;
    if (!enabled)
    {
        m_panning = false;
    }
}

void TmChart::setCrosshair(double x, bool visible)
{
    m_crosshair_x       = x;
    m_crosshair_visible = visible;
    update();   // overlay only - deliberately keeps the backing store
}

void TmChart::setZoomBand(double x0, double x1, bool visible)
{
    m_band_x0      = x0;
    m_band_x1      = x1;
    m_band_visible = visible;
    update();   // overlay only - deliberately keeps the backing store
}

// ------------------------------------------------------------- geometry -----

chart::View TmChart::viewFor(Axis axis) const
{
    chart::View v;
    v.xLower = m_x_lower;
    v.xUpper = m_x_upper;
    v.yLower = axis == Axis::Left ? m_left_lower : m_right_lower;
    v.yUpper = axis == Axis::Left ? m_left_upper : m_right_upper;
    v.left   = m_plot_area.left();
    v.top    = m_plot_area.top();
    v.width  = m_plot_area.width();
    v.height = m_plot_area.height();
    return v;
}

// The transforms are the core's (chartgeometry.h), so the chart and the geometry it draws cannot disagree.
double TmChart::pixelToX(double px) const
{
    ensureLayout();
    return chart::pixelToX(viewFor(Axis::Left), px);
}

double TmChart::xToPixel(double x) const
{
    ensureLayout();
    return chart::xToPixel(viewFor(Axis::Left), x);
}

double TmChart::pixelToLeft(double py) const
{
    ensureLayout();
    return chart::pixelToY(viewFor(Axis::Left), py);
}

double TmChart::leftToPixel(double value) const
{
    ensureLayout();
    return chart::yToPixel(viewFor(Axis::Left), value);
}

double TmChart::rightToPixel(double value) const
{
    ensureLayout();
    return chart::yToPixel(viewFor(Axis::Right), value);
}

void TmChart::recalcPlotArea() const
{
    const QFontMetrics fm(font());
    const int text_h = fm.height();

    // Left margin: widest left-axis tick label, plus the rotated axis title. A
    // hidden axis reserves nothing, so the chart reclaims the space.
    int left = kOuterMargin;
    if (m_left_visible)
    {
        left += fm.horizontalAdvance(QStringLiteral("-000.0")) + kTickLength;
        if (!m_left_label.isEmpty())
        {
            left += text_h + kAxisLabelGap;
        }
    }

    int right = kOuterMargin;
    if (m_right_visible)
    {
        right += fm.horizontalAdvance(QStringLiteral("-000.0")) + kTickLength;
        if (!m_right_label.isEmpty())
        {
            right += text_h + kAxisLabelGap;
        }
    }

    int top = kOuterMargin + m_top_inset;
    if (!m_title.isEmpty())
    {
        top += text_h + kAxisLabelGap;
    }

    // Bottom: tick labels are DDD:HH:MM:SS, so reserve a full text line, plus the
    // axis title beneath it.
    int bottom = kOuterMargin + text_h + kTickLength;
    if (!m_x_label.isEmpty())
    {
        bottom += text_h + kAxisLabelGap;
    }

    const QRectF previous = m_plot_area;
    m_plot_area = QRectF(left, top,
                         std::max(1, width()  - left - right),
                         std::max(1, height() - top  - bottom));
    m_layout_size = size();
    if (m_plot_area != previous)
    {
        invalidateSeriesGeometry();   // the segments are in pixels, so they all move
    }
}

void TmChart::ensureLayout() const
{
    if (m_layout_size != size())
    {
        recalcPlotArea();
    }
}

QRectF TmChart::plotArea() const
{
    ensureLayout();
    return m_plot_area;
}

// --------------------------------------------------------------- painting ---

void TmChart::paintEvent(QPaintEvent* /*event*/)
{
    ensureLayout();
    QPainter painter(this);

    // The data layer is painted once into m_backing and then blitted. The cost of
    // a repaint that changed nothing - a crosshair move, a tooltip, a legend drag -
    // becomes the blit rather than every series' rasterization, which is what the
    // geometry cache alone could not avoid.
    const qreal dpr = devicePixelRatioF();
    const QSize want(qRound(width() * dpr), qRound(height() * dpr));
    if (want.isEmpty())
    {
        return;
    }
    if (!m_backing_valid || m_backing.size() != want)
    {
        m_backing = QPixmap(want);
        m_backing.setDevicePixelRatio(dpr);
        QPainter into(&m_backing);
        renderData(into, rect());
        m_backing_valid = true;
    }
    painter.drawPixmap(0, 0, m_backing);
    renderOverlays(painter);
}

void TmChart::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    recalcPlotArea();
}

void TmChart::renderTo(QPainter& painter, const QSize& size) const
{
    // Export renders at the widget's own geometry, so the exported image matches
    // what is on screen rather than re-laying-out at a different aspect ratio.
    render(painter, QRect(QPoint(0, 0), size), /*with_overlays=*/false);
}

void TmChart::render(QPainter& painter, const QRect& rect, bool with_overlays) const
{
    ensureLayout();
    renderData(painter, rect);
    if (with_overlays)
    {
        renderOverlays(painter);
    }
}

void TmChart::renderData(QPainter& painter, const QRect& rect) const
{
    ensureLayout();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect, m_background);

    drawTitle(painter, rect);
    drawGrid(painter);
    drawSeries(painter);
    drawAxes(painter);
    painter.restore();
}

void TmChart::renderOverlays(QPainter& painter) const
{
    ensureLayout();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (m_band_visible)
    {
        const double p0 = xToPixel(std::min(m_band_x0, m_band_x1));
        const double p1 = xToPixel(std::max(m_band_x0, m_band_x1));
        QColor fill = m_foreground;
        fill.setAlpha(PlotConstants::kZoomBandAlpha);
        painter.fillRect(QRectF(p0, m_plot_area.top(), p1 - p0, m_plot_area.height()), fill);
    }
    if (m_crosshair_visible)
    {
        QColor line = m_foreground;
        line.setAlpha(PlotConstants::kCrosshairAlpha);
        painter.setPen(QPen(line, 0, Qt::DashLine));
        const double px = xToPixel(m_crosshair_x);
        painter.drawLine(QPointF(px, m_plot_area.top()),
                         QPointF(px, m_plot_area.bottom()));
    }
    painter.restore();
}

void TmChart::updateData()
{
    m_backing_valid = false;
    update();
}

void TmChart::drawTitle(QPainter& painter, const QRect& rect) const
{
    if (m_title.isEmpty())
    {
        return;
    }
    painter.save();
    painter.setPen(m_title_color);
    QFont f = font();
    f.setBold(true);
    painter.setFont(f);
    painter.drawText(QRect(rect.left(), kOuterMargin / 2, rect.width(),
                           QFontMetrics(f).height()),
                     Qt::AlignHCenter | Qt::AlignVCenter, m_title);
    painter.restore();
}

void TmChart::drawGrid(QPainter& painter) const
{
    painter.save();
    painter.setPen(QPen(m_grid, 0, Qt::DotLine));

    for (double t : xTickValues())
    {
        const double px = xToPixel(t);
        painter.drawLine(QPointF(px, m_plot_area.top()), QPointF(px, m_plot_area.bottom()));
    }
    // Horizontal lines follow whichever Y axis is drawn - a grid line that lines up
    // with no visible tick reads as a misaligned chart. Left wins when both are up,
    // matching the tick labels the eye lands on first.
    if (m_left_visible)
    {
        for (double t : tickValues(m_left_lower, m_left_upper, PlotConstants::kYTickCount))
        {
            const double py = leftToPixel(t);
            painter.drawLine(QPointF(m_plot_area.left(), py), QPointF(m_plot_area.right(), py));
        }
    }
    else if (m_right_visible)
    {
        for (double t : tickValues(m_right_lower, m_right_upper, PlotConstants::kYTickCount))
        {
            const double py = rightToPixel(t);
            painter.drawLine(QPointF(m_plot_area.left(), py), QPointF(m_plot_area.right(), py));
        }
    }
    painter.restore();
}

void TmChart::rebuildSeriesGeometry(const Series& s) const
{
    // The core builds it (chartgeometry.h): the samples in view only, one by one while there are few enough per
    // pixel column, otherwise each column's mean line and its min-to-max band. The Avalonia chart draws from the same
    // function. Copied into Qt's types here because drawLines() takes them: only drawLines() avoids Qt's stroker for
    // a non-cosmetic pen (twelve channels of an hour at 10 ms measured 1423 ms through drawPath(), 1725 ms through
    // drawPolyline() and 27 ms through drawLines()).
    const chart::SeriesGeometry g = chart::buildSeriesGeometry(
        std::span<const double>(s.xs.constData(), static_cast<std::size_t>(s.xs.size())),
        std::span<const double>(s.ys.constData(), static_cast<std::size_t>(s.ys.size())), s.sortedX, viewFor(s.axis));

    QVector<QLineF> segments;
    segments.reserve(static_cast<int>(g.segments.size()));
    for (const chart::Segment& seg : g.segments)
    {
        segments.push_back(QLineF(seg.from.x, seg.from.y, seg.to.x, seg.to.y));
    }
    QVector<QPolygonF> bands;
    bands.reserve(static_cast<int>(g.bands.size()));
    for (const std::vector<chart::Point>& band : g.bands)
    {
        QPolygonF poly;
        poly.reserve(static_cast<int>(band.size()));
        for (const chart::Point& pt : band)
        {
            poly.push_back(QPointF(pt.x, pt.y));
        }
        bands.push_back(poly);
    }
    s.cachedSegments = segments;
    s.cachedBands    = bands;
}

void TmChart::drawSeries(QPainter& painter) const
{
    painter.save();
    // Clip so a series running outside the visible range cannot paint over the
    // axes or the title.
    painter.setClipRect(m_plot_area);

    for (const Series& s : m_series)
    {
        if (!s.visible || s.xs.size() < 2)
        {
            continue;
        }
        painter.setPen(s.pen);

        if (!s.segmentsValid)
        {
            rebuildSeriesGeometry(s);
            s.segmentsValid = true;
        }

        // The band goes down first so the mean line reads on top of it.
        if (!s.cachedBands.isEmpty())
        {
            QColor fill = s.pen.color();
            fill.setAlpha(PlotConstants::kEnvelopeBandAlpha);
            QColor edge = s.pen.color();
            edge.setAlpha(PlotConstants::kEnvelopeEdgeAlpha);
            // Outlined, not just filled: the polygon's top and bottom edges are the
            // per-column minima and maxima, so stroking them is what keeps a
            // one-sample excursion as sharp as it was before the band existed. A
            // cosmetic pen (width 0) both draws crisply and skips Qt's stroker.
            painter.setPen(QPen(edge, 0));
            painter.setBrush(fill);
            for (const QPolygonF& band : s.cachedBands)
            {
                painter.drawPolygon(band);
            }
            painter.setBrush(Qt::NoBrush);
        }

        painter.setPen(s.pen);
        painter.drawLines(s.cachedSegments);
    }
    painter.restore();
}

void TmChart::drawAxes(QPainter& painter) const
{
    painter.save();
    painter.setPen(QPen(m_foreground));
    const QFontMetrics fm(font());
    const int text_h = fm.height();

    // Axis lines: bottom always; left and right only when that axis is in use.
    if (m_left_visible)
    {
        painter.drawLine(m_plot_area.bottomLeft(), m_plot_area.topLeft());
    }
    painter.drawLine(m_plot_area.bottomLeft(), m_plot_area.bottomRight());
    if (m_right_visible)
    {
        painter.drawLine(m_plot_area.bottomRight(), m_plot_area.topRight());
    }

    // --- X ticks and labels ---
    //
    // Tick marks are always drawn, but labels are decluttered: elapsed-time labels
    // are wide (DDD:HH:MM:SS) and at the default tick count they overlap into an
    // unreadable smear on a normal-width window. Draw a label only when it clears
    // the previous one, and always keep the last tick's label so the range end
    // stays readable.
    const QVector<double> x_ticks = xTickValues();
    double last_label_right = -1e9;
    for (int i = 0; i < x_ticks.size(); ++i)
    {
        const double px = xToPixel(x_ticks[i]);
        painter.drawLine(QPointF(px, m_plot_area.bottom()),
                         QPointF(px, m_plot_area.bottom() + kTickLength));

        const QString label = formatX(x_ticks[i]);
        const double w      = fm.horizontalAdvance(label) + kTickLabelGapPx;
        const double left   = px - w / 2.0;
        const bool is_last  = (i == x_ticks.size() - 1);
        if (left < last_label_right && !is_last)
        {
            continue;
        }
        // Labels at either end are pulled inside the widget rather than overflowing
        // it. The last one may displace the one before it - hence the clearance
        // check above; when there is no room for both, the end of the range wins.
        //
        // The first one matters whenever the left axis is hidden: without that
        // gutter the plot area starts at the outer margin, so a centred label would
        // hang off the left edge and render as a truncated fragment.
        double draw_left = left;
        if (is_last)
        {
            draw_left = std::min(draw_left, m_plot_area.right() - w);
        }
        draw_left = std::max(draw_left, 0.0);
        painter.drawText(QRectF(draw_left, m_plot_area.bottom() + kTickLength, w, text_h),
                         Qt::AlignHCenter | Qt::AlignTop, label);
        last_label_right = draw_left + w;
    }

    // --- Left ticks and labels ---
    if (m_left_visible)
    {
        for (double t : tickValues(m_left_lower, m_left_upper, PlotConstants::kYTickCount))
        {
            const double py = leftToPixel(t);
            painter.drawLine(QPointF(m_plot_area.left() - kTickLength, py),
                             QPointF(m_plot_area.left(), py));
            painter.drawText(QRectF(0, py - text_h / 2.0,
                                    m_plot_area.left() - kTickLength - 2, text_h),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(t, 'f', 1));
        }
    }

    // --- Right ticks and labels ---
    if (m_right_visible)
    {
        for (double t : tickValues(m_right_lower, m_right_upper, PlotConstants::kYTickCount))
        {
            const double py = rightToPixel(t);
            painter.drawLine(QPointF(m_plot_area.right(), py),
                             QPointF(m_plot_area.right() + kTickLength, py));
            painter.drawText(QRectF(m_plot_area.right() + kTickLength + 2, py - text_h / 2.0,
                                    width() - m_plot_area.right() - kTickLength - 2, text_h),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QString::number(t, 'f', 1));
        }
    }

    // --- Axis titles ---
    if (!m_x_label.isEmpty())
    {
        painter.drawText(QRectF(m_plot_area.left(), height() - text_h - kOuterMargin / 2.0,
                                m_plot_area.width(), text_h),
                         Qt::AlignHCenter | Qt::AlignVCenter, m_x_label);
    }
    // Y titles read bottom-to-top alongside their axis, the usual convention.
    if (m_left_visible && !m_left_label.isEmpty())
    {
        painter.save();
        painter.translate(kOuterMargin, m_plot_area.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-m_plot_area.height() / 2.0, -text_h / 2.0,
                                m_plot_area.height(), text_h),
                         Qt::AlignCenter, m_left_label);
        painter.restore();
    }
    if (m_right_visible && !m_right_label.isEmpty())
    {
        painter.save();
        painter.translate(width() - kOuterMargin, m_plot_area.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-m_plot_area.height() / 2.0, -text_h / 2.0,
                                m_plot_area.height(), text_h),
                         Qt::AlignCenter, m_right_label);
        painter.restore();
    }
    painter.restore();
}

QVector<double> TmChart::timeTickValues(double lower, double upper, int count,
                                        double origin)
{
    // The core's (chart::timeTickValues): a round step, on round times of the clock.
    const std::vector<double> ticks = chart::timeTickValues(lower, upper, count, origin);
    return QVector<double>(ticks.begin(), ticks.end());
}

QVector<double> TmChart::xTickValues() const
{
    // The formatter is what makes the axis a TIME axis; without one this is a
    // general chart whose X values are just numbers, and snapping them to 15 s would
    // be meaningless.
    return m_formatter ? timeTickValues(m_x_lower, m_x_upper, m_x_tick_count, m_time_origin)
                       : tickValues(m_x_lower, m_x_upper, m_x_tick_count);
}

QVector<double> TmChart::tickValues(double lower, double upper, int count)
{
    const std::vector<double> ticks = chart::tickValues(lower, upper, count);
    return QVector<double>(ticks.begin(), ticks.end());
}

QString TmChart::formatX(double value) const
{
    return m_formatter ? m_formatter(value) : QString::number(value, 'f', 1);
}

// ------------------------------------------------------------ interaction ---

void TmChart::wheelEvent(QWheelEvent* event)
{
    if (!m_interactions || !usableRange(m_x_lower, m_x_upper))
    {
        QWidget::wheelEvent(event);
        return;
    }
    const int notches = event->angleDelta().y();
    if (notches == 0)
    {
        return;
    }
    // Zoom about the cursor, not the centre, so the point under the pointer stays
    // put - the behaviour a user expects from a map or a plot.
    const double anchor = pixelToX(event->position().x());
    const double factor = (notches > 0) ? kWheelZoomStep : 1.0 / kWheelZoomStep;
    const double lower  = anchor - (anchor - m_x_lower) * factor;
    const double upper  = anchor + (m_x_upper - anchor) * factor;
    if (upper - lower < PlotConstants::kMinBandZoomSpanSec)
    {
        return;
    }
    setXRange(lower, upper);
    emit xRangeChangedByUser(lower, upper);
    event->accept();
}

void TmChart::mousePressEvent(QMouseEvent* event)
{
    emit mousePressed(event);
    // Plain left-drag pans; Ctrl+left and middle are the band-zoom gesture, which
    // PlotWidget drives via the mousePressed signal, so they must not also pan.
    if (m_interactions && event->button() == Qt::LeftButton
        && !(event->modifiers() & Qt::ControlModifier))
    {
        m_panning      = true;
        m_pan_anchor_x = pixelToX(event->position().x());
    }
    QWidget::mousePressEvent(event);
}

void TmChart::mouseMoveEvent(QMouseEvent* event)
{
    emit mouseMoved(event);
    if (m_panning && usableRange(m_x_lower, m_x_upper))
    {
        // Keep the data point grabbed at press under the cursor: shift the range
        // by the difference between where it was grabbed and where it is now.
        const double under_cursor = pixelToX(event->position().x());
        const double shift        = m_pan_anchor_x - under_cursor;
        if (shift != 0.0)
        {
            const double lower = m_x_lower + shift;
            const double upper = m_x_upper + shift;
            setXRange(lower, upper);
            emit xRangeChangedByUser(lower, upper);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void TmChart::mouseReleaseEvent(QMouseEvent* event)
{
    m_panning = false;
    emit mouseReleased(event);
    QWidget::mouseReleaseEvent(event);
}

void TmChart::mouseDoubleClickEvent(QMouseEvent* event)
{
    emit mouseDoubleClicked(event);
    QWidget::mouseDoubleClickEvent(event);
}
