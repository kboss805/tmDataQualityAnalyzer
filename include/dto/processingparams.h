/**
 * @file processingparams.h
 * @brief Validated parameters bundle for a single AGC processing run.
 */

#ifndef PROCESSINGPARAMS_H
#define PROCESSINGPARAMS_H

#include <cstdint>
#include <QString>

#include "streamconfig.h"

class PacketQueue;

/**
 * @brief PCM frame attributes resolved from TMATS + user overrides by the reader.
 *
 * In the single-reader architecture the Ch10 reader parses TMATS once and calls
 * Set_Attributes_Ext_PcmF1 per stream, then copies the resolved scalar fields
 * here so the worker never needs the file handle or the irig106 attribute struct.
 */
struct ResolvedPcmAttrs {
    uint64_t syncPat       = 0;     ///< Resolved minor-frame sync pattern.
    uint64_t syncMask      = 0;     ///< Resolved minor-frame sync mask.
    uint32_t syncPatLen    = 0;     ///< Sync pattern length in bits.
    uint32_t bitsInFrame   = 0;     ///< Bits per minor frame.
    uint32_t wordsInFrame  = 0;     ///< Words per minor frame.
    uint32_t wordLen       = 0;     ///< Common word length in bits.
    uint64_t wordMask      = 0;     ///< Common word mask.
    double   delta100ns    = 0.0;   ///< Bit period in 100 ns units (TMATS or user override).
    uint32_t minSyncs      = 0;     ///< Minimum consecutive syncs before frames are saved.
    bool     needsSwap     = true;  ///< True if raw PCM data must be byte-swapped.
                                    ///< Set by the reader from ProcessingParams::swapBytes;
                                    ///< NOT from the irig106 attributes, which never carry it
                                    ///< (see Ch10PacketReader where this is assigned).
    bool     resolved      = false; ///< True once the reader has populated this struct.
};

/// @brief Validated parameters bundle passed to the worker thread.
struct ProcessingParams {
    QString filename;              ///< Path to the .ch10 input file.
    int sourceId = 0;             ///< Id of the .ch10 file (multi-file session) this job belongs to; 0 for the first/only source. Carried into the completed ProcessedStreamData by ProcessingCoordinator (mirrors jobIndex).
    int timeChannelId = -1;       ///< Resolved time channel ID.
    int pcmChannelId = -1;        ///< Resolved PCM channel ID.
    uint64_t frameSync = 0;       ///< Frame sync pattern as a numeric value.
    uint64_t frameSyncMask = 0;   ///< Frame sync mask. 0 = derive all-ones from syncPatternLength.
    int syncPatternLength = 0;    ///< Sync pattern length in bits.
    int wordsInMinorFrame = 0;    ///< Words per PCM minor frame (data words + 1).
    int bitsInMinorFrame = 0;     ///< Total bits per PCM minor frame.
    uint64_t startSeconds = 0;    ///< Start of extraction window (IRIG seconds).
    uint64_t stopSeconds = 0;     ///< End of extraction window (IRIG seconds).
    double samplePeriodSec = 0.1; ///< Output sample period in seconds (default 100 ms).
    bool isRandomized = false;    ///< True if RNRZ-L encoding (user-specified).
    bool isInverted   = false;    ///< True if raw data should be bit-inverted before any other processing.
    /// True if each packet's payload should have its byte pairs swapped before any
    /// other transform. **File-scoped, not per-stream**: byte order is a property of
    /// the recorder that produced the file, so every stream in one recording shares
    /// it. MainViewModel stamps the same value onto every job it builds.
    ///
    /// Defaults true, which is what the application did unconditionally before this
    /// was settable, so existing recordings and templates are unaffected.
    bool swapBytes    = true;

    StreamMode mode = StreamMode::ReceiverChannelInfo; ///< Processing mode for this stream.
    double dataRateBps = 0.0;     ///< User bit rate in bits/sec. 0 = use TMATS-derived rate.
    QString streamLabel;          ///< Display label of the source stream (for plot/logs).

    /// Derive the sample-windowing clock from bits processed (data rate) starting
    /// at zero, instead of from IRIG absolute time. Calibration extraction sets
    /// this: it only needs steps measured over elapsed stream time, and a cal
    /// file's IRIG time may be large/absent/non-monotonic (it is a different file
    /// than the one the time channel was selected for), which would otherwise
    /// derail the per-period windowing. The plot path leaves this false because it
    /// genuinely needs wall/IRIG time for the x-axis and cross-stream alignment.
    bool useDataRateClock = false;

    ResolvedPcmAttrs resolvedAttrs; ///< Filled by the reader before the worker starts.
    PacketQueue* packetQueue = nullptr; ///< Worker's input queue (owned by the coordinator).
};

#endif // PROCESSINGPARAMS_H
