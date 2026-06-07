/**
 * @file streamconfig.h
 * @brief Per-stream (PCM channel) processing configuration captured in the
 *        StreamConfigDialog and stored in MainViewModel until the user presses Process.
 */

#ifndef STREAMCONFIG_H
#define STREAMCONFIG_H

#include <QString>

#include "constants.h"

/**
 * @brief How a single PCM stream should be processed.
 *
 * - ReceiverChannelInfo: extracts calibrated receiver-channel (SNR) values from
 *   the minor frame using a Receiver Parameters TOML (word map + calibration).
 * - FrameSyncLockStats: only measures frame-sync lock percentage; no receiver
 *   parameters required.
 */
enum class StreamMode
{
    ReceiverChannelInfo = 0,
    FrameSyncLockStats  = 1,
};

/**
 * @brief Configuration for one PCM stream (channel) found in a Chapter 10 file.
 *
 * Built per-row by StreamConfigDialog and stored in MainViewModel. Streams with
 * @c process == false are skipped when the user presses the Process button.
 */
struct StreamConfig
{
    int        pcmChannelId      = -1;      ///< Chapter 10 channel ID of this PCM stream.
    QString    label;                       ///< Display label, e.g. "Ch 32".
    bool       process           = false;   ///< Whether to process this stream when Process is pressed.
    StreamMode mode              = StreamMode::FrameSyncLockStats;

    // --- Frame parameters (shared by both modes) ---
    QString    frameSyncPattern  = PCMConstants::kDefaultFrameSync;      ///< Frame sync pattern as hex string (e.g. "FE6B2840").
    QString    frameSyncMask     = PCMConstants::kDefaultFrameSyncMask;  ///< Frame sync mask as hex string (e.g. "FFFFFFFF").
    int        bitsInMinorFrame  = PCMConstants::kDefaultBitsPerFrame;   ///< Total bits per minor frame (including sync word bits).
    bool       randomized        = false;   ///< true = RNRZ-L descrambler; false = NRZ-L.
    int        sampleRateIndex   = UIConstants::kDefaultSampleRateIndex; ///< Output sample rate index (0=1Hz, 1=10Hz, 2=100Hz).
    double     dataRateMbps      = 0.0;     ///< Data rate in Mbps. 0 = use the TMATS-derived bit rate.
    double     tmatsDataRateMbps = 0.0;     ///< TMATS-declared bit rate in Mbps (read-only, for display).

    // --- Receiver SNR calibration (ReceiverChannelInfo mode only) ---
    int        polarityIndex     = UIConstants::kDefaultPolarityIndex;   ///< 0 = Positive, 1 = Negative.
    int        slopeIndex        = UIConstants::kDefaultSlopeIndex;      ///< Voltage range index (see kSlopeVoltageLower/Upper).
    double     scaleDdBPerV      = PCMConstants::kDefaultScaleDdBPerV;   ///< Calibration scale in dB/V.
    int        numReceivers      = PCMConstants::kDefaultNumReceivers;   ///< Number of receivers.
    int        receiverChannels  = PCMConstants::kDefaultReceiverChannels; ///< Receiver channels per receiver.
    QString    receiverParamsToml;  ///< Path to the Receiver Parameters TOML (word map, required for ReceiverChannelInfo mode).
};

#endif // STREAMCONFIG_H
