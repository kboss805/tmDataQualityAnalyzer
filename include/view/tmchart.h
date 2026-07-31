#ifndef TMCHART_H
#define TMCHART_H

#include <functional>

#include <QColor>
#include <QPen>
#include <QString>
#include <QVector>
#include <QWidget>

#include "constants.h"

/// @brief First-party 2D line chart: the rendering engine behind PlotWidget.
///
/// Replaces the vendored QCustomPlot. The library was third-party and protected
/// (never editable), so every rendering behaviour had to be adapted around it in
/// application code; this class is deliberately scoped to exactly what
/// tmDataQualityAnalyzer needs rather than being a general charting library:
///
///  - one shared X axis (elapsed time) plus an independent left and right Y axis,
///  - polyline series bound to either Y axis, each with its own pen/visibility,
///  - horizontal-only wheel zoom and drag pan, reported back so the ViewModel
///    stays the single source of truth for the visible range,
///  - a cursor crosshair and a drag-to-zoom band, drawn rather than composed from
///    child items so they cost no widgets and stay out of exports,
///  - one render() path shared by the screen and by PNG/SVG/PDF export, which is
///    what keeps "what you see" and "what you export" from drifting apart.
///
/// Coordinates are data values; pixel conversion is via xToPixel()/pixelToX().
/// Everything paints in paintEvent(), so there is no retained scene graph and no
/// explicit replot() call - update() is enough.
class TmChart : public QWidget
{
    Q_OBJECT

public:
    /// Which Y axis a series is measured against.
    enum class Axis { Left, Right };

    explicit TmChart(QWidget* parent = nullptr);

    // --- Series ----------------------------------------------------------
    /// Appends an empty series bound to @p axis. @return its index.
    int  addSeries(Axis axis);
    /// Removes the series at @p index; later indices shift down by one.
    void removeSeries(int index);
    void clearSeries();
    int  seriesCount() const { return m_series.size(); }

    void setSeriesData(int index, const QVector<double>& xs, const QVector<double>& ys);
    void setSeriesPen(int index, const QPen& pen);
    void setSeriesVisible(int index, bool visible);
    void setSeriesName(int index, const QString& name);
    bool seriesVisible(int index) const;
    QPen seriesPen(int index) const;
    QString seriesName(int index) const;

    // --- Ranges ----------------------------------------------------------
    void setXRange(double lower, double upper);
    void setLeftRange(double lower, double upper);
    void setRightRange(double lower, double upper);
    double xLower() const { return m_x_lower; }
    double xUpper() const { return m_x_upper; }

    // --- Labels and chrome ------------------------------------------------
    void setTitle(const QString& title);
    void setXLabel(const QString& label);
    void setLeftLabel(const QString& label);
    void setRightLabel(const QString& label);
    /// The right axis is drawn only when SNR series exist.
    void setRightAxisVisible(bool visible);
    /// Extra headroom reserved above the plot area, in pixels.
    ///
    /// The host overlays controls on the chart's top-left corner; without this the
    /// plot area starts at the outer margin and those controls sit directly on the
    /// axis line and the topmost Y tick label. Reserving space pushes the axes down
    /// instead of drawing chrome over them.
    void setTopInset(int px);
    /// Number of X ticks drawn, evenly spaced across the visible range.
    void setXTickCount(int count);
    /// Formats an X value for its tick label (elapsed seconds -> DDD:HH:MM:SS).
    /// Without one, values print as plain numbers.
    void setTimeFormatter(std::function<QString(double)> formatter);

    /// Applies the palette. Kept as one call so a theme switch cannot leave some
    /// elements on the old colours.
    void setThemeColors(const QColor& background, const QColor& foreground,
                        const QColor& grid, const QColor& title);

    // --- Interaction state -------------------------------------------------
    /// Enables wheel zoom and drag pan of the X axis. Off until data is loaded,
    /// so an empty chart cannot be scrolled into a meaningless range.
    void setInteractionsEnabled(bool enabled);
    bool interactionsEnabled() const { return m_interactions; }

