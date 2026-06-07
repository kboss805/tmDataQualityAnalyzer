/**
 * @file mainview.h
 * @brief Application main window — thin View layer delegating to MainViewModel.
 */

#ifndef MAINVIEW_H
#define MAINVIEW_H

#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMimeData>
#include <QTextBrowser>
#include <QMenuBar>
#include <QProgressBar>
#include <QScrollBar>
#include <QStringList>
#include <QToolBar>
#include <QVBoxLayout>

#include "timefields.h"

class MainViewModel;
class PlotViewModel;
class PlotWidget;
class TimeExtractionWidget;
struct ProcessedStreamData;

/**
 * @brief Thin View layer: builds widgets, connects to ViewModel signals,
 *        and forwards user actions to MainViewModel.
 *
 * Delegates time controls to TimeExtractionWidget.
 */
class MainView : public QMainWindow
{
    Q_OBJECT

public:
    /// @param[in] parent Optional parent widget.
    explicit MainView(QWidget *parent = nullptr);
    ~MainView() override;
    Q_DISABLE_COPY_MOVE(MainView)

private slots:
    /// @name User-initiated action slots
    /// @{
    /// Shows a message box with the given error text.
    void displayErrorMessage(const QString& message);
    /// Opens a file dialog and loads the selected .ch10 file.
    void inputFileButtonPressed();
    /// Toggles between light and dark themes.
    void onToggleTheme();
    /// Validates inputs and starts background processing.
    void progressProcessButtonPressed();
    /// Opens the StreamConfigDialog after a file has loaded.
    void onFileReadyForStreamConfig();
    /// @}

    /// @name ViewModel-driven update slots
    /// @{
    /// Enables or disables controls based on file-loaded state.
    void onFileLoadedChanged();
    /// Fills the start/stop time fields from the loaded file.
    void onFileTimesChanged();
    /// Updates the progress bar value.
    void onProgressChanged();
    /// Updates UI state when processing starts or stops.
    void onProcessingChanged();
    /// Appends a freshly processed stream's data to the plot.
    void onStreamProcessed(const ProcessedStreamData& data);
    /// Handles completion of background processing.
    void onProcessingFinished(bool success);
    /// Appends a message to the log window.
    void onLogMessage(const QString& message);
    /// @}

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    /// @name Widget setup helpers
    /// @{
    void setUpMenuBar();                     ///< Creates the menu bar.
    void setUpMainLayout();                  ///< Creates the top-level layout.
    void setUpConnections();                 ///< Connects all ViewModel signals to View slots.
    /// @}

    /// @name Bulk state helpers
    /// @{
    void setAllControlsEnabled(bool enabled);          ///< Enables or disables all interactive controls.
    void saveLastCh10Dir();                              ///< Persists m_last_ch10_dir to QSettings.
    void logError(const QString& message);               ///< Appends a red error entry to the log window.
    void logWarning(const QString& message);             ///< Appends a dark-yellow warning entry to the log window.
    void logSuccess(const QString& message);             ///< Appends a green success entry to the log window.
    void updateStatusBar();                              ///< Refreshes the status bar from the ViewModel.
    void updateRecentFilesMenu();                        ///< Rebuilds the Recent Files submenu.
    /// @}

    MainViewModel* m_view_model;             ///< Owning ViewModel instance.

    QVBoxLayout* m_controls_layout;          ///< Left-side vertical controls layout.
    QDockWidget* m_controls_dock;            ///< Fixed left dock for controls panel.
    PlotWidget* m_plot_widget;               ///< Plot view widget (central widget).
    PlotViewModel* m_plot_view_model;        ///< Plot ViewModel owning series data.
    QAction* m_theme_action;                 ///< File > Toggle theme action.

    QToolBar* m_toolbar;                     ///< Main toolbar.
    QAction* m_toolbar_open_action;          ///< Toolbar open action.
    QAction* m_process_action;               ///< Toolbar process/play action.
    QAction* m_cancel_action;                ///< Toolbar cancel/stop action (visible during processing).

    TimeExtractionWidget* m_time_widget;     ///< Time extraction controls.

    QTextBrowser* m_log_preview;             ///< Compact log preview in the controls panel.
    QProgressBar* m_progress_bar;            ///< Processing progress bar.
    QMenu* m_recent_menu;                    ///< File > Recent Files submenu.

    QString m_last_ch10_dir;                 ///< Last directory used in Ch10 file dialogs.
    QString m_dialog_start_time;             ///< Start time text last confirmed in the stream config dialog.
    QString m_dialog_stop_time;              ///< Stop time text last confirmed in the stream config dialog.
};
#endif // MAINVIEW_H
