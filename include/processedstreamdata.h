/**
 * @file processedstreamdata.h
 * @brief In-memory result of processing a single PCM stream.
 *
 * Replaces the old CSV-on-disk output. FrameProcessor accumulates one of these
 * per run; ProcessingCoordinator hands it to PlotViewModel, which appends its
 * series to the (accumulating) plot.
 */

#ifndef PROCESSEDSTREAMDATA_H
#define PROCESSEDSTREAMDATA_H

#include <QString>
#include <QVector>

#include "streamconfig.h"

/**
 * @brief One receiver-channel (SNR) series within a processed stream.
 *
 * @c values is parallel to ProcessedStreamData::timesSec.
 */
struct ProcessedChannelSeries
{
    QString         name;   ///< Parameter name, e.g. "L_RCVR1".
    int             word = -1; ///< Zero-based word index within the minor frame.
    QVector<double> values; ///< Calibrated value per output time sample.
};

/**
 * @brief All in-memory output for a single processed PCM stream.
 *
 * Time-sample vectors are parallel: @c timesSec[i], @c lockPercent[i], and
 * each ProcessedChannelSeries::values[i] all describe output sample @c i.
 * For FrameSyncLockStats mode, @c channels is empty (lock percentage only).
 */
struct ProcessedStreamData
{
    QString    streamLabel;         ///< Display label of the source stream, e.g. "Ch 32".
    int        pcmChannelId = -1;   ///< Source PCM channel ID.
    int        jobIndex     = 0;    ///< 0-based position of this stream in the original job list; used to preserve legend order regardless of parallel completion order.
    StreamMode mode = StreamMode::ReceiverChannelInfo; ///< Mode used to produce this data.

    QVector<double> timesSec;       ///< Absolute IRIG seconds per output sample.
    QVector<double> lockPercent;    ///< Frame-sync lock percentage (0..100) per output sample.
    QVector<double> accumulatedMissedFrames; ///< Cumulative missed frames count per output sample (monotonic, parallel to timesSec).
    QVector<ProcessedChannelSeries> channels; ///< SNR channel series (empty in lock-only mode).

    /// @return true if at least one output sample was recorded.
    bool hasSamples() const { return !timesSec.isEmpty(); }
};

#endif // PROCESSEDSTREAMDATA_H
