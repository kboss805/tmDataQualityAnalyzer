/**
 * @file tst_plotwidget.cpp
 * @brief Smoke tests for PlotWidget — construction, ViewModel connection, theme.
 */

#include "tst_plotwidget.h"

#include <QApplication>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QMenu>
#include <QStyle>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtTest>

#include "constants.h"
#include "plotviewmodel.h"
#include "plotwidget.h"
#include "processedstreamdata.h"
#include "streamconfig.h"

void TestPlotWidget::constructsWithoutCrash()
{
    // Construction exercises QCustomPlot setup, axis ticker registration,
    // legend panel, toolbar spinboxes, and signal connections.
    PlotWidget* widget = new PlotWidget();
    QVERIFY(widget != nullptr);
    delete widget;
}

void TestPlotWidget::setViewModelWithNullDoesNotCrash()
{
    PlotWidget widget;
    // Passing nullptr must not crash (guard in setViewModel).
    widget.setViewModel(nullptr);
    QVERIFY(true);
}

void TestPlotWidget::setViewModelConnectsWithoutCrash()
{
    PlotWidget widget;
    PlotViewModel vm;
    // Connecting a valid ViewModel must not crash.
    widget.setViewModel(&vm);
    QVERIFY(true);
}

void TestPlotWidget::applyThemeDarkDoesNotCrash()
{
    PlotWidget widget;
    PlotViewModel vm;
    widget.setViewModel(&vm);
    widget.applyTheme(true);
    QVERIFY(true);
}

void TestPlotWidget::applyThemeLightDoesNotCrash()
{
    PlotWidget widget;
    PlotViewModel vm;
    widget.setViewModel(&vm);
    widget.applyTheme(false);
    QVERIFY(true);
}

void TestPlotWidget::legendOverlayPopulatesFromData()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);

    // No data yet: the legend overlay stays hidden.
    QVERIFY(widget.m_legend_overlay->isHidden());

    // Add one lock stream (yields a lock + a missed-frames series). addStreamData
    // emits dataChanged(), which drives the widget's rebuildChart + rebuildLegend.
    ProcessedStreamData d;
    d.streamLabel  = "Ch 5";
    d.pcmChannelId = 5;
    d.mode         = StreamMode::FrameSyncLockStats;
    d.timesSec                = { 0.0, 1.0, 2.0 };
    d.lockPercent             = { 90.0, 95.0, 100.0 };
    d.accumulatedMissedFrames = { 0.0, 1.0, 1.0 };
    vm.addStreamData(d);

    // With the default Lock % view active, only the lock series is visible, so the
    // legend shows exactly one row and is no longer hidden.
    QVERIFY(!widget.m_legend_overlay->isHidden());
    QCOMPARE(widget.m_legend_rows->count(), 1);
}

void TestPlotWidget::legendOverlaySizesCorrectlyAfterRebuild()
{
    // Regression test for a real bug: on a SECOND rebuild (adding a stream after
    // the legend already has rows), the QScrollArea (widgetResizable=true) has
    // already squeezed the legend's content widget down to fit its earlier,
    // smaller size, and querying that widget's aggregate sizeHint() at that point
    // returns a stale (0,0) even though every row still has a valid sizeHint —
    // collapsing the overlay to a near-invisible frame-only box. rebuildLegend()
    // must compute the content size itself, from each row's sizeHint(), rather
    // than trusting the container widget's sizeHint() after the fact.
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    widget.resize(1900, 950);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    ProcessedStreamData d1;
    d1.streamLabel = "CH-01 PRN 15 5M";
    d1.pcmChannelId = 1;
    d1.mode = StreamMode::FrameSyncLockStats;
    d1.timesSec = { 0.0, 1.0, 2.0, 3.0 };
    d1.lockPercent = { 90.0, 95.0, 100.0, 99.0 };
    d1.accumulatedMissedFrames = { 0.0, 1.0, 1.0, 2.0 };
    vm.addStreamData(d1);

    // A real, visible row must contribute well beyond the frame's own padding.
    const int frame_only = 2 * PlotConstants::kLegendContentMargin;
    QVERIFY(widget.m_legend_overlay->width()  > frame_only + 20);
    QVERIFY(widget.m_legend_overlay->height() > frame_only + 8);

    // Second stream triggers a full rebuild (old row torn down, two new rows
    // built) — this is the rebuild path that previously collapsed to 12x12.
    ProcessedStreamData d2;
    d2.streamLabel = "CH-02 PRN 15 20M";
    d2.pcmChannelId = 2;
    d2.mode = StreamMode::FrameSyncLockStats;
    d2.timesSec = { 0.0, 1.0, 2.0, 3.0 };
    d2.lockPercent = { 88.0, 93.0, 97.0, 96.0 };
    d2.accumulatedMissedFrames = { 0.0, 0.0, 1.0, 1.0 };
    vm.addStreamData(d2);

    QCOMPARE(widget.m_legend_rows->count(), 2);
    QVERIFY(widget.m_legend_overlay->width()  > frame_only + 20);
    QVERIFY(widget.m_legend_overlay->height() > frame_only + 8);
}

