/**
 * @file framesyncparams.h
 * @brief Frame-sync / acquisition parameter bundle shared by the UI-layer
 *        StreamConfig and the CalibrationExtractor::Request.
 *
 * These six fields recur together everywhere a PCM stream's bit-level extraction
 * is described in its user-facing (hex/Mbps) form: the stream config captured in
 * the dialogs, and the calibration extraction request. Bundling them keeps the
 * two in step and collapses long parameter lists into one value. (The worker-layer
 * ProcessingParams deliberately keeps the *resolved numeric* form separate.)
 */

#ifndef FRAMESYNCPARAMS_H
#define FRAMESYNCPARAMS_H

#include <QString>

#include "constants.h"

/// @brief A PCM stream's frame-sync / acquisition parameters in UI form.
struct FrameSyncParams
{
    QString pattern = PCMConstants::kDefaultFrameSync;          ///< Frame sync pattern as hex string (e.g. "FE6B2840").
    QString mask    = PCMConstants::kDefaultFrameSyncMask;      ///< Frame sync mask as hex string (e.g. "FFFFFFFF").
    int     bitsInMinorFrame = PCMConstants::kDefaultBitsPerFrame; ///< Total bits per minor frame (including sync word bits).
    bool    randomized = false;   ///< true = RNRZ-L descrambler; false = NRZ-L.
    bool    inverted   = false;   ///< true = bit-for-bit inversion of raw data before derandomization.
    double  dataRateMbps = 0.0;   ///< Data rate in Mbps. 0 = use the TMATS-derived bit rate.
};

#endif // FRAMESYNCPARAMS_H
