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

    // exportCsv tests
    void exportCsvCreatesFile();
    void exportCsvHeaderAndData();
    void exportCsvEmptyNoFile();
};

#endif // TST_PLOTVIEWMODEL_H