void TestPlotWidget::legendUsesShortNameForSnrSeries()
{
    // SNR series carry a long "<id> - <TMATS stream label> <ch.name>" identity for
    // CSV export/import (PlotViewModel::addStreamData), which can be verbose enough
    // to crowd the legend. The legend row should show a short "CH<id> <ch.name>"
    // form instead, without losing the channel/receiver suffix.
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);

    ProcessedStreamData d;
    d.streamLabel  = "CH-01 2250.5MHZ AGC 800Kbps RNRZ-L"; // long, descriptive TMATS name
    d.pcmChannelId = 40;
    d.mode         = StreamMode::ReceiverChannelInfo;
    d.timesSec     = { 0.0, 1.0, 2.0 };
    ProcessedChannelSeries ch;
    ch.name   = "L_RCVR3";
    ch.values = { -80.0, -79.0, -78.0 };
    d.channels.append(ch);
    vm.addStreamData(d);

    QCOMPARE(widget.m_legend_rows->count(), 1);
    auto* row = widget.m_legend_rows->itemAt(0)->widget();
    QVERIFY(row != nullptr);
    auto* label = qobject_cast<QLabel*>(row->layout()->itemAt(1)->widget());
    QVERIFY(label != nullptr);
    QCOMPARE(label->text(), QString("CH40 L_RCVR3"));
    QVERIFY(!label->text().contains("2250.5MHZ"));
}

void TestPlotWidget::legendReservesScrollbarGutter()
{
    // Regression guard: the legend's row layout must reserve a right-side gutter
    // matching the style's scrollbar width, so the vertical scrollbar (shown once
    // content overflows the height cap) never overlaps the last characters of a
    // row's label.
    PlotWidget widget;
    const int expected_gutter = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    QVERIFY(expected_gutter > 0);
    QCOMPARE(widget.m_legend_rows->contentsMargins().right(), expected_gutter);
}

void TestPlotWidget::legendRowsOverrideGlobalWidgetBackground()
{
    // Regression test: the app's global theme QSS (resources/win11-{dark,light}.qss)
    // gives every plain QWidget a solid background-color via a bare "QWidget {}"
    // selector, so each legend row (an unstyled QWidget wrapping the swatch+label)
    // would otherwise paint as an opaque chip against the translucent overlay,
    // producing a boxed/tabular look instead of just a line and text floating over
    // the plot. rebuildLegend() must give each row an objectName the overlay's
    // stylesheet can target with a more specific selector to force it transparent.
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);

    ProcessedStreamData d;
    d.streamLabel  = "Ch 7";
    d.pcmChannelId = 7;
    d.mode         = StreamMode::FrameSyncLockStats;
    d.timesSec                = { 0.0, 1.0, 2.0 };
    d.lockPercent             = { 90.0, 95.0, 100.0 };
    d.accumulatedMissedFrames = { 0.0, 1.0, 1.0 };
    vm.addStreamData(d);

    QCOMPARE(widget.m_legend_rows->count(), 1);
    auto* row = widget.m_legend_rows->itemAt(0)->widget();
    QVERIFY(row != nullptr);
    QCOMPARE(row->objectName(), QString("legendRow"));

    const QString stylesheet = widget.m_legend_overlay->styleSheet();
    QVERIFY(stylesheet.contains("QWidget#legendRow"));
    QVERIFY(stylesheet.contains("background: transparent"));
}

