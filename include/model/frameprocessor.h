/**
 * @file frameprocessor.h
 * @brief PCM frame-sync extraction worker.
 *
 * Consumes decoded PCM packets from a per-stream PacketQueue (filled by
 * Ch10PacketReader) and accumulates frame-sync lock / receiver-channel results
 * in memory. The PCM encoding type (NRZ-L or RNRZ-L) is specified by the caller
 * via ProcessingParams::is_randomized. The processor no longer opens the Ch10
 * file or parses TMATS — that is done once by the reader.
 */

#ifndef FRAMEPROCESSOR_H
#define FRAMEPROCESSOR_H

#include <atomic>
#include <cstdint>

#include <QObject>
#include <QString>
#include <QVector>

#include "processedstreamdata.h"
#include "processingparams.h"

class FrameSetup;
struct ParameterInfo;

/**
 * @brief Extracts PCM minor frames from a queue of decoded packets.
 *
 * Created fresh per processing run, moved to a worker thread, and auto-deleted
 * when the thread finishes. Pulls PacketItems from params.packet_queue until the
 * end-of-stream sentinel.
 */
class FrameProcessor : public QObject
{
    Q_OBJECT
    friend class TestFrameProcessor;

public:
    explicit FrameProcessor(QObject* parent = nullptr);
    ~FrameProcessor();

    FrameProcessor(const FrameProcessor&) = delete;
    FrameProcessor& operator=(const FrameProcessor&) = delete;
    FrameProcessor(FrameProcessor&&) = delete;
    FrameProcessor& operator=(FrameProcessor&&) = delete;

    /**
     * @brief Processes the stream's packets into an in-memory result.
     *
     * Reads PacketItems from @c params.packet_queue, derandomizes/byte-swaps as
     * configured, scans for frame sync, and averages samples at the requested
     * rate. Results accumulate in an in-memory ProcessedStreamData (see result()).
     * Emits processingFinished() on completion.
     *
     * @param[in] params      Validated parameters, including resolved_attrs and packet_queue.
     * @param[in] frame_setup Frame parameter definitions (word map, calibration). May be
     *                        empty/null for FrameSyncLockStats mode.
     * @return true if processing completed without errors.
     */
    bool process(const ProcessingParams& params, FrameSetup* frame_setup);

    /// @return The accumulated in-memory output of the last process() call.
    const ProcessedStreamData& result() const { return m_result; }
    /// Moves the accumulated output out of the processor (clears the internal buffer).
    ProcessedStreamData takeResult() { return std::move(m_result); }

    /// Requests a cooperative abort of the current processing run.
    void requestAbort();

signals:
    /// Emitted when process() finishes; @p success is true on clean completion.
    void processingFinished(bool success);
    /// Emitted at key processing stages with a human-readable status message.
    void logMessage(const QString& message);
    /// Emitted when an error occurs during processing.
    void errorOccurred(const QString& message);

private:
    /// @brief Per-packet timing information for timestamp computation.
    struct PacketTimeRef {
        double   base_abs_seconds; ///< Absolute IRIG seconds at this packet's base time.
        uint64_t start_bit;        ///< Starting bit position in the continuous stream.
        uint64_t num_bits;         ///< Number of data bits from this packet.
    };

    /// @brief Run totals/diagnostics collected during a scan, consumed once at the
    /// end to emit the success log or the "no syncs / no frames" error report.
    struct ScanDiagnostics {
        uint64_t total_bytes_processed  = 0;
        uint64_t total_syncs_found      = 0;
        uint64_t total_frames_extracted = 0;
        uint64_t boundary_syncs         = 0;
        uint64_t max_sync_run           = 0;
        bool     buffer_ever_filled     = false;
        double   first_data_time        = -1.0;
        double   last_data_time         = 0.0;
        uint64_t rows_written           = 0;
        uint32_t bits_in_frame          = 0;
        uint32_t words_in_frame         = 0;
        uint32_t sync_pat_len           = 0;
        uint32_t min_syncs              = 0;
        // The pattern actually scanned for. Reported because sync_pat_len alone
        // cannot distinguish two configurations: PRN-11 and PRN-15 are both 32-bit
        // patterns, so a stream configured with the wrong one looks identical in the
        // failure report unless the value itself is printed.
        uint64_t sync_pat               = 0;
        uint64_t sync_mask              = 0;
        double   elapsed_sec            = 0.0;
    };

