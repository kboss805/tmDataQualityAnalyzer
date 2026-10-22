/**
 * @file mainviewmodel.cpp
 * @brief Implementation of MainViewModel — UI state, stream configuration, and validation.
 *
 * Processing orchestration (worker-thread lifecycle) is delegated to
 * ProcessingCoordinator. Per-stream frame/receiver parameters are loaded from
 * the TOML files chosen in StreamConfigDialog.
 */

#include "mainviewmodel.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "chapter10reader.h"
#include "constants.h"
#include "processingcoordinator.h"
#include "qtbridge.h"
#include "streamjobbuilder.h"

MainViewModel::MainViewModel(QObject* parent)
    : QObject(parent),
      m_file_loaded(false),
      m_time_channel_index(0),
      m_pcm_channel_index(-1)
{
    QString app_dir = QCoreApplication::applicationDirPath();
    QString one_up = QDir::cleanPath(app_dir + "/..");
    QString two_up = QDir::cleanPath(app_dir + "/../..");
    if (QDir(app_dir + "/" + UIConstants::kSettingsDirName).exists())
    {
        m_app_root = app_dir;
    }
    else if (QDir(one_up + "/" + UIConstants::kSettingsDirName).exists())
    {
        m_app_root = one_up;
    }
    else
    {
        m_app_root = two_up;
    }
    m_reader = new Chapter10Reader();

    QSettings app_settings;
    m_last_ini_dir = app_settings.value(UIConstants::kSettingsKeyLastTomlDir).toString();
    if (m_last_ini_dir.isEmpty())
    {
        m_last_ini_dir = m_app_root + "/" + UIConstants::kSettingsDirName;
    }

    // Load recent files, pruning non-existent entries
    QStringList saved_recent = app_settings.value(UIConstants::kSettingsKeyRecentFiles).toStringList();
    for (const QString& path : saved_recent)
    {
        if (QFileInfo::exists(path))
        {
            m_recent_files.append(path);
        }
    }

    m_coordinator = new ProcessingCoordinator(this);
    connect(m_coordinator, &ProcessingCoordinator::progressChanged,
            this, [this]() { emit progressPercentChanged(); });
    connect(m_coordinator, &ProcessingCoordinator::processingStateChanged,
            this, [this]() { emit processingChanged(); });
    connect(m_coordinator, &ProcessingCoordinator::streamProcessed,
            this, &MainViewModel::streamProcessed);
    connect(m_coordinator, &ProcessingCoordinator::processingFinished,
            this, &MainViewModel::onCoordinatorProcessingFinished);
    connect(m_coordinator, &ProcessingCoordinator::logMessageReceived,
            this, &MainViewModel::logMessageReceived);
    connect(m_coordinator, &ProcessingCoordinator::errorOccurred,
            this, &MainViewModel::errorOccurred);
}

MainViewModel::~MainViewModel()
{
    // m_coordinator is a child QObject and is auto-deleted after this body runs.
    delete m_reader;
}

////////////////////////////////////////////////////////////////////////////////
//                            PROPERTY GETTERS                                //
////////////////////////////////////////////////////////////////////////////////

QString MainViewModel::inputFilename() const { return m_input_filename; }

QStringList MainViewModel::timeChannelList() const
{
    return toQStringList(m_reader->getTimeChannelComboBoxList());
}

QStringList MainViewModel::pcmChannelList() const
{
    return toQStringList(m_reader->getPCMChannelComboBoxList());
}

int MainViewModel::timeChannelIndex() const { return m_time_channel_index; }

bool MainViewModel::swapBytes() const { return m_swap_bytes; }

void MainViewModel::setSwapBytes(bool on)
{
    if (m_swap_bytes == on) return;
    m_swap_bytes = on;
    emit swapBytesChanged();
}
int MainViewModel::pcmChannelIndex() const { return m_pcm_channel_index; }
bool MainViewModel::fileLoaded() const { return m_file_loaded; }
int MainViewModel::progressPercent() const { return m_coordinator->progressPercent(); }
bool MainViewModel::processing() const { return m_coordinator->processing(); }


////////////////////////////////////////////////////////////////////////////////
//                            PROPERTY SETTERS                                //
////////////////////////////////////////////////////////////////////////////////

void MainViewModel::setTimeChannelIndex(int index)
{
    if (m_time_channel_index == index)
    {
        return;
    }
    m_time_channel_index = index;
    m_reader->timeChannelChanged(index);
    emit timeChannelIndexChanged();
}

void MainViewModel::setPcmChannelIndex(int index)
{
    if (m_pcm_channel_index == index)
    {
        return;
    }
    m_pcm_channel_index = index;
    m_reader->pcmChannelChanged(index);
    emit pcmChannelIndexChanged();
}

