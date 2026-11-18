/**
 * @file ch10packetreader.cpp
 * @brief Implementation of Ch10PacketReader — single-pass read and per-stream fan-out.
 */

#include "ch10packetreader.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

#include "i106_decode_pcmf1.h"
#include "i106_decode_time.h"

#include "ch10setup.h"
#include "constants.h"
#include "intervalgate.h"
#include "irig106lock.h"
#include "packetqueue.h"
#include "tmatswalk.h"

using namespace Irig106;

namespace {
    constexpr double kTimeGapThreshold = 2.0;
    constexpr int    kPercent100 = 100;
    constexpr int    kPercent10  = 10;
}

////////////////////////////////////////////////////////////////////////////////
//                       CONSTRUCTOR / DESTRUCTOR                             //
////////////////////////////////////////////////////////////////////////////////

Ch10PacketReader::Ch10PacketReader()
    : m_status(I106_OK),
      m_header(),
      m_tmats_info(),
      m_irig_time(),
      m_abort_requested(false)
{
    m_channel_info.resize(PCMConstants::kMaxChannelCount, nullptr);
    m_buffer.resize(PCMConstants::kDefaultBufferSize);
}

Ch10PacketReader::~Ch10PacketReader()   // NOLINT(bugprone-exception-escape): only a mutex that cannot be locked throws
{
    closeFile();
    freeChanInfoTable(m_channel_info);
    // After the table, whose entries point into the tree. irig106 allocates the tree and only gives it back when
    // asked; it touches only this reader's tree, so it needs no lock.
    enI106_Free_TmatsInfo(&m_tmats_info);
}

void Ch10PacketReader::closeFile()
{
    if (!m_file_open)
    {
        return;
    }
    // Closing writes the library's handle table, the state the setup lock exists for: the close marks the slot free
    // before its last writes to it, so an open on another thread could claim the slot and have it marked closed.
    const auto setup_lock = lockIrig106Setup();
    enI106Ch10Close(m_file_handle);
    m_file_open = false;
}

void Ch10PacketReader::requestAbort()
{
    m_abort_requested.store(true, std::memory_order_relaxed);
}

void Ch10PacketReader::setEvents(std::shared_ptr<ProcessingEvents> events)
{
    m_report.setSink(std::move(events));
}

////////////////////////////////////////////////////////////////////////////////
//                          IRIG106 HELPERS                                   //
////////////////////////////////////////////////////////////////////////////////

