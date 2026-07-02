/**
 * @file tst_plotwidget.h
 * @brief Smoke tests for PlotWidget — construction, ViewModel connection, theme.
 */

#ifndef TST_PLOTWIDGET_H
#define TST_PLOTWIDGET_H

#include <QObject>

class TestPlotWidget : public QObject
{
    Q_OBJECT

private slots:
    void constructsWithoutCrash();
    void setViewModelWithNullDoesNotCrash();
    void setViewModelConnectsWithoutCrash();
    void applyThemeDarkDoesNotCrash();
    void applyThemeLightDoesNotCrash();
    void legendOverlayPopulatesFromData();
    void legendOverlaySizesCorrectlyAfterRebuild();
};

#endif // TST_PLOTWIDGET_H
