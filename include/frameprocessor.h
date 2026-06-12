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

    /// @name PCM bit-level helpers
    /// @{
    /**
     * @brief Applies IRIG 106 Appendix D self-synchronizing descrambler.
     * @param[in,out] data       Raw byte buffer to derandomize in-place.
     * @param[in]     total_bits Number of valid bits in the buffer.
     * @param[in,out] lfsr       15-bit LFSR state carried across packets.
     */
    static void derandomizeBitstream(uint8_t* data, uint64_t total_bits, uint16_t& lfsr);
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
