/**
 * @file ch10packetreader.cpp
 * @brief Implementation of Ch10PacketReader — single-pass read and per-stream fan-out.
 */

#include "ch10packetreader.h"

#include <cstdlib>
#include <cstring>

#include <QElapsedTimer>
#include <QFileInfo>

#include "i106_decode_pcmf1.h"
#include "i106_decode_time.h"

#include "constants.h"
#include "packetqueue.h"

using namespace Irig106;

namespace {
    constexpr double k100NsToSeconds = 1.0e-7;
    constexpr double kTimeGapThreshold = 2.0;
    constexpr int    kPercent100 = 100;
    constexpr int    kPercent10  = 10;
}

////////////////////////////////////////////////////////////////////////////////
//                       CONSTRUCTOR / DESTRUCTOR                             //
////////////////////////////////////////////////////////////////////////////////

Ch10PacketReader::Ch10PacketReader(QObject* parent)
    : QObject(parent),
      m_status(I106_OK),
      m_header(),
      m_tmats_info(),
      m_irig_time(),
      m_abort_requested(false)
{
    m_channel_info.resize(PCMConstants::kMaxChannelCount, nullptr);
    m_buffer.resize(PCMConstants::kDefaultBufferSize);
}

Ch10PacketReader::~Ch10PacketReader()
{
    if (m_file_open)
    {
        enI106Ch10Close(m_file_handle);
        m_file_open = false;
    }
    freeChanInfoTable(m_channel_info);
}

void Ch10PacketReader::requestAbort()
{
    m_abort_requested.store(true, std::memory_order_relaxed);
}

////////////////////////////////////////////////////////////////////////////////
//                          IRIG106 HELPERS                                   //
////////////////////////////////////////////////////////////////////////////////

