/**
 * @file plotwidget.h
 * @brief View widget for AGC signal plot — QCustomPlot chart with controls.
 */

#ifndef PLOTWIDGET_H
#define PLOTWIDGET_H

#include <QDoubleSpinBox>
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
    /// Syncs axis ranges from ViewModel to the QCustomPlot axes.
    void updateAxes();
    /// Updates chart title from ViewModel.
    void updateTitle();
    /// Handles opening the Customize Plot dialog.
    void onCustomizePlotClicked();
    /// Handles user editing manual Y range spinboxes.
    void onManualYChanged();
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
    /// Parses "DDD:HH:MM:SS" text to elapsed seconds using the ViewModel base time.
    double parseTimeToElapsed(const QString& text) const;
    void setUpLayout();
    void setUpConnections();

    /// Syncs the left-axis toggle button's text/enabled state to the ViewModel.
    void updateAxisViewButton();
    /// Rebuilds the legend panel from current ViewModel series visibility.
    void rebuildLegend();
    /// Shows or hides the centered "Loading..." overlay over the chart.
    void showLoadingIndicator(bool visible);
    void resizeEvent(QResizeEvent* event) override;



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
    QDoubleSpinBox* m_y_min_spin = nullptr;
    QDoubleSpinBox* m_y_max_spin = nullptr;
    /// @}

    /// @name Bottom controls
    /// @{
    QLineEdit* m_x_start_edit = nullptr;
    QLineEdit* m_x_stop_edit = nullptr;
    QPushButton* m_reset_btn = nullptr;
    /// @}

    /// @name Graph tracking
    /// @{
    QVector<QCPGraph*> m_graphs;          ///< Maps series index → QCPGraph pointer.
    /// @}

    QPushButton* m_customize_btn = nullptr;

    /// @name Legend panel
    /// @{
    QScrollArea*  m_legend_scroll  = nullptr; ///< Scroll area wrapping the legend grid.
    QWidget*      m_legend_widget  = nullptr; ///< Inner container inside the scroll area.
    QGridLayout*  m_legend_grid    = nullptr; ///< 4-column grid of swatch+label pairs.
    /// @}

    QLabel* m_loading_label = nullptr; ///< Overlay label shown while CSV is parsing.
    bool m_updating_from_vm = false;  ///< Guard against signal loops.
    QColor m_title_color;              ///< Chart title text color (accent blue).
};

#endif // PLOTWIDGET_H
