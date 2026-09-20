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
    friend class TestChapter10Reader;

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

    QStringList getTimeChannelComboBoxList() const; ///< @return Display strings for time channels.
    QStringList getPCMChannelComboBoxList() const;  ///< @return Display strings for PCM channels.

    /// @return (channel ID, display label) for every PCM channel, in list order.
    QList<QPair<int, QString>> getPCMChannelList() const;

    /// @}

    /**
     * @brief Returns the TMATS-declared bit rate for a PCM channel.
     * @param[in] channel_id PCM channel ID to look up.
     * @return Bit rate in bits/second, or 0 if not found in TMATS.
     */
    double getTmatsDataRateBps(int channel_id) const;

    int getCurrentTimeChannelID() const; ///< @return Currently selected time channel ID.
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
    bool processTmatsPacket(Irig106::SuI106Ch10Header& header);
    void applyTmatsNames();
    void inferChannelTypeFromHeader(int channel_id);
    void categorizeChannels();

    Irig106::EnI106Status m_status;                             ///< Last irig106 API return status.
    QString m_filename;                                         ///< Path to the currently loaded file.
    int m_file_handle;                                          ///< irig106 file handle.

    Irig106::SuI106Ch10Header m_header;                         ///< Reusable packet header buffer.
    QByteArray m_buffer;                                        ///< Packet data read buffer.
    Irig106::SuTmatsInfo m_tmats_info;                          ///< TMATS info structure.

    QMap<int, ChannelData*> m_channel_data;  ///< All channels discovered in the file.
    QList<ChannelData*> m_time_channels;     ///< Subset of channels with type "TIMEIN".
    QList<ChannelData*> m_pcm_channels;      ///< Subset of channels with type "PCMIN".
    int m_current_time_channel;              ///< Currently selected time channel ID (-1 = none).
    int m_current_pcm_channel;               ///< Currently selected PCM channel ID (-1 = none).
};

#endif // CHAPTER10READER_H
