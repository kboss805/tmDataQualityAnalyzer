#include "tst_constants.h"

#include <QtTest>

#include "plotconstants.h"

// The constants in constants.h are pinned by the native suite (native/tmdq_core_tests/test_constants.cpp); this one
// keeps plotconstants.h, which is Qt (QColor, QString).

void TestConstants::plotConstants()
{
    QCOMPARE(PlotConstants::kPlotDockMinWidth, 1024);
    QCOMPARE(PlotConstants::kAxisMarginFactor, 0.05);
    QCOMPARE(PlotConstants::kLockAxisMin, 0.0);
    QCOMPARE(PlotConstants::kLockAxisMax, 100.0);
    QCOMPARE(QString(PlotConstants::kDefaultPlotTitle), QString("Framesync/SNR Plot"));
    QCOMPARE(QString(PlotConstants::kYAxisLabel),  QString("Framesync Lock (%)"));
    QCOMPARE(QString(PlotConstants::kMissedFramesAxisLabel), QString("Accumulated Missed Frames"));
    QCOMPARE(QString(PlotConstants::kSnrAxisLabel), QString("Receiver SNR (dB)"));
    QCOMPARE(QString(PlotConstants::kXAxisLabel), QString("Time (DDD:HH:MM:SS)"));
    QCOMPARE(PlotConstants::kNumSnrPrimaryColors, 3);

    // Theme colors
    QCOMPARE(PlotConstants::kDarkBackground, QColor(32, 32, 32));
    QCOMPARE(PlotConstants::kLightBackground, QColor(255, 255, 255));
    QCOMPARE(PlotConstants::kDarkForeground, QColor(220, 220, 220));
    QCOMPARE(PlotConstants::kLightForeground, QColor(30, 30, 30));
    QCOMPARE(PlotConstants::kDarkGridColor, QColor(60, 60, 60));
    QCOMPARE(PlotConstants::kLightGridColor, QColor(200, 200, 200));

    // Plot widget parameters
    QCOMPARE(PlotConstants::kTickCount, 10);
    QCOMPARE(PlotConstants::kGraphPenWidth, 1.5);
}
