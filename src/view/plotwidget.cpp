/**
 * @file plotwidget.cpp
 * @brief Implementation of PlotWidget - TmChart chart with controls.
 */

#include "plotwidget.h"

#include <algorithm>

#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QToolTip>
#include <QtSvg/QSvgGenerator>
#include <QVBoxLayout>
#include <QSettings>
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>

#include "qcustomplot.h"

#include "constants.h"
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
    styleLegendOverlay(dark);
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

    rebuildLegend();
    m_plot->update();
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

void PlotWidget::showPlotContextMenu(const QPoint& pos)
{
    QMenu* menu = buildContextMenu();
    if (menu == nullptr)
    {
        return;
    }
    menu->exec(m_plot->mapToGlobal(pos));
    delete menu;
}

QMenu* PlotWidget::buildContextMenu()
{
    if (m_view_model == nullptr)
    {
        return nullptr;
    }

    // Rebuilt on every request so the source list, active view mode and override
    // state are always current. Nothing here is cached.
    const bool has_data = m_view_model->hasData();

    QMenu& menu = *(new QMenu(this));
    // Qt suppresses action tooltips in menus unless asked; the View Mode submenu
    // uses one to advertise its 'V' shortcut (a cycling key has no single entry to
    // hang a QKeySequence on).
    menu.setToolTipsVisible(true);

    // Set Plot Title leads the menu: it is the most frequently used entry when
    // preparing a plot for a report.
    QAction* title_act = menu.addAction(QStringLiteral("Set Plot Title..."));
    title_act->setEnabled(has_data);
    connect(title_act, &QAction::triggered, this, &PlotWidget::onSetPlotTitle);

    menu.addSeparator();

    // --- Plot File: which processed file to view (US1.1) ------------------
    QMenu* file_menu = menu.addMenu(QStringLiteral("Plot File"));
    const QVector<QPair<int, QString>> sources = m_view_model->sourceList();
    // Only meaningful with more than one source: a single file has nothing to
    // switch between.
    file_menu->setEnabled(has_data && sources.size() > 1);
    if (sources.size() > 1)
    {
        auto* file_group = new QActionGroup(file_menu);
        file_group->setExclusive(true);
        const int visible = m_view_model->visibleSource();

        QAction* all_act = file_menu->addAction(QStringLiteral("All files (overlaid)"));
        all_act->setCheckable(true);
        all_act->setChecked(visible < 0);
        all_act->setActionGroup(file_group);
        connect(all_act, &QAction::triggered, this,
                [this]() { m_view_model->setVisibleSource(-1); });

        file_menu->addSeparator();
        for (const QPair<int, QString>& src : sources)
        {
            QAction* act = file_menu->addAction(src.second);
            act->setCheckable(true);
            act->setChecked(visible == src.first);
            act->setActionGroup(file_group);
            const int source_id = src.first;
            connect(act, &QAction::triggered, this,
                    [this, source_id]() { m_view_model->setVisibleSource(source_id); });
        }
    }

    // --- View Mode: the left-axis metric ----------------------------------
    QMenu* view_menu = menu.addMenu(QStringLiteral("View Mode"));
    // 'V' cycles between the two modes; shown on the submenu since the key toggles
    // rather than selecting one specific entry.
    view_menu->setToolTip(QStringLiteral("V - switch metric"));
    // Only meaningful when both left-axis metrics exist (a frame-sync stream
    // produces both); SNR-only data leaves it disabled.
    const bool both_metrics = m_view_model->hasLockSeries() && m_view_model->hasMissedFramesSeries();
    view_menu->setEnabled(has_data && both_metrics);
    {
        auto* view_group = new QActionGroup(view_menu);
        view_group->setExclusive(true);
        const PlotViewModel::LockAxisView current = m_view_model->lockAxisView();

        struct ModeEntry { const char* text; PlotViewModel::LockAxisView mode; };
        const ModeEntry modes[] = {
            { "Lock Percentage", PlotViewModel::LockAxisView::LockPercent },
            { "Accumulation",    PlotViewModel::LockAxisView::MissedFrames },
        };
        for (const ModeEntry& entry : modes)
        {
            QAction* act = view_menu->addAction(QString::fromLatin1(entry.text));
            act->setCheckable(true);
            act->setChecked(current == entry.mode);
            act->setActionGroup(view_group);
            const PlotViewModel::LockAxisView mode = entry.mode;
            connect(act, &QAction::triggered, this,
                    [this, mode]() { m_view_model->setLockAxisView(mode); });
        }
    }

    menu.addSeparator();

    QAction* customize_act = menu.addAction(QStringLiteral("Customize View..."));
    customize_act->setEnabled(has_data);
    connect(customize_act, &QAction::triggered, this, &PlotWidget::onCustomizePlotClicked);

    // Mirrors the on-chart legend toggle. The button is the primary affordance;
    // this entry makes the control discoverable and keeps both in sync.
    QAction* legend_act = menu.addAction(QStringLiteral("Show Legend"));
    legend_act->setCheckable(true);
    legend_act->setChecked(m_legend_visible);
    legend_act->setEnabled(has_data);
    // Shortcut text only - the key itself is handled in keyPressEvent (widget-scoped,
    // so a bare letter can't swallow typing elsewhere). Setting it here is what makes
    // the shortcut discoverable instead of hidden; setShortcutVisibleInContextMenu is
    // required because Qt hides shortcut text in context menus by default.
    legend_act->setShortcut(QKeySequence(Qt::Key_L));
    legend_act->setShortcutVisibleInContextMenu(true);
    connect(legend_act, &QAction::triggered, this,
            [this](bool checked) { setLegendVisible(checked); });

    // --- Readout: which series the hover value comes from ------------------
    // Default is whatever lies nearest the cursor, which is ambiguous where
    // series overlap; pinning one makes the readout follow that series only.
    QMenu* readout_menu = menu.addMenu(QStringLiteral("Readout"));
    readout_menu->setEnabled(has_data);
    if (has_data)
    {
        // Drop a pin whose series is gone (reprocessed - which mints new ids - or
        // hidden), so the menu doesn't come up with nothing selected. The readout
        // itself already falls back to nearest in that case.
        if (m_readout_series_id != kReadoutNearest)
        {
            const int idx = m_view_model->indexOfSeriesId(m_readout_series_id);
            if (idx < 0 || !m_view_model->effectiveVisible(m_view_model->allSeries()[idx]))
            {
                m_readout_series_id = kReadoutNearest;
            }
        }

        auto* readout_group = new QActionGroup(readout_menu);
        readout_group->setExclusive(true);

        QAction* nearest_act = readout_menu->addAction(QStringLiteral("Nearest series (automatic)"));
        nearest_act->setCheckable(true);
        nearest_act->setChecked(m_readout_series_id == kReadoutNearest);
        nearest_act->setActionGroup(readout_group);
        connect(nearest_act, &QAction::triggered, this,
                [this]() { m_readout_series_id = kReadoutNearest; });

        readout_menu->addSeparator();

        // Only series actually on screen: pinning to a hidden one would read as
        // a broken readout.
        const auto& all_series = m_view_model->allSeries();
        for (const PlotSeriesData& s : all_series)
        {
            if (!m_view_model->effectiveVisible(s))
            {
                continue;
            }
            QAction* act = readout_menu->addAction(s.name);
            act->setCheckable(true);
            act->setChecked(m_readout_series_id == s.id);
            act->setActionGroup(readout_group);
            const int series_id = s.id;
            connect(act, &QAction::triggered, this,
                    [this, series_id]() { m_readout_series_id = series_id; });
        }
    }

    menu.addSeparator();

    // --- X axis -----------------------------------------------------------
    QMenu* x_menu = menu.addMenu(QStringLiteral("X Axis"));
    x_menu->setEnabled(has_data);
    QAction* window_act = x_menu->addAction(QStringLiteral("Set Time Window..."));
    connect(window_act, &QAction::triggered, this, &PlotWidget::onSetTimeWindow);
    QAction* reset_x_act = x_menu->addAction(QStringLiteral("Reset Span"));
    reset_x_act->setShortcut(QKeySequence(Qt::Key_Home));
    reset_x_act->setShortcutVisibleInContextMenu(true);
    connect(reset_x_act, &QAction::triggered, this, &PlotWidget::onResetXAxis);

    // --- Y axes -----------------------------------------------------------
    QMenu* y_menu = menu.addMenu(QStringLiteral("Y Axes"));
    y_menu->setEnabled(has_data);
    QAction* left_act = y_menu->addAction(QStringLiteral("Set Left Max..."));
    connect(left_act, &QAction::triggered, this, &PlotWidget::onSetLeftYMax);
    QAction* right_act = y_menu->addAction(QStringLiteral("Set Right Max..."));
    connect(right_act, &QAction::triggered, this, &PlotWidget::onSetRightYMax);
    y_menu->addSeparator();
    QAction* reset_y_act = y_menu->addAction(QStringLiteral("Reset"));
    // 'R' resets BOTH axes, so it is advertised here rather than implying it only
    // clears the Y overrides.
    reset_y_act->setToolTip(QStringLiteral("R - reset both axes"));
    connect(reset_y_act, &QAction::triggered, this, &PlotWidget::onResetYAxes);

    menu.addSeparator();

    QAction* export_act = menu.addAction(QStringLiteral("Export..."));
    export_act->setEnabled(has_data);
    connect(export_act, &QAction::triggered, this, &PlotWidget::onExportPlot);

    return &menu;
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
    const double max_allowed = is_lock_pct ? 100.0 : 10000000.0;

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


void PlotWidget::onExportPlot()
{
    if (m_view_model == nullptr || !m_view_model->hasData())
    {
        return;
    }

    QSettings settings;
    QString last_dir = settings.value(UIConstants::kSettingsKeyLastCh10Dir, QCoreApplication::applicationDirPath()).toString();

    QString default_name = m_view_model->plotTitle().isEmpty() ? "plot" : m_view_model->plotTitle();
    default_name.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString base_path = QDir(last_dir).filePath(default_name);

    ExportDialog dialog(base_path + ".csv", base_path + ".png", base_path + "_log.txt", this);
    if (dialog.exec() == QDialog::Accepted)
    {
        if (dialog.exportCsv())
        {
            if (m_view_model->exportCsv(dialog.csvPath()))
            {
                emit logMessage(QString("<span style='color:green;'>Data exported to <a href='file:///%1'>%1</a></span>")
                                .arg(dialog.csvPath()));
            }
            else
            {
                emit logMessage(QString("<span style='color:red;'>Error: Failed to export data to %1</span>")
                                .arg(dialog.csvPath()));
            }
        }

        if (dialog.exportImage())
        {
            exportImage(dialog.imagePath());
        }

        if (dialog.exportLog())
        {
            const QString log_path = dialog.logPath();
            const QString log_text = m_log_text_provider ? m_log_text_provider() : QString();

            QFile file(log_path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                QTextStream stream(&file);
                stream << log_text;
                if (!log_text.endsWith('\n'))
                {
                    stream << '\n';
                }
                file.close();
                emit logMessage(QString("<span style='color:green;'>Log exported to <a href='file:///%1'>%1</a></span>")
                                .arg(log_path));
            }
            else
            {
                emit logMessage(QString("<span style='color:red;'>Error: Failed to export log to %1</span>")
                                .arg(log_path));
            }
        }
    }
}

bool PlotWidget::exportImage(const QString& path)
{
    if (m_view_model == nullptr)
    {
        return false;
    }

    QString filename = path;
    QString suffix = QFileInfo(filename).suffix().toLower();
    bool success = false;
    QString formatStr;

    if (suffix == "png")
    {
        // The legend floats over the chart as a child widget, so render it onto
        // the image at its on-screen position (WYSIWYG - if the legend is
        // scrolled, the exported view matches).
        QPixmap px(m_plot->size());
        px.fill(Qt::transparent);
        QPainter painter(&px);
        m_plot->renderTo(painter, m_plot->size());
        if (m_legend_overlay != nullptr && m_legend_overlay->isVisible())
        {
            m_legend_overlay->render(&painter, m_legend_overlay->pos());
        }
        painter.end();
        success = px.save(filename, "PNG");
        formatStr = "PNG";
    }
    else if (suffix == "svg")
    {
        QSvgGenerator generator;
        generator.setFileName(filename);
        generator.setSize(m_plot->size());
        generator.setViewBox(QRect(0, 0, m_plot->width(), m_plot->height()));
        generator.setTitle(m_view_model->plotTitle());
        generator.setDescription("Generated by tmDataQualityAnalyzer");

        QPainter painter;
        success = painter.begin(&generator);
        if (success)
        {
            m_plot->renderTo(painter, m_plot->size());
            if (m_legend_overlay != nullptr && m_legend_overlay->isVisible())
            {
                m_legend_overlay->render(&painter, m_legend_overlay->pos());
            }
            painter.end();
        }
        formatStr = "SVG";
    }
    else
    {
        // Default to PDF
        if (!filename.endsWith(".pdf", Qt::CaseInsensitive))
        {
            filename += ".pdf";
        }
        // Previously QCustomPlot::savePdf(), which was a third rendering path and
        // silently dropped the legend - PDF exports lost it while PNG and SVG kept
        // it. Now the same renderTo() as the other two, so all three agree.
        QPdfWriter writer(filename);
        writer.setPageSize(QPageSize(m_plot->size(), QPageSize::Point));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter painter;
        success = painter.begin(&writer);
        if (success)
        {
            // The PDF device has its own (much finer) resolution, so scale the
            // logical widget geometry onto the page rather than painting 1:1.
            const double sx = writer.width()  / static_cast<double>(m_plot->width());
            const double sy = writer.height() / static_cast<double>(m_plot->height());
            painter.scale(sx, sy);
            m_plot->renderTo(painter, m_plot->size());
            if (m_legend_overlay != nullptr && m_legend_overlay->isVisible())
            {
                m_legend_overlay->render(&painter, m_legend_overlay->pos());
            }
            painter.end();
        }
        formatStr = "PDF";
    }

    if (success)
    {
        emit logMessage(QString("<span style='color:green;'>Plot exported as %1 to <a href='file:///%2'>%2</a></span>")
                        .arg(formatStr)
                        .arg(filename));
    }
    else
    {
        emit logMessage(QString("<span style='color:red;'>Error: Failed to export plot to %1</span>")
                        .arg(filename));
    }
    return success;
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
    m_plot->setLeftRange(0, 100);
    m_plot->setRightAxisVisible(true);
    m_plot->setRightLabel(PlotConstants::kSnrAxisLabel);
    m_plot->setMinimumHeight(PlotConstants::kPlotMinChartHeight);
    main_layout->addWidget(m_plot, 1);
    main_layout->addSpacing(4);

    // --- Movable legend overlay (floats over the chart interior) ---
    // A translucent, rounded frame parented to m_plot so it composites over the
    // chart. Holds a single-column, vertically scrolling list of line-swatch +
    // label rows; the user can drag it anywhere inside the plot (see eventFilter).
    m_legend_overlay = new QFrame(m_plot);
    m_legend_overlay->setObjectName("legendOverlay");
    m_legend_overlay->setCursor(Qt::OpenHandCursor);
    auto* overlay_layout = new QVBoxLayout(m_legend_overlay);
    overlay_layout->setContentsMargins(PlotConstants::kLegendContentMargin,
                                       PlotConstants::kLegendContentMargin,
                                       PlotConstants::kLegendContentMargin,
                                       PlotConstants::kLegendContentMargin);
    overlay_layout->setSpacing(0);

    m_legend_widget = new QWidget;
    m_legend_widget->setObjectName("legendContent");
    m_legend_rows = new QVBoxLayout(m_legend_widget);
    // Reserve a permanent right-side gutter matching the style's scrollbar width so
    // the vertical scrollbar — shown only once content overflows the height cap —
    // never sits on top of the last few characters of a row's label. Always-on
    // rather than conditional: whether a row's text runs into that gutter isn't
    // knowable until the scrollbar decision is made, so a fixed reservation is the
    // only way to guarantee no overlap regardless of content length.
    const int scrollbar_gutter = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    m_legend_rows->setContentsMargins(0, 0, scrollbar_gutter, 0);
    m_legend_rows->setSpacing(PlotConstants::kLegendRowSpacing);

    m_legend_scroll = new QScrollArea(m_legend_overlay);
    m_legend_scroll->setObjectName("legendScroll");
    m_legend_scroll->setWidget(m_legend_widget);
    m_legend_scroll->setWidgetResizable(true);
    m_legend_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_legend_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_legend_scroll->setFrameShape(QFrame::NoFrame);
    m_legend_scroll->viewport()->setAutoFillBackground(false);
    m_legend_scroll->viewport()->setCursor(Qt::OpenHandCursor);
    overlay_layout->addWidget(m_legend_scroll);

    // Drag wiring: each row is click-through (WA_TransparentForMouseEvents set in
    // rebuildLegend), so content presses fall to the viewport; the frame catches
    // presses on its padding. Both — plus m_plot resizes — route to eventFilter().
    m_legend_overlay->installEventFilter(this);
    m_legend_scroll->viewport()->installEventFilter(this);
    m_plot->installEventFilter(this);
    m_legend_overlay->hide();

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
        for (const LegendRow& row : std::as_const(m_legend_row_by_id))
        {
            m_legend_rows->removeWidget(row.widget);
            row.widget->deleteLater();
        }
        m_legend_row_by_id.clear();
        m_legend_overlay->hide();
        return;
    }

    const auto& all_series = m_view_model->allSeries();
    const PlotViewModel::LockAxisView axis_view = m_view_model->lockAxisView();
    int shown = 0;
    // Track the content's natural (unconstrained) size ourselves from each row's
    // sizeHint() as it's built, rather than asking the container widget for its
    // aggregate sizeHint() afterwards: once the QScrollArea (widgetResizable=true)
    // has squeezed m_legend_widget down to a small viewport on an earlier, sparser
    // rebuild, QWidget::sizeHint()/QLayout::sizeHint() can report a stale/zero size
    // on the next rebuild even though the layout correctly holds the new rows —
    // this was reproduced directly (row sizeHints valid, aggregate sizeHint (0,0)).
    int content_w = 0;
    int content_h = 0;

    // Reconciled by series id (mirrors rebuildChart()'s m_graph_by_id): a run with
    // many streams calls rebuildLegend() once per completed stream, so rebuilding
    // every row's widgets from scratch each time is O(streams^2) widget churn
    // across the run. Existing rows are updated in place and only reordered in
    // the layout (cheap bookkeeping); only genuinely new/removed series pay for
    // widget construction/destruction.
    QHash<int, LegendRow> next_row_by_id;
    next_row_by_id.reserve(static_cast<int>(all_series.size()));

    for (const PlotSeriesData& s : all_series)
    {
        // Only show visible series; skip whichever lock metric is not active so the
        // legend matches exactly what is drawn (and honor the active source filter).
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

        LegendRow row = m_legend_row_by_id.take(s.id);
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
            row.swatch->setFixedSize(PlotConstants::kLegendSwatchLen, PlotConstants::kLegendSwatchThick);

            row.label = new QLabel;
            row.label->setObjectName("legendLabel");
            row.label->setAttribute(Qt::WA_TransparentForMouseEvents, true);

            row_layout->addWidget(row.swatch, 0, Qt::AlignVCenter);
            row_layout->addWidget(row.label, 1, Qt::AlignVCenter);
        }
        row.swatch->setStyleSheet(QString("background-color: %1; border-radius: 1px;").arg(s.color.name()));
        row.label->setText(legendDisplayName(s));

        // Re-add regardless of whether the row is new or reused: removeWidget() is
        // a no-op bookkeeping call for a widget not currently in the layout, and
        // this guarantees row order always matches all_series order (which can
        // shift — addStreamData() re-sorts by job submission index as parallel
        // streams complete out of order) without destroying/recreating widgets.
        m_legend_rows->removeWidget(row.widget);
        m_legend_rows->addWidget(row.widget);
        next_row_by_id.insert(s.id, row);

        const QSize row_hint = row.widget->sizeHint();
        content_w = qMax(content_w, row_hint.width());
        content_h += row_hint.height();
        if (shown > 0)
        {
            content_h += PlotConstants::kLegendRowSpacing;
        }
        ++shown;
    }

    // Rows left in m_legend_row_by_id belong to series that are no longer shown
    // (hidden, reprocessed away, or axis-view switched) — remove them.
    for (const LegendRow& row : std::as_const(m_legend_row_by_id))
    {
        m_legend_rows->removeWidget(row.widget);
        row.widget->deleteLater();
    }
    m_legend_row_by_id = std::move(next_row_by_id);

    if (shown == 0)
    {
        m_legend_overlay->hide();
        return;
    }

    // m_legend_rows carries a permanent right-side gutter (set in setUpLayout, same
    // pixelMetric) so the vertical scrollbar never overlaps a row's text; include it
    // here so the overlay is sized to fit that reserved space rather than clip it.
    const int scrollbar_gutter = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    styleLegendOverlay(m_dark_theme);
    layoutLegendOverlay(QSize(content_w + scrollbar_gutter, content_h));
    // The user's show/hide choice wins over "there are rows to draw": keep the
    // overlay hidden (and out of exports) until they turn it back on.
    m_legend_overlay->setVisible(m_legend_visible);
    if (m_legend_visible)
    {
        m_legend_overlay->raise();
    }
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

