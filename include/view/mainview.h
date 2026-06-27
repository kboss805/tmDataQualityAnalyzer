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
struct ProcessedStreamData;

/**
 * @brief Thin View layer: builds widgets, connects to ViewModel signals,
 *        and forwards user actions to MainViewModel.
 *
 */
class MainView : public QMainWindow
{
    Q_OBJECT

public:
    /// @param[in] parent Optional parent widget.
    explicit MainView(QWidget *parent = nullptr);
    ~MainView() override;
    Q_DISABLE_COPY_MOVE(MainView)

    // Grants the unit test access to the private file-routing/import internals
    // (openPath/importCsv/isSupportedFile and m_plot_view_model) so the View-layer
    // CSV routing can be exercised without exposing it on the public API.
    friend class TestMainView;

private slots:
    /// @name User-initiated action slots
    /// @{
    /// Shows a message box with the given error text.
    void displayErrorMessage(const QString& message);
    /// Opens a file dialog and loads the selected .ch10 or .csv file.
    void inputFileButtonPressed();
    /// Opens a CSV-filtered file dialog and imports the selected exported CSV.
    void importFileButtonPressed();
    /// Toggles between light and dark themes.
    void onToggleTheme();
    /// Opens the StreamConfigDialog after a file has loaded.
    void onFileReadyForStreamConfig();
    /// @}

    /// @name ViewModel-driven update slots
    /// @{
    /// Enables or disables controls based on file-loaded state.
    void onFileLoadedChanged();
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
    /// Sets toolbar action icons (export/import) to the dark or light theme variant.
    void applyToolbarIconsForTheme(bool dark);
    /// @}

    /// @name Bulk state helpers
    /// @{
    void startProcessingFromDialog();                    ///< Starts background processing.
    void setAllControlsEnabled(bool enabled);          ///< Enables or disables all interactive controls.
    void saveLastCh10Dir();                              ///< Persists m_last_ch10_dir to QSettings.
    /// Routes a path to the .ch10 processing pipeline or the CSV importer by extension.
    void openPath(const QString& path);
    /// Loads a previously exported CSV straight into the plot (no processing pipeline).
    void importCsv(const QString& path);
    /// @return True if the path is a supported input (.ch10 or .csv).
    static bool isSupportedFile(const QString& path);
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
    QAction* m_open_action;                  ///< File > Open... action.

    QToolBar* m_toolbar;                     ///< Main toolbar.
    QAction* m_toolbar_open_action;          ///< Toolbar open action.
    QAction* m_cancel_action;                ///< Toolbar cancel/stop action (visible during processing).
    QAction* m_import_action;                ///< Toolbar import-CSV action (left of export).
    QAction* m_export_action;                ///< Toolbar export plot action.

    QTextBrowser* m_log_preview;             ///< Compact log preview in the controls panel.
    QProgressBar* m_progress_bar;            ///< Processing progress bar.
    QMenu* m_recent_menu;                    ///< File > Recent Files submenu.

    QString m_last_ch10_dir;                 ///< Last directory used in the Open file dialog (.ch10/.csv).
    QString m_pending_csv_path;              ///< CSV import in flight; finalized on the plot's load result.
};
#endif // MAINVIEW_H
