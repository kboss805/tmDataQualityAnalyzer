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

#include "constants.h"
#include "mainviewmodel.h"
#include "plotviewmodel.h"
#include "plotwidget.h"
#include "processedstreamdata.h"
#include "streamconfigdialog.h"
#include "timeextractionwidget.h"
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
    m_controls_layout->setContentsMargins(2, UIConstants::kLayoutSpacingSmall, UIConstants::kLayoutSpacingLarge, UIConstants::kLayoutSpacingSmall);

    // set up constituent parts
    setUpMenuBar();

    m_time_widget = new TimeExtractionWidget;

    m_progress_bar = new QProgressBar;
    m_progress_bar->setMinimum(0);
    m_progress_bar->setMaximum(UIConstants::kProgressBarMax);
    m_progress_bar->setValue(0);

    m_log_preview = new QTextBrowser;
    m_log_preview->setReadOnly(true);
    m_log_preview->setOpenLinks(false);
    m_log_preview->setMinimumHeight(UIConstants::kLogPreviewHeight);
    // Match log preview background to the surrounding panel (same gray as file section).
    QPalette log_palette = m_log_preview->palette();
    log_palette.setColor(QPalette::Base, qApp->palette().color(QPalette::Window));
    m_log_preview->setPalette(log_palette);

    // Log preview at the bottom of the controls panel, above the progress bar
    m_controls_layout->addWidget(m_log_preview, 1);
    m_controls_layout->addSpacing(4);
    m_controls_layout->addWidget(m_progress_bar);

    // PlotWidget as central widget — fills all space right of the controls dock
    m_plot_view_model = new PlotViewModel(this);
    m_plot_widget = new PlotWidget;
    m_plot_widget->setViewModel(m_plot_view_model);
    m_plot_widget->setMinimumSize(PlotConstants::kPlotDockMinWidth, PlotConstants::kPlotDockMinHeight);

    QSettings plot_settings;
    bool dark = plot_settings.value(UIConstants::kSettingsKeyTheme, UIConstants::kThemeDark).toString()
                == UIConstants::kThemeDark;
    m_plot_widget->applyTheme(dark);

    // Wrap plot in a layout with a vertical separator on the left
    QWidget* central_wrapper = new QWidget;
    QHBoxLayout* central_layout = new QHBoxLayout(central_wrapper);
    central_layout->setContentsMargins(0, 0, 0, 0);
    central_layout->setSpacing(UIConstants::kLayoutSpacingLarge);

    QFrame* vsep = new QFrame;
    vsep->setFrameShape(QFrame::VLine);
    vsep->setFrameShadow(QFrame::Sunken);
    central_layout->addWidget(vsep);
    central_layout->addWidget(m_plot_widget, 1);

    setCentralWidget(central_wrapper);

    // Controls in a fixed left dock widget
    QWidget* controls_widget = new QWidget;
    controls_widget->setLayout(m_controls_layout);
    controls_widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

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
    setWindowTitle("Chapter 10 to CSV AGC Converter");

    // Initialize time widget state (hidden — time controls live in the stream config dialog)
    m_time_widget->setAllEnabled(false);
    m_time_widget->setExtractAllTime(true);
    m_time_widget->clearTimes();
    // Keep the widget alive but hidden (parented to this so it's cleaned up)
    m_time_widget->hide();

    m_progress_bar->setValue(0);

    statusBar()->showMessage("No file loaded");

    showMaximized();
}

