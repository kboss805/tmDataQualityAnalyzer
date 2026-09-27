/**
 * @file plotcontextmenu.cpp
 * @brief PlotWidget's right-click menu (US4.1).
 *
 * Every plot control is reached from this menu or from an on-chart chip - there
 * are no external control rows - so building it is 200 lines of QAction wiring
 * that has little to do with the rest of the widget. It is rebuilt on every
 * request, so the source list, the active view mode and the override state are
 * always current; nothing here is cached.
 *
 * These are still PlotWidget members: the handlers they connect to mutate the
 * widget, so the menu is not separable into a type of its own without inventing
 * an interface for them to call back through.
 */

#include "plotwidget.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>

#include "constants.h"
#include "plotviewmodel.h"

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
    // The key lives in the submenu TITLE, not a QKeySequence: 'V' cycles between the
    // modes rather than selecting one entry, and Qt will not render a shortcut on a
    // submenu at all. It was a tooltip before, which discovers nothing - a shortcut
    // you have to hover to find is barely a shortcut.
    QMenu* view_menu = menu.addMenu(QStringLiteral("View Mode  (V)"));
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
    // Min before max, and both axes' pairs kept together: the two limits of one
    // axis are read as a pair, and an operator pinning a window - lock % between 98
    // and 100, say - sets them one after the other.
    QAction* left_min_act = y_menu->addAction(QStringLiteral("Set Left Min..."));
    connect(left_min_act, &QAction::triggered, this, &PlotWidget::onSetLeftYMin);
    QAction* left_act = y_menu->addAction(QStringLiteral("Set Left Max..."));
    connect(left_act, &QAction::triggered, this, &PlotWidget::onSetLeftYMax);
    y_menu->addSeparator();
    QAction* right_min_act = y_menu->addAction(QStringLiteral("Set Right Min..."));
    connect(right_min_act, &QAction::triggered, this, &PlotWidget::onSetRightYMin);
    QAction* right_act = y_menu->addAction(QStringLiteral("Set Right Max..."));
    connect(right_act, &QAction::triggered, this, &PlotWidget::onSetRightYMax);
    y_menu->addSeparator();
    QAction* reset_y_act = y_menu->addAction(QStringLiteral("Reset"));
    connect(reset_y_act, &QAction::triggered, this, &PlotWidget::onResetYAxes);

    // --- Reset View: both axes at once ------------------------------------
    // Top-level rather than inside a submenu for two reasons. It is the menu's only
    // equivalent of the on-chart Reset view chip - the axis resets are otherwise
    // split across 'X Axis > Reset Span' and 'Y Axes > Reset', so there was no one
    // action that did what the chip does. And a top-level QAction is the only place
    // Qt will actually render the 'R' key, which previously survived as a tooltip on
    // the Y-axis reset that also misdescribed it as resetting both.
    QAction* reset_view_act = menu.addAction(QStringLiteral("Reset View"));
    reset_view_act->setEnabled(has_data);
    reset_view_act->setShortcut(QKeySequence(Qt::Key_R));
    reset_view_act->setShortcutVisibleInContextMenu(true);
    connect(reset_view_act, &QAction::triggered, this, &PlotWidget::resetView);

    menu.addSeparator();

    QAction* export_act = menu.addAction(QStringLiteral("Export..."));
    export_act->setEnabled(has_data);
    connect(export_act, &QAction::triggered, this, &PlotWidget::onExportPlot);

    return &menu;
}
