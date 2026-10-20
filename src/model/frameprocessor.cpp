/**
 * @file frameprocessor.cpp
 * @brief Implementation of FrameProcessor — PCM frame extraction into memory.
 */

#include "frameprocessor.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <cstdint>
#include <vector>

#include "irig106ch10.h"       // IWYU pragma: keep
#include "i106_time.h"         // IWYU pragma: keep
#include "i106_decode_tmats.h" // IWYU pragma: keep
#include "i106_decode_pcmf1.h"

#include "containerutil.h"
#include "framesetup.h"
#include "packetqueue.h"

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

// Static method
void FrameProcessor::invertBits(uint8_t* data, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        data[i] = ~data[i];
    }
}

////////////////////////////////////////////////////////////////////////////////
//                       CONSTRUCTOR / DESTRUCTOR                             //
////////////////////////////////////////////////////////////////////////////////

FrameProcessor::FrameProcessor()
    : m_abort_requested(false)
{
}

FrameProcessor::~FrameProcessor() = default;

void FrameProcessor::requestAbort()
{
    m_abort_requested.store(true, std::memory_order_relaxed);
}

void FrameProcessor::setEvents(std::shared_ptr<ProcessingEvents> events)
{
    m_events = std::move(events);
}

void FrameProcessor::reportLog(const std::string& message) const
{
    if (m_events)
    {
        m_events->logMessage(message);
    }
}

void FrameProcessor::reportError(const std::string& message) const
{
    if (m_events)
    {
        m_events->errorOccurred(message);
    }
}

void FrameProcessor::reportFinished(bool success) const
{
    if (m_events)
    {
        m_events->processingFinished(success);
    }
}

////////////////////////////////////////////////////////////////////////////////
//                          PROCESSING                                        //
////////////////////////////////////////////////////////////////////////////////

