/**
 * @file chapter10reader.h
 * @brief Reads IRIG 106 Chapter 10 file metadata and manages channel selection.
 *
 * Code largely taken from irig106utils github.
 * Created by Lap Doan
 */

#ifndef CHAPTER10READER_H
#define CHAPTER10READER_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "irig106ch10.h"
#include "i106_time.h"
#include "i106_decode_tmats.h"

#include "channeldata.h"


/**
 * @brief Reads IRIG 106 Chapter 10 file metadata and manages channel selection.
 *
 * Wraps the irig106utils C library to open .ch10 files, enumerate time and PCM
 * channels from the TMATS record, and provide channel/time accessors. PCM frame
 * extraction is handled by FrameProcessor.
 *
 * A plain class, with no Qt: text is UTF-8 `std::string`, the path is a
 * `std::filesystem::path`, and a failed load leaves its reason in lastError() rather
 * than emitting a signal - loading is synchronous, on the caller's thread, so the caller
 * already knows when to look. The layers that show channel lists convert them at their
 * own boundary (`qtbridge.h`).
 */
class Chapter10Reader
{
    friend class TestChapter10Reader;

public:
    Chapter10Reader();
    ~Chapter10Reader() = default;

    // Rule of 5: Delete copy/move operations to manage resources safely
    Chapter10Reader(const Chapter10Reader&) = delete;
    Chapter10Reader& operator=(const Chapter10Reader&) = delete;
    Chapter10Reader(Chapter10Reader&&) = delete;
    Chapter10Reader& operator=(Chapter10Reader&&) = delete;

    /// Resets channel lists and selection state.
    void clearSettings();

    /**
     * @brief Scans the file for TMATS metadata and catalogs all channels.
     *
     * Replaces whatever a previous load left behind (channels, lists and selection) once
     * the new file has opened; a file that fails to open leaves the old contents alone.
     *
     * @param[in] filename Path to the .ch10 file, opened in the process code page (the
     *            UTF-8 manifest makes that UTF-8, which is what lets a non-ASCII folder open).
     * @return true if channels were loaded successfully. On failure lastError() says why.
     */
    bool loadChannels(const std::filesystem::path& filename);

    /// @return Why the last loadChannels() failed, or empty if it succeeded (or none ran).
    const std::string& lastError() const { return m_last_error; }

    /// @return "<id> - <name>" for every time channel, in channel-id order.
    std::vector<std::string> getTimeChannelComboBoxList() const;
    /// @return "<id> - <name>" for every PCM channel, in channel-id order.
    std::vector<std::string> getPCMChannelComboBoxList() const;

    /// @return (channel ID, bare name) for every PCM channel, in list order.
    std::vector<std::pair<int, std::string>> getPCMChannelList() const;

    /**
     * @brief Returns the TMATS-declared bit rate for a PCM channel.
     * @param[in] channel_id PCM channel ID to look up.
     * @return Bit rate in bits/second, or 0 if not found in TMATS.
     */
    double getTmatsDataRateBps(int channel_id) const;

    int getCurrentTimeChannelID() const; ///< @return Currently selected time channel ID.
    int getFirstPCMChannelID() const;    ///< @return Channel ID of the first PCM channel, or -1 if none.

    /// Updates the selected time channel from a combo box index (0 is the placeholder).
    void timeChannelChanged(int combobox_index);
    /// Updates the selected PCM channel from a combo box index (0 is the placeholder).
    void pcmChannelChanged(int combobox_index);

private:
    /// Closes the currently open Chapter 10 file.
    void closeFile();
    /// Builds combo box display strings for the channels whose ids are in @p channel_ids.
    std::vector<std::string> buildChannelComboBoxList(const std::vector<int>& channel_ids) const;
    bool processTmatsPacket(Irig106::SuI106Ch10Header& header);
    void applyTmatsNames();
    void inferChannelTypeFromHeader(int channel_id);
    void categorizeChannels();

    Irig106::EnI106Status m_status;                             ///< Last irig106 API return status.
    int m_file_handle;                                          ///< irig106 file handle.
    std::string m_last_error;                                   ///< Reason the last load failed; empty on success.

    Irig106::SuI106Ch10Header m_header;                         ///< Reusable packet header buffer.
    std::vector<uint8_t> m_buffer;                              ///< Packet data read buffer.
    Irig106::SuTmatsInfo m_tmats_info;                          ///< TMATS info structure.

    std::map<int, ChannelData> m_channel_data; ///< All channels discovered in the file, by id.
    std::vector<int> m_time_channels;          ///< Ids of channels with type "TIMEIN", in id order.
    std::vector<int> m_pcm_channels;           ///< Ids of channels with type "PCMIN", in id order.
    int m_current_time_channel;                ///< Currently selected time channel ID (-1 = none).
    int m_current_pcm_channel;                 ///< Currently selected PCM channel ID (-1 = none).
};

#endif // CHAPTER10READER_H
