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
      m_pcm_channel_index(0),
      m_extract_all_time(true)
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
            this, &MainViewModel::processingFinished);
    connect(m_coordinator, &ProcessingCoordinator::logMessageReceived,
            this, &MainViewModel::logMessageReceived);
    connect(m_coordinator, &ProcessingCoordinator::errorOccurred,
            this, &MainViewModel::errorOccurred);
}

MainViewModel::~MainViewModel()
{
    // m_coordinator is a child QObject and is auto-deleted before these.
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

bool MainViewModel::extractAllTime() const { return m_extract_all_time; }

int MainViewModel::startDayOfYear() const { return m_reader->getStartDayOfYear(); }
int MainViewModel::startHour() const { return m_reader->getStartHour(); }
int MainViewModel::startMinute() const { return m_reader->getStartMinute(); }
int MainViewModel::startSecond() const { return m_reader->getStartSecond(); }
int MainViewModel::stopDayOfYear() const { return m_reader->getStopDayOfYear(); }
int MainViewModel::stopHour() const { return m_reader->getStopHour(); }
int MainViewModel::stopMinute() const { return m_reader->getStopMinute(); }
int MainViewModel::stopSecond() const { return m_reader->getStopSecond(); }

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

void MainViewModel::setExtractAllTime(bool value)
{
    if (m_extract_all_time == value)
    {
        return;
    }
    m_extract_all_time = value;
    emit extractAllTimeChanged();
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
        cfg.label = "Ch " + QString::number(ch.first);
        cfg.process = false;
        cfg.mode = StreamMode::FrameSyncLockStats;
        cfg.dataRateMbps = 0.0;

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

////////////////////////////////////////////////////////////////////////////////
//                               HELPERS                                      //
////////////////////////////////////////////////////////////////////////////////

QString MainViewModel::channelPrefix(int index)
{
    if (index < UIConstants::kNumKnownPrefixes)
    {
        return UIConstants::kChannelPrefixes[index];
    }
    return "CH" + QString::number(index + 1);
}

QString MainViewModel::parameterName(int channel_index, int receiver_index)
{
    return channelPrefix(channel_index) + "_RCVR" + QString::number(receiver_index + 1);
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

    QString time_range = QString("%1:%2:%3:%4 - %5:%6:%7:%8")
        .arg(m_reader->getStartDayOfYear(), 3, UIConstants::kDecimalBase, QChar('0'))
        .arg(m_reader->getStartHour(), 2, UIConstants::kDecimalBase, QChar('0'))
        .arg(m_reader->getStartMinute(), 2, UIConstants::kDecimalBase, QChar('0'))
        .arg(m_reader->getStartSecond(), 2, UIConstants::kDecimalBase, QChar('0'))
        .arg(m_reader->getStopDayOfYear(), 3, UIConstants::kDecimalBase, QChar('0'))
        .arg(m_reader->getStopHour(), 2, UIConstants::kDecimalBase, QChar('0'))
        .arg(m_reader->getStopMinute(), 2, UIConstants::kDecimalBase, QChar('0'))
        .arg(m_reader->getStopSecond(), 2, UIConstants::kDecimalBase, QChar('0'));

    return info.fileName() + "  |  " + size_str +
        "  |  Time: " + QString::number(time_count) +
        ", PCM: " + QString::number(pcm_count) +
        "  |  " + time_range;
}

////////////////////////////////////////////////////////////////////////////////
//                              COMMANDS                                      //
////////////////////////////////////////////////////////////////////////////////

void MainViewModel::logStartupInfo()
{
    emit logMessageReceived("tmDataQualityAnalyzer v" + AppVersion::toString());
    emit logMessageReceived("Load a Chapter 10 file to configure per-stream processing.");
}

void MainViewModel::openFile(const QString& filename)
{
    clearState();

    m_input_filename = filename;
    QFileInfo file_info(filename);
    emit logMessageReceived("Opening: " + file_info.fileName());

    if (!m_reader->loadChannels(filename))
    {
        m_input_filename.clear();
        return;
    }

    // Log file metadata
    qint64 file_bytes = file_info.size();
    QString size_str = (file_bytes >= UIConstants::kBytesPerMB)
        ? QString::number(static_cast<double>(file_bytes) / UIConstants::kBytesPerMB, 'f', 1) + " MB"
        : QString::number(static_cast<double>(file_bytes) / UIConstants::kBytesPerKB, 'f', 1) + " KB";
    int duration_sec = ((m_reader->getStopDayOfYear() - m_reader->getStartDayOfYear()) * UIConstants::kSecondsPerDay)
        + ((m_reader->getStopHour() - m_reader->getStartHour()) * UIConstants::kSecondsPerHour)
        + ((m_reader->getStopMinute() - m_reader->getStartMinute()) * UIConstants::kSecondsPerMinute)
        + (m_reader->getStopSecond() - m_reader->getStartSecond());
    emit logMessageReceived("  File size: " + size_str +
        ", Recording duration: " + QString::number(duration_sec) + "s");

    // Log channels found
    QStringList time_list = m_reader->getTimeChannelComboBoxList();
    QStringList pcm_list = m_reader->getPCMChannelComboBoxList();
    emit logMessageReceived("  Time channels: " + (time_list.isEmpty() ? "none" : QString::number(time_list.size())));
    emit logMessageReceived("  PCM channels: " + (pcm_list.isEmpty() ? "none" : QString::number(pcm_list.size())));

    m_file_loaded = true;
    m_stream_configs.clear();
    addRecentFile(filename);
    emit inputFilenameChanged();
    emit channelListsChanged();
    emit fileTimesChanged();
    emit fileLoadedChanged();
    emit fileReadyForStreamConfig();
}

void MainViewModel::startProcessing(const QString& start_time, const QString& stop_time)
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
    if (!buildBaseParams(start_time, stop_time, base, error))
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
            // Free any already-built jobs before bailing out.
            for (StreamJob& built : jobs)
            {
                delete built.frameSetup;
            }
            emit errorOccurred(error);
            return;
        }
        jobs.push_back(job);
    }

    if (jobs.isEmpty())
    {
        emit errorOccurred("No streams selected for processing. Configure streams first.");
        return;
    }

    emit logMessageReceived("--- Processing Summary ---");
    emit logMessageReceived("  Input: " + QFileInfo(m_input_filename).fileName());
    emit logMessageReceived("  Streams: " + QString::number(jobs.size()));
    if (m_extract_all_time)
    {
        emit logMessageReceived("  Time range: all");
    }
    else
    {
        emit logMessageReceived("  Time range: " +
            QString::number(base.startSeconds) + "s - " +
            QString::number(base.stopSeconds) + "s");
    }
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
    m_coordinator->reset();
    m_reader->clearSettings();

    emit inputFilenameChanged();
    emit channelListsChanged();
    emit fileLoadedChanged();
    emit fileTimesChanged();
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