bool FrameProcessor::process(const ProcessingParams& params, FrameSetup* frame_setup)
{
    const auto  start_seconds  = params.startSeconds;
    const auto  stop_seconds   = params.stopSeconds;
    const bool  is_randomized  = params.isRandomized;
    const bool  is_inverted    = params.isInverted;
    const bool  receiver_mode  = (params.mode == StreamMode::ReceiverChannelInfo);

    PacketQueue* queue = params.packetQueue;
    if (queue == nullptr)
    {
        reportError(streamTag(params) + "Internal error: no packet queue for stream.");
        reportFinished(false);
        return false;
    }

    const ResolvedPcmAttrs& attrs = params.resolvedAttrs;
    if (!attrs.resolved)
    {
        reportError(streamTag(params) + "Internal error: PCM attributes not resolved.");
        reportFinished(false);
        return false;
    }

    // Monotonic, so a wall-clock adjustment mid-run cannot report a negative duration.
    const auto run_start = std::chrono::steady_clock::now();

    // Initialize the in-memory result bundle for this run.
    m_result = ProcessedStreamData();
    m_result.streamLabel  = params.streamLabel;
    m_result.pcmChannelId = params.pcmChannelId;
    m_result.mode         = params.mode;

    // Cache enabled parameters for the hot loop and build the index-aligned
    // in-memory channel series (see buildEnabledParams).
    std::vector<ParameterInfo*> enabled_params = buildEnabledParams(frame_setup, receiver_mode);
    for (auto* param : enabled_params)
    {
        param->sample_sum = 0;
    }

    // ---- Scan working state, seeded from the reader-resolved frame attributes ----
    ScanState state;
    state.sync_pat        = attrs.syncPat;
    state.sync_mask       = attrs.syncMask;
    state.word_mask       = attrs.wordMask;
    state.sync_pat_len    = attrs.syncPatLen;
    state.bits_in_frame   = attrs.bitsInFrame;
    state.words_in_frame  = attrs.wordsInFrame;
    state.word_len        = attrs.wordLen;
    state.min_syncs       = attrs.minSyncs;
    state.start_seconds   = static_cast<double>(start_seconds);
    state.stop_seconds    = static_cast<double>(stop_seconds);
    // Output windowing clock. Seeded lazily from the first packet's actual IRIG
    // timestamp below (not from start_seconds, which is just 0 when the caller has
    // no real time bound) so the clock doesn't have to "catch up" sample-by-sample
    // from the IRIG day epoch to wherever the data actually starts.
    state.sample_period       = params.samplePeriodSec;
    state.current_time_sample = static_cast<double>(start_seconds);
    state.next_time_sample    = state.current_time_sample + state.sample_period;
    state.frame_words = std::vector<uint64_t>(state.words_in_frame, 0);

    const double delta_100ns    = attrs.delta100ns;
    const bool   needs_swap     = attrs.needsSwap;
    // When set, the windowing clock is derived from bits processed (data rate)
    // starting at zero, rather than IRIG absolute time. See ProcessingParams.
    const bool   use_rate_clock = params.useDataRateClock;

    // Per-bit diagnostics the scan helpers don't touch.
    uint64_t total_bytes_processed = 0;
    // Byte-swap bookkeeping, reported once after the drain. See the SwapBytes_PcmF1
    // call site below for why a skip is possible at all.
    uint64_t total_packets         = 0;
    uint64_t swap_skipped_packets  = 0;
    double   first_data_time       = -1.0;
    double   last_data_time        = 0.0;
    bool     time_seeded           = false;

    // Timestamp tracking: keep current and previous packet time references.
    uint64_t global_bit_offset = 0;
    PacketTimeRef current_time_ref = {0.0, 0, 0};
    PacketTimeRef prev_time_ref = {0.0, 0, 0};
    bool has_time_ref = false;

    // This worker's private, mutable copy of the packet in flight. Queue payloads are shared
    // with every other stream on the channel and are const, but swap / invert / derandomize
    // modify in place. Reused across packets, so it settles at the largest packet and stops
    // allocating.
    std::vector<uint8_t> packet_bytes;

    // Derandomization state carried across packets.
    bool needs_derand = is_randomized;
    uint16_t lfsr_state = 0;

    // Only name the window when it is actually bounded; an unset stop prints as
    // 18446744073709551615s, which is noise on every ordinary run.
    const bool bounded = (start_seconds != 0) || (stop_seconds != UINT64_MAX);
    reportLog(streamTag(params)
              + (bounded ? std::format("processing ({}s to {}s)...", start_seconds, stop_seconds)
                         : std::string("processing...")));

    // -----------------------------------------------------------------------
    // Consume packets from the queue until the end-of-stream sentinel.
    // -----------------------------------------------------------------------
    while (true)
    {
        if (m_abort_requested.load(std::memory_order_relaxed))
        {
            reportLog(streamTag(params) + "processing cancelled by user.");
            reportFinished(false);
            return false;
        }

        PacketItem item = queue->dequeue();
        if (item.endOfStream)
        {
            break;
        }

        if (item.payload)
        {
            packet_bytes.assign(item.payload->begin(), item.payload->end());
        }
        else
        {
            packet_bytes.clear();
        }
        uint8_t* raw_data = packet_bytes.data();
        uint32_t raw_len = static_cast<uint32_t>(packet_bytes.size());
        uint64_t packet_bits = item.packetBits;

        if (needs_swap)
        {
            // SwapBytes_PcmF1 returns I106_BUFFER_OVERRUN on an odd byte count and
            // returns BEFORE touching the buffer, so that packet stays unswapped while
            // its even-length neighbours are swapped. Discarding the return - which is
            // what this did - left one stream silently carrying two different byte
            // orders, and the operator no way to see it. Count them and say so after
            // the drain rather than per packet, which would flood the log.
            if (SwapBytes_PcmF1(raw_data, static_cast<long>(raw_len)) != I106_OK)
            {
                swap_skipped_packets++;
            }
        }
        total_packets++;
        if (is_inverted)
        {
            invertBits(raw_data, raw_len);
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

        if (!time_seeded && !use_rate_clock)
        {
            double seed = item.baseAbsSeconds > state.start_seconds
                ? item.baseAbsSeconds
                : state.start_seconds;
            state.current_time_sample = seed;
            state.next_time_sample = seed + state.sample_period;
            time_seeded = true;
        }

        double time_per_bit = delta_100ns * k100NsToSeconds;

        constexpr uint64_t kAbortCheckMask = 0xFFFF;
        for (uint64_t bit_pos = 0; bit_pos < packet_bits; bit_pos++)
        {
            if ((bit_pos & kAbortCheckMask) == 0 && m_abort_requested.load(std::memory_order_relaxed))
            {
                reportLog(streamTag(params) + "processing cancelled by user.");
                reportFinished(false);
                return false;
            }

            double current_time_eval = use_rate_clock
                ? static_cast<double>(global_bit_offset + bit_pos) * time_per_bit
                : current_time_ref.base_abs_seconds + (static_cast<double>(bit_pos) * time_per_bit);

            if (first_data_time < 0.0) first_data_time = current_time_eval;
            last_data_time = current_time_eval;

            // Time only delimits the averaging window; all per-bit accounting below
            // is gated on the bit falling inside the selected [start, stop] window.
            const bool in_window = (current_time_eval >= state.start_seconds &&
                                    current_time_eval <= state.stop_seconds);
            if (in_window)
            {
                closeElapsedWindows(state, current_time_eval, params, enabled_params);
            }

            uint32_t mbyte_idx = static_cast<uint32_t>(bit_pos / 8);
            constexpr uint8_t kHighBit = 0x80;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            uint8_t bit_val = ((raw_data[mbyte_idx] & (kHighBit >> (bit_pos % 8))) != 0) ? 1 : 0;

            scanBit(state, bit_val, in_window, enabled_params);
        }

        global_bit_offset += packet_bits;
        total_bytes_processed += raw_len;
    }

    // Report any byte-swap skips. Silence here is what made this hard to diagnose:
    // the stream simply failed to sync, with nothing to distinguish "wrong pattern"
    // from "half this stream was not transformed the way you asked".
    if (swap_skipped_packets > 0)
    {
        if (swap_skipped_packets == total_packets)
        {
            // Every packet skipped: the setting is inert for this stream, which is
            // worth saying plainly - the operator's choice had no effect at all.
            reportLog(streamTag(params)
                      + std::format("all {} payloads are odd-length and cannot be "
                                    "byte-pair swapped — processed as Legacy Chapter 10 "
                                    "regardless of the setting.",
                                    swap_skipped_packets));
        }
        else
        {
            // The genuinely corrupting case: one stream, two byte orders.
            reportLog(streamTag(params)
                      + std::format("warning: byte swap skipped on {} of {} packets "
                                    "(odd length) — mixed byte order, sync may be unreliable.",
                                    swap_skipped_packets, total_packets));
        }
    }

    // Flush the last set of accumulated samples.
    if (state.n_samples > 0)
    {
        double lock_pct = 0.0;
        if (state.total_bits_in_window > 0)
        {
            // valid_bits_in_window is only incremented inside the same window guard
            // as total_bits_in_window and only while in lock, so it can never exceed
            // it — lock_pct is already bounded at 100, no clamp needed (matching the
            // in-loop computation above).
            lock_pct = (static_cast<double>(state.valid_bits_in_window) /
                        static_cast<double>(state.total_bits_in_window)) * 100.0;
        }
        recordTimeSample(state.current_time_sample, state.n_samples, lock_pct,
                         static_cast<double>(state.accumulated_missed_frames), enabled_params);
        state.rows_written++;
    }

    // duration_cast truncates to whole milliseconds, exactly as QElapsedTimer::elapsed()
    // did, so the one-decimal seconds this feeds in the completion log are unchanged.
    const int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - run_start).count();
    constexpr double kMsPerSec = 1000.0;

    ScanDiagnostics diag;
    diag.total_bytes_processed  = total_bytes_processed;
    diag.total_syncs_found      = state.total_syncs_found;
    diag.total_frames_extracted = state.total_frames_extracted;
    diag.boundary_syncs         = state.boundary_syncs;
    diag.max_sync_run           = state.max_sync_run;
    diag.buffer_ever_filled     = state.buffer_ever_filled;
    diag.first_data_time        = first_data_time;
    diag.last_data_time         = last_data_time;
    diag.rows_written           = state.rows_written;
    diag.bits_in_frame          = state.bits_in_frame;
    diag.words_in_frame         = state.words_in_frame;
    diag.sync_pat_len           = state.sync_pat_len;
    diag.min_syncs              = state.min_syncs;
    diag.sync_pat               = state.sync_pat;
    diag.sync_mask              = state.sync_mask;
    diag.elapsed_sec            = static_cast<double>(elapsed_ms) / kMsPerSec;
    return reportCompletion(params, diag);
}

