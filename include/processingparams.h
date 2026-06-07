/**
 * @file processingparams.h
 * @brief Validated parameters bundle for a single AGC processing run.
 */

#ifndef PROCESSINGPARAMS_H
#define PROCESSINGPARAMS_H

#include <cstdint>
#include <QString>

#include "streamconfig.h"

/// @brief Calibration scaling parameters.
struct CalibrationParams {
    double scale_lower_bound = 0; ///< Lower dB bound (voltage_lower * range_dB_per_V).
    double scale_upper_bound = 0; ///< Upper dB bound (voltage_upper * range_dB_per_V).
    bool negative_polarity = false; ///< True if AGC polarity is negative.
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
    CalibrationParams calibration; ///< Calibration slope/scale parameters.
    uint64_t start_seconds = 0;   ///< Start of extraction window (IRIG seconds).
    uint64_t stop_seconds = 0;    ///< End of extraction window (IRIG seconds).
    int sample_rate = 1;          ///< Output sample rate in Hz.
    bool is_randomized = false;   ///< True if RNRZ-L encoding (user-specified).

    StreamMode mode = StreamMode::ReceiverChannelInfo; ///< Processing mode for this stream.
    double data_rate_bps = 0.0;   ///< User bit rate in bits/sec. 0 = use TMATS-derived rate.
    QString stream_label;         ///< Display label of the source stream (for plot/logs).
};

#endif // PROCESSINGPARAMS_H
