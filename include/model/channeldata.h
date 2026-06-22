/**
 * @file channeldata.h
 * @brief Metadata for a single channel within a Chapter 10 recording.
 *
 * Created by Lap Doan
 */

#ifndef CHANNELDATA_H
#define CHANNELDATA_H

#include <QString>

/**
 * @brief Stores metadata for one data channel found in a Chapter 10 file.
 *
 * Each channel has an ID, a type (e.g., "TIMEIN" or "PCMIN"), a human-readable
 * name from the TMATS record, and a packet count.
 */
class ChannelData
{
public:
    /// @param[in] channel_id Numeric channel identifier from the Chapter 10 header.
    explicit ChannelData(int channel_id);
    ~ChannelData() = default;
    ChannelData(const ChannelData&) = default;
    ChannelData& operator=(const ChannelData&) = default;
    ChannelData(ChannelData&&) = default;
    ChannelData& operator=(ChannelData&&) = default;

    int channelID() const;       ///< @return The channel identifier.
    QString channelType() const; ///< @return The TMATS channel type string.
    QString channelName() const; ///< @return The human-readable channel name.
    int channelCount() const;    ///< @return Number of packets seen for this channel.

    /// @param[in] channel_type TMATS channel type (e.g., "PCMIN").
    void setChannelType(const QString& channel_type);
    /// @param[in] channel_name Human-readable name from the TMATS data source.
    void setChannelName(const QString& channel_name);
    /// Increments the packet count by one.
    void incrementChannelCount();

private:
    int m_channel_id;        ///< Numeric channel identifier.
    QString m_channel_type;  ///< TMATS channel type string.
    QString m_channel_name;  ///< Human-readable channel name.
    int m_channel_count;     ///< Number of data packets observed.
};

#endif // CHANNELDATA_H