void FrameProcessor::closeElapsedWindows(ScanState& s, double current_time_eval,
                                         const ProcessingParams& params,
                                         const std::vector<ParameterInfo*>& enabled_params)
{
    while (s.next_time_sample < current_time_eval)
    {
        double lock_pct = 0.0;
        if (s.total_bits_in_window > 0)
        {
            lock_pct = (static_cast<double>(s.valid_bits_in_window) /
                        static_cast<double>(s.total_bits_in_window)) * 100.0;
        }

        if (s.total_bits_in_window > 0)
        {
            // Bits were processed for this window — real data, whether or not any
            // frame locked. If the window was entirely out of lock (n_samples == 0)
            // its missed frames were ALREADY counted bit-by-bit by the in-stream
            // rollover in scanBit(), so we must NOT extrapolate here. Extrapolating
            // data-present dropouts double-counts and biases long-frame streams,
            // whose windows more often contain zero locked frames than short-frame
            // streams with many more frames per window.
            recordTimeSample(s.current_time_sample, s.n_samples, lock_pct,
                             static_cast<double>(s.accumulated_missed_frames), enabled_params);
        }
        else
        {
            // Genuine recording gap: no packets at all spanned this window, so the
            // in-stream counter never ran. Extrapolate the frames that would have
            // been received from the expected data rate.
            double bit_rate = params.dataRateBps;
            if (bit_rate <= 0.0 && params.resolvedAttrs.delta100ns > 0.0)
            {
                bit_rate = 1e7 / params.resolvedAttrs.delta100ns;
            }

            if (bit_rate > 0.0)
            {
                double expected_bits = s.sample_period * bit_rate;
                uint64_t extrapolated_frames = static_cast<uint64_t>(expected_bits / s.bits_in_frame);
                s.accumulated_missed_frames += extrapolated_frames;
                s.sync_count = UINT64_MAX; // Ensure lock is dropped during gap
                s.current_sync_run = 0;
            }

            recordTimeSample(s.current_time_sample, 0, 0.0, // lock_pct is 0.0 during gap
                             static_cast<double>(s.accumulated_missed_frames), enabled_params);
        }

        s.rows_written++;
        s.n_samples = 0;
        s.valid_bits_in_window = 0;
        s.total_bits_in_window = 0;

        s.current_time_sample += s.sample_period;
        s.next_time_sample += s.sample_period;
    }
}

