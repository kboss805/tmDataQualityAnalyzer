/**
 * @file plotwidget.cpp
 * @brief Implementation of PlotWidget - TmChart chart with controls.
 */

#include "plotwidget.h"

#include <algorithm>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QStyle>
#include <QTextStream>
#include <QToolTip>
#include <QtSvg/QSvgGenerator>
#include <QVBoxLayout>


#include "constants.h"
#include "plotlegendoverlay.h"
#include "plotviewmodel.h"
#include "exportdialog.h"
#include "plotcustomizationdialog.h"

namespace {
    /// @return true for metrics drawn on the left axis (lock % and frame-sync errors).
    bool isLeftAxisMetric(PlotSeriesData::MetricType type)
    {
        return type == PlotSeriesData::MetricType::FrameSyncLock
            || type == PlotSeriesData::MetricType::AccumulatedMissedFrames;
    }

    /// @return the legend-overlay display text for a series. SNR series carry a
    /// "<pcmChannelId> - <streamLabel> <ch.name>" identity in s.name (that exact
    /// format matters for CSV export/import and renaming — see the NOTE in
    /// PlotViewModel::addStreamData() — so it is left untouched there). The legend
    /// only needs the channel number and receiver/channel suffix to disambiguate
    /// series on screen, not the full TMATS-derived stream title, which can be long
    /// enough to crowd the overlay. Rebuilt from receiverIndex/channelIndex rather
    /// than parsed out of s.name so it can't drift from the SNR naming convention.
    QString legendDisplayName(const PlotSeriesData& s)
    {
        if (s.metricType != PlotSeriesData::MetricType::SNR)
        {
            return s.name;
        }
        const QString prefix = (s.channelIndex >= 0
                                && s.channelIndex < static_cast<int>(UIConstants::kChannelPrefixes.size()))
            ? QString(UIConstants::kChannelPrefixes[s.channelIndex])
            : QString::number(s.channelIndex + 1);
        return QString("CH%1 %2_RCVR%3").arg(s.streamOrder).arg(prefix).arg(s.receiverIndex);
    }
}

////////////////////////////////////////////////////////////////////////////////
// PlotWidget
////////////////////////////////////////////////////////////////////////////////

PlotWidget::PlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setUpLayout();
    setUpConnections();
}

void PlotWidget::setViewModel(PlotViewModel* vm)
{
    if (m_view_model != nullptr)
    {
        QObject::disconnect(m_view_model, nullptr, this, nullptr);
    }

    m_view_model = vm;
    if (vm == nullptr)
    {
        return;
    }

    // X tick labels are elapsed file time (DDD:HH:MM:SS), which only the ViewModel
    // can format. Previously an QCPAxisTicker subclass existed solely to override
    // three virtuals for this; a formatter callback replaces the whole class.
    m_plot->setTimeFormatter([vm](double value) { return vm->formatTime(value); });
    m_plot->setXTickCount(PlotConstants::kTickCount);

    connect(vm, &PlotViewModel::dataChanged,  this, &PlotWidget::onDataChanged);
    connect(vm, &PlotViewModel::dataChanged,  this, [this]() { showLoadingIndicator(false); });
    connect(vm, &PlotViewModel::loadStarted,  this, [this]() { showLoadingIndicator(true);  });
    connect(vm, &PlotViewModel::loadFailed,   this, [this]() { showLoadingIndicator(false); });
    connect(vm, &PlotViewModel::seriesVisibilityChanged, this, &PlotWidget::onSeriesVisibilityToggled);
    connect(vm, &PlotViewModel::seriesAppearanceChanged, this, &PlotWidget::onSeriesAppearanceChanged);
    connect(vm, &PlotViewModel::axisRangeChanged, this, &PlotWidget::updateAxes);
    connect(vm, &PlotViewModel::plotTitleChanged, this, &PlotWidget::updateTitle);
    connect(vm, &PlotViewModel::lockAxisViewChanged, this, &PlotWidget::onLockAxisViewChanged);
    // No sourcesChanged handler needed: the Plot File submenu is built from
    // sourceList() each time the context menu opens, so it is never stale.
}

void PlotWidget::setLogTextProvider(std::function<QString()> provider)
{
    m_log_text_provider = std::move(provider);
}

void PlotWidget::applyTheme(bool dark)
{
    QColor bg = dark ? PlotConstants::kDarkBackground : PlotConstants::kLightBackground;
    QColor fg = dark ? PlotConstants::kDarkForeground : PlotConstants::kLightForeground;
    QColor grid = dark ? PlotConstants::kDarkGridColor : PlotConstants::kLightGridColor;
    m_title_color = dark ? QColor("#60CDFF") : QColor("#005FB8");

    // One call rather than a dozen per-axis setters: a theme switch cannot leave
    // some elements painted in the old palette.
    m_plot->setThemeColors(bg, fg, grid, m_title_color);

    m_dark_theme = dark;
    m_legend->applyTheme(dark);
    styleLegendToggle(dark);

    m_plot->update();
}



