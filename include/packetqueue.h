/**
 * @file packetqueue.h
 * @brief Bounded, thread-safe hand-off queue between the single Ch10 reader
 *        thread and a per-stream FrameProcessor worker thread.
 *
 * One queue exists per processed stream. The reader enqueues decoded PCM packet
 * payloads (already time-stamped, but not yet byte-swapped or derandomized); the
 * worker dequeues and processes them. The queue is bounded so the reader blocks
 * when a worker falls behind, preventing the whole file from being buffered in RAM.
 */

#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H

#include <QByteArray>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>

/**
 * @brief One unit of work handed from the reader to a worker.
 *
 * @c payload holds the raw PCM bits for one Ch10 packet with the 4-byte
 * SuPcmF1_ChanSpec header already stripped. Byte-swap and derandomization are
 * performed by the worker (derandomization is stateful per stream).
 */
struct PacketItem
{
    QByteArray payload;                 ///< Raw PCM payload bits (ChanSpec header stripped).
    double     baseAbsSeconds = 0.0;    ///< Absolute IRIG seconds of this packet's base time.
    uint64_t   packetBits     = 0;      ///< Number of valid PCM bits in @c payload.
    bool       endOfStream    = false;  ///< Sentinel: no more packets for this stream.
};

/**
 * @brief Bounded MPSC-style queue (single reader producer, single worker consumer).
 *
 * Producer calls enqueue() (blocks while full); consumer calls dequeue() (blocks
 * while empty). close() unblocks both sides for cooperative cancellation: after
 * close(), enqueue() discards and returns false, and dequeue() returns an
 * end-of-stream sentinel once drained.
 */
class PacketQueue
{
public:
    explicit PacketQueue(int max_depth = kDefaultMaxDepth)
        : m_max_depth(max_depth > 0 ? max_depth : kDefaultMaxDepth)
    {
    }

    PacketQueue(const PacketQueue&)            = delete;
    PacketQueue& operator=(const PacketQueue&) = delete;
    PacketQueue(PacketQueue&&)                 = delete;
    PacketQueue& operator=(PacketQueue&&)      = delete;

    /**
     * @brief Enqueues @p item, blocking while the queue is full.
     * @return false if the queue was closed (item discarded), true otherwise.
     */
    bool enqueue(PacketItem item)
    {
        QMutexLocker locker(&m_mutex);
        while (!m_closed && m_queue.size() >= m_max_depth)
        {
            m_not_full.wait(&m_mutex);
        }
        if (m_closed)
        {
            return false;
        }
        m_queue.enqueue(std::move(item));
        m_not_empty.wakeOne();
        return true;
    }

    /**
     * @brief Dequeues the next item, blocking while the queue is empty.
     *
     * If the queue is closed and drained, returns a synthetic end-of-stream item
     * so the consumer can terminate without special-casing the closed state.
     */
    PacketItem dequeue()
    {
        QMutexLocker locker(&m_mutex);
        while (m_queue.isEmpty() && !m_closed)
        {
            m_not_empty.wait(&m_mutex);
        }
        if (m_queue.isEmpty())
        {
            PacketItem eos;
            eos.endOfStream = true;
            return eos;
        }
        PacketItem item = m_queue.dequeue();
        m_not_full.wakeOne();
        return item;
    }

    /// Closes the queue and wakes all blocked producers/consumers.
    void close()
    {
        QMutexLocker locker(&m_mutex);
        m_closed = true;
        m_not_full.wakeAll();
        m_not_empty.wakeAll();
    }

private:
    static constexpr int kDefaultMaxDepth = 64; ///< Max packets buffered per stream.

    QMutex            m_mutex;
    QWaitCondition    m_not_full;
    QWaitCondition    m_not_empty;
    QQueue<PacketItem> m_queue;
    int               m_max_depth;
    bool              m_closed = false;
};

#endif // PACKETQUEUE_H
