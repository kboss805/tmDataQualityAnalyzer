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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "irig106ch10.h"
#include "i106_time.h"
#include "i106_decode_tmats.h"
#include "i106_decode_pcmf1.h"

#include "processingevents.h"
#include "processingparams.h"

class PacketQueue;

/**
 * @brief What the reader holds for one track number the TMATS names: for a PCM source the decoder's attributes for it.
 *
 * A track number whose source is not PCM, or has no data type, still has an entry (so a later source claiming the same
 * number is ignored), with no attributes.
 */
struct ChannelInfo
{
    /// Gives back the buffers the decoder allocated for the attributes, then the attributes themselves.
    struct PcmAttributesDeleter
    {
        void operator()(Irig106::SuPcmF1_Attributes* attributes) const;
    };

    /// The decoded attributes of a PCM source; null for a source that is not PCM, or whose attributes could not be allocated.
    std::unique_ptr<Irig106::SuPcmF1_Attributes, PcmAttributesDeleter> pcm;
};

/**
 * @brief Reads a Ch10 file once and dispatches PCM packets to stream queues.
 *
 * Lifecycle: its owner (PipelineRunner for a run, CalibrationExtractor for an extraction)
 * constructs the reader, calls prepare() synchronously (opens the file, parses TMATS,
 * resolves attributes, builds routing), then runs run() on a thread of its own. It is plain C++: progress, log lines and errors go to a
 * ProcessingEvents sink (setEvents()), called on the thread that runs run().
 */
class Ch10PacketReader
{
public:
    Ch10PacketReader();
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
     * @c resolvedAttrs and records its @c packetQueue in the routing table.
     *
     * @param[in]     filename        Path to the .ch10 file.
     * @param[in]     time_channel_id Channel ID of the IRIG time channel.
     * @param[in,out] params_list     Per-stream params (resolvedAttrs filled here).
     * @param[out]    error           Human-readable failure description.
     * @return true on success.
     */
    bool prepare(const std::filesystem::path& filename,
                 int time_channel_id,
                 std::vector<ProcessingParams*>& params_list,
                 std::string& error);

    /// Requests a cooperative abort of the read loop.
    void requestAbort();

    /// Makes the read loop report its progress at the next packet header, whatever the throttle says and even if the
    /// percentage has not changed; once. Safe from any thread. A test's way to make the reader report at a chosen moment
    /// (its sink can then act inside the read) instead of waiting out the 100 ms throttle.
    void requestProgress();

    /// Runs the single-pass read/dispatch loop. Returns when the file is exhausted, a read
    /// fails, or an abort is requested; every queue has then been given its end marker.
    void run();

    /// Where progress, log lines and errors are reported. Set before run(); not safe to
    /// change while it is running. Null (the default) reports nowhere.
    void setEvents(std::shared_ptr<ProcessingEvents> events);

private:
    /// Builds per-channel attribute structures from TMATS metadata, by track number.
    static Irig106::EnI106Status assembleAttributesFromTMATS(
        Irig106::SuTmatsInfo* tmats_info,
        std::map<int, ChannelInfo>& channel_info);

    /// Reads the data of the packet whose header was just read into m_buffer (ch10setup::readPacketData), refusing a
    /// data length below @p min_data_length or past the packet. Returns false, having reported why, on failure.
    bool readPacket(std::uint32_t min_data_length);
    /// Posts an end-of-stream sentinel to every distinct queue.
    void postSentinels();
    /// Closes the recording if it is open, under the irig106 setup lock (the close writes the handle table).
    void closeFile();

    Irig106::EnI106Status m_status;
    int                   m_file_handle = -1;
    bool                  m_file_open   = false;
    Irig106::SuI106Ch10Header m_header;
    std::vector<uint8_t>  m_buffer;
    Irig106::SuTmatsInfo  m_tmats_info;
    std::map<int, ChannelInfo> m_channel_info; ///< By track number. Emptied before the tree is freed, which its attributes were set up from.
    Irig106::SuIrig106Time m_irig_time;
    int64_t               m_total_file_size = 0;
    int                   m_time_channel_id = -1;

    /// channel ID -> queues that want that channel's PCM packets.
    std::unordered_map<int, std::vector<PacketQueue*>> m_routing;
    /// All distinct queues (for sentinel fan-out).
    std::vector<PacketQueue*> m_all_queues;

    std::atomic<bool> m_abort_requested;
    std::atomic<bool> m_progress_requested{false};   ///< See requestProgress().
    EventReporter m_report; ///< Where progress, log lines and errors go; see setEvents().
};

#endif // CH10PACKETREADER_H