////////////////////////////////////////////////////////////////////////////////
//                          STREAM CONFIGURATION                              //
////////////////////////////////////////////////////////////////////////////////

QVector<StreamConfig> MainViewModel::buildDefaultStreamConfigs() const
{
    QVector<StreamConfig> configs;
    const std::vector<std::pair<int, std::string>> pcm_channels = m_reader->getPCMChannelList();
    configs.reserve(static_cast<int>(pcm_channels.size()));
    for (const auto& ch : pcm_channels)
    {
        StreamConfig cfg;
        cfg.pcmChannelId = ch.first;
        cfg.label = QString::fromStdString(ch.second);
        cfg.process = false;
        cfg.mode = StreamMode::FrameSyncLockStats;
        cfg.sync.dataRateMbps = 0.0;

        // Populate the TMATS-reported bit rate so the dialog can display it.
        double bps = m_reader->getTmatsDataRateBps(ch.first);
        cfg.tmatsDataRateMbps = (bps > 0.0) ? bps / 1e6 : 0.0;

        configs.push_back(cfg);
    }
    return configs;
}

void MainViewModel::setStreamConfigs(const QVector<StreamConfig>& configs)
{
    m_stream_configs = configs;
}

const QVector<StreamConfig>& MainViewModel::streamConfigs() const
{
    return m_stream_configs;
}

const QVector<Source>& MainViewModel::sources() const
{
    return m_sources;
}

void MainViewModel::removeSource(int sourceId)
{
    const int before = static_cast<int>(m_sources.size());
    m_sources.erase(std::remove_if(m_sources.begin(), m_sources.end(),
                                   [sourceId](const Source& s) { return s.sourceId == sourceId; }),
                   m_sources.end());
    if (static_cast<int>(m_sources.size()) != before)
    {
        emit sourcesChanged();
    }
}

////////////////////////////////////////////////////////////////////////////////
//                               HELPERS                                      //
////////////////////////////////////////////////////////////////////////////////

MainViewModel::LogLevel MainViewModel::classifyLogMessage(const QString& message)
{
    // Severity cues, highest precedence first. These strings come from the
    // processing pipeline / reader; this is the single place that maps them to a
    // level so the View never has to sniff message text.
    if (message.contains("ERROR"))
        return LogLevel::Error;
    if (message.contains("WARNING"))
        return LogLevel::Warning;
    if (message.startsWith("Pre-scan result:") || message.startsWith("Processing complete"))
        return LogLevel::Success;
    return LogLevel::Info;
}

////////////////////////////////////////////////////////////////////////////////
//                            MODEL ACCESSORS                                 //
////////////////////////////////////////////////////////////////////////////////

Chapter10Reader* MainViewModel::reader() const { return m_reader; }
QString MainViewModel::appRoot() const { return m_app_root; }
QString MainViewModel::lastIniDir() const { return m_last_ini_dir; }

void MainViewModel::setLastIniDir(const QString& dir)
{
    m_last_ini_dir = dir;
    QSettings app_settings;
    app_settings.setValue(UIConstants::kSettingsKeyLastTomlDir, m_last_ini_dir);
}

QStringList MainViewModel::recentFiles() const { return m_recent_files; }

void MainViewModel::addRecentFile(const QString& filepath)
{
    m_recent_files.removeAll(filepath);
    m_recent_files.prepend(filepath);
    while (m_recent_files.size() > UIConstants::kMaxRecentFiles)
    {
        m_recent_files.removeLast();
    }

    QSettings app_settings;
    app_settings.setValue(UIConstants::kSettingsKeyRecentFiles, m_recent_files);
    emit recentFilesChanged();
}

void MainViewModel::clearRecentFiles()
{
    m_recent_files.clear();
    QSettings app_settings;
    app_settings.remove(UIConstants::kSettingsKeyRecentFiles);
    emit recentFilesChanged();
}

QString MainViewModel::fileMetadataSummary() const
{
    if (!m_file_loaded)
    {
        return "No file loaded";
    }

    QFileInfo info(m_input_filename);
    qint64 bytes = info.size();
    QString size_str = (bytes >= UIConstants::kBytesPerMB)
        ? QString::number(static_cast<double>(bytes) / UIConstants::kBytesPerMB, 'f', 1) + " MB"
        : QString::number(static_cast<double>(bytes) / UIConstants::kBytesPerKB, 'f', 1) + " KB";

    int time_count = static_cast<int>(m_reader->getTimeChannelComboBoxList().size());
    int pcm_count = static_cast<int>(m_reader->getPCMChannelComboBoxList().size());

    return info.fileName() + "  |  " + size_str +
        "  |  Time: " + QString::number(time_count) +
        ", PCM: " + QString::number(pcm_count);
}

