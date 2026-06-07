/**
 * @file plotwidget.h
 * @brief View widget for AGC signal plot — QCustomPlot chart with controls.
 */

#ifndef PLOTWIDGET_H
#define PLOTWIDGET_H

#include <functional>

#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

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

    /// Applies theme colors (dark/light) to the chart.
    void applyTheme(bool dark);

    /// Builds the receiver legend tree from settings (disabled until data loads).
    void initReceiverLegend(int receiver_count, int channels_per_receiver,
                            const std::function<QString(int)>& channel_prefix_fn);

public slots:
    /// Rebuilds all chart series from the ViewModel data (no legend rebuild).
    void rebuildChart();

private slots:
    /// Called when new CSV data is loaded — rebuilds both chart and legend.
    void onDataChanged();
    /// Toggles a single graph's visibility without full rebuild.
    void onSeriesVisibilityToggled(int index);
    /// Syncs axis ranges from ViewModel to the QCustomPlot axes.
    void updateAxes();
    /// Updates chart title from ViewModel.
    void updateTitle();
    /// Handles a series visibility checkbox toggle.
    void onLegendCheckboxToggled(int series_index, bool checked);
    /// Handles user editing manual Y range spinboxes.
    void onManualYChanged();
    /// Handles user editing X range spinboxes.
    void onXRangeChanged();
    /// Resets all axes to auto/full range.
    void onResetAxes();
    /// Exports the current plot to an image file (PDF, PNG, SVG).
    void onExportPlot();
    /// Copies visible plot data to the clipboard as comma-separated values.
    void onCopyDataToClipboard();
    /// Shows a tooltip with the nearest data point value under the cursor.
    void onPlotMouseMove(QMouseEvent* event);

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
    void rebuildLegend();
    /// Shows or hides the centered "Loading..." overlay over the chart.
    void showLoadingIndicator(bool visible);
    void resizeEvent(QResizeEvent* event) override;

    /// @name Legend helpers (extracted to reduce cognitive complexity)
    /// @{
    void clearLegendContents();
    void syncLegendScrollbars();
    void connectExpandCollapseToggle(QPushButton* toggle_btn);
    void connectLegendItemChanged(QTreeWidget* tree);
    void setAllLegendChecks(bool checked);
    /// @}

    PlotViewModel* m_view_model = nullptr;

    /// @name Chart
    /// @{
    QCustomPlot* m_plot = nullptr;
    /// @}

    /// @name Top toolbar controls
    /// @{
    QLineEdit* m_title_edit = nullptr;
    QDoubleSpinBox* m_y_min_spin = nullptr;
    QDoubleSpinBox* m_y_max_spin = nullptr;
    /// @}

    /// @name Bottom controls
    /// @{
    QLineEdit* m_x_start_edit = nullptr;
    QLineEdit* m_x_stop_edit = nullptr;
    QPushButton* m_reset_btn = nullptr;
    QPushButton* m_export_btn = nullptr;
    QPushButton* m_copy_data_btn = nullptr;
    /// @}

    /// @name Graph tracking
    /// @{
    QVector<QCPGraph*> m_graphs;          ///< Maps series index → QCPGraph pointer.
    /// @}

    /// @name Legend tree panel
    /// @{
    QWidget* m_legend_panel;
    QVBoxLayout* m_legend_layout;
    QVector<QTreeWidget*> m_legend_trees;
    /// @}

    QLabel* m_loading_label = nullptr; ///< Overlay label shown while CSV is parsing.
    bool m_updating_from_vm = false;  ///< Guard against signal loops.
    QColor m_title_color;              ///< Chart title text color (accent blue).
};

#endif // PLOTWIDGET_H
