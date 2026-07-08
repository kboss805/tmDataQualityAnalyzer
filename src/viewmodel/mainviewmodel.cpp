/**
 * @file mainviewmodel.cpp
 * @brief Implementation of MainViewModel — UI state, stream configuration, and validation.
 *
 * Processing orchestration (worker-thread lifecycle) is delegated to
 * ProcessingCoordinator. Per-stream frame/receiver parameters are loaded from
 * the TOML files chosen in StreamConfigDialog.
 */

#include "mainviewmodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "chapter10reader.h"
#include "constants.h"
#include "framesetup.h"
#include "processingcoordinator.h"

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

    connect(m_reader, &Chapter10Reader::displayErrorMessage,
            this, &MainViewModel::errorOccurred);

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
    return m_reader->getTimeChannelComboBoxList();
}

QStringList MainViewModel::pcmChannelList() const
{
    return m_reader->getPCMChannelComboBoxList();
}

int MainViewModel::timeChannelIndex() const { return m_time_channel_index; }
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
    const QList<QPair<int, QString>> pcm_channels = m_reader->getPCMChannelList();
    configs.reserve(static_cast<int>(pcm_channels.size()));
    for (const auto& ch : pcm_channels)
    {
        StreamConfig cfg;
        cfg.pcmChannelId = ch.first;
        cfg.label = ch.second;
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

////////////////////////////////////////////////////////////////////////////////
//                               HELPERS                                      //
////////////////////////////////////////////////////////////////////////////////

QString MainViewModel::channelPrefix(int index)
{
    // Delegate to FrameSetup, the single source of truth for default parameter
    // naming shared with the calibration extractor.
    return FrameSetup::channelPrefix(index);
}

QString MainViewModel::parameterName(int channel_index, int receiver_index)
{
    return FrameSetup::receiverParameterName(channel_index, receiver_index);
}

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
    emit logMessageReceived("tmDataQualityAnalyzer v" + AppVersion::toString());
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
    connect(reader, &Chapter10Reader::displayErrorMessage,
            this, &MainViewModel::errorOccurred);

    if (!reader->loadChannels(filename))
    {
        // The attempted load never became the active reader; discard it and leave
        // m_reader (and whatever file it has loaded, if any) untouched.
        delete reader;
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
    QStringList time_list = m_reader->getTimeChannelComboBoxList();
    QStringList pcm_list = m_reader->getPCMChannelComboBoxList();
    emit logMessageReceived("  Time channels: " + (time_list.isEmpty() ? "none" : QString::number(time_list.size())));
    emit logMessageReceived("  PCM channels: " + (pcm_list.isEmpty() ? "none" : QString::number(pcm_list.size())));

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
        src.streamConfigs = m_stream_configs;
        m_sources.push_back(src);
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
        emit logMessageReceived("  " + job.params.streamLabel + ": "
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
    m_sources.clear();
    m_next_source_id = 0;
    m_pending_source_id = 0;
    m_coordinator->reset();
    m_reader->clearSettings();

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
    out.filename = m_input_filename;
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
    const QString stream_desc = cfg.label.isEmpty()
        ? ("Ch " + QString::number(cfg.pcmChannelId)) : cfg.label;

    // ---- Validate and decode inline frame sync fields ----
    const QString& sync_hex = cfg.sync.pattern;
    if (sync_hex.isEmpty())
    {
        error = stream_desc + ": A frame sync pattern is required.";
        return false;
    }
    bool sync_ok = false;
    uint64_t frame_sync = sync_hex.toULongLong(&sync_ok, UIConstants::kHexBase);
    if (!sync_ok)
    {
        error = stream_desc + ": Invalid frame sync pattern '" + sync_hex + "'.";
        return false;
    }
    int sync_pattern_length = static_cast<int>(sync_hex.length()) * 4;

    uint64_t frame_sync_mask = 0;
    const QString& mask_hex = cfg.sync.mask;
    if (mask_hex.isEmpty())
    {
        frame_sync_mask = (sync_pattern_length > 0 && sync_pattern_length < 64)
            ? (1ULL << sync_pattern_length) - 1
            : 0xFFFFFFFFFFFFFFFFULL;
    }
    else
    {
        bool mask_ok = false;
        frame_sync_mask = mask_hex.toULongLong(&mask_ok, UIConstants::kHexBase);
        if (!mask_ok)
        {
            error = stream_desc + ": Invalid frame sync mask '" + mask_hex + "'.";
            return false;
        }
    }

    const int bits_in_minor_frame = cfg.sync.bitsInMinorFrame;
    if (bits_in_minor_frame < PCMConstants::kMinFrameLengthBits)
    {
        error = stream_desc + ": Bits/Frame must be >= " +
                QString::number(PCMConstants::kMinFrameLengthBits) + ".";
        return false;
    }
    // Derive word count for the frame-setup word-map loader (ceiling division).
    int words_in_minor_frame =
        (bits_in_minor_frame + PCMConstants::kCommonWordLen - 1) / PCMConstants::kCommonWordLen;

    // ---- Assemble the per-stream ProcessingParams ----
    out_job.params = base;
    out_job.params.pcmChannelId       = cfg.pcmChannelId;
    out_job.params.frameSync          = frame_sync;
    out_job.params.frameSyncMask      = frame_sync_mask;
    out_job.params.syncPatternLength  = sync_pattern_length;
    out_job.params.wordsInMinorFrame  = words_in_minor_frame;
    out_job.params.bitsInMinorFrame   = bits_in_minor_frame;
    out_job.params.isRandomized       = cfg.sync.randomized;
    out_job.params.isInverted         = cfg.sync.inverted;
    out_job.params.mode               = cfg.mode;
    out_job.params.dataRateBps        = (cfg.sync.dataRateMbps > 0.0) ? cfg.sync.dataRateMbps * 1e6 : 0.0;
    out_job.params.streamLabel        = stream_desc;

    // Set per-stream sample period.
    switch (cfg.samplePeriodIndex)
    {
        case 0: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod1s;    break;
        case 1: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod100ms; break;
        case 2: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod10ms;  break;
        default: out_job.params.samplePeriodSec = UIConstants::kSamplePeriod100ms; break;
    }

    // ---- Frame parameter table (word map + calibration) ----
    if (cfg.mode == StreamMode::FrameSyncLockStats)
    {
        // Lock-only: no receiver parameters needed.
        out_job.frameSetup = std::make_shared<FrameSetup>(nullptr);
        return true;
    }

    // Built directly into out_job.frameSetup (not a local shared_ptr moved in at
    // the end) — that avoids a live-but-moved-from local sitting in scope after
    // its "last" use, which a future edit could silently dereference as null.
    out_job.frameSetup = std::make_shared<FrameSetup>(nullptr);
    if (cfg.receiverParamsToml.isEmpty())
    {
        // No Receiver Parameters TOML provided: fall back to the shipped default
        // Receiver Parameters file rather than synthesizing a sequential word map.
        const QString default_rcvr_params = m_app_root + "/" + UIConstants::kSettingsDirName +
            "/" + UIConstants::kReceiverParamsDirName + "/" + UIConstants::kDefaultTomlFilename;
        if (QFileInfo::exists(default_rcvr_params) &&
            out_job.frameSetup->tryLoadingFile(default_rcvr_params, words_in_minor_frame))
        {
            // Loaded successfully from the default file.
        }
        else
        {
            // Default file missing or unusable: build the default word map
            // (sequential words for NumReceivers x ReceiverChannels parameters).
            QString map_error;
            if (!out_job.frameSetup->buildDefaultReceiverMap(cfg.numReceivers, cfg.receiverChannels,
                                                             words_in_minor_frame, map_error))
            {
                error = stream_desc + ": " + map_error;
                return false;
            }
        }
    }
    else if (!QFileInfo::exists(cfg.receiverParamsToml))
    {
        error = stream_desc + ": Receiver Parameters file '" +
                QFileInfo(cfg.receiverParamsToml).fileName() + "' was not found.";
        return false;
    }
    else if (!out_job.frameSetup->tryLoadingFile(cfg.receiverParamsToml, words_in_minor_frame))
    {
        error = stream_desc + ": Failed to load Receiver Parameters from '" +
                QFileInfo(cfg.receiverParamsToml).fileName() + "'. Check the word map.";
        return false;
    }
    if (out_job.frameSetup->length() == 0)
    {
        error = stream_desc + ": Receiver Parameters file contains no parameters.";
        return false;
    }

    // Read calibration directly from the StreamConfig (set via the Receiver SNR sub-dialog).
    int polarity_idx      = cfg.polarityIndex;
    int slope_idx         = cfg.slopeIndex;
    double scale_dB_per_V = cfg.scaleDdBPerV;

    if (scale_dB_per_V <= 0)
    {
        error = stream_desc + ": Scale (dB/V) must be a positive number.";
        return false;
    }
    if (slope_idx < 0 || slope_idx > UIConstants::kMaxSlopeIndex)
    {
        error = stream_desc + ": Slope index is invalid.";
        return false;
    }

    // The linear voltage→dB math lives in the Model (FrameSetup) so it is
    // single-sourced and unit-testable rather than inline in the ViewModel.
    out_job.frameSetup->applyLinearCalibration(polarity_idx, slope_idx, scale_dB_per_V);

    // Attach non-linear step-calibration profiles (US5.3) by word index (Model side).
    if (!cfg.calibrationByWord.isEmpty())
    {
        const int attached = out_job.frameSetup->attachCalibrationProfiles(cfg.calibrationByWord);
        // Surface whether profiles actually reached the word map: provided > 0 but
        // attached == 0 means a word-index mismatch and a silent fall back to linear.
        emit logMessageReceived("  " + stream_desc + ": non-linear calibration — provided "
                    + QString::number(cfg.calibrationByWord.size())
                    + " profile(s), attached " + QString::number(attached) + " to word map.");
    }
    else
    {
        emit logMessageReceived("  " + stream_desc
            + ": no non-linear calibration profiles configured — using linear calibration.");
    }

    return true;
}
