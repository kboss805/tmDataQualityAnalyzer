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
        "Day,Time,L_RCVR1,R_RCVR1,L_RCVR2\n"
        "45,10:00:00.000,-80.5,-75.2,-90.1\n"
        "45,10:00:01.000,-80.3,-75.0,-89.8\n"
        "45,10:00:02.000,-80.1,-74.8,-89.5\n";
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
        "Day,Time,L_RCVR1\n"
        "45,10:00:00.000,-80.0\n"
        "45,10:00:05.500,-79.0\n"
        "46,10:00:00.000,-78.0\n";
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
        "Day,Time,L_RCVR1,R_RCVR1,L_RCVR2\n"
        "1,00:00:00.000,-80.0,-75.0,-90.0\n";
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
        "Day,Time,L_RCVR1\n"
        "1,00:00:00.000,12.3\n"
        "1,00:00:01.000,47.8\n";
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
        "Day,Time,L_RCVR1\n"
        "1,00:00:00.000,-100.0\n"
        "1,00:00:01.000,-50.0\n";
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
        "Day,Time,L_RCVR1\n"
        "1,00:00:00.000,-80.0\n"
        "1,00:01:00.000,-75.0\n";
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
        "Day,Time,L_RCVR1,R_RCVR1\n"
        "1,00:00:00.000,-80.0,-75.0\n";
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
        "Day,Time,L_RCVR1\n"
        "1,00:00:00.000,-80.0\n";
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
        "Day,Time,L_RCVR1\n"
        "45,10:30:15.000,-80.0\n";
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
        "Day,Time,L_RCVR1\n"
        "45,23:59:50.000,-80.0\n";
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
        "Day,Time,L_RCVR1\n"
        "45,00:00:30.000,-80.0\n";
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
    QString csv = "Day,Time,L_RCVR1,R_RCVR1\n";
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
    // CSV with some valid and some malformed rows
    QString csv =
        "Day,Time,L_RCVR1\n"
        "45,10:00:00.000,-80.0\n"
        "short_row\n"
        "45,10:00:02.000,-78.0\n"
        ",,-999.0\n"
        "45,10:00:04.000,-76.0\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));

    // Should load at least the valid rows without crashing
    QVERIFY(vm.hasData());
    QVERIFY(vm.seriesAt(0).xValues.size() >= 2);

    QFile::remove(path);
}

void TestPlotViewModel::lockSeriesMetricType()
{
    // A CSV with the "Framesync Lock (%)" column must give that series MetricType::FrameSyncLock.
    // All other columns must remain MetricType::SNR.
    QString csv =
        "Day,Time,Framesync Lock (%),L_RCVR1\n"
        "1,00:00:00.000,95.0,-80.0\n"
        "1,00:00:01.000,90.0,-79.5\n";
    QString path = writeTempCsv(csv);

    PlotViewModel vm;
    QVERIFY(vm.loadCsvFile(path));
    QCOMPARE(vm.seriesCount(), 2);

    QCOMPARE(vm.seriesAt(0).name, QString("Framesync Lock (%)"));
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
        "Day,Time,Framesync Lock (%),L_RCVR1\n"
        "1,00:00:00.000,87.5,-80.0\n";
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

void TestPlotViewModel::lockAxisRange()
{
    // lockYMin() / lockYMax() must always return 0 / 100 regardless of data.
    PlotViewModel vm;
    QCOMPARE(vm.lockYMin(), 0.0);
    QCOMPARE(vm.lockYMax(), 100.0);

    QString csv =
        "Day,Time,Framesync Lock (%),L_RCVR1\n"
        "1,00:00:00.000,50.0,-80.0\n";
    QString path = writeTempCsv(csv);
    QVERIFY(vm.loadCsvFile(path));

    QCOMPARE(vm.lockYMin(), 0.0);
    QCOMPARE(vm.lockYMax(), 100.0);

    QFile::remove(path);
}

void TestPlotViewModel::hasLockSeriesTrue()
{
    QString csv =
        "Day,Time,Framesync Lock (%),L_RCVR1\n"
        "1,00:00:00.000,75.0,-80.0\n";
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
        "Day,Time,L_RCVR1,R_RCVR1\n"
        "1,00:00:00.000,-80.0,-75.0\n";
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
        "Day,Time,Framesync Lock (%),L_RCVR1\n"
        "1,00:00:00.000,95.0,12.3\n"
        "1,00:00:01.000,80.0,47.8\n";
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
        "Day,Time,L_RCVR1\n"
        "1,00:00:00.000,-80.0\n"
        "1,00:00:01.000,-79.0\n";
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
// Frame sync error accumulation (US2.1) tests
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
    QCOMPARE(data_spy.count(), 1);

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
// exportCsv tests
// ---------------------------------------------------------------------------

void TestPlotViewModel::exportCsvCreatesFile()
{
    // Load some data, then export — file must be created.
    QString csv =
        "Day,Time,L_RCVR1\n"
        "1,00:00:00.000,-80.0\n"
        "1,00:00:01.000,-79.5\n";
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
        "Day,Time,L_RCVR1\n"
        "45,10:00:00.000,-80.0\n"
        "45,10:00:01.000,-79.0\n";
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
    // A stream with frame sync errors must export a "Frame Sync Errors" column
    // alongside the lock column (exportCsv iterates all series).
    PlotViewModel vm;
    vm.addStreamData(makeLockAndErrorStream(
        "Ch32", 32, {0.0, 1.0}, {90.0, 80.0}, {0.0, 3.0}));

    QString out_path = QDir::tempPath() + "/tst_export_errors.csv";
    QFile::remove(out_path);
    QVERIFY(vm.exportCsv(out_path));

    QFile f(out_path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream stream(&f);
    QString header = stream.readLine();
    QVERIFY2(header.contains("Lock (%)"), "Header must contain the lock column");
    QVERIFY2(header.contains("Accumulated Missed Frames"), "Header must contain the missed-frames column");
    f.close();

    QFile::remove(out_path);
}

