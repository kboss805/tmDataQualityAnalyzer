/**
 * @file mainview.cpp
 * @brief Implementation of MainView — Qt Widgets UI and ViewModel signal wiring.
 */

#include "mainview.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyleHints>
#include <QTime>
#include <QToolButton>
#include <QUrl>
#include <QWidgetAction>

#include "batchapplydialog.h"
#include "batchcontroller.h"
#include "chapter10reader.h"
#include "constants.h"
#include "mainviewmodel.h"
#include "plotviewmodel.h"
#include "plotwidget.h"
#include "processedstreamdata.h"
#include "processingprogressdialog.h"
#include "processingtemplate.h"
#include "processingtemplateschema.h"
#include "source.h"
#include "streamconfigdialog.h"

namespace
{

    /// CSS applied to every log line so multi-line messages survive the trip into
    /// the log's rich text.
    ///
    /// toHtmlEscaped() escapes the markup characters but leaves a newline as a
    /// literal newline, and HTML collapses that to a single space - so the
    /// frame-sync diagnostics, which are deliberately laid out over several aligned
    /// lines, arrived as one run-on paragraph. `pre-wrap` keeps both the newlines
    /// and the runs of spaces that align the "Looking for:" / "Config:" / "Syncs:"
    /// columns, while still wrapping lines too long for the sidebar.
    constexpr const char* kLogWhiteSpace = "white-space: pre-wrap;";



}


MainView::MainView(QWidget *parent)
    : QMainWindow(parent)
    , m_view_model(new MainViewModel(this))
{
    setAcceptDrops(true);

    setUpMainLayout();

    QSettings app_settings;
    m_last_ch10_dir = app_settings.value(UIConstants::kSettingsKeyLastCh10Dir).toString();
    if (m_last_ch10_dir.isEmpty())
    {
        m_last_ch10_dir = QCoreApplication::applicationDirPath();
    }

    setUpConnections();
    m_view_model->logStartupInfo();
}

MainView::~MainView()
{
}

void MainView::saveLastCh10Dir()
{
    QSettings app_settings;
    app_settings.setValue(UIConstants::kSettingsKeyLastCh10Dir, m_last_ch10_dir);
}

////////////////////////////////////////////////////////////////////////////////
//                                 SET UP GUI                                 //
////////////////////////////////////////////////////////////////////////////////

void MainView::setUpMainLayout()
{
    m_sidebar_layout = new QVBoxLayout;
    m_sidebar_layout->setSpacing(0);
    m_sidebar_layout->setContentsMargins(2, UIConstants::kLayoutSpacingSmall, UIConstants::kLayoutSpacingSmall, UIConstants::kLayoutSpacingSmall);

    // set up constituent parts
    setUpChrome();

    m_progress_dialog = new ProcessingProgressDialog(this);

    m_log_preview = new QTextBrowser;
    m_log_preview->setReadOnly(true);
    m_log_preview->setOpenLinks(false);
    m_log_preview->setMinimumHeight(UIConstants::kLogPreviewHeight);

    // Log preview fills the sidebar; progress/cancel now live in
    // ProcessingProgressDialog, shown only while processing is active.
    m_sidebar_layout->addWidget(m_log_preview, 1);

    // PlotWidget as central widget — fills all space right of the sidebar dock
    m_plot_view_model = new PlotViewModel(this);
    m_batch = new BatchController(m_view_model, m_plot_view_model, this);
    m_plot_widget = new PlotWidget;
    m_plot_widget->setViewModel(m_plot_view_model);
    m_plot_widget->setMinimumWidth(PlotConstants::kPlotDockMinWidth);

    QSettings plot_settings;
    bool dark = plot_settings.value(UIConstants::kSettingsKeyTheme, UIConstants::kThemeDark).toString()
                == UIConstants::kThemeDark;
    m_plot_widget->applyTheme(dark);
    applyActionIconsForTheme(dark);

    // The Customize Plot button replaces the old legend and is initialized disabled.

    // The plot widget already has a layout with its own margins.
    // Setting it directly as the central widget allows the QMainWindow
    // dock separator to naturally space it ~4px away from the dock.
    // With the dock right margin (8px) and plot left margin (4px), 
    // the total visual gap becomes exactly 16px.
    setCentralWidget(m_plot_widget);

    // Sidebar in a left dock widget (holds the log); shown/hidden from the title bar.
    QWidget* sidebar_widget = new QWidget;
    sidebar_widget->setLayout(m_sidebar_layout);
    sidebar_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    m_sidebar_dock = new QDockWidget(this);
    m_sidebar_dock->setTitleBarWidget(new QWidget);
    m_sidebar_dock->setWidget(sidebar_widget);
    m_sidebar_dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_sidebar_dock->setMinimumWidth(UIConstants::kSidebarMinWidth);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebar_dock);

    // Restore the sidebar's last shown/hidden state (default shown) and sync the
    // title-bar toggle. Done here, once the dock exists, since setUpChrome() (which
    // builds the toggle) runs before the dock is created.
    const bool sidebar_shown =
        QSettings().value(UIConstants::kSettingsKeySidebarVisible, true).toBool();
    m_sidebar_dock->setVisible(sidebar_shown);
    if (m_sidebar_toggle != nullptr)
    {
        QSignalBlocker block(m_sidebar_toggle);
        m_sidebar_toggle->setChecked(sidebar_shown);
    }

    // Intercept drag events from every widget in the window by filtering at
    // the application level — simpler and more complete than listing individual
    // child viewports.
    qApp->installEventFilter(this);

    // additional settings
    setWindowTitle("TM Data Quality Analyzer");

    statusBar()->showMessage("No file loaded");
    
    resize(UIConstants::kInitialWindowWidth, UIConstants::kInitialWindowHeight);
}






