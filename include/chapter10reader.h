/**
 * @file chapter10reader.h
 * @brief Reads IRIG 106 Chapter 10 file metadata and manages channel selection.
 *
 * Code largely taken from irig106utils github.
 * Created by Lap Doan
 */

#ifndef CHAPTER10READER_H
#define CHAPTER10READER_H

#include <array>

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QString>

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
 */
class Chapter10Reader : public QObject
{
    Q_OBJECT

public:
    explicit Chapter10Reader(QObject* parent = nullptr);
    ~Chapter10Reader();

    // Rule of 5: Delete copy/move operations to manage resources safely
    Chapter10Reader(const Chapter10Reader&) = delete;
    Chapter10Reader& operator=(const Chapter10Reader&) = delete;
    Chapter10Reader(Chapter10Reader&&) = delete;
    Chapter10Reader& operator=(Chapter10Reader&&) = delete;

    /**
     * @brief Opens a Chapter 10 file and synchronizes the time reference.
     * @param[in] filename Path to the .ch10 file.
     * @return true on success.
     */
    bool tryLoadingFile(const QString& filename);

    /// Closes the currently open Chapter 10 file and frees the read buffer.
    void closeFile() const;

    /// Resets channel lists and selection state.
    void clearSettings();

    /**
     * @brief Scans the file for TMATS metadata and catalogs all channels.
     * @param[in] filename Path to the .ch10 file.
     * @return true if channels were loaded successfully.
     */
    bool loadChannels(const QString& filename);

    /// Ensures a ChannelData entry exists for @p channel_id.
    void addChannelInfoEntry(int channel_id);

    QStringList getTimeChannelComboBoxList() const; ///< @return Display strings for time channels.
    QStringList getPCMChannelComboBoxList() const;  ///< @return Display strings for PCM channels.

    /// @return (channel ID, display label) for every PCM channel, in list order.
    QList<QPair<int, QString>> getPCMChannelList() const;

    /// @name File time accessors
    /// @{
    int getStartDayOfYear() const;  ///< @return 1-indexed day-of-year of the first data packet.
    int getStartHour() const;       ///< @return Hour component of the start time.
    int getStartMinute() const;     ///< @return Minute component of the start time.
    int getStartSecond() const;     ///< @return Second component of the start time.
    int getStopDayOfYear() const;   ///< @return 1-indexed day-of-year of the last data packet.
    int getStopHour() const;        ///< @return Hour component of the stop time.
    int getStopMinute() const;      ///< @return Minute component of the stop time.
    int getStopSecond() const;      ///< @return Second component of the stop time.
    /// @}

    /**
     * @brief Converts day/hour/minute/second to IRIG-relative seconds.
     * @return Absolute seconds accounting for the file's IRIG time offset.
     */
    uint64_t dhmsToUInt64(int day, int hour, int minute, int second) const;

    /**
     * @brief Returns the TMATS-declared bit rate for a PCM channel.
     * @param[in] channel_id PCM channel ID to look up.
     * @return Bit rate in bits/second, or 0 if not found in TMATS.
     */
    double getTmatsDataRateBps(int channel_id) const;

    /// @return List index matching @p channel_id, or -1 if not found.
    int getTimeChannelIndex(int channel_id) const;
    /// @return List index matching @p channel_id, or -1 if not found.
    int getPCMChannelIndex(int channel_id) const;
    int getCurrentTimeChannelID() const; ///< @return Currently selected time channel ID.
    int getCurrentPCMChannelID() const;  ///< @return Currently selected PCM channel ID.
    int getFirstPCMChannelID() const;    ///< @return Channel ID of the first PCM channel, or -1 if none.

signals:
    /// Emitted when an error occurs during file operations.
    void displayErrorMessage(const QString& message);

public slots:

    /// Updates the selected time channel from a combo box index.
    void timeChannelChanged(int combobox_index);
    /// Updates the selected PCM channel from a combo box index.
    void pcmChannelChanged(int combobox_index);

private:
    /// Builds combo box display strings from a list of channel metadata.
    static QStringList buildChannelComboBoxList(const QList<ChannelData*>& channels);
    /// Returns the list index of @p channel_id, or -1 if not found.
    static int findChannelIndex(const QList<ChannelData*>& channels, int channel_id);
    void processPacketTime(Irig106::SuI106Ch10Header& header, bool& found_start_time);
    bool processTmatsPacket(Irig106::SuI106Ch10Header& header);
    void finalizeTimeCalc();
    void applyTmatsNames();
    void inferChannelTypeFromHeader(int channel_id);
    void categorizeChannels();

    Irig106::EnI106Status m_status;                             ///< Last irig106 API return status.
    QString m_filename;                                         ///< Path to the currently loaded file.
    int m_file_handle;                                          ///< irig106 file handle.

    Irig106::SuI106Ch10Header m_header;                         ///< Reusable packet header buffer.
    QByteArray m_buffer;                                        ///< Packet data read buffer.
    std::array<unsigned char, 6> m_relative_start_time;         ///< Relative time of first data packet.
    std::array<unsigned char, 6> m_relative_stop_time;          ///< Relative time of last data packet.
    tm m_file_start_time;                                       ///< Decoded calendar start time.
    tm m_file_stop_time;                                        ///< Decoded calendar stop time.
    bool m_times_loaded = false;                                ///< True if start/stop times have been decoded.
    uint64_t m_time_difference;                                 ///< Offset between DOY-based and IRIG absolute seconds.
    Irig106::SuIrig106Time m_irig_time;                         ///< Reusable IRIG time struct.
    Irig106::SuTmatsInfo m_tmats_info;                          ///< TMATS info structure.

    QMap<int, ChannelData*> m_channel_data;  ///< All channels discovered in the file.
    QList<ChannelData*> m_time_channels;     ///< Subset of channels with type "TIMEIN".
    QList<ChannelData*> m_pcm_channels;      ///< Subset of channels with type "PCMIN".
    int m_current_time_channel;              ///< Currently selected time channel ID (-1 = none).
    int m_current_pcm_channel;               ///< Currently selected PCM channel ID (-1 = none).
};

#endif // CHAPTER10READER_H
