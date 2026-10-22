/**
 * @file metrictype.h
 * @brief Which kind of measurement a plot series carries.
 *
 * Its own header so the CSV column schema (SeriesColumnSchema), which is part of the
 * Qt-free core, can name a metric without pulling in PlotSeriesData, which carries Qt
 * types for the plot.
 */

#ifndef METRICTYPE_H
#define METRICTYPE_H

/**
 * @brief Distinguishes SNR series (right axis) from the left-axis frame-sync metrics.
 *
 * FrameSyncLock and AccumulatedMissedFrames share the left axis and are shown one at a time
 * per the active LockAxisView.
 */
enum class MetricType
{
    SNR,
    FrameSyncLock,
    AccumulatedMissedFrames,
};

#endif // METRICTYPE_H
