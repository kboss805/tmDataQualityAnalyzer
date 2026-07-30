#include "tmchart.h"

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include "constants.h"

namespace
{
/// Gap between the plot area and the widget edge on a side with no axis text.
constexpr int kOuterMargin   = 10;
/// Extra room past the tick labels for an axis title.
constexpr int kAxisLabelGap  = 6;
/// Tick mark length, in pixels, drawn outward from the axis line.
constexpr int kTickLength    = 4;
/// Wheel notch -> zoom factor. Matches the feel of the previous implementation:
/// one notch changes the visible span by ~15%.
constexpr double kWheelZoomStep = 0.85;

/// @return True when @p lower / @p upper describe a usable, finite span. Guards
/// every transform: a degenerate range would divide by zero and a non-finite one
/// would poison every coordinate derived from it.
bool usableRange(double lower, double upper)
{
    return std::isfinite(lower) && std::isfinite(upper) && (upper - lower) > 0.0;
}
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
        update();
    }
}

void TmChart::clearSeries()
{
    m_series.clear();
    update();
}

void TmChart::setSeriesData(int index, const QVector<double>& xs, const QVector<double>& ys)
{
    if (!indexValid(index))
    {
        return;
    }
    // Mismatched lengths would read past the end of the shorter vector while
    // drawing; keep only the paired prefix.
    const int n = std::min(xs.size(), ys.size());
    m_series[index].xs = xs.mid(0, n);
    m_series[index].ys = ys.mid(0, n);
    update();
}

void TmChart::setSeriesPen(int index, const QPen& pen)
{
    if (indexValid(index))
    {
        m_series[index].pen = pen;
        update();
    }
}