bool MainViewModel::validateTimeFields(const QString& ddd, const QString& hh,
                                        const QString& mm, const QString& ss,
                                        TimeFields& out)
{
    bool ddd_ok = false;
    bool hh_ok = false;
    bool mm_ok = false;
    bool ss_ok = false;
    out.ddd = ddd.toInt(&ddd_ok);
    out.hh = hh.toInt(&hh_ok);
    out.mm = mm.toInt(&mm_ok);
    out.ss = ss.toInt(&ss_ok);

    return ddd_ok && hh_ok && mm_ok && ss_ok &&
           out.ddd >= UIConstants::kMinDayOfYear && out.ddd <= UIConstants::kMaxDayOfYear &&
           out.hh >= 0 && out.hh <= UIConstants::kMaxHour &&
           out.mm >= 0 && out.mm <= UIConstants::kMaxMinute &&
           out.ss >= 0 && out.ss <= UIConstants::kMaxSecond;
}

QString MainViewModel::validateTimeRange(const QString& start_text,
                                          const QString& stop_text)
{
    QStringList start_parts = start_text.split(":");
    QStringList stop_parts = stop_text.split(":");

    if (start_parts.size() != 4 || stop_parts.size() != 4)
    {
        return "Start and stop times must be in DDD:HH:MM:SS format.";
    }

    TimeFields s;
    if (!validateTimeFields(start_parts[0], start_parts[1],
                             start_parts[2], start_parts[3], s))
    {
        return "Start time is out of valid range. Day: 1-366, Hour: 0-23, Minute: 0-59, Second: 0-59.";
    }

    TimeFields e;
    if (!validateTimeFields(stop_parts[0], stop_parts[1],
                             stop_parts[2], stop_parts[3], e))
    {
        return "Stop time is out of valid range. Day: 1-366, Hour: 0-23, Minute: 0-59, Second: 0-59.";
    }

    long long start_total = (s.ddd * (long long)UIConstants::kSecondsPerDay)
        + (s.hh * (long long)UIConstants::kSecondsPerHour)
        + (s.mm * (long long)UIConstants::kSecondsPerMinute) + s.ss;
    long long stop_total = (e.ddd * (long long)UIConstants::kSecondsPerDay)
        + (e.hh * (long long)UIConstants::kSecondsPerHour)
        + (e.mm * (long long)UIConstants::kSecondsPerMinute) + e.ss;

    if (stop_total <= start_total)
    {
        return "Stop time must be after start time.";
    }

    return {};
}