////////////////////////////////////////////////////////////////////////////////
//                              GUI CONNECTIONS                               //
////////////////////////////////////////////////////////////////////////////////

void MainView::setUpConnections()
{
    // The batch loop renders through the widget (only the widget can draw the
    // plot) and reports through the log, so the View supplies both.
    m_batch->setImageExporter([this](const QString& path) {
        return m_plot_widget->exportImage(path);
    });
    connect(m_batch, &BatchController::message, this,
            [this](MainViewModel::LogLevel level, const QString& text) {
                switch (level)
                {
                    case MainViewModel::LogLevel::Error:   logError(text);   break;
                    case MainViewModel::LogLevel::Warning: logWarning(text); break;
                    case MainViewModel::LogLevel::Success: logSuccess(text); break;
                    case MainViewModel::LogLevel::Info:    onLogMessage(text); break;
                }
            });

    // ViewModel -> View: data binding
    connect(m_view_model, &MainViewModel::fileLoadedChanged, this, &MainView::updateStatusBar);
    connect(m_view_model, &MainViewModel::recentFilesChanged, this, &MainView::updateRecentFilesMenu);
    connect(m_view_model, &MainViewModel::progressPercentChanged, this, &MainView::onProgressChanged);
    connect(m_view_model, &MainViewModel::processingChanged, this, &MainView::onProcessingChanged);
    connect(m_view_model, &MainViewModel::fileReadyForStreamConfig,
            this, &MainView::onFileReadyForStreamConfig);
    connect(m_view_model, &MainViewModel::sourceReadyForStreamConfig,
            this, &MainView::onSourceReadyForStreamConfig);
    connect(m_view_model, &MainViewModel::streamProcessed, this, &MainView::onStreamProcessed);

    // Save as Template is only meaningful once at least one source has finished
    // processing successfully.
    connect(m_view_model, &MainViewModel::sourcesChanged, this, [this]() {
        m_save_template_action->setEnabled(!m_view_model->sources().isEmpty());
    });

    // Informational: a newly added source's recording doesn't overlap what's
    // already loaded (still correct on the shared elapsed axis; just a large gap).
    connect(m_plot_view_model, &PlotViewModel::nonOverlappingSourceWarning, this, [this](int sourceId) {
        Q_UNUSED(sourceId);
        logWarning("The most recently added source's recording time ('"
                   + QFileInfo(m_view_model->inputFilename()).fileName()
                   + "') does not overlap the data already loaded. They will share "
                     "one time axis with a large gap between them.");
    });

    connect(m_view_model, &MainViewModel::errorOccurred, this, &MainView::displayErrorMessage);
    connect(m_view_model, &MainViewModel::processingFinished, this, &MainView::onProcessingFinished);
    connect(m_view_model, &MainViewModel::logMessageReceived, this, &MainView::onLogMessage);

    // ProcessingProgressDialog -> ViewModel: Cancel button
    connect(m_progress_dialog, &ProcessingProgressDialog::cancelRequested,
            this, [this]() { m_view_model->cancelProcessing(); });

    // PlotWidget -> Log window
    connect(m_plot_widget, &PlotWidget::logMessage, this, &MainView::onLogMessage);

    // Let the Export dialog pull the current log window contents as plain text.
    m_plot_widget->setLogTextProvider([this]() {
        return m_log_preview->toPlainText();
    });
    connect(m_plot_view_model, &PlotViewModel::loadFailed, this, [this]() {
        if (!m_pending_csv_path.isEmpty())
        {
            const QString name = QFileInfo(m_pending_csv_path).fileName();
            m_pending_csv_path.clear();
            displayErrorMessage("ERROR: '" + name + "' is not a recognized exported CSV (expected a '"
                                + QString(PlotConstants::kCsvTimeHeader)
                                + "' column with data rows). Nothing was imported.");
        }
        else
        {
            displayErrorMessage("Failed to load CSV file for plotting.");
        }
    });

    // Finalize a successful CSV import (recent files, plot title, status bar,
    // success log). loadSucceeded() fires only for async imports, so this never
    // collides with dataChanged() from in-memory stream processing or clearData().
    connect(m_plot_view_model, &PlotViewModel::loadSucceeded, this, [this]() {
        if (m_pending_csv_path.isEmpty())
        {
            return;
        }
        const QString path = m_pending_csv_path;
        m_pending_csv_path.clear();
        m_view_model->addRecentFile(path);
        m_plot_view_model->setPlotTitle(QFileInfo(path).baseName());
        statusBar()->showMessage("Imported CSV: " + QFileInfo(path).fileName());
        logSuccess("Imported CSV: " + QFileInfo(path).fileName());
    });

    connect(m_plot_view_model, &PlotViewModel::loadWarning, this, &MainView::logWarning);

    // Update toolbar button enablement when data changes
    connect(m_plot_view_model, &PlotViewModel::dataChanged, this, [this]() {
        bool has_data = m_plot_view_model->hasData();
        m_export_action->setEnabled(has_data);
    });

    // Connect toolbar actions to PlotWidget slots
    connect(m_export_action, &QAction::triggered,
            m_plot_widget, &PlotWidget::onExportPlot);

    connect(m_log_preview, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        QDesktopServices::openUrl(url);
    });
}

