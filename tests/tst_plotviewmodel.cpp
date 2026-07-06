/**
 * @file tst_plotviewmodel.cpp
 * @brief Implementation of PlotViewModel unit tests.
 */

#include "tst_plotviewmodel.h"

#include <QEventLoop>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>
#include <QtTest>

#include "constants.h"
#include "plotviewmodel.h"
#include "processedstreamdata.h"
#include "streamconfig.h"

/// Helper: writes CSV content to a temp file and returns its path.
/// The caller is responsible for deleting the file.
static QString writeTempCsv(const QString& content)
{
    QTemporaryFile file;
    file.setAutoRemove(false);
    file.setFileTemplate(QDir::tempPath() + "/tst_plot_XXXXXX.csv");
    if (!file.open())
        return {};
    QTextStream stream(&file);
    stream << content;
    file.close();
    return file.fileName();
}

void TestPlotViewModel::defaultState()
{
    PlotViewModel vm;
    QVERIFY(!vm.hasData());
    QCOMPARE(vm.seriesCount(), 0);
    QCOMPARE(vm.plotTitle(), QString(PlotConstants::kDefaultPlotTitle));
    QVERIFY(vm.yAutoScale());
}

void TestPlotViewModel::loadCsvFile()
{
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1,R_RCVR1,L_RCVR2\n"
        "45:10:00:00.000,-80.5,-75.2,-90.1\n"
        "45:10:00:01.000,-80.3,-75.0,-89.8\n"
        "45:10:00:02.000,-80.1,-74.8,-89.5\n";
    QString path = writeTempCsv(csv);
    QVERIFY(!path.isEmpty());

    PlotViewModel vm;
    QSignalSpy spy(&vm, &PlotViewModel::dataChanged);

    QVERIFY(vm.loadCsvFile(path));
    QCOMPARE(spy.count(), 1);
    QVERIFY(vm.hasData());
    QCOMPARE(vm.seriesCount(), 3);

    // Verify series names
    QCOMPARE(vm.seriesAt(0).name, QString("L_RCVR1"));
    QCOMPARE(vm.seriesAt(1).name, QString("R_RCVR1"));
    QCOMPARE(vm.seriesAt(2).name, QString("L_RCVR2"));

    // Verify data point count
    QCOMPARE(vm.seriesAt(0).xValues.size(), 3);
    QCOMPARE(vm.seriesAt(0).yValues.size(), 3);

    // Verify first Y value
    QCOMPARE(vm.seriesAt(0).yValues[0], -80.5);

    QFile::remove(path);
}

void TestPlotViewModel::csvTimeConversion()
{
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "45:10:00:00.000,-80.0\n"
        "45:10:00:05.500,-79.0\n"
        "46:10:00:00.000,-78.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    // First sample: elapsed = 0.0
    QCOMPARE(vm.seriesAt(0).xValues[0], 0.0);
    // Second sample: 5.5 seconds later
    QCOMPARE(vm.seriesAt(0).xValues[1], 5.5);
    // Third sample: next day same time = 86400.0 seconds later
    QCOMPARE(vm.seriesAt(0).xValues[2], 86400.0);

    // X range should span from 0 to 86400.0
    QCOMPARE(vm.xMin(), 0.0);
    QCOMPARE(vm.xMax(), 86400.0);

    QFile::remove(path);
}

void TestPlotViewModel::seriesColorAssignment()
{
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1,R_RCVR1,L_RCVR2\n"
        "1:00:00:00.000,-80.0,-75.0,-90.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    // Same receiver (RCVR1) channels should share hue
    QColor c0 = vm.seriesAt(0).color; // L_RCVR1
    QColor c1 = vm.seriesAt(1).color; // R_RCVR1
    QColor c2 = vm.seriesAt(2).color; // L_RCVR2

    // RCVR1 channels share base hue
    QCOMPARE(c0.hue(), c1.hue());
    // RCVR2 has a different hue
    QVERIFY(c0.hue() != c2.hue());

    // Second channel of same receiver has lower saturation
    QVERIFY(c1.saturation() < c0.saturation());

    QFile::remove(path);
}

void TestPlotViewModel::yAutoRange()
{
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "1:00:00:00.000,12.3\n"
        "1:00:00:01.000,47.8\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    // Rounded to nearest 5 dB, clipped at 0: floor(12.3/5)*5=10, ceil(47.8/5)*5=50
    double expected_min = 10.0;
    double expected_max = 50.0;
    QCOMPARE(vm.yMin(), expected_min);
    QCOMPARE(vm.yMax(), expected_max);
    QCOMPARE(vm.dataYMin(), expected_min);
    QCOMPARE(vm.dataYMax(), expected_max);

    QFile::remove(path);
}

void TestPlotViewModel::yManualRange()
{
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "1:00:00:00.000,-100.0\n"
        "1:00:00:01.000,-50.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    QSignalSpy spy(&vm, &PlotViewModel::axisRangeChanged);

    // Set manual range
    vm.setYManualRange(-120.0, -30.0);
    QVERIFY(!vm.yAutoScale());
    QCOMPARE(vm.yMin(), -120.0);
    QCOMPARE(vm.yMax(), -30.0);
    QCOMPARE(spy.count(), 1);

    // Reset to auto
    vm.resetYRange();
    QVERIFY(vm.yAutoScale());
    // Should revert to auto-computed range
    QVERIFY(vm.yMin() > -120.0);
    QCOMPARE(spy.count(), 2);

    QFile::remove(path);
}