void Ch10PacketReader::freeChanInfoTable(QVector<SuChanInfo*>& channel_info)
{
    for (auto* info : channel_info)
    {
        if (info != nullptr)
        {
            if (info->psuAttributes != nullptr)
            {
                if (strcasecmp(info->psuRDataSrc->szChannelDataType, "PCMIN") == 0)
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
                                                           QVector<SuChanInfo*>& channel_info)
{
    if ((tmats_info->psuFirstGRecord == nullptr) || (tmats_info->psuFirstRRecord == nullptr))
    {
        return I106_INVALID_DATA;
    }

    if (channel_info.size() < PCMConstants::kMaxChannelCount)
    {
        channel_info.resize(PCMConstants::kMaxChannelCount, nullptr);
    }

    SuRRecord* psuRRecord = tmats_info->psuFirstRRecord;
    while (psuRRecord != nullptr)
    {
        SuRDataSource* psuRDataSrc = psuRRecord->psuFirstDataSource;
        while (psuRDataSrc != nullptr)
        {
            if (psuRDataSrc->szTrackNumber == nullptr)
            {
                psuRDataSrc = psuRDataSrc->psuNext;
                continue;
            }

            int iTrackNumber = atoi(psuRDataSrc->szTrackNumber);
            // atoi() yields 0 on non-numeric text and can be negative for a
            // malformed track string; a negative index would read out of bounds
            // below, so skip such a data source rather than indexing with it.
            if (iTrackNumber < 0)
            {
                psuRDataSrc = psuRDataSrc->psuNext;
                continue;
            }
            if (iTrackNumber >= PCMConstants::kMaxChannelCount)
            {
                return I106_BUFFER_TOO_SMALL;
            }

            if (channel_info[iTrackNumber] == nullptr)
            {
                channel_info[iTrackNumber] = new SuChanInfo();
                memset(channel_info[iTrackNumber], 0, sizeof(SuChanInfo));

                channel_info[iTrackNumber]->uChID = static_cast<uint16_t>(iTrackNumber);
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                channel_info[iTrackNumber]->bEnabled = (psuRDataSrc->szEnabled[0] == 'T') ? 1 : 0;
                channel_info[iTrackNumber]->psuRDataSrc = psuRDataSrc;

                if (strcasecmp(psuRDataSrc->szChannelDataType, "PCMIN") == 0)
                {
                    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory, cppcoreguidelines-no-malloc)
                    channel_info[iTrackNumber]->psuAttributes = calloc(1, sizeof(SuPcmF1_Attributes));
                    if (channel_info[iTrackNumber]->psuAttributes == nullptr)
                    {
                        freeChanInfoTable(channel_info);
                        return I106_BUFFER_TOO_SMALL;
                    }
                    (void)Set_Attributes_PcmF1(psuRDataSrc,
                        static_cast<SuPcmF1_Attributes*>(channel_info[iTrackNumber]->psuAttributes));
                }
            }

            psuRDataSrc = psuRDataSrc->psuNext;
        }
        psuRRecord = psuRRecord->psuNext;
    }

    return I106_OK;
}

bool Ch10PacketReader::ensureBufferCapacity(qsizetype required)
{
    if (required > PCMConstants::kMaxPacketBufferSize)
        return false;
    if (m_buffer.size() >= required)
        return true;
    try {
        m_buffer.resize(required);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////////
//                              PREPARE                                        //
////////////////////////////////////////////////////////////////////////////////

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool Ch10PacketReader::prepare(const QString& filename,
                               int time_channel_id,
                               QVector<ProcessingParams*>& params_list,
                               QString& error)
{
    m_time_channel_id = time_channel_id;
    m_total_file_size = QFileInfo(filename).size(); // qint64, handles >2 GB correctly

    m_status = enI106Ch10Open(&m_file_handle, filename.toUtf8().constData(), I106_READ);
    if (m_status != I106_OK && m_status != I106_OPEN_WARNING)
    {
        error = "Error opening data file.";
        return false;
    }
    m_file_open = true;

    m_status = enI106_SyncTime(m_file_handle, bFALSE, 0);
    if (m_status != I106_OK)
    {
        error = "Error establishing time sync.";
        return false;
    }

    // First packet must be TMATS.
    m_status = enI106Ch10ReadNextHeader(m_file_handle, &m_header);
    if (m_status != I106_OK)
    {
        error = "Failed to read first header.";
        return false;
    }
    if (m_header.ubyDataType != I106CH10_DTYPE_TMATS)
    {
        error = "Failed to find TMATS message.";
        return false;
    }
    if (!ensureBufferCapacity(static_cast<qsizetype>(m_header.ulPacketLen)))
    {
        error = "Memory allocation failed.";
        return false;
    }
    m_status = enI106Ch10ReadData(m_file_handle, static_cast<unsigned long>(m_buffer.size()), m_buffer.data());
    if (m_status != I106_OK)
    {
        error = "Failed to read TMATS data.";
        return false;
    }
    memset(&m_tmats_info, 0, sizeof(m_tmats_info));
    m_status = enI106_Decode_Tmats(&m_header, m_buffer.data(), &m_tmats_info);
    if (m_status != I106_OK)
    {
        error = "Failed to decode TMATS.";
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
            error = "Channel info not set up for PCM channel " + QString::number(ch) + ".";
            return false;
        }
        auto* pcm_attrs = static_cast<SuPcmF1_Attributes*>(m_channel_info[ch]->psuAttributes);
        if (pcm_attrs == nullptr)
        {
            error = "Unable to load PCM attributes for channel " + QString::number(ch) + ".";
            return false;
        }

        const int64_t sync_mask_i64 = (p->frameSyncMask != 0)
            ? static_cast<int64_t>(p->frameSyncMask)
            : static_cast<int64_t>((p->syncPatternLength > 0 && p->syncPatternLength < 64)
                ? (1ULL << p->syncPatternLength) - 1
                : 0xFFFFFFFFFFFFFFFFULL);

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
        ra.wordMask      = pcm_attrs->ullCommonWordMask;
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
            constexpr double k100NsPerSecond = 1e7;
            delta_100ns = k100NsPerSecond / p->dataRateBps;
        }
        ra.delta100ns = delta_100ns;

        // Give the resolved struct to the worker params
        p->resolvedAttrs = ra;

        if (p->packetQueue != nullptr)
        {
            m_routing[ch].push_back(p->packetQueue);
            if (!m_all_queues.contains(p->packetQueue))
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
    emit logMessage("Reading Chapter 10 file...");

    int  last_reported_percent = -1;
    QElapsedTimer progress_timer;
    progress_timer.start();
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
            emit errorOccurred("File read error during data collection.");
            break;
        }

        if (m_abort_requested.load(std::memory_order_relaxed))
        {
            emit logMessage("Reading cancelled by user.");
            break;
        }

        if (m_total_file_size > 0 && progress_timer.elapsed() >= PCMConstants::kProgressReportIntervalMs)
        {
            progress_timer.restart();
            int64_t current_pos = 0;
            enI106Ch10GetPos(m_file_handle, &current_pos);
            int percent = static_cast<int>(current_pos * kPercent100 / m_total_file_size);
            if (percent != last_reported_percent)
            {
                if (percent / kPercent10 != last_reported_percent / kPercent10 && percent > 0)
                {
                    emit logMessage(QString::number(percent) + "% read...");
                }
                last_reported_percent = percent;
                emit progressUpdated(percent);
            }
        }

        // IRIG time packets: keep the file handle's time sync current.
        if (m_header.ubyDataType == I106CH10_DTYPE_IRIG_TIME && m_header.uChID == m_time_channel_id)
        {
            if (!ensureBufferCapacity(static_cast<qsizetype>(m_header.ulPacketLen)))
            {
                emit errorOccurred("Memory allocation failed.");
                break;
            }
            m_status = enI106Ch10ReadData(m_file_handle, static_cast<unsigned long>(m_buffer.size()), m_buffer.data());
            if (m_status != I106_OK)
            {
                emit errorOccurred("File read error; aborting parsing.");
                break;
            }
            enI106_Decode_TimeF1(&m_header, m_buffer.data(), &m_irig_time);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            enI106_SetRelTime(m_file_handle, &m_irig_time, m_header.aubyRefTime);

            double pkt_time = static_cast<double>(m_irig_time.ulSecs) +
                              (k100NsToSeconds * static_cast<double>(m_irig_time.ulFrac));
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

            if (!ensureBufferCapacity(static_cast<qsizetype>(m_header.ulPacketLen)))
            {
                emit errorOccurred("Memory allocation failed.");
                break;
            }
            m_status = enI106Ch10ReadData(m_file_handle, static_cast<unsigned long>(m_buffer.size()), m_buffer.data());
            if (m_status != I106_OK)
            {
                emit errorOccurred("File read error; aborting parsing.");
                break;
            }

            uint32_t data_offset = sizeof(SuPcmF1_ChanSpec);
            if (m_header.ulDataLen <= data_offset)
            {
                continue;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            const char* raw_data = m_buffer.data() + data_offset;
            uint32_t raw_len = m_header.ulDataLen - data_offset;

            // Convert this packet's base time to absolute seconds using the
            // current (latest) time sync state.
            int64_t pkt_base_rel = 0;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            vTimeArray2LLInt(m_header.aubyRefTime, &pkt_base_rel);
            enI106_RelInt2IrigTime(m_file_handle, pkt_base_rel, &m_irig_time);
            double base_abs_seconds = static_cast<double>(m_irig_time.ulSecs) +
                                      (k100NsToSeconds * static_cast<double>(m_irig_time.ulFrac));

            PacketItem item;
            item.payload       = QByteArray(raw_data, static_cast<qsizetype>(raw_len));
            item.baseAbsSeconds = base_abs_seconds;
            item.packetBits    = static_cast<uint64_t>(raw_len) * 8;

            const QVector<PacketQueue*>& queues = route_it.value();
            for (PacketQueue* q : queues)
            {
                q->enqueue(item); // implicitly-shared copy; worker detaches on write
            }
        }
    }

    postSentinels();

    if (time_gaps_detected > 0)
    {
        emit logMessage(QString("WARNING: %1 time gap(s) detected in recording.").arg(time_gaps_detected));
    }

    if (m_file_open)
    {
        enI106Ch10Close(m_file_handle);
        m_file_open = false;
    }

    emit progressUpdated(kPercent100);
    emit finished();
}