bool MainViewModel::buildBaseParams(const QString& start_time, const QString& stop_time,
                                    ProcessingParams& out, QString& error)
{
    out.filename = m_input_filename;

    out.timeChannelId = m_reader->getCurrentTimeChannelID();
    if (out.timeChannelId < 0)
    {
        error = "Invalid time channel.";
        return false;
    }

    QStringList start_parts = start_time.split(":");
    if (start_parts.size() != 4)
    {
        error = "Invalid start time format.";
        return false;
    }
    TimeFields s;
    if (!validateTimeFields(start_parts[0], start_parts[1], start_parts[2], start_parts[3], s))
    {
        error = "Invalid start time.";
        return false;
    }
    out.startSeconds = m_reader->dhmsToUInt64(s.ddd, s.hh, s.mm, s.ss);

    QStringList stop_parts = stop_time.split(":");
    if (stop_parts.size() != 4)
    {
        error = "Invalid stop time format.";
        return false;
    }
    TimeFields e;
    if (!validateTimeFields(stop_parts[0], stop_parts[1], stop_parts[2], stop_parts[3], e))
    {
        error = "Invalid stop time.";
        return false;
    }
    out.stopSeconds = m_reader->dhmsToUInt64(e.ddd, e.hh, e.mm, e.ss);

    if (out.stopSeconds < out.startSeconds)
    {
        error = "Stop time must be after start time.";
        return false;
    }

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
    const QString& sync_hex = cfg.frameSyncPattern;
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
    const QString& mask_hex = cfg.frameSyncMask;
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

    const int bits_in_minor_frame = cfg.bitsInMinorFrame;
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
    out_job.params.isRandomized       = cfg.randomized;
    out_job.params.mode               = cfg.mode;
    out_job.params.dataRateBps        = (cfg.dataRateMbps > 0.0) ? cfg.dataRateMbps * 1e6 : 0.0;
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
        out_job.frameSetup = new FrameSetup(nullptr);
        return true;
    }

    auto* frame_setup = new FrameSetup(nullptr);
    if (cfg.receiverParamsToml.isEmpty())
    {
        // No Receiver Parameters TOML provided: build a default word map by
        // assigning sequential words to NumReceivers x ReceiverChannels parameters.
        const int total_params = cfg.numReceivers * cfg.receiverChannels;
        if (total_params <= 0 || total_params >= words_in_minor_frame)
        {
            delete frame_setup;
            error = stream_desc + ": Num Receivers x Receiver Channels exceeds the "
                    "words available in the minor frame.";
            return false;
        }
        for (int r = 0; r < cfg.numReceivers; r++)
        {
            for (int c = 0; c < cfg.receiverChannels; c++)
            {
                frame_setup->addParameter(parameterName(c, r), r * cfg.receiverChannels + c);
            }
        }
    }
    else if (!QFileInfo::exists(cfg.receiverParamsToml))
    {
        delete frame_setup;
        error = stream_desc + ": Receiver Parameters file '" +
                QFileInfo(cfg.receiverParamsToml).fileName() + "' was not found.";
        return false;
    }
    else if (!frame_setup->tryLoadingFile(cfg.receiverParamsToml, words_in_minor_frame))
    {
        delete frame_setup;
        error = stream_desc + ": Failed to load Receiver Parameters from '" +
                QFileInfo(cfg.receiverParamsToml).fileName() + "'. Check the word map.";
        return false;
    }
    if (frame_setup->length() == 0)
    {
        delete frame_setup;
        error = stream_desc + ": Receiver Parameters file contains no parameters.";
        return false;
    }

    // Read calibration directly from the StreamConfig (set via the Receiver SNR sub-dialog).
    int polarity_idx      = cfg.polarityIndex;
    int slope_idx         = cfg.slopeIndex;
    double scale_dB_per_V = cfg.scaleDdBPerV;

    if (scale_dB_per_V <= 0)
    {
        delete frame_setup;
        error = stream_desc + ": Scale (dB/V) must be a positive number.";
        return false;
    }
    if (slope_idx < 0 || slope_idx > UIConstants::kMaxSlopeIndex)
    {
        delete frame_setup;
        error = stream_desc + ": Slope index is invalid.";
        return false;
    }

    double voltage_lower = UIConstants::kSlopeVoltageLower[slope_idx] * scale_dB_per_V;
    double voltage_upper = UIConstants::kSlopeVoltageUpper[slope_idx] * scale_dB_per_V;
    bool negative_polarity = (polarity_idx == 1);

    // Apply slope/scale to every parameter and enable it for output.
    for (int i = 0; i < frame_setup->length(); i++)
    {
        ParameterInfo* param = frame_setup->getParameter(i);
        param->slope = (voltage_upper - voltage_lower) / PCMConstants::kMaxRawSampleValue;
        if (negative_polarity)
        {
            param->slope *= -1;
            param->scale = -voltage_upper / (voltage_upper - voltage_lower) * PCMConstants::kMaxRawSampleValue;
        }
        else
        {
            param->scale = voltage_lower / (voltage_upper - voltage_lower) * PCMConstants::kMaxRawSampleValue;
        }
        param->is_enabled = true;
        param->sample_sum = 0;
    }

    out_job.frameSetup = frame_setup;
    return true;
}