void MainView::setUpMenuBar()
{
    QMenuBar* menu_bar = menuBar();
    QMenu* file_menu = menu_bar->addMenu("&File");

    QAction* open_action = file_menu->addAction("Open...");
    open_action->setShortcut(QKeySequence::Open);
    m_recent_menu = file_menu->addMenu("Recent Files");
    updateRecentFilesMenu();
    file_menu->addSeparator();

    QSettings app_settings;
    QString current_theme = app_settings.value(UIConstants::kSettingsKeyTheme, UIConstants::kThemeDark).toString();
    m_theme_action = file_menu->addAction(
        (current_theme == UIConstants::kThemeDark) ? "Switch to Light Theme" : "Switch to Dark Theme");
    file_menu->addSeparator();

    QAction* exit_action = file_menu->addAction("Exit");

    connect(open_action, &QAction::triggered, this, &MainView::inputFileButtonPressed);
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
            "<h3>Chapter 10 to CSV AGC Converter</h3>"
            "<p>Version " + AppVersion::toString() + "</p>"
            "<p>Extracts PCM data from IRIG 106 Chapter 10 recordings "
            "and exports receiver channel samples to CSV format.</p>");
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
    m_toolbar_open_action->setToolTip("Open Ch10 File (Ctrl+O)");
    connect(m_toolbar_open_action, &QAction::triggered,
            this, &MainView::inputFileButtonPressed);

    m_toolbar->addSeparator();

    m_process_action = m_toolbar->addAction(
        QIcon(":/resources/play.svg"), "Process");
    m_process_action->setToolTip("Process Ch10 to CSV (Ctrl+R)");
    m_process_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_process_action->setEnabled(false);
    connect(m_process_action, &QAction::triggered,
            this, &MainView::progressProcessButtonPressed);

    m_cancel_action = m_toolbar->addAction(
        QIcon(":/resources/stop.svg"), "Cancel");
    m_cancel_action->setToolTip("Cancel Processing");
    m_cancel_action->setEnabled(false);
    connect(m_cancel_action, &QAction::triggered,
            this, [this]() { m_view_model->cancelProcessing(); });

    QWidget* toolbar_spacer = new QWidget;
    toolbar_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(toolbar_spacer);
}


////////////////////////////////////////////////////////////////////////////////
//                              GUI CONNECTIONS                               //
////////////////////////////////////////////////////////////////////////////////

