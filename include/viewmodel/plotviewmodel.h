/**
 * @file plotviewmodel.h
 * @brief ViewModel for the AGC signal plot — series state, axis ranges, colors.
 */

#ifndef PLOTVIEWMODEL_H
#define PLOTVIEWMODEL_H

#include <QColor>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVector>

#include "csvseriesparser.h"
#include "plotseriesdata.h"
#include "processedstreamdata.h"

/**
 * @brief ViewModel for the AGC signal plot window.
 *
 * Holds all series data in memory and exposes axis ranges, colors, and series
 * visibility for the View. File parsing is delegated to the Model-layer
 * CsvSeriesParser; in-memory results arrive via addStreamData().
 */
class PlotViewModel : public QObject
{
    Q_OBJECT

public:
    /// Which left-axis metric is currently shown. Lock % and missed frames
    /// share the left axis; only one is visible at a time.
    enum class LockAxisView { LockPercent, MissedFrames };

    explicit PlotViewModel(QObject* parent = nullptr);

    /// @name Data loading
    /// @{
    /// Parses the CSV file synchronously and populates series data. Returns true on success.
    bool loadCsvFile(const QString& filepath);
    /// Parses the CSV file on a background thread. Emits loadStarted(), then dataChanged() or loadFailed().
    void loadCsvFileAsync(const QString& filepath);
    /// Appends one processed stream's in-memory series to the plot (accumulating). Emits dataChanged().
    void addStreamData(const ProcessedStreamData& data);
    /// Resets all data to empty state.
    void clearData();
    /// Exports current data to a CSV file.
    bool exportCsv(const QString& filepath) const;
    /// @}

    /// @name Accessors
    /// @{
    bool hasData() const;                          ///< @return True if series data is loaded.
    bool isLoading() const;                        ///< @return True if an async parse is in progress.
    int seriesCount() const;                       ///< @return Number of loaded series.
    const PlotSeriesData& seriesAt(int index) const; ///< @return Series at the given index.
    const QVector<PlotSeriesData>& allSeries() const; ///< @return All series data.
    void renameSeries(int index, const QString& name);   ///< Overrides the display name of a series.
    void recolorSeries(int index, const QColor& color);  ///< Overrides the color of a series.

    QString plotTitle() const;                     ///< @return Current plot title.
    double xMin() const;                           ///< @return Data X minimum (elapsed seconds).
    double xMax() const;                           ///< @return Data X maximum (elapsed seconds).
    double yMin() const;                           ///< @return Current SNR Y minimum (auto or manual).
    double yMax() const;                           ///< @return Current SNR Y maximum: user override (kept above yMin), else auto or manual.
    double dataYMin() const;                       ///< @return Computed SNR Y minimum from data.
    double dataYMax() const;                       ///< @return Computed SNR Y maximum from data.
    bool yAutoScale() const;                       ///< @return True if SNR Y axis is auto-scaled.
    double lockYMin() const;                       ///< @return Lock axis minimum (always 0).
    double lockYMax() const;                       ///< @return Lock axis maximum (always 100).
    double leftYMax() const;                       ///< @return Left axis max: user override, or 100 (lock %) / auto (missed frames).
    bool hasLockSeries() const;                    ///< @return True if any FrameSyncLock series are loaded.

    LockAxisView lockAxisView() const;             ///< @return Active left-axis metric (lock % vs missed frames).
    bool hasMissedFramesSeries() const;            ///< @return True if any AccumulatedMissedFrames series are loaded.
    double missedFramesMax() const;                ///< @return Max value across visible AccumulatedMissedFrames series (>= 1).

    double xViewMin() const;                       ///< @return Current X viewport minimum.
    double xViewMax() const;                       ///< @return Current X viewport maximum.

    int baseDay() const;                           ///< @return DOY of the first sample.
    double baseTimeOffset() const;                 ///< @return Seconds-since-midnight of first sample.
    /// Converts elapsed seconds to "DDD:HH:MM:SS" using the file's base time.
    QString formatTime(double elapsed) const;
    /// Inverse of formatTime: parses "DDD:HH:MM:SS" to elapsed seconds against the
    /// file's base time. Returns 0.0 when the text is not in that 4-field format.
    double parseTime(const QString& text) const;
    /// @}