////////////////////////////////////////////////////////////////////////////////
//                        VIEWMODEL-DRIVEN SLOTS                              //
////////////////////////////////////////////////////////////////////////////////


void MainView::onProgressChanged()
{
    m_progress_dialog->setProgress(m_view_model->progressPercent());
}

void MainView::onProcessingChanged()
{
    if (m_view_model->processing())
    {
        setAllControlsEnabled(false);
        m_progress_dialog->reset();
        m_progress_dialog->show();
    }
    else
    {
        setAllControlsEnabled(true);
        m_progress_dialog->hide();
    }
}

void MainView::onStreamProcessed(const ProcessedStreamData& data)
{
    onLogMessage("  " + data.streamLabel + ": " +
                 QString::number(data.timesSec.size()) + " samples, " +
                 QString::number(data.channels.size()) + " channel(s)");
    m_plot_view_model->addStreamData(data);
}

void MainView::onProcessingFinished(bool success)
{
    if (m_batch->active())
    {
        m_batch->onProcessingFinished(success);
        return;
    }

    if (success)
    {
        logSuccess("Processing complete — results plotted from memory.");
        m_plot_view_model->setPlotTitle(QFileInfo(m_view_model->inputFilename()).baseName());
        // Label this source for the plot toolbar's file selector (multi-file via Add Source).
        if (!m_view_model->sources().isEmpty())
        {
            const Source& src = m_view_model->sources().last();
            m_plot_view_model->setSourceLabel(src.sourceId, QFileInfo(src.filepath).baseName());
        }
    }
}

void MainView::onLogMessage(const QString& message)
{
    // Severity classification is ViewModel policy; the View only renders by level.
    switch (MainViewModel::classifyLogMessage(message))
    {
    case MainViewModel::LogLevel::Error:
        logError(message);
        return;
    case MainViewModel::LogLevel::Warning:
        logWarning(message);
        return;
    case MainViewModel::LogLevel::Success:
        logSuccess(message);
        return;
    case MainViewModel::LogLevel::Info:
        break;
    }

    // Info: a timestamped neutral entry, with a passthrough for messages that
    // already carry HTML color/link markup (e.g. export results).
    QString timestamp = QTime::currentTime().toString("HH:mm:ss") + "&nbsp;&nbsp;";
    QString html;
    if (message.startsWith("<span"))
    {
        // Already pre-formatted HTML (e.g. export success/failure messages with
        // color styling and clickable file links) — append as-is, don't escape.
        html = timestamp + message;
    }
    else
    {
        // Wrap in a neutral <span> so Qt treats this as explicit HTML and does not
        // auto-detect bare file paths (e.g. C:/...) as clickable anchors.
        html = "<span style='" + QString(kLogWhiteSpace) + "'>" + timestamp
               + message.toHtmlEscaped() + "</span>";
    }
    m_log_preview->append(html);
    m_log_preview->verticalScrollBar()->setValue(m_log_preview->verticalScrollBar()->maximum());
}