void MainView::setUpConnections()
{
    // TimeExtractionWidget -> ViewModel
    connect(m_time_widget, &TimeExtractionWidget::extractAllTimeChanged,
            m_view_model, &MainViewModel::setExtractAllTime);
    // When "Extract All Time" is re-checked, restore the full file time range in the fields
    connect(m_time_widget, &TimeExtractionWidget::extractAllTimeChanged,
            this, [this](bool checked) {
                if (checked)
                {
                    m_time_widget->fillTimes(
                        {m_view_model->startDayOfYear(), m_view_model->startHour(),
                         m_view_model->startMinute(),    m_view_model->startSecond()},
                        {m_view_model->stopDayOfYear(),  m_view_model->stopHour(),
                         m_view_model->stopMinute(),     m_view_model->stopSecond()});
                }
            });
    // Clamp start/stop time fields to the file's actual time range on editingFinished
    auto clampTimeFn = [this](bool is_start) {
        // Parse a DDD:HH:MM:SS string to total seconds; returns -1 on parse error
        auto toSeconds = [](const QString& text) -> long long {
            const QStringList p = text.split(':');
            if (p.size() != 4) { return -1LL; }
            bool ok1, ok2, ok3, ok4;
            int d = p[0].toInt(&ok1), h = p[1].toInt(&ok2),
                m = p[2].toInt(&ok3), s = p[3].toInt(&ok4);
            if (!ok1 || !ok2 || !ok3 || !ok4) { return -1LL; }
            return d * 86400LL + h * 3600LL + m * 60LL + s;
        };

        auto fromSeconds = [](long long total, int& d, int& h, int& m, int& s) {
            d = static_cast<int>(total / 86400);
            int rem = static_cast<int>(total % 86400);
            h = rem / 3600; rem %= 3600;
            m = rem / 60;   s = rem % 60;
        };

        // File bounds (absolute seconds)
        long long file_min = m_view_model->startDayOfYear() * 86400LL
                           + m_view_model->startHour()   * 3600LL
                           + m_view_model->startMinute() * 60LL
                           + m_view_model->startSecond();
        long long file_max = m_view_model->stopDayOfYear() * 86400LL
                           + m_view_model->stopHour()   * 3600LL
                           + m_view_model->stopMinute() * 60LL
                           + m_view_model->stopSecond();

        const QString entered_start = m_time_widget->startTimeText();
        const QString entered_stop  = m_time_widget->stopTimeText();
        long long start_sec = toSeconds(entered_start);
        long long stop_sec  = toSeconds(entered_stop);

        // On parse failure restore the corresponding file bound and warn
        if (start_sec < 0)
        {
            logWarning(QString("Invalid start time \"%1\" — reset to file start.").arg(entered_start));
            start_sec = file_min;
        }
        if (stop_sec < 0)
        {
            logWarning(QString("Invalid stop time \"%1\" — reset to file stop.").arg(entered_stop));
            stop_sec = file_max;
        }

        // Clamp to file range and warn if out of bounds
        if (start_sec < file_min || start_sec > file_max)
        {
            logWarning(QString("Start time \"%1\" is outside the file time range — clamped to file bounds.").arg(entered_start));
            start_sec = qBound(file_min, start_sec, file_max);
        }
        if (stop_sec < file_min || stop_sec > file_max)
        {
            logWarning(QString("Stop time \"%1\" is outside the file time range — clamped to file bounds.").arg(entered_stop));
            stop_sec = qBound(file_min, stop_sec, file_max);
        }

        // Enforce ordering: if the field being edited conflicts, clamp it to the other
        if (start_sec > stop_sec)
        {
            if (is_start)
            {
                logWarning("Start time is after stop time — clamped to stop time.");
                start_sec = stop_sec;
            }
            else
            {
                logWarning("Stop time is before start time — clamped to start time.");
                stop_sec = start_sec;
            }
        }

        TimeFields start_tf, stop_tf;
        fromSeconds(start_sec, start_tf.ddd, start_tf.hh, start_tf.mm, start_tf.ss);
        fromSeconds(stop_sec,  stop_tf.ddd,  stop_tf.hh,  stop_tf.mm,  stop_tf.ss);
        m_time_widget->fillTimes(start_tf, stop_tf);
    };

    connect(m_time_widget, &TimeExtractionWidget::startTimeEditingFinished,
            this, [clampTimeFn]() { clampTimeFn(true); });
    connect(m_time_widget, &TimeExtractionWidget::stopTimeEditingFinished,
            this, [clampTimeFn]() { clampTimeFn(false); });

    // ViewModel -> View: data binding
    connect(m_view_model, &MainViewModel::fileLoadedChanged, this, &MainView::onFileLoadedChanged);
    connect(m_view_model, &MainViewModel::fileLoadedChanged, this, &MainView::updateStatusBar);
    connect(m_view_model, &MainViewModel::recentFilesChanged, this, &MainView::updateRecentFilesMenu);
    connect(m_view_model, &MainViewModel::fileTimesChanged, this, &MainView::onFileTimesChanged);
    connect(m_view_model, &MainViewModel::progressPercentChanged, this, &MainView::onProgressChanged);
    connect(m_view_model, &MainViewModel::processingChanged, this, &MainView::onProcessingChanged);
    connect(m_view_model, &MainViewModel::fileReadyForStreamConfig,
            this, &MainView::onFileReadyForStreamConfig);
    connect(m_view_model, &MainViewModel::streamProcessed, this, &MainView::onStreamProcessed);

    // ViewModel -> TimeExtractionWidget
    connect(m_view_model, &MainViewModel::extractAllTimeChanged, this, [this]() {
        m_time_widget->setExtractAllTime(m_view_model->extractAllTime());
    });

    connect(m_view_model, &MainViewModel::errorOccurred, this, &MainView::displayErrorMessage);
    connect(m_view_model, &MainViewModel::processingFinished, this, &MainView::onProcessingFinished);
    connect(m_view_model, &MainViewModel::logMessageReceived, this, &MainView::onLogMessage);

    // PlotWidget -> Log window
    connect(m_plot_widget, &PlotWidget::logMessage, this, &MainView::onLogMessage);
    connect(m_plot_view_model, &PlotViewModel::loadFailed, this, [this]() {
        displayErrorMessage("Failed to load CSV file for plotting.");
    });

    connect(m_log_preview, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        QDesktopServices::openUrl(url);
    });
}

