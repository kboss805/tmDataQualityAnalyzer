/**
 * @file tst_mainview.cpp
 * @brief Smoke tests for MainView — construction, destruction, basic widget checks.
 */

#include "tst_mainview.h"

#include <QColor>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolButton>
#include <QtTest>
#include <QWidgetAction>

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



void TestMainView::fullManualPathEmptyWhenNotInstalled()
{
    // The full manual is an optional installer task, so "not there" is the normal
    // case for a default install - not a broken one. It must resolve to empty so the
    // Help action falls back to the manual compiled into the executable, which is
    // what makes "there is always a manual" a structural guarantee.
    //
    // Tested against an empty temporary directory: probing the real app root would
    // answer a question about this checkout, where the file always exists because it
    // is generated and tracked.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(MainView::fullManualPathIn(dir.path()).isEmpty());

    // An unknown root is also "not installed" rather than a crash.
    QVERIFY(MainView::fullManualPathIn(QString()).isEmpty());
}

void TestMainView::fullManualPathFoundWhenInstalled()
{
    // ...and when the optional file IS present beside the app root, it is found and
    // preferred over the built-in manual.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = QDir(dir.path()).filePath(UIConstants::kFullManualFilename);

    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("<html><body>full manual</body></html>");
    f.close();

    QCOMPARE(MainView::fullManualPathIn(dir.path()), path);
}

void TestMainView::helpSubmenuHoldsManualAndAbout()
{
    // User Manual and About are collapsed under one Help row rather than sitting as
    // two flat entries. Asserted structurally because the alternative - a screenshot
    // - cannot tell a submenu from a section header: both render as one row of text
    // with the same two entries underneath, and only one of them collapses.
    MainView view;

    QMenu* menu = view.m_menu_button->menu();
    QVERIFY(menu != nullptr);

    QVERIFY(view.m_help_menu != nullptr);
    QCOMPARE(view.m_help_menu->title(), QString("Help"));

    // The submenu's row is in the top-level menu...
    QVERIFY(menu->actions().contains(view.m_help_menu->menuAction()));

    // ...and both entries live inside it, not beside it.
    QStringList help_entries;
    for (QAction* a : view.m_help_menu->actions())
    {
        if (!a->isSeparator()) help_entries << a->text();
    }
    QCOMPARE(help_entries, QStringList({ "User Manual...", "About..." }));

    // The thing that would silently regress: an entry left behind at the top level.
    for (QAction* a : menu->actions())
    {
        QVERIFY2(a->text() != QStringLiteral("User Manual..."),
                 "User Manual must live in the Help submenu, not the top-level menu");
        QVERIFY2(a->text() != QStringLiteral("About..."),
                 "About must live in the Help submenu, not the top-level menu");
    }
}

void TestMainView::exitIsTheLastMenuEntry()
{
    // Exit ends the application, so it belongs at the bottom of the menu rather than
    // inside the Process section with the file commands. Position is the whole point
    // of this entry, and it is exactly what a later addition to the menu breaks
    // without breaking anything else - a new action appended after it still works,
    // still looks reasonable, and quietly moves Exit off the bottom.
    MainView view;

    QMenu* menu = view.m_menu_button->menu();
    QVERIFY(menu != nullptr);

    QStringList entries;
    for (QAction* a : menu->actions())
    {
        if (!a->isSeparator() && !a->text().isEmpty())
        {
            entries << a->text();
        }
    }
    QVERIFY(!entries.isEmpty());
    QCOMPARE(entries.last(), QString("Exit"));

    // ...and it is below Help, not merely last among the file commands.
    QVERIFY(entries.indexOf("Exit") > entries.indexOf("Help"));
}

