/**
 * @file tst_mainview.cpp
 * @brief Smoke tests for MainView — construction, destruction, basic widget checks.
 */

#include "tst_mainview.h"

#include <QFile>
#include <QMenuBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolBar>
#include <QtTest>

#include "constants.h"
#include "mainview.h"
#include "mainviewmodel.h"
#include "plotviewmodel.h"

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

void TestMainView::menuBarExists()
{
    MainView view;
    QVERIFY2(view.menuBar() != nullptr, "MainView must have a menu bar");
    QVERIFY2(!view.menuBar()->actions().isEmpty(), "Menu bar must have at least one menu");
}

void TestMainView::toolBarExists()
{
    MainView view;
    QList<QToolBar*> toolbars = view.findChildren<QToolBar*>();
    QVERIFY2(!toolbars.isEmpty(), "MainView must have at least one toolbar");
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
