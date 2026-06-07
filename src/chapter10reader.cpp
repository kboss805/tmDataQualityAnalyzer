/**
 * @file chapter10reader.cpp
 * @brief Implementation of Chapter10Reader — IRIG 106 file metadata scanner.
 */

#include "chapter10reader.h"

#include "constants.h"

using namespace Irig106;

Chapter10Reader::Chapter10Reader(QObject* parent) :
    QObject(parent),
    m_status(I106_OK),
    m_filename(""),
    m_file_handle(0),
    m_header(),
    m_relative_start_time{},
    m_relative_stop_time{},
    m_file_start_time(),
    m_file_stop_time(),
    m_time_difference(0),
    m_irig_time(),
    m_tmats_info(),
    m_current_time_channel(-1),
    m_current_pcm_channel(-1)
{
    m_buffer.resize(PCMConstants::kDefaultBufferSize);

    // Initialize start/stop time structures to zero
    memset(&m_file_start_time, 0, sizeof(tm));
    memset(&m_file_stop_time, 0, sizeof(tm));
}

Chapter10Reader::~Chapter10Reader()
{
    qDeleteAll(m_channel_data);
}

bool Chapter10Reader::tryLoadingFile(const QString& filename)
{
    m_status = enI106Ch10Open(&m_file_handle, filename.toLocal8Bit().constData(), I106_READ);

    if (m_status != I106_OK && m_status != I106_OPEN_WARNING)
    {
        emit displayErrorMessage(QString("Error opening data file."));
        return false;
    }

    m_status = enI106_SyncTime(m_file_handle, bFALSE, 0);

    if (m_status != I106_OK)
    {
        emit displayErrorMessage(QString("Error establishing time sync."));
        return false;
    }

    m_filename = filename;

    return true;
}

void Chapter10Reader::closeFile() const
{
    enI106Ch10Close(m_file_handle);
}

void Chapter10Reader::clearSettings()
{
    qDeleteAll(m_channel_data);
    m_channel_data.clear();
    m_time_channels.clear();
    m_pcm_channels.clear();
    m_current_time_channel = -1; // no channel selected
    m_current_pcm_channel = -1;
}

void Chapter10Reader::inferChannelTypeFromHeader(int channel_id)
{
    if (!m_channel_data[channel_id]->channelType().isEmpty())
    {
        return;
    }
    if (m_header.ubyDataType == I106CH10_DTYPE_IRIG_TIME)
    {
        m_channel_data[channel_id]->setChannelType(PCMConstants::kChannelTypeTime);
        if (m_channel_data[channel_id]->channelName().isEmpty())
        {
            m_channel_data[channel_id]->setChannelName("Time");
        }
    }
    else if (m_header.ubyDataType == I106CH10_DTYPE_PCM_FMT_1)
    {
        m_channel_data[channel_id]->setChannelType(PCMConstants::kChannelTypePcm);
        if (m_channel_data[channel_id]->channelName().isEmpty())
        {
            m_channel_data[channel_id]->setChannelName("PCM");
        }
    }
}

