/**
 * @file frameprocessor.cpp
 * @brief Implementation of FrameProcessor — PCM frame extraction into memory.
 */

#include "frameprocessor.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <vector>

#include "irig106ch10.h"       // IWYU pragma: keep
#include "i106_time.h"         // IWYU pragma: keep
#include "i106_decode_tmats.h" // IWYU pragma: keep
#include "i106_decode_pcmf1.h"

#include "constants.h"
#include "containerutil.h"
#include "framesetup.h"
#include "packetqueue.h"

using namespace Irig106;

namespace {

/// Where a run of bits starting at @p from ends: the first bit after it whose time is past @p window_end (the bit that
/// would close the window), or @p limit when no bit before that is. The result is in (@p from, @p limit]. @p time_at
/// gives a bit's time. The caller has closed every window that ended before bit @p from, so that bit is no later than
/// the end.
///
/// The answer must be exactly the bit a per-bit check ("has this bit's time passed the end?") would stop at, because a
/// window's contents depend on which bit closes it. So it is searched for with that same check, never computed: time
/// does not decrease along a packet (a bit count times a period, plus a base), so the bits past the end are a tail, and
/// the first of them is found by doubling the step until one is past the end and then halving back. That costs a few
/// checks per window however long the window is, and a run of a few bits costs a few. Where time does not grow (a
/// period of zero or less, or a time that is not a number) no bit is past the end, and the run is the whole stretch, as
/// the per-bit check would have left it.
template <typename TimeAt>
uint64_t firstBitPastWindow(double window_end, uint64_t from, uint64_t limit, const TimeAt& time_at)
{
    uint64_t not_past = from;  // every bit up to and including this one is known not to be past the end
    uint64_t past     = limit; // the first bit known to be past the end, or the limit
    for (uint64_t step = 1; not_past + step < limit; step *= 2)
    {
        if (time_at(not_past + step) > window_end)
        {
            past = not_past + step;
            break;
        }
        not_past += step;
    }

    // The first bit past the end is in (not_past, past].
    uint64_t first = not_past + 1;
    while (first < past)
    {
        const uint64_t middle = first + (past - first) / 2;
        if (time_at(middle) > window_end)
        {
            past = middle;
        }
        else
        {
            first = middle + 1;
        }
    }
    return first;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////
//                          PCM BIT-LEVEL HELPERS                             //
////////////////////////////////////////////////////////////////////////////////

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
    m_report.setSink(std::move(events));
}

////////////////////////////////////////////////////////////////////////////////
//                          PROCESSING                                        //
////////////////////////////////////////////////////////////////////////////////

bool FrameProcessor::process(const ProcessingParams& params, const FrameSetup* frame_setup)
{
    const bool  is_randomized  = params.isRandomized;
    const bool  is_inverted    = params.isInverted;
    const bool  receiver_mode  = (params.mode == StreamMode::ReceiverChannelInfo);

    PacketQueue* queue = params.packetQueue;
    if (queue == nullptr)
    {
        m_report.error(streamTag(params) + "Internal error: no packet queue for stream.");
        m_report.finished(false);
        return false;
    }

    const ResolvedPcmAttrs& attrs = params.resolvedAttrs;
    if (!attrs.resolved)
    {
        m_report.error(streamTag(params) + "Internal error: PCM attributes not resolved.");
        m_report.finished(false);
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
    std::vector<const ParameterInfo*> enabled_params = buildEnabledParams(frame_setup, receiver_mode);

    // ---- Scan working state, seeded from the reader-resolved frame attributes ----
    ScanState state;
    state.channel_sums.assign(enabled_params.size(), 0.0);
    state.sync_pat        = attrs.syncPat;
    state.sync_mask       = attrs.syncMask;
    state.word_mask       = attrs.wordMask;
    state.sync_pat_len    = attrs.syncPatLen;
    state.bits_in_frame   = attrs.bitsInFrame;
    state.words_in_frame  = attrs.wordsInFrame;
    state.word_len        = attrs.wordLen;
    state.min_syncs       = attrs.minSyncs;
    // Output windowing clock. Seeded lazily from the first packet's actual IRIG
    // timestamp below (not from zero) so the clock doesn't have to "catch up"
    // sample-by-sample from the IRIG day epoch to wherever the data actually starts.
    state.sample_period       = params.samplePeriodSec;
    state.current_time_sample = 0.0;
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
    bool     time_seeded           = false;

    // Bits consumed so far: the data-rate clock's time base.
    uint64_t global_bit_offset = 0;

    // This worker's private, mutable copy of the packet in flight. Queue payloads are shared
    // with every other stream on the channel and are const, but swap / invert / derandomize
    // modify in place. Reused across packets, so it settles at the largest packet and stops
    // allocating.
    std::vector<uint8_t> packet_bytes;

    // Derandomization state carried across packets.
    bool needs_derand = is_randomized;
    uint16_t lfsr_state = 0;

    m_report.log(streamTag(params) + "processing...");

    // -----------------------------------------------------------------------
    // Consume packets from the queue until the end-of-stream sentinel.
    // -----------------------------------------------------------------------
    while (true)
    {
        if (m_abort_requested.load(std::memory_order_relaxed))
        {
            m_report.log(streamTag(params) + "processing cancelled by user.");
            m_report.finished(false);
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

        if (!time_seeded && !use_rate_clock)
        {
            // A time before the epoch, which a valid recording cannot carry, seeds at zero.
            double seed = item.baseAbsSeconds > 0.0 ? item.baseAbsSeconds : 0.0;
            state.current_time_sample = seed;
            state.next_time_sample = seed + state.sample_period;
            time_seeded = true;
        }

        const double time_per_bit = delta_100ns * PCMConstants::k100NsToSeconds;
        // The time of bit k of this packet, on whichever clock the run uses.
        const auto time_at = [&](uint64_t k) {
            return use_rate_clock
                ? static_cast<double>(global_bit_offset + k) * time_per_bit
                : item.baseAbsSeconds + (static_cast<double>(k) * time_per_bit);
        };

        // The packet is scanned in runs of bits that cannot close a window, so the per-bit work is the state machine
        // alone. A window closes when a bit's time passes its end, and time only grows along a packet, so the first
        // bit that does is found once per window instead of asked of every bit.
        constexpr uint64_t kAbortCheckMask = 0xFFFF;
        uint64_t bit_pos = 0;
        while (bit_pos < packet_bits)
        {
            if ((bit_pos & kAbortCheckMask) == 0 && m_abort_requested.load(std::memory_order_relaxed))
            {
                m_report.log(streamTag(params) + "processing cancelled by user.");
                m_report.finished(false);
                return false;
            }

            // Time only delimits the averaging windows: every bit of the recording is counted.
            closeElapsedWindows(state, time_at(bit_pos), params, enabled_params);

            // A run ends at the next abort check, the packet's end, or the first bit past this window's end.
            const uint64_t limit = std::min(packet_bits, (bit_pos | kAbortCheckMask) + 1);
            const uint64_t run_end = firstBitPastWindow(state.next_time_sample, bit_pos, limit, time_at);
            scanBits(state, raw_data, bit_pos, run_end, enabled_params);
            bit_pos = run_end;
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
            m_report.log(streamTag(params)
                         + std::format("all {} payloads are odd-length and cannot be "
                                       "byte-pair swapped — processed as Legacy Chapter 10 "
                                       "regardless of the setting.",
                                       swap_skipped_packets));
        }
        else
        {
            // The genuinely corrupting case: one stream, two byte orders.
            m_report.log(streamTag(params)
                         + std::format("warning: byte swap skipped on {} of {} packets "
                                       "(odd length) — mixed byte order, sync may be unreliable.",
                                       swap_skipped_packets, total_packets));
        }
    }

    // Flush the last set of accumulated samples.
    if (state.n_samples > 0)
    {
        recordTimeSample(state.current_time_sample, state.n_samples, state.lockPercent(),
                         static_cast<double>(state.accumulated_missed_frames), enabled_params, state.channel_sums);
        state.rows_written++;
    }

    // duration_cast truncates to whole milliseconds; the completion log shows the result
    // as seconds to one decimal.
    const int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - run_start).count();
    constexpr double kMsPerSec = 1000.0;

    ScanDiagnostics diag;
    diag.total_bytes_processed  = total_bytes_processed;
    diag.total_syncs_found      = state.total_syncs_found;
    diag.total_frames_extracted = state.total_frames_extracted;
    diag.boundary_syncs         = state.boundary_syncs;
    diag.rows_written           = state.rows_written;
    diag.bits_in_frame          = state.bits_in_frame;
    diag.sync_pat_len           = state.sync_pat_len;
    diag.sync_pat               = state.sync_pat;
    diag.sync_mask              = state.sync_mask;
    diag.elapsed_sec            = static_cast<double>(elapsed_ms) / kMsPerSec;
    return reportCompletion(params, diag);
}

void FrameProcessor::closeElapsedWindows(ScanState& s, double current_time_eval,
                                         const ProcessingParams& params,
                                         const std::vector<const ParameterInfo*>& enabled_params)
{
    while (s.next_time_sample < current_time_eval)
    {
        if (s.total_bits_in_window > 0)
        {
            // Bits were processed for this window — real data, whether or not any
            // frame locked. If the window was entirely out of lock (n_samples == 0)
            // its missed frames were ALREADY counted bit-by-bit by the in-stream
            // rollover in scanBits(), so we must NOT extrapolate here. Extrapolating
            // data-present dropouts double-counts and biases long-frame streams,
            // whose windows more often contain zero locked frames than short-frame
            // streams with many more frames per window.
            recordTimeSample(s.current_time_sample, s.n_samples, s.lockPercent(),
                             static_cast<double>(s.accumulated_missed_frames), enabled_params, s.channel_sums);
        }
        else
        {
            // Genuine recording gap: no packets at all spanned this window, so the
            // in-stream counter never ran. Extrapolate the frames that would have
            // been received from the expected data rate.
            double bit_rate = params.dataRateBps;
            if (bit_rate <= 0.0 && params.resolvedAttrs.delta100ns > 0.0)
            {
                bit_rate = PCMConstants::k100NsPerSecond / params.resolvedAttrs.delta100ns;
            }

            if (bit_rate > 0.0)
            {
                double expected_bits = s.sample_period * bit_rate;
                uint64_t extrapolated_frames = static_cast<uint64_t>(expected_bits / s.bits_in_frame);
                s.accumulated_missed_frames += extrapolated_frames;
                s.sync_count = UINT64_MAX; // Ensure lock is dropped during gap
            }

            recordTimeSample(s.current_time_sample, 0, 0.0, // lock_pct is 0.0 during gap
                             static_cast<double>(s.accumulated_missed_frames), enabled_params, s.channel_sums);
        }

        s.rows_written++;
        s.n_samples = 0;
        s.valid_bits_in_window = 0;
        s.total_bits_in_window = 0;

        s.current_time_sample += s.sample_period;
        s.next_time_sample += s.sample_period;
    }
}

void FrameProcessor::scanBits(ScanState& s, const uint8_t* data, uint64_t from, uint64_t to,
                              const std::vector<const ParameterInfo*>& enabled_params)
{
    // The state machine runs on copies of its registers, written back once at the end. Through @p s every register is
    // a memory location the compiler must assume the frame-word store below can change, so each bit would load and
    // store them all; as locals they stay in CPU registers, which is most of what this costs.
    const uint64_t sync_pat      = s.sync_pat;
    const uint64_t sync_mask     = s.sync_mask;
    const uint64_t word_mask     = s.word_mask;
    const uint64_t sync_pat_len  = s.sync_pat_len;
    const uint32_t bits_in_frame = s.bits_in_frame;
    const uint32_t words_in_frame = s.words_in_frame;
    const uint32_t word_len      = s.word_len;
    const uint32_t min_syncs     = s.min_syncs;
    uint64_t* const frame_words  = s.frame_words.data();

    uint64_t test_word                = s.test_word;
    uint64_t bits_loaded              = s.bits_loaded;
    uint32_t minor_frame_bit_count    = s.minor_frame_bit_count;
    uint32_t minor_frame_word_count   = s.minor_frame_word_count;
    uint32_t data_word_bit_count      = s.data_word_bit_count;
    int32_t  save_data                = s.save_data;
    uint64_t sync_count               = s.sync_count;
    uint64_t accumulated_missed       = s.accumulated_missed_frames;
    uint64_t valid_bits               = s.valid_bits_in_window;

    for (uint64_t pos = from; pos < to; pos++)
    {
        constexpr unsigned kLastBitOfByte = 7;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const uint64_t bit_val = (data[pos >> 3] >> (kLastBitOfByte - (pos & kLastBitOfByte))) & 1u;

        test_word = (test_word << 1) | bit_val;
        bits_loaded++;
        minor_frame_bit_count++;

        if (minor_frame_bit_count > bits_in_frame)
        {
            accumulated_missed++;
            sync_count = UINT64_MAX; // Loss of lock
            minor_frame_bit_count = 1; // Roll over to continuously track missed frames
        }

        const bool in_lock = (sync_count != UINT64_MAX) && (sync_count >= min_syncs);

        // Check for sync word
        const bool sync_match = bits_loaded >= sync_pat_len && (test_word & sync_mask) == sync_pat;
        if (sync_match)
        {
            s.total_syncs_found++;
        }

        // In LOCK state, ignore false positives (off-phase matches). Only process
        // sync matches at the exact expected frame boundary so that PRN data patterns
        // cannot disrupt word collection.
        const bool at_boundary = (minor_frame_bit_count == bits_in_frame);
        if (sync_match && (!in_lock || at_boundary))
        {
            if (at_boundary)
            {
                sync_count++;
                s.boundary_syncs++;
            }
            else
            {
                // Off-phase acquisition: a sync match that is NOT on a frame boundary.
                // boundary_syncs is deliberately not incremented here - it used to be,
                // which made the failure diagnostic report an off-phase match as
                // "boundary-aligned" and told operators the opposite of the truth.
                sync_count = 1;
            }

            // Only extract a sample once the sync match is boundary-aligned; an
            // off-phase match (re-acquiring lock) must not be treated as a confirmed
            // frame, or PRN data can be sampled as if it were locked.
            if (at_boundary && sync_count >= min_syncs && save_data > 1)
            {
                for (std::size_t i = 0; i < enabled_params.size(); i++)
                {
                    const ParameterInfo* param = enabled_params[i];
                    if (param->word >= 0 &&
                        param->word < static_cast<int>(words_in_frame))
                    {
                        int64_t raw_value = static_cast<int64_t>(frame_words[param->word] & word_mask);
                        // Accumulate RAW counts; calibration (linear or non-linear)
                        // is applied once to the windowed average in
                        // recordTimeSample(). Averaging raw first is required for
                        // the non-linear profile: the profile maps averaged-raw ->
                        // true dB, so mean(interpolate(raw)) would bias each plateau
                        // off the true step value. For linear cal the two orders are
                        // identical.
                        s.channel_sums[i] += static_cast<double>(raw_value);
                    }
                }

                s.n_samples++;
                s.total_frames_extracted++;
            }

            minor_frame_bit_count = 0;
            minor_frame_word_count = 1;
            data_word_bit_count = 0;
            save_data = 1;
        }
        else
        {
            // Accumulate data word bits between sync patterns. A match ignored in lock is one of them: it is data that
            // happens to look like the sync, and its bit belongs to the word being read. Leaving it out (as this did) read
            // every later word of the frame one bit late.
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

        if (in_lock) {
            valid_bits++;
        }
    }

    s.test_word                 = test_word;
    s.bits_loaded               = bits_loaded;
    s.minor_frame_bit_count     = minor_frame_bit_count;
    s.minor_frame_word_count    = minor_frame_word_count;
    s.data_word_bit_count       = data_word_bit_count;
    s.save_data                 = save_data;
    s.sync_count                = sync_count;
    s.accumulated_missed_frames = accumulated_missed;
    s.valid_bits_in_window      = valid_bits;
    s.total_bits_in_window     += to - from;
}

std::vector<const ParameterInfo*> FrameProcessor::buildEnabledParams(const FrameSetup* frame_setup, bool receiver_mode)
{
    std::vector<const ParameterInfo*> enabled_params;
    if (receiver_mode && frame_setup != nullptr)
    {
        enabled_params.reserve(frame_setup->length());
        for (int i = 0; i < frame_setup->length(); i++)
        {
            enabled_params.push_back(frame_setup->getParameter(i));
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
    // by. The label is appended only when it adds something - buildStreamJob falls
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
    if (d.sync_mask == fullSyncMask(static_cast<int>(d.sync_pat_len)))
    {
        return std::format("pattern 0x{:X} ({} bits)", d.sync_pat, d.sync_pat_len);
    }
    return std::format("pattern 0x{:X} ({} bits, mask 0x{:X})", d.sync_pat, d.sync_pat_len,
                       d.sync_mask);
}

bool FrameProcessor::reportCompletion(const ProcessingParams& params, const ScanDiagnostics& d)
{
    const std::string tag = streamTag(params);

    m_report.log(tag + std::format("{} bytes processed, {} syncs found, {} frames extracted.",
                                   d.total_bytes_processed, d.total_syncs_found,
                                   d.total_frames_extracted));

    // Both failures report on ONE line. The operators of this application know their
    // own formats, so the log's job is to state what was configured and what came of
    // it - not to explain frame sync or to go looking for a setting that might work.
    // syncSpec() is kept because the pattern is the one field that distinguishes two
    // otherwise identical-looking configurations (PRN-11 and PRN-15 are both 32 bits).
    if (d.total_syncs_found == 0)
    {
        m_report.error(tag + std::format("no frame sync found — {}, {} bits/frame.",
                                         syncSpec(d), d.bits_in_frame));
        m_report.finished(false);
        return false;
    }

    if (d.total_frames_extracted == 0)
    {
        m_report.error(tag + std::format("sync found but no frames extracted — {}, {} "
                                         "bits/frame; {} syncs, {} boundary-aligned.",
                                         syncSpec(d), d.bits_in_frame, d.total_syncs_found,
                                         d.boundary_syncs));
        m_report.finished(false);
        return false;
    }

    m_report.log(tag + std::format("complete — {} samples extracted, elapsed {:.1f}s.",
                                   d.rows_written, d.elapsed_sec));

    m_report.finished(true);
    return true;
}

void FrameProcessor::recordTimeSample(double current_time_sample,
                                      int n_samples,
                                      double lock_percentage,
                                      double accumulated_missed_frames,
                                      const std::vector<const ParameterInfo*>& enabled_params,
                                      std::vector<double>& channel_sums)
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

    // enabled_params is index-aligned to m_result.channels (built by buildEnabledParams()).
    // channel_sums holds the running sum of RAW counts for this window; calibration
    // is applied here, once, to the windowed average — see the sampling loop for
    // why averaging precedes calibration (required for non-linear profiles).
    // n_samples can be 0 for a window that produced no extracted frames (a
    // recording gap, or an SNR window entirely out of lock); emit 0 in that case
    // rather than calibrating a meaningless zero average.
    for (int i = 0; i < intSize(enabled_params); i++)
    {
        const ParameterInfo* param = enabled_params[i];
        double value = 0.0;
        if (n_samples > 0)
        {
            const double mean_raw = channel_sums[i] / n_samples;
            value = param->profile.valid
                ? interpolateCalibration(mean_raw, param->profile)
                : (mean_raw + param->scale) * param->slope;
        }
        m_result.channels[i].values.push_back(value);
        channel_sums[i] = 0;
    }
}
