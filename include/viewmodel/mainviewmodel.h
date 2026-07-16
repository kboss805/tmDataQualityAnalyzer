/**
 * @file mainviewmodel.h
 * @brief ViewModel mediating between the View (MainView) and Model layer.
 */

#ifndef MAINVIEWMODEL_H
#define MAINVIEWMODEL_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "processedstreamdata.h"
#include "processingparams.h"
#include "source.h"
#include "streamconfig.h"
#include "timefields.h"

class Chapter10Reader;
class FrameSetup;
class ProcessingCoordinator;
struct StreamJob;

/**
 * @brief Mediates between the View (MainView) and Model layer.
 *
 * Owns application state, validation, and the per-stream configuration captured
 * by StreamConfigDialog. Processing orchestration (worker-thread lifecycle) is
 * delegated to ProcessingCoordinator. The View binds to Q_PROPERTYs and connects
 * signals; it never performs business logic.
 */
class MainViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString inputFilename READ inputFilename NOTIFY inputFilenameChanged)
    Q_PROPERTY(QStringList timeChannelList READ timeChannelList NOTIFY channelListsChanged)
    Q_PROPERTY(QStringList pcmChannelList READ pcmChannelList NOTIFY channelListsChanged)
    Q_PROPERTY(int timeChannelIndex READ timeChannelIndex WRITE setTimeChannelIndex NOTIFY timeChannelIndexChanged)
    Q_PROPERTY(int pcmChannelIndex READ pcmChannelIndex WRITE setPcmChannelIndex NOTIFY pcmChannelIndexChanged)
    Q_PROPERTY(bool fileLoaded READ fileLoaded NOTIFY fileLoadedChanged)
    Q_PROPERTY(int progressPercent READ progressPercent NOTIFY progressPercentChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)



public:
    explicit MainViewModel(QObject* parent = nullptr);
    ~MainViewModel();

    MainViewModel(const MainViewModel&)            = delete;
    MainViewModel& operator=(const MainViewModel&) = delete;
    MainViewModel(MainViewModel&&)                 = delete;
    MainViewModel& operator=(MainViewModel&&)      = delete;

    /// @name Property getters
    /// @{
    QString inputFilename() const;              ///< @return Path to the loaded .ch10 file.
    QStringList timeChannelList() const;         ///< @return Display strings for time channel combo box.
    QStringList pcmChannelList() const;          ///< @return Display strings for PCM channel combo box.
    int timeChannelIndex() const;                ///< @return Currently selected time channel index.
    int pcmChannelIndex() const;                 ///< @return Currently selected PCM channel index.
    bool fileLoaded() const;                     ///< @return True if a .ch10 file is loaded.
    int progressPercent() const;                 ///< @return Current processing progress (0--100).
    bool processing() const;                     ///< @return True while background processing is active.


    /// @}

    /// @name Property setters
    /// @{
    void setTimeChannelIndex(int index);         ///< Sets the selected time channel index.
    void setPcmChannelIndex(int index);           ///< Sets the selected PCM channel index.
    /// @}

    /// @name Stream configuration
    /// @{

    /// @return Default per-stream configs (one per PCM channel in the loaded file).
    QVector<StreamConfig> buildDefaultStreamConfigs() const;
    /// Stores the per-stream configuration captured by StreamConfigDialog.
    void setStreamConfigs(const QVector<StreamConfig>& configs);
    /// @return The currently stored per-stream configuration.
    const QVector<StreamConfig>& streamConfigs() const;
    /// @}

    /// @name Multi-file sessions (Phase 1 multi-file input, docs/multi-file-input-design.md)
    /// @{
    /// @return Every source successfully processed so far this session (in add order).
    const QVector<Source>& sources() const;
    /// Loads a second (or later) .ch10 file's metadata WITHOUT resetting the session
    /// (contrast openFile(), which starts a fresh session). Emits
    /// sourceReadyForStreamConfig() on success so the View can configure and process
    /// it, appending its streams to the existing plot instead of replacing it.
    void addSource(const QString& filename);
    /// Removes a source's Source record from sources() (the View is responsible for
    /// also removing its plot series via PlotViewModel::removeSource(), since this
    /// ViewModel does not own the plot). A no-op if no source has @p sourceId; emits
    /// sourcesChanged() otherwise.
    void removeSource(int sourceId);
    /// @}

    /// @name Helpers
    /// @{

    /// @return Channel prefix string ("L", "R", "C", ...) for the given index.
    static QString channelPrefix(int index);
    /// @return Full parameter name (e.g., "L_RCVR1") for a channel/receiver pair.
    static QString parameterName(int channel_index, int receiver_index);

    /// Severity of a log line, classified from its text. The classification policy
    /// lives in the ViewModel (not the View) so it is single-sourced and testable;
    /// the View only maps the level to a render style.
    enum class LogLevel { Info, Success, Warning, Error };
    static LogLevel classifyLogMessage(const QString& message);

    /// @}

    /// @return Human-readable metadata summary for the status bar.
    QString fileMetadataSummary() const;

    /// @name Recent files
    /// @{
    QStringList recentFiles() const;             ///< @return List of recent file paths.
    void addRecentFile(const QString& filepath); ///< Adds a file to the recent files list.
    void clearRecentFiles();                     ///< Clears the recent files list.
    /// @}

    /// @name Model accessors
    /// @{
    Chapter10Reader* reader() const;             ///< @return Pointer to the Chapter10Reader instance.
    QString appRoot() const;                     ///< @return Application root directory path.
    QString lastIniDir() const;                  ///< @return Last directory used in TOML file dialogs.
    void setLastIniDir(const QString& dir);      ///< Records and persists the last TOML dialog directory.
    /// @}

    /// Logs startup configuration to the log window.
    void logStartupInfo();
    /// Opens a .ch10 file and populates channel lists.
    void openFile(const QString& filename);

    /**
     * @brief Validates the configured streams and starts background processing.
     */
    void startProcessing();

    /// Resets all state to defaults and closes the loaded file.
    void clearState();
    /// Requests cancellation of the current processing run.
    void cancelProcessing();