void PlotWidget::layoutLegendOverlay(const QSize& content)
{
    if (m_legend_overlay == nullptr || m_plot == nullptr)
    {
        return;
    }

    const int frame = 2 * PlotConstants::kLegendContentMargin;

    // Cap the legend to a fraction of the chart so a dense plot can't let it grow
    // to swallow the data; overflow past the height cap scrolls. The scrollbar
    // gutter is already folded into content's width (see rebuildLegend), so no
    // further reservation is needed here even when the height cap forces scrolling.
    const int max_w = qMax(80, static_cast<int>(m_plot->width()  * PlotConstants::kLegendMaxWidthFrac));
    const int max_h = qMax(60, static_cast<int>(m_plot->height() * PlotConstants::kLegendMaxHeightFrac));

    const int w = qMin(content.width() + frame, max_w);
    const int h = qMin(content.height() + frame, max_h);
    m_legend_overlay->resize(w, h);

    // Keep the default top-right anchor until the user drags it; afterwards just
    // keep it inside the (possibly resized) chart.
    if (m_legend_user_moved)
    {
        clampLegendIntoView();
    }
    else
    {
        positionLegendTopRight();
    }
}

void PlotWidget::positionLegendTopRight()
{
    if (m_legend_overlay == nullptr || m_plot == nullptr)
    {
        return;
    }
    const int m = PlotConstants::kLegendMarginPx;
    const int x = m_plot->width() - m_legend_overlay->width() - m;
    m_legend_overlay->move(qMax(m, x), m);
}