    /// @name Mutators
    /// @{
    void setSeriesVisible(int index, bool visible);
    /// Same as setSeriesVisible() but does not emit seriesVisibilityChanged() or
    /// recompute the Y range — for batched edits (e.g. the Customize Plot Series
    /// dialog toggling many checkboxes before OK) where the caller calls
    /// commitAppearanceChanges() once after every change is applied, instead of
    /// paying for a full legend/graph refresh per checkbox.
    void setSeriesVisibleQuiet(int index, bool visible);
    void setPlotTitle(const QString& title);
    void setYManualRange(double min, double max);
    void setYAutoScale(bool enabled);
    void setXViewRange(double min, double max);
    void resetXRange();
    void resetYRange();
    void setLeftYMaxOverride(double max);   ///< User-set left axis maximum; resets on resetYRange().
    void setRightYMaxOverride(double max);  ///< User-set right (SNR) axis maximum; resets on resetYRange().
    /// Switches the left-axis metric and flips visibility of lock/missed frames series.
    void setLockAxisView(LockAxisView view);
    /// Notifies views that series colors/names/visibility changed. Call once after
    /// a batch of renameSeries()/recolorSeries()/setSeriesVisibleQuiet() edits (e.g.
    /// from the Customize Plot dialog) — those are pure setters and do not signal
    /// on their own. Recomputes the Y range (if auto-scaled) before signaling.
    void commitAppearanceChanges();
    /// @}

signals:
    void dataChanged();                            ///< Emitted when CSV data is loaded or cleared.
    void lockAxisViewChanged();                     ///< Emitted when the left-axis metric mode changes.
    void loadStarted();                            ///< Emitted when an async load begins.
    void loadSucceeded();                          ///< Emitted after an async load completes successfully (follows dataChanged()).
    void loadFailed();                             ///< Emitted when an async load fails.
    void loadWarning(const QString& message);       ///< Emitted after a load that succeeded but skipped malformed rows.
    void seriesVisibilityChanged(int index);        ///< Emitted when a series visibility toggles.
    void seriesAppearanceChanged();                 ///< Emitted after a batch of color/name edits so views can refresh.
    void plotTitleChanged();                        ///< Emitted when the plot title changes.
    void axisRangeChanged();                        ///< Emitted when X or Y axis ranges change.

private slots:
    void onParseFinished();                         ///< Receives result from background parse thread.

private:
    /// Assigns colors to all series based on receiver grouping.
    void assignColors();
    /// Returns a shaded variant of `base` for shadeLevel > 0 (0 returns `base` unchanged).
    /// Used to derive additional stream/channel colors from a small set of primaries.
    static QColor shadeOfColor(const QColor& base, int shadeLevel);
    /// @return True if `type` is one of the left-axis metrics (FrameSyncLock or
    /// AccumulatedMissedFrames) that share the axis and a stream's color/name.
    static bool isLeftAxisMetric(PlotSeriesData::MetricType type);
    /// @return True if `candidate` is the left-axis sibling of the stream identified
    /// by (streamLabel, streamOrder): the same stream's other left-axis metric (e.g.
    /// a Lock series' Missed-Frames counterpart). Matches on streamLabel AND
    /// streamOrder (source PCM channel id) together — streamLabel alone is not
    /// unique (two streams can share a TMATS-derived name), so a rename/recolor
    /// edit must not cross-apply to an unrelated same-labeled stream.
    static bool isFrameSyncSibling(const QString& streamLabel, int streamOrder,
                                   const PlotSeriesData& candidate);
    /// Computes Y axis range from visible series data with margin.
    void computeYRange();
    /// Commits a CsvParseResult into member state and emits dataChanged().
    void commitParseResult(CsvParseResult&& result);

    QVector<PlotSeriesData> m_series;              ///< All loaded series data.
    int m_next_series_id = 1;                      ///< Monotonic source of stable per-series ids (PlotSeriesData::id).
    QString m_plot_title;                          ///< User-defined plot title.

    double m_x_min = 0.0;                          ///< Data X range minimum.
    double m_x_max = 0.0;                          ///< Data X range maximum.
    double m_x_view_min = 0.0;                     ///< Current viewport X minimum.
    double m_x_view_max = 0.0;                     ///< Current viewport X maximum.

    double m_data_y_min = 0.0;                     ///< Computed SNR Y minimum from visible data.
    double m_data_y_max = 0.0;                     ///< Computed SNR Y maximum from visible data.
    double m_y_manual_min = 0.0;                   ///< Manual SNR Y minimum override.
    double m_y_manual_max = 0.0;                   ///< Manual SNR Y maximum override.
    bool m_y_auto_scale = true;                    ///< True to auto-scale SNR Y axis.
    double m_lock_y_min = 0.0;                     ///< Lock axis minimum (fixed at 0).
    double m_lock_y_max = 100.0;                   ///< Lock axis maximum (fixed at 100).
    bool m_has_lock_series = false;                ///< True if any FrameSyncLock series are present.
    bool m_left_y_max_user_set = false;            ///< True when user has overridden the left axis max.
    double m_left_y_max_user = 100.0;             ///< User-set left axis max value.
    bool m_right_y_max_user_set = false;           ///< True when user has overridden the right axis max.
    double m_right_y_max_user = 0.0;              ///< User-set right axis max value.
    bool m_has_missed_frames_series = false;       ///< True if any AccumulatedMissedFrames series are present.
    LockAxisView m_lock_axis_view = LockAxisView::LockPercent; ///< Active left-axis metric.

    int m_base_day = 0;                            ///< DOY of the first sample (for display).
    double m_base_time_offset = 0.0;               ///< Seconds-since-midnight of first sample.
    double m_base_abs_seconds = 0.0;               ///< Absolute IRIG seconds of the first accumulated sample.

    QFutureWatcher<CsvParseResult>* m_parse_watcher = nullptr; ///< Watcher for async parse future.
    bool m_loading = false;                        ///< True while an async parse is in flight.
};

#endif // PLOTVIEWMODEL_H