void TestMainView::openAndSaveTemplateCarryIcons()
{
    // Open and Save as Template carry the folder and save glyphs. Asserted as
    // non-null icons rather than by resource path: a missing Qt resource yields a
    // null QIcon, and setIcon() on a path that does not exist fails silently - the
    // menu entry simply renders without a glyph, which no other test would notice.
    MainView view;

    QVERIFY(view.m_open_action != nullptr);
    QVERIFY(view.m_save_template_action != nullptr);
    QVERIFY2(!view.m_open_action->icon().isNull(), "Open... has no icon");
    QVERIFY2(!view.m_save_template_action->icon().isNull(),
             "Save as Template... has no icon");

    // ...and they must actually render. A QIcon built from a missing or malformed
    // SVG is non-null but produces an empty pixmap, so the check above alone would
    // pass on artwork that draws nothing.
    const QSize sz(16, 16);
    QVERIFY2(!view.m_open_action->icon().pixmap(sz).isNull(), "folder glyph is empty");
    QVERIFY2(!view.m_save_template_action->icon().pixmap(sz).isNull(),
             "save glyph is empty");
}

void TestMainView::menuSectionHeadersSurviveTheStylesheet()
{
    // The menu's section headers went unrendered for every themed build: the app
    // sets a stylesheet at startup, and QStyleSheetStyle then owns menu-item
    // painting and never draws a QMenu::addSection() label. Nothing failed - the
    // separator still appeared, just nameless - so the code, docs and manual all
    // described named groups no user had seen.
    //
    // Asserted with the REAL stylesheet applied, because unstyled is exactly the
    // configuration in which the old code also passed.
    QFile qss(QCoreApplication::applicationDirPath() + "/../../resources/win11-dark.qss");
    QVERIFY2(qss.open(QIODevice::ReadOnly | QIODevice::Text),
             "theme stylesheet not found - this test is meaningless without it");
    const QString saved = qApp->styleSheet();
    qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));

    MainView view;
    QMenu* menu = view.m_menu_button->menu();
    QVERIFY(menu != nullptr);
    menu->ensurePolished();

    QStringList headers;
    for (QAction* a : menu->actions())
    {
        auto* wa = qobject_cast<QWidgetAction*>(a);
        if (!wa) continue;
        auto* label = qobject_cast<QLabel*>(wa->defaultWidget());
        if (!label || label->objectName() != QStringLiteral("menuSectionLabel")) continue;

        headers << label->text();
        // A header that renders is the point: a label with room to draw in. The
        // old addSection() path collapsed to the separator's fixed 1px band.
        QVERIFY2(label->sizeHint().height() > 1,
                 qPrintable(QStringLiteral("section '%1' has no height to draw in")
                                .arg(label->text())));
        // Not a command: it must not be selectable or keyboard-reachable.
        QVERIFY(!a->isEnabled());
    }

    QCOMPARE(headers, QStringList({ "Process", "Import/Export", "Settings" }));

    qApp->setStyleSheet(saved);
}

void TestMainView::logPreservesMultiLineMessages()
{
    // The frame-sync diagnostics are laid out over several aligned lines - "Looking
    // for:", "Config:", "Syncs:" - and that layout is the whole reason they are
    // readable. logError() escapes the message with toHtmlEscaped(), which handles the
    // markup characters but leaves a newline as a literal newline; HTML then collapses
    // it to a single space, so a carefully formatted report arrived in the log as one
    // run-on paragraph. Invisible to every other test, and only caught by looking at a
    // screenshot of the real thing.
    MainView view;

    const QString multi = QStringLiteral("first line\n  second line\n  third line");
    view.logError(multi);

    const QString shown = view.m_log_preview->toPlainText();

    // The line breaks must survive into the rendered document, not just the source
    // string. Asserting on toPlainText() rather than toHtml() keeps this about what
    // the operator sees rather than which CSS property achieved it.
    QVERIFY2(shown.contains(QStringLiteral("first line\n")),
             qPrintable(QStringLiteral("newline collapsed; log reads: %1").arg(shown)));
    QVERIFY2(shown.contains(QStringLiteral("second line\n")),
             qPrintable(QStringLiteral("newline collapsed; log reads: %1").arg(shown)));
    QVERIFY2(!shown.contains(QStringLiteral("first line   second line")),
             qPrintable(QStringLiteral("lines were joined into one paragraph: %1").arg(shown)));

    // The leading spaces that align the report's columns must survive too - HTML
    // collapses runs of spaces just as readily as it collapses newlines.
    QVERIFY2(shown.contains(QStringLiteral("\n  second line")),
             qPrintable(QStringLiteral("indent collapsed; log reads: %1").arg(shown)));
}