////////////////////////////////////////////////////////////////////////////////
//                         USER-INITIATED ACTIONS                             //
////////////////////////////////////////////////////////////////////////////////

void MainView::displayErrorMessage(const QString& message)
{
    logError(message);
}

void MainView::inputFileButtonPressed()
{
    QString filename = QFileDialog::getOpenFileName(this, tr("Open Chapter 10 File"),
                                                    m_last_ch10_dir,
                                                    tr("Chapter 10 Files (*.ch10)"));
    if (filename.isEmpty())
    {
        return;
    }

    openPath(filename);
}

void MainView::importFileButtonPressed()
{
    QString filename = QFileDialog::getOpenFileName(this, tr("Import CSV File"),
                                                    m_last_ch10_dir,
                                                    tr("Exported CSV (*.csv)"));
    if (filename.isEmpty())
    {
        return;
    }

    openPath(filename);
}


void MainView::saveTemplateButtonPressed()
{
    const QVector<Source>& sources = m_view_model->sources();
    if (sources.isEmpty())
    {
        logWarning("Nothing to save as a template -- no sources have finished processing yet.");
        return;
    }

    // A template captures ONE source's stream configuration. With several loaded,
    // let the user pick which (same picker style as Remove Source).
    int source_index = 0;
    if (sources.size() > 1)
    {
        QStringList labels;
        labels.reserve(sources.size());
        for (const Source& s : sources)
            labels.append(QFileInfo(s.filepath).fileName() + QString(" (source %1)").arg(s.sourceId));

        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Save as Template"),
            tr("Capture the per-stream settings from which source?"), labels, 0, /*editable=*/false, &ok);
        if (!ok)
            return;
        source_index = labels.indexOf(chosen);
        if (source_index < 0)
            return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Save as Template"),
        m_last_ch10_dir, tr("Template Files (*.json)"));
    if (path.isEmpty())
        return;

    const ProcessingTemplate tmpl =
        BatchController::buildTemplate(sources.at(source_index), *m_plot_view_model);
    const QJsonDocument doc = ProcessingTemplateSchema::toJson(tmpl);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        logError("Could not write template file: " + path);
        return;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    m_last_ch10_dir = QFileInfo(path).absolutePath();
    saveLastCh10Dir();
    logSuccess("Template saved: " + QFileInfo(path).fileName());
}

bool MainView::isSupportedFile(const QString& path)
{
    return path.endsWith(".ch10", Qt::CaseInsensitive)
           || path.endsWith(".csv", Qt::CaseInsensitive);
}

void MainView::openPath(const QString& path)
{
    m_last_ch10_dir = QFileInfo(path).absolutePath();
    saveLastCh10Dir();

    if (path.endsWith(".csv", Qt::CaseInsensitive))
    {
        importCsv(path);
    }
    else
    {
        m_view_model->openFile(path);
    }
}

void MainView::importCsv(const QString& path)
{
    // Don't let a CSV import collide with a ch10 load/processing run or another
    // in-flight import; the plot is shared state.
    if (m_view_model->processing() || m_plot_view_model->isLoading())
    {
        return;
    }

    onLogMessage("Importing CSV: " + QFileInfo(path).fileName());

    // m_pending_csv_path is consumed by the loadSucceeded/loadFailed handlers to
    // finalize (or report) this import. clearData() resets the prior plot first.
    m_plot_view_model->clearData();
    m_pending_csv_path = path;
    m_plot_view_model->loadCsvFileAsync(path);
}

void MainView::onFileReadyForStreamConfig()
{
    // A fresh file starts a fresh plot; processing accumulates into it.
    showStreamConfigDialogForPendingSource(/*clearPlotFirst=*/true);
}

void MainView::onSourceReadyForStreamConfig()
{
    // addSource() is now driven only by the Apply Template batch loop, which applies
    // the template's configs to each file without a per-file Configure Streams step.
    if (m_batch->active())
    {
        m_batch->onSourceReady();
    }
}

