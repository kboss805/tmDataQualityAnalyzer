/**
 * @file plotlegendoverlay.cpp
 * @brief Implementation of the movable legend overlay (US4.0).
 */

#include "plotlegendoverlay.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

#include "constants.h"

PlotLegendOverlay::PlotLegendOverlay(QWidget* chart)
    : QFrame(chart)
    , m_chart(chart)
{
    setObjectName("legendOverlay");
    setCursor(Qt::OpenHandCursor);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(PlotConstants::kLegendContentMargin,
                              PlotConstants::kLegendContentMargin,
                              PlotConstants::kLegendContentMargin,
                              PlotConstants::kLegendContentMargin);
    outer->setSpacing(0);

    m_content = new QWidget;
    m_content->setObjectName("legendContent");
    m_rows = new QVBoxLayout(m_content);
    // Reserve a permanent right-side gutter matching the style's scrollbar width so
    // the vertical scrollbar - shown only once content overflows the height cap -
    // never sits on top of the last few characters of a row's label. Always-on
    // rather than conditional: whether a row's text runs into that gutter isn't
    // knowable until the scrollbar decision is made, so a fixed reservation is the
    // only way to guarantee no overlap regardless of content length.
    m_rows->setContentsMargins(0, 0, rowGutter(), 0);
    m_rows->setSpacing(PlotConstants::kLegendRowSpacing);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("legendScroll");
    m_scroll->setWidget(m_content);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->viewport()->setCursor(Qt::OpenHandCursor);
    outer->addWidget(m_scroll);

    // Rows are click-through (WA_TransparentForMouseEvents below), so a press on
    // the content falls to the viewport; presses on the frame's own padding arrive
    // as this widget's mouse events.
    m_scroll->viewport()->installEventFilter(this);

    hide();
}

int PlotLegendOverlay::rowGutter() const
{
    return QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);
}

int PlotLegendOverlay::rowCount() const
{
    return m_rows->count();
}

QWidget* PlotLegendOverlay::rowAt(int index) const
{
    QLayoutItem* item = m_rows->itemAt(index);
    return item == nullptr ? nullptr : item->widget();
}

void PlotLegendOverlay::rebuild(const QVector<Entry>& entries, bool dark, bool wanted)
{
    m_dark = dark;

    // Track the content's natural (unconstrained) size from each row's sizeHint()
    // as it is built, rather than asking the container widget for its aggregate
    // sizeHint() afterwards: once the QScrollArea (widgetResizable=true) has
    // squeezed m_content down to a small viewport on an earlier, sparser rebuild,
    // QWidget::sizeHint()/QLayout::sizeHint() can report a stale/zero size on the
    // next rebuild even though the layout correctly holds the new rows - this was
    // reproduced directly (row sizeHints valid, aggregate sizeHint (0,0)).
    int content_w = 0;
    int content_h = 0;
    int shown     = 0;

    // Reconciled by series id (mirrors PlotWidget's chart rebuild): a run with many
    // streams rebuilds once per completed stream, so rebuilding every row's widgets
    // from scratch each time is O(streams^2) widget churn across the run. Existing
    // rows are updated in place and only reordered in the layout (cheap
    // bookkeeping); only genuinely new/removed series pay for widget
    // construction/destruction.
    QHash<int, Row> next_row_by_id;
    next_row_by_id.reserve(entries.size());

    for (const Entry& entry : entries)
    {
        Row row = m_row_by_id.take(entry.id);
        if (row.widget == nullptr)
        {
            // Read-only row: [line swatch][label]. Both are click-through so a press
            // anywhere on the content falls to the viewport and starts a drag; color
            // and name are edited from the Customize Plot Series dialog.
            row.widget = new QWidget;
            row.widget->setObjectName("legendRow");
            row.widget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            auto* row_layout = new QHBoxLayout(row.widget);
            row_layout->setContentsMargins(0, 0, 0, 0);
            row_layout->setSpacing(6);

            row.swatch = new QLabel;
            row.swatch->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            row.swatch->setFixedSize(PlotConstants::kLegendSwatchLen,
                                     PlotConstants::kLegendSwatchThick);

            row.label = new QLabel;
            row.label->setObjectName("legendLabel");
            row.label->setAttribute(Qt::WA_TransparentForMouseEvents, true);

            row_layout->addWidget(row.swatch, 0, Qt::AlignVCenter);
            row_layout->addWidget(row.label, 1, Qt::AlignVCenter);
        }
        row.swatch->setStyleSheet(
            QString("background-color: %1; border-radius: 1px;").arg(entry.color.name()));
        row.label->setText(entry.label);

        // Re-add regardless of whether the row is new or reused: removeWidget() is a
        // no-op bookkeeping call for a widget not currently in the layout, and this
        // guarantees row order always matches the caller's order (which can shift -
        // parallel streams complete out of order) without destroying and recreating
        // widgets.
        m_rows->removeWidget(row.widget);
        m_rows->addWidget(row.widget);
        next_row_by_id.insert(entry.id, row);

        const QSize row_hint = row.widget->sizeHint();
        content_w = qMax(content_w, row_hint.width());
        content_h += row_hint.height();
        if (shown > 0)
        {
            content_h += PlotConstants::kLegendRowSpacing;
        }
        ++shown;
    }

    // Rows left behind belong to series that are no longer shown (hidden,
    // reprocessed away, or axis-view switched).
    for (const Row& row : std::as_const(m_row_by_id))
    {
        m_rows->removeWidget(row.widget);
        row.widget->deleteLater();
    }
    m_row_by_id = std::move(next_row_by_id);

    if (shown == 0)
    {
        hide();
        return;
    }

    // The row layout carries a permanent right-side gutter, so include it here to
    // size the overlay to fit that reserved space rather than clip it.
    applyTheme(m_dark);
    layoutToContent(QSize(content_w + rowGutter(), content_h));
    setVisible(wanted);
    if (wanted)
    {
        raise();
    }
}

