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
    void fileReadyForStreamConfig();

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

    Chapter10Reader*        m_reader;       ///< Reader backing the currently loaded file's channel data.
    ProcessingCoordinator*  m_coordinator;  ///< Owns processing worker thread(s).

    QString m_app_root;                      ///< Application root directory.
    QString m_input_filename;                ///< Path to the loaded .ch10 file.
    QString m_last_ini_dir;                  ///< Last directory used in TOML file dialogs.
    bool m_file_loaded;                      ///< True when a .ch10 file is loaded.

    int m_time_channel_index;                ///< Selected time channel combo box index.
    int m_pcm_channel_index;                 ///< Selected PCM channel combo box index.



    QVector<StreamConfig> m_stream_configs;  ///< Per-stream configuration from StreamConfigDialog.
    QStringList m_recent_files;              ///< Most-recently-opened file paths.
};

#endif // MAINVIEWMODEL_H
