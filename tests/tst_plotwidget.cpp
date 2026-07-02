/**
 * @file tst_plotwidget.cpp
 * @brief Smoke tests for PlotWidget — construction, ViewModel connection, theme.
 */

#include "tst_plotwidget.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
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
