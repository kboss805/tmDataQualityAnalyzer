/**
 * @file channeldata.cpp
 * @brief Implementation of ChannelData — per-channel metadata storage.
 */

#include "channeldata.h"

ChannelData::ChannelData(int channel_id) :
    m_channel_id(channel_id),
    m_channel_count(0)
{
}

int ChannelData::channelID() const
{
    return m_channel_id;
}

QString ChannelData::channelType() const
{
    return m_channel_type;
}

QString ChannelData::channelName() const
{
    return m_channel_name;
}

int ChannelData::channelCount() const
{
    return m_channel_count;
}

void ChannelData::setChannelType(const QString& channel_type)
{
    m_channel_type = channel_type;
}

void ChannelData::setChannelName(const QString& channel_name)
{
    m_channel_name = channel_name;
}

void ChannelData::incrementChannelCount()
{
    m_channel_count++;
}
