/**
 * @file mainview.cpp
 * @brief Implementation of MainView — Qt Widgets UI and ViewModel signal wiring.
 */

#include "mainview.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFrame>
#include <QMessageBox>
#include <QPixmap>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTime>
#include <QUrl>

#include "chapter10reader.h"
#include "constants.h"
#include "mainviewmodel.h"
#include "plotviewmodel.h"
#include "plotwidget.h"
#include "processedstreamdata.h"
#include "streamconfigdialog.h"
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

    m_progress_bar = new QProgressBar;
    m_progress_bar->setMinimum(0);
    m_progress_bar->setMaximum(UIConstants::kProgressBarMax);
    m_progress_bar->setValue(0);

    m_log_preview = new QTextBrowser;
    m_log_preview->setReadOnly(true);
    m_log_preview->setOpenLinks(false);
    m_log_preview->setMinimumHeight(UIConstants::kLogPreviewHeight);

    // Log preview at the bottom of the controls panel, above the progress bar
    m_controls_layout->addWidget(m_log_preview, 1);
    m_controls_layout->addSpacing(4);
    m_controls_layout->addWidget(m_progress_bar);

    // PlotWidget as central widget — fills all space right of the controls dock
    m_plot_view_model = new PlotViewModel(this);
    m_plot_widget = new PlotWidget;
    m_plot_widget->setViewModel(m_plot_view_model);
    m_plot_widget->setMinimumWidth(PlotConstants::kPlotDockMinWidth);

    QSettings plot_settings;
    bool dark = plot_settings.value(UIConstants::kSettingsKeyTheme, UIConstants::kThemeDark).toString()
                == UIConstants::kThemeDark;
    m_plot_widget->applyTheme(dark);
    applyToolbarIconsForTheme(dark);

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

    m_progress_bar->setValue(0);

    statusBar()->showMessage("No file loaded");
    
    resize(UIConstants::kInitialWindowWidth, UIConstants::kInitialWindowHeight);
}

void MainView::setUpMenuBar()
{
    QMenuBar* menu_bar = menuBar();
    QMenu* file_menu = menu_bar->addMenu("&File");

    m_open_action = file_menu->addAction("Open...");
    m_open_action->setShortcut(QKeySequence::Open);
    m_recent_menu = file_menu->addMenu("Recent Files");
    updateRecentFilesMenu();
    file_menu->addSeparator();

    QSettings app_settings;
    QString current_theme = app_settings.value(UIConstants::kSettingsKeyTheme, UIConstants::kThemeDark).toString();
    m_theme_action = file_menu->addAction(
        (current_theme == UIConstants::kThemeDark) ? "Switch to Light Theme" : "Switch to Dark Theme");
    file_menu->addSeparator();

    QAction* exit_action = file_menu->addAction("Exit");

    connect(m_open_action, &QAction::triggered, this, &MainView::inputFileButtonPressed);
    connect(m_theme_action, &QAction::triggered, this, &MainView::onToggleTheme);
    connect(exit_action, &QAction::triggered, this, &QMainWindow::close);

    QMenu* help_menu = menu_bar->addMenu("&Help");
    QAction* about_action = help_menu->addAction("About...");
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

    // Toolbar
    m_toolbar = addToolBar("Main");
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolbar->setIconSize(QSize(UIConstants::kToolbarIconSize, UIConstants::kToolbarIconSize));

    m_toolbar_open_action = m_toolbar->addAction(
        QIcon(":/resources/folder-open.svg"), "Open Ch10 File");
    m_toolbar_open_action->setToolTip("Open Chapter 10 File (Ctrl+O)");
    connect(m_toolbar_open_action, &QAction::triggered,
            this, &MainView::inputFileButtonPressed);

    m_toolbar->addSeparator();

    m_cancel_action = m_toolbar->addAction(
        QIcon(":/resources/stop.svg"), "Cancel");
    m_cancel_action->setToolTip("Cancel");
    m_cancel_action->setEnabled(false);
    connect(m_cancel_action, &QAction::triggered,
            this, [this]() { m_view_model->cancelProcessing(); });

    m_toolbar->addSeparator();

    // Import sits just left of Export. Icon (orange) is theme-dependent; set by
    // applyToolbarIconsForTheme().
    m_import_action = m_toolbar->addAction("Import");
    m_import_action->setToolTip("Import a previously exported CSV file");
    connect(m_import_action, &QAction::triggered,
            this, &MainView::importFileButtonPressed);

    // Icon (green) is theme-dependent; set by applyToolbarIconsForTheme().
    m_export_action = m_toolbar->addAction("Export");
    m_export_action->setToolTip("Export plot data and images");
    m_export_action->setEnabled(false);

    QWidget* toolbar_spacer = new QWidget;
    toolbar_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(toolbar_spacer);
}


////////////////////////////////////////////////////////////////////////////////
//                              GUI CONNECTIONS                               //
////////////////////////////////////////////////////////////////////////////////

void MainView::setUpConnections()
{
    // ViewModel -> View: data binding
    connect(m_view_model, &MainViewModel::fileLoadedChanged, this, &MainView::onFileLoadedChanged);
    connect(m_view_model, &MainViewModel::fileLoadedChanged, this, &MainView::updateStatusBar);
    connect(m_view_model, &MainViewModel::recentFilesChanged, this, &MainView::updateRecentFilesMenu);
    connect(m_view_model, &MainViewModel::progressPercentChanged, this, &MainView::onProgressChanged);
    connect(m_view_model, &MainViewModel::processingChanged, this, &MainView::onProcessingChanged);
    connect(m_view_model, &MainViewModel::fileReadyForStreamConfig,
            this, &MainView::onFileReadyForStreamConfig);
    connect(m_view_model, &MainViewModel::streamProcessed, this, &MainView::onStreamProcessed);

    connect(m_view_model, &MainViewModel::errorOccurred, this, &MainView::displayErrorMessage);
    connect(m_view_model, &MainViewModel::processingFinished, this, &MainView::onProcessingFinished);
    connect(m_view_model, &MainViewModel::logMessageReceived, this, &MainView::onLogMessage);

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


void MainView::onFileLoadedChanged()
{
    if (!m_view_model->fileLoaded())
        m_progress_bar->setValue(0);
}

void MainView::onProgressChanged()
{
    m_progress_bar->setValue(m_view_model->progressPercent());
}

void MainView::onProcessingChanged()
{
    if (m_view_model->processing())
    {
        setAllControlsEnabled(false);
        m_cancel_action->setEnabled(true);
        m_progress_bar->setValue(0);
    }
    else
    {
        setAllControlsEnabled(true);
        m_cancel_action->setEnabled(false);
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
    if (success)
    {
        m_progress_bar->setValue(UIConstants::kProgressBarMax);
        logSuccess("Processing complete — results plotted from memory.");
        m_plot_view_model->setPlotTitle(QFileInfo(m_view_model->inputFilename()).baseName());
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
    m_plot_view_model->clearData();

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
    applyToolbarIconsForTheme(new_theme == UIConstants::kThemeDark);
}

void MainView::applyToolbarIconsForTheme(bool dark)
{
    // Green (export) / orange (import) icons have brighter dark-theme variants and
    // deeper light-theme variants so they read against both toolbar backgrounds.
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
    m_toolbar_open_action->setEnabled(enabled);
    m_open_action->setEnabled(enabled);
    m_recent_menu->setEnabled(enabled);
    m_import_action->setEnabled(enabled);
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

