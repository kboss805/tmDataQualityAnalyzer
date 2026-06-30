/**
 * @file plotseriesdata.h
 * @brief Plain data type (DTO) for a single plot series.
 *
 * Shared by the plot ViewModel (PlotViewModel), the View (PlotWidget,
 * PlotCustomizationDialog), and the Model-layer CSV parser (CsvSeriesParser).
 * Kept in its own header so none of those layers has to include another's
 * header just to name this struct.
 */

#ifndef PLOTSERIESDATA_H
#define PLOTSERIESDATA_H

#include <limits>

#include <QColor>
#include <QString>
#include <QVector>

/**
 * @brief Data for a single plot series (one receiver channel or lock statistic).
 *
 * Built by CsvSeriesParser from CSV output, or by PlotViewModel::addStreamData()
 * from in-memory processed stream results.
 */
struct PlotSeriesData
{
    /// Distinguishes SNR series (right axis) from the left-axis frame-sync metrics.
    /// FrameSyncLock and AccumulatedMissedFrames share the left axis and are shown one at a
    /// time per the active LockAxisView.
    enum class MetricType { SNR, FrameSyncLock, AccumulatedMissedFrames };

    QString name;             ///< Column header, e.g., "L_RCVR1" or "Framesync Lock (%)".
    QString streamLabel;      ///< Source stream's label (ProcessedStreamData::streamLabel); identifies
                               ///< which logical stream this series belongs to, so reprocessing that
                               ///< same stream replaces its prior series instead of stacking a duplicate.
    int receiverIndex = 0;    ///< 1-based receiver number from "_RCVR<N>" suffix; 0 for lock series.
    int channelIndex  = 0;    ///< 0-based within receiver, for color shade.
    int streamOrder    = 0;   ///< Source PCM channel ID, for ordering series within a legend group.
    int streamSequence = 0;   ///< 0-based job index; used to sort legend entries in submission order regardless of parallel completion order.
    int id = 0;               ///< Unique, stable per-series identity assigned by PlotViewModel. A given id's
                               ///< data is immutable (reprocessing yields new ids), letting the View reconcile
                               ///< graphs across appends without re-copying unchanged series data.
    MetricType metricType = MetricType::SNR; ///< Which axis this series belongs to.
    QVector<double> xValues;  ///< Elapsed seconds from first sample.
    QVector<double> yValues;  ///< Calibrated dB (SNR) or percentage (lock) values.
    bool visible = true;      ///< Whether this series is currently shown.
    QColor color;             ///< Assigned display color.
    double yMinCached = std::numeric_limits<double>::max();    ///< Cached min Y value.
    double yMaxCached = std::numeric_limits<double>::lowest(); ///< Cached max Y value.
};

#endif // PLOTSERIESDATA_H
