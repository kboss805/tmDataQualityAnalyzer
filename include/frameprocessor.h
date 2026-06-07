/**
 * @file frameprocessor.h
 * @brief Self-contained PCM frame extraction and CSV output processor.
 *
 * The PCM encoding type (NRZ-L or RNRZ-L) is specified by the caller via
 * ProcessingParams::is_randomized; runtime auto-detection has been removed.
 */

#ifndef FRAMEPROCESSOR_H
#define FRAMEPROCESSOR_H

#include <atomic>

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QString>
#include <QVector>

#include "irig106ch10.h"
#include "i106_time.h"
#include "i106_decode_tmats.h"

#include "constants.h"
#include "processedstreamdata.h"
#include "processingparams.h"

class FrameSetup;
struct ParameterInfo;

/**
 * @brief Per-channel bookkeeping used by the irig106 C helper layer.
 *
 * Mirrors the irig106utils Hungarian-notation style.
 */
typedef struct SuChanInfo
{
    uint16_t                uChID;              ///< Channel identifier.
    int                     bEnabled;           ///< Non-zero if channel is enabled.
    Irig106::SuRDataSource* psuRDataSrc;        ///< Pointer to the TMATS R-record data source.
    void*                   psuAttributes;      ///< Pointer to decoded attributes (type varies).
} SuChanInfo;

/**
 * @brief Extracts PCM minor frames from a Chapter 10 file and writes CSV output.
 *
 * Created fresh per processing run, moved to a worker thread, and auto-deleted
 * when the thread finishes. Owns its own irig106 file handle and buffers.
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
     * @brief Extracts AGC samples from a Chapter 10 file into memory.
     *
     * Iterates through all packets, decoding PCM minor frames on the selected
     * channel and averaging samples at the requested rate. Results accumulate in
     * an in-memory ProcessedStreamData (see result()) rather than a CSV file.
     * Emits progressUpdated() periodically and processingFinished() on completion.
     *
     * @param[in] params      Validated processing parameters (file, channels, timing, mode, etc.).
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
    /// Emitted periodically during process() with completion percentage.
    void progressUpdated(int percent);
    /// Emitted when process() finishes; @p success is true on clean completion.
    void processingFinished(bool success);
    /// Emitted at key processing stages with a human-readable status message.
    void logMessage(const QString& message);
    /// Emitted when an error occurs during processing.
    void errorOccurred(const QString& message);

private:
    /// @brief Per-packet timing information for timestamp computation.
    struct PacketTimeRef {
        int64_t base_time;    ///< Packet header reference time (100ns units).
        uint64_t start_bit;   ///< Starting bit position in combined buffer.
        uint64_t num_bits;    ///< Number of data bits from this packet.
    };

    /// @name File I/O helpers
    /// @{
    bool openFile(const QString& filename);
    void closeFile() const;
    /// @}

    /// Grows m_buffer to at least @p required bytes. Returns false on allocation failure.
    bool ensureBufferCapacity(qsizetype required);

    /// @name irig106 C helper wrappers
    /// @{
    /**
     * @brief Deallocates the per-channel info table and associated attributes.
     * @param[in,out] channel_info Vector of SuChanInfo pointers to free.
     */
    static void freeChanInfoTable(QVector<SuChanInfo*>& channel_info);

    /**
     * @brief Builds per-channel attribute structures from TMATS metadata.
     * @param[in]     tmats_info    Parsed TMATS metadata.
     * @param[in,out] channel_info  Vector of SuChanInfo pointers to populate.
     * @return I106_OK on success.
     */
    static Irig106::EnI106Status assembleAttributesFromTMATS(
        Irig106::SuTmatsInfo* tmats_info,
        QVector<SuChanInfo*>& channel_info);
    /// @}

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
     * @brief Scans a bitstream for the first occurrence of a sync pattern.
     * @param[in] data         Raw byte buffer to scan.
     * @param[in] total_bits   Number of valid bits in the buffer.
     * @param[in] sync_pat     Expected sync pattern value.
     * @param[in] sync_mask    Bitmask for sync pattern comparison.
     * @param[in] sync_pat_len Sync pattern length in bits.
     * @return true if the pattern was found.
     */
    static bool hasSyncPattern(const uint8_t* data, uint64_t total_bits,
                               uint64_t sync_pat, uint64_t sync_mask,
                               uint32_t sync_pat_len);
    /// @}

    /**
     * @brief Appends one averaged time sample to the in-memory result.
     * @param[in]     current_time_sample Absolute IRIG seconds for this sample.
     * @param[in]     n_samples           Number of raw samples averaged.
     * @param[in]     lock_percentage     Frame sync lock percentage for this window (0–100).
     * @param[in]     enabled_params      Parameter definitions, index-aligned to m_result.channels.
     */
    void recordTimeSample(double current_time_sample,
                          int n_samples,
                          double lock_percentage,
                          const QVector<ParameterInfo*>& enabled_params);

    Irig106::EnI106Status m_status;                             ///< Last irig106 API return status.
    int m_file_handle;                                          ///< irig106 file handle.
    Irig106::SuI106Ch10Header m_header;                         ///< Reusable packet header buffer.
    QByteArray m_buffer;                                        ///< Packet data read buffer.
    Irig106::SuTmatsInfo m_tmats_info;                          ///< Parsed TMATS metadata.
    QVector<SuChanInfo*> m_channel_info;                        ///< Per-channel attribute table.
    Irig106::SuIrig106Time m_irig_time;                         ///< Reusable IRIG time struct.
    int64_t m_total_file_size;                                  ///< Input file size in bytes (for progress).
    std::atomic<bool> m_abort_requested;                         ///< Thread-safe abort flag.
    ProcessedStreamData m_result;                                ///< Accumulated in-memory output of the current run.
};

#endif // FRAMEPROCESSOR_H