void PlotWidget::rebuildChart()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    m_updating_from_vm = true;

    const auto& all_series = m_view_model->allSeries();

    // Rebuild the chart's series from the series list, keyed by stable series id.
    //
    // Rebuilt wholesale rather than reconciled in place because TmChart identifies
    // series by index and removeSeries() shifts later indices down - a cached index
    // would quietly start addressing a different curve. That is affordable here:
    // QVector is implicitly shared, so handing the same samples back is a refcount
    // bump, not a copy of the data.
    m_plot->clearSeries();
    m_series_index_by_id.clear();
    m_series_index_by_id.reserve(static_cast<int>(all_series.size()));

    for (const PlotSeriesData& s : all_series)
    {
        const int idx = m_plot->addSeries(isLeftAxisMetric(s.metricType)
                                              ? TmChart::Axis::Left
                                              : TmChart::Axis::Right);
        m_plot->setSeriesName(idx, s.name);
        m_plot->setSeriesData(idx, s.xValues, s.yValues);
        m_plot->setSeriesPen(idx, QPen(s.color, PlotConstants::kGraphPenWidth));
        m_plot->setSeriesVisible(idx, m_view_model->effectiveVisible(s));
        m_series_index_by_id.insert(s.id, idx);
    }

    // Set axis labels. The left axis label tracks the active left-axis view.
    m_plot->setXLabel(PlotConstants::kXAxisLabel);
    m_plot->setLeftLabel(
        m_view_model->lockAxisView() == PlotViewModel::LockAxisView::MissedFrames
            ? PlotConstants::kMissedFramesAxisLabel
            : PlotConstants::kYAxisLabel);

    // Update title and axes without triggering extra replots
    updateTitle();
    updateAxisVisibility();
    updateAxes();

    // Mouse pan/zoom only once there is data. The context menu gates its own
    // actions on hasData() when it is built.
    const bool has_data = m_view_model->hasData();
    m_plot->setInteractionsEnabled(has_data);

    // The overlay chips only make sense once something is plotted.
    updateOverlayChips();

    updatePlotCursor();
    updateOverlayChips();

    m_plot->update();
    m_updating_from_vm = false;
}

void PlotWidget::onDataChanged()
{
    rebuildChart();
    rebuildLegend();
}

void PlotWidget::onSeriesVisibilityToggled(int index)
{
    if (m_view_model == nullptr)
    {
        return;
    }
    const auto& series = m_view_model->allSeries();
    if (index < 0 || index >= static_cast<int>(series.size()))
    {
        return;
    }
    // Resolve through the id map: the chart's index order matches the series list
    // as built, but going via the id keeps this correct if they ever diverge.
    const int chart_index = m_series_index_by_id.value(series[index].id, -1);
    if (chart_index < 0)
    {
        return;
    }
    m_plot->setSeriesVisible(chart_index,
                             m_view_model->effectiveVisible(m_view_model->seriesAt(index)));
    updateAxisVisibility();   // hiding the last SNR series reclaims the right axis
    m_plot->update();
    rebuildLegend();
}

void PlotWidget::onSeriesAppearanceChanged()
{
    if (m_view_model == nullptr)
    {
        return;
    }
    // Re-apply per-series color and visibility to the graphs (names live only in
    // the legend/VM), then rebuild the legend rows. Axes/data are untouched, so no
    // rebuildChart. Visibility is included here because PlotCustomizationDialog
    // batches its checkbox edits through setSeriesVisibleQuiet() + this one signal
    // instead of one seriesVisibilityChanged() per checkbox. Looked up by stable
    // series id via m_series_index_by_id (not by position) so this stays
    // correct even if a background streamProcessed()/addStreamData() added or
    // reordered series while this dialog-driven signal was in flight.
    for (const PlotSeriesData& s : m_view_model->allSeries())
    {
        const int idx = m_series_index_by_id.value(s.id, -1);
        if (idx >= 0)
        {
            m_plot->setSeriesPen(idx, QPen(s.color, PlotConstants::kGraphPenWidth));
            m_plot->setSeriesVisible(idx, m_view_model->effectiveVisible(s));
        }
    }
    updateAxisVisibility();
    rebuildLegend();
    m_plot->update();
}

void PlotWidget::onLockAxisViewChanged()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    // The metric toggle only flips per-series visibility and the left-axis label;
    // the graphs and their data are unchanged, so sync visibility in place instead
    // of rebuilding every series. Axis ranges arrive separately
    // via axisRangeChanged -> updateAxes(). Looked up by stable series id via
    // m_series_index_by_id (not by position) so this stays correct even if a
    // background streamProcessed()/addStreamData() added or reordered series while
    // this signal was in flight.
    for (const PlotSeriesData& s : m_view_model->allSeries())
    {
        const int idx = m_series_index_by_id.value(s.id, -1);
        if (idx >= 0)
        {
            // Re-apply color as well as visibility: a custom recolor propagates to the
            // lock/missed sibling in the ViewModel, and that sibling first becomes
            // visible here, so its pen must be refreshed from the (updated) series color.
            m_plot->setSeriesPen(idx, QPen(s.color, PlotConstants::kGraphPenWidth));
            m_plot->setSeriesVisible(idx, m_view_model->effectiveVisible(s));
        }
    }

    m_plot->setLeftLabel(
        m_view_model->lockAxisView() == PlotViewModel::LockAxisView::MissedFrames
            ? PlotConstants::kMissedFramesAxisLabel
            : PlotConstants::kYAxisLabel);

    updateAxisVisibility();
    rebuildLegend();
    m_plot->update();
}