bool Chapter10Reader::loadChannels(const QString& filename)
{
    m_filename = filename;
    QByteArray ba_filename = m_filename.toLocal8Bit();
    char* psz_filename = ba_filename.data();

    // Open the file
    m_status = enI106Ch10Open(&m_file_handle, psz_filename, I106_READ);
    if (m_status != I106_OK)
    {
        emit displayErrorMessage("Error opening file: " + m_filename);
        return false;
    }

    // Establish time reference so enI106_Rel2IrigTime() can convert
    // relative header timestamps to absolute IRIG time.
    m_status = enI106_SyncTime(m_file_handle, bFALSE, 0);
    if (m_status != I106_OK)
    {
        emit displayErrorMessage("Error establishing time sync.");
        closeFile();
        return false;
    }

    bool found_start_time = false;
    qDeleteAll(m_channel_data);
    m_channel_data.clear();

    while (true)
    {
        // Read the next header
        m_status = enI106Ch10ReadNextHeader(m_file_handle, &m_header);
        if (m_status == I106_EOF)
        {
            break;
        }

        if (m_status != I106_OK)
        {
            break;
        }

        // Make sure our buffer is big enough
        if (m_buffer.size() < static_cast<qsizetype>(uGetDataLen(&m_header)))
        {
            try {
                m_buffer.resize(uGetDataLen(&m_header));
            } catch (const std::bad_alloc&) {
                emit displayErrorMessage("Memory allocation failed.");
                closeFile();
                return false;
            }
        }

        // Read the data buffer
        m_status = enI106Ch10ReadData(m_file_handle, static_cast<unsigned long>(m_buffer.size()), m_buffer.data());

        // Check for data read errors
        if (m_status != I106_OK)
        {
            break;
        }

        int channel_id = m_header.uChID;

        // If the channel is not in the map, add it
        if (!m_channel_data.contains(channel_id))
        {
            m_channel_data.insert(channel_id, new ChannelData(channel_id));
        }
        m_channel_data[channel_id]->incrementChannelCount();

        // Set channel type and fallback name from packet header when not already set by TMATS
        inferChannelTypeFromHeader(channel_id);

        processPacketTime(m_header, found_start_time);

        // Check for TMATS
        if (m_header.ubyDataType == I106CH10_DTYPE_TMATS)
        {
            if (!processTmatsPacket(m_header))
            {
                break;
            }
        }
    } // end while

    finalizeTimeCalc();
    categorizeChannels();
    closeFile();

    return true;
}

void Chapter10Reader::addChannelInfoEntry(int channel_id)
{
    // If an entry for channel_id doesn't exist in m_channel_info, make one.
    // If the entry already exists, do nothing.
    if (!m_channel_data.contains(channel_id))
    {
        m_channel_data.insert(channel_id, new ChannelData(channel_id));
    }
}

QStringList Chapter10Reader::buildChannelComboBoxList(const QList<ChannelData*>& channels)
{
    QStringList list;
    for (const auto* channel : channels)
    {
        list.append(QString::number(channel->channelID()) + " - " + channel->channelName());
    }
    return list;
}

QStringList Chapter10Reader::getTimeChannelComboBoxList() const
{
    return buildChannelComboBoxList(m_time_channels);
}

QStringList Chapter10Reader::getPCMChannelComboBoxList() const
{
    return buildChannelComboBoxList(m_pcm_channels);
}

QList<QPair<int, QString>> Chapter10Reader::getPCMChannelList() const
{
    QList<QPair<int, QString>> list;
    list.reserve(m_pcm_channels.size());
    for (const auto* channel : m_pcm_channels)
    {
        list.append({channel->channelID(),
                     QString::number(channel->channelID()) + " - " + channel->channelName()});
    }
    return list;
}

// DOY is 1-indexed, so add 1
int Chapter10Reader::getStartDayOfYear() const
{
    return m_times_loaded ? m_file_start_time.tm_yday + 1 : 0;
}

int Chapter10Reader::getStartHour() const
{
    return m_times_loaded ? m_file_start_time.tm_hour : 0;
}

int Chapter10Reader::getStartMinute() const
{
    return m_times_loaded ? m_file_start_time.tm_min : 0;
}

int Chapter10Reader::getStartSecond() const
{
    return m_times_loaded ? m_file_start_time.tm_sec : 0;
}

// DOY is 1-indexed, so add 1
int Chapter10Reader::getStopDayOfYear() const
{
    return m_times_loaded ? m_file_stop_time.tm_yday + 1 : 0;
}

int Chapter10Reader::getStopHour() const
{
    return m_times_loaded ? m_file_stop_time.tm_hour : 0;
}

int Chapter10Reader::getStopMinute() const
{
    return m_times_loaded ? m_file_stop_time.tm_min : 0;
}

int Chapter10Reader::getStopSecond() const
{
    return m_times_loaded ? m_file_stop_time.tm_sec : 0;
}

void Chapter10Reader::processPacketTime(Irig106::SuI106Ch10Header& header, bool& found_start_time)
{
    // If we haven't found a time yet, checks to see if this is one
    if (!found_start_time)
    {
        if (header.ubyDataType == I106CH10_DTYPE_IRIG_TIME)
        {
            found_start_time = true;
            memcpy(m_relative_start_time.data(),
                   &header.aubyRefTime[0],
                   sizeof(m_relative_start_time));
        }
    }

    // Always catch the last time, which will be the stop time
    if (header.ubyDataType == I106CH10_DTYPE_IRIG_TIME)
    {
        memcpy(m_relative_stop_time.data(),
               &header.aubyRefTime[0],
               sizeof(m_relative_stop_time));
    }
}