// Helper: a PlotWidget with one lock stream loaded, ready to export.
static void loadOneLockStream(PlotViewModel& vm)
{
    ProcessedStreamData d;
    d.streamLabel  = "Ch 5";
    d.pcmChannelId = 5;
    d.mode         = StreamMode::FrameSyncLockStats;
    d.timesSec                = { 0.0, 1.0, 2.0 };
    d.lockPercent             = { 90.0, 95.0, 100.0 };
    d.accumulatedMissedFrames = { 0.0, 1.0, 1.0 };
    vm.addStreamData(d);
}

void TestPlotWidget::exportImageWritesPngHeadlessly()
{
    // exportImage() is the headless entry point extracted from onExportPlot(): given
    // a path, it renders the chart (+legend) and writes the file with no dialog. This
    // is what the batch-export flow calls per file.
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    loadOneLockStream(vm);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("plot.png");

    QVERIFY(widget.exportImage(path));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(QFileInfo(path).size() > 0);
}

void TestPlotWidget::exportImageWritesSvgHeadlessly()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    loadOneLockStream(vm);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("plot.svg");

    QVERIFY(widget.exportImage(path));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(QFileInfo(path).size() > 0);
}

void TestPlotWidget::exportImageDefaultsUnknownSuffixToPdf()
{
    // An unrecognized suffix falls through to PDF, appending ".pdf" to the path.
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    loadOneLockStream(vm);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("plot.bogus");

    QVERIFY(widget.exportImage(path));
    QVERIFY(QFileInfo::exists(path + ".pdf"));
    QVERIFY(QFileInfo(path + ".pdf").size() > 0);
}


// ---------------------------------------------------------------------------
// Right-click context menu (replaces the old external control rows)
// ---------------------------------------------------------------------------

namespace {

/// Adds one frame-sync stream, which yields both a lock and a missed-frames
/// series (so the View Mode submenu has both metrics available).
void addLockStream(PlotViewModel& vm, const QString& label, int channel_id)
{
    ProcessedStreamData d;
    d.streamLabel  = label;
    d.pcmChannelId = channel_id;
    d.mode         = StreamMode::FrameSyncLockStats;
    d.timesSec                = { 0.0, 1.0, 2.0 };
    d.lockPercent             = { 90.0, 95.0, 100.0 };
    d.accumulatedMissedFrames = { 0.0, 1.0, 1.0 };
    vm.addStreamData(d);
}

/// @return the top-level action whose text starts with @p prefix, else nullptr.
QAction* findAction(QMenu* menu, const QString& prefix)
{
    const QList<QAction*> actions = menu->actions();
    for (QAction* a : actions)
    {
        if (a->text().startsWith(prefix))
            return a;
    }
    return nullptr;
}

} // namespace

void TestPlotWidget::contextMenuListsExpectedTopLevelItems()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);

    QScopedPointer<QMenu> menu(widget.buildContextMenu());
    QVERIFY(!menu.isNull());

    // Set Plot Title leads the menu (the most-used entry when preparing a plot
    // for a report), so its position is pinned.
    QVERIFY(!menu->actions().isEmpty());
    QVERIFY(menu->actions().first()->text().startsWith("Set Plot Title"));

    // Every control that used to live in the external rows must have a home here.
    QVERIFY(findAction(menu.data(), "Plot File") != nullptr);
    QVERIFY(findAction(menu.data(), "View Mode") != nullptr);
    QVERIFY(findAction(menu.data(), "Customize View") != nullptr);
    QVERIFY(findAction(menu.data(), "Set Plot Title") != nullptr);
    QVERIFY(findAction(menu.data(), "X Axis") != nullptr);
    QVERIFY(findAction(menu.data(), "Y Axes") != nullptr);
    QVERIFY(findAction(menu.data(), "Export") != nullptr);
}