void TmChart::setSeriesVisible(int index, bool visible)
{
    if (indexValid(index))
    {
        m_series[index].visible = visible;
        update();
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

void TmChart::setXRange(double lower, double upper)
{
    if (!usableRange(lower, upper))
    {
        return;
    }
    m_x_lower = lower;
    m_x_upper = upper;
    update();
}

void TmChart::setLeftRange(double lower, double upper)
{
    if (!usableRange(lower, upper))
    {
        return;
    }
    m_left_lower = lower;
    m_left_upper = upper;
    update();
}

void TmChart::setRightRange(double lower, double upper)
{
    if (!usableRange(lower, upper))
    {
        return;
    }
    m_right_lower = lower;
    m_right_upper = upper;
    update();
}

// ---------------------------------------------------------------- chrome ----

void TmChart::setTitle(const QString& title)
{
    m_title = title;
    recalcPlotArea();   // a title appearing/vanishing changes the top margin
    update();
}

void TmChart::setXLabel(const QString& label)
{
    m_x_label = label;
    recalcPlotArea();
    update();
}

void TmChart::setLeftLabel(const QString& label)
{
    m_left_label = label;
    recalcPlotArea();
    update();
}

void TmChart::setRightLabel(const QString& label)
{
    m_right_label = label;
    recalcPlotArea();
    update();
}

void TmChart::setRightAxisVisible(bool visible)
{
    if (m_right_visible == visible)
    {
        return;
    }
    m_right_visible = visible;
    recalcPlotArea();   // reclaims the right margin when hidden
    update();
}

void TmChart::setXTickCount(int count)
{
    m_x_tick_count = std::max(2, count);
    update();
}

void TmChart::setTimeFormatter(std::function<QString(double)> formatter)
{
    m_formatter = std::move(formatter);
    update();
}

void TmChart::setThemeColors(const QColor& background, const QColor& foreground,
                             const QColor& grid, const QColor& title)
{
    m_background  = background;
    m_foreground  = foreground;
    m_grid        = grid;
    m_title_color = title;
    update();
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
    update();
}

void TmChart::setZoomBand(double x0, double x1, bool visible)
{
    m_band_x0      = x0;
    m_band_x1      = x1;
    m_band_visible = visible;
    update();
}

// ------------------------------------------------------------- geometry -----

double TmChart::pixelToX(double px) const
{
    ensureLayout();
    if (m_plot_area.width() <= 0)
    {
        return m_x_lower;
    }
    const double frac = (px - m_plot_area.left()) / static_cast<double>(m_plot_area.width());
    return m_x_lower + frac * (m_x_upper - m_x_lower);
}

double TmChart::xToPixel(double x) const
{
    ensureLayout();
    if (!usableRange(m_x_lower, m_x_upper))
    {
        return m_plot_area.left();
    }
    const double frac = (x - m_x_lower) / (m_x_upper - m_x_lower);
    return m_plot_area.left() + frac * m_plot_area.width();
}

double TmChart::pixelToLeft(double py) const
{
    ensureLayout();
    if (m_plot_area.height() <= 0)
    {
        return m_left_lower;
    }
    // Screen Y grows downward, data Y upward.
    const double frac = (m_plot_area.bottom() - py) / static_cast<double>(m_plot_area.height());
    return m_left_lower + frac * (m_left_upper - m_left_lower);
}

double TmChart::leftToPixel(double value) const
{
    ensureLayout();
    if (!usableRange(m_left_lower, m_left_upper))
    {
        return m_plot_area.bottom();
    }
    const double frac = (value - m_left_lower) / (m_left_upper - m_left_lower);
    return m_plot_area.bottom() - frac * m_plot_area.height();
}

double TmChart::rightToPixel(double value) const
{
    ensureLayout();
    if (!usableRange(m_right_lower, m_right_upper))
    {
        return m_plot_area.bottom();
    }
    const double frac = (value - m_right_lower) / (m_right_upper - m_right_lower);
    return m_plot_area.bottom() - frac * m_plot_area.height();
}

void TmChart::recalcPlotArea() const
{
    const QFontMetrics fm(font());
    const int text_h = fm.height();

    // Left margin: widest left-axis tick label, plus the rotated axis title.
    int left = kOuterMargin + fm.horizontalAdvance(QStringLiteral("-000.0")) + kTickLength;
    if (!m_left_label.isEmpty())
    {
        left += text_h + kAxisLabelGap;
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

    int top = kOuterMargin;
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

    m_plot_area = QRectF(left, top,
                         std::max(1, width()  - left - right),
                         std::max(1, height() - top  - bottom));
    m_layout_size = size();
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
    QPainter painter(this);
    render(painter, rect(), /*with_overlays=*/true);
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
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect, m_background);

    drawTitle(painter, rect);
    drawGrid(painter);
    drawSeries(painter);
    drawAxes(painter);

    if (with_overlays)
    {
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
    }
    painter.restore();
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

    for (double t : tickValues(m_x_lower, m_x_upper, m_x_tick_count))
    {
        const double px = xToPixel(t);
        painter.drawLine(QPointF(px, m_plot_area.top()), QPointF(px, m_plot_area.bottom()));
    }
    for (double t : tickValues(m_left_lower, m_left_upper, PlotConstants::kYTickCount))
    {
        const double py = leftToPixel(t);
        painter.drawLine(QPointF(m_plot_area.left(), py), QPointF(m_plot_area.right(), py));
    }
    painter.restore();
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

        QPainterPath path;
        bool started = false;
        for (int i = 0; i < s.xs.size(); ++i)
        {
            const double vx = s.xs[i];
            const double vy = s.ys[i];
            if (!std::isfinite(vx) || !std::isfinite(vy))
            {
                // A gap in the data must break the line rather than draw a
                // straight segment across it, which would read as real data.
                started = false;
                continue;
            }
            const QPointF pt(xToPixel(vx),
                             s.axis == Axis::Left ? leftToPixel(vy) : rightToPixel(vy));
            if (!started)
            {
                path.moveTo(pt);
                started = true;
            }
            else
            {
                path.lineTo(pt);
            }
        }
        painter.drawPath(path);
    }
    painter.restore();
}

void TmChart::drawAxes(QPainter& painter) const
{
    painter.save();
    painter.setPen(QPen(m_foreground));
    const QFontMetrics fm(font());
    const int text_h = fm.height();

    // Axis lines: left, bottom, and right only when that axis is in use.
    painter.drawLine(m_plot_area.bottomLeft(), m_plot_area.topLeft());
    painter.drawLine(m_plot_area.bottomLeft(), m_plot_area.bottomRight());
    if (m_right_visible)
    {
        painter.drawLine(m_plot_area.bottomRight(), m_plot_area.topRight());
    }

    // --- X ticks and labels ---
    for (double t : tickValues(m_x_lower, m_x_upper, m_x_tick_count))
    {
        const double px = xToPixel(t);
        painter.drawLine(QPointF(px, m_plot_area.bottom()),
                         QPointF(px, m_plot_area.bottom() + kTickLength));
        const QString label = formatX(t);
        const int w = fm.horizontalAdvance(label) + 8;
        painter.drawText(QRectF(px - w / 2.0, m_plot_area.bottom() + kTickLength, w, text_h),
                         Qt::AlignHCenter | Qt::AlignTop, label);
    }

    // --- Left ticks and labels ---
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
    if (!m_left_label.isEmpty())
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

QVector<double> TmChart::tickValues(double lower, double upper, int count)
{
    QVector<double> ticks;
    if (!usableRange(lower, upper))
    {
        return ticks;
    }
    const int n = std::max(2, count);
    ticks.reserve(n);
    const double step = (upper - lower) / (n - 1);
    for (int i = 0; i < n; ++i)
    {
        ticks.append(lower + i * step);
    }
    return ticks;
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
