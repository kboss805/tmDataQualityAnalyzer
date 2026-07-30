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
    void legendUsesShortNameForSnrSeries();
    void legendReservesScrollbarGutter();
    void legendRowsOverrideGlobalWidgetBackground();
    void exportImageWritesPngHeadlessly();
    void exportImageWritesSvgHeadlessly();
    void exportImageDefaultsUnknownSuffixToPdf();
    void contextMenuListsExpectedTopLevelItems();
    void contextMenuItemsDisabledUntilDataLoads();
    void contextMenuPlotFileSubmenuTracksSources();
    void contextMenuViewModeReflectsAndSetsMode();
    void contextMenuResetActionsClearAxisOverrides();
    void contextMenuSetTitleAppliesToViewModel();
    void wheelZoomAndDragPanRemainEnabled();
    void noExternalControlWidgetsRemain();
    void legendToggleShowsAndHidesLegend();
    void legendToggleAppearsOnlyWithData();
    void legendChipIsLabelledAndDescribed();
    void contextMenuShowLegendMirrorsToggle();
    void overlayBarHoldsChipsAndIsChartParented();
    void viewModeChipSwitchesLeftAxisMetric();
    void resetChipAppearsOnlyWhenViewChanged();
    void bandZoomAppliesDraggedRange();
    void doubleClickResetsSpanViaViewModel();
    void readoutMenuListsVisibleSeriesAndPins();
    void readoutPinDropsWhenSeriesGoesAway();
    void keyboardShortcutsDriveTheView();
    void keyboardShortcutsIgnoredWithoutData();
    void keyboardShortcutsAreWidgetScopedNotApplicationWide();
};

#endif // TST_PLOTWIDGET_H