void MainView::showStreamConfigDialogForPendingSource(bool clearPlotFirst)
{
    if (clearPlotFirst)
    {
        m_plot_view_model->clearData();
    }

    StreamConfigDialog dialog(m_view_model->buildDefaultStreamConfigs(),
                              m_view_model->lastIniDir(),
                              m_view_model->timeChannelList(),
                              m_view_model->timeChannelIndex(),
                              m_view_model->reader()->getCurrentTimeChannelID(),
                              m_view_model->appRoot(),
                              m_view_model->swapBytes(),
                              this);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_view_model->setTimeChannelIndex(dialog.timeChannelIndex());
        m_view_model->setSwapBytes(dialog.swapBytes());
        m_view_model->setStreamConfigs(dialog.configs());

        startProcessingFromDialog();
    }
}

void MainView::confirmAndStartBatch(const ProcessingTemplate& tmpl, const QStringList& files,
                                    bool showReuseAppearance)
{
    // Validating reads every file's channel list, so it is the slow part of
    // starting a batch; the dialog then shows which files will run and which are
    // rejected, and why.
    const QVector<BatchController::FileCheck> checks = BatchController::validate(tmpl, files);

    QList<BatchApplyDialog::FileEntry> entries;
    QStringList matched;
    entries.reserve(checks.size());
    for (const BatchController::FileCheck& check : checks)
    {
        BatchApplyDialog::FileEntry entry;
        entry.filepath = check.filepath;
        entry.ok       = check.ok;
        entry.reason   = check.reason;
        entries.append(entry);
        if (check.ok)
        {
            matched.append(check.filepath);
        }
    }

    BatchApplyDialog dialog(entries, m_last_ch10_dir, showReuseAppearance, this);
    if (dialog.exec() != QDialog::Accepted || matched.isEmpty())
    {
        return;
    }

    BatchController::Options options;
    options.exportPerFile   = dialog.exportPerFile();
    options.reuseAppearance = dialog.reuseAppearance();
    options.outputDir       = dialog.outputDir();
    m_batch->start(tmpl, matched, options);
}

void MainView::applyTemplateButtonPressed()
{
    if (m_view_model->processing() || m_batch->active())
    {
        return;
    }

    // 1. Pick + parse the template.
    const QString template_path = QFileDialog::getOpenFileName(this, tr("Apply Template to Files"),
        m_last_ch10_dir, tr("Template Files (*.json)"));
    if (template_path.isEmpty())
    {
        return;
    }

    QFile file(template_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        logError("Could not open template file: " + template_path);
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        logError("Template file is not valid JSON: " + parse_error.errorString());
        return;
    }

    ProcessingTemplate tmpl;
    const ProcessingTemplateSchema::LoadStatus status = ProcessingTemplateSchema::fromJson(doc, tmpl);
    if (status == ProcessingTemplateSchema::LoadStatus::UnsupportedSchemaVersion)
    {
        logError("Template file uses an unsupported schema version.");
        return;
    }
    if (status == ProcessingTemplateSchema::LoadStatus::InvalidFormat)
    {
        logError("Template file is not a valid template (unrecognized format).");
        return;
    }
    if (tmpl.entries.isEmpty())
    {
        logError("Template has no stream entries.");
        return;
    }

    m_last_ch10_dir = QFileInfo(template_path).absolutePath();
    saveLastCh10Dir();

    // 2. Pick the .ch10 files to run, then hand off to the shared batch kickoff
    //    (validate against the template, confirm output options, run).
    const QStringList picked = QFileDialog::getOpenFileNames(this, tr("Select Chapter 10 Files"),
        m_last_ch10_dir, tr("Chapter 10 Files (*.ch10)"));
    if (picked.isEmpty())
    {
        return;
    }

    confirmAndStartBatch(tmpl, picked, /*showReuseAppearance=*/true);
}







QString MainView::fullManualPathIn(const QString& app_root)
{
    // Empty means "not installed", which is a normal outcome - the full manual is an
    // optional installer task. Callers fall back to the compiled-in base manual.
    //
    // Static and root-parameterised so it can be tested against a temporary directory.
    // A test that probed the real app root would be answering a question about the
    // developer's checkout - where this file always exists, since it is generated and
    // tracked - rather than about the logic.
    if (app_root.isEmpty())
    {
        return QString();
    }
    const QString candidate = QDir(app_root).filePath(UIConstants::kFullManualFilename);
    return QFileInfo::exists(candidate) ? candidate : QString();
}