void PlotWidget::clampLegendIntoView()
{
    if (m_legend_overlay == nullptr || m_plot == nullptr)
    {
        return;
    }
    const int m = PlotConstants::kLegendMarginPx;
    const int max_x = qMax(m, m_plot->width()  - m_legend_overlay->width()  - m);
    const int max_y = qMax(m, m_plot->height() - m_legend_overlay->height() - m);
    QPoint p = m_legend_overlay->pos();
    p.setX(qBound(m, p.x(), max_x));
    p.setY(qBound(m, p.y(), max_y));
    m_legend_overlay->move(p);
}

void PlotWidget::styleLegendOverlay(bool dark)
{
    if (m_legend_overlay == nullptr)
    {
        return;
    }
    const QColor bg     = dark ? QColor(32, 32, 32, PlotConstants::kLegendBgAlpha)
                               : QColor(255, 255, 255, PlotConstants::kLegendBgAlpha);
    const QColor border = dark ? QColor(90, 90, 90) : QColor(170, 170, 170);
    const QColor text   = dark ? QColor(230, 230, 230) : QColor(30, 30, 30);
    // The app's global theme QSS (resources/win11-{dark,light}.qss) gives every
    // plain QWidget a solid background-color, so each legend row would otherwise
    // paint as an opaque chip against the overlay's own translucent background
    // (a boxed/tabular look). QWidget#legendRow overrides that with a more
    // specific id selector so only the swatch + text show through.
    m_legend_overlay->setStyleSheet(QString(
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
        && m_legend_overlay != nullptr && m_legend_overlay->isVisible())
    {
        if (m_legend_user_moved)
        {
            clampLegendIntoView();
        }
        else
        {
            positionLegendTopRight();
        }
        return false; // let QCustomPlot handle its own resize too
    }

    // Drag the legend: content rows are click-through, so presses arrive on the
    // viewport; presses on the frame padding arrive on the frame itself.
    const bool on_legend = (watched == m_legend_overlay)
        || (m_legend_scroll != nullptr && watched == m_legend_scroll->viewport());
    if (on_legend && m_legend_overlay != nullptr && m_legend_overlay->isVisible())
    {
        switch (event->type())
        {
            case QEvent::MouseButtonPress:
            {
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton)
                {
                    m_dragging_legend   = true;
                    m_drag_start_global = me->globalPosition().toPoint();
                    m_legend_start_pos  = m_legend_overlay->pos();
                    m_legend_overlay->setCursor(Qt::ClosedHandCursor);
                    return true;
                }
                break;
            }
            case QEvent::MouseMove:
            {
                if (m_dragging_legend)
                {
                    auto* me = static_cast<QMouseEvent*>(event);
                    const QPoint delta = me->globalPosition().toPoint() - m_drag_start_global;
                    m_legend_overlay->move(m_legend_start_pos + delta);
                    clampLegendIntoView();
                    m_legend_user_moved = true;
                    return true;
                }
                break;
            }
            case QEvent::MouseButtonRelease:
            {
                if (m_dragging_legend)
                {
                    m_dragging_legend = false;
                    m_legend_overlay->setCursor(Qt::OpenHandCursor);
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
