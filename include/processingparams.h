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
    uint64_t sync_pat      = 0;     ///< Resolved minor-frame sync pattern.
    uint64_t sync_mask     = 0;     ///< Resolved minor-frame sync mask.
    uint32_t sync_pat_len  = 0;     ///< Sync pattern length in bits.
    uint32_t bits_in_frame = 0;     ///< Bits per minor frame.
    uint32_t words_in_frame = 0;    ///< Words per minor frame.
    uint32_t word_len      = 0;     ///< Common word length in bits.
    uint64_t word_mask     = 0;     ///< Common word mask.
    double   delta_100ns   = 0.0;   ///< Bit period in 100 ns units (TMATS or user override).
    uint32_t min_syncs     = 0;     ///< Minimum consecutive syncs before frames are saved.
    bool     needs_swap    = true;  ///< True if raw PCM data must be byte-swapped.
    bool     resolved      = false; ///< True once the reader has populated this struct.
};

/// @brief Validated parameters bundle passed to the worker thread.
struct ProcessingParams {
    QString filename;              ///< Path to the .ch10 input file.
    int time_channel_id = -1;     ///< Resolved time channel ID.
    int pcm_channel_id = -1;      ///< Resolved PCM channel ID.
    uint64_t frame_sync = 0;      ///< Frame sync pattern as a numeric value.
    uint64_t frame_sync_mask = 0; ///< Frame sync mask. 0 = derive all-ones from sync_pattern_length.
    int sync_pattern_length = 0;  ///< Sync pattern length in bits.
    int words_in_minor_frame = 0; ///< Words per PCM minor frame (data words + 1).
    int bits_in_minor_frame = 0;  ///< Total bits per PCM minor frame.
    uint64_t start_seconds = 0;   ///< Start of extraction window (IRIG seconds).
    uint64_t stop_seconds = 0;    ///< End of extraction window (IRIG seconds).
    double sample_period_sec = 0.1; ///< Output sample period in seconds (default 100 ms).
    bool is_randomized = false;   ///< True if RNRZ-L encoding (user-specified).

    StreamMode mode = StreamMode::ReceiverChannelInfo; ///< Processing mode for this stream.
    double data_rate_bps = 0.0;   ///< User bit rate in bits/sec. 0 = use TMATS-derived rate.
    QString stream_label;         ///< Display label of the source stream (for plot/logs).

    ResolvedPcmAttrs resolved_attrs; ///< Filled by the reader before the worker starts.
    PacketQueue* packet_queue = nullptr; ///< Worker's input queue (owned by the coordinator).
};

#endif // PROCESSINGPARAMS_H
