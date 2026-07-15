/**
 * @file mainview.cpp
 * @brief Implementation of MainView — Qt Widgets UI and ViewModel signal wiring.
 */

#include "mainview.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTime>
#include <QUrl>

#include "batchapplydialog.h"
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
#include "templatematcher.h"
#include "timefields.h"


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
    m_controls_layout = new QVBoxLayout;
    m_controls_layout->setSpacing(0);
    m_controls_layout->setContentsMargins(2, UIConstants::kLayoutSpacingSmall, UIConstants::kLayoutSpacingSmall, UIConstants::kLayoutSpacingSmall);

    // set up constituent parts
    setUpMenuBar();

    m_progress_dialog = new ProcessingProgressDialog(this);

    m_log_preview = new QTextBrowser;
    m_log_preview->setReadOnly(true);
    m_log_preview->setOpenLinks(false);
    m_log_preview->setMinimumHeight(UIConstants::kLogPreviewHeight);

    // Log preview fills the controls panel; progress/cancel now live in
    // ProcessingProgressDialog, shown only while processing is active.
    m_controls_layout->addWidget(m_log_preview, 1);

    // PlotWidget as central widget — fills all space right of the controls dock
    m_plot_view_model = new PlotViewModel(this);
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

    // Controls in a left dock widget
    QWidget* controls_widget = new QWidget;
    controls_widget->setLayout(m_controls_layout);
    controls_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    m_controls_dock = new QDockWidget(this);
    m_controls_dock->setTitleBarWidget(new QWidget);
    m_controls_dock->setWidget(controls_widget);
    m_controls_dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_controls_dock->setMinimumWidth(UIConstants::kControlsDockMinWidth);
    addDockWidget(Qt::LeftDockWidgetArea, m_controls_dock);

    // Intercept drag events from every widget in the window by filtering at
    // the application level — simpler and more complete than listing individual
    // child viewports.
    qApp->installEventFilter(this);

    // additional settings
    setWindowTitle("TM Data Quality Analyzer");

    statusBar()->showMessage("No file loaded");
    
    resize(UIConstants::kInitialWindowWidth, UIConstants::kInitialWindowHeight);
}