signals:
    void inputFilenameChanged();      ///< Emitted when the input file path changes.
    void channelListsChanged();       ///< Emitted when channel combo box lists are rebuilt.
    void timeChannelIndexChanged();   ///< Emitted when the selected time channel changes.
    void pcmChannelIndexChanged();    ///< Emitted when the selected PCM channel changes.
    void fileLoadedChanged();         ///< Emitted when the file-loaded state changes.
    void progressPercentChanged();    ///< Emitted when the processing progress updates.
    void processingChanged();         ///< Emitted when processing starts or stops.


    /// Emitted after a file loads successfully so the View can show StreamConfigDialog.
    /// The View clears the plot first -- this always starts a fresh session.
    void fileReadyForStreamConfig();
    /// Emitted after addSource() loads a second/later file's metadata so the View can
    /// show StreamConfigDialog for it. Unlike fileReadyForStreamConfig(), the View must
    /// NOT clear the plot -- this source's streams are meant to accumulate into it.
    void sourceReadyForStreamConfig();
    /// Emitted when sources() changes: a source finalized after a successful
    /// processing run, or removed via removeSource(), or reset by clearState().
    void sourcesChanged();

    /// Emitted when the recent files list changes.
    void recentFilesChanged();
    /// Emitted when a validation or processing error occurs.
    void errorOccurred(const QString& message);
    /// Emitted once per stream with its accumulated in-memory result.
    void streamProcessed(const ProcessedStreamData& data);
    /// Emitted when background processing finishes.
    void processingFinished(bool success);
    /// Emitted when the background processor sends a log message.
    void logMessageReceived(const QString& message);

private:
    /// Builds one StreamJob (params + owned FrameSetup) from a stream config. Returns false on error.
    bool buildStreamJob(const StreamConfig& cfg,
                        const ProcessingParams& base,
                        StreamJob& out_job,
                        QString& error);
    /// Validates and fills the time/file fields shared by all streams.
    bool buildBaseParams(ProcessingParams& out, QString& error);
    /// Shared metadata-loading logic behind openFile() and addSource(): opens a new
    /// Chapter10Reader for @p filename, and on success swaps it in as the active
    /// reader, sets m_input_filename, resets m_stream_configs, logs metadata, and
    /// records the recent file. Does NOT touch m_sources/m_pending_source_id or emit
    /// a "ready" signal -- the caller assigns the source id and picks which signal to
    /// emit (fileReadyForStreamConfig vs sourceReadyForStreamConfig) since that's the
    /// one behavioral difference between opening a fresh session and adding a source.
    bool loadFileMetadata(const QString& filename);
    /// Finalizes the pending source into m_sources on a successful processing run
    /// (see the ProcessingCoordinator::processingFinished connection), then re-emits
    /// processingFinished() for the View.
    void onCoordinatorProcessingFinished(bool success);

    Chapter10Reader*        m_reader;       ///< Reader backing the currently loaded file's channel data.
    ProcessingCoordinator*  m_coordinator;  ///< Owns processing worker thread(s).

    QString m_app_root;                      ///< Application root directory.
    QString m_input_filename;                ///< Path to the file currently open/being configured.
    QString m_last_ini_dir;                  ///< Last directory used in TOML file dialogs.
    bool m_file_loaded;                      ///< True when a .ch10 file is loaded.

    int m_time_channel_index;                ///< Selected time channel combo box index.
    int m_pcm_channel_index;                 ///< Selected PCM channel combo box index.

    QVector<StreamConfig> m_stream_configs;  ///< Per-stream configuration of the file currently being configured.
    QStringList m_recent_files;              ///< Most-recently-opened file paths.

    QVector<Source> m_sources;               ///< Every source successfully processed so far this session.
    int m_next_source_id  = 0;               ///< Monotonic id source; assigned to each opened/added file in order.
    int m_pending_source_id = 0;             ///< Id assigned to the file currently open/being configured.
};

#endif // MAINVIEWMODEL_H