void PlotWidget::updateAxisVisibility()
{
    if (m_view_model == nullptr)
    {
        return;
    }
    const bool left  = m_view_model->hasVisibleLeftAxisSeries();
    const bool right = m_view_model->hasVisibleRightAxisSeries();

    // A frame-sync-only file has nothing on the right axis and an SNR-only file
    // nothing on the left. Drawing the unused one anyway leaves it auto-ranged to a
    // meaningless 0..1 under a label naming data that is not there, which reads as a
    // real measurement pinned near zero.
    //
    // With nothing plotted at all - before a file is opened, or with every series
    // hidden - the left axis is kept so the chart still reads as a chart rather than
    // a bare box. An empty axis only misleads once something else IS plotted.
    m_plot->setLeftAxisVisible(left || !right);
    m_plot->setRightAxisVisible(right);
}

void PlotWidget::updateAxes()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    m_updating_from_vm = true;

    m_plot->setXRange(m_view_model->xViewMin(), m_view_model->xViewMax());

    // Left axis (yAxis): uses user override if set, else 100 for Lock % or auto for Missed Frames.
    const double left_max = m_view_model->leftYMax();
    m_plot->setLeftRange(0.0, left_max);

    // Right axis (yAxis2) auto-scales to SNR data limits, or manual/user-override limits
    m_plot->setRightRange(m_view_model->yMin(), m_view_model->yMax());

    updateOverlayChips();

    m_plot->update();
    m_updating_from_vm = false;
}

void PlotWidget::updateTitle()
{
    if (m_view_model == nullptr)
    {
        return;
    }

    m_updating_from_vm = true;

    // TmChart draws the title itself, so setting the text is the whole job - the
    // previous implementation had to find-or-create a layout element and insert a
    // row for it.
    m_plot->setTitle(m_view_model->plotTitle());
    m_plot->update();
    m_updating_from_vm = false;
}

void PlotWidget::onCustomizePlotClicked()
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        return;
    }

    PlotCustomizationDialog dialog(m_view_model, this);
    dialog.exec();
}



void PlotWidget::onSetPlotTitle()
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        return;
    }

    bool ok = false;
    const QString title = QInputDialog::getText(
        this, QStringLiteral("Set Plot Title"), QStringLiteral("Plot title:"),
        QLineEdit::Normal, m_view_model->plotTitle(), &ok);
    if (ok)
    {
        m_view_model->setPlotTitle(title);
    }
}

void PlotWidget::onSetTimeWindow()
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        return;
    }

    // Two fields in one dialog, seeded with the current window. The text format is
    // PlotViewModel::formatTime()/parseTime() (DDD:HH:MM:SS), so entered values mean
    // exactly what they did in the old Start/Stop fields.
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Set Time Window"));

    auto* start_edit = new QLineEdit(m_view_model->formatTime(m_view_model->xViewMin()), &dialog);
    auto* stop_edit  = new QLineEdit(m_view_model->formatTime(m_view_model->xViewMax()), &dialog);
    start_edit->setPlaceholderText(QStringLiteral("DDD:HH:MM:SS"));
    stop_edit->setPlaceholderText(QStringLiteral("DDD:HH:MM:SS"));

    auto* form = new QGridLayout;
    form->addWidget(new QLabel(QStringLiteral("Start:"), &dialog), 0, 0);
    form->addWidget(start_edit, 0, 1);
    form->addWidget(new QLabel(QStringLiteral("Stop:"), &dialog), 1, 0);
    form->addWidget(stop_edit, 1, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const QString entered_start = start_edit->text();
    const QString entered_stop  = stop_edit->text();
    applyTimeWindow(m_view_model->parseTime(entered_start),
                    m_view_model->parseTime(entered_stop),
                    entered_start, entered_stop);
}

void PlotWidget::onResetXAxis()
{
    if (m_view_model != nullptr)
    {
        m_view_model->resetXRange();
    }
}

void PlotWidget::onSetLeftYMax()
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        return;
    }

    // Lock % is a 0-100 quantity; accumulated missed frames is unbounded, so the
    // accepted range follows the active left-axis metric (as the old spinbox did).
    const bool is_lock_pct =
        (m_view_model->lockAxisView() == PlotViewModel::LockAxisView::LockPercent);
    const double max_allowed = is_lock_pct ? PlotConstants::kLockAxisMax : 10000000.0;

    bool ok = false;
    const double value = QInputDialog::getDouble(
        this, QStringLiteral("Set Left Y-Axis Maximum"),
        is_lock_pct ? QStringLiteral("Maximum lock %:")
                    : QStringLiteral("Maximum accumulated missed frames:"),
        m_view_model->leftYMax(), 1.0, max_allowed, 0, &ok);
    if (ok)
    {
        m_view_model->setLeftYMaxOverride(value);
    }
}

void PlotWidget::onSetRightYMax()
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        return;
    }

    bool ok = false;
    const double value = QInputDialog::getDouble(
        this, QStringLiteral("Set Right Y-Axis Maximum"),
        QStringLiteral("Maximum SNR (dB):"),
        m_view_model->yMax(), 1.0, PlotConstants::kYSpinBoxMax, 1, &ok);
    if (ok)
    {
        m_view_model->setRightYMaxOverride(value);
    }
}

void PlotWidget::onResetYAxes()
{
    if (m_view_model != nullptr)
    {
        m_view_model->resetYRange();
    }
}