bool Chapter10Reader::processTmatsPacket(Irig106::SuI106Ch10Header& header)
{
    // Decode TMATS metadata into m_tmats_info for later use by applyTmatsNames().
    // Name/type application is deferred to categorizeChannels() so that all channels
    // are already in m_channel_data before the TMATS lookup runs.
    memset(&m_tmats_info, 0, sizeof(m_tmats_info));
    m_status = enI106_Decode_Tmats(&header, m_buffer.data(), &m_tmats_info);
    return m_status == I106_OK;
}

void Chapter10Reader::finalizeTimeCalc()
{
    // Translate start and stop times
    SuIrig106Time start_real_time;
    enI106_Rel2IrigTime(m_file_handle, m_relative_start_time.data(), &start_real_time);
    gmtime_s(&m_file_start_time, &(start_real_time.ulSecs));

    SuIrig106Time stop_real_time;
    enI106_Rel2IrigTime(m_file_handle, m_relative_stop_time.data(), &stop_real_time);
    gmtime_s(&m_file_stop_time, &(stop_real_time.ulSecs));

    m_times_loaded = true;

    // Use constants and proper types to avoid overflow/implicit widening
    const uint64_t seconds_in_day = static_cast<uint64_t>(PCMConstants::kHoursPerDay) *
                                    PCMConstants::kMinutesPerHour *
                                    PCMConstants::kSecondsPerMinute;

    m_time_difference = start_real_time.ulSecs -
        ((static_cast<uint64_t>(m_file_start_time.tm_yday) * seconds_in_day) +
         (static_cast<uint64_t>(m_file_start_time.tm_hour) * PCMConstants::kMinutesPerHour * PCMConstants::kSecondsPerMinute) +
         (static_cast<uint64_t>(m_file_start_time.tm_min) * PCMConstants::kSecondsPerMinute) +
         static_cast<uint64_t>(m_file_start_time.tm_sec));
}

void Chapter10Reader::applyTmatsNames()
{
    // Apply TMATS-derived names and types now that all channels are in m_channel_data.
    // processTmatsPacket() runs on the first packet when most channels aren't yet
    // discovered, so we re-apply the TMATS metadata here where the map is complete.
    SuRRecord* record = m_tmats_info.psuFirstRRecord;
    while (record != nullptr)
    {
        SuRDataSource* data_source = record->psuFirstDataSource;
        while (data_source != nullptr)
        {
            if (data_source->szTrackNumber != nullptr)
            {
                int track_number = atoi(data_source->szTrackNumber);
                if (m_channel_data.contains(track_number))
                {
                    // Name priority: szDataSourceID (descriptive TMATS R-record identifier)
                    //              → szChanDataLinkName (Ch10 rev 07+ link name, often generic)
                    //              → szPcmDataLinkName (rev -04/-05 link name)
                    QString dsid = (data_source->szDataSourceID != nullptr)
                                   ? QString(data_source->szDataSourceID) : QString();
                    QString cdln = (data_source->szChanDataLinkName != nullptr)
                                   ? QString(data_source->szChanDataLinkName) : QString();
                    QString pdln = (data_source->szPcmDataLinkName != nullptr)
                                   ? QString(data_source->szPcmDataLinkName) : QString();
                    if (!dsid.isEmpty())
                    {
                        m_channel_data[track_number]->setChannelName(dsid);
                    }
                    else if (!cdln.isEmpty())
                    {
                        m_channel_data[track_number]->setChannelName(cdln);
                    }
                    else if (!pdln.isEmpty())
                    {
                        m_channel_data[track_number]->setChannelName(pdln);
                    }
                    if (data_source->szChannelDataType != nullptr)
                    {
                        m_channel_data[track_number]->setChannelType(QString(data_source->szChannelDataType));
                    }
                }
            }
            data_source = data_source->psuNext;
        }
        record = record->psuNext;
    }
}

