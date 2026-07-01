/**
 * @file tst_plotwidget.cpp
 * @brief Smoke tests for PlotWidget — construction, ViewModel connection, theme.
 */

#include "tst_plotwidget.h"

#include <QFrame>
#include <QVBoxLayout>
#include <QtTest>

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