void PlotWidget::applyTimeWindow(double raw_start, double raw_stop,
                                 const QString& entered_start, const QString& entered_stop)
{
    if (m_view_model == nullptr)
    {
        return;
    }

    const double data_min = m_view_model->xMin();
    const double data_max = m_view_model->xMax();

    double start = qBound(data_min, raw_start, data_max);
    double stop  = qBound(data_min, raw_stop,  data_max);

    // Warn if either value was clamped to the file bounds
    if (!qFuzzyCompare(start, raw_start))
    {
        emit logMessage(QString("<span style='color:#DAA520;'>Start \"%1\" is outside the file time range - clamped to file bounds.</span>")
                        .arg(entered_start));
    }
    if (!qFuzzyCompare(stop, raw_stop))
    {
        emit logMessage(QString("<span style='color:#DAA520;'>Stop \"%1\" is outside the file time range - clamped to file bounds.</span>")
                        .arg(entered_stop));
    }

    // Enforce start <= stop. Both values arrive together from the dialog, so
    // (unlike the old two-field form, which clamped whichever field had focus)
    // the stop is pulled up to the start.
    if (start > stop)
    {
        emit logMessage("<span style='color:#DAA520;'>Stop time is before Start - clamped to start time.</span>");
        stop = start;
    }

    m_view_model->setXViewRange(start, stop);
}




void PlotWidget::handlePlotXRangeChanged(double lower, double upper)
{
    if (m_updating_from_vm || m_view_model == nullptr)
    {
        return;
    }

    double x_max = m_view_model->xMax();
    double width = upper - lower;

    if (lower < 0.0)
    {
        lower = 0.0;
        upper = width;
    }
    if (upper > x_max)
    {
        upper = x_max;
        lower = qMax(0.0, x_max - width);
    }

    m_view_model->setXViewRange(lower, upper);
}

void PlotWidget::setUpLayout()
{
    // Needed for the keyboard shortcuts in keyPressEvent: without a focus policy
    // this widget can never hold focus, so it would never see a key press.
    setFocusPolicy(Qt::StrongFocus);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(4, 4, 4, 4);
    main_layout->setSpacing(0);

    // --- TmChart chart (fills the widget; all controls live in the
    // right-click context menu, see showPlotContextMenu) ---
    m_plot = new TmChart(this);
    // Horizontal-only pan and zoom are inherent to TmChart - it has no Y gestures
    // to switch off, which is what the two axisRect() calls here used to do.
    m_plot->setInteractionsEnabled(false);
    // Right-click opens the control menu. Pan is bound to left-drag and zoom to
    // the wheel, so the right button is otherwise unused.
    m_plot->setContextMenuPolicy(Qt::CustomContextMenu);
    m_plot->setXLabel(PlotConstants::kXAxisLabel);
    m_plot->setLeftLabel(PlotConstants::kYAxisLabel);
    m_plot->setLeftRange(PlotConstants::kLockAxisMin, PlotConstants::kLockAxisMax);
    // Axis visibility is data-driven from here on (updateAxisVisibility); TmChart
    // starts left-only, which is what an empty chart should show.
    // Keep the axes clear of the overlay chip bar (see kOverlayHeadroomPx).
    m_plot->setTopInset(PlotConstants::kOverlayHeadroomPx);
    m_plot->setRightLabel(PlotConstants::kSnrAxisLabel);
    m_plot->setMinimumHeight(PlotConstants::kPlotMinChartHeight);
    main_layout->addWidget(m_plot, 1);
    main_layout->addSpacing(4);

    // --- Movable legend overlay (floats over the chart interior) ---
    // A translucent, rounded frame parented to m_plot so it composites over the
    // chart. Holds a single-column, vertically scrolling list of line-swatch +
    // label rows; the user can drag it anywhere inside the plot (see eventFilter).
    m_legend = new PlotLegendOverlay(m_plot);
    // The chart's own resizes re-anchor the legend (see eventFilter).
    m_plot->installEventFilter(this);

    // On-chart legend show/hide control. An overlay (child of m_plot) rather than
    // an external control, matching the rest of the plot UI. Anchored top-left so
    // it never collides with the legend's default top-right corner. Hidden until
    // there is data, and never rendered into exports (exportImage composites only
    // m_legend_overlay).
    m_legend_visible = QSettings().value(UIConstants::kSettingsKeyLegendVisible, true).toBool();

    // A single chip bar keeps every persistent on-chart control in one place
    // instead of scattering buttons around the plot. Chips inside it show and
    // hide independently, so the bar shrinks to only what is currently useful.
    m_overlay_bar = new QWidget(m_plot);
    m_overlay_bar->setObjectName("overlayBar");
    m_overlay_bar->setAttribute(Qt::WA_TranslucentBackground);
    auto* chip_row = new QHBoxLayout(m_overlay_bar);
    chip_row->setContentsMargins(0, 0, 0, 0);
    chip_row->setSpacing(PlotConstants::kOverlayChipSpacingPx);

    m_legend_toggle = new QToolButton(m_overlay_bar);
    m_legend_toggle->setObjectName("legendToggle");
    m_legend_toggle->setCheckable(true);
    m_legend_toggle->setChecked(m_legend_visible);
    m_legend_toggle->setCursor(Qt::PointingHandCursor);
    // Text only, like the chips beside it: a bare glyph gave no indication of what
    // the button did, and text + glyph took more of the chart than the label alone.
    m_legend_toggle->setText(QStringLiteral("Toggle Legend"));
    m_legend_toggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_legend_toggle->setFixedHeight(PlotConstants::kOverlayChipHeightPx);
    chip_row->addWidget(m_legend_toggle);

    // One click to flip the left axis between its two metrics - the same choice as
    // the View Mode submenu, for the case where the user flips back and forth.
    m_view_mode_chip = new QToolButton(m_overlay_bar);
    m_view_mode_chip->setObjectName("overlayChip");
    m_view_mode_chip->setCursor(Qt::PointingHandCursor);
    m_view_mode_chip->setFixedHeight(PlotConstants::kOverlayChipHeightPx);
    m_view_mode_chip->hide();
    chip_row->addWidget(m_view_mode_chip);

    // Self-hiding: only appears once the view is actually zoomed or an axis max is
    // pinned, so at rest it costs nothing.
    m_reset_chip = new QToolButton(m_overlay_bar);
    m_reset_chip->setObjectName("overlayChip");
    m_reset_chip->setCursor(Qt::PointingHandCursor);
    m_reset_chip->setFixedHeight(PlotConstants::kOverlayChipHeightPx);
    m_reset_chip->setText(QStringLiteral("Reset view"));
    m_reset_chip->setToolTip(QStringLiteral("Restore the full time span and automatic axis scaling"
                                            " (or double-click the chart)"));
    m_reset_chip->hide();
    chip_row->addWidget(m_reset_chip);

    // Entering a chip must clear the chart's data readout first (see eventFilter),
    // otherwise Qt keeps that tooltip - owned by m_plot, which contains the chips -
    // and the chip's own tooltip never appears.
    m_overlay_bar->installEventFilter(this);
    m_legend_toggle->installEventFilter(this);
    m_view_mode_chip->installEventFilter(this);
    m_reset_chip->installEventFilter(this);

    m_overlay_bar->hide();

    // The crosshair and zoom band are painted by TmChart itself rather than being
    // items it owns, so there is nothing to construct here - see setCrosshair() and
    // setZoomBand(). They stay out of exports by construction.

    // Loading overlay (child of m_plot so it floats over the chart)
    m_loading_label = new QLabel("Loading...", m_plot);
    m_loading_label->setAlignment(Qt::AlignCenter);
    m_loading_label->setObjectName("LoadingLabel");
    m_loading_label->adjustSize();
    m_loading_label->hide();

}