void TestPlotViewModel::xTimeWindow()
{
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "1:00:00:00.000,-80.0\n"
        "1:00:01:00.000,-75.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    QSignalSpy spy(&vm, &PlotViewModel::axisRangeChanged);

    // Set X view to 10-30 seconds
    vm.setXViewRange(10.0, 30.0);
    QCOMPARE(vm.xViewMin(), 10.0);
    QCOMPARE(vm.xViewMax(), 30.0);
    QCOMPARE(spy.count(), 1);

    // Reset
    vm.resetXRange();
    QCOMPARE(vm.xViewMin(), vm.xMin());
    QCOMPARE(vm.xViewMax(), vm.xMax());
    QCOMPARE(spy.count(), 2);

    QFile::remove(path);
}

void TestPlotViewModel::seriesVisibility()
{
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1,R_RCVR1\n"
        "1:00:00:00.000,-80.0,-75.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    QSignalSpy spy(&vm, &PlotViewModel::seriesVisibilityChanged);

    // Both visible by default
    QVERIFY(vm.seriesAt(0).visible);
    QVERIFY(vm.seriesAt(1).visible);

    // Hide first series
    vm.setSeriesVisible(0, false);
    QVERIFY(!vm.seriesAt(0).visible);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);

    // Out-of-bounds is a no-op
    vm.setSeriesVisible(-1, false);
    vm.setSeriesVisible(99, false);
    QCOMPARE(spy.count(), 1);

    QFile::remove(path);
}

