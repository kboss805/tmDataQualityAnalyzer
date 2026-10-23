/**
 * @file chapter10reader.cpp
 * @brief Implementation of Chapter10Reader — IRIG 106 file metadata scanner.
 */

#include "chapter10reader.h"

#include <cstdlib>
#include <cstring>
#include <new>

#include "constants.h"
#include "irig106lock.h"
#include "textparse.h"

using namespace Irig106;

namespace {

/// ASCII upper-casing, enough for the TMATS "channel enabled" flag (T / TRUE / Y / 1).
std::string upperAscii(std::string text)
{
    for (char& c : text)
    {
        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

/// A TMATS field as text; a missing field is the empty string.
std::string fieldText(const char* field)
{
    return (field != nullptr) ? std::string(field) : std::string();
}

} // namespace

Chapter10Reader::Chapter10Reader() :
    m_status(I106_OK),
    m_file_handle(0),
    m_header(),
    m_tmats_info(),
    m_current_time_channel(-1),
    m_current_pcm_channel(-1)
{
    m_buffer.resize(PCMConstants::kDefaultBufferSize);
}

void Chapter10Reader::closeFile()
{
    enI106Ch10Close(m_file_handle);
}

void Chapter10Reader::clearSettings()
{
    m_channel_data.clear();
    m_time_channels.clear();
    m_pcm_channels.clear();
    m_current_time_channel = -1; // no channel selected
    m_current_pcm_channel = -1;
}

void Chapter10Reader::inferChannelTypeFromHeader(int channel_id)
{
    ChannelData& channel = m_channel_data.at(channel_id);
    if (!channel.channelType().empty())
    {
        return;
    }
    if (m_header.ubyDataType == I106CH10_DTYPE_IRIG_TIME)
    {
        channel.setChannelType(PCMConstants::kChannelTypeTime);
        if (channel.channelName().empty())
        {
            channel.setChannelName("Time");
        }
    }
    else if (m_header.ubyDataType == I106CH10_DTYPE_PCM_FMT_1)
    {
        channel.setChannelType(PCMConstants::kChannelTypePcm);
        if (channel.channelName().empty())
        {
            channel.setChannelName("PCM");
        }
    }
}

bool Chapter10Reader::loadChannels(const std::filesystem::path& filename)
{
    m_last_error.clear();
    const std::string path_text = filename.string();

    // The whole load is setup: open, time sync and the TMATS decode all touch irig106's
    // process-wide state, so it is serialised with every other reader's setup.
    const auto setup_lock = lockIrig106Setup();

    // Open the file
    m_status = enI106Ch10Open(&m_file_handle, path_text.c_str(), I106_READ);
    if (m_status != I106_OK)
    {
        m_last_error = "Error opening file: " + path_text;
        return false;
    }

    // Establish time reference so enI106_Rel2IrigTime() can convert
    // relative header timestamps to absolute IRIG time.
    m_status = enI106_SyncTime(m_file_handle, bFALSE, 0);
    if (m_status != I106_OK)
    {
        m_last_error = "Error establishing time sync.";
        closeFile();
        return false;
    }

    // The new file replaces the old contents - channels, the lists that name them, and the
    // selection. (The lists used to keep pointers into the channels, which dangled and then
    // doubled up when a reader was loaded a second time.)
    clearSettings();

    // A valid Chapter 10 file's FIRST packet is TMATS, which enumerates every
    // channel — so channel discovery only needs that one packet (no full-file scan,
    // no worker thread). Scan until the first TMATS packet and stop. A file whose
    // first packet is not TMATS is not a processable Chapter 10 stream
    // (Ch10PacketReader::prepare() enforces the same), so reject it immediately
    // rather than scanning to EOF.
    bool first_packet = true;
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

        if (first_packet && m_header.ubyDataType != I106CH10_DTYPE_TMATS)
        {
            m_last_error = "Failed to find TMATS message (expected as the first packet).";
            closeFile();
            return false;
        }
        first_packet = false;

        // Make sure our buffer is big enough
        if (m_buffer.size() < static_cast<std::size_t>(uGetDataLen(&m_header)))
        {
            try {
                m_buffer.resize(uGetDataLen(&m_header));
            } catch (const std::bad_alloc&) {
                m_last_error = "Memory allocation failed.";
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
        m_channel_data.try_emplace(channel_id, channel_id);
        m_channel_data.at(channel_id).incrementChannelCount();

        // Set channel type and fallback name from packet header when not already set by TMATS
        inferChannelTypeFromHeader(channel_id);

        // TMATS enumerates all channels we care about; stop scanning once it parses.
        if (m_header.ubyDataType == I106CH10_DTYPE_TMATS)
        {
            if (processTmatsPacket(m_header))
            {
                break;
            }
        }
    } // end while

    categorizeChannels();
    closeFile();

    return true;
}

std::vector<std::string> Chapter10Reader::buildChannelComboBoxList(const std::vector<int>& channel_ids) const
{
    std::vector<std::string> list;
    list.reserve(channel_ids.size());
    for (int id : channel_ids)
    {
        list.push_back(std::to_string(id) + " - " + m_channel_data.at(id).channelName());
    }
    return list;
}

std::vector<std::string> Chapter10Reader::getTimeChannelComboBoxList() const
{
    return buildChannelComboBoxList(m_time_channels);
}

std::vector<std::string> Chapter10Reader::getPCMChannelComboBoxList() const
{
    return buildChannelComboBoxList(m_pcm_channels);
}

std::vector<std::pair<int, std::string>> Chapter10Reader::getPCMChannelList() const
{
    std::vector<std::pair<int, std::string>> list;
    list.reserve(m_pcm_channels.size());
    for (int id : m_pcm_channels)
    {
        // Bare channel name (no "<id> - " prefix): this drives the per-stream
        // label shown in the Configure Streams dialog and the Frame Sync Lock
        // plot series, where the TMATS channel number is just noise. The Receiver
        // SNR plot series re-adds the channel number itself (see
        // PlotViewModel::addStreamData) since multiple SNR streams in one file
        // need it to stay distinguishable.
        list.emplace_back(id, m_channel_data.at(id).channelName());
    }
    return list;
}

std::vector<std::pair<int, std::string>> Chapter10Reader::getTimeChannelList() const
{
    std::vector<std::pair<int, std::string>> list;
    list.reserve(m_time_channels.size());
    for (int id : m_time_channels)
    {
        list.emplace_back(id, m_channel_data.at(id).channelName());
    }
    return list;
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
            // TMATS\R-x\CHE-n ("Channel Enabled") marks data sources the recorder
            // is configured to capture but that may not actually be enabled for
            // this recording. Skip explicitly-disabled sources so they don't show
            // up as selectable channels; treat a missing CHE-n (common on older
            // TMATS revisions) as enabled, since omission isn't a disable signal.
            const std::string che = upperAscii(textparse::trimmed(fieldText(data_source->szEnabled)));
            const bool channel_enabled = che.empty() || che == "T" || che == "TRUE" || che == "Y" || che == "1";

            if (data_source->szTrackNumber != nullptr && channel_enabled)
            {
                int track_number = atoi(data_source->szTrackNumber);
                m_channel_data.try_emplace(track_number, track_number);
                ChannelData& channel = m_channel_data.at(track_number);

                // Name priority: szDataSourceID (descriptive TMATS R-record identifier)
                //              → szChanDataLinkName (Ch10 rev 07+ link name, often generic)
                //              → szPcmDataLinkName (rev -04/-05 link name)
                const std::string dsid = fieldText(data_source->szDataSourceID);
                const std::string cdln = fieldText(data_source->szChanDataLinkName);
                const std::string pdln = fieldText(data_source->szPcmDataLinkName);
                if (!dsid.empty())
                {
                    channel.setChannelName(dsid);
                }
                else if (!cdln.empty())
                {
                    channel.setChannelName(cdln);
                }
                else if (!pdln.empty())
                {
                    channel.setChannelName(pdln);
                }
                if (data_source->szChannelDataType != nullptr)
                {
                    const std::string cdt = textparse::trimmed(data_source->szChannelDataType);
                    if (cdt == "11" || cdt == "TIMEIN") {
                        channel.setChannelType(PCMConstants::kChannelTypeTime);
                    } else if (cdt == "01" || cdt == "09" || cdt == "PCMIN") {
                        channel.setChannelType(PCMConstants::kChannelTypePcm);
                    } else {
                        channel.setChannelType(cdt);
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

    // Go through m_channel_data (in id order) and sort channels into time and PCM lists
    for (const auto& entry : m_channel_data)
    {
        const ChannelData& channel = entry.second;
        if (channel.channelType() == PCMConstants::kChannelTypeTime)
        {
            m_time_channels.push_back(channel.channelID());
            if (m_current_time_channel == -1)
            {
                m_current_time_channel = channel.channelID();
            }
        }
        if (channel.channelType() == PCMConstants::kChannelTypePcm)
        {
            m_pcm_channels.push_back(channel.channelID());
            if (m_current_pcm_channel == -1)
            {
                m_current_pcm_channel = channel.channelID();
            }
        }
    }
}

void Chapter10Reader::timeChannelChanged(int combobox_index)
{
    // subtract 1 from the index to account for "Select a Time Stream"
    int list_index = combobox_index - 1;
    if (list_index < 0 || list_index >= static_cast<int>(m_time_channels.size()))
    {
        m_current_time_channel = -1;
    }
    else
    {
        m_current_time_channel = m_time_channels[list_index];
    }
}

void Chapter10Reader::pcmChannelChanged(int combobox_index)
{
    // subtract 1 from the index to account for "Select a PCM Stream"
    int list_index = combobox_index - 1;
    if (list_index < 0 || list_index >= static_cast<int>(m_pcm_channels.size()))
    {
        m_current_pcm_channel = -1;
    }
    else
    {
        m_current_pcm_channel = m_pcm_channels[list_index];
    }
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
                        double bps = 0.0;
                        if (textparse::parseDouble(sz, bps) && bps > 0.0)
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

int Chapter10Reader::getCurrentTimeChannelID() const
{
    return m_current_time_channel;
}

int Chapter10Reader::getFirstPCMChannelID() const
{
    if (m_pcm_channels.empty())
    {
        return -1;
    }
    return m_pcm_channels.front();
}