void FrameProcessor::scanBit(ScanState& s, uint8_t bit_val, bool in_window,
                             const std::vector<ParameterInfo*>& enabled_params)
{
    s.test_word = (s.test_word << 1) | bit_val;
    s.bits_loaded++;
    s.minor_frame_bit_count++;

    if (s.minor_frame_bit_count > s.bits_in_frame)
    {
        if (in_window)
        {
            s.accumulated_missed_frames++;
        }
        s.sync_count = UINT64_MAX; // Loss of lock
        s.current_sync_run = 0;
        s.minor_frame_bit_count = 1; // Roll over to continuously track missed frames
    }

    const bool in_lock = (s.sync_count != UINT64_MAX) && (s.sync_count >= s.min_syncs);

    // Check for sync word
    if (s.bits_loaded >= s.sync_pat_len &&
        (s.test_word & s.sync_mask) == s.sync_pat)
    {
        s.total_syncs_found++;

        // In LOCK state, ignore false positives (off-phase matches). Only process
        // sync matches at the exact expected frame boundary so that PRN data patterns
        // cannot disrupt word collection.
        bool at_boundary = (s.minor_frame_bit_count == s.bits_in_frame);
        if (!in_lock || at_boundary)
        {
            if (at_boundary)
            {
                s.sync_count++;
                s.boundary_syncs++;
                s.current_sync_run++;
                if (s.current_sync_run > s.max_sync_run)
                    s.max_sync_run = s.current_sync_run;
            }
            else
            {
                // Off-phase acquisition: a sync match that is NOT on a frame boundary.
                // boundary_syncs is deliberately not incremented here - it used to be,
                // which made the failure diagnostic report an off-phase match as
                // "boundary-aligned" and told operators the opposite of the truth.
                s.sync_count = 1;
                s.current_sync_run = 1;
                if (s.current_sync_run > s.max_sync_run)
                    s.max_sync_run = s.current_sync_run;
            }

            // Only extract a sample once the sync match is boundary-aligned; an
            // off-phase match (re-acquiring lock) must not be treated as a confirmed
            // frame, or PRN data can be sampled as if it were locked.
            if (at_boundary && s.sync_count >= s.min_syncs && s.save_data > 1)
            {
                // We only extract parameters if we are within the selected processing time bounds
                if (in_window)
                {
                    for (auto* param : enabled_params)
                    {
                        if (param->word >= 0 &&
                            param->word < static_cast<int>(s.words_in_frame))
                        {
                            int64_t raw_value = static_cast<int64_t>(s.frame_words[param->word] & s.word_mask);
                            // Accumulate RAW counts; calibration (linear or non-linear)
                            // is applied once to the windowed average in
                            // recordTimeSample(). Averaging raw first is required for
                            // the non-linear profile: the profile maps averaged-raw ->
                            // true dB, so mean(interpolate(raw)) would bias each plateau
                            // off the true step value. For linear cal the two orders are
                            // identical.
                            param->sample_sum += static_cast<double>(raw_value);
                        }
                    }

                    s.n_samples++;
                    s.total_frames_extracted++;
                }
            }

            s.minor_frame_bit_count = 0;
            s.minor_frame_word_count = 1;
            s.data_word_bit_count = 0;
            s.save_data = 1;
        }
        // else: in_lock && !at_boundary — false positive, leave all state intact.
    }
    else
    {
        // Accumulate data word bits between sync patterns
        if (s.save_data == 1)
        {
            s.data_word_bit_count++;
            if (s.data_word_bit_count >= s.word_len)
            {
                if (s.minor_frame_word_count - 1 < s.words_in_frame)
                {
                    s.frame_words[s.minor_frame_word_count - 1] = s.test_word;
                }
                s.data_word_bit_count = 0;
                s.minor_frame_word_count++;
            }

            if (s.minor_frame_word_count >= s.words_in_frame)
            {
                s.save_data = 2;
                s.buffer_ever_filled = true;
            }
        }
    }

    if (in_window)
    {
        s.total_bits_in_window++;
        if (in_lock) {
            s.valid_bits_in_window++;
        }
    }
}

