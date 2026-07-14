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

#include "processingtemplate.h"
#include "timefields.h"

class MainViewModel;
class PlotViewModel;
class PlotWidget;
class ProcessingProgressDialog;
struct ProcessedStreamData;
struct Source;

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
    /// Captures one processed source's per-stream settings (+ series appearance)
    /// as a reusable, file-path-independent processing template (Batch Apply).
    void saveTemplateButtonPressed();
    /// Picks a template + a set of .ch10 files, validates each file's channel set
    /// against the template, and (on confirm) processes every matching file in
    /// one batch -- either into one merged plot or as separate per-file output.
    void applyTemplateButtonPressed();
    /// Multi-selects .ch10 files that share the same channel IDs, shows the
    /// Configure Streams dialog once (against the first file), then batch-processes
    /// every matching file with that config -- no template file needed.
    void openMultipleButtonPressed();
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
    /// Builds a ProcessingTemplate from @p src's stream configs, capturing each
    /// stream's current series appearance (name/color) from the plot ViewModel.
    ProcessingTemplate buildTemplateFromSource(const Source& src) const;
    /// Shared batch kickoff: validates each of @p files against @p tmpl's channel
    /// set, shows BatchApplyDialog (@p showReuseAppearance gates the appearance
    /// option), and on confirm starts the batch run. Used by both Apply Template
    /// and Open Multiple Files.
    void startBatchFromTemplate(const ProcessingTemplate& tmpl, const QStringList& files,
                                bool showReuseAppearance);
    /// After the first Open-Multiple file's metadata loads: shows Configure Streams
    /// once, builds an in-memory template from it, and batch-processes m_multi_pending_files.
    void configureMultiThenBatch();

    /// @name Batch apply state machine (one sequential run per matched file)
    /// @{
    /// Starts the next not-yet-processed batch file: (separate mode) clears the
    /// plot first, then addSource() and waits for onSourceReadyForStreamConfig();
    /// calls finishBatch() once every file has been attempted.
    void advanceBatch();
    /// Applies the batch template's stream configs to the just-loaded file (after
    /// re-validating its channel set) and starts processing -- the batch
    /// counterpart of applyPendingSessionSourceConfig().
    void applyBatchSourceConfig();
    /// Handles a batch file's processing completion: reapplies template appearance,
    /// exports (separate mode), then advances to the next file.
    void onBatchProcessingFinished(bool success);
    /// Re-applies the batch template's captured series names/colors onto the
    /// freshly-created series of @p sourceId, matched by channel id + metric/rx/ch.
    void reapplyTemplateAppearance(int sourceId);
    /// Finalizes the batch: sets a merged title or logs the separate-output summary.
    void finishBatch();
    /// @}

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
    QAction* m_open_multiple_action;         ///< File > Open Multiple Files... action (direct batch).
    QAction* m_save_template_action;         ///< File > Save as Template... action (Batch Apply).
    QAction* m_apply_template_action;        ///< File > Apply Template to Files... action (Batch Apply).

    QToolBar* m_toolbar;                     ///< Main toolbar.
    QAction* m_toolbar_open_action;          ///< Toolbar open action.
    QAction* m_import_action;                ///< Toolbar import-CSV action (left of export).
    QAction* m_export_action;                ///< Toolbar export plot action.

    QTextBrowser* m_log_preview;             ///< Compact log preview in the controls panel.
    ProcessingProgressDialog* m_progress_dialog; ///< Modal progress/cancel dialog shown while processing runs.
    QMenu* m_recent_menu;                    ///< File > Recent Files submenu.

    QString m_last_ch10_dir;                 ///< Last directory used in the Open file dialog (.ch10/.csv).
    QString m_pending_csv_path;              ///< CSV import in flight; finalized on the plot's load result.

    /// @name Open Multiple Files state (direct batch)
    /// @{
    bool m_configuring_multi = false;        ///< True between openMultiple's file pick and its Configure Streams dialog.
    QStringList m_multi_pending_files;       ///< Files picked for an Open Multiple Files run.
    /// @}

    /// @name Batch apply state (Batch Apply)
    /// @{
    bool m_batch_active = false;             ///< True while a batch-apply run is in flight.
    ProcessingTemplate m_batch_template;     ///< The template being applied to every batch file.
    QStringList m_batch_files;               ///< The matched files to process, in order.
    int  m_batch_index = 0;                  ///< Index into m_batch_files of the file in flight.
    bool m_batch_merged = true;              ///< True = one merged plot; false = separate per-file output.
    bool m_batch_reuse_appearance = true;    ///< True = reapply the template's saved series names/colors.
    QString m_batch_output_dir;              ///< Output folder for separate-mode CSV/image exports.
    int  m_batch_processed = 0;              ///< Count of files successfully processed this batch.
    int  m_batch_skipped = 0;                ///< Count of files skipped/failed this batch.
    /// @}
};
#endif // MAINVIEW_H