void TestPlotViewModel::clearData()
{
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "1:00:00:00.000,-80.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));
    QVERIFY(vm.hasData());

    QSignalSpy spy(&vm, &PlotViewModel::dataChanged);
    vm.clearData();

    QVERIFY(!vm.hasData());
    QCOMPARE(vm.seriesCount(), 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(vm.plotTitle(), QString(PlotConstants::kDefaultPlotTitle));

    QFile::remove(path);
}

void TestPlotViewModel::plotTitleDefault()
{
    PlotViewModel vm;
    QCOMPARE(vm.plotTitle(), QString(PlotConstants::kDefaultPlotTitle));
}

void TestPlotViewModel::plotTitleChange()
{
    PlotViewModel vm;
    QSignalSpy spy(&vm, &PlotViewModel::plotTitleChanged);

    vm.setPlotTitle("My Custom Title");
    QCOMPARE(vm.plotTitle(), QString("My Custom Title"));
    QCOMPARE(spy.count(), 1);

    // Setting same value is a no-op
    vm.setPlotTitle("My Custom Title");
    QCOMPARE(spy.count(), 1);
}

void TestPlotViewModel::loadInvalidFile()
{
    PlotViewModel vm;
    QVERIFY(!vm.loadCsvFile("/nonexistent/path.csv"));
    QVERIFY(!vm.hasData());
}

void TestPlotViewModel::loadEmptyFile()
{
    QString path = writeTempCsv("");
    PlotViewModel vm;
    QVERIFY(!vm.loadCsvFile(path));
    QVERIFY(!vm.hasData());
    QFile::remove(path);
}

void TestPlotViewModel::formatTimeZeroElapsed()
{
    // Load CSV to set base day/time, then test formatTime at zero elapsed
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "45:10:30:15.000,-80.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    // At elapsed=0, should return base time: 045:10:30:15
    QString result = vm.formatTime(0.0);
    QCOMPARE(result, QString("045:10:30:15"));

    QFile::remove(path);
}

void TestPlotViewModel::formatTimeDayBoundary()
{
    // Test elapsed seconds crossing into the next day
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "45:23:59:50.000,-80.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    // 10 seconds after 23:59:50 = next day 00:00:00
    QString result = vm.formatTime(10.0);
    QCOMPARE(result, QString("046:00:00:00"));

    // 70 seconds after 23:59:50 = next day 00:01:00
    result = vm.formatTime(70.0);
    QCOMPARE(result, QString("046:00:01:00"));

    QFile::remove(path);
}

void TestPlotViewModel::formatTimeNegativeElapsed()
{
    // Negative elapsed should wrap to previous day
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "45:00:00:30.000,-80.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    // -30 seconds from 00:00:30 = 00:00:00 same day
    QString result = vm.formatTime(-30.0);
    QCOMPARE(result, QString("045:00:00:00"));

    // -31 seconds from 00:00:30 = previous day 23:59:59
    result = vm.formatTime(-31.0);
    QCOMPARE(result, QString("044:23:59:59"));

    QFile::remove(path);
}

void TestPlotViewModel::loadCsvHeaderOnly()
{
    // CSV with header but no data rows
    QString csv = "Time (DOY:HH:MM:SS.mmm),L_RCVR1,R_RCVR1\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    bool loaded = vm.loadCsvFile(path);

    // Should either fail or load with no data points
    if (loaded)
    {
        // If it loaded, series should have no data points
        for (int i = 0; i < vm.seriesCount(); i++)
        {
            QVERIFY(vm.seriesAt(i).xValues.isEmpty());
        }
    }
    else
    {
        QVERIFY(!vm.hasData());
    }

    QFile::remove(path);
}

void TestPlotViewModel::loadCsvMalformedRows()
{
    // CSV with three valid rows and two malformed ones: a too-short row and a
    // row whose combined timestamp is empty/unparseable.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "45:10:00:00.000,-80.0\n"
        "short_row\n"
        "45:10:00:02.000,-78.0\n"
        ",-999.0\n"
        "45:10:00:04.000,-76.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QSignalSpy warn_spy(&vm, &PlotViewModel::loadWarning);
    QVERIFY(vm.loadCsvFile(path));

    // The three valid rows load; the two malformed rows are skipped, not crashed on.
    QVERIFY(vm.hasData());
    QCOMPARE(vm.seriesAt(0).xValues.size(), 3);

    // C7: the skipped rows are surfaced via loadWarning rather than silently dropped.
    QCOMPARE(warn_spy.count(), 1);
    QVERIFY2(warn_spy.at(0).at(0).toString().contains("2"),
             "Warning must report the count (2) of skipped malformed rows");

    QFile::remove(path);
}

void TestPlotViewModel::lockSeriesMetricType()
{
    // A column qualified with the " Lock (%)" suffix must give that series
    // MetricType::FrameSyncLock, with the suffix stripped back to the bare legend
    // name. All other columns must remain MetricType::SNR.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),Ch1 Lock (%),L_RCVR1\n"
        "1:00:00:00.000,95.0,-80.0\n"
        "1:00:00:01.000,90.0,-79.5\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));
    QCOMPARE(vm.seriesCount(), 2);

    QCOMPARE(vm.seriesAt(0).name, QString("Ch1"));
    QCOMPARE(static_cast<int>(vm.seriesAt(0).metricType),
             static_cast<int>(PlotSeriesData::MetricType::FrameSyncLock));

    QCOMPARE(vm.seriesAt(1).name, QString("L_RCVR1"));
    QCOMPARE(static_cast<int>(vm.seriesAt(1).metricType),
             static_cast<int>(PlotSeriesData::MetricType::SNR));

    QFile::remove(path);
}

void TestPlotViewModel::lockSeriesColor()
{
    // Lock series must be assigned kFrameSyncLockColor, not a receiver-palette color.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),Ch1 Lock (%),L_RCVR1\n"
        "1:00:00:00.000,87.5,-80.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    QColor lock_color = vm.seriesAt(0).color;
    QCOMPARE(lock_color, PlotConstants::kFrameSyncLockColor);

    // SNR series must NOT share the lock color
    QColor snr_color = vm.seriesAt(1).color;
    QVERIFY(snr_color != PlotConstants::kFrameSyncLockColor);

    QFile::remove(path);
}

void TestPlotViewModel::importMultiStreamColorsAndVisibility()
{
    // Regression: an imported CSV must look like the live (processed) plot —
    //   1. each stream's left-axis curve gets its own color (not all collapsed
    //      onto one), and a stream's lock + missed-frames siblings share a color,
    //   2. only the active left-axis metric (lock %) is visible, so lock curves
    //      are not overdrawn by accumulated-missed-frames curves.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),"
        "Stream1 Lock (%),Stream1 Accumulated Missed Frames,"
        "Stream2 Lock (%),Stream2 Accumulated Missed Frames\n"
        "1:00:00:00.000,100.0,0,98.0,1\n"
        "1:00:00:01.000,99.0,2,97.0,3\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));
    QCOMPARE(vm.seriesCount(), 4);

    const PlotSeriesData& s1_lock   = vm.seriesAt(0);
    const PlotSeriesData& s1_missed = vm.seriesAt(1);
    const PlotSeriesData& s2_lock   = vm.seriesAt(2);
    const PlotSeriesData& s2_missed = vm.seriesAt(3);

    // Visibility follows the default lock-% view: lock visible, missed hidden.
    QVERIFY(s1_lock.visible);
    QVERIFY(!s1_missed.visible);
    QVERIFY(s2_lock.visible);
    QVERIFY(!s2_missed.visible);

    // Distinct streams get distinct colors; a stream's lock/missed share a color.
    QVERIFY2(s1_lock.color != s2_lock.color, "Each stream must get its own color");
    QCOMPARE(s1_lock.color, s1_missed.color);
    QCOMPARE(s2_lock.color, s2_missed.color);

    QFile::remove(path);
}

void TestPlotViewModel::importSnrStreamGrouping()
{
    // Regression: SNR export columns are "<pcmId> - <label> <chName>". On import
    // the parser must recover streamOrder (the PCM id) and streamLabel so the
    // Customize Plot Series dialog groups/labels SNR channels per stream, and it
    // must count channelIndex per (stream, receiver) — not file-wide — so two
    // streams that both use RCVR1 don't accumulate channel indices across the file.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),"
        "5 - PRN 15 5M L_RCVR1,5 - PRN 15 5M R_RCVR1,6 - PRN 11 1M L_RCVR1\n"
        "1:00:00:00.000,-80.0,-81.0,-82.0\n"
        "1:00:00:01.000,-79.0,-80.5,-81.5\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));
    QCOMPARE(vm.seriesCount(), 3);

    // Stream 5, receiver 1, two channels (L then R) -> channelIndex 0, 1.
    QCOMPARE(vm.seriesAt(0).streamOrder, 5);
    QCOMPARE(vm.seriesAt(0).streamLabel, QString("PRN 15 5M"));
    QCOMPARE(vm.seriesAt(0).receiverIndex, 1);
    QCOMPARE(vm.seriesAt(0).channelIndex, 0);

    QCOMPARE(vm.seriesAt(1).streamOrder, 5);
    QCOMPARE(vm.seriesAt(1).receiverIndex, 1);
    QCOMPARE(vm.seriesAt(1).channelIndex, 1);

    // Stream 6 is a distinct group, and its RCVR1 channel restarts at index 0.
    QCOMPARE(vm.seriesAt(2).streamOrder, 6);
    QCOMPARE(vm.seriesAt(2).streamLabel, QString("PRN 11 1M"));
    QCOMPARE(vm.seriesAt(2).receiverIndex, 1);
    QCOMPARE(vm.seriesAt(2).channelIndex, 0);

    QFile::remove(path);
}

void TestPlotViewModel::importExportRoundTrip()
{
    // Integration: import a CSV (lock + missed + two SNR channels), re-export it,
    // and import the result. The two loaded states must be equivalent — names,
    // metric types, per-stream grouping/colors, visibility, and values — proving
    // exportCsv() and the parser are faithful inverses for US6.3.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),"
        "S1 Lock (%),S1 Accumulated Missed Frames,"
        "5 - S1 L_RCVR1,5 - S1 R_RCVR1\n"
        "1:00:00:00.000,100.0,0,-80.0,-81.0\n"
        "1:00:00:01.000,99.0,1,-79.0,-80.5\n";
    QString path_a = writeTempCsv(csv);

    PlotViewModel vm1;
    QVERIFY(vm1.loadCsvFile(path_a));

    QString path_b = writeTempCsv("");   // mint a unique path; exportCsv overwrites it
    QVERIFY(vm1.exportCsv(path_b));

    PlotViewModel vm2;
    QVERIFY(vm2.loadCsvFile(path_b));

    QCOMPARE(vm2.seriesCount(), vm1.seriesCount());
    for (int i = 0; i < vm1.seriesCount(); ++i)
    {
        const PlotSeriesData& a = vm1.seriesAt(i);
        const PlotSeriesData& b = vm2.seriesAt(i);
        QCOMPARE(b.name, a.name);
        QCOMPARE(static_cast<int>(b.metricType), static_cast<int>(a.metricType));
        QCOMPARE(b.streamOrder, a.streamOrder);
        QCOMPARE(b.streamLabel, a.streamLabel);
        QCOMPARE(b.receiverIndex, a.receiverIndex);
        QCOMPARE(b.channelIndex, a.channelIndex);
        QCOMPARE(b.visible, a.visible);
        QCOMPARE(b.color, a.color);
        QCOMPARE(b.yValues, a.yValues);
    }

    QFile::remove(path_a);
    QFile::remove(path_b);
}

void TestPlotViewModel::lockAxisRange()
{
    // lockYMin() / lockYMax() must always return 0 / 100 regardless of data.
    PlotViewModel vm;
    QCOMPARE(vm.lockYMin(), 0.0);
    QCOMPARE(vm.lockYMax(), 100.0);

    QString csv =
        "Time (DOY:HH:MM:SS.mmm),Ch1 Lock (%),L_RCVR1\n"
        "1:00:00:00.000,50.0,-80.0\n";
    QString path = writeTempCsv(csv);
    QVERIFY(vm.loadCsvFile(path));

    QCOMPARE(vm.lockYMin(), 0.0);
    QCOMPARE(vm.lockYMax(), 100.0);

    QFile::remove(path);
}

void TestPlotViewModel::hasLockSeriesTrue()
{
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),Ch1 Lock (%),L_RCVR1\n"
        "1:00:00:00.000,75.0,-80.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(!vm.hasLockSeries());   // false before any data loaded
    QVERIFY(vm.loadCsvFile(path));
    QVERIFY(vm.hasLockSeries());    // true after loading a CSV that contains the column

    QFile::remove(path);
}