std::vector<ParameterInfo*> FrameProcessor::buildEnabledParams(FrameSetup* frame_setup, bool receiver_mode)
{
    std::vector<ParameterInfo*> enabled_params;
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
    return enabled_params;
}

std::string FrameProcessor::streamTag(const ProcessingParams& params)
{
    // Every message a worker emits carries this. Streams are processed by parallel
    // workers writing into one shared log, so their output interleaves in an order
    // that is not even deterministic between runs; without an identity, four
    // failure reports are indistinguishable from each other.
    //
    // The channel ID leads because that is what the operator configured the stream
    // by. The label is appended only when it adds something - MainViewModel falls
    // back to "Ch <id>" when TMATS gives no name, and "[CH 13 Ch 13]" is noise.
    const std::string& label = params.streamLabel;
    const std::string fallback = "Ch " + std::to_string(params.pcmChannelId);
    if (label.empty() || label == fallback)
        return std::format("[CH {}] ", params.pcmChannelId);
    return std::format("[CH {} {}] ", params.pcmChannelId, label);
}

std::string FrameProcessor::syncSpec(const ScanDiagnostics& d)
{
    // The pattern is worth its characters: PRN-11 and PRN-15 are both 32 bits, so a
    // stream running the wrong one is invisible in a report that prints only the
    // length. The mask is not - it is all-ones on every ordinary configuration, so
    // it appears only when it is actually constraining something.
    const uint64_t all_ones = (d.sync_pat_len >= 64)
        ? ~0ULL : ((1ULL << d.sync_pat_len) - 1);

    if (d.sync_mask == all_ones)
    {
        return std::format("pattern 0x{:X} ({} bits)", d.sync_pat, d.sync_pat_len);
    }
    return std::format("pattern 0x{:X} ({} bits, mask 0x{:X})", d.sync_pat, d.sync_pat_len,
                       d.sync_mask);
}

