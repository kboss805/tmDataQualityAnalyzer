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
        emit errorOccurred(streamTag(params) + "Internal error: no packet queue for stream.");
        emit processingFinished(false);
        return false;
    }

    const ResolvedPcmAttrs& attrs = params.resolvedAttrs;
    if (!attrs.resolved)
    {
        emit errorOccurred(streamTag(params) + "Internal error: PCM attributes not resolved.");
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

    // Cache enabled parameters for the hot loop and build the index-aligned
    // in-memory channel series (see buildEnabledParams).
    QVector<ParameterInfo*> enabled_params = buildEnabledParams(frame_setup, receiver_mode);
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
    state.frame_words = QVector<uint64_t>(state.words_in_frame, 0);

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

    // Derandomization state carried across packets.
    bool needs_derand = is_randomized;
    uint16_t lfsr_state = 0;

    emit logMessage(streamTag(params) + QString("processing (window: start=%1s stop=%2s)...")
                    .arg(start_seconds).arg(stop_seconds));

    // -----------------------------------------------------------------------
    // Consume packets from the queue until the end-of-stream sentinel.
    // -----------------------------------------------------------------------
    while (true)
    {
        if (m_abort_requested.load(std::memory_order_relaxed))
        {
            emit logMessage(streamTag(params) + "processing cancelled by user.");
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
                emit logMessage(streamTag(params) + "processing cancelled by user.");
                emit processingFinished(false);
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
            emit logMessage(
                streamTag(params) + QString("all %1 packets have an odd payload length, which "
                        "cannot be byte-pair swapped, so this stream was processed as Legacy "
                        "Chapter 10 no matter how that setting is set. It is the only stream "
                        "shape the setting cannot reach.")
                    .arg(swap_skipped_packets));
        }
        else
        {
            // The genuinely corrupting case: one stream, two byte orders.
            emit logMessage(
                streamTag(params) + QString("warning: byte swap skipped on %1 of %2 packets (odd payload "
                        "length). Those packets were left unswapped while the rest were "
                        "swapped, so this stream carries two different byte orders and "
                        "frame sync may be unreliable.")
                    .arg(swap_skipped_packets)
                    .arg(total_packets));
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

    qint64 elapsed_ms = elapsed_timer.elapsed();
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
                                         const QVector<ParameterInfo*>& enabled_params)
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
                             const QVector<ParameterInfo*>& enabled_params)
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

QVector<ParameterInfo*> FrameProcessor::buildEnabledParams(FrameSetup* frame_setup, bool receiver_mode)
{
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
    return enabled_params;
}

QString FrameProcessor::streamTag(const ProcessingParams& params)
{
    // Every message a worker emits carries this. Streams are processed by parallel
    // workers writing into one shared log, so their output interleaves in an order
    // that is not even deterministic between runs; without an identity, four
    // failure reports are indistinguishable from each other.
    //
    // The channel ID leads because that is what the operator configured the stream
    // by. The label is appended only when it adds something - MainViewModel falls
    // back to "Ch <id>" when TMATS gives no name, and "[CH 13 Ch 13]" is noise.
    const QString fallback = QStringLiteral("Ch ") + QString::number(params.pcmChannelId);
    if (params.streamLabel.isEmpty() || params.streamLabel == fallback)
        return QStringLiteral("[CH %1] ").arg(params.pcmChannelId);
    return QStringLiteral("[CH %1 %2] ").arg(params.pcmChannelId).arg(params.streamLabel);
}

QString FrameProcessor::syncSpec(const ScanDiagnostics& d)
{
    // Pattern first, because it is the field that distinguishes configurations the
    // other numbers cannot: PRN-11 and PRN-15 are both 32 bits, so a stream running
    // the wrong one is invisible in a report that prints only the length.
    QString s = QStringLiteral("pattern=0x%1  (%2 bits)")
                    .arg(QString::number(d.sync_pat, 16).toUpper())
                    .arg(d.sync_pat_len);

    // An all-ones mask is the default and says nothing; anything else is a real
    // constraint the operator should see, because it silently excuses mismatches.
    const uint64_t all_ones = (d.sync_pat_len >= 64)
        ? ~0ULL : ((1ULL << d.sync_pat_len) - 1);
    if (d.sync_mask != all_ones)
    {
        s += QStringLiteral("  mask=0x%1")
                 .arg(QString::number(d.sync_mask, 16).toUpper());
    }
    else
    {
        s += QStringLiteral("  mask=all bits significant");
    }
    return s;
}

bool FrameProcessor::reportCompletion(const ProcessingParams& params, const ScanDiagnostics& d)
{
    const QString tag = streamTag(params);

    emit logMessage(tag + QString::number(d.total_bytes_processed) + " bytes processed, "
                    + QString::number(d.total_syncs_found) + " syncs found, "
                    + QString::number(d.total_frames_extracted) + " frames extracted.");

    if (d.total_syncs_found == 0)
    {
        emit errorOccurred(tag + "Frame sync pattern was not found in the data stream.\n"
                           "  Looking for:     " + syncSpec(d) + "\n"
                           "  Verify the frame sync pattern, bits/frame and PCM channel are correct.");
        emit processingFinished(false);
        return false;
    }

    if (d.total_frames_extracted == 0)
    {
        // Distinguish the two very different shapes of this failure. A handful of
        // matches with none on a frame boundary is not a marginal lock - it is the
        // pattern occurring by chance, which a 32-bit pattern does roughly once per
        // 4 Gbit. Saying so is the difference between "tune something" and "this
        // stream is not what you configured".
        const QString hint = (d.boundary_syncs == 0)
            ? QStringLiteral(
                  "\n  No sync ever landed on a frame boundary. The pattern matched "
                  "somewhere, but never at the\n  expected frame spacing - so the sync "
                  "pattern, bits/frame, or the data's byte order is\n  wrong for this "
                  "stream. Chance alone produces about one 32-bit match per 4 Gbit.")
            : QString();

        emit errorOccurred(
            tag +
            QString("Frame sync pattern was found but no valid frames were extracted.\n"
                    "  Looking for:     %1\n"
                    "  Config:          bits/frame=%2  words/frame=%3  min_syncs=%4\n"
                    "  Syncs:           %5 total  |  %6 boundary-aligned  |  longest run=%7 (need %8)\n"
                    "  Frame buffer:    %9\n"
                    "  Data time range: %10s – %11s  (window: %12s – %13s)")
            .arg(syncSpec(d))
            .arg(d.bits_in_frame).arg(d.words_in_frame).arg(d.min_syncs)
            .arg(d.total_syncs_found).arg(d.boundary_syncs).arg(d.max_sync_run).arg(d.min_syncs)
            .arg(d.buffer_ever_filled ? "filled at least once (save_data reached 2)"
                                      : "NEVER filled — words_in_frame may be too large")
            .arg(d.first_data_time, 0, 'f', 3).arg(d.last_data_time, 0, 'f', 3)
            .arg(params.startSeconds).arg(params.stopSeconds)
            + hint);
        emit processingFinished(false);
        return false;
    }

    emit logMessage(tag + QString("complete — %1 samples extracted, elapsed %2s.")
        .arg(d.rows_written)
        .arg(d.elapsed_sec, 0, 'f', 1));

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
    // sample_sum holds the running sum of RAW counts for this window; calibration
    // is applied here, once, to the windowed average — see the sampling loop for
    // why averaging precedes calibration (required for non-linear profiles).
    // n_samples can be 0 for a window that produced no extracted frames (a
    // recording gap, or an SNR window entirely out of lock); emit 0 in that case
    // rather than calibrating a meaningless zero average.
    for (int i = 0; i < enabled_params.size(); i++)
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