void PlotWidget::setUpConnections()
{
    // Every control lives in the plot's right-click menu; it is rebuilt on each
    // request so it always reflects the current sources/mode/overrides.
    connect(m_plot, &QWidget::customContextMenuRequested,
            this, &PlotWidget::showPlotContextMenu);

    connect(m_plot, &TmChart::mouseMoved, this, &PlotWidget::onPlotMouseMove);

    // Grab-cursor feedback for click-and-drag panning: an open hand over the chart
    // says "this can be dragged", and pressing closes it. Shown whenever data is
    // loaded, regardless of zoom level - when the full span is in view a drag
    // simply has nowhere to go, which is the same behaviour the cursor implies.
    // The legend overlay and its viewport set their own cursors, so dragging the
    // legend is unaffected.
    connect(m_plot, &TmChart::mousePressed, this, [this](QMouseEvent* event) {
        // Clicking the chart focuses the plot, which is what makes the keyboard
        // shortcuts (L/V/R/Home/arrows/+/-) reachable: they are widget-scoped, so
        // they only fire while this widget has focus. Done before the has-data
        // guard so focus follows the click either way.
        setFocus(Qt::MouseFocusReason);
        if (m_view_model == nullptr || !m_view_model->hasData())
        {
            return;
        }
        // Drag out a time range to zoom into it. Bound to Ctrl+left-drag and
        // middle-drag: plain left-drag stays panning, and the right button is
        // taken by the context menu, so neither can be reused here.
        const bool band = (event->button() == Qt::MiddleButton)
                       || (event->button() == Qt::LeftButton
                           && (event->modifiers() & Qt::ControlModifier));
        if (band)
        {
            m_band_zooming = true;
            m_band_start_x = m_plot->pixelToX(event->pos().x());
            m_plot->setCursor(Qt::CrossCursor);
            return;
        }
        if (event->button() == Qt::LeftButton)
        {
            m_plot->setCursor(Qt::ClosedHandCursor);
        }
    });
    connect(m_plot, &TmChart::mouseReleased, this, [this](QMouseEvent* event) {
        if (m_band_zooming)
        {
            m_band_zooming = false;
            m_plot->setZoomBand(0.0, 0.0, false);
            applyBandZoom(m_band_start_x, m_plot->pixelToX(event->pos().x()));
        }
        updatePlotCursor();
    });

    connect(m_legend_toggle, &QToolButton::clicked, this,
            [this](bool checked) { setLegendVisible(checked); });

    // View Mode chip: flip to the other left-axis metric.
    connect(m_view_mode_chip, &QToolButton::clicked, this, [this]() {
        if (m_view_model == nullptr)
        {
            return;
        }
        const bool is_lock =
            (m_view_model->lockAxisView() == PlotViewModel::LockAxisView::LockPercent);
        m_view_model->setLockAxisView(is_lock ? PlotViewModel::LockAxisView::MissedFrames
                                              : PlotViewModel::LockAxisView::LockPercent);
    });

    // Reset chip: full span + automatic axis scaling.
    connect(m_reset_chip, &QToolButton::clicked, this, [this]() {
        if (m_view_model != nullptr)
        {
            m_view_model->resetXRange();
            m_view_model->resetYRange();
        }
    });

    // Double-click anywhere on the chart restores the full time span - the same
    // gesture most plotting tools use, and a shortcut for the Reset chip.
    connect(m_plot, &TmChart::mouseDoubleClicked, this, [this](QMouseEvent*) {
        if (m_view_model != nullptr && m_view_model->hasData())
        {
            m_view_model->resetXRange();
        }
    });

    // TmChart emits this ONLY for user gestures (wheel zoom, drag pan), never from
    // setXRange(). The previous signal fired on programmatic range changes too,
    // which is why the m_updating_from_vm guard exists; it is kept because
    // updateAxes() still sets ranges while handling ViewModel signals.
    connect(m_plot, &TmChart::xRangeChangedByUser,
            this, [this](double lower, double upper) { handlePlotXRangeChanged(lower, upper); });
}