    /// Vertical dashed line following the cursor; @p visible false hides it.
    void setCrosshair(double x, bool visible);
    /// Translucent band showing the drag-to-zoom selection.
    void setZoomBand(double x0, double x1, bool visible);

    // --- Geometry ---------------------------------------------------------
    /// @return The rectangle the data is drawn in (excludes axes and title).
    ///
    /// QRectF, not QRect, on purpose: painting and the coordinate transforms work
    /// in continuous coordinates where the right edge is left + width, whereas
    /// QRect::right() is the last pixel INDEX (left + width - 1). Mixing the two
    /// clips the final pixel column of data off the plot.
    QRectF plotArea() const;
    double pixelToX(double px) const;
    double xToPixel(double x) const;
    double pixelToLeft(double py) const;
    double leftToPixel(double value) const;
    double rightToPixel(double value) const;

    // --- Export -----------------------------------------------------------
    /// Paints the whole chart at @p size onto @p painter. Used for PNG, SVG and
    /// PDF alike so exports cannot drift from the on-screen rendering. Overlays
    /// (crosshair, zoom band) are deliberately excluded: they are cursor state,
    /// not data.
    void renderTo(QPainter& painter, const QSize& size) const;

signals:
    /// Emitted when the user zooms or pans, NOT when setXRange() is called - so
    /// the ViewModel round-trip cannot feed back on itself.
    void xRangeChangedByUser(double lower, double upper);
    void mousePressed(QMouseEvent* event);
    void mouseMoved(QMouseEvent* event);
    void mouseReleased(QMouseEvent* event);
    void mouseDoubleClicked(QMouseEvent* event);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    struct Series
    {
        QVector<double> xs;
        QVector<double> ys;
        QPen    pen{Qt::white};
        QString name;
        Axis    axis    = Axis::Left;
        bool    visible = true;
    };

    /// Recomputes m_plot_area from the current size, fonts and which chrome is
    /// shown.
    void recalcPlotArea() const;
    /// Recomputes the layout if the widget has been resized since it was last
    /// computed. Every geometry accessor calls this because a widget that is
    /// resized but never shown - which is exactly how the tests and the headless
    /// export path use it - receives no resizeEvent, leaving a stale empty plot
    /// area that silently collapses every coordinate to zero.
    void ensureLayout() const;
    /// Paints everything at @p rect. Shared by paintEvent() and renderTo();
    /// @p with_overlays is false for export.
    void render(QPainter& painter, const QRect& rect, bool with_overlays) const;
    void drawGrid(QPainter& painter) const;
    void drawAxes(QPainter& painter) const;
    void drawSeries(QPainter& painter) const;
    void drawTitle(QPainter& painter, const QRect& rect) const;
    /// @return @p count evenly spaced values across [lower, upper].
    static QVector<double> tickValues(double lower, double upper, int count);
    QString formatX(double value) const;
    bool indexValid(int index) const { return index >= 0 && index < m_series.size(); }

    QVector<Series> m_series;

    double m_x_lower = 0.0;
    double m_x_upper = 1.0;
    double m_left_lower = 0.0;
    double m_left_upper = 1.0;
    double m_right_lower = 0.0;
    double m_right_upper = 1.0;

    QString m_title;
    QString m_x_label;
    QString m_left_label;
    QString m_right_label;
    bool    m_right_visible = false;
    int     m_x_tick_count  = PlotConstants::kTickCount;
    int     m_top_inset     = 0;   ///< Extra reserved headroom; see setTopInset().
    std::function<QString(double)> m_formatter;

    QColor m_background{Qt::black};
    QColor m_foreground{Qt::white};
    QColor m_grid{Qt::gray};
    QColor m_title_color{Qt::white};

    mutable QRectF m_plot_area;
    mutable QSize m_layout_size;   ///< Widget size m_plot_area was computed for.

    bool   m_interactions = false;
    bool   m_panning      = false;
    double m_pan_anchor_x = 0.0;   ///< Data X under the cursor when the pan began.

    bool   m_crosshair_visible = false;
    double m_crosshair_x       = 0.0;
    bool   m_band_visible      = false;
    double m_band_x0           = 0.0;
    double m_band_x1           = 0.0;
};

#endif // TMCHART_H