    /// @brief Mutable working state of one frame-sync scan, threaded through the
    /// per-bit helpers. Cached frame geometry + window bounds are set once at the
    /// start of process(); the rest are the sliding sync/word registers, output
    /// windowing, lock-percentage counters, and run totals.
    struct ScanState {
        // Frame geometry (cached from ResolvedPcmAttrs; read-only during the scan).
        uint64_t sync_pat = 0, sync_mask = 0, word_mask = 0;
        uint32_t sync_pat_len = 0, bits_in_frame = 0, words_in_frame = 0, word_len = 0, min_syncs = 0;
        // Processing window bounds + output cadence (read-only during the scan).
        double start_seconds = 0.0, stop_seconds = 0.0, sample_period = 0.0;
        // Sliding sync/word registers.
        uint64_t test_word = 0, bits_loaded = 0;
        uint32_t minor_frame_bit_count = 0, minor_frame_word_count = 0, data_word_bit_count = 0;
        int32_t  save_data = 0;            ///< 0=waiting, 1=collecting, 2=frame complete.
        uint64_t sync_count = UINT64_MAX;  ///< No in-phase sync confirmed yet.
        QVector<uint64_t> frame_words;     ///< Words of the minor frame being collected.
        // Output windowing.
        double current_time_sample = 0.0, next_time_sample = 0.0;
        int    n_samples = 0;
        // Lock-percentage counters (reset each output window).
        uint64_t valid_bits_in_window = 0, total_bits_in_window = 0;
        uint64_t accumulated_missed_frames = 0; ///< Monotonic across the run.
        // Run totals / diagnostics.
        uint64_t total_syncs_found = 0, total_frames_extracted = 0, rows_written = 0;
        uint64_t boundary_syncs = 0, max_sync_run = 0, current_sync_run = 0;
        bool     buffer_ever_filled = false;
    };

    /// Closes every output window whose end @p current_time_eval has passed, emitting
    /// one averaged sample per window (or an extrapolated missed-frame count for a
    /// genuine recording gap) and advancing the windowing clock in @p s.
    void closeElapsedWindows(ScanState& s, double current_time_eval,
                             const ProcessingParams& params,
                             const QVector<ParameterInfo*>& enabled_params);

    /// Feeds one decoded PCM bit through the acquire/lock state machine: shifts the
    /// sync register, tracks frame-boundary rollover/missed frames, honors in-phase
    /// sync matches (rejecting off-phase ones while locked), collects data words, and
    /// tallies lock-percentage bits. @p in_window gates window-bounded accounting.
    void scanBit(ScanState& s, uint8_t bit_val, bool in_window,
                 const QVector<ParameterInfo*>& enabled_params);

    /// Caches the enabled parameters for the hot loop and builds the index-aligned
    /// in-memory channel series in m_result. @return the enabled-parameter pointers.
    QVector<ParameterInfo*> buildEnabledParams(FrameSetup* frame_setup, bool receiver_mode);

    /// Emits the completion log (or the no-syncs / no-frames error) for a finished
    /// scan and signals processingFinished(). @return true on a successful run.
    /// Identity prefix for every message this worker logs, e.g. "[CH 13 PCM03] ".
    /// Parallel workers share one log, so an unlabelled message cannot be traced
    /// back to the stream that produced it.
    static QString streamTag(const ProcessingParams& params);

    /// Human-readable description of the sync pattern actually being scanned for.
    static QString syncSpec(const ScanDiagnostics& d);

    bool reportCompletion(const ProcessingParams& params, const ScanDiagnostics& diag);

    /// @name PCM bit-level helpers
    /// @{
    /**
     * @brief Applies IRIG 106 Appendix D self-synchronizing descrambler.
     * @param[in,out] data       Raw byte buffer to derandomize in-place.
     * @param[in]     total_bits Number of valid bits in the buffer.
     * @param[in,out] lfsr       15-bit LFSR state carried across packets.
     */
    static void derandomizeBitstream(uint8_t* data, uint64_t total_bits, uint16_t& lfsr);
    /**
     * @brief Inverts every bit in the buffer (bitwise NOT on each byte).
     * @param[in,out] data   Raw byte buffer to invert in-place.
     * @param[in]     length Number of bytes in the buffer.
     */
    static void invertBits(uint8_t* data, uint32_t length);
    /// @}

    /**
     * @brief Appends one averaged time sample to the in-memory result.
     */
    void recordTimeSample(double current_time_sample,
                          int n_samples,
                          double lock_percentage,
                          double accumulated_missed_frames,
                          const QVector<ParameterInfo*>& enabled_params);

    std::atomic<bool> m_abort_requested; ///< Thread-safe abort flag.
    ProcessedStreamData m_result;        ///< Accumulated in-memory output of the current run.
};

#endif // FRAMEPROCESSOR_H
