/**
 * @file frameprocessor.cpp
 * @brief Implementation of FrameProcessor — PCM frame extraction into memory.
 */

#include "frameprocessor.h"

#include <ctime>
#include <utility>

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QVector>

#include "constants.h"
#include "framesetup.h"
#include "i106_decode_pcmf1.h"
#include "i106_decode_time.h"

using namespace Irig106;

namespace {
    // Time threshold for gap detection in seconds
    constexpr double kTimeGapThreshold = 2.0;
    // Conversion factor from 100ns units to seconds
    constexpr double k100NsToSeconds = 1.0e-7;
    // Percentage reporting intervals
    constexpr int kPercent100 = 100;
    constexpr int kPercent10 = 10;
}

////////////////////////////////////////////////////////////////////////////////
//                          IRIG106 HELPER METHODS                            //
////////////////////////////////////////////////////////////////////////////////

void FrameProcessor::freeChanInfoTable(QVector<SuChanInfo*>& channel_info)
{
    for(auto* info : channel_info)
    {
        if(info != nullptr)
        {
            if(info->psuAttributes != nullptr)
            {
                if (strcasecmp(info->psuRDataSrc->szChannelDataType,"PCMIN") == 0)
                {
                    FreeOutputBuffers_PcmF1(static_cast<SuPcmF1_Attributes*>(info->psuAttributes));
                }
                free(info->psuAttributes); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
                info->psuAttributes = nullptr;
            }

            delete info;
        }
    }
    // Clear and reset to null pointers
    channel_info.clear();
    channel_info.resize(PCMConstants::kMaxChannelCount, nullptr);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
EnI106Status FrameProcessor::assembleAttributesFromTMATS(SuTmatsInfo* tmats_info,
                                                         QVector<SuChanInfo*>& channel_info)
{
    SuRRecord* psuRRecord = nullptr;
    SuRDataSource* psuRDataSrc = nullptr;
    int iTrackNumber = 0;

    if((tmats_info->psuFirstGRecord == nullptr) || (tmats_info->psuFirstRRecord == nullptr))
    {
        // For simplicity in this legacy port, we just logging error
        return(I106_INVALID_DATA);
    }

    // Ensure vector is sized correctly
    if (channel_info.size() < PCMConstants::kMaxChannelCount)
    {
        channel_info.resize(PCMConstants::kMaxChannelCount, nullptr);
    }

    psuRRecord = tmats_info->psuFirstRRecord;
    while (psuRRecord != nullptr)
    {
        psuRDataSrc = psuRRecord->psuFirstDataSource;
        while (psuRDataSrc != nullptr)
        {
            if(psuRDataSrc->szTrackNumber == nullptr)
            {
                // Uninitialized track number, skip
                psuRDataSrc = psuRDataSrc->psuNext;
                continue;
            }

            iTrackNumber = atoi(psuRDataSrc->szTrackNumber);

            if(iTrackNumber >= PCMConstants::kMaxChannelCount)
            {
                return(I106_BUFFER_TOO_SMALL);
            }

            if (channel_info[iTrackNumber] == nullptr)
            {
                channel_info[iTrackNumber] = new SuChanInfo();
                memset(channel_info[iTrackNumber], 0, sizeof(SuChanInfo));

                channel_info[iTrackNumber]->uChID = static_cast<uint16_t>(iTrackNumber);
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                channel_info[iTrackNumber]->bEnabled = (psuRDataSrc->szEnabled[0] == 'T') ? 1 : 0;
                channel_info[iTrackNumber]->psuRDataSrc = psuRDataSrc;

                if (strcasecmp(psuRDataSrc->szChannelDataType,"PCMIN") == 0)
                {
                    // Allocation using calloc to match legacy C API expectations
                    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory, cppcoreguidelines-no-malloc)
                    channel_info[iTrackNumber]->psuAttributes = calloc(1, sizeof(SuPcmF1_Attributes));
                    if(channel_info[iTrackNumber]->psuAttributes == nullptr)
                    {
                        freeChanInfoTable(channel_info);
                        return(I106_BUFFER_TOO_SMALL);
                    }
                    (void)Set_Attributes_PcmF1(psuRDataSrc, static_cast<SuPcmF1_Attributes*>(channel_info[iTrackNumber]->psuAttributes));
                }
            }

            psuRDataSrc = psuRDataSrc->psuNext;
        }

        psuRRecord = psuRRecord->psuNext;
    }

    return(I106_OK);
}

