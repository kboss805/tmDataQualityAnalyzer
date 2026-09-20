/**
 * @file mainview.h
 * @brief Application main window — thin View layer delegating to MainViewModel.
 */

#ifndef MAINVIEW_H
#define MAINVIEW_H
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QMainWindow>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "processingtemplate.h"

class MainViewModel;
class PlotViewModel;
class PlotWidget;
class ProcessingProgressDialog;
class QToolButton;
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
    /// Captures one processed source's per-stream settings (+ series appearance)
    /// as a reusable, file-path-independent processing template (Batch Apply).
    void saveTemplateButtonPressed();
    /// Picks a template + a set of .ch10 files, validates each file's channel set
    /// against the template, and (on confirm) batch-processes every matching file
    /// (retaining all in memory; optional per-file CSV+image export).
    void applyTemplateButtonPressed();
    /// Toggles between light and dark themes.
    void onToggleTheme();

    /// @return Path to the full user manual under @p app_root, or empty if absent.
    ///
    /// Absence is a normal state, not an error: the full manual is an installer
    /// option, while the base manual is compiled in and can never be missing.
    /// Static and root-parameterised so it is testable against a temporary
    /// directory rather than the developer's own checkout.
    static QString fullManualPathIn(const QString& app_root);

    /// fullManualPathIn() applied to the ViewModel's app root.
    QString installedFullManualPath() const;
    /// Opens the StreamConfigDialog after a file has loaded (a fresh session).
    void onFileReadyForStreamConfig();
    /// Applies the batch template's config after the Apply Template loop's
    /// addSource() has loaded the next file's metadata (no dialog).
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
    /// Frameless window: intercepts WM_NCCALCSIZE (strip the native title bar) and
    /// WM_NCHITTEST (report the drag caption + resize borders so Windows still
    /// handles move/resize/snap/double-click-maximize natively).
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    /// Keeps the maximize/restore button glyph in sync with the window state.
    void changeEvent(QEvent* event) override;

private:
    /// @name Widget setup helpers
    /// @{
    void setUpMenuBar();                     ///< Builds the custom title bar (hamburger menu + window buttons).
    void setUpMainLayout();                  ///< Creates the top-level layout.
    void setUpConnections();                 ///< Connects all ViewModel signals to View slots.
    /// Sets the Import/Export menu-action icons to the dark or light theme variant.
    void applyActionIconsForTheme(bool dark);
    /// Updates the maximize/restore button glyph + tooltip from the window state.
    void updateMaximizeButton();
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

    /// @name Batch apply state machine (one sequential run per matched file)
    /// @{
    /// Starts the next not-yet-processed batch file: addSource() and waits for
    /// onSourceReadyForStreamConfig(); calls finishBatch() once every file has been
    /// attempted.
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

    QVBoxLayout* m_sidebar_layout;           ///< Left-side vertical sidebar layout.
    QDockWidget* m_sidebar_dock;             ///< Left sidebar dock (holds the log); toggled from the title bar.
    QToolButton* m_menu_button = nullptr;    ///< Title-bar hamburger button (opens the main menu).
    QToolButton* m_sidebar_toggle = nullptr; ///< Title-bar show/hide-sidebar button (checked = shown).
    PlotWidget* m_plot_widget;               ///< Plot view widget (central widget).
    PlotViewModel* m_plot_view_model;        ///< Plot ViewModel owning series data.
    QToolButton* m_max_button = nullptr;     ///< Title-bar maximize/restore button (glyph tracks window state).
    QAction* m_theme_action;                 ///< Toggle theme action (Settings section).
    QAction* m_open_action;                  ///< Open... action (Process section).
    QAction* m_save_template_action;         ///< File > Save as Template... action (Batch Apply).
    QAction* m_apply_template_action;        ///< File > Apply Template to Files... action (Batch Apply).

    QAction* m_import_action;                ///< File > Import CSV... action.
    QAction* m_export_action;                ///< File > Export... action.

    QTextBrowser* m_log_preview;             ///< Compact log preview in the sidebar.
    ProcessingProgressDialog* m_progress_dialog; ///< Modal progress/cancel dialog shown while processing runs.
    QMenu* m_recent_menu;                    ///< File > Recent Files submenu.
    QMenu* m_help_menu = nullptr;            ///< Help submenu (User Manual, About).

    QString m_last_ch10_dir;                 ///< Last directory used in the Open file dialog (.ch10/.csv).
    QString m_pending_csv_path;              ///< CSV import in flight; finalized on the plot's load result.

    /// @name Batch apply state (Batch Apply)
    /// @{
    bool m_batch_active = false;             ///< True while a batch-apply run is in flight.
    ProcessingTemplate m_batch_template;     ///< The template being applied to every batch file.
    QStringList m_batch_files;               ///< The matched files to process, in order.
    int  m_batch_index = 0;                  ///< Index into m_batch_files of the file in flight.
    bool m_batch_export_per_file = false;    ///< True = also write a CSV+image per file (post-pass) to m_batch_output_dir.
    bool m_batch_reuse_appearance = true;    ///< True = reapply the template's saved series names/colors.
    QString m_batch_output_dir;              ///< Output folder for per-file CSV/image exports.
    int  m_batch_processed = 0;              ///< Count of files successfully processed this batch.
    int  m_batch_skipped = 0;                ///< Count of files skipped/failed this batch.
    /// @}
};
#endif // MAINVIEW_H
