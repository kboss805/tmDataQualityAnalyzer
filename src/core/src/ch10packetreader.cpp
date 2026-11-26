/**
 * @file ch10packetreader.cpp
 * @brief Implementation of Ch10PacketReader — single-pass read and per-stream fan-out.
 */

#include "ch10packetreader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <new>
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
    m_buffer.resize(PCMConstants::kDefaultBufferSize);
}

Ch10PacketReader::~Ch10PacketReader()   // NOLINT(bugprone-exception-escape): only a mutex that cannot be locked throws
{
    closeFile();
    m_channel_info.clear();
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

void Ch10PacketReader::requestProgress()
{
    m_progress_requested.store(true, std::memory_order_relaxed);
}

void Ch10PacketReader::setEvents(std::shared_ptr<ProcessingEvents> events)
{
    m_report.setSink(std::move(events));
}

////////////////////////////////////////////////////////////////////////////////
//                          IRIG106 HELPERS                                   //
////////////////////////////////////////////////////////////////////////////////

void ChannelInfo::PcmAttributesDeleter::operator()(SuPcmF1_Attributes* attributes) const
{
    FreeOutputBuffers_PcmF1(attributes);
    delete attributes; // NOLINT(cppcoreguidelines-owning-memory): the one place these are given back
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
EnI106Status Ch10PacketReader::assembleAttributesFromTMATS(SuTmatsInfo* tmats_info,
                                                           std::map<int, ChannelInfo>& channel_info)
{
    if ((tmats_info->psuFirstGRecord == nullptr) || (tmats_info->psuFirstRRecord == nullptr))
    {
        return I106_INVALID_DATA;
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

        if (!channel_info.contains(iTrackNumber))
        {
            ChannelInfo& info = channel_info[iTrackNumber];

            // The decoder leaves a field the TMATS does not carry NULL, and Chapter10Reader lists such a channel
            // (a missing CHE-n counts as enabled), so the data type is checked before it is compared. A source with
            // none is not taken for PCM: it gets no attributes, and a stream asked of it is refused in prepare().
            if (data_source.szChannelDataType != nullptr && strcasecmp(data_source.szChannelDataType, PCMConstants::kChannelTypePcm) == 0)
            {
                // Zeroed, as the decoder expects; a refusal to allocate is a status, not an exception.
                info.pcm.reset(new (std::nothrow) SuPcmF1_Attributes());
                if (info.pcm == nullptr)
                {
                    channel_info.clear();
                    status = I106_BUFFER_TOO_SMALL;
                    return tmatswalk::Walk::Stop;
                }
                // The status is not what decides: it is I106_INVALID_PARAMETER for a source with no P record (which
                // leaves the attributes zeroed) and I106_UNSUPPORTED for an LSB-first word order or a sync type other
                // than FPT (after the bit rate is read; the operator's settings replace the rest). prepare() checks
                // what a worker would read instead: the bit period, and the word length it supplies itself.
                (void)Set_Attributes_PcmF1(&data_source, info.pcm.get());
            }
        }
        return tmatswalk::Walk::Continue;
    });

    return status;
}

/// Reads the packet whose header was just read into m_buffer, saying why if it cannot (the run's own error text).
/// @p min_data_length is the fewest data bytes a packet of its type can have (see ch10setup::readPacketData).
bool Ch10PacketReader::readPacket(std::uint32_t min_data_length)
{
    switch (ch10setup::readPacketData(m_file_handle, m_header, min_data_length, m_buffer, m_status))
    {
    case ch10setup::PacketRead::Ok:
        return true;
    case ch10setup::PacketRead::Memory:
        m_report.error("Memory allocation failed.");
        return false;
    case ch10setup::PacketRead::ReadError:
        m_report.error("File read error; aborting parsing.");
        return false;
    case ch10setup::PacketRead::BadLength:
        m_report.error(std::format("A packet on channel {} claims {} bytes of data but carries {}: the recording is "
                                   "corrupt; aborting parsing.",
                                   m_header.uChID, m_header.ulDataLen, ch10setup::bodyLength(m_header)));
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
    m_channel_info.clear();
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
        const auto channel = m_channel_info.find(ch);
        if (ch < 0 || ch >= PCMConstants::kMaxChannelCount || channel == m_channel_info.end())
        {
            error = "Channel info not set up for PCM channel " + std::to_string(ch) + ".";
            return false;
        }
        SuPcmF1_Attributes* pcm_attrs = channel->second.pcm.get();
        if (pcm_attrs == nullptr)
        {
            error = "Unable to load PCM attributes for channel " + std::to_string(ch) + ".";
            return false;
        }

        const int64_t sync_mask_i64 = (p->frameSyncMask != 0)
            ? static_cast<int64_t>(p->frameSyncMask)
            : static_cast<int64_t>(fullSyncMask(p->syncPatternLength));

        // Refused (and nothing set) only for a data source with no P record: Set_Attributes_PcmF1 left its attributes
        // zeroed, psuRDataSrc included. What a worker reads from them is then supplied here instead - the word length
        // below, and the bit period, which must come from the operator's Data Rate.
        (void)Set_Attributes_Ext_PcmF1(pcm_attrs->psuRDataSrc, pcm_attrs,
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
        // The length handed to the library above, not read back from it: for a data source with no P record the
        // library takes no settings and leaves its word length 0, which masked every receiver word to 0.
        ra.wordLen       = PCMConstants::kCommonWordLen;
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
        // From the TMATS alone, a P record with no bit rate (P-x\D2) gives 1e7 / 0, infinity, and a data source with no
        // P record gives 0: every bit then has the same time or none, and the stream later failed with a "time jumps
        // ahead by inf s" that blamed the time channel. Said here, where the operator can still enter the rate.
        if (!std::isfinite(delta_100ns) || delta_100ns <= 0.0)
        {
            error = std::format("Channel {}'s TMATS gives no bit rate (P-x\\D2): enter the stream's Data Rate.", ch);
            return false;
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
        // A requested report (requestProgress()) is a relaxed load per packet, and an exchange only when one was asked.
        const bool requested = m_progress_requested.load(std::memory_order_relaxed) &&
                               m_progress_requested.exchange(false, std::memory_order_relaxed);
        if (m_total_file_size > 0 && (requested || progress_gate.due()))
        {
            progress_gate.reset();
            int64_t current_pos = 0;
            enI106Ch10GetPos(m_file_handle, &current_pos);
            int percent = static_cast<int>(current_pos * kPercent100 / m_total_file_size);
            if (percent != last_reported_percent || requested)
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
            if (!readPacket(0))
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

            // The channel-specific word, then the PCM bits: a data length shorter than the word is a corrupt header.
            if (!readPacket(static_cast<std::uint32_t>(sizeof(SuPcmF1_ChanSpec))))
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