void Ch10PacketReader::freeChanInfoTable(std::vector<SuChanInfo*>& channel_info)
{
    for (auto* info : channel_info)
    {
        if (info != nullptr)
        {
            if (info->psuAttributes != nullptr)
            {
                if (strcasecmp(info->psuRDataSrc->szChannelDataType, PCMConstants::kChannelTypePcm) == 0)
                {
                    FreeOutputBuffers_PcmF1(static_cast<SuPcmF1_Attributes*>(info->psuAttributes));
                }
                free(info->psuAttributes); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
                info->psuAttributes = nullptr;
            }
            delete info;
        }
    }
    channel_info.clear();
    channel_info.resize(PCMConstants::kMaxChannelCount, nullptr);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
EnI106Status Ch10PacketReader::assembleAttributesFromTMATS(SuTmatsInfo* tmats_info,
                                                           std::vector<SuChanInfo*>& channel_info)
{
    if ((tmats_info->psuFirstGRecord == nullptr) || (tmats_info->psuFirstRRecord == nullptr))
    {
        return I106_INVALID_DATA;
    }

    if (channel_info.size() < PCMConstants::kMaxChannelCount)
    {
        channel_info.resize(PCMConstants::kMaxChannelCount, nullptr);
    }

    EnI106Status status = I106_OK;
    tmatswalk::forEachDataSource(*tmats_info, [&](SuRDataSource& data_source) {
        // A data source with no usable track number (none, or a negative one, which would index out of bounds) is
        // skipped rather than indexed with.
        const std::optional<int> track = tmatswalk::trackNumber(data_source);
        if (!track.has_value())
        {
            return tmatswalk::Walk::Continue;
        }
        const int iTrackNumber = *track;
        if (iTrackNumber >= PCMConstants::kMaxChannelCount)
        {
            status = I106_BUFFER_TOO_SMALL;
            return tmatswalk::Walk::Stop;
        }

        if (channel_info[iTrackNumber] == nullptr)
        {
            channel_info[iTrackNumber] = new SuChanInfo();
            memset(channel_info[iTrackNumber], 0, sizeof(SuChanInfo));

            channel_info[iTrackNumber]->psuRDataSrc = &data_source;

            // The decoder leaves a field the TMATS does not carry NULL, and Chapter10Reader lists such a channel
            // (a missing CHE-n counts as enabled), so the data type is checked before it is compared. A source with
            // none is not taken for PCM: it gets no attributes, and a stream asked of it is refused in prepare().
            if (data_source.szChannelDataType != nullptr && strcasecmp(data_source.szChannelDataType, PCMConstants::kChannelTypePcm) == 0)
            {
                // NOLINTNEXTLINE(cppcoreguidelines-owning-memory, cppcoreguidelines-no-malloc)
                channel_info[iTrackNumber]->psuAttributes = calloc(1, sizeof(SuPcmF1_Attributes));
                if (channel_info[iTrackNumber]->psuAttributes == nullptr)
                {
                    freeChanInfoTable(channel_info);
                    status = I106_BUFFER_TOO_SMALL;
                    return tmatswalk::Walk::Stop;
                }
                (void)Set_Attributes_PcmF1(&data_source,
                    static_cast<SuPcmF1_Attributes*>(channel_info[iTrackNumber]->psuAttributes));
            }
        }
        return tmatswalk::Walk::Continue;
    });

    return status;
}

/// Reads the packet whose header was just read into m_buffer, saying why if it cannot (the run's own error text).
bool Ch10PacketReader::readPacket()
{
    switch (ch10setup::readPacketData(m_file_handle, m_header, m_buffer, m_status))
    {
    case ch10setup::PacketRead::Ok:
        return true;
    case ch10setup::PacketRead::Memory:
        m_report.error("Memory allocation failed.");
        return false;
    case ch10setup::PacketRead::ReadError:
        m_report.error("File read error; aborting parsing.");
        return false;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////
//                              PREPARE                                        //
////////////////////////////////////////////////////////////////////////////////

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool Ch10PacketReader::prepare(const std::filesystem::path& filename,
                               int time_channel_id,
                               std::vector<ProcessingParams*>& params_list,
                               std::string& error)
{
    closeFile();   // a reader prepared again gives its earlier handle back first (before the lock: the close takes it)
    // The decoder frees whatever tree an earlier prepare() left (the member starts zeroed; zeroing it would orphan that
    // tree), so the table that points into that tree goes first.
    freeChanInfoTable(m_channel_info);
    m_time_channel_id = time_channel_id;
    // 64-bit, so a recording over 2 GB is sized correctly; 0 (no progress) if it cannot be sized.
    std::error_code size_error;
    const auto file_size = std::filesystem::file_size(filename, size_error);
    m_total_file_size = size_error ? 0 : static_cast<int64_t>(file_size);

    // Setup is the part of irig106 that is not thread-safe (the handle table, the TMATS decoder's
    // statics), so it is serialised across every reader in the process. Held to the end of the
    // function, whichever way it returns; reading packets afterwards needs no lock.
    const auto setup_lock = lockIrig106Setup();

    // Open, time sync, and the first packet - TMATS - read and decoded: the steps Chapter10Reader takes too.
    ch10setup::Failure failure = ch10setup::openRecording(setup_lock, filename, m_file_handle, m_status);
    m_file_open = (failure != ch10setup::Failure::Open);   // anything but a failed open leaves a handle to close
    if (failure == ch10setup::Failure::None)
    {
        failure = ch10setup::readTmats(setup_lock, m_file_handle, m_header, m_buffer, m_tmats_info, m_status);
    }
    if (failure != ch10setup::Failure::None)
    {
        error = ch10setup::describe(failure);
        return false;
    }
    m_status = assembleAttributesFromTMATS(&m_tmats_info, m_channel_info);
    if (m_status != I106_OK)
    {
        error = "Failed to assemble attributes from TMATS.";
        return false;
    }

    // Resolve per-stream PCM attributes and build the routing table.
    for (ProcessingParams* p : params_list)
    {
        const int ch = p->pcmChannelId;
        if (ch < 0 || ch >= PCMConstants::kMaxChannelCount || m_channel_info[ch] == nullptr)
        {
            error = "Channel info not set up for PCM channel " + std::to_string(ch) + ".";
            return false;
        }
        auto* pcm_attrs = static_cast<SuPcmF1_Attributes*>(m_channel_info[ch]->psuAttributes);
        if (pcm_attrs == nullptr)
        {
            error = "Unable to load PCM attributes for channel " + std::to_string(ch) + ".";
            return false;
        }

        const int64_t sync_mask_i64 = (p->frameSyncMask != 0)
            ? static_cast<int64_t>(p->frameSyncMask)
            : static_cast<int64_t>(fullSyncMask(p->syncPatternLength));

        Set_Attributes_Ext_PcmF1(pcm_attrs->psuRDataSrc, pcm_attrs,
                                 -1, -1,
                                 PCMConstants::kCommonWordLen,
                                 -1, -1, -1,
                                 PCMConstants::kNumMinorFrames,
                                 p->wordsInMinorFrame,
                                 p->bitsInMinorFrame,
                                 -1,
                                 p->syncPatternLength,
                                 static_cast<int64_t>(p->frameSync),
                                 -1,
                                 sync_mask_i64,
                                 -1);

        ResolvedPcmAttrs ra;
        ra.syncPat       = p->frameSync;
        ra.syncMask      = static_cast<uint64_t>(sync_mask_i64);
        ra.syncPatLen    = p->syncPatternLength;
        ra.bitsInFrame   = p->bitsInMinorFrame;
        ra.wordsInFrame  = p->wordsInMinorFrame;
        ra.wordLen       = pcm_attrs->ulCommonWordLen;
        // Every bit of a word, built from the word length. NOT the library's ullCommonWordMask: Set_Attributes_Ext_PcmF1
        // ends with `ullCommonWordMask &= ullMinorFrameSyncMask`, tying a receiver word's bits to which SYNC bits the
        // operator marked significant - a sync mask of 0x00FF00FF left every AGC word 8 bits wide, and 0xFFFF0000 would
        // have zeroed them all. third_party/irig106 is not ours to change, so the mask is not read back from it.
        ra.wordMask      = (ra.wordLen >= 64) ? ~uint64_t{0} : ((uint64_t{1} << ra.wordLen) - 1);
        // ulMinSyncs == 0 means the TMATS field was not set; treat as 1 (at minimum
        // one confirmed boundary-aligned sync is required before extracting frames).
        ra.minSyncs      = std::max(1u, pcm_attrs->ulMinSyncs);
        // Byte order comes from the operator, not from the library attributes.
        // bDontSwapRawData is memset to 0 by Set_Attributes_PcmF1 and only ever
        // assigned by Set_Attributes_Ext_PcmF1 when its lNoByteSwap argument is not
        // -1 - and we pass -1 above, because TMATS has no field for it. So the old
        // expression was a constant `true` dressed up as a lookup, and there was no
        // way to process a recording whose payload is not byte-swapped.
        ra.needsSwap     = p->swapBytes;
        ra.resolved      = true;

        double delta_100ns = pcm_attrs->dDelta100NanoSeconds;
        if (p->dataRateBps > 0.0)
        {
            delta_100ns = PCMConstants::k100NsPerSecond / p->dataRateBps;
        }
        ra.delta100ns = delta_100ns;

        // Give the resolved struct to the worker params
        p->resolvedAttrs = ra;

        if (p->packetQueue != nullptr)
        {
            m_routing[ch].push_back(p->packetQueue);
            if (std::find(m_all_queues.begin(), m_all_queues.end(), p->packetQueue) == m_all_queues.end())
            {
                m_all_queues.push_back(p->packetQueue);
            }
        }
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////
//                                RUN                                          //
////////////////////////////////////////////////////////////////////////////////

void Ch10PacketReader::postSentinels()
{
    for (PacketQueue* q : m_all_queues)
    {
        PacketItem eos;
        eos.endOfStream = true;
        q->enqueue(std::move(eos));
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void Ch10PacketReader::run()
{
    m_report.log("Reading Chapter 10 file...");

    int  last_reported_percent = -1;
    IntervalGate progress_gate{std::chrono::milliseconds(PCMConstants::kProgressReportIntervalMs)};
    double prev_time_seconds = -1.0;
    int  time_gaps_detected = 0;

    while (true)
    {
        m_status = enI106Ch10ReadNextHeader(m_file_handle, &m_header);
        if (m_status == I106_EOF)
        {
            break;
        }
        if (m_status != I106_OK)
        {
            m_report.error("File read error during data collection.");
            break;
        }

        if (m_abort_requested.load(std::memory_order_relaxed))
        {
            m_report.log("Reading cancelled by user.");
            break;
        }

        // The size test comes first on purpose: it short-circuits, so a file of unknown
        // size never reads the clock at all, as before. This runs once per packet header,
        // so the clock read is the per-iteration cost - one monotonic read either way.
        if (m_total_file_size > 0 && progress_gate.due())
        {
            progress_gate.reset();
            int64_t current_pos = 0;
            enI106Ch10GetPos(m_file_handle, &current_pos);
            int percent = static_cast<int>(current_pos * kPercent100 / m_total_file_size);
            if (percent != last_reported_percent)
            {
                if (percent / kPercent10 != last_reported_percent / kPercent10 && percent > 0)
                {
                    m_report.log(std::to_string(percent) + "% read...");
                }
                last_reported_percent = percent;
                m_report.progress(percent);
            }
        }

        // IRIG time packets: keep the file handle's time sync current.
        if (m_header.ubyDataType == I106CH10_DTYPE_IRIG_TIME && m_header.uChID == m_time_channel_id)
        {
            if (!readPacket())
            {
                break;
            }
            enI106_Decode_TimeF1(&m_header, m_buffer.data(), &m_irig_time);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            enI106_SetRelTime(m_file_handle, &m_irig_time, m_header.aubyRefTime);

            double pkt_time = static_cast<double>(m_irig_time.ulSecs) +
                              (PCMConstants::k100NsToSeconds * static_cast<double>(m_irig_time.ulFrac));
            if (prev_time_seconds >= 0)
            {
                double gap = pkt_time - prev_time_seconds;
                if (gap > kTimeGapThreshold)
                {
                    time_gaps_detected++;
                }
            }
            prev_time_seconds = pkt_time;
            continue;
        }

        // PCM packets: dispatch to every stream that wants this channel.
        if (m_header.ubyDataType == I106CH10_DTYPE_PCM_FMT_1)
        {
            auto route_it = m_routing.find(m_header.uChID);
            if (route_it == m_routing.end())
            {
                continue; // No stream is interested in this channel.
            }

            if (!readPacket())
            {
                break;
            }

            uint32_t data_offset = sizeof(SuPcmF1_ChanSpec);
            if (m_header.ulDataLen <= data_offset)
            {
                continue;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            const uint8_t* raw_data = m_buffer.data() + data_offset;
            uint32_t raw_len = m_header.ulDataLen - data_offset;

            // Convert this packet's base time to absolute seconds using the
            // current (latest) time sync state.
            int64_t pkt_base_rel = 0;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            vTimeArray2LLInt(m_header.aubyRefTime, &pkt_base_rel);
            enI106_RelInt2IrigTime(m_file_handle, pkt_base_rel, &m_irig_time);
            double base_abs_seconds = static_cast<double>(m_irig_time.ulSecs) +
                                      (PCMConstants::k100NsToSeconds * static_cast<double>(m_irig_time.ulFrac));

            PacketItem item;
            item.payload       = std::make_shared<const std::vector<uint8_t>>(raw_data, raw_data + raw_len);
            item.baseAbsSeconds = base_abs_seconds;
            item.packetBits    = static_cast<uint64_t>(raw_len) * 8;

            const std::vector<PacketQueue*>& queues = route_it->second;
            for (PacketQueue* q : queues)
            {
                q->enqueue(item); // copies the shared_ptr, not the bytes; each worker copies before it mutates
            }
        }
    }

    postSentinels();

    if (time_gaps_detected > 0)
    {
        m_report.log(std::format("WARNING: {} time gap(s) detected in recording.", time_gaps_detected));
    }

    closeFile();

    m_report.progress(kPercent100);
}
