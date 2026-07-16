/**
 * @file tst_mainview.cpp
 * @brief Smoke tests for MainView — construction, destruction, basic widget checks.
 */

#include "tst_mainview.h"

#include <QColor>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolButton>
#include <QtTest>

#include "constants.h"
#include "mainview.h"
#include "mainviewmodel.h"
#include "plotseriesdata.h"
#include "plotviewmodel.h"
#include "processedstreamdata.h"
#include "processingtemplate.h"
#include "source.h"
#include "streamconfig.h"

namespace {
/// Writes @p contents to @p path; fails the calling test if the write fails.
void writeCsv(const QString& path, const QString& contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream(&file) << contents;
    file.close();
}
}  // namespace

void TestMainView::constructsAndDestroysWithoutCrash()
{
    // Construction exercises the full widget hierarchy (toolbar, docks, tree, log, plot).
    // If any setup step throws or crashes, this test fails.
    MainView* view = new MainView();
    QVERIFY(view != nullptr);
    delete view;
}

void TestMainView::windowTitleIsNonEmpty()
{
    MainView view;
    QVERIFY2(!view.windowTitle().isEmpty(), "Window title must be set after construction");
}

void TestMainView::titleBarExists()
{
    MainView view;
    // The window uses a custom title bar (set via setMenuWidget) rather than a
    // native menu bar; it must exist and host the hamburger + window buttons.
    QWidget* title_bar = view.menuWidget();
    QVERIFY2(title_bar != nullptr, "MainView must have a custom title-bar menu widget");
    QVERIFY2(!title_bar->findChildren<QToolButton*>().isEmpty(),
             "Title bar must have buttons (hamburger + window controls)");
}

void TestMainView::supportedFileDetection()
{
    // .ch10 and .csv are accepted (case-insensitive); everything else is not.
    QVERIFY(MainView::isSupportedFile("C:/data/run.ch10"));
    QVERIFY(MainView::isSupportedFile("run.CH10"));
    QVERIFY(MainView::isSupportedFile("C:/data/export.csv"));
    QVERIFY(MainView::isSupportedFile("EXPORT.CSV"));
    QVERIFY(!MainView::isSupportedFile("notes.txt"));
    QVERIFY(!MainView::isSupportedFile("archive.csv.zip"));
    QVERIFY(!MainView::isSupportedFile(QString()));
}

void TestMainView::openPathImportsCsv()
{
    // openPath() routes by extension: a .csv must reach the importer and populate
    // the plot (not the Ch10 processing pipeline). The .ch10 branch is exercised
    // by the live app rather than here, since openFile() spins a reader thread and
    // ends in the modal Configure Streams dialog.
    MainView view;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("routed.csv");
    writeCsv(path, QString(PlotConstants::kCsvTimeHeader) + ",Stream1 Lock (%)\n"
                   "001:00:00:00.000,100.0\n"
                   "001:00:00:01.000,95.0\n");

    QVERIFY(!view.m_plot_view_model->hasData());
    view.openPath(path);
    QTRY_VERIFY(view.m_plot_view_model->hasData());
}

void TestMainView::importValidCsvPopulatesPlot()
{
    MainView view;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("export.csv");

    // A minimal file in the app's own export format: the combined-time column
    // plus one Frame Sync Lock series and one Receiver SNR series.
    writeCsv(path, QString(PlotConstants::kCsvTimeHeader) + ",Stream1 Lock (%),L_RCVR1\n"
                   "001:00:00:00.000,100.0,3.5\n"
                   "001:00:00:01.000,99.5,3.6\n"
                   "001:00:00:02.000,98.0,3.7\n");

    QVERIFY(!view.m_plot_view_model->hasData());
    view.importCsv(path);

    // Import is asynchronous (parsed on a worker thread); wait for the result.
    QTRY_VERIFY(view.m_plot_view_model->hasData());
    QCOMPARE(view.m_plot_view_model->seriesCount(), 2);
}

void TestMainView::importInvalidCsvIsRejected()
{
    MainView view;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("legacy.csv");

    // The legacy "Day,Time,..." layout is not this app's export format and must
    // be rejected rather than mis-parsed into garbage series.
    writeCsv(path, "Day,Time,Value\n1,2,3\n");

    QSignalSpy fail_spy(view.m_plot_view_model, &PlotViewModel::loadFailed);
    view.importCsv(path);

    QTRY_COMPARE(fail_spy.count(), 1);
    QVERIFY(!view.m_plot_view_model->hasData());

    // A rejected import must not be recorded as a recent file nor logged as a
    // success (regression guard for the premature-finalize bug where clearData()'s
    // dataChanged() fired the success path before the parse failed).
    QVERIFY(!view.m_view_model->recentFiles().contains(path));
    QVERIFY(!view.m_log_preview->toPlainText().contains("Imported CSV"));
}