void TestPlotWidget::contextMenuItemsDisabledUntilDataLoads()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);

    // No data: the data-dependent entries are disabled (the old widgets were
    // likewise disabled until a file loaded).
    {
        QScopedPointer<QMenu> menu(widget.buildContextMenu());
        QVERIFY(!findAction(menu.data(), "Customize View")->isEnabled());
        QVERIFY(!findAction(menu.data(), "Set Plot Title")->isEnabled());
        QVERIFY(!findAction(menu.data(), "X Axis")->isEnabled());
        QVERIFY(!findAction(menu.data(), "Y Axes")->isEnabled());
        QVERIFY(!findAction(menu.data(), "Export")->isEnabled());
    }

    addLockStream(vm, "Ch 5", 5);

    // With data they become available.
    {
        QScopedPointer<QMenu> menu(widget.buildContextMenu());
        QVERIFY(findAction(menu.data(), "Customize View")->isEnabled());
        QVERIFY(findAction(menu.data(), "Set Plot Title")->isEnabled());
        QVERIFY(findAction(menu.data(), "X Axis")->isEnabled());
        QVERIFY(findAction(menu.data(), "Y Axes")->isEnabled());
        QVERIFY(findAction(menu.data(), "Export")->isEnabled());
    }
}

void TestPlotWidget::contextMenuPlotFileSubmenuTracksSources()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);

    // A single source has nothing to switch between, so Plot File stays disabled
    // (this mirrors the old combo box's behavior).
    addLockStream(vm, "Ch 5", 5);
    {
        QScopedPointer<QMenu> menu(widget.buildContextMenu());
        QVERIFY(!findAction(menu.data(), "Plot File")->isEnabled());
    }

    // A second source enables it and lists "All files (overlaid)" plus one entry
    // per source; triggering an entry filters the plot to that source.
    ProcessedStreamData d;
    d.streamLabel  = "Ch 6";
    d.pcmChannelId = 6;
    d.sourceId     = 1;
    d.mode         = StreamMode::FrameSyncLockStats;
    d.timesSec                = { 0.0, 1.0 };
    d.lockPercent             = { 80.0, 85.0 };
    d.accumulatedMissedFrames = { 0.0, 0.0 };
    vm.addStreamData(d);

    QScopedPointer<QMenu> menu(widget.buildContextMenu());
    QAction* file_act = findAction(menu.data(), "Plot File");
    QVERIFY(file_act->isEnabled());
    QMenu* file_menu = file_act->menu();
    QVERIFY(file_menu != nullptr);

    QAction* all_act = findAction(file_menu, "All files");
    QVERIFY(all_act != nullptr);
    QVERIFY(all_act->isChecked());  // default view is every source overlaid

    // The last action is the second source; selecting it isolates that source.
    QAction* last = file_menu->actions().last();
    last->trigger();
    QCOMPARE(vm.visibleSource(), 1);
}

void TestPlotWidget::contextMenuViewModeReflectsAndSetsMode()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    addLockStream(vm, "Ch 5", 5);

    QScopedPointer<QMenu> menu(widget.buildContextMenu());
    QAction* mode_act = findAction(menu.data(), "View Mode");
    QVERIFY(mode_act->isEnabled());   // stream produced both left-axis metrics
    QMenu* mode_menu = mode_act->menu();
    QVERIFY(mode_menu != nullptr);

    QAction* lock_act  = findAction(mode_menu, "Lock Percentage");
    QAction* accum_act = findAction(mode_menu, "Accumulation");
    QVERIFY(lock_act != nullptr && accum_act != nullptr);

    // Reflects the ViewModel's current mode...
    QCOMPARE(vm.lockAxisView(), PlotViewModel::LockAxisView::LockPercent);
    QVERIFY(lock_act->isChecked());
    QVERIFY(!accum_act->isChecked());

    // ...and setting it round-trips to the ViewModel.
    accum_act->trigger();
    QCOMPARE(vm.lockAxisView(), PlotViewModel::LockAxisView::MissedFrames);
}