void TestPlotViewModel::hasLockSeriesFalse()
{
    // A CSV without the lock column must leave hasLockSeries() = false.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1,R_RCVR1\n"
        "1:00:00:00.000,-80.0,-75.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));
    QVERIFY(!vm.hasLockSeries());

    QFile::remove(path);
}

void TestPlotViewModel::yAutoRangeIgnoresLockSeries()
{
    // The lock series (0–100%) must NOT inflate the SNR Y-range.
    // With SNR values of 12.3 and 47.8, the auto-range should be 10–50 (rounded to 5 dB),
    // unchanged regardless of the lock percentages present in the same file.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),Ch1 Lock (%),L_RCVR1\n"
        "1:00:00:00.000,95.0,12.3\n"
        "1:00:00:01.000,80.0,47.8\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    // Same expectation as yAutoRange test: floor(12.3/5)*5=10, ceil(47.8/5)*5=50
    QCOMPARE(vm.yMin(), 10.0);
    QCOMPARE(vm.yMax(), 50.0);

    // Lock axis stays fixed regardless
    QCOMPARE(vm.lockYMin(), 0.0);
    QCOMPARE(vm.lockYMax(), 100.0);

    QFile::remove(path);
}

void TestPlotViewModel::loadCsvFileAsyncEmitsDataChanged()
{
    // The async path must emit dataChanged() on success and leave hasData() true.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "1:00:00:00.000,-80.0\n"
        "1:00:00:01.000,-79.0\n";
    QString path = writeTempCsv(csv);
    QVERIFY(!path.isEmpty());

    PlotViewModel vm;
    QSignalSpy data_spy(&vm, &PlotViewModel::dataChanged);
    QSignalSpy fail_spy(&vm, &PlotViewModel::loadFailed);

    // loadCsvFileAsync starts a background future; use an event loop to wait.
    QEventLoop loop;
    QObject::connect(&vm, &PlotViewModel::dataChanged, &loop, &QEventLoop::quit);
    QObject::connect(&vm, &PlotViewModel::loadFailed,  &loop, &QEventLoop::quit);
    // Safety timeout: quit the loop after 5 seconds to prevent a hung test.
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);

    vm.loadCsvFileAsync(path);
    loop.exec();

    QVERIFY2(data_spy.count() == 1, "dataChanged() must be emitted exactly once on success");
    QVERIFY2(fail_spy.isEmpty(),    "loadFailed() must NOT be emitted on a valid file");
    QVERIFY2(vm.hasData(),          "hasData() must be true after a successful async load");
    QCOMPARE(vm.seriesCount(), 1);

    QFile::remove(path);
}

void TestPlotViewModel::loadCsvFileAsyncEmitsLoadFailed()
{
    // The async path must emit loadFailed() and leave hasData() false for a
    // nonexistent file. dataChanged() must NOT be emitted.
    PlotViewModel vm;
    QSignalSpy data_spy(&vm, &PlotViewModel::dataChanged);
    QSignalSpy fail_spy(&vm, &PlotViewModel::loadFailed);

    QEventLoop loop;
    QObject::connect(&vm, &PlotViewModel::dataChanged, &loop, &QEventLoop::quit);
    QObject::connect(&vm, &PlotViewModel::loadFailed,  &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);

    vm.loadCsvFileAsync("/nonexistent/path/that/does/not/exist.csv");
    loop.exec();

    QVERIFY2(fail_spy.count() == 1,  "loadFailed() must be emitted for a nonexistent file");
    QVERIFY2(data_spy.isEmpty(),     "dataChanged() must NOT be emitted on failure");
    QVERIFY2(!vm.hasData(),          "hasData() must remain false after a failed async load");
}

