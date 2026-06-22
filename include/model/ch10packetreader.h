/**
 * @file ch10packetreader.h
 * @brief Single-pass Chapter 10 reader that fans PCM packets out to per-stream
 *        worker queues.
 *
 * In the multi-stream pipeline exactly one Ch10PacketReader opens the file and
 * reads it once. It parses TMATS, resolves each stream's PCM attributes, decodes
 * IRIG time packets to keep the time sync current, converts each PCM packet's
 * base time to absolute seconds, and dispatches the raw payload to the matching
 * stream's PacketQueue. Worker FrameProcessors consume from those queues, so the
 * file is read once regardless of how many streams are processed.
 */

#ifndef CH10PACKETREADER_H
#define CH10PACKETREADER_H

#include <atomic>

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include "irig106ch10.h"
#include "i106_time.h"
#include "i106_decode_tmats.h"

#include "processingparams.h"

class PacketQueue;

/**
 * @brief Per-channel bookkeeping used by the irig106 C helper layer.
 */
typedef struct SuChanInfo
{
    uint16_t                uChID;          ///< Channel identifier.
    int                     bEnabled;       ///< Non-zero if channel is enabled.
    Irig106::SuRDataSource* psuRDataSrc;    ///< Pointer to the TMATS R-record data source.
    void*                   psuAttributes;  ///< Pointer to decoded attributes (type varies).
} SuChanInfo;

/**
 * @brief Reads a Ch10 file once and dispatches PCM packets to stream queues.
 *
 * Lifecycle: the coordinator constructs the reader, calls prepare() synchronously
 * (opens the file, parses TMATS, resolves attributes, builds routing), then moves
 * the reader to its own QThread and invokes run() via QThread::started.
 */
class Ch10PacketReader : public QObject
{
    Q_OBJECT

public:
    explicit Ch10PacketReader(QObject* parent = nullptr);
    ~Ch10PacketReader();

    Ch10PacketReader(const Ch10PacketReader&)            = delete;
    Ch10PacketReader& operator=(const Ch10PacketReader&) = delete;
    Ch10PacketReader(Ch10PacketReader&&)                 = delete;
    Ch10PacketReader& operator=(Ch10PacketReader&&)      = delete;

    /**
     * @brief Opens the file, parses TMATS, and resolves each stream's attributes.
     *
     * Must be called (synchronously, before run()) on whichever thread will not
     * race the reader thread. Leaves the file open and positioned just past the
     * TMATS packet so run() can continue the single pass. Populates each entry's
     * @c resolved_attrs and records its @c packet_queue in the routing table.
     *
     * @param[in]     filename        Path to the .ch10 file.
     * @param[in]     time_channel_id Channel ID of the IRIG time channel.
     * @param[in,out] params_list     Per-stream params (resolved_attrs filled here).
     * @param[out]    error           Human-readable failure description.
     * @return true on success.
     */
    bool prepare(const QString& filename,
                 int time_channel_id,
                 QVector<ProcessingParams*>& params_list,
                 QString& error);

    /// Requests a cooperative abort of the read loop.
    void requestAbort();

public slots:
    /// Runs the single-pass read/dispatch loop. Connected to QThread::started.
    void run();

signals:
    /// Emitted periodically with file-position completion percentage (0..100).
    void progressUpdated(int percent);
    /// Emitted at key stages / on warnings with a human-readable message.
    void logMessage(const QString& message);
    /// Emitted when a fatal read/setup error occurs.
    void errorOccurred(const QString& message);
    /// Emitted once when the read loop has finished (clean, EOF, or aborted).
    void finished();

private:
    /// Frees the per-channel attribute table.
    static void freeChanInfoTable(QVector<SuChanInfo*>& channel_info);
    /// Builds per-channel attribute structures from TMATS metadata.
    static Irig106::EnI106Status assembleAttributesFromTMATS(
        Irig106::SuTmatsInfo* tmats_info,
        QVector<SuChanInfo*>& channel_info);

    /// Grows m_buffer to at least @p required bytes. Returns false on failure.
    bool ensureBufferCapacity(qsizetype required);
    /// Posts an end-of-stream sentinel to every distinct queue.
    void postSentinels();

    Irig106::EnI106Status m_status;
    int                   m_file_handle = -1;
    bool                  m_file_open   = false;
    Irig106::SuI106Ch10Header m_header;
    QByteArray            m_buffer;
    Irig106::SuTmatsInfo  m_tmats_info;
    QVector<SuChanInfo*>  m_channel_info;
    Irig106::SuIrig106Time m_irig_time;
    int64_t               m_total_file_size = 0;
    int                   m_time_channel_id = -1;

    /// channel ID -> queues that want that channel's PCM packets.
    QHash<int, QVector<PacketQueue*>> m_routing;
    /// All distinct queues (for sentinel fan-out).
    QVector<PacketQueue*> m_all_queues;

    std::atomic<bool> m_abort_requested;
};

#endif // CH10PACKETREADER_H