////////////////////////////////////////////////////////////////////////////////
//                        VIEWMODEL-DRIVEN SLOTS                              //
////////////////////////////////////////////////////////////////////////////////


void MainView::onFileLoadedChanged()
{
    bool loaded = m_view_model->fileLoaded();

    m_process_action->setEnabled(loaded);

    if (!loaded)
    {
        m_progress_bar->setValue(0);
        m_process_action->setEnabled(false);
    }
}

void MainView::onFileTimesChanged()
{
    if (!m_view_model->fileLoaded())
    {
        return;
    }
    // Store file times into the hidden time widget so the dialog can read them.
    m_time_widget->fillTimes(
        {m_view_model->startDayOfYear(), m_view_model->startHour(),
         m_view_model->startMinute(),    m_view_model->startSecond()},
        {m_view_model->stopDayOfYear(),  m_view_model->stopHour(),
         m_view_model->stopMinute(),     m_view_model->stopSecond()});
}

void MainView::onProgressChanged()
{
    m_progress_bar->setValue(m_view_model->progressPercent());
}

void MainView::onProcessingChanged()
{
    if (m_view_model->processing())
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        setAllControlsEnabled(false);
        m_process_action->setEnabled(false);
        m_cancel_action->setEnabled(true);
        m_progress_bar->setValue(0);
    }
    else
    {
        QApplication::restoreOverrideCursor();
        setAllControlsEnabled(true);
        m_cancel_action->setEnabled(false);
    }
}

void MainView::onStreamProcessed(const ProcessedStreamData& data)
{
    // Append this stream's in-memory series to the (accumulating) plot.
    m_plot_view_model->addStreamData(data);
}

void MainView::onProcessingFinished(bool success)
{
    if (success)
    {
        m_progress_bar->setValue(UIConstants::kProgressBarMax);
        logSuccess("Processing complete — results plotted from memory.");
    }
}

void MainView::onLogMessage(const QString& message)
{
    if (message.contains("ERROR"))
    {
        logError(message);
    }
    else if (message.contains("WARNING"))
    {
        logWarning(message);
    }
    else if (message.startsWith("Pre-scan result:") || message.startsWith("Processing complete"))
    {
        logSuccess(message);
    }
    else
    {
        // Wrap in a neutral <span> so Qt treats this as explicit HTML and does not
        // auto-detect bare file paths (e.g. C:/...) as clickable anchors.
        QString html = "<span>" + QTime::currentTime().toString("HH:mm:ss") + "&nbsp;&nbsp;" +
                       message.toHtmlEscaped() + "</span>";
        m_log_preview->append(html);
        m_log_preview->verticalScrollBar()->setValue(m_log_preview->verticalScrollBar()->maximum());
    }
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
    QString filename = QFileDialog::getOpenFileName(this, tr("Open Ch10 File"),
                                                    m_last_ch10_dir,
                                                    tr("Chapter 10 Files (*.ch10);;All Files (*.*)"));
    if (filename.isEmpty())
    {
        return;
    }

    m_last_ch10_dir = QFileInfo(filename).absolutePath();
    saveLastCh10Dir();

    m_view_model->openFile(filename);
}