bool FrameProcessor::reportCompletion(const ProcessingParams& params, const ScanDiagnostics& d)
{
    const std::string tag = streamTag(params);

    reportLog(tag + std::format("{} bytes processed, {} syncs found, {} frames extracted.",
                                d.total_bytes_processed, d.total_syncs_found,
                                d.total_frames_extracted));

    // Both failures report on ONE line. The operators of this application know their
    // own formats, so the log's job is to state what was configured and what came of
    // it - not to explain frame sync or to go looking for a setting that might work.
    // syncSpec() is kept because the pattern is the one field that distinguishes two
    // otherwise identical-looking configurations (PRN-11 and PRN-15 are both 32 bits).
    if (d.total_syncs_found == 0)
    {
        reportError(tag + std::format("no frame sync found — {}, {} bits/frame.",
                                      syncSpec(d), d.bits_in_frame));
        reportFinished(false);
        return false;
    }

    if (d.total_frames_extracted == 0)
    {
        reportError(tag + std::format("sync found but no frames extracted — {}, {} "
                                      "bits/frame; {} syncs, {} boundary-aligned.",
                                      syncSpec(d), d.bits_in_frame, d.total_syncs_found,
                                      d.boundary_syncs));
        reportFinished(false);
        return false;
    }

    reportLog(tag + std::format("complete — {} samples extracted, elapsed {:.1f}s.",
                                d.rows_written, d.elapsed_sec));

    reportFinished(true);
    return true;
}

void FrameProcessor::recordTimeSample(double current_time_sample,
                                      int n_samples,
                                      double lock_percentage,
                                      double accumulated_missed_frames,
                                      const std::vector<ParameterInfo*>& enabled_params)
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
    // sample_sum holds the running sum of RAW counts for this window; calibration
    // is applied here, once, to the windowed average — see the sampling loop for
    // why averaging precedes calibration (required for non-linear profiles).
    // n_samples can be 0 for a window that produced no extracted frames (a
    // recording gap, or an SNR window entirely out of lock); emit 0 in that case
    // rather than calibrating a meaningless zero average.
    for (int i = 0; i < intSize(enabled_params); i++)
    {
        ParameterInfo* param = enabled_params[i];
        double value = 0.0;
        if (n_samples > 0)
        {
            const double mean_raw = param->sample_sum / n_samples;
            value = param->profile.valid
                ? interpolateCalibration(mean_raw, param->profile)
                : (mean_raw + param->scale) * param->slope;
        }
        m_result.channels[i].values.push_back(value);
        param->sample_sum = 0;
    }
}
// End of file!