void Chapter10Reader::categorizeChannels()
{
    applyTmatsNames();

    // Go through m_channel_data and sort channels into time and PCM lists
    for (auto it = m_channel_data.begin(); it != m_channel_data.end(); ++it)
    {
        ChannelData* channel = it.value();
        if (channel->channelType() == PCMConstants::kChannelTypeTime)
        {
            m_time_channels.append(channel);
            if (m_current_time_channel == -1)
            {
                m_current_time_channel = channel->channelID();
            }
        }
        if (channel->channelType() == PCMConstants::kChannelTypePcm)
        {
            m_pcm_channels.append(channel);
            if (m_current_pcm_channel == -1)
            {
                m_current_pcm_channel = channel->channelID();
            }
        }
    }
}

void Chapter10Reader::timeChannelChanged(int combobox_index)
{
    // subtract 1 from the index to account for "Select a Time Stream"
    int list_index = combobox_index - 1;
    if (list_index < 0 || list_index >= m_time_channels.size())
    {
        m_current_time_channel = -1;
    }
    else
    {
        m_current_time_channel = m_time_channels[list_index]->channelID();
    }
}

void Chapter10Reader::pcmChannelChanged(int combobox_index)
{
    // subtract 1 from the index to account for "Select a PCM Stream"
    int list_index = combobox_index - 1;
    if (list_index < 0 || list_index >= m_pcm_channels.size())
    {
        m_current_pcm_channel = -1;
    }
    else
    {
        m_current_pcm_channel = m_pcm_channels[list_index]->channelID();
    }
}

uint64_t Chapter10Reader::dhmsToUInt64(int day, int hour, int minute, int second) const
{
    return m_time_difference +
            (static_cast<uint64_t>(day - 1) * PCMConstants::kHoursPerDay * PCMConstants::kMinutesPerHour * PCMConstants::kSecondsPerMinute) +
            (static_cast<uint64_t>(hour) * PCMConstants::kMinutesPerHour * PCMConstants::kSecondsPerMinute) +
            (static_cast<uint64_t>(minute) * PCMConstants::kSecondsPerMinute) +
            static_cast<uint64_t>(second);
}

double Chapter10Reader::getTmatsDataRateBps(int channel_id) const
{
    // Walk the R record -> data source -> P record chain looking for
    // the P-x\D2 (Bits Per Second) field that corresponds to this channel.
    SuRRecord* record = m_tmats_info.psuFirstRRecord;
    while (record != nullptr)
    {
        SuRDataSource* data_source = record->psuFirstDataSource;
        while (data_source != nullptr)
        {
            if (data_source->szTrackNumber != nullptr)
            {
                int track = atoi(data_source->szTrackNumber);
                if (track == channel_id && data_source->psuPRecord != nullptr)
                {
                    const char* sz = data_source->psuPRecord->szBitsPerSec;
                    if (sz != nullptr && sz[0] != '\0')
                    {
                        bool ok = false;
                        double bps = QString(sz).toDouble(&ok);
                        if (ok && bps > 0.0)
                            return bps;
                    }
                }
            }
            data_source = data_source->psuNext;
        }
        record = record->psuNext;
    }
    return 0.0;
}

int Chapter10Reader::findChannelIndex(const QList<ChannelData*>& channels, int channel_id)
{
    for (qsizetype i = 0; i < channels.size(); i++)
    {
        if (channels[i]->channelID() == channel_id)
            return static_cast<int>(i);
    }
    return -1;
}

int Chapter10Reader::getTimeChannelIndex(int channel_id) const
{
    return findChannelIndex(m_time_channels, channel_id);
}

int Chapter10Reader::getPCMChannelIndex(int channel_id) const
{
    return findChannelIndex(m_pcm_channels, channel_id);
}

int Chapter10Reader::getCurrentTimeChannelID() const
{
    return m_current_time_channel;
}

int Chapter10Reader::getCurrentPCMChannelID() const
{
    return m_current_pcm_channel;
}

int Chapter10Reader::getFirstPCMChannelID() const
{
    if (m_pcm_channels.isEmpty())
    {
        return -1;
    }
    return m_pcm_channels[0]->channelID();
}