////////////////////////////////////////////////////////////////////////////////
//                              COMMANDS                                      //
////////////////////////////////////////////////////////////////////////////////

void MainViewModel::logStartupInfo()
{
    emit logMessageReceived("tmDataQualityAnalyzer v" + QString::fromStdString(AppVersion::toString()));
    emit logMessageReceived("Load a Chapter 10 file to configure per-stream processing.");
}

bool MainViewModel::loadFileMetadata(const QString& filename)
{
    m_input_filename = filename;
    QFileInfo file_info(filename);
    emit logMessageReceived("Opening: " + file_info.fileName());

    // Channel enumeration reads only the file's first (TMATS) packet, which
    // enumerates every channel, so it runs synchronously on the GUI thread — no
    // worker thread, progress, or cancel machinery (and so no moveToThread races).
    auto* reader = new Chapter10Reader();

    if (!reader->loadChannels(toFsPath(filename)))
    {
        // The attempted load never became the active reader; discard it and leave
        // m_reader (and whatever file it has loaded, if any) untouched. The reader
        // reports why through lastError() rather than a signal.
        const QString reason = QString::fromStdString(reader->lastError());
        delete reader;
        emit errorOccurred(reason);
        m_input_filename.clear();
        return false;
    }

    // Success: the new reader becomes the active one.
    delete m_reader;
    m_reader = reader;

    // Log file metadata
    qint64 file_bytes = file_info.size();
    QString size_str = (file_bytes >= UIConstants::kBytesPerMB)
        ? QString::number(static_cast<double>(file_bytes) / UIConstants::kBytesPerMB, 'f', 1) + " MB"
        : QString::number(static_cast<double>(file_bytes) / UIConstants::kBytesPerKB, 'f', 1) + " KB";
    emit logMessageReceived("  File size: " + size_str);

    // Log channels found
    const int time_count = static_cast<int>(m_reader->getTimeChannelComboBoxList().size());
    const int pcm_count = static_cast<int>(m_reader->getPCMChannelComboBoxList().size());
    emit logMessageReceived("  Time channels: " + (time_count == 0 ? QString("none") : QString::number(time_count)));
    emit logMessageReceived("  PCM channels: " + (pcm_count == 0 ? QString("none") : QString::number(pcm_count)));

    m_file_loaded = true;
    m_stream_configs.clear();
    addRecentFile(m_input_filename);
    emit inputFilenameChanged();
    emit channelListsChanged();
    emit fileLoadedChanged();
    return true;
}

void MainViewModel::openFile(const QString& filename)
{
    if (m_coordinator->processing())
    {
        return;
    }

    // A fresh Open always starts a new session -- reset any prior sources.
    clearState();

    if (!loadFileMetadata(filename))
    {
        return;
    }

    m_pending_source_id = m_next_source_id++;
    emit fileReadyForStreamConfig();
}

void MainViewModel::addSource(const QString& filename)
{
    if (m_coordinator->processing())
    {
        return;
    }

    // Unlike openFile(): no clearState() -- this file's streams are meant to
    // accumulate into the existing plot/session, not replace it.
    if (!loadFileMetadata(filename))
    {
        return;
    }

    m_pending_source_id = m_next_source_id++;
    emit sourceReadyForStreamConfig();
}

void MainViewModel::onCoordinatorProcessingFinished(bool success)
{
    // Only a run that actually produced data becomes a permanent part of the
    // session -- a failed/fully-cancelled run leaves no plot series behind
    // either, so registering it as a "source" would be a phantom bookkeeping
    // entry with no data to show for it.
    if (success)
    {
        Source src;
        src.filepath = m_input_filename;
        src.sourceId = m_pending_source_id;
        src.timeChannelIndex = m_time_channel_index;
        src.swapBytes        = m_swap_bytes;
        src.streamConfigs = m_stream_configs;
        m_sources.push_back(src);
        emit sourcesChanged();
    }
    emit processingFinished(success);
}

