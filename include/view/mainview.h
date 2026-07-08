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
#include <QScrollBar>
#include <QStringList>
#include <QToolBar>
#include <QVBoxLayout>

#include "session.h"
#include "timefields.h"

class MainViewModel;
class PlotViewModel;
class PlotWidget;
class ProcessingProgressDialog;
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
    /// Opens a .ch10-filtered file dialog and adds the selected file as another
    /// source in the current session (accumulating, not replacing, the plot).
    void addSourceButtonPressed();
    /// Shows a picker of currently loaded sources and removes the selected one
    /// (drops its Source record and its plot series).
    void removeSourceButtonPressed();
    /// Writes the current sources + coarse plot view state to a session JSON file.
    void saveSessionButtonPressed();
    /// Opens a session JSON file and replays it over the Phase 1 pipeline: clears
    /// current state, then adds each source in order (skipping any whose file no
    /// longer exists), applying its saved view state once all sources finish.
    void openSessionButtonPressed();
    /// Toggles between light and dark themes.
    void onToggleTheme();
    /// Opens the StreamConfigDialog after a file has loaded (a fresh session).
    void onFileReadyForStreamConfig();
    /// Opens the StreamConfigDialog after addSource() has loaded a second/later
    /// file's metadata (accumulating into the existing session/plot).
    void onSourceReadyForStreamConfig();
    /// @}

    /// @name ViewModel-driven update slots
    /// @{
    /// Updates the progress dialog's progress bar value.
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
    /// Shows the Configure Streams dialog for the file MainViewModel just loaded
    /// metadata for, then starts processing on Accept. @p clearPlotFirst is true
    /// for a fresh Open (new session) and false for Add Source (accumulate).
    void showStreamConfigDialogForPendingSource(bool clearPlotFirst);
    /// Applies the pending session source's saved timeChannelIndex/streamConfigs
    /// (no dialog) and starts processing -- the session-load counterpart of
    /// showStreamConfigDialogForPendingSource().
    void applyPendingSessionSourceConfig();
    /// Starts the next not-yet-processed source in m_pending_session. A source
    /// whose resolved file no longer exists prompts the user via
    /// promptMissingSessionSource() (docs/session-save-load-design.md §5) rather
    /// than failing the whole load; calls finishSessionLoad() once every source
    /// has been attempted, or aborts immediately on Cancel.
    void advanceSessionLoad();

    /// User's choice when a session-load source's file can't be found (§5).
    enum class MissingSourceAction { Skip, Locate, Cancel };
    /// Shows the "file not found" prompt for @p missingPath and returns the
    /// user's choice.
    MissingSourceAction promptMissingSessionSource(const QString& missingPath);
    /// Applies m_pending_session's saved view state to the plot and clears
    /// m_loading_session -- the last step of an Open Session replay.
    void finishSessionLoad();
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
    QAction* m_add_source_action;            ///< File > Add Source... action (multi-file input).
    QAction* m_remove_source_action;         ///< File > Remove Source... action (multi-file input).
    QAction* m_save_session_action;          ///< File > Save Session As... action (session save/load).
    QAction* m_open_session_action;          ///< File > Open Session... action (session save/load).

    QToolBar* m_toolbar;                     ///< Main toolbar.
    QAction* m_toolbar_open_action;          ///< Toolbar open action.
    QAction* m_import_action;                ///< Toolbar import-CSV action (left of export).
    QAction* m_export_action;                ///< Toolbar export plot action.

    QTextBrowser* m_log_preview;             ///< Compact log preview in the controls panel.
    ProcessingProgressDialog* m_progress_dialog; ///< Modal progress/cancel dialog shown while processing runs.
    QMenu* m_recent_menu;                    ///< File > Recent Files submenu.

    QString m_last_ch10_dir;                 ///< Last directory used in the Open file dialog (.ch10/.csv).
    QString m_pending_csv_path;              ///< CSV import in flight; finalized on the plot's load result.

    /// @name Session load replay state (Phase 6)
    /// @{
    bool m_loading_session = false;          ///< True while replaying an opened session's sources.
    Session m_pending_session;               ///< The session being replayed.
    QString m_pending_session_dir;           ///< Directory of the session file (for relative path resolution).
    int m_pending_session_index = 0;         ///< Index into m_pending_session.sources of the source in flight.
    /// @}
};
#endif // MAINVIEW_H
