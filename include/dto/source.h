/**
 * @file source.h
 * @brief One .ch10 file's configuration within a (possibly multi-file) session.
 *
 * See docs/multi-file-input-design.md §2. MainViewModel holds a QVector<Source>
 * for every successfully-processed file in the current session; a processing
 * template captures one Source's stream configs for reuse across other files.
 */

#ifndef SOURCE_H
#define SOURCE_H

#include <QString>
#include <QVector>

#include "streamconfig.h"

/**
 * @brief A single loaded/processed .ch10 file and its per-stream configuration.
 *
 * @c sourceId is stable within the session (assigned when the file is opened or
 * added) and travels with every series/stream produced from this source
 * (ProcessedStreamData::sourceId, PlotSeriesData::sourceId), so cross-source
 * identity never collides even if two sources reuse the same PCM channel id.
 */
struct Source
{
    QString filepath;                     ///< Path to the .ch10 file.
    int     sourceId = 0;                 ///< Stable id within the session (0 = first/only source).
    int     timeChannelIndex = 0;         ///< Selected time channel combo box index for this file.
    QVector<StreamConfig> streamConfigs;  ///< Per-stream configuration used for this file's processing run.
};

#endif // SOURCE_H