////////////////////////////////////////////////////////////////////////////////
// Legend overlay (movable box floating over the chart)
////////////////////////////////////////////////////////////////////////////////

void PlotWidget::rebuildLegend()
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        m_legend->rebuild({}, m_dark_theme, m_legend_visible);
        return;
    }

    // Which series the legend lists is the ViewModel's business, so it is decided
    // here; the overlay is handed the finished list and owns everything after it.
    const PlotViewModel::LockAxisView axis_view = m_view_model->lockAxisView();
    QVector<PlotLegendOverlay::Entry> entries;
    for (const PlotSeriesData& s : m_view_model->allSeries())
    {
        // Only visible series, and only the active lock metric, so the legend
        // matches exactly what is drawn (and honors the active source filter).
        if (!m_view_model->effectiveVisible(s))
        {
            continue;
        }
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock &&
            axis_view != PlotViewModel::LockAxisView::LockPercent)
        {
            continue;
        }
        if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames &&
            axis_view != PlotViewModel::LockAxisView::MissedFrames)
        {
            continue;
        }
        entries.push_back(PlotLegendOverlay::Entry{ s.id, legendDisplayName(s), s.color });
    }

    m_legend->rebuild(entries, m_dark_theme, m_legend_visible);
}

void PlotWidget::setLegendVisible(bool visible)
{
    if (m_legend_visible == visible)
    {
        return;
    }
    m_legend_visible = visible;
    QSettings().setValue(UIConstants::kSettingsKeyLegendVisible, visible);

    if (m_legend_toggle != nullptr)
    {
        QSignalBlocker blocker(m_legend_toggle);
        m_legend_toggle->setChecked(visible);
        // Refresh the tooltip here rather than only in the chip-update pass:
        // toggling the legend doesn't run that pass, so the hint would otherwise
        // keep offering the action the user just took.
        m_legend_toggle->setToolTip(visible
            ? QStringLiteral("Hide the legend (also in the right-click menu)")
            : QStringLiteral("Show the legend (also in the right-click menu)"));
    }
    // rebuildLegend() re-evaluates rows and applies the new visibility (it also
    // keeps the overlay hidden when there is nothing to show).
    rebuildLegend();
}

void PlotWidget::styleLegendToggle(bool dark)
{
    if (m_legend_toggle == nullptr)
    {
        return;
    }

    // Translucent chips so they read over chart data, same visual language as the
    // legend overlay itself. One stylesheet on the bar covers every chip.
    const QColor bg = dark ? PlotConstants::kDarkBackground : PlotConstants::kLightBackground;
    const QColor border = dark ? PlotConstants::kDarkGridColor : PlotConstants::kLightGridColor;
    const QColor fg = dark ? PlotConstants::kDarkForeground : PlotConstants::kLightForeground;
    const QString accent = m_title_color.isValid() ? m_title_color.name() : border.name();

    if (m_overlay_bar != nullptr)
    {
        m_overlay_bar->setStyleSheet(QString(
            "QToolButton#legendToggle, QToolButton#overlayChip {"
            " background-color: rgba(%1,%2,%3,%4); border: 1px solid %5;"
            " border-radius: %6px; color: %7; padding: 0 8px; }"
            "QToolButton#legendToggle:hover, QToolButton#overlayChip:hover {"
            " border: 1px solid %8; }"
            "QToolButton#legendToggle:checked { border: 1px solid %8; }")
            .arg(bg.red()).arg(bg.green()).arg(bg.blue())
            .arg(PlotConstants::kLegendBgAlpha)
            .arg(border.name())
            .arg(PlotConstants::kLegendCornerRadius)
            .arg(fg.name())
            .arg(accent));
    }

    // Chart-item overlays follow the theme too.
}

void PlotWidget::updateOverlayChips()
{
    if (m_overlay_bar == nullptr || m_view_model == nullptr)
    {
        return;
    }

    const bool has_data = m_view_model->hasData();

    // View Mode chip: same availability rule as the context-menu submenu - both
    // left-axis metrics must exist. Its label names the metric it switches TO, so
    // the click's outcome is obvious without reading the current axis.
    const bool both_metrics =
        m_view_model->hasLockSeries() && m_view_model->hasMissedFramesSeries();
    m_view_mode_chip->setVisible(has_data && both_metrics);
    if (has_data && both_metrics)
    {
        const bool is_lock =
            (m_view_model->lockAxisView() == PlotViewModel::LockAxisView::LockPercent);
        m_view_mode_chip->setText(is_lock ? QStringLiteral("Show Accumulation")
                                          : QStringLiteral("Show Lock %"));
        m_view_mode_chip->setToolTip(is_lock
            ? QStringLiteral("Switch the left axis to Accumulated Missed Frames")
            : QStringLiteral("Switch the left axis to Frame Sync Lock %"));
    }

    if (m_legend_toggle != nullptr)
    {
        m_legend_toggle->setToolTip(m_legend_visible
            ? QStringLiteral("Hide the legend (also in the right-click menu)")
            : QStringLiteral("Show the legend (also in the right-click menu)"));
    }

    // Reset chip: only meaningful once something has actually been changed away
    // from the default view, so it stays hidden the rest of the time.
    const bool zoomed = has_data
        && (!qFuzzyCompare(m_view_model->xViewMin(), m_view_model->xMin())
            || !qFuzzyCompare(m_view_model->xViewMax(), m_view_model->xMax()));
    const bool pinned = has_data
        && (m_view_model->hasLeftYMaxOverride() || m_view_model->hasRightYMaxOverride());
    m_reset_chip->setVisible(zoomed || pinned);

    m_overlay_bar->setVisible(has_data);
    m_overlay_bar->adjustSize();
    positionLegendToggle();
}