void TestPlotWidget::contextMenuResetActionsClearAxisOverrides()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    addLockStream(vm, "Ch 5", 5);

    // Narrow the X window and pin both Y maxima, as the menu dialogs would.
    vm.setXViewRange(0.5, 1.5);
    vm.setLeftYMaxOverride(42.0);
    vm.setRightYMaxOverride(24.0);
    QVERIFY(vm.hasLeftYMaxOverride());
    QVERIFY(vm.hasRightYMaxOverride());

    QScopedPointer<QMenu> menu(widget.buildContextMenu());

    // X Axis > Reset Span restores the full data span...
    QMenu* x_menu = findAction(menu.data(), "X Axis")->menu();
    findAction(x_menu, "Reset Span")->trigger();
    QCOMPARE(vm.xViewMin(), vm.xMin());
    QCOMPARE(vm.xViewMax(), vm.xMax());

    // ...and Y Axes > Reset clears BOTH overrides (the old single Reset button
    // did the X and Y halves together; they are separate menu items now).
    QMenu* y_menu = findAction(menu.data(), "Y Axes")->menu();
    findAction(y_menu, "Reset")->trigger();
    QVERIFY(!vm.hasLeftYMaxOverride());
    QVERIFY(!vm.hasRightYMaxOverride());
}

void TestPlotWidget::contextMenuSetTitleAppliesToViewModel()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    addLockStream(vm, "Ch 5", 5);

    // The dialog itself is modal, so drive the ViewModel path the action uses.
    // (The chart title element must follow the ViewModel now that the external
    // title text box is gone.)
    vm.setPlotTitle("Flight 12 - Lock");
    QCOMPARE(vm.plotTitle(), QString("Flight 12 - Lock"));
}

void TestPlotWidget::wheelZoomAndDragPanRemainEnabled()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);

    // Regression guard for the toolbar removal: mouse interaction is what is left
    // for navigating the plot, so it must switch on once data arrives, and stay
    // horizontal-only (X zoom/pan).
    QVERIFY(!widget.m_plot->interactions().testFlag(QCP::iRangeZoom));

    addLockStream(vm, "Ch 5", 5);

    QVERIFY(widget.m_plot->interactions().testFlag(QCP::iRangeZoom));
    QVERIFY(widget.m_plot->interactions().testFlag(QCP::iRangeDrag));
    QCOMPARE(widget.m_plot->axisRect()->rangeZoom(), Qt::Horizontal);
    QCOMPARE(widget.m_plot->axisRect()->rangeDrag(), Qt::Horizontal);
}

void TestPlotWidget::noExternalControlWidgetsRemain()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    addLockStream(vm, "Ch 5", 5);

    // The point of the redesign: the chart is the whole surface. No combo boxes,
    // spin boxes, line edits or push buttons may live outside the chart - every
    // control moved into the right-click menu. (The legend overlay's labels are
    // QLabels, which is why only interactive control types are asserted on.)
    // Dialogs spawned by menu actions are separate top-level windows, so they are
    // not children of this widget and don't trip this check.
    // Interactive controls are allowed ON the chart (overlays like the legend
    // toggle) but never outside it, so each hit must be a descendant of m_plot.
    auto isChartOverlay = [&widget](QWidget* w) {
        for (QWidget* a = w->parentWidget(); a != nullptr; a = a->parentWidget())
        {
            if (a == widget.m_plot)
                return true;
        }
        return false;
    };

    QList<QWidget*> controls;
    for (QWidget* w : widget.findChildren<QComboBox*>())       controls << w;
    for (QWidget* w : widget.findChildren<QDoubleSpinBox*>())   controls << w;
    for (QWidget* w : widget.findChildren<QSpinBox*>())         controls << w;
    for (QWidget* w : widget.findChildren<QLineEdit*>())        controls << w;
    for (QWidget* w : widget.findChildren<QAbstractButton*>())  controls << w;

    for (QWidget* w : controls)
    {
        QVERIFY2(isChartOverlay(w),
                 qPrintable(QString("Control '%1' (%2) lives outside the chart; every control "
                                    "must be an on-chart overlay or a context-menu item.")
                                .arg(w->objectName(), QString::fromLatin1(w->metaObject()->className()))));
    }
}

