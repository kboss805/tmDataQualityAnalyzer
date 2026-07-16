/**
 * @file seriesappearance.h
 * @brief Per-series display customization (name + color) captured for reuse in a
 *        processing template (Batch Apply, docs/processing-template-design.md).
 */

#ifndef SERIESAPPEARANCE_H
#define SERIESAPPEARANCE_H

#include <QColor>
#include <QString>

#include "plotseriesdata.h"

/**
 * @brief One series' user-chosen name and color, keyed within a template stream
 *        entry by (metricType, receiverIndex, channelIndex).
 *
 * A frame-sync lock stream contributes one record (receiver/channel 0); a
 * Receiver SNR stream contributes one per receiver channel. Reapplied after a
 * batch file finishes processing, matched onto that file's freshly-created
 * series by the same triple -- exact, since matching files share channel ids.
 */
struct SeriesAppearance
{
    PlotSeriesData::MetricType metricType = PlotSeriesData::MetricType::SNR;
    int     receiverIndex = 0;   ///< 1-based receiver number; 0 for lock/missed series.
    int     channelIndex  = 0;   ///< 0-based channel within receiver; 0 for lock/missed series.
    QString name;                ///< User-chosen series name.
    QColor  color;               ///< User-chosen series color.
};

#endif // SERIESAPPEARANCE_H
