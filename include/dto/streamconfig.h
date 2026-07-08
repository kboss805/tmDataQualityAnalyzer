/**
 * @file streamconfig.h
 * @brief Per-stream (PCM channel) processing configuration captured in the
 *        StreamConfigDialog and stored in MainViewModel until the user presses Process.
 */

#ifndef STREAMCONFIG_H
#define STREAMCONFIG_H

#include <QHash>
#include <QString>

#include "calibrationprofile.h"
#include "constants.h"
#include "framesyncparams.h"

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
    FrameSyncParams sync;                   ///< Frame sync pattern/mask, frame length, scrambling, data rate.
    int        samplePeriodIndex  = UIConstants::kDefaultSamplePeriodIndex; ///< Output sample period index (0=1s, 1=100ms, 2=10ms).
    double     tmatsDataRateMbps = 0.0;     ///< TMATS-declared bit rate in Mbps (read-only, for display).

    // --- Receiver SNR calibration (ReceiverChannelInfo mode only) ---
    int        polarityIndex     = UIConstants::kDefaultPolarityIndex;   ///< 0 = Positive, 1 = Negative.
    int        slopeIndex        = UIConstants::kDefaultSlopeIndex;      ///< Voltage range index (see kSlopeVoltageLower/Upper).
    double     scaleDdBPerV      = PCMConstants::kDefaultScaleDdBPerV;   ///< Calibration scale in dB/V.
    int        numReceivers      = PCMConstants::kDefaultNumReceivers;   ///< Number of receivers.
    int        receiverChannels  = PCMConstants::kDefaultReceiverChannels; ///< Receiver channels per receiver.
    QString    receiverParamsToml;  ///< Path to the Receiver Parameters TOML (word map, required for ReceiverChannelInfo mode).

    /// Optional non-linear step calibration profiles (US5.3), keyed by zero-based
    /// word index within the minor frame (matches ParameterInfo::word). Session-only;
    /// never serialized. Empty = linear math for every channel.
    QHash<int, CalibrationProfile> calibrationByWord;

    // --- Calibration input references (session save/load, Phase 6) ---
    // The extracted calibrationByWord profiles above are never serialized, but a
    // session still stores what produced them so re-extraction can be re-run after
    // load (v1 loads linear-only; auto re-extraction is a documented future
    // follow-up). Empty calCh10Path means no non-linear calibration was extracted.
    QString calCh10Path;    ///< Path to the calibration Chapter 10 recording used for extraction.
    QString stepTomlPath;   ///< Path to the step-config TOML used for extraction.
    double  clipStartSec = 0.0; ///< Seconds clipped from the start before step detection.
    double  clipEndSec   = 0.0; ///< Seconds clipped from the end before step detection.
};

#endif // STREAMCONFIG_H