void TestMainView::batchReapplyAppearanceRenamesMatchingSeries()
{
    // Batch apply's appearance-reuse step: after a file finishes, the template's
    // captured name/color for each stream must be re-applied onto that file's
    // freshly-created series, matched by (channel id, metric, receiver, channel).
    // Driving the full addSource/processing loop needs a real .ch10 fixture, so
    // this exercises the reapply step directly against synthetic plot data.
    MainView view;

    ProcessedStreamData d;
    d.streamLabel = "Ch 5";
    d.pcmChannelId = 5;
    d.sourceId = 0;
    d.mode = StreamMode::FrameSyncLockStats;
    d.timesSec                = { 0.0, 1.0, 2.0 };
    d.lockPercent             = { 90.0, 95.0, 100.0 };
    d.accumulatedMissedFrames = { 0.0, 1.0, 1.0 };
    view.m_plot_view_model->addStreamData(d);

    // Template with a custom appearance for channel 5's lock series.
    ProcessingTemplate tmpl;
    TemplateStreamEntry entry;
    entry.config.pcmChannelId = 5;
    SeriesAppearance appearance;
    appearance.metricType    = PlotSeriesData::MetricType::FrameSyncLock;
    appearance.receiverIndex = 0;
    appearance.channelIndex  = 0;
    appearance.name          = "Custom Lock";
    appearance.color         = QColor("#123456");
    entry.appearance.append(appearance);
    tmpl.entries.append(entry);

    view.m_batch_template = tmpl;
    view.reapplyTemplateAppearance(/*sourceId=*/0);

    bool found = false;
    for (const PlotSeriesData& s : view.m_plot_view_model->allSeries())
    {
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock && s.streamOrder == 5)
        {
            QCOMPARE(s.name, QString("Custom Lock"));
            QCOMPARE(s.color, QColor("#123456"));
            found = true;
        }
    }
    QVERIFY(found);
}

void TestMainView::batchBuildTemplateCapturesConfigsAndAppearance()
{
    // Save-as-Template capture: buildTemplateFromSource() must carry the source's
    // stream configs + time channel, and snapshot each stream's current series
    // appearance (name/color) keyed by metric/receiver/channel.
    MainView view;

    ProcessedStreamData d;
    d.streamLabel = "Ch 5";
    d.pcmChannelId = 5;
    d.sourceId = 0;
    d.mode = StreamMode::FrameSyncLockStats;
    d.timesSec                = { 0.0, 1.0, 2.0 };
    d.lockPercent             = { 90.0, 95.0, 100.0 };
    d.accumulatedMissedFrames = { 0.0, 1.0, 1.0 };
    view.m_plot_view_model->addStreamData(d);

    // Customize the lock series so the capture has something non-default to grab.
    int lock_id = -1;
    for (const PlotSeriesData& s : view.m_plot_view_model->allSeries())
    {
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
            lock_id = s.id;
    }
    QVERIFY(lock_id >= 0);
    view.m_plot_view_model->renameSeriesById(lock_id, "MyLock");
    view.m_plot_view_model->recolorSeriesById(lock_id, QColor("#abcdef"));
    view.m_plot_view_model->commitAppearanceChanges();

    Source src;
    src.sourceId = 0;
    src.timeChannelIndex = 2;
    StreamConfig cfg;
    cfg.pcmChannelId = 5;
    cfg.label = "Ch 5";
    cfg.process = true;
    cfg.mode = StreamMode::FrameSyncLockStats;
    src.streamConfigs.append(cfg);

    const ProcessingTemplate tmpl = view.buildTemplateFromSource(src);

    QCOMPARE(tmpl.timeChannelIndex, 2);
    QCOMPARE(tmpl.entries.size(), 1);
    QCOMPARE(tmpl.entries[0].config.pcmChannelId, 5);

    bool found_lock_appearance = false;
    for (const SeriesAppearance& a : tmpl.entries[0].appearance)
    {
        if (a.metricType == PlotSeriesData::MetricType::FrameSyncLock)
        {
            QCOMPARE(a.name, QString("MyLock"));
            QCOMPARE(a.color, QColor("#abcdef"));
            found_lock_appearance = true;
        }
    }
    QVERIFY(found_lock_appearance);
}