////////////////////////////////////////////////////////////////////////////////
//                          PCM BIT-LEVEL HELPERS                             //
////////////////////////////////////////////////////////////////////////////////

// Static method
void FrameProcessor::derandomizeBitstream(uint8_t* data, uint64_t total_bits, uint16_t& lfsr)
{
    // Constants for RNRZ-L derandomization
    const uint16_t kLfsrMask = 0x7FFF;
    const int kTap1 = 13;
    const int kTap2 = 14;

    for (uint64_t i = 0; i < total_bits; i++)
    {
        uint32_t byte_idx = static_cast<uint32_t>(i >> 3);
        uint8_t bit_idx = static_cast<uint8_t>(7 - (i & 7));

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        int in_bit  = (data[byte_idx] >> bit_idx) & 1;
        int lfsr_out_bit = ((lfsr >> kTap1) & 1) ^ ((lfsr >> kTap2) & 1);
        int descrambled_bit = in_bit ^ lfsr_out_bit;
        lfsr = ((lfsr << 1) | in_bit) & kLfsrMask;

        if (descrambled_bit != 0)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            data[byte_idx] |= static_cast<uint8_t>(1 << bit_idx);
        }
        else
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            data[byte_idx] &= static_cast<uint8_t>(~(1 << bit_idx));
        }
    }
}