void MainView::onFileReadyForStreamConfig()
{
    // A fresh file starts a fresh plot; processing accumulates into it.
    m_plot_view_model->clearData();

    // Build time fields from the hidden time widget (populated in onFileTimesChanged).
    TimeFields start_tf {m_view_model->startDayOfYear(), m_view_model->startHour(),
                         m_view_model->startMinute(),    m_view_model->startSecond()};
    TimeFields stop_tf  {m_view_model->stopDayOfYear(),  m_view_model->stopHour(),
                         m_view_model->stopMinute(),     m_view_model->stopSecond()};

    StreamConfigDialog dialog(m_view_model->buildDefaultStreamConfigs(),
                              m_view_model->lastIniDir(),
                              m_view_model->timeChannelList(),
                              m_view_model->timeChannelIndex(),
                              start_tf,
                              stop_tf,
                              m_time_widget->extractAllTime(),
                              this);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_view_model->setTimeChannelIndex(dialog.timeChannelIndex());
        m_view_model->setStreamConfigs(dialog.configs());
        m_view_model->setExtractAllTime(dialog.extractAllTime());

        // Store the chosen time values back into the hidden widget
        // so progressProcessButtonPressed() can read them.
        m_time_widget->setExtractAllTime(dialog.extractAllTime());
        // Parse and store start/stop time from dialog for processing
        m_dialog_start_time = dialog.startTimeText();
        m_dialog_stop_time  = dialog.stopTimeText();

        int count = m_view_model->processableStreamCount();
        logSuccess(QString("Configured %1 stream(s) for processing. Press Process (\u25b6) to run.")
                       .arg(count));
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
}

void MainView::progressProcessButtonPressed()
{
    if (m_view_model->processing())
    {
        m_view_model->cancelProcessing();
        return;
    }

    // Use the time values stored from the last stream config dialog.
    bool extract_all = m_time_widget->extractAllTime();
    QString start_time = m_dialog_start_time;
    QString stop_time  = m_dialog_stop_time;

    if (!extract_all)
    {
        QString warning = MainViewModel::validateTimeRange(start_time, stop_time);
        if (!warning.isEmpty())
        {
            logWarning(warning);
            return;
        }
    }

    if (m_view_model->processableStreamCount() == 0)
    {
        logWarning("No streams configured for processing. Use the stream configuration dialog "
                   "(re-open the file) to enable one or more streams.");
        return;
    }

    // If extract all time, pass the full file range as the time bounds.
    if (extract_all)
    {
        start_time = QString("%1:%2:%3:%4")
            .arg(m_view_model->startDayOfYear(), 3, 10, QChar('0'))
            .arg(m_view_model->startHour(), 2, 10, QChar('0'))
            .arg(m_view_model->startMinute(), 2, 10, QChar('0'))
            .arg(m_view_model->startSecond(), 2, 10, QChar('0'));
        stop_time = QString("%1:%2:%3:%4")
            .arg(m_view_model->stopDayOfYear(), 3, 10, QChar('0'))
            .arg(m_view_model->stopHour(), 2, 10, QChar('0'))
            .arg(m_view_model->stopMinute(), 2, 10, QChar('0'))
            .arg(m_view_model->stopSecond(), 2, 10, QChar('0'));
    }

    m_view_model->startProcessing(start_time, stop_time);
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
            if (url.toLocalFile().endsWith(".ch10", Qt::CaseInsensitive))
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
        if (file.endsWith(".ch10", Qt::CaseInsensitive))
        {
            m_last_ch10_dir = QFileInfo(file).absolutePath();
            saveLastCh10Dir();
            m_view_model->openFile(file);
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
    m_process_action->setEnabled(enabled);
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
            m_last_ch10_dir = QFileInfo(filepath).absolutePath();
            saveLastCh10Dir();
            m_view_model->openFile(filepath);
        });
    }

    m_recent_menu->addSeparator();
    QAction* clear_action = m_recent_menu->addAction("Clear Recent Files");
    connect(clear_action, &QAction::triggered, this, [this]() {
        m_view_model->clearRecentFiles();
    });
}