void PlotWidget::updateCrosshair(double x, bool visible)
{
    if (m_plot == nullptr)
    {
        return;
    }
    // The chart spans the line across the plot area itself, so only the time
    // coordinate is needed here.
    m_plot->setCrosshair(x, visible);
}

void PlotWidget::applyBandZoom(double lower, double upper)
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        return;
    }
    if (lower > upper)
    {
        std::swap(lower, upper);
    }
    // A click (or a stray micro-drag) must not collapse the view to nothing.
    if (upper - lower < PlotConstants::kMinBandZoomSpanSec)
    {
        return;
    }
    applyTimeWindow(lower, upper,
                    m_view_model->formatTime(lower), m_view_model->formatTime(upper));
}

void PlotWidget::keyPressEvent(QKeyEvent* event)
{
    // Every shortcut here acts on loaded data; with nothing plotted they would all
    // be no-ops, so fall through to the base class and let the key propagate.
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        QWidget::keyPressEvent(event);
        return;
    }

    switch (event->key())
    {
    case Qt::Key_L:
        setLegendVisible(!m_legend_visible);
        break;
    case Qt::Key_V:
        cycleViewMode();
        break;
    case Qt::Key_R:
        resetView();
        break;
    case Qt::Key_Home:
        // Narrower than R on purpose: restore the full time span but keep any
        // Y maximum the user pinned, which is often the thing they want held
        // steady while ranging over the recording.
        onResetXAxis();
        break;
    case Qt::Key_Left:
        panTimeWindow(-PlotConstants::kKeyPanFraction);
        break;
    case Qt::Key_Right:
        panTimeWindow(PlotConstants::kKeyPanFraction);
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:   // unshifted '+' on a US layout
        zoomTimeWindow(PlotConstants::kKeyZoomFactor);
        break;
    case Qt::Key_Minus:
        zoomTimeWindow(1.0 / PlotConstants::kKeyZoomFactor);
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void PlotWidget::panTimeWindow(double fraction)
{
    const double lower = m_view_model->xViewMin();
    const double upper = m_view_model->xViewMax();
    const double span  = upper - lower;
    if (span <= 0.0)
    {
        return;
    }
    // Already showing the whole recording: there is nowhere to pan to, and
    // applyTimeWindow would just clamp back to the same window.
    if (lower <= m_view_model->xMin() && upper >= m_view_model->xMax())
    {
        return;
    }
    const double shift = span * fraction;
    applyTimeWindow(lower + shift, upper + shift,
                    m_view_model->formatTime(lower + shift),
                    m_view_model->formatTime(upper + shift));
}

void PlotWidget::zoomTimeWindow(double factor)
{
    const double lower = m_view_model->xViewMin();
    const double upper = m_view_model->xViewMax();
    const double span  = upper - lower;
    if (span <= 0.0)
    {
        return;
    }
    const double centre    = lower + span / 2.0;
    const double new_span  = span * factor;
    // Same floor the band-zoom drag uses, so a long press of '+' cannot collapse
    // the view to a degenerate window.
    if (new_span < PlotConstants::kMinBandZoomSpanSec)
    {
        return;
    }
    const double new_lower = centre - new_span / 2.0;
    const double new_upper = centre + new_span / 2.0;
    applyTimeWindow(new_lower, new_upper,
                    m_view_model->formatTime(new_lower),
                    m_view_model->formatTime(new_upper));
}

void PlotWidget::cycleViewMode()
{
    // Guard matches the View Mode chip and menu: with only one left-axis metric
    // present there is no other mode to switch to.
    if (!m_view_model->hasLockSeries() || !m_view_model->hasMissedFramesSeries())
    {
        return;
    }
    const bool showing_lock =
        (m_view_model->lockAxisView() == PlotViewModel::LockAxisView::LockPercent);
    m_view_model->setLockAxisView(showing_lock ? PlotViewModel::LockAxisView::MissedFrames
                                              : PlotViewModel::LockAxisView::LockPercent);
}

void PlotWidget::resetView()
{
    onResetXAxis();
    onResetYAxes();
}

void PlotWidget::updatePlotCursor()
{
    if (m_plot == nullptr)
    {
        return;
    }
    // Open hand = "drag me to pan". Plain arrow before any data is loaded, where
    // dragging would be meaningless.
    const bool pannable = (m_view_model != nullptr && m_view_model->hasData());
    m_plot->setCursor(pannable ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void PlotWidget::positionLegendToggle()
{
    if (m_overlay_bar == nullptr || m_plot == nullptr)
    {
        return;
    }
    const int m = PlotConstants::kLegendMarginPx;
    m_overlay_bar->move(m, m);
    m_overlay_bar->raise();
}





void PlotWidget::onPlotMouseMove(QMouseEvent* event)
{
    if (m_view_model == nullptr || !m_view_model->hasData()
        || m_view_model->allSeries().empty())
    {
        return;
    }

    const double x_coord = m_plot->pixelToX(event->pos().x());

    // Drag-to-zoom in progress: stretch the rubber band across the full height of
    // the axis rect between the drag origin and the cursor. The crosshair would
    // just be noise underneath it, so it is suppressed until the drag ends.
    if (m_band_zooming)
    {
        // Only the X extent matters: the band always spans the full plot height.
        m_plot->setZoomBand(m_band_start_x, x_coord, true);
        updateCrosshair(x_coord, false);
        return;
    }

    // Vertical time line under the cursor, so values on several series can be read
    // off at the same instant. The tooltip below supplies the value readout.
    updateCrosshair(x_coord, true);

    // Find the nearest visible data point across all graphs
    double best_dist = std::numeric_limits<double>::max();
    double best_x = 0.0;
    double best_y = 0.0;
    QString best_name;
    int best_series_index = -1;

    // When the readout is pinned to one series, search only that series - and skip
    // the proximity gate below, so it always reports a value at the cursor's time
    // rather than going blank whenever another curve happens to be closer.
    const int pinned_index = (m_readout_series_id == kReadoutNearest)
        ? -1 : m_view_model->indexOfSeriesId(m_readout_series_id);
    const bool pinned = (pinned_index >= 0);

    const auto& all_series = m_view_model->allSeries();
    for (int i = 0; i < static_cast<int>(all_series.size()); i++)
    {
        if (pinned && i != pinned_index)
        {
            continue;
        }
        if (!m_view_model->effectiveVisible(all_series[i]))
        {
            continue;
        }
        const auto& xs = all_series[i].xValues;
        if (xs.isEmpty())
        {
            continue;
        }
        // Binary search for nearest x
        auto it = std::lower_bound(xs.constBegin(), xs.constEnd(), x_coord);
        for (auto check = it; check != xs.constEnd() && check - it < 2; ++check)
        {
            double dist = qAbs(*check - x_coord);
            if (dist < best_dist)
            {
                best_dist = dist;
                int idx = static_cast<int>(check - xs.constBegin());
                best_x = xs[idx];
                best_y = all_series[i].yValues[idx];
                best_name = all_series[i].name;
                best_series_index = i;
            }
        }
        if (it != xs.constBegin())
        {
            --it;
            double dist = qAbs(*it - x_coord);
            if (dist < best_dist)
            {
                best_dist = dist;
                int idx = static_cast<int>(it - xs.constBegin());
                best_x = xs[idx];
                best_y = all_series[i].yValues[idx];
                best_name = all_series[i].name;
                best_series_index = i;
            }
        }
    }

    // Unpinned, the readout only appears when the cursor is actually near a point
    // (10 px), so it doesn't shout values at you from across the chart. A pinned
    // series is exempt: the user asked for that one specifically.
    const double pixel_dist = qAbs(m_plot->xToPixel(best_x) - event->pos().x());
    if ((pinned || pixel_dist <= 10.0) && !best_name.isEmpty() && best_series_index >= 0)
    {
        const PlotSeriesData::MetricType metric = all_series[best_series_index].metricType;
        QString unit;
        switch (metric)
        {
            case PlotSeriesData::MetricType::FrameSyncLock:         unit = "%";        break;
            case PlotSeriesData::MetricType::AccumulatedMissedFrames: unit = " frames"; break;
            case PlotSeriesData::MetricType::SNR:                   unit = " dB";      break;
        }
        QString tip = QString("%1%5\n%2\n%3%4")
            .arg(best_name)
            .arg(m_view_model->formatTime(best_x))
            .arg(QString::number(best_y, 'f', 2))
            .arg(unit)
            .arg(pinned ? QStringLiteral("  (pinned)") : QString());
        QToolTip::showText(event->globalPosition().toPoint(), tip, m_plot);
    }
    else
    {
        QToolTip::hideText();
    }
}

void PlotWidget::showLoadingIndicator(bool visible)
{
    if (!visible)
    {
        m_loading_label->hide();
        return;
    }
    m_loading_label->adjustSize();
    m_loading_label->move((m_plot->width()  - m_loading_label->width())  / 2,
                          (m_plot->height() - m_loading_label->height()) / 2);
    m_loading_label->raise();
    m_loading_label->show();
}

void PlotWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_loading_label != nullptr && m_loading_label->isVisible())
    {
        showLoadingIndicator(true);  // re-centre on resize
    }
    // Keep the on-chart overlay bar anchored to the (new) top-left corner.
    if (m_overlay_bar != nullptr && m_overlay_bar->isVisible())
    {
        positionLegendToggle();
    }
}

bool PlotWidget::eventFilter(QObject* watched, QEvent* event)
{
    // Dismiss the chart's data readout as soon as the cursor lands on a chip.
    // That tooltip is shown with m_plot as its owning widget, and the chips live
    // *inside* m_plot, so Qt considers it still current and won't replace it with
    // the chip's own tooltip - leaving the chips looking like they have none.
    if (event->type() == QEvent::Enter && m_overlay_bar != nullptr
        && (watched == m_overlay_bar || watched->parent() == m_overlay_bar))
    {
        QToolTip::hideText();
    }

    // Keep the legend inside the chart when the chart itself resizes.
    if (watched == m_plot && event->type() == QEvent::Resize
        && m_legend != nullptr && m_legend->isVisible())
    {
        m_legend->reposition();
        return false; // let the chart handle its own resize too
    }

    return QWidget::eventFilter(watched, event);
}
