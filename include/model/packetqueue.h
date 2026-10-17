/**
 * @file packetqueue.h
 * @brief Bounded, thread-safe hand-off queue between the single Ch10 reader
 *        thread and a per-stream FrameProcessor worker thread.
 *
 * One queue exists per processed stream. The reader enqueues decoded PCM packet
 * payloads (already time-stamped, but not yet byte-swapped or derandomized); the
 * worker dequeues and processes them. The queue is bounded so the reader blocks
 * when a worker falls behind, preventing the whole file from being buffered in RAM.
 *
 * **Synchronisation is `std`, not Qt** (`std::mutex` / `std::condition_variable` /
 * `std::deque` in place of `QMutex` / `QWaitCondition` / `QQueue`). First step of the
 * de-Qt work in `docs/future_plans/dotnet-port-assessment.md`: this file is pure
 * infrastructure with no Qt semantics to preserve, and the interop spike already drives
 * the reader with `std::thread`. `PacketItem::payload` stays a `QByteArray` for now -
 * converting it reaches into both the producer and the consumer, so it is its own step.
 */

#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

#include <QByteArray>

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
        : m_max_depth(max_depth > 0 ? static_cast<std::size_t>(max_depth)
                                    : static_cast<std::size_t>(kDefaultMaxDepth))
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
        std::unique_lock<std::mutex> lock(m_mutex);
        // Predicate form rather than a bare wait in a while loop: it is the same
        // condition the Qt version looped on, and it handles spurious wakeups itself.
        m_not_full.wait(lock, [this] {
            return m_closed || m_queue.size() < m_max_depth;
        });
        if (m_closed)
        {
            return false;
        }
        m_queue.push_back(std::move(item));
        m_not_empty.notify_one();
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
        std::unique_lock<std::mutex> lock(m_mutex);
        m_not_empty.wait(lock, [this] {
            return !m_queue.empty() || m_closed;
        });
        if (m_queue.empty())
        {
            PacketItem eos;
            eos.endOfStream = true;
            return eos;
        }
        PacketItem item = std::move(m_queue.front());
        m_queue.pop_front();
        m_not_full.notify_one();
        return item;
    }

    /// Closes the queue and wakes all blocked producers/consumers.
    void close()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
        m_not_full.notify_all();
        m_not_empty.notify_all();
    }

private:
    static constexpr int kDefaultMaxDepth = 64; ///< Max packets buffered per stream.

    std::mutex              m_mutex;
    std::condition_variable m_not_full;
    std::condition_variable m_not_empty;
    std::deque<PacketItem>  m_queue;
    /// Stored unsigned so the fullness check needs no signed/unsigned cast against
    /// std::deque::size(); the constructor still takes an int for the existing API.
    std::size_t             m_max_depth;
    bool                    m_closed = false;
};

#endif // PACKETQUEUE_H
