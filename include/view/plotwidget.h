/**
 * @file plotwidget.h
 * @brief View widget for AGC signal plot — QCustomPlot chart with controls.
 */

#ifndef PLOTWIDGET_H
#define PLOTWIDGET_H

#include <QDoubleSpinBox>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

#include "qcustomplot.h"

class PlotViewModel;

/**
 * @brief Custom axis ticker that formats elapsed seconds as DDD:HH:MM:SS.
 *
 * Delegates label formatting to PlotViewModel::formatTime().
 */
class TimeHackTicker : public QCPAxisTicker
{
public:
    /// Sets the PlotViewModel used for time formatting.
    void setViewModel(PlotViewModel* vm) { m_vm = vm; }

protected:
    /// Overrides the default numeric label with DDD:HH:MM:SS format.
    QString getTickLabel(double tick, const QLocale& locale,
                         QChar formatChar, int precision) override;
    /// Forces an even step so createTickVector() yields exactly tickCount() ticks.
    double getTickStep(const QCPRange& range) override;
    /// No sub-ticks — keeps the time axis from getting more cluttered.
    int getSubTickCount(double tickStep) override;
    /// Generates exactly tickCount() ticks evenly spaced across the range.
    QVector<double> createTickVector(double tickStep, const QCPRange& range) override;

private:
    PlotViewModel* m_vm = nullptr;
};

/**
 * @brief Self-contained plot widget: QCustomPlot chart + toolbar controls + legend.
 *
 * Owns its own QCustomPlot instance. Connects to a PlotViewModel for data.
 * Placed inside a QDockWidget by MainView.
 */
class PlotWidget : public QWidget
{
    Q_OBJECT
    friend class TestPlotWidget;

public:
    explicit PlotWidget(QWidget* parent = nullptr);

    /// Connects this widget to a PlotViewModel instance.
    void setViewModel(PlotViewModel* vm);

    /// Sets a callback that returns the current log window text, used to
    /// optionally export the log to a file from the Export dialog.
    void setLogTextProvider(std::function<QString()> provider);

    /// Applies theme colors (dark/light) to the chart.
    void applyTheme(bool dark);



public slots:
    /// Rebuilds all chart series from the ViewModel data (no legend rebuild).
    void rebuildChart();

    /// Exports the current plot to an image file (PDF, PNG, SVG).
    void onExportPlot();

private slots:
    /// Called when new CSV data is loaded — rebuilds both chart and legend.
    void onDataChanged();
    /// Toggles a single graph's visibility without full rebuild.
    void onSeriesVisibilityToggled(int index);
    /// Re-applies series colors to graphs and rebuilds the legend after color/name
    /// edits made in the Customize Plot dialog (no axis/data changes).
    void onSeriesAppearanceChanged();
    /// Switches the left-axis metric (lock % vs missed frames) by syncing graph
    /// visibility and the axis label in place — no chart rebuild.
    void onLockAxisViewChanged();
    /// Syncs axis ranges from ViewModel to the QCustomPlot axes.
    void updateAxes();
    /// Updates chart title from ViewModel.
    void updateTitle();
    /// Handles opening the Customize Plot dialog.
    void onCustomizePlotClicked();
    /// Handles user editing X range spinboxes.
    void onXRangeChanged();
    /// Resets all axes to auto/full range.
    void onResetAxes();
    /// Shows a tooltip with the nearest data point value under the cursor.
    void onPlotMouseMove(QMouseEvent* event);
    /// Toggles the left axis between Framesync Lock (%) and Missed Frames.
    void onAxisViewToggleClicked();

signals:
    /// Emitted when a log message should be displayed.
    void logMessage(const QString& message);

private:
    /// Handles QCustomPlot axis range change from mouse interaction.
    void handlePlotXRangeChanged(double lower, double upper);
    /// Handles QCustomPlot Y axis range change from mouse interaction.
    void handlePlotYRangeChanged(double lower, double upper);
    void setUpLayout();
    void setUpConnections();

    /// Syncs the left-axis toggle button's text/enabled state to the ViewModel.
    void updateAxisViewButton();
    /// Rebuilds the floating legend's rows from current ViewModel series visibility.
    void rebuildLegend();
    /// Sizes the legend to its content (height/width capped) and clamps it in view.
    void layoutLegendOverlay();
    /// Places the legend at its default top-right corner inside the chart.
    void positionLegendTopRight();
    /// Clamps the legend fully inside the chart's current bounds.
    void clampLegendIntoView();
    /// Applies the translucent background, border, and text colors for the theme.
    void styleLegendOverlay(bool dark);
    /// Shows or hides the centered "Loading..." overlay over the chart.
    void showLoadingIndicator(bool visible);
    void resizeEvent(QResizeEvent* event) override;
    /// Intercepts mouse events on the legend (and its viewport) to drag it.
    bool eventFilter(QObject* watched, QEvent* event) override;



    PlotViewModel* m_view_model = nullptr;

    /// Callback returning the current log window text for log export.
    std::function<QString()> m_log_text_provider;

    /// @name Chart
    /// @{
    QCustomPlot* m_plot = nullptr;
    /// @}

    /// @name Top toolbar controls
    /// @{
    QLineEdit* m_title_edit = nullptr;
    QPushButton* m_axis_view_btn = nullptr; ///< Toggles left axis: Lock (%) vs Missed Frames.
    /// @}

    /// @name Bottom controls
    /// @{
    QLineEdit* m_x_start_edit = nullptr;
    QLineEdit* m_x_stop_edit = nullptr;
    QPushButton* m_reset_btn = nullptr;
    QDoubleSpinBox* m_left_y_max_spin = nullptr;  ///< User-adjustable max for the left (lock/missed-frames) axis.
    QDoubleSpinBox* m_right_y_max_spin = nullptr; ///< User-adjustable max for the right (SNR) axis.
    /// @}

    /// @name Graph tracking
    /// @{
    QVector<QCPGraph*> m_graphs;          ///< Series index → QCPGraph, aligned to PlotViewModel::allSeries().
    QHash<int, QCPGraph*> m_graph_by_id;  ///< Series id → QCPGraph, for incremental reconcile across appends.
    /// @}

    QPushButton* m_customize_btn = nullptr;

    /// @name Legend overlay (movable box floating over the chart, child of m_plot)
    /// @{
    QFrame*      m_legend_overlay   = nullptr; ///< Translucent, rounded, draggable frame.
    QScrollArea* m_legend_scroll    = nullptr; ///< Scroll area inside the frame (vertical scroll for dense plots).
    QWidget*     m_legend_widget    = nullptr; ///< Inner container holding one row per visible series.
    QVBoxLayout* m_legend_rows      = nullptr; ///< Single-column list of swatch+label rows.
    bool         m_dragging_legend  = false;   ///< True while the user is dragging the legend.
    bool         m_legend_user_moved = false;  ///< True once dragged; suppresses the top-right auto-anchor.
    QPoint       m_drag_start_global;          ///< Global cursor position captured at drag start.
    QPoint       m_legend_start_pos;           ///< Legend top-left (in m_plot coords) at drag start.
    /// @}

    QLabel* m_loading_label = nullptr; ///< Overlay label shown while CSV is parsing.
    bool m_updating_from_vm = false;  ///< Guard against signal loops.
    bool m_dark_theme = false;        ///< Last applied theme, so rebuilt legend rows restyle correctly.
    QColor m_title_color;              ///< Chart title text color (accent blue).
};

#endif // PLOTWIDGET_H
