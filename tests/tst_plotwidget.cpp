/**
 * @file tst_plotwidget.cpp
 * @brief Smoke tests for PlotWidget — construction, ViewModel connection, theme.
 */

#include "tst_plotwidget.h"

#include <QFrame>
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