QString MainView::installedFullManualPath() const
{
    return (m_view_model == nullptr) ? QString()
                                     : fullManualPathIn(m_view_model->appRoot());
}

void MainView::applyApplicationTheme(bool dark)
{
    QFile qss_file(dark ? ":/resources/win11-dark.qss" : ":/resources/win11-light.qss");
    if (qss_file.open(QFile::ReadOnly))
    {
        qApp->setStyleSheet(QLatin1String(qss_file.readAll()));
        qss_file.close();
    }

    // The stylesheet paints widgets; it has no reach into a window's frame, which on
    // Windows is drawn by the system from Qt's colour scheme. Without this a dialog
    // in the light theme kept a dark title bar.
    QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark
                                                       : Qt::ColorScheme::Light);
}

void MainView::onToggleTheme()
{
    QSettings app_settings;
    QString current_theme = app_settings.value(UIConstants::kSettingsKeyTheme, UIConstants::kThemeDark).toString();
    QString new_theme = (current_theme == UIConstants::kThemeDark) ? UIConstants::kThemeLight : UIConstants::kThemeDark;

    applyApplicationTheme(new_theme == UIConstants::kThemeDark);

    app_settings.setValue(UIConstants::kSettingsKeyTheme, new_theme);

    m_theme_action->setText(
        (new_theme == UIConstants::kThemeDark) ? "Switch to Light Theme" : "Switch to Dark Theme");

    m_plot_widget->applyTheme(new_theme == UIConstants::kThemeDark);
    applyActionIconsForTheme(new_theme == UIConstants::kThemeDark);
}


void MainView::startProcessingFromDialog()
{
    m_view_model->startProcessing();
}

////////////////////////////////////////////////////////////////////////////////
//                            DRAG AND DROP                                   //
////////////////////////////////////////////////////////////////////////////////

bool MainView::eventFilter(QObject* obj, QEvent* event)
{
    // Only intercept drag events aimed at widgets that belong to this window.
    auto* widget = qobject_cast<QWidget*>(obj);
    if (!widget || widget->window() != this)
        return QMainWindow::eventFilter(obj, event);

    switch (event->type())
    {
    case QEvent::DragEnter:
        dragEnterEvent(static_cast<QDragEnterEvent*>(event));
        return true;
    case QEvent::DragMove:
        static_cast<QDragMoveEvent*>(event)->acceptProposedAction();
        return true;
    case QEvent::Drop:
        dropEvent(static_cast<QDropEvent*>(event));
        return true;
    default:
        break;
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        for (const QUrl& url : event->mimeData()->urls())
        {
            if (isSupportedFile(url.toLocalFile()))
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainView::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls())
    {
        QString file = url.toLocalFile();
        if (isSupportedFile(file))
        {
            openPath(file);
            return;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
//                              HELPER METHODS                                //
////////////////////////////////////////////////////////////////////////////////

void MainView::setAllControlsEnabled(bool enabled)
{
    m_open_action->setEnabled(enabled);
    m_recent_menu->setEnabled(enabled);
    m_import_action->setEnabled(enabled);
    // Save as Template additionally requires at least one finalized source.
    m_save_template_action->setEnabled(enabled && !m_view_model->sources().isEmpty());
    m_apply_template_action->setEnabled(enabled);
}

void MainView::logError(const QString& message)
{
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    QString html = "<span style='color: red; " + QString(kLogWhiteSpace) + "'>" + timestamp + "  " +
                   message.toHtmlEscaped() + "</span>";
    m_log_preview->append(html);
    m_log_preview->verticalScrollBar()->setValue(m_log_preview->verticalScrollBar()->maximum());
}

void MainView::logWarning(const QString& message)
{
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    QString html = "<span style='color: #DAA520; " + QString(kLogWhiteSpace) + "'>" + timestamp + "  " +
                   message.toHtmlEscaped() + "</span>";
    m_log_preview->append(html);
    m_log_preview->verticalScrollBar()->setValue(m_log_preview->verticalScrollBar()->maximum());
}

void MainView::logSuccess(const QString& message)
{
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    QString html = "<span style='color: green; " + QString(kLogWhiteSpace) + "'>" + timestamp + "  " +
                   message.toHtmlEscaped() + "</span>";
    m_log_preview->append(html);
    m_log_preview->verticalScrollBar()->setValue(m_log_preview->verticalScrollBar()->maximum());
}

void MainView::updateStatusBar()
{
    statusBar()->showMessage(m_view_model->fileMetadataSummary());
}