void TestPlotWidget::legendToggleShowsAndHidesLegend()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    widget.setLegendVisible(true);   // known starting state (the pref is persisted)
    addLockStream(vm, "Ch 5", 5);

    QVERIFY(!widget.m_legend_overlay->isHidden());

    // Hiding via the on-chart toggle takes the legend off the chart - and with it
    // out of exported images, since exportImage only composites a visible overlay.
    widget.setLegendVisible(false);
    QVERIFY(widget.m_legend_overlay->isHidden());
    QVERIFY(!widget.m_legend_toggle->isChecked());

    // A rebuild (e.g. another stream finishing) must not resurrect it.
    addLockStream(vm, "Ch 6", 6);
    QVERIFY(widget.m_legend_overlay->isHidden());

    widget.setLegendVisible(true);
    QVERIFY(!widget.m_legend_overlay->isHidden());
    QVERIFY(widget.m_legend_toggle->isChecked());
}

void TestPlotWidget::legendToggleAppearsOnlyWithData()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);

    // Nothing plotted yet: the chip bar carrying the toggle is hidden, so the
    // toggle is off the chart. Asserted on the bar's own hidden flag because that
    // is the widget the code shows/hides - and isVisible() would be false here
    // regardless, since this test never show()s the top-level widget.
    QVERIFY(widget.m_overlay_bar->isHidden());

    addLockStream(vm, "Ch 5", 5);
    QVERIFY(!widget.m_overlay_bar->isHidden());
    QVERIFY(!widget.m_legend_toggle->isHidden());  // not hidden in its own right

    // It is an overlay on the chart (inside the chip bar), not an external control.
    QCOMPARE(widget.m_legend_toggle->parentWidget(), widget.m_overlay_bar);
    QCOMPARE(widget.m_overlay_bar->parentWidget(), static_cast<QWidget*>(widget.m_plot));
}

void TestPlotWidget::contextMenuShowLegendMirrorsToggle()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    widget.setLegendVisible(true);
    addLockStream(vm, "Ch 5", 5);

    // The menu entry reflects the current state...
    {
        QScopedPointer<QMenu> menu(widget.buildContextMenu());
        QAction* act = findAction(menu.data(), "Show Legend");
        QVERIFY(act != nullptr);
        QVERIFY(act->isCheckable());
        QVERIFY(act->isChecked());

        // ...and toggling it drives the same path as the on-chart button.
        // trigger() on a checkable action flips the state itself and emits
        // triggered(newState), so it must NOT be pre-set here.
        act->trigger();
    }
    QVERIFY(widget.m_legend_overlay->isHidden());
    QVERIFY(!widget.m_legend_toggle->isChecked());

    // Reopening the menu shows the updated state (it is rebuilt per request).
    QScopedPointer<QMenu> menu2(widget.buildContextMenu());
    QVERIFY(!findAction(menu2.data(), "Show Legend")->isChecked());

    widget.setLegendVisible(true);   // leave the persisted pref as we found it
}

void TestPlotWidget::overlayBarHoldsChipsAndIsChartParented()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);

    // All persistent on-chart controls live in ONE bar rather than being scattered
    // around the chart, and the bar is parented to the chart itself.
    QVERIFY(widget.m_overlay_bar != nullptr);
    QCOMPARE(widget.m_overlay_bar->parentWidget(), static_cast<QWidget*>(widget.m_plot));
    QCOMPARE(widget.m_legend_toggle->parentWidget(),  widget.m_overlay_bar);
    QCOMPARE(widget.m_view_mode_chip->parentWidget(), widget.m_overlay_bar);
    QCOMPARE(widget.m_reset_chip->parentWidget(),     widget.m_overlay_bar);

    // Nothing shows before there is data to interact with.
    QVERIFY(widget.m_overlay_bar->isHidden());

    addLockStream(vm, "Ch 5", 5);
    QVERIFY(!widget.m_overlay_bar->isHidden());
}