// ---------------------------------------------------------------------------
// addStreamData tests
// ---------------------------------------------------------------------------

/// Builds a minimal ProcessedStreamData with one time sample and a lock value.
static ProcessedStreamData makeLockOnlyStream(const QString& label, double time_sec, double lock_pct)
{
    ProcessedStreamData d;
    d.streamLabel = label;
    d.mode = StreamMode::FrameSyncLockStats;
    d.timesSec.push_back(time_sec);
    d.lockPercent.push_back(lock_pct);
    return d;
}

/// Builds a ProcessedStreamData with one SNR channel and one time sample.
static ProcessedStreamData makeReceiverStream(const QString& label, double time_sec, double lock_pct, const QString& ch_name, double snr)
{
    ProcessedStreamData d;
    d.streamLabel = label;
    d.mode = StreamMode::ReceiverChannelInfo;
    d.timesSec.push_back(time_sec);
    d.lockPercent.push_back(lock_pct);
    ProcessedChannelSeries ch;
    ch.name = ch_name;
    ch.word = 1;
    ch.values.push_back(snr);
    d.channels.push_back(ch);
    return d;
}

void TestPlotViewModel::addStreamDataLockSeriesCreated()
{
    PlotViewModel vm;
    QSignalSpy spy(&vm, &PlotViewModel::dataChanged);

    ProcessedStreamData d = makeLockOnlyStream("Ch32", 1000000.0, 87.5);
    vm.addStreamData(d);

    QCOMPARE(spy.count(), 1);
    QVERIFY(vm.hasData());
    QVERIFY(vm.hasLockSeries());

    // Should have exactly one series: the lock series
    QCOMPARE(vm.seriesCount(), 1);
    const PlotSeriesData& s = vm.seriesAt(0);
    QCOMPARE(static_cast<int>(s.metricType),
             static_cast<int>(PlotSeriesData::MetricType::FrameSyncLock));
    QCOMPARE(s.yValues.size(), 1);
    QCOMPARE(s.yValues[0], 87.5);

    // X values should be elapsed from the first sample (zero for the first)
    QCOMPARE(s.xValues.size(), 1);
    QCOMPARE(s.xValues[0], 0.0);
}

void TestPlotViewModel::addStreamDataSNRSeriesCreated()
{
    PlotViewModel vm;

    ProcessedStreamData d = makeReceiverStream("Ch32", 2000000.0, 90.0, "L_RCVR1", -45.3);
    vm.addStreamData(d);

    QVERIFY(vm.hasData());
    // Should have lock series + one SNR series = 2 total
    QCOMPARE(vm.seriesCount(), 2);

    // First series is lock
    QCOMPARE(static_cast<int>(vm.seriesAt(0).metricType),
             static_cast<int>(PlotSeriesData::MetricType::FrameSyncLock));
    QCOMPARE(vm.seriesAt(0).yValues[0], 90.0);

    // Second series is SNR
    QCOMPARE(static_cast<int>(vm.seriesAt(1).metricType),
             static_cast<int>(PlotSeriesData::MetricType::SNR));
    QCOMPARE(vm.seriesAt(1).yValues[0], -45.3);
}

void TestPlotViewModel::addStreamDataMultipleStreamsAccumulate()
{
    PlotViewModel vm;

    // Add two lock-only streams with different absolute start times
    const double t0 = 86400.0;   // Day 1, 00:00:00
    const double t1 = t0 + 5.0;  // 5 seconds later

    vm.addStreamData(makeLockOnlyStream("Ch32", t0, 80.0));
    vm.addStreamData(makeLockOnlyStream("Ch33", t1, 70.0));

    QVERIFY(vm.hasData());
    QCOMPARE(vm.seriesCount(), 2);

    // Both series should have one sample each; X of second stream = 5s elapsed
    QCOMPARE(vm.seriesAt(0).xValues[0], 0.0);
    QCOMPARE(vm.seriesAt(1).xValues[0], 5.0);

    // X viewport should span from 0 to 5
    QCOMPARE(vm.xMax(), 5.0);
}