void PlotLegendOverlay::applyTheme(bool dark)
{
    m_dark = dark;

    const QColor bg     = dark ? QColor(32, 32, 32, PlotConstants::kLegendBgAlpha)
                               : QColor(255, 255, 255, PlotConstants::kLegendBgAlpha);
    const QColor border = dark ? QColor(90, 90, 90) : QColor(170, 170, 170);
    const QColor text   = dark ? QColor(230, 230, 230) : QColor(30, 30, 30);
    // The app's global theme QSS (resources/win11-{dark,light}.qss) gives every
    // plain QWidget a solid background-color, so each legend row would otherwise
    // paint as an opaque chip against the overlay's own translucent background
    // (a boxed/tabular look). QWidget#legendRow overrides that with a more
    // specific id selector so only the swatch + text show through.
    setStyleSheet(QString(
        "QFrame#legendOverlay { background-color: rgba(%1,%2,%3,%4);"
        " border: 1px solid %5; border-radius: %6px; }"
        "QScrollArea#legendScroll { background: transparent; border: none; }"
        "QWidget#legendContent { background: transparent; }"
        "QWidget#legendRow { background: transparent; }"
        "QLabel#legendLabel { background: transparent; color: %7; }")
                      .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha())
                      .arg(border.name())
                      .arg(PlotConstants::kLegendCornerRadius)
                      .arg(text.name()));
}

void PlotLegendOverlay::layoutToContent(const QSize& content)
{
    if (m_chart == nullptr)
    {
        return;
    }

    const int frame = 2 * PlotConstants::kLegendContentMargin;

    // Cap the legend to a fraction of the chart so a dense plot can't let it grow
    // to swallow the data; overflow past the height cap scrolls. The scrollbar
    // gutter is already folded into content's width, so no further reservation is
    // needed here even when the height cap forces scrolling.
    const int max_w = qMax(80, static_cast<int>(m_chart->width() * PlotConstants::kLegendMaxWidthFrac));
    const int max_h = qMax(60, static_cast<int>(m_chart->height() * PlotConstants::kLegendMaxHeightFrac));

    resize(qMin(content.width() + frame, max_w), qMin(content.height() + frame, max_h));
    reposition();
}

void PlotLegendOverlay::reposition()
{
    // Keep the default top-right anchor until the user drags it; afterwards just
    // keep it inside the (possibly resized) chart.
    if (m_user_moved)
    {
        clampIntoView();
    }
    else
    {
        positionTopRight();
    }
}

void PlotLegendOverlay::positionTopRight()
{
    if (m_chart == nullptr)
    {
        return;
    }
    const int m = PlotConstants::kLegendMarginPx;
    move(qMax(m, m_chart->width() - width() - m), m);
}

void PlotLegendOverlay::clampIntoView()
{
    if (m_chart == nullptr)
    {
        return;
    }
    const int m     = PlotConstants::kLegendMarginPx;
    const int max_x = qMax(m, m_chart->width() - width() - m);
    const int max_y = qMax(m, m_chart->height() - height() - m);
    QPoint p = pos();
    p.setX(qBound(m, p.x(), max_x));
    p.setY(qBound(m, p.y(), max_y));
    move(p);
}

bool PlotLegendOverlay::beginDrag(const QPoint& globalPos)
{
    m_dragging         = true;
    m_drag_start_global = globalPos;
    m_start_pos        = pos();
    setCursor(Qt::ClosedHandCursor);
    return true;
}

bool PlotLegendOverlay::continueDrag(const QPoint& globalPos)
{
    if (!m_dragging)
    {
        return false;
    }
    move(m_start_pos + (globalPos - m_drag_start_global));
    clampIntoView();
    m_user_moved = true;
    return true;
}

bool PlotLegendOverlay::endDrag()
{
    if (!m_dragging)
    {
        return false;
    }
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
    return true;
}

void PlotLegendOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && beginDrag(event->globalPosition().toPoint()))
    {
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void PlotLegendOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (continueDrag(event->globalPosition().toPoint()))
    {
        event->accept();
        return;
    }
    QFrame::mouseMoveEvent(event);
}

void PlotLegendOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (endDrag())
    {
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

bool PlotLegendOverlay::eventFilter(QObject* watched, QEvent* event)
{
    // Presses that land on the scroll viewport (i.e. on the click-through rows)
    // drag the overlay just as presses on its own padding do.
    if (m_scroll != nullptr && watched == m_scroll->viewport() && isVisible())
    {
        switch (event->type())
        {
            case QEvent::MouseButtonPress:
            {
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton)
                {
                    return beginDrag(me->globalPosition().toPoint());
                }
                break;
            }
            case QEvent::MouseMove:
                if (continueDrag(static_cast<QMouseEvent*>(event)->globalPosition().toPoint()))
                {
                    return true;
                }
                break;
            case QEvent::MouseButtonRelease:
                if (endDrag())
                {
                    return true;
                }
                break;
            default:
                break;
        }
    }
    return QFrame::eventFilter(watched, event);
}