void TestPlotWidget::viewModeChipSwitchesLeftAxisMetric()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    addLockStream(vm, "Ch 5", 5);   // yields both left-axis metrics

    QVERIFY(!widget.m_view_mode_chip->isHidden());
    // The chip names the metric it switches TO, so the outcome is readable.
    QCOMPARE(vm.lockAxisView(), PlotViewModel::LockAxisView::LockPercent);
    QCOMPARE(widget.m_view_mode_chip->text(), QString("Accumulation"));

    widget.m_view_mode_chip->click();
    QCOMPARE(vm.lockAxisView(), PlotViewModel::LockAxisView::MissedFrames);
    QCOMPARE(widget.m_view_mode_chip->text(), QString("Lock %"));

    widget.m_view_mode_chip->click();
    QCOMPARE(vm.lockAxisView(), PlotViewModel::LockAxisView::LockPercent);
}

void TestPlotWidget::resetChipAppearsOnlyWhenViewChanged()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    addLockStream(vm, "Ch 5", 5);

    // At the default full-span view with automatic axes the chip adds nothing, so
    // it stays hidden - that is what keeps the overlay uncluttered at rest.
    QVERIFY(widget.m_reset_chip->isHidden());

    vm.setXViewRange(0.5, 1.5);
    QVERIFY(!widget.m_reset_chip->isHidden());

    widget.m_reset_chip->click();
    QCOMPARE(vm.xViewMin(), vm.xMin());
    QCOMPARE(vm.xViewMax(), vm.xMax());
    QVERIFY(widget.m_reset_chip->isHidden());

    // A pinned axis maximum also counts as "changed from default".
    vm.setLeftYMaxOverride(42.0);
    QVERIFY(!widget.m_reset_chip->isHidden());
    widget.m_reset_chip->click();
    QVERIFY(!vm.hasLeftYMaxOverride());
    QVERIFY(widget.m_reset_chip->isHidden());
}

void TestPlotWidget::bandZoomAppliesDraggedRange()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    addLockStream(vm, "Ch 5", 5);   // spans 0..2 s

    // A dragged range zooms to exactly that span, in either drag direction.
    widget.applyBandZoom(0.5, 1.5);
    QCOMPARE(vm.xViewMin(), 0.5);
    QCOMPARE(vm.xViewMax(), 1.5);

    vm.resetXRange();
    widget.applyBandZoom(1.5, 0.5);          // dragged right-to-left
    QCOMPARE(vm.xViewMin(), 0.5);
    QCOMPARE(vm.xViewMax(), 1.5);

    // A click (zero-width drag) must not collapse the view to nothing.
    vm.resetXRange();
    widget.applyBandZoom(1.0, 1.0);
    QCOMPARE(vm.xViewMin(), vm.xMin());
    QCOMPARE(vm.xViewMax(), vm.xMax());
}

void TestPlotWidget::doubleClickResetsSpanViaViewModel()
{
    PlotViewModel vm;
    PlotWidget widget;
    widget.setViewModel(&vm);
    addLockStream(vm, "Ch 5", 5);

    vm.setXViewRange(0.5, 1.5);
    QVERIFY(!qFuzzyCompare(vm.xViewMin(), vm.xMin()));

    // The chart's double-click handler restores the full span (same result the
    // Reset chip and X Axis > Reset Span produce).
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(10, 10), QPointF(10, 10),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    emit widget.m_plot->mouseDoubleClick(&dbl);

    QCOMPARE(vm.xViewMin(), vm.xMin());
    QCOMPARE(vm.xViewMax(), vm.xMax());
}