void TestPlotViewModel::addStreamDataEmptyDataNoOp()
{
    PlotViewModel vm;
    QSignalSpy spy(&vm, &PlotViewModel::dataChanged);

    ProcessedStreamData empty;
    vm.addStreamData(empty);

    // Empty data should not change vm state
    QVERIFY(!vm.hasData());
    QCOMPARE(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// Frame sync error accumulation (US3.1) tests
// ---------------------------------------------------------------------------

/// Builds a lock-only stream with parallel lock % and cumulative error vectors.
static ProcessedStreamData makeLockAndErrorStream(const QString& label,
                                                  int pcm_channel_id,
                                                  const QVector<double>& times,
                                                  const QVector<double>& lock,
                                                  const QVector<double>& errors)
{
    ProcessedStreamData d;
    d.streamLabel = label;
    d.pcmChannelId = pcm_channel_id;
    d.mode = StreamMode::FrameSyncLockStats;
    d.timesSec = times;
    d.lockPercent = lock;
    d.accumulatedMissedFrames = errors;
    return d;
}

void TestPlotViewModel::addStreamDataErrorSeriesCreated()
{
    PlotViewModel vm;
    ProcessedStreamData d = makeLockAndErrorStream(
        "Ch32", 32, {1000000.0, 1000001.0}, {90.0, 80.0}, {0.0, 3.0});
    vm.addStreamData(d);

    QVERIFY(vm.hasLockSeries());
    QVERIFY(vm.hasMissedFramesSeries());
    // Lock series + error series = 2.
    QCOMPARE(vm.seriesCount(), 2);

    int lock_idx = -1;
    int err_idx = -1;
    for (int i = 0; i < vm.seriesCount(); i++)
    {
        if (vm.seriesAt(i).metricType == PlotSeriesData::MetricType::FrameSyncLock) lock_idx = i;
        if (vm.seriesAt(i).metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames) err_idx = i;
    }
    QVERIFY(lock_idx >= 0);
    QVERIFY(err_idx >= 0);

    // Default view is LockPercent: lock visible, errors hidden.
    QCOMPARE(vm.lockAxisView(), PlotViewModel::LockAxisView::LockPercent);
    QVERIFY(vm.seriesAt(lock_idx).visible);
    QVERIFY(!vm.seriesAt(err_idx).visible);

    // Error series carries the cumulative counts.
    QCOMPARE(vm.seriesAt(err_idx).yValues, QVector<double>({0.0, 3.0}));
}

void TestPlotViewModel::errorSeriesSharesLockColor()
{
    PlotViewModel vm;
    vm.addStreamData(makeLockAndErrorStream("Ch32", 32, {0.0}, {90.0}, {2.0}));

    QColor lock_color;
    QColor err_color;
    for (int i = 0; i < vm.seriesCount(); i++)
    {
        if (vm.seriesAt(i).metricType == PlotSeriesData::MetricType::FrameSyncLock)
            lock_color = vm.seriesAt(i).color;
        if (vm.seriesAt(i).metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
            err_color = vm.seriesAt(i).color;
    }
    // A stream's lock and error curves represent the same stream, one visible at
    // a time, so they must share a color.
    QCOMPARE(err_color, lock_color);
}

void TestPlotViewModel::setLockAxisViewTogglesVisibility()
{
    PlotViewModel vm;
    vm.addStreamData(makeLockAndErrorStream(
        "Ch32", 32, {0.0, 1.0}, {90.0, 80.0}, {0.0, 3.0}));

    QSignalSpy view_spy(&vm, &PlotViewModel::lockAxisViewChanged);
    QSignalSpy data_spy(&vm, &PlotViewModel::dataChanged);

    vm.setLockAxisView(PlotViewModel::LockAxisView::MissedFrames);
    QCOMPARE(vm.lockAxisView(), PlotViewModel::LockAxisView::MissedFrames);
    QCOMPARE(view_spy.count(), 1);
    // The metric toggle is a visibility/axis change, not a data change: the View
    // updates incrementally on lockAxisViewChanged rather than rebuilding the chart,
    // so dataChanged() is intentionally NOT emitted here.
    QCOMPARE(data_spy.count(), 0);

    for (int i = 0; i < vm.seriesCount(); i++)
    {
        const PlotSeriesData& s = vm.seriesAt(i);
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
            QVERIFY(!s.visible);
        if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
            QVERIFY(s.visible);
    }

    // Setting the same view again is a no-op (no extra signals).
    vm.setLockAxisView(PlotViewModel::LockAxisView::MissedFrames);
    QCOMPARE(view_spy.count(), 1);

    // Toggle back.
    vm.setLockAxisView(PlotViewModel::LockAxisView::LockPercent);
    QCOMPARE(view_spy.count(), 2);
    for (int i = 0; i < vm.seriesCount(); i++)
    {
        const PlotSeriesData& s = vm.seriesAt(i);
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
            QVERIFY(s.visible);
        if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
            QVERIFY(!s.visible);
    }
}

void TestPlotViewModel::setLockAxisViewPreservesStreamSelection()
{
    PlotViewModel vm;
    vm.addStreamData(makeLockAndErrorStream(
        "Ch32", 32, {0.0, 1.0}, {90.0, 80.0}, {0.0, 3.0}));
    vm.addStreamData(makeLockAndErrorStream(
        "Ch33", 33, {0.0, 1.0}, {95.0, 85.0}, {0.0, 1.0}));

    // Deselect stream 32 while in LockPercent mode by hiding its lock series.
    int deselected_lock_index = -1;
    for (int i = 0; i < vm.seriesCount(); i++)
    {
        const PlotSeriesData& s = vm.seriesAt(i);
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock
            && s.streamOrder == 32)
        {
            deselected_lock_index = i;
        }
    }
    QVERIFY(deselected_lock_index >= 0);
    vm.setSeriesVisible(deselected_lock_index, false);

    // Switch to MissedFrames: stream 32 must stay hidden, stream 33 visible.
    vm.setLockAxisView(PlotViewModel::LockAxisView::MissedFrames);
    for (int i = 0; i < vm.seriesCount(); i++)
    {
        const PlotSeriesData& s = vm.seriesAt(i);
        if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
            QCOMPARE(s.visible, s.streamOrder != 32);
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
            QVERIFY(!s.visible);
    }

    // Switch back to LockPercent: selection still respected.
    vm.setLockAxisView(PlotViewModel::LockAxisView::LockPercent);
    for (int i = 0; i < vm.seriesCount(); i++)
    {
        const PlotSeriesData& s = vm.seriesAt(i);
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
            QCOMPARE(s.visible, s.streamOrder != 32);
        if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
            QVERIFY(!s.visible);
    }
}

void TestPlotViewModel::frameSyncErrorMaxReflectsData()
{
    PlotViewModel vm;
    // A degenerate (no error) stream still yields a sensible (>=1) top.
    vm.addStreamData(makeLockAndErrorStream("Ch32", 32, {0.0}, {100.0}, {0.0}));
    vm.setLockAxisView(PlotViewModel::LockAxisView::MissedFrames);
    QVERIFY(vm.missedFramesMax() >= 1.0);

    PlotViewModel vm2;
    vm2.addStreamData(makeLockAndErrorStream(
        "Ch32", 32, {0.0, 1.0, 2.0}, {90.0, 80.0, 70.0}, {1.0, 4.0, 9.0}));
    vm2.setLockAxisView(PlotViewModel::LockAxisView::MissedFrames);
    QCOMPARE(vm2.missedFramesMax(), 9.0);
}

// ---------------------------------------------------------------------------
// Stream identity: streamLabel + streamOrder (pre-v2.6.0 regression coverage)
// ---------------------------------------------------------------------------

namespace {
/// @return the index of the series matching @p order and @p metric, or -1.
int findSeriesIndex(const PlotViewModel& vm, int order, PlotSeriesData::MetricType metric)
{
    for (int i = 0; i < vm.seriesCount(); i++)
    {
        const PlotSeriesData& s = vm.seriesAt(i);
        if (s.streamOrder == order && s.metricType == metric)
            return i;
    }
    return -1;
}
} // namespace

void TestPlotViewModel::reprocessOnlySameStreamOrderReplaced()
{
    // Two streams share a TMATS-derived label ("Ch 01") but are distinct PCM
    // channels. Before the fix, addStreamData's reprocess-replace matched on
    // streamLabel alone, so reprocessing one would silently erase the other's
    // series too.
    PlotViewModel vm;
    vm.addStreamData(makeLockAndErrorStream("Ch 01", 5, {0.0, 1.0}, {90.0, 95.0}, {0.0, 1.0}));
    vm.addStreamData(makeLockAndErrorStream("Ch 01", 6, {0.0, 1.0}, {50.0, 55.0}, {0.0, 2.0}));
    QCOMPARE(vm.seriesCount(), 4); // 2 streams x (lock + missed-frames)

    // Reprocess only stream 5, with different values.
    vm.addStreamData(makeLockAndErrorStream("Ch 01", 5, {0.0, 1.0, 2.0}, {10.0, 20.0, 30.0}, {0.0, 0.0, 5.0}));

    QCOMPARE(vm.seriesCount(), 4); // still 4 — stream 6's pair must survive untouched
    const int lock5 = findSeriesIndex(vm, 5, PlotSeriesData::MetricType::FrameSyncLock);
    const int lock6 = findSeriesIndex(vm, 6, PlotSeriesData::MetricType::FrameSyncLock);
    QVERIFY(lock5 >= 0);
    QVERIFY(lock6 >= 0);
    QCOMPARE(vm.seriesAt(lock5).yValues, QVector<double>({10.0, 20.0, 30.0})); // reprocessed
    QCOMPARE(vm.seriesAt(lock6).yValues, QVector<double>({50.0, 55.0}));      // untouched
}

void TestPlotViewModel::renameSeriesOnlySameStreamOrderSiblingRenamed()
{
    PlotViewModel vm;
    vm.addStreamData(makeLockAndErrorStream("Ch 01", 5, {0.0, 1.0}, {90.0, 95.0}, {0.0, 1.0}));
    vm.addStreamData(makeLockAndErrorStream("Ch 01", 6, {0.0, 1.0}, {50.0, 55.0}, {0.0, 2.0}));

    const int lock5   = findSeriesIndex(vm, 5, PlotSeriesData::MetricType::FrameSyncLock);
    const int missed5 = findSeriesIndex(vm, 5, PlotSeriesData::MetricType::AccumulatedMissedFrames);
    const int lock6   = findSeriesIndex(vm, 6, PlotSeriesData::MetricType::FrameSyncLock);
    const int missed6 = findSeriesIndex(vm, 6, PlotSeriesData::MetricType::AccumulatedMissedFrames);
    // Guard indices before use: a regression here (e.g. reprocess-replace erasing
    // stream 6 while adding stream 5) must fail this QVERIFY cleanly rather than
    // crash the whole test binary on an out-of-range seriesAt().
    QVERIFY(lock5 >= 0);
    QVERIFY(missed5 >= 0);
    QVERIFY(lock6 >= 0);
    QVERIFY(missed6 >= 0);

    vm.renameSeries(lock5, "Renamed Stream 5");

    // Stream 5's own lock/missed-frames pair renamed together...
    QCOMPARE(vm.seriesAt(lock5).name, QString("Renamed Stream 5"));
    QCOMPARE(vm.seriesAt(missed5).name, QString("Renamed Stream 5"));
    // ...but stream 6 shares the same original label and must NOT be touched.
    QCOMPARE(vm.seriesAt(lock6).name, QString("Ch 01"));
    QCOMPARE(vm.seriesAt(missed6).name, QString("Ch 01"));
}

void TestPlotViewModel::recolorSeriesOnlySameStreamOrderSiblingRecolored()
{
    PlotViewModel vm;
    vm.addStreamData(makeLockAndErrorStream("Ch 01", 5, {0.0, 1.0}, {90.0, 95.0}, {0.0, 1.0}));
    vm.addStreamData(makeLockAndErrorStream("Ch 01", 6, {0.0, 1.0}, {50.0, 55.0}, {0.0, 2.0}));

    const int lock5   = findSeriesIndex(vm, 5, PlotSeriesData::MetricType::FrameSyncLock);
    const int missed5 = findSeriesIndex(vm, 5, PlotSeriesData::MetricType::AccumulatedMissedFrames);
    const int lock6   = findSeriesIndex(vm, 6, PlotSeriesData::MetricType::FrameSyncLock);
    const int missed6 = findSeriesIndex(vm, 6, PlotSeriesData::MetricType::AccumulatedMissedFrames);
    QVERIFY(lock5 >= 0);
    QVERIFY(missed5 >= 0);
    QVERIFY(lock6 >= 0);
    QVERIFY(missed6 >= 0);
    const QColor original6Color = vm.seriesAt(lock6).color;

    vm.recolorSeries(lock5, QColor(Qt::magenta));

    QCOMPARE(vm.seriesAt(lock5).color, QColor(Qt::magenta));
    QCOMPARE(vm.seriesAt(missed5).color, QColor(Qt::magenta));
    // Stream 6 shares the same original label and must keep its own color.
    QCOMPARE(vm.seriesAt(lock6).color, original6Color);
    QCOMPARE(vm.seriesAt(missed6).color, original6Color);
}

// ---------------------------------------------------------------------------
// exportCsv tests
// ---------------------------------------------------------------------------

void TestPlotViewModel::exportCsvCreatesFile()
{
    // Load some data, then export — file must be created.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "1:00:00:00.000,-80.0\n"
        "1:00:00:01.000,-79.5\n";
    QString in_path = writeTempCsv(csv);
    QVERIFY(!in_path.isEmpty());

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(in_path));

    QString out_path = QDir::tempPath() + "/tst_export_creates.csv";
    QFile::remove(out_path);

    QVERIFY(vm.exportCsv(out_path));
    QVERIFY(QFile::exists(out_path));

    QFile::remove(in_path);
    QFile::remove(out_path);
}

void TestPlotViewModel::exportCsvHeaderAndData()
{
    // The exported CSV must have a time column header and value rows.
    QString csv =
        "Time (DOY:HH:MM:SS.mmm),L_RCVR1\n"
        "45:10:00:00.000,-80.0\n"
        "45:10:00:01.000,-79.0\n";
    QString in_path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(in_path));

    QString out_path = QDir::tempPath() + "/tst_export_content.csv";
    QFile::remove(out_path);
    QVERIFY(vm.exportCsv(out_path));

    // Read back and verify structure
    QFile f(out_path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream stream(&f);
    QString header = stream.readLine();
    QVERIFY2(header.startsWith("Time"), "First column header must be Time...");
    QVERIFY2(header.contains("L_RCVR1"), "Header must contain series name");

    QString row1 = stream.readLine();
    QVERIFY2(!row1.isEmpty(), "First data row must not be empty");
    // Row must contain a comma-separated value (the SNR)
    QVERIFY2(row1.contains(','), "Data row must be comma-delimited");

    f.close();
    QFile::remove(in_path);
    QFile::remove(out_path);
}

void TestPlotViewModel::exportCsvEmptyNoFile()
{
    // A VM with no data loaded must return false and must not create the file.
    PlotViewModel vm;
    QVERIFY(!vm.hasData());

    QString out_path = QDir::tempPath() + "/tst_export_empty.csv";
    QFile::remove(out_path);

    QVERIFY(!vm.exportCsv(out_path));
    QVERIFY(!QFile::exists(out_path));
}

void TestPlotViewModel::exportCsvIncludesErrorColumn()
{
    // A stream with frame sync errors must export BOTH the lock series and the
    // accumulated-missed-frames series as separate columns (exportCsv iterates
    // all series). The plot-legend series names are stored bare (suffixes stripped
    // since v2.5.1), but exportCsv qualifies the CSV header per metric so the two
    // otherwise-identical columns are self-describing.
    PlotViewModel vm;
    vm.addStreamData(makeLockAndErrorStream(
        "Ch32", 32, {0.0, 1.0}, {90.0, 80.0}, {0.0, 3.0}));

    QString out_path = QDir::tempPath() + "/tst_export_errors.csv";
    QFile::remove(out_path);
    QVERIFY(vm.exportCsv(out_path));

    QFile f(out_path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream stream(&f);
    const QStringList header = stream.readLine().split(',');
    // Time column + lock series + missed-frames series = 3 columns, each carrying
    // the stream label plus its metric qualifier.
    QCOMPARE(header.size(), 3);
    QCOMPARE(header.at(1), QString("Ch32 Lock (%)"));
    QCOMPARE(header.at(2), QString("Ch32 Accumulated Missed Frames"));

    // The missed-frames data must actually be written (value 3.0 at the 2nd row),
    // proving the second series is exported and not dropped.
    const QString body = stream.readAll();
    QVERIFY2(body.contains(",3"), "Missed-frames value must appear in the exported rows");
    f.close();

    QFile::remove(out_path);
}

void TestPlotViewModel::exportCsvRoundTripsThroughLoad()
{
    // C1: a file written by exportCsv must load back through loadCsvFile with the
    // same series identities and values — the exporter and importer are a matched
    // pair. This is the regression guard for the format divergence where exported
    // CSVs could not be re-imported.
    PlotViewModel vm;
    vm.addStreamData(makeLockAndErrorStream(
        "Ch32", 32, {0.0, 1.0}, {90.0, 80.0}, {0.0, 3.0}));

    QString out_path = QDir::tempPath() + "/tst_export_roundtrip.csv";
    QFile::remove(out_path);
    QVERIFY(vm.exportCsv(out_path));

    PlotViewModel reloaded;
    QVERIFY(reloaded.loadCsvFile(out_path));

    // Both the lock and missed-frames series survive, re-typed from their qualified
    // headers and stripped back to the bare "Ch32" legend name.
    QCOMPARE(reloaded.seriesCount(), 2);
    QVERIFY(reloaded.hasLockSeries());
    QVERIFY(reloaded.hasMissedFramesSeries());

    int lock_idx = -1;
    int missed_idx = -1;
    for (int i = 0; i < reloaded.seriesCount(); i++)
    {
        const PlotSeriesData& s = reloaded.seriesAt(i);
        QCOMPARE(s.name, QString("Ch32"));
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)            lock_idx = i;
        if (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)  missed_idx = i;
    }
    QVERIFY(lock_idx >= 0);
    QVERIFY(missed_idx >= 0);

    QCOMPARE(reloaded.seriesAt(lock_idx).yValues, QVector<double>({90.0, 80.0}));
    QCOMPARE(reloaded.seriesAt(missed_idx).yValues, QVector<double>({0.0, 3.0}));

    QFile::remove(out_path);
}

