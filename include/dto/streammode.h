/**
 * @file streammode.h
 * @brief How a single PCM stream is processed.
 *
 * Its own header so the core's input (ProcessingParams) can name a mode without pulling in
 * StreamConfig, which carries Qt types for the dialogs.
 */

#ifndef STREAMMODE_H
#define STREAMMODE_H

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

#endif // STREAMMODE_H
