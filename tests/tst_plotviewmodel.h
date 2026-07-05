/**
 * @file tst_plotviewmodel.h
 * @brief Unit tests for PlotViewModel — CSV parsing, series data, axis ranges.
 */

#ifndef TST_PLOTVIEWMODEL_H
#define TST_PLOTVIEWMODEL_H

#include <QObject>

class TestPlotViewModel : public QObject
{
    Q_OBJECT

private slots:
    void defaultState();
    void loadCsvFile();
    void csvTimeConversion();
    void seriesColorAssignment();
    void yAutoRange();
    void yManualRange();
    void xTimeWindow();
    void seriesVisibility();
    void clearData();
    void plotTitleDefault();
    void plotTitleChange();
    void loadInvalidFile();
    void loadEmptyFile();
    void formatTimeZeroElapsed();
    void formatTimeDayBoundary();
    void formatTimeNegativeElapsed();
    void loadCsvHeaderOnly();
    void loadCsvMalformedRows();
    void lockSeriesMetricType();
    void lockSeriesColor();
    void importMultiStreamColorsAndVisibility();
    void importSnrStreamGrouping();
    void importExportRoundTrip();
    void lockAxisRange();
    void hasLockSeriesTrue();
    void hasLockSeriesFalse();
    void yAutoRangeIgnoresLockSeries();
    void loadCsvFileAsyncEmitsDataChanged();
    void loadCsvFileAsyncEmitsLoadFailed();

    // In-memory addStreamData tests
    void addStreamDataLockSeriesCreated();
    void addStreamDataSNRSeriesCreated();
    void addStreamDataMultipleStreamsAccumulate();
    void addStreamDataEmptyDataNoOp();

    // Frame sync error accumulation (US2.1) tests
    void addStreamDataErrorSeriesCreated();
    void errorSeriesSharesLockColor();
    void setLockAxisViewTogglesVisibility();
    void setLockAxisViewPreservesStreamSelection();
    void frameSyncErrorMaxReflectsData();

    // Stream identity: two streams sharing a TMATS-derived streamLabel must stay
    // distinct by streamOrder (pcmChannelId) — regression coverage for a
    // pre-v2.6.0 cross-contamination bug (reprocess-replace and rename/recolor
    // sibling-sync all matched on streamLabel alone).
    void reprocessOnlySameStreamOrderReplaced();
    void renameSeriesOnlySameStreamOrderSiblingRenamed();
    void recolorSeriesOnlySameStreamOrderSiblingRecolored();

    // exportCsv tests
    void exportCsvCreatesFile();
    void exportCsvHeaderAndData();
    void exportCsvEmptyNoFile();
    void exportCsvIncludesErrorColumn();
    void exportCsvRoundTripsThroughLoad();
};

#endif // TST_PLOTVIEWMODEL_H