void MainView::setUpMenuBar()
{
    // A single hamburger menu holds everything, grouped into sections (addSection
    // renders the muted headers). Flat rather than nested submenus because the
    // whole menu is small and scannable in one glance.
    QMenu* menu = menuBar()->addMenu(QStringLiteral("☰"));
    menu->menuAction()->setToolTip(tr("Menu"));

    // --- Process ---
    menu->addSection(tr("Process"));

    m_open_action = menu->addAction("Open...");
    m_open_action->setShortcut(QKeySequence::Open);
    connect(m_open_action, &QAction::triggered, this, &MainView::inputFileButtonPressed);

    // Recent Files sits directly under Open -- both are "get a file onto the plot".
    m_recent_menu = menu->addMenu("Recent Files");
    updateRecentFilesMenu();

    // Save as Template: capture the current per-stream settings as a reusable,
    // file-path-independent template for Batch Apply. It needs at least one
    // finished source, so it starts disabled.
    m_save_template_action = menu->addAction("Save as Template...");
    m_save_template_action->setEnabled(false);
    connect(m_save_template_action, &QAction::triggered, this, &MainView::saveTemplateButtonPressed);

    // Apply Template to Files: always available -- it opens its own file pickers.
    m_apply_template_action = menu->addAction("Apply Template to Files...");
    connect(m_apply_template_action, &QAction::triggered, this, &MainView::applyTemplateButtonPressed);

    menu->addSeparator();

    QAction* exit_action = menu->addAction("Exit");
    connect(exit_action, &QAction::triggered, this, &QMainWindow::close);

    // --- Import/Export ---
    menu->addSection(tr("Import/Export"));

    // Import a previously exported CSV straight into the plot (US6.3).
    m_import_action = menu->addAction("Import CSV...");
    m_import_action->setToolTip("Import a previously exported CSV file");
    connect(m_import_action, &QAction::triggered, this, &MainView::importFileButtonPressed);

    // Export the current plot's data / image / log. Disabled until data loads;
    // wired to PlotWidget::onExportPlot in setUpConnections().
    m_export_action = menu->addAction("Export...");
    m_export_action->setToolTip("Export plot data and images");
    m_export_action->setEnabled(false);

    // --- Settings ---
    menu->addSection(tr("Settings"));

    QSettings app_settings;
    QString current_theme = app_settings.value(UIConstants::kSettingsKeyTheme, UIConstants::kThemeDark).toString();
    m_theme_action = menu->addAction(
        (current_theme == UIConstants::kThemeDark) ? "Switch to Light Theme" : "Switch to Dark Theme");
    connect(m_theme_action, &QAction::triggered, this, &MainView::onToggleTheme);

    // --- Help ---
    menu->addSection(tr("Help"));

    QAction* about_action = menu->addAction("About...");
    connect(about_action, &QAction::triggered, this, [this]() {
        QMessageBox about_box(this);
        about_box.setWindowTitle("About");
        about_box.setIconPixmap(QPixmap(":/resources/icon.ico").scaled(
            UIConstants::kAboutIconSize, UIConstants::kAboutIconSize,
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
        about_box.setText(
            "<h3>TM Data Quality Analyzer</h3>"
            "<p>Version " + AppVersion::toString() + "</p>"
            "<p>Analyzes framesync lock statistics and receiver SNR data from "
            "IRIG 106 Chapter 10 PCM recordings and plots the results over time.</p>");
        about_box.exec();
    });
}


////////////////////////////////////////////////////////////////////////////////
//                              GUI CONNECTIONS                               //
////////////////////////////////////////////////////////////////////////////////

void MainView::setUpConnections()
{
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
    if (m_batch_active)
    {
        onBatchProcessingFinished(success);
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
        html = "<span>" + timestamp + message.toHtmlEscaped() + "</span>";
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

ProcessingTemplate MainView::buildTemplateFromSource(const Source& src) const
{
    ProcessingTemplate tmpl;
    tmpl.appVersion       = AppVersion::toString();
    tmpl.name             = QFileInfo(src.filepath).baseName();
    tmpl.timeChannelIndex = src.timeChannelIndex;

    for (const StreamConfig& cfg : src.streamConfigs)
    {
        TemplateStreamEntry entry;
        entry.config = cfg;

        // Capture the current appearance of every plot series this stream produced,
        // keyed (within the entry) by metric/receiver/channel. Only series from this
        // source and this stream's channel id are this entry's; a non-processed
        // stream produces none, leaving the appearance list empty.
        for (const PlotSeriesData& series : m_plot_view_model->allSeries())
        {
            if (series.sourceId != src.sourceId || series.streamOrder != cfg.pcmChannelId)
                continue;

            SeriesAppearance appearance;
            appearance.metricType    = series.metricType;
            appearance.receiverIndex = series.receiverIndex;
            appearance.channelIndex  = series.channelIndex;
            appearance.name          = series.name;
            appearance.color         = series.color;
            entry.appearance.append(appearance);
        }

        tmpl.entries.append(entry);
    }
    return tmpl;
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

    const ProcessingTemplate tmpl = buildTemplateFromSource(sources.at(source_index));
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
    if (m_batch_active)
    {
        applyBatchSourceConfig();
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
                              this);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_view_model->setTimeChannelIndex(dialog.timeChannelIndex());
        m_view_model->setStreamConfigs(dialog.configs());

        startProcessingFromDialog();
    }
}

namespace
{
    /// Human-readable reason a file's channel set didn't match a template.
    QString describeMismatch(const TemplateMatcher::MatchResult& match)
    {
        QStringList parts;
        if (!match.missing.isEmpty())
        {
            QStringList ids;
            for (int id : match.missing)
                ids << QString::number(id);
            parts << QObject::tr("missing channel(s) %1").arg(ids.join(", "));
        }
        if (!match.extra.isEmpty())
        {
            QStringList ids;
            for (int id : match.extra)
                ids << QString::number(id);
            parts << QObject::tr("extra channel(s) %1").arg(ids.join(", "));
        }
        return parts.join("; ");
    }
}

void MainView::applyTemplateButtonPressed()
{
    if (m_view_model->processing() || m_batch_active)
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

    startBatchFromTemplate(tmpl, picked, /*showReuseAppearance=*/true);
}

void MainView::startBatchFromTemplate(const ProcessingTemplate& tmpl, const QStringList& files,
                                      bool showReuseAppearance)
{
    // Validate each file's channel set against the template, up front, so the
    // dialog can show which files will run and which are rejected (and why).
    QList<BatchApplyDialog::FileEntry> entries;
    entries.reserve(files.size());
    for (const QString& file_path : files)
    {
        BatchApplyDialog::FileEntry entry;
        entry.filepath = file_path;

        Chapter10Reader reader;
        if (!reader.loadChannels(file_path))
        {
            entry.ok = false;
            entry.reason = tr("could not read channels");
        }
        else
        {
            const TemplateMatcher::MatchResult match =
                TemplateMatcher::matchFile(tmpl, reader.getPCMChannelList());
            entry.ok = match.ok;
            if (!match.ok)
                entry.reason = describeMismatch(match);
        }
        entries.append(entry);
    }

    // Confirm output options (merged vs separate, appearance reuse, output dir).
    BatchApplyDialog dialog(entries, m_last_ch10_dir, showReuseAppearance, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    // Kick off the batch over the matched files only.
    m_batch_files.clear();
    for (const BatchApplyDialog::FileEntry& e : entries)
    {
        if (e.ok)
            m_batch_files.append(e.filepath);
    }
    if (m_batch_files.isEmpty())
    {
        return;
    }

    m_batch_template         = tmpl;
    m_batch_export_per_file  = dialog.exportPerFile();
    m_batch_reuse_appearance = dialog.reuseAppearance();
    m_batch_output_dir       = dialog.outputDir();
    m_batch_index            = 0;
    m_batch_processed        = 0;
    m_batch_skipped          = 0;
    m_batch_active           = true;

    // A batch always starts a fresh session/plot.
    m_view_model->clearState();
    m_plot_view_model->clearData();

    logSuccess(tr("Processing %1 file(s)...").arg(m_batch_files.size()));
    advanceBatch();
}

void MainView::advanceBatch()
{
    while (m_batch_index < m_batch_files.size())
    {
        const QString path = m_batch_files.at(m_batch_index);

        if (!QFileInfo::exists(path))
        {
            logWarning("Skipped missing batch file: " + path);
            ++m_batch_skipped;
            ++m_batch_index;
            continue;
        }

        // Every file is retained in memory (accumulated onto the shared axis) so the
        // user can browse them via the plot toolbar's file selector; per-file export,
        // if requested, runs as a post-pass in finishBatch().
        m_view_model->addSource(path);
        return; // wait for onSourceReadyForStreamConfig()
    }

    finishBatch();
}

void MainView::applyBatchSourceConfig()
{
    // Belt-and-suspenders re-check against the freshly loaded file (the up-front
    // validation could be stale if the file changed on disk since it was picked).
    const TemplateMatcher::MatchResult match =
        TemplateMatcher::matchFile(m_batch_template, m_view_model->reader()->getPCMChannelList());
    if (!match.ok)
    {
        logWarning("Skipped (channels no longer match template): "
                   + QFileInfo(m_view_model->inputFilename()).fileName());
        ++m_batch_skipped;
        ++m_batch_index;
        advanceBatch();
        return;
    }

    // Files match exactly, so the template's stored pcmChannelIds are correct for
    // this file -- feed its configs straight through to processing.
    QVector<StreamConfig> configs;
    configs.reserve(m_batch_template.entries.size());
    for (const TemplateStreamEntry& entry : m_batch_template.entries)
        configs.append(entry.config);

    m_view_model->setTimeChannelIndex(m_batch_template.timeChannelIndex);
    m_view_model->setStreamConfigs(configs);
    m_view_model->startProcessing();
}

void MainView::onBatchProcessingFinished(bool success)
{
    const QString base = QFileInfo(m_view_model->inputFilename()).baseName();

    if (success)
    {
        ++m_batch_processed;

        // The just-finished run was appended as the newest source before this
        // signal (MainViewModel::onCoordinatorProcessingFinished).
        const int sourceId = m_view_model->sources().isEmpty()
            ? 0 : m_view_model->sources().last().sourceId;

        // Label the source for the plot toolbar's file selector, and reapply the
        // template's saved names/colors onto this file's fresh series.
        m_plot_view_model->setSourceLabel(sourceId, base);
        if (m_batch_reuse_appearance)
            reapplyTemplateAppearance(sourceId);
    }
    else
    {
        ++m_batch_skipped;
        logError("Batch file failed to process: " + base);
    }

    ++m_batch_index;
    advanceBatch();
}

void MainView::reapplyTemplateAppearance(int sourceId)
{
    for (const TemplateStreamEntry& entry : m_batch_template.entries)
    {
        for (const SeriesAppearance& appearance : entry.appearance)
        {
            for (const PlotSeriesData& series : m_plot_view_model->allSeries())
            {
                if (series.sourceId != sourceId
                    || series.streamOrder != entry.config.pcmChannelId
                    || series.metricType != appearance.metricType
                    || series.receiverIndex != appearance.receiverIndex
                    || series.channelIndex != appearance.channelIndex)
                {
                    continue;
                }
                m_plot_view_model->renameSeriesById(series.id, appearance.name);
                m_plot_view_model->recolorSeriesById(series.id, appearance.color);
                break;
            }
        }
    }
    // renameSeriesById/recolorSeriesById are pure setters; one commit refreshes views.
    m_plot_view_model->commitAppearanceChanges();
}

void MainView::finishBatch()
{
    m_batch_active = false;

    const QVector<Source>& sources = m_view_model->sources();

    // Optional per-file export post-pass: isolate each source in turn (so the CSV
    // and the rendered images both cover just that file), then export. Runs over the
    // fully-retained plot after all files are processed.
    if (m_batch_export_per_file)
    {
        // One CSV per file (carries every metric's columns), plus, when the batch has
        // frame-sync data, a separate image for each left-axis metric: Frame Sync
        // Lock % and Accumulated Missed Frames. Restore the user's view afterward.
        const PlotViewModel::LockAxisView original_view = m_plot_view_model->lockAxisView();
        const bool has_frame_sync = m_plot_view_model->hasLockSeries()
                                    || m_plot_view_model->hasMissedFramesSeries();

        for (const Source& src : sources)
        {
            const QString base = QFileInfo(src.filepath).baseName();
            m_plot_view_model->setVisibleSource(src.sourceId); // rebuilds chart to this file

            const QString csv_path = QDir(m_batch_output_dir).filePath(base + ".csv");
            if (m_plot_view_model->exportCsv(csv_path, src.sourceId))
                logSuccess("Exported: " + csv_path);
            else
                logError("Failed to export CSV: " + csv_path);

            if (has_frame_sync)
            {
                m_plot_view_model->setLockAxisView(PlotViewModel::LockAxisView::LockPercent);
                m_plot_widget->exportImage(QDir(m_batch_output_dir).filePath(base + "_framesync_lock.png"));
                m_plot_view_model->setLockAxisView(PlotViewModel::LockAxisView::MissedFrames);
                m_plot_widget->exportImage(QDir(m_batch_output_dir).filePath(base + "_missed_frames.png"));
            }
            else
            {
                m_plot_widget->exportImage(QDir(m_batch_output_dir).filePath(base + ".png"));
            }
        }

        m_plot_view_model->setLockAxisView(original_view);
    }

    // Default the plot to the first processed file (browse intent); a single file
    // leaves the selector disabled and shows everything. "All files (overlaid)" is
    // available from the dropdown.
    if (sources.size() > 1)
    {
        m_plot_view_model->setVisibleSource(sources.first().sourceId);
        m_plot_view_model->setPlotTitle(QFileInfo(sources.first().filepath).baseName());
    }
    else if (sources.size() == 1)
    {
        m_plot_view_model->setPlotTitle(QFileInfo(sources.first().filepath).baseName());
    }

    logSuccess(tr("Batch complete: %1 processed, %2 skipped.%3")
                   .arg(m_batch_processed).arg(m_batch_skipped)
                   .arg(m_batch_export_per_file ? tr(" Output written to %1.").arg(m_batch_output_dir)
                                                : QString()));
}

void MainView::onToggleTheme()
{
    QSettings app_settings;
    QString current_theme = app_settings.value(UIConstants::kSettingsKeyTheme, UIConstants::kThemeDark).toString();
    QString new_theme = (current_theme == UIConstants::kThemeDark) ? UIConstants::kThemeLight : UIConstants::kThemeDark;

    QString qss_path = (new_theme == UIConstants::kThemeLight)
        ? ":/resources/win11-light.qss"
        : ":/resources/win11-dark.qss";

    QFile qss_file(qss_path);
    if (qss_file.open(QFile::ReadOnly))
    {
        qobject_cast<QApplication*>(QApplication::instance())->setStyleSheet(
            QLatin1String(qss_file.readAll()));
        qss_file.close();
    }

    app_settings.setValue(UIConstants::kSettingsKeyTheme, new_theme);

    m_theme_action->setText(
        (new_theme == UIConstants::kThemeDark) ? "Switch to Light Theme" : "Switch to Dark Theme");

    m_plot_widget->applyTheme(new_theme == UIConstants::kThemeDark);
    applyActionIconsForTheme(new_theme == UIConstants::kThemeDark);
}

void MainView::applyActionIconsForTheme(bool dark)
{
    // Green (export) / orange (import) menu-item icons have brighter dark-theme
    // variants and deeper light-theme variants so they read against both themes.
    const QString suffix = dark ? "-dark" : "-light";
    m_export_action->setIcon(QIcon(":/resources/export" + suffix + ".svg"));
    m_import_action->setIcon(QIcon(":/resources/import" + suffix + ".svg"));
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
    QString html = "<span style='color: red;'>" + timestamp + "  " +
                   message.toHtmlEscaped() + "</span>";
    m_log_preview->append(html);
    m_log_preview->verticalScrollBar()->setValue(m_log_preview->verticalScrollBar()->maximum());
}

void MainView::logWarning(const QString& message)
{
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    QString html = "<span style='color: #DAA520;'>" + timestamp + "  " +
                   message.toHtmlEscaped() + "</span>";
    m_log_preview->append(html);
    m_log_preview->verticalScrollBar()->setValue(m_log_preview->verticalScrollBar()->maximum());
}

void MainView::logSuccess(const QString& message)
{
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    QString html = "<span style='color: green;'>" + timestamp + "  " +
                   message.toHtmlEscaped() + "</span>";
    m_log_preview->append(html);
    m_log_preview->verticalScrollBar()->setValue(m_log_preview->verticalScrollBar()->maximum());
}

void MainView::updateStatusBar()
{
    statusBar()->showMessage(m_view_model->fileMetadataSummary());
}

void MainView::updateRecentFilesMenu()
{
    m_recent_menu->clear();
    QStringList recent = m_view_model->recentFiles();

    if (recent.isEmpty())
    {
        QAction* placeholder = m_recent_menu->addAction("(No recent files)");
        placeholder->setEnabled(false);
        return;
    }

    for (const QString& filepath : recent)
    {
        QString display = QFileInfo(filepath).fileName();
        QAction* action = m_recent_menu->addAction(display);
        action->setToolTip(filepath);
        connect(action, &QAction::triggered, this, [this, filepath]() {
            openPath(filepath);
        });
    }

    m_recent_menu->addSeparator();
    QAction* clear_action = m_recent_menu->addAction("Clear Recent Files");
    connect(clear_action, &QAction::triggered, this, [this]() {
        m_view_model->clearRecentFiles();
    });
}