void MainViewModel::startProcessing()
{
    if (m_coordinator->processing())
    {
        return;
    }

    if (m_input_filename.isEmpty())
    {
        emit errorOccurred("No file loaded.");
        return;
    }

    QString error;
    ProcessingParams base;
    if (!buildBaseParams(base, error))
    {
        emit errorOccurred(error);
        return;
    }

    QVector<StreamJob> jobs;
    for (const auto& cfg : m_stream_configs)
    {
        if (!cfg.process)
        {
            continue;
        }

        StreamJob job;
        if (!buildStreamJob(cfg, base, job, error))
        {
            // Already-built jobs free themselves: each StreamJob owns its FrameSetup.
            emit errorOccurred(error);
            return;
        }
        jobs.push_back(std::move(job));
    }

    if (jobs.isEmpty())
    {
        emit errorOccurred("No streams selected for processing. Configure streams first.");
        return;
    }

    emit logMessageReceived("--- Processing Summary ---");
    emit logMessageReceived("  Input: " + QFileInfo(m_input_filename).fileName());
    emit logMessageReceived("  Streams: " + QString::number(jobs.size()));
    emit logMessageReceived("  Time range: all");
    for (const StreamJob& job : jobs)
        emit logMessageReceived("  " + QString::fromStdString(job.params.streamLabel) + ": "
                                + QString::number(job.params.samplePeriodSec * 1000.0) + " ms period");

    m_coordinator->startProcessing(std::move(jobs));
}

void MainViewModel::clearState()
{
    m_input_filename.clear();
    m_file_loaded = false;
    m_time_channel_index = 0;
    m_pcm_channel_index = 0;
    m_stream_configs.clear();
    const bool had_sources = !m_sources.isEmpty();
    m_sources.clear();
    m_next_source_id = 0;
    m_pending_source_id = 0;
    m_coordinator->reset();
    m_reader->clearSettings();
    if (had_sources)
    {
        emit sourcesChanged();
    }

    emit inputFilenameChanged();
    emit channelListsChanged();
    emit fileLoadedChanged();

    emit progressPercentChanged();
    emit processingChanged();
}

void MainViewModel::cancelProcessing()
{
    m_coordinator->cancelProcessing();
}

////////////////////////////////////////////////////////////////////////////////
//                        VALIDATION & JOB BUILDING                           //
////////////////////////////////////////////////////////////////////////////////



bool MainViewModel::buildBaseParams(ProcessingParams& out, QString& error)
{
    out.filename = toFsPath(m_input_filename);
    out.sourceId = m_pending_source_id;

    out.timeChannelId = m_reader->getCurrentTimeChannelID();
    if (out.timeChannelId < 0)
    {
        error = "Invalid time channel.";
        return false;
    }

    out.startSeconds = 0;
    out.stopSeconds = std::numeric_limits<uint64_t>::max();

    return true;
}

bool MainViewModel::buildStreamJob(const StreamConfig& cfg,
                                   const ProcessingParams& base,
                                   StreamJob& out_job,
                                   QString& error)
{
    // The user's settings are Qt types; the job builder (the core) reads plain ones.
    StreamJobConfig job_cfg;
    job_cfg.pcmChannelId      = cfg.pcmChannelId;
    job_cfg.label             = cfg.label.toStdString();
    job_cfg.mode              = cfg.mode;
    job_cfg.syncPattern       = cfg.sync.pattern.toStdString();
    job_cfg.syncMask          = cfg.sync.mask.toStdString();
    job_cfg.bitsInMinorFrame  = cfg.sync.bitsInMinorFrame;
    job_cfg.randomized        = cfg.sync.randomized;
    job_cfg.inverted          = cfg.sync.inverted;
    job_cfg.dataRateMbps      = cfg.sync.dataRateMbps;
    job_cfg.samplePeriodIndex = cfg.samplePeriodIndex;
    job_cfg.polarityIndex     = cfg.polarityIndex;
    job_cfg.slopeIndex        = cfg.slopeIndex;
    job_cfg.scaleDdBPerV      = cfg.scaleDdBPerV;
    job_cfg.numReceivers      = cfg.numReceivers;
    job_cfg.receiverChannels  = cfg.receiverChannels;
    if (!cfg.receiverParamsToml.isEmpty())
    {
        job_cfg.receiverParamsToml = toFsPath(cfg.receiverParamsToml);
    }
    // The dialogs keep the extracted profiles in a QHash; the core takes a std map.
    for (auto it = cfg.calibrationByWord.constBegin(); it != cfg.calibrationByWord.constEnd(); ++it)
    {
        job_cfg.calibrationByWord.emplace(it.key(), it.value());
    }

    // File-scoped, so it comes from the view model rather than from cfg: every job built for this
    // recording gets the same value. Sourcing it per stream would allow one file's channels to
    // disagree about their own byte order.
    StreamJobContext context;
    context.appRoot   = m_app_root.toStdString();
    context.swapBytes = m_swap_bytes;

    StreamJobReport report;
    if (!::buildStreamJob(job_cfg, base, context, out_job, report))
    {
        error = QString::fromStdString(report.error);
        return false;
    }
    for (const std::string& line : report.log)
    {
        emit logMessageReceived(QString::fromStdString(line));
    }
    return true;
}
