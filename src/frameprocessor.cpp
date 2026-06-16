/**
 * @file frameprocessor.cpp
 * @brief Implementation of FrameProcessor — PCM frame extraction into memory.
 */

#include "frameprocessor.h"

#include <QElapsedTimer>
#include <QVector>

#include "framesetup.h"
#include "packetqueue.h"
#include "irig106ch10.h"       // IWYU pragma: keep
#include "i106_time.h"         // IWYU pragma: keep
#include "i106_decode_tmats.h" // IWYU pragma: keep
#include "i106_decode_pcmf1.h"

using namespace Irig106;

namespace {
    // Conversion factor from 100ns units to seconds
    constexpr double k100NsToSeconds = 1.0e-7;
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

////////////////////////////////////////////////////////////////////////////////
//                       CONSTRUCTOR / DESTRUCTOR                             //
////////////////////////////////////////////////////////////////////////////////

FrameProcessor::FrameProcessor(QObject* parent)
    : QObject(parent),
      m_abort_requested(false)
{
}

FrameProcessor::~FrameProcessor() = default;

void FrameProcessor::requestAbort()
{
    m_abort_requested.store(true, std::memory_order_relaxed);
}

////////////////////////////////////////////////////////////////////////////////
//                          PROCESSING                                        //
////////////////////////////////////////////////////////////////////////////////

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool FrameProcessor::process(const ProcessingParams& params, FrameSetup* frame_setup)
{
    const auto  start_seconds  = params.startSeconds;
    const auto  stop_seconds   = params.stopSeconds;
    const bool  is_randomized  = params.isRandomized;
    const bool  receiver_mode  = (params.mode == StreamMode::ReceiverChannelInfo);

    PacketQueue* queue = params.packetQueue;
    if (queue == nullptr)
    {
        emit errorOccurred("Internal error: no packet queue for stream.");
        emit processingFinished(false);
        return false;
    }

    const ResolvedPcmAttrs& attrs = params.resolvedAttrs;
    if (!attrs.resolved)
    {
        emit errorOccurred("Internal error: PCM attributes not resolved.");
        emit processingFinished(false);
        return false;
    }

    QElapsedTimer elapsed_timer;
    elapsed_timer.start();

    // Initialize the in-memory result bundle for this run.
    m_result = ProcessedStreamData();
    m_result.streamLabel  = params.streamLabel;
    m_result.pcmChannelId = params.pcmChannelId;
    m_result.mode         = params.mode;

    // Pre-cache enabled parameters to avoid repeated iteration in hot loops.
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

    // ---- Frame extraction state (resolved by the reader from TMATS) ----
    uint64_t sync_pat      = attrs.syncPat;
    uint64_t sync_mask     = attrs.syncMask;
    uint32_t sync_pat_len  = attrs.syncPatLen;
    uint32_t bits_in_frame = attrs.bitsInFrame;
    uint32_t words_in_frame = attrs.wordsInFrame;
    uint32_t word_len      = attrs.wordLen;
    uint64_t word_mask     = attrs.wordMask;
    uint32_t min_syncs     = attrs.minSyncs;
    double   delta_100ns   = attrs.delta100ns;
    const bool needs_swap  = attrs.needsSwap;

    uint64_t test_word = 0;
    uint64_t bits_loaded = 0;
    uint32_t minor_frame_bit_count = 0;
    uint32_t minor_frame_word_count = 0;
    uint32_t data_word_bit_count = 0;
    int32_t save_data = 0;     // 0=waiting, 1=collecting, 2=frame complete
    uint64_t sync_count = UINT64_MAX; // -1 equivalent: no sync found yet
    bool first_sync_interval_found = false;
    uint64_t total_syncs_found = 0;
    uint64_t total_frames_extracted = 0;
    uint64_t total_bytes_processed = 0;
    uint64_t rows_written = 0;

    QVector<uint64_t> frame_words(words_in_frame, 0);

    // Output sample windowing state.
    double sample_period = params.samplePeriodSec;
    double current_time_sample = static_cast<double>(start_seconds);
    double next_time_sample = current_time_sample + sample_period;
    int n_samples = 0;

    // Counters for lock percentage calculation
    uint64_t valid_bits_in_window = 0;
    uint64_t total_bits_in_window = 0;

    // Cumulative count of missed frames within the
    // processing window. Monotonic across the whole run — one value emitted per
    // output sample alongside the lock percentage.
    uint64_t accumulated_missed_frames = 0;

    for (auto* param : enabled_params)
    {
        param->sample_sum = 0;
    }

    // Timestamp tracking: keep current and previous packet time references.
    uint64_t global_bit_offset = 0;
    PacketTimeRef current_time_ref = {0.0, 0, 0};
    PacketTimeRef prev_time_ref = {0.0, 0, 0};
    bool has_time_ref = false;

    // Derandomization state carried across packets.
    bool needs_derand = is_randomized;
    uint16_t lfsr_state = 0;

    emit logMessage(QString("Processing stream %1 (window: start=%2s stop=%3s)...")
                    .arg(params.streamLabel).arg(start_seconds).arg(stop_seconds));

    // -----------------------------------------------------------------------
    // Consume packets from the queue until the end-of-stream sentinel.
    // -----------------------------------------------------------------------
    while (true)
    {
        if (m_abort_requested.load(std::memory_order_relaxed))
        {
            emit logMessage("Processing cancelled by user.");
            emit processingFinished(false);
            return false;
        }

        PacketItem item = queue->dequeue();
        if (item.endOfStream)
        {
            break;
        }

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        auto* raw_data = reinterpret_cast<uint8_t*>(item.payload.data()); // detaches COW copy
        uint32_t raw_len = static_cast<uint32_t>(item.payload.size());
        uint64_t packet_bits = item.packetBits;

        if (needs_swap)
        {
            SwapBytes_PcmF1(raw_data, static_cast<long>(raw_len));
        }
        if (needs_derand)
        {
            derandomizeBitstream(raw_data, packet_bits, lfsr_state);
        }

        // Update time references (keep current + previous for boundary frames).
        if (has_time_ref)
        {
            prev_time_ref = current_time_ref;
        }
        current_time_ref.base_abs_seconds = item.baseAbsSeconds;
        current_time_ref.start_bit        = global_bit_offset;
        current_time_ref.num_bits         = packet_bits;
        has_time_ref = true;

        double time_per_bit = delta_100ns * k100NsToSeconds;

        constexpr uint64_t kAbortCheckMask = 0xFFFF;
        for (uint64_t bit_pos = 0; bit_pos < packet_bits; bit_pos++)
        {
            if ((bit_pos & kAbortCheckMask) == 0 && m_abort_requested.load(std::memory_order_relaxed))
            {
                emit logMessage("Processing cancelled by user.");
                emit processingFinished(false);
                return false;
            }

            double current_time_eval = current_time_ref.base_abs_seconds + (static_cast<double>(bit_pos) * time_per_bit);

            if (current_time_eval >= static_cast<double>(start_seconds) && current_time_eval <= static_cast<double>(stop_seconds))
            {
                while (next_time_sample < current_time_eval)
                {
                    double lock_pct = 0.0;
                    if (total_bits_in_window > 0)
                    {
                        lock_pct = (static_cast<double>(valid_bits_in_window) / static_cast<double>(total_bits_in_window)) * 100.0;
                    }

                    if (n_samples > 0)
                    {
                        recordTimeSample(current_time_sample, n_samples, lock_pct,
                                         static_cast<double>(accumulated_missed_frames), enabled_params);
                    }
                    else
                    {
                        // Time gap detected (no packets processed for this second).
                        // Extrapolate missed frames for this gap based on expected data rate.
                        double bit_rate = params.dataRateBps;
                        if (bit_rate <= 0.0 && params.resolvedAttrs.delta100ns > 0.0)
                        {
                            bit_rate = 1e7 / params.resolvedAttrs.delta100ns;
                        }

                        if (first_sync_interval_found && bit_rate > 0.0)
                        {
                            double expected_bits = sample_period * bit_rate;
                            uint64_t extrapolated_frames = static_cast<uint64_t>(expected_bits / bits_in_frame);
                            accumulated_missed_frames += extrapolated_frames;
                            sync_count = UINT64_MAX; // Ensure lock is dropped during gap
                        }

                        recordTimeSample(current_time_sample, 1, 0.0, // lock_pct is 0.0 during gap
                                         static_cast<double>(accumulated_missed_frames), enabled_params);
                    }
                    
                    rows_written++;
                    n_samples = 0;
                    valid_bits_in_window = 0;
                    total_bits_in_window = 0;
                    
                    current_time_sample += sample_period;
                    next_time_sample += sample_period;
                }
            }

            uint32_t mbyte_idx = static_cast<uint32_t>(bit_pos / 8);
            constexpr uint8_t kHighBit = 0x80;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            uint8_t bit_val = ((raw_data[mbyte_idx] & (kHighBit >> (bit_pos % 8))) != 0) ? 1 : 0;

            test_word = (test_word << 1) | bit_val;
            bits_loaded++;
            minor_frame_bit_count++;

            if (minor_frame_bit_count > bits_in_frame)
            {
                if (first_sync_interval_found) // Don't accumulate missed frames before the very first valid interval is EVER found
                {
                    if (current_time_eval >= static_cast<double>(start_seconds) &&
                        current_time_eval <= static_cast<double>(stop_seconds))
                    {
                        accumulated_missed_frames++;
                    }
                    sync_count = UINT64_MAX; // Loss of lock
                }
                minor_frame_bit_count = 1; // Roll over to continuously track missed frames
            }
            
            bool in_lock = (sync_count != UINT64_MAX) && (sync_count >= min_syncs);

            // Check for sync word
            if (bits_loaded >= sync_pat_len &&
                (test_word & sync_mask) == sync_pat)
            {
                total_syncs_found++;

                // In LOCK state, ignore false positives (off-phase matches). Only
                // process sync matches at the exact expected frame boundary so that PRN
                // data patterns cannot disrupt word collection.
                bool at_boundary = (minor_frame_bit_count == bits_in_frame);
                if (!in_lock || at_boundary)
                {
                    if (at_boundary)
                    {
                        sync_count++;
                        first_sync_interval_found = true;

                        if (sync_count >= min_syncs && save_data > 1)
                        {
                            // We only extract parameters if we are within the selected processing time bounds
                            if (current_time_eval >= static_cast<double>(start_seconds) && current_time_eval <= static_cast<double>(stop_seconds))
                            {
                                for (auto* param : enabled_params)
                                {
                                    if (param->word >= 0 &&
                                        param->word < static_cast<int>(words_in_frame))
                                    {
                                        int64_t raw_value = static_cast<int64_t>(frame_words[param->word] & word_mask);
                                        // Non-linear step calibration (US3.2) when a valid profile is present;
                                        // otherwise the linear (raw + offset) * slope model.
                                        double scaled_value = param->profile.valid
                                            ? interpolateCalibration(static_cast<double>(raw_value), param->profile)
                                            : (static_cast<double>(raw_value) + param->scale) * param->slope;
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
                // else: in_lock && !at_boundary — false positive, leave all state intact.
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

            if (current_time_eval >= static_cast<double>(start_seconds) && current_time_eval <= static_cast<double>(stop_seconds))
            {
                total_bits_in_window++;
                if (in_lock) {
                    valid_bits_in_window++;
                }
            }
        }

        global_bit_offset += packet_bits;
        total_bytes_processed += raw_len;
    }

    // Flush the last set of accumulated samples
    if (n_samples > 0)
    {
        double lock_pct = 0.0;
        if (total_bits_in_window > 0)
        {
            lock_pct = (static_cast<double>(valid_bits_in_window) / static_cast<double>(total_bits_in_window)) * 100.0;
            if (lock_pct > 100.0) lock_pct = 100.0;
        }
        recordTimeSample(current_time_sample, n_samples, lock_pct,
                         static_cast<double>(accumulated_missed_frames), enabled_params);
        rows_written++;
    }

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

    emit logMessage(QString("Stream %1 complete — %2 samples extracted, elapsed %3s.")
        .arg(params.streamLabel)
        .arg(rows_written)
        .arg(elapsed_sec, 0, 'f', 1));

    emit processingFinished(true);
    return true;
}

void FrameProcessor::recordTimeSample(double current_time_sample,
                                      int n_samples,
                                      double lock_percentage,
                                      double accumulated_missed_frames,
                                      const QVector<ParameterInfo*>& enabled_params)
{
    // Store the absolute IRIG seconds; the plot derives DOY/HH:MM:SS from this.
    m_result.timesSec.push_back(current_time_sample);

    // Frame-sync lock percentage and missed-frame accumulation are only
    // meaningful for FrameSyncLockStats streams. Receiver (SNR) streams are
    // fed from a separate piece of equipment and are always considered
    // locked, so no lock series is produced for them.
    if (m_result.mode == StreamMode::FrameSyncLockStats)
    {
        m_result.lockPercent.push_back(lock_percentage);
        m_result.accumulatedMissedFrames.push_back(accumulated_missed_frames);
    }

    // enabled_params is index-aligned to m_result.channels (built in process()).
    for (int i = 0; i < enabled_params.size(); i++)
    {
        ParameterInfo* param = enabled_params[i];
        m_result.channels[i].values.push_back(param->sample_sum / n_samples);
        param->sample_sum = 0;
    }
}
// End of file!
