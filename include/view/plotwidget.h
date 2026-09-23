/**
 * @file plotwidget.h
 * @brief View widget for AGC signal plot - TmChart chart with controls.
 */

#ifndef PLOTWIDGET_H
#define PLOTWIDGET_H

#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QMouseEvent>
#include <QPoint>
#include <QResizeEvent>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

class QMenu;

#include <functional>

#include "tmchart.h"

class PlotLegendOverlay;
class PlotViewModel;

/**
 * @brief Custom axis ticker that formats elapsed seconds as DDD:HH:MM:SS.
 *
 * Delegates label formatting to PlotViewModel::formatTime().
 */

/**
 * @brief Self-contained plot widget: TmChart chart + movable legend.
 *
 * Owns its own TmChart instance. Connects to a PlotViewModel for data.
 * Placed inside a QDockWidget by MainView.
 *
 * The chart fills the whole widget: there are no external control rows. Every
 * control (Plot File, View Mode, Customize View, plot title, X/Y axis ranges,
 * Export) lives in the plot's **right-click context menu** — see
 * showPlotContextMenu(). Mouse-wheel zoom and click-drag pan on the X axis are
 * unaffected; right-click is otherwise unused by the chart.
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

    /// Renders the current plot (chart + composited legend) to an image file,
    /// choosing PNG/SVG/PDF from @p path's suffix (unrecognized → PDF). Emits a
    /// success/failure logMessage() and returns whether the write succeeded.
    /// Headless — no dialog — so both onExportPlot() and batch export can call it.
    bool exportImage(const QString& path);

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
    /// Syncs axis ranges from ViewModel to the chart axes.
    /// Shows or hides each Y axis according to whether anything is drawn against
    /// it. Called wherever series visibility is established, not just on rebuild,
    /// so hiding the last SNR series also reclaims the right axis.
    void updateAxisVisibility();
    void updateAxes();
    /// Updates chart title from ViewModel.
    void updateTitle();
    /// Handles opening the Customize Plot dialog.
    void onCustomizePlotClicked();
    /// Builds and shows the plot's right-click context menu at @p pos (chart coords).
    void showPlotContextMenu(const QPoint& pos);
    /// Builds the context menu without showing it, so its actions can be inspected
    /// (and unit-tested) without entering QMenu::exec()'s modal event loop.
    /// Caller takes ownership. Returns nullptr when no ViewModel is attached.
    QMenu* buildContextMenu();
    /// Prompts for a custom plot title.
    void onSetPlotTitle();
    /// Prompts for a start/stop time window (DDD:HH:MM:SS) and applies it.
    void onSetTimeWindow();
    /// Resets the X axis to the full data span.
    void onResetXAxis();
    /// Prompts for the left y-axis maximum override.
    void onSetLeftYMax();
    /// Prompts for the right y-axis maximum override.
    void onSetRightYMax();
    /// Clears both y-axis maximum overrides (back to auto).
    void onResetYAxes();
    /// Shows a tooltip with the nearest data point value under the cursor.
    void onPlotMouseMove(QMouseEvent* event);

signals:
    /// Emitted when a log message should be displayed.
    void logMessage(const QString& message);

private:
    /// Handles a chart X range change from mouse interaction.
    void handlePlotXRangeChanged(double lower, double upper);
    void setUpLayout();
    void setUpConnections();

    /// Clamps @p start / @p stop to the data bounds (warning to the log if a value
    /// was clamped), enforces start <= stop, and applies the window to the ViewModel.
    /// Shared by the Set Time Window dialog so entered values behave exactly as the
    /// old start/stop fields did.
    void applyTimeWindow(double start, double stop,
                         const QString& entered_start, const QString& entered_stop);
    /// Rebuilds the floating legend's rows from current ViewModel series visibility.
    void rebuildLegend();
    /// Applies the translucent background, border, and text colors for the theme.
    /// Applies the overlay chips' theme-appropriate glyphs and styling.
    void styleLegendToggle(bool dark);
    /// Anchors the overlay chip bar at the chart's top-left (the legend itself
    /// defaults to the top-right, so they never collide) and keeps it on top.
    void positionLegendToggle();
    /// Shows/hides and relabels the overlay chips for the current state: the View
    /// Mode chip only when both left-axis metrics exist, the Reset chip only when
    /// the view is actually zoomed or an axis maximum is pinned.
    void updateOverlayChips();
    /// Moves the crosshair to @p x (plot coords) and shows it, or hides it when
    /// @p visible is false. Replots.
    void updateCrosshair(double x, bool visible);
    /// Applies @p lower..@p upper as the X window (ordered, ignored if degenerate)
    /// after a band-zoom drag.
    void applyBandZoom(double lower, double upper);
    /// Sets the chart's resting cursor: an open hand once data is loaded (the plot
    /// can be click-dragged to pan), a plain arrow before that. Pressing swaps in a
    /// closed hand; releasing calls back here.
    void updatePlotCursor();
    /// Shows/hides the legend per @p visible, persists the choice, and refreshes
    /// the toggle's checked state. Does nothing if the state is already @p visible.
    void setLegendVisible(bool visible);
    /// Shows or hides the centered "Loading..." overlay over the chart.
    void showLoadingIndicator(bool visible);
    void resizeEvent(QResizeEvent* event) override;
    /// Intercepts mouse events on the legend (and its viewport) to drag it.
    bool eventFilter(QObject* watched, QEvent* event) override;

    /// Keyboard equivalents for the most-used context-menu and chip actions.
    ///
    /// Deliberately **widget-scoped** rather than application-wide: these are bare
    /// letters, and an application shortcut on a bare letter is dispatched ahead of
    /// the focus widget's key handling, so it would swallow that character
    /// everywhere the user types (the plot-title dialog, the time-window fields,
    /// any config dialog). Handling them here means they only fire while the plot
    /// itself has focus, which a click already gives it (see the panning gesture).
    void keyPressEvent(QKeyEvent* event) override;
    /// Slides the X window by @p fraction of the visible span; negative moves
    /// earlier. Clamped to the data range by applyTimeWindow(), and a no-op when
    /// already showing everything.
    void panTimeWindow(double fraction);
    /// Scales the X window about its centre. @p factor < 1 zooms in, > 1 zooms out.
    void zoomTimeWindow(double factor);
    /// Switches the left axis to the other metric. No-op unless the data has both
    /// (an SNR-only file has nothing to switch to), matching the View Mode chip.
    void cycleViewMode();
    /// Restores the full X span and drops both Y maximum overrides - the keyboard
    /// equivalent of the "Reset view" chip.
    void resetView();



    PlotViewModel* m_view_model = nullptr;

    /// Callback returning the current log window text for log export.
    std::function<QString()> m_log_text_provider;

    /// @name Chart
    /// @{
    TmChart* m_plot = nullptr;
    /// @}

    /// @name Graph tracking
    /// @{
    QHash<int, int> m_series_index_by_id; ///< Series id -> TmChart series index. Rebuilt whenever
                                          ///< series are added or removed: TmChart::removeSeries()
                                          ///< shifts later indices down, so a cached index would
                                          ///< silently start pointing at a different curve.
    /// @}

    /// @name Legend overlay (movable box floating over the chart, child of m_plot)
    /// @{
    PlotLegendOverlay* m_legend = nullptr; ///< Owns its rows, size, placement, styling and drag.
    /// @{

    /// Overlay chip bar floating at the chart's top-left: the only persistent
    /// on-chart chrome. Holds the legend toggle, the View Mode chip and the
    /// self-hiding Reset chip. Deliberately an overlay rather than an external
    /// control row, and never composited into exported images — only
    /// m_legend_overlay is rendered into exports.
    QWidget* m_overlay_bar = nullptr;
    /// On-chart show/hide control for the legend.
    QToolButton* m_legend_toggle = nullptr;
    /// One-click switch between the two left-axis metrics; hidden unless the data
    /// provides both (same rule as the View Mode submenu).
    QToolButton* m_view_mode_chip = nullptr;
    /// Restores the full time span and clears axis-max overrides. Hidden while the
    /// view is already at full span with no overrides, so it adds no idle clutter.
    QToolButton* m_reset_chip = nullptr;

    /// @name Chart-item overlays (no persistent chrome)
    /// @{
    bool   m_band_zooming = false;              ///< True while a band-zoom drag is in progress.
    double m_band_start_x = 0.0;                ///< Plot-coordinate X where the band drag began.
    /// @}
    /// Series the hover readout is pinned to, by stable series id, or
    /// kReadoutNearest for the default "whatever is nearest the cursor" behaviour.
    /// Pinned by id (not index) so a reprocess, which renumbers indices, can't
    /// silently retarget the readout at a different stream.
    static constexpr int kReadoutNearest = -1;
    int m_readout_series_id = kReadoutNearest;

    /// Whether the user wants the legend shown; persisted across sessions
    /// (UIConstants::kSettingsKeyLegendVisible). When false the overlay stays
    /// hidden even with data loaded, and is excluded from exports.
    bool m_legend_visible = true;
    /// @}

    QLabel* m_loading_label = nullptr; ///< Overlay label shown while CSV is parsing.
    bool m_updating_from_vm = false;  ///< Guard against signal loops.
    bool m_dark_theme = false;        ///< Last applied theme, so rebuilt legend rows restyle correctly.
    QColor m_title_color;              ///< Chart title text color (accent blue).
};

#endif // PLOTWIDGET_H
