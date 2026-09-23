/**
 * @file plotlegendoverlay.h
 * @brief The movable legend that floats over the chart (US4.0).
 *
 * A translucent, rounded frame parented to the chart so it composites over it,
 * holding a single-column, vertically scrolling list of line-swatch + label
 * rows. It owns everything about itself - its rows, its size, where it sits, how
 * it is styled, and the drag that moves it. PlotWidget decides *what* to show
 * (which series, in which order) and whether the user wants it visible; this
 * class decides nothing about the data.
 */

#ifndef PLOTLEGENDOVERLAY_H
#define PLOTLEGENDOVERLAY_H

#include <QColor>
#include <QFrame>
#include <QHash>
#include <QPoint>
#include <QString>
#include <QVector>

class QLabel;
class QScrollArea;
class QVBoxLayout;

/**
 * @brief Draggable legend overlay for PlotWidget's chart.
 */
class PlotLegendOverlay : public QFrame
{
    Q_OBJECT

public:
    /// One row: a line swatch in the series' color, and its label. The id is the
    /// series' stable id, which is what rows are reconciled by across rebuilds.
    struct Entry
    {
        int     id = -1;
        QString label;
        QColor  color;
    };

    /// @param chart The widget to float over; also this overlay's parent.
    explicit PlotLegendOverlay(QWidget* chart);

    /// Rebuilds the rows from @p entries, restyles for @p dark, then sizes and
    /// places itself. Shows itself only when @p wanted and there is something to
    /// show - the user's hide choice outranks having rows to draw, and an empty
    /// legend is never drawn over the chart.
    void rebuild(const QVector<Entry>& entries, bool dark, bool wanted);

    /// Re-anchors after the chart resizes: top-right until the user has dragged
    /// it, clamped inside the chart afterwards.
    void reposition();

    /// Applies the light/dark palette. Called by rebuild(); public so a theme
    /// switch with no data change can restyle without rebuilding rows.
    void applyTheme(bool dark);

    /// @name Observation points
    /// Used by TestPlotWidget to assert row structure and the scrollbar gutter,
    /// which are the two things a legend regression has actually broken here.
    /// @{
    int      rowCount() const;
    QWidget* rowAt(int index) const;
    int      rowGutter() const;
    /// @}

protected:
    /// Drags the overlay: content rows are click-through, so presses arrive on
    /// the scroll viewport; presses on the frame's padding arrive on the frame.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    /// One row's widgets, kept together so a rebuild can update an existing row
    /// in place rather than tearing it down.
    struct Row
    {
        QWidget* widget = nullptr;
        QLabel*  swatch = nullptr;
        QLabel*  label  = nullptr;
    };

    void layoutToContent(const QSize& content);
    void positionTopRight();
    void clampIntoView();
    bool beginDrag(const QPoint& globalPos);
    bool continueDrag(const QPoint& globalPos);
    bool endDrag();

    QWidget*     m_chart   = nullptr; ///< The chart this floats over (also the parent).
    QScrollArea* m_scroll  = nullptr; ///< Scroll area (vertical scroll for dense plots).
    QWidget*     m_content = nullptr; ///< Inner container holding one row per series.
    QVBoxLayout* m_rows    = nullptr; ///< Single-column list of swatch+label rows.

    /// Series id -> its row, for incremental reconcile across rebuilds.
    QHash<int, Row> m_row_by_id;

    bool   m_dragging   = false; ///< True while the user is dragging.
    bool   m_user_moved = false; ///< True once dragged; suppresses the top-right anchor.
    QPoint m_drag_start_global;  ///< Cursor position at drag start.
    QPoint m_start_pos;          ///< Overlay top-left (in chart coords) at drag start.
    bool   m_dark = false;       ///< Last applied theme.
};

#endif // PLOTLEGENDOVERLAY_H
