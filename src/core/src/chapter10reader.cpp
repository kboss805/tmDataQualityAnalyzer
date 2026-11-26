/**
 * @file chapter10reader.cpp
 * @brief Implementation of Chapter10Reader — IRIG 106 file metadata scanner.
 */

#include "chapter10reader.h"

#include <optional>

#include "ch10setup.h"
#include "constants.h"
#include "irig106lock.h"
#include "textparse.h"
#include "tmatswalk.h"

using namespace Irig106;

namespace {

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

Chapter10Reader::~Chapter10Reader()   // NOLINT(bugprone-exception-escape): only a mutex that cannot be locked throws
{
    // A load closes the file on every path it returns by; one that threw (std::bad_alloc while listing the channels)
    // left it open, and with it one of irig106's hundred handles for the life of the process. Closing takes the setup
    // lock: it gives the slot back to the library's table.
    if (m_file_open)
    {
        const auto setup_lock = lockIrig106Setup();
        closeFile();
    }
    // Touches only this reader's tree (the decoder's statics are read during a decode alone), so it needs no lock.
    enI106_Free_TmatsInfo(&m_tmats_info);
}

void Chapter10Reader::closeFile()
{
    if (!m_file_open)
    {
        return;
    }
    enI106Ch10Close(m_file_handle);
    m_file_open = false;
}

void Chapter10Reader::clearSettings()
{
    m_channel_data.clear();
    m_time_channels.clear();
    m_pcm_channels.clear();
    m_current_time_channel = -1; // no channel selected
    m_current_pcm_channel = -1;
}

bool Chapter10Reader::loadChannels(const std::filesystem::path& filename)
{
    m_last_error.clear();

    // The whole load is setup: open, time sync and the TMATS decode all touch irig106's
    // process-wide state, so it is serialised with every other reader's setup.
    const auto setup_lock = lockIrig106Setup();
    closeFile();   // a load that threw part-way left its handle open: given back before another one is opened

    ch10setup::Failure failure = ch10setup::openRecording(setup_lock, filename, m_file_handle, m_status);
    m_file_open = (failure != ch10setup::Failure::Open);   // anything but a failed open leaves a handle to close
    if (failure == ch10setup::Failure::Open)
    {
        m_last_error = "Error opening file: " + filename.string();
        return false;
    }
    if (failure != ch10setup::Failure::None)
    {
        m_last_error = ch10setup::describe(failure);
        closeFile();
        return false;
    }

    // The new file replaces the old contents - channels, the lists that name them, and the
    // selection. (The lists used to keep pointers into the channels, which dangled and then
    // doubled up when a reader was loaded a second time.)
    clearSettings();

    // A valid Chapter 10 file's FIRST packet is TMATS, which enumerates every channel, so channel discovery
    // needs only that one packet: no scan of the file, no worker thread. A file that does not begin with a
    // TMATS packet that reads and decodes is not a processable stream; Ch10PacketReader::prepare() asks the
    // same of it, through the same steps.
    failure = ch10setup::readTmats(setup_lock, m_file_handle, m_header, m_buffer, m_tmats_info, m_status);
    if (failure != ch10setup::Failure::None)
    {
        m_last_error = ch10setup::describe(failure);
        closeFile();
        return false;
    }

    // The TMATS packet's own channel is known to the reader; the channels TMATS describes are added from its text.
    m_channel_data.try_emplace(m_header.uChID, m_header.uChID);
    m_channel_data.at(m_header.uChID).incrementChannelCount();

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
        // SeriesColumnSchema::snrSeriesName) since multiple SNR streams in one file
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

void Chapter10Reader::applyTmatsNames()
{
    // Apply TMATS-derived names and types now that all channels are in m_channel_data.
    // The TMATS packet is read before any channel is listed, so the names are applied here
    // where the map is complete.
    tmatswalk::forEachDataSource(m_tmats_info, [this](Irig106::SuRDataSource& data_source) {
        // TMATS\R-x\CHE-n ("Channel Enabled") marks data sources the recorder
        // is configured to capture but that may not actually be enabled for
        // this recording. Skip explicitly-disabled sources so they don't show
        // up as selectable channels; treat a missing CHE-n (common on older
        // TMATS revisions) as enabled, since omission isn't a disable signal.
        const std::string che = textparse::asciiUpper(textparse::trimmed(fieldText(data_source.szEnabled)));
        const bool channel_enabled = che.empty() || che == "T" || che == "TRUE" || che == "Y" || che == "1";

        const std::optional<int> track_number = tmatswalk::trackNumber(data_source);
        if (track_number.has_value() && channel_enabled)
        {
            m_channel_data.try_emplace(*track_number, *track_number);
            ChannelData& channel = m_channel_data.at(*track_number);

            // Name priority: szDataSourceID (descriptive TMATS R-record identifier)
            //              -> szChanDataLinkName (Ch10 rev 07+ link name, often generic)
            //              -> szPcmDataLinkName (rev -04/-05 link name)
            const std::string dsid = fieldText(data_source.szDataSourceID);
            const std::string cdln = fieldText(data_source.szChanDataLinkName);
            const std::string pdln = fieldText(data_source.szPcmDataLinkName);
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
            if (data_source.szChannelDataType != nullptr)
            {
                const std::string cdt = textparse::trimmed(data_source.szChannelDataType);
                if (cdt == "11" || cdt == PCMConstants::kChannelTypeTime) {
                    channel.setChannelType(PCMConstants::kChannelTypeTime);
                } else if (cdt == "01" || cdt == "09" || cdt == PCMConstants::kChannelTypePcm) {
                    channel.setChannelType(PCMConstants::kChannelTypePcm);
                } else {
                    channel.setChannelType(cdt);
                }
            }
        }
        return tmatswalk::Walk::Continue;
    });
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
    double rate = 0.0;
    tmatswalk::forEachDataSource(m_tmats_info, [&](const Irig106::SuRDataSource& data_source) {
        if (tmatswalk::trackNumber(data_source) == channel_id && data_source.psuPRecord != nullptr)
        {
            const char* sz = data_source.psuPRecord->szBitsPerSec;
            double bps = 0.0;
            if (sz != nullptr && sz[0] != '\0' && textparse::parseDouble(sz, bps) && bps > 0.0)
            {
                rate = bps;
                return tmatswalk::Walk::Stop;
            }
        }
        return tmatswalk::Walk::Continue;
    });
    return rate;
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