// Static method
bool FrameProcessor::hasSyncPattern(const uint8_t* data, uint64_t total_bits,
                                    uint64_t sync_pat, uint64_t sync_mask,
                                    uint32_t sync_pat_len)
{
    uint64_t test_word = 0;
    uint64_t bits_loaded = 0;
    const uint8_t kHighBitMask = 0x80;

    for (uint64_t i = 0; i < total_bits; i++)
    {
        uint32_t byte_idx = static_cast<uint32_t>(i / 8);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        uint8_t bit_val = ((data[byte_idx] & (kHighBitMask >> (i % 8))) != 0) ? 1 : 0;
        test_word = (test_word << 1) | bit_val;
        bits_loaded++;
        if (bits_loaded >= sync_pat_len &&
            (test_word & sync_mask) == sync_pat)
        {
            return true;
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////
//                       CONSTRUCTOR / DESTRUCTOR                             //
////////////////////////////////////////////////////////////////////////////////

FrameProcessor::FrameProcessor(QObject* parent)
    : QObject(parent),
      m_total_file_size(0),
      m_abort_requested(false)
{
    putenv("TZ=GMT0");
    tzset();

    m_channel_info.resize(PCMConstants::kMaxChannelCount, nullptr);
    m_buffer.resize(PCMConstants::kDefaultBufferSize);
}

FrameProcessor::~FrameProcessor()
{
    freeChanInfoTable(m_channel_info);
}

void FrameProcessor::requestAbort()
{
    m_abort_requested.store(true, std::memory_order_relaxed);
}


////////////////////////////////////////////////////////////////////////////////
//                            FILE I/O                                        //
////////////////////////////////////////////////////////////////////////////////

bool FrameProcessor::ensureBufferCapacity(qsizetype required)
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

bool FrameProcessor::openFile(const QString& filename)
{
    m_status = enI106Ch10Open(&m_file_handle, filename.toUtf8().constData(), I106_READ);

    if (m_status != I106_OK && m_status != I106_OPEN_WARNING)
    {
        emit errorOccurred("Error opening data file.");
        return false;
    }

    m_status = enI106_SyncTime(m_file_handle, bFALSE, 0);

    if (m_status != I106_OK)
    {
        emit errorOccurred("Error establishing time sync.");
        return false;
    }

    return true;
}

void FrameProcessor::closeFile() const
{
    if (m_file_handle >= 0)
    {
        enI106Ch10Close(m_file_handle);
    }
    // Vector clears automatically
}

////////////////////////////////////////////////////////////////////////////////
//                          PROCESSING                                        //
////////////////////////////////////////////////////////////////////////////////

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool FrameProcessor::process(const ProcessingParams& params, FrameSetup* frame_setup)
{
    const auto& filename            = params.filename;
    const int   time_channel_id     = params.time_channel_id;
    const int   pcm_channel_id      = params.pcm_channel_id;
    const auto  frame_sync          = params.frame_sync;
    const int   sync_pattern_len    = params.sync_pattern_length;
    const int   words_in_minor_frame = params.words_in_minor_frame;
    const int   bits_in_minor_frame = params.bits_in_minor_frame;
    const auto  start_seconds       = params.start_seconds;
    const auto  stop_seconds        = params.stop_seconds;
    const int   sample_rate         = params.sample_rate;
    const bool  is_randomized       = params.is_randomized;
    const bool  receiver_mode       = (params.mode == StreamMode::ReceiverChannelInfo);

    QElapsedTimer elapsed_timer;
    elapsed_timer.start();

    m_total_file_size = QFileInfo(filename).size();
    int last_reported_percent = -1;

    // Validate channel IDs before using them as array indices
    if (time_channel_id < 0 || time_channel_id >= PCMConstants::kMaxChannelCount)
    {
        emit errorOccurred("Time channel ID is out of range.");
        emit processingFinished(false);
        return false;
    }
    if (pcm_channel_id < 0 || pcm_channel_id >= PCMConstants::kMaxChannelCount)
    {
        emit errorOccurred("PCM channel ID is out of range.");
        emit processingFinished(false);
        return false;
    }

    // Clear channel info for this run
    freeChanInfoTable(m_channel_info);

    // Open input file and sync time
    emit logMessage("Opening Chapter 10 file...");
    if (!openFile(filename))
    {
        emit errorOccurred("Failed to load Chapter 10 file.");
        emit processingFinished(false);
        return false;
    }

    // Cleanup helper for error paths
    auto fail = [&](const QString& msg) -> bool {
        emit errorOccurred(msg);
        closeFile();
        emit processingFinished(false);
        return false;
    };

    // Initialize the in-memory result bundle for this run.
    m_result = ProcessedStreamData();
    m_result.streamLabel  = params.stream_label;
    m_result.pcmChannelId = pcm_channel_id;
    m_result.mode         = params.mode;

    // Pre-cache enabled parameters to avoid repeated iteration in hot loops.
    // FrameSyncLockStats mode records lock percentage only — no receiver channels.
    QVector<ParameterInfo*> enabled_params;
    if (receiver_mode && frame_setup != nullptr)
    {
        enabled_params.reserve(frame_setup->length());
        for (int i = 0; i < frame_setup->length(); i++)
        {
            ParameterInfo* param = frame_setup->getParameter(i);
            if (param->is_enabled)
            {
                enabled_params.push_back(param);
            }
        }
    }

    // Build the in-memory channel series, index-aligned to enabled_params.
    m_result.channels.reserve(enabled_params.size());
    for (const auto* param : enabled_params)
    {
        ProcessedChannelSeries series;
        series.name = param->name;
        series.word = param->word;
        m_result.channels.push_back(series);
    }

    // Read and process the first packet (must be TMATS)
    emit logMessage("Reading TMATS metadata...");
    m_status = enI106Ch10ReadNextHeader(m_file_handle, &m_header);

    if (m_status != I106_OK)
    {
        return fail("Failed to read first header.");
    }

    if (m_header.ubyDataType == I106CH10_DTYPE_TMATS)
    {
        if (!ensureBufferCapacity(static_cast<qsizetype>(m_header.ulPacketLen)))
        {
            return fail("Memory allocation failed.");
        }

        m_status = enI106Ch10ReadData(m_file_handle, static_cast<unsigned long>(m_buffer.size()), m_buffer.data());
        if (m_status != I106_OK)
        {
            return fail("Failed to read data from first header.");
        }

        memset(&m_tmats_info, 0, sizeof(m_tmats_info));
        m_status = enI106_Decode_Tmats(&m_header, m_buffer.data(), &m_tmats_info);
        if (m_status != I106_OK)
        {
            return fail("Failed to process TMATS info from first header.");
        }

        m_status = assembleAttributesFromTMATS(&m_tmats_info, m_channel_info);
        if (m_status != I106_OK)
        {
            return fail("Failed to assemble attributes from TMATS header.");
        }
    }
    else
    {
        return fail("Failed to find TMATS message.");
    }

    // Set up PCM attributes for the selected channel
    emit logMessage("Setting up PCM attributes...");
    if (m_channel_info[pcm_channel_id] == nullptr)
    {
        return fail("Channel info not set up for selected PCM channel.");
    }

    auto* pcm_attrs = static_cast<SuPcmF1_Attributes*>(m_channel_info[pcm_channel_id]->psuAttributes);
    if (pcm_attrs == nullptr)
    {
        return fail("Unable to load PCM attributes.");
    }

    // Use caller-supplied mask (already derived or user-specified by the coordinator/viewmodel).
    const int64_t sync_mask_i64 = (params.frame_sync_mask != 0)
        ? static_cast<int64_t>(params.frame_sync_mask)
        : static_cast<int64_t>((sync_pattern_len > 0 && sync_pattern_len < 64)
            ? (1ULL << sync_pattern_len) - 1
            : 0x7FFFFFFFFFFFFFFFULL);

    Set_Attributes_Ext_PcmF1(pcm_attrs->psuRDataSrc, pcm_attrs,
                              -1, // lRecordNum
                              -1, // lBitsPerSec
                              PCMConstants::kCommonWordLen,
                              -1, // lWordTransferOrder
                              -1, // lParityType
                              -1, // lParityTransferOrder
                              PCMConstants::kNumMinorFrames,
                              words_in_minor_frame,
                              bits_in_minor_frame,
                              -1, // lMinorFrameSyncType
                              sync_pattern_len,
                              static_cast<int64_t>(frame_sync), // llMinorFrameSyncPat
                              -1, // lMinSyncs
                              sync_mask_i64, // llMinorFrameSyncMask
                              -1); // lNoByteSwap (use TMATS default)

    // -----------------------------------------------------------------------
    // Set up frame extraction state machine
    // -----------------------------------------------------------------------
    uint64_t sync_pat = pcm_attrs->ullMinorFrameSyncPat;
    uint64_t sync_mask = pcm_attrs->ullMinorFrameSyncMask;
    uint32_t sync_pat_len = pcm_attrs->ulMinorFrameSyncPatLen;
    uint32_t bits_in_frame = pcm_attrs->ulBitsInMinorFrame;
    uint32_t words_in_frame = pcm_attrs->ulWordsInMinorFrame;
    uint32_t word_len = pcm_attrs->ulCommonWordLen;
    uint64_t word_mask = pcm_attrs->ullCommonWordMask;
    double delta_100ns = pcm_attrs->dDelta100NanoSeconds;

    // The bit period (delta_100ns) defaults to the TMATS-derived bit rate. If the
    // user supplied an explicit data rate, override it (TMATS is often wrong/missing).
    if (params.data_rate_bps > 0.0)
    {
        constexpr double k100NsPerSecond = 1e7;
        delta_100ns = k100NsPerSecond / params.data_rate_bps;
        emit logMessage(QString("Using user data rate: %1 Mbps")
                        .arg(params.data_rate_bps / 1e6, 0, 'f', 3));
    }

    uint64_t test_word = 0;
    uint64_t bits_loaded = 0;
    uint32_t minor_frame_bit_count = 0;
    uint32_t minor_frame_word_count = 0;
    uint32_t data_word_bit_count = 0;
    int32_t save_data = 0;     // 0=waiting, 1=collecting, 2=frame complete
    uint64_t sync_count = UINT64_MAX; // -1 equivalent: no sync found yet
    uint64_t total_syncs_found = 0;
    uint64_t total_frames_extracted = 0;
    uint64_t total_bytes_processed = 0;
    uint64_t rows_written = 0;

    QVector<uint64_t> frame_words(words_in_frame, 0);

    // CSV output state
    double sample_period = 1.0 / static_cast<double>(sample_rate);
    double current_time_sample = static_cast<double>(start_seconds);
    double next_time_sample = current_time_sample + sample_period;
    int n_samples = 0;

    // Expected frames per output time window — used to compute lock percentage.
    // delta_100ns is time-per-bit in 100 ns units (derived by irig106 from TMATS bitrate).
    double expected_frames_per_window = 0.0;
    if (delta_100ns > 0.0 && bits_in_frame > 0)
    {
        constexpr double k100NsPerSecond = 1e7;
        double bitrate_bps = k100NsPerSecond / delta_100ns;
        expected_frames_per_window = (bitrate_bps / static_cast<double>(bits_in_frame)) * sample_period;
    }

    for (auto* param : enabled_params)
    {
        param->sample_sum = 0;
    }

    // Timestamp tracking: keep current and previous packet time references
    uint64_t global_bit_offset = 0;
    PacketTimeRef current_time_ref = {0, 0, 0};
    PacketTimeRef prev_time_ref = {0, 0, 0};
    bool has_time_ref = false;

    // Time gap detection
    double prev_time_seconds = -1.0;
    int time_gaps_detected = 0;

    // Derandomization state (determined by Randomized flag from TOML config)
    bool needs_derand = is_randomized;
    uint16_t lfsr_state = 0;

    // -----------------------------------------------------------------------
    // Single pass: read packets and process PCM data immediately
    // -----------------------------------------------------------------------
    emit logMessage("Processing PCM data...");
    emit logMessage(QString("Time window: start=%1s stop=%2s")
                    .arg(start_seconds).arg(stop_seconds));
    int packet_count = 0;

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
            emit logMessage("Processing cancelled by user.");
            emit processingFinished(false);
            return false;
        }

        // Report progress every N packets to reduce I/O overhead
        packet_count++;
        if (m_total_file_size > 0 && (packet_count % PCMConstants::kProgressReportInterval) == 0)
        {
            int64_t current_pos = 0;
            enI106Ch10GetPos(m_file_handle, &current_pos);
            int percent = static_cast<int>(current_pos * kPercent100 / m_total_file_size);
            if (percent != last_reported_percent)
            {
                if (percent / kPercent10 != last_reported_percent / kPercent10 && percent > 0)
                {
                    emit logMessage(QString::number(percent) + "% complete...");
                }
                last_reported_percent = percent;
                emit progressUpdated(percent);
            }
        }

        // Process IRIG time packets to maintain time sync
        if (m_header.ubyDataType == I106CH10_DTYPE_IRIG_TIME && m_header.uChID == time_channel_id)
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
                    auto gap_epoch = static_cast<time_t>(pkt_time);
                    struct tm* gt = gmtime(&gap_epoch);
                    if (gt != nullptr)
                    {
                        constexpr int kBase10 = 10;
                        emit logMessage(QString("WARNING: Time gap of %1s at DOY %2 %3:%4:%5")
                            .arg(gap, 0, 'f', 1)
                            .arg(gt->tm_yday + 1, 3, kBase10, QChar('0'))
                            .arg(gt->tm_hour, 2, kBase10, QChar('0'))
                            .arg(gt->tm_min, 2, kBase10, QChar('0'))
                            .arg(gt->tm_sec, 2, kBase10, QChar('0')));
                    }
                }
            }
            prev_time_seconds = pkt_time;
        }

        // Process PCM data from the selected channel
        if (m_header.ubyDataType == I106CH10_DTYPE_PCM_FMT_1 && m_header.uChID == pcm_channel_id)
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

            // Skip the 4-byte SuPcmF1_ChanSpec header to get raw PCM data
            uint32_t data_offset = sizeof(SuPcmF1_ChanSpec);
            if (m_header.ulDataLen <= data_offset)
            {
                continue;
            }

            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            auto* raw_data = reinterpret_cast<uint8_t*>(m_buffer.data() + data_offset);
            uint32_t raw_len = m_header.ulDataLen - data_offset;
            uint64_t packet_bits = static_cast<uint64_t>(raw_len) * 8;

            // Byte-swap raw data if needed (library default: swap)
            if (pcm_attrs->bDontSwapRawData == 0)
            {
                SwapBytes_PcmF1(raw_data, static_cast<long>(raw_len));
            }

            if (needs_derand)
            {
                derandomizeBitstream(raw_data, packet_bits, lfsr_state);
            }

            // Update time references (keep current + previous for boundary frames)
            int64_t pkt_base_time = 0;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            vTimeArray2LLInt(m_header.aubyRefTime, &pkt_base_time);

            if (has_time_ref)
            {
                prev_time_ref = current_time_ref;
            }

            current_time_ref.base_time = pkt_base_time;
            current_time_ref.start_bit = global_bit_offset;
            current_time_ref.num_bits = packet_bits;
            has_time_ref = true;

            constexpr uint64_t kAbortCheckMask = 0xFFFF;
            // Process all bits in this packet through the frame extraction state machine
            for (uint64_t bit_pos = 0; bit_pos < packet_bits; bit_pos++)
            {
                if ((bit_pos & kAbortCheckMask) == 0 && m_abort_requested.load(std::memory_order_relaxed))
                {
                    emit logMessage("Processing cancelled by user.");
                    emit processingFinished(false);
                    return false;
                }

                uint32_t mbyte_idx = static_cast<uint32_t>(bit_pos / 8);
                constexpr uint8_t kHighBit = 0x80;
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                uint8_t bit_val = ((raw_data[mbyte_idx] & (kHighBit >> (bit_pos % 8))) != 0) ? 1 : 0;

                test_word = (test_word << 1) | bit_val;
                bits_loaded++;
                minor_frame_bit_count++;

                // Check for sync word
                if (bits_loaded >= sync_pat_len &&
                    (test_word & sync_mask) == sync_pat)
                {
                    total_syncs_found++;

                    if (minor_frame_bit_count == bits_in_frame)
                    {
                        sync_count++;

                        if (sync_count >= pcm_attrs->ulMinSyncs && save_data > 1)
                        {
                            // Compute per-frame time using bit-level interpolation
                            uint64_t global_bit_pos = global_bit_offset + bit_pos;
                            uint64_t frame_start_bit = global_bit_pos + 1 - bits_in_frame;

                            const PacketTimeRef& ref =
                                (frame_start_bit >= current_time_ref.start_bit)
                                    ? current_time_ref : prev_time_ref;

                            int64_t frame_rel_time = ref.base_time +
                                static_cast<int64_t>(
                                    static_cast<double>(frame_start_bit - ref.start_bit) * delta_100ns);

                            enI106_RelInt2IrigTime(m_file_handle, frame_rel_time, &m_irig_time);
                            double current_time = (k100NsToSeconds * static_cast<double>(m_irig_time.ulFrac))
                                                  + static_cast<double>(m_irig_time.ulSecs);
                            bool write_samples = false;

                            if (current_time >= static_cast<double>(start_seconds) && current_time <= static_cast<double>(stop_seconds))
                            {
                                if (next_time_sample < current_time)
                                {
                                    if (n_samples > 0)
                                    {
                                        double lock_pct = 0.0;
                                        if (expected_frames_per_window > 0.0)
                                        {
                                            lock_pct = (static_cast<double>(n_samples) / expected_frames_per_window) * 100.0;
                                            if (lock_pct > 100.0) lock_pct = 100.0;
                                        }
                                        recordTimeSample(current_time_sample, n_samples, lock_pct, enabled_params);
                                        rows_written++;
                                    }

                                    n_samples = 0;
                                    write_samples = true;
                                }

                                if (write_samples)
                                {
                                    while (next_time_sample < current_time)
                                    {
                                        current_time_sample += sample_period;
                                        next_time_sample += sample_period;
                                    }
                                }

                                for (auto* param : enabled_params)
                                {
                                    if (param->word >= 0 &&
                                        param->word < static_cast<int>(words_in_frame))
                                    {
                                        int64_t raw_value = static_cast<int64_t>(frame_words[param->word] & word_mask);
                                        double scaled_value = (static_cast<double>(raw_value) + param->scale) * param->slope;
                                        param->sample_sum += scaled_value;
                                    }
                                }

                                n_samples++;
                                total_frames_extracted++;
                            }
                        }
                    }

                    minor_frame_bit_count = 0;
                    minor_frame_word_count = 1;
                    data_word_bit_count = 0;
                    save_data = 1;
                }
                else
                {
                    // Accumulate data word bits between sync patterns
                    if (save_data == 1)
                    {
                        data_word_bit_count++;
                        if (data_word_bit_count >= word_len)
                        {
                            if (minor_frame_word_count - 1 < words_in_frame)
                            {
                                frame_words[minor_frame_word_count - 1] = test_word;
                            }
                            data_word_bit_count = 0;
                            minor_frame_word_count++;
                        }

                        if (minor_frame_word_count >= words_in_frame)
                        {
                            save_data = 2;
                        }
                    }
                }
            }

            global_bit_offset += packet_bits;
            total_bytes_processed += raw_len;
        }
    }

    closeFile();

    // Flush the last set of accumulated samples
    if (n_samples > 0)
    {
        double lock_pct = 0.0;
        if (expected_frames_per_window > 0.0)
        {
            lock_pct = (static_cast<double>(n_samples) / expected_frames_per_window) * 100.0;
            if (lock_pct > 100.0) lock_pct = 100.0;
        }
        recordTimeSample(current_time_sample, n_samples, lock_pct, enabled_params);
        rows_written++;
    }

    emit progressUpdated(kPercent100);
    emit logMessage(QString::number(total_bytes_processed) + " bytes processed, "
                    + QString::number(total_syncs_found) + " syncs found, "
                    + QString::number(total_frames_extracted) + " frames extracted.");

    if (total_syncs_found == 0)
    {
        emit errorOccurred("Frame sync pattern was not found in the data stream. "
                           "Verify the frame sync pattern and PCM channel are correct.");
        emit processingFinished(false);
        return false;
    }

    if (total_frames_extracted == 0)
    {
        emit errorOccurred("Frame sync pattern was found but no valid frames were extracted. "
                           "Check the frame parameters and time window settings.");
        emit processingFinished(false);
        return false;
    }

    qint64 elapsed_ms = elapsed_timer.elapsed();
    constexpr double kMsPerSec = 1000.0;
    double elapsed_sec = static_cast<double>(elapsed_ms) / kMsPerSec;

    emit logMessage(QString("Processing complete — %1 samples extracted, elapsed %2s.")
        .arg(rows_written)
        .arg(elapsed_sec, 0, 'f', 1));

    if (time_gaps_detected > 0)
    {
        emit logMessage(QString("WARNING: %1 time gap(s) detected in recording.").arg(time_gaps_detected));
    }

    emit processingFinished(true);
    return true;
}

void FrameProcessor::recordTimeSample(double current_time_sample,
                                      int n_samples,
                                      double lock_percentage,
                                      const QVector<ParameterInfo*>& enabled_params)
{
    // Store the absolute IRIG seconds; the plot derives DOY/HH:MM:SS from this.
    m_result.timesSec.push_back(current_time_sample);
    m_result.lockPercent.push_back(lock_percentage);

    // enabled_params is index-aligned to m_result.channels (built in process()).
    for (int i = 0; i < enabled_params.size(); i++)
    {
        ParameterInfo* param = enabled_params[i];
        m_result.channels[i].values.push_back(param->sample_sum / n_samples);
        param->sample_sum = 0;
    }
}
// End of file!
