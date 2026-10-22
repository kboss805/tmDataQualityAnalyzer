/**
 * @file seriesbuilder.h
 * @brief Qt-free logic that turns processed streams into plot series on one shared time axis.
 *
 * The data half of what `PlotViewModel::addStreamData`, `removeSource` and the CSV import's commit did
 * (de-Qt, plan step 1.6): which series a processed stream yields and what they are called, how the receiver
 * channels are numbered, the elapsed-time axis and the clock it hangs from, how that clock moves when
 * a source that starts earlier arrives or the earliest source goes, whether a new source overlaps what is
 * already loaded, and the order an imported CSV's streams are given.
 *
 * What stays with the plot is the *storage* and the presentation: the series live in `QVector`s because
 * the chart shares those buffers (one elapsed array per stream, referenced by each of its series and by
 * the chart), colours, visibility, axis ranges and the signals. Putting the samples in the core would copy
 * them; the C# application will decide how it holds them. These functions work on views of the caller's
 * data and return values, so they do not care how it is stored.
 */

#ifndef SERIESBUILDER_H
#define SERIESBUILDER_H

#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "metrictype.h"
#include "processedstreamdata.h"

/// Which stream a series belongs to. The same stream run again replaces its earlier series; two streams
/// are the same only if all three agree - a label alone is not unique (two streams can share a TMATS-derived
/// name), a channel id alone is not (it is 0 for streams without a real one), and two recordings can reuse
/// both.
struct StreamKey
{
    int sourceId = 0;
    std::string label;      ///< UTF-8.
    int channel = 0;        ///< PCM channel id, kept on a series as its stream order.

    bool operator==(const StreamKey& other) const
    {
        return sourceId == other.sourceId && label == other.label && channel == other.channel;
    }
};

/// The key of the stream @p data came from.
StreamKey streamKeyOf(const ProcessedStreamData& data);

/// One series a processed stream yields, before the plot gives it colour, visibility and an id.
struct SeriesSpec
{
    MetricType  metricType = MetricType::SNR;
    std::string name;                   ///< The legend/CSV name: the stream label for the frame-sync metrics, the
                                        ///< SNR name (channel, label, parameter) for a receiver channel. UTF-8.
    std::string streamLabel;            ///< UTF-8.
    int receiverIndex = 0;              ///< From the parameter name's last "_RCVR<N>"; 0 for the frame-sync metrics.
    int channelIndex = 0;               ///< 0-based count within (receiver) among this stream's SNR series.
    int streamOrder = 0;                ///< The stream's PCM channel id.
    int streamSequence = 0;             ///< The stream's job index, for ordering streams in the legend.
    int sourceId = 0;
    std::span<const double> yValues;    ///< A view of the processed data; it must outlive the spec.
    double yMin = std::numeric_limits<double>::max();     ///< Smallest value (as `qMin` sees NaN), `max()` if none.
    double yMax = std::numeric_limits<double>::lowest();  ///< Largest value, `lowest()` if none.
};

/// A processed stream as series on the shared axis.
struct StreamSeries
{
    std::vector<double> elapsed;        ///< Seconds from the shared base to each sample; shared by every series.
    std::vector<SeriesSpec> series;     ///< Lock %, then accumulated missed frames, then one per receiver channel;
                                        ///< a metric the stream has no data for is left out.
};

/// Builds the series of @p data against a time base at @p baseAbsSeconds. The frame-sync series exist only
/// when their arrays do. Each receiver channel's number is the one after the last "_RCVR" in its name, or 0
/// when there is none or it is not a number.
StreamSeries buildStreamSeries(const ProcessedStreamData& data, double baseAbsSeconds);

/**
 * @brief The clock the shared elapsed axis hangs from.
 *
 * Elapsed time is measured from the earliest absolute sample across every source, so the base has to move
 * when a source that starts earlier arrives (existing series shift right by the difference) and when the
 * source holding the earliest sample is removed (the rest shift left). It also knows the day and time of day
 * of the base, for labelling the axis.
 */
class SeriesTimeBase
{
public:
    double baseAbsSeconds() const { return m_abs_seconds; }
    int baseDay() const { return m_day; }                ///< Day of year of the base, 1-based.
    double baseTimeOffset() const { return m_offset; }   ///< Seconds after midnight of the base.

    /// Forgets the base: day 0, time 0, absolute 0.
    void reset();

    /// Sets the day and time of day directly, as an imported CSV states them. The absolute base is not
    /// touched: an import has no absolute time.
    void setClock(int day, double offset);

    /// Whether a source spanning [@p newMinAbs, @p newMaxAbs] absolute seconds touches the data already
    /// loaded, which spans from the base for @p existingMaxElapsed seconds. Touching at one instant counts.
    bool overlaps(double newMinAbs, double newMaxAbs, double existingMaxElapsed) const;

    /// A stream whose first absolute sample is @p firstAbsSeconds is being added; @p hasSeries is whether any
    /// series remain once the stream it replaces is gone. With none the base becomes this sample (so it can move
    /// later); otherwise it moves only earlier. @return the seconds to ADD to every existing series' elapsed
    /// values (0 when the base did not move).
    double adopt(double firstAbsSeconds, bool hasSeries);

    /// A source was removed. @p earliestElapsed is the smallest first elapsed value among the series that
    /// remain, against the current base (empty if none has a sample). If it is later than the base, the base
    /// moves up to it. @return the seconds to SUBTRACT from every remaining series' elapsed values.
    double advance(std::optional<double> earliestElapsed);

private:
    void recomputeClock();

    double m_abs_seconds = 0.0;   ///< Absolute IRIG seconds of elapsed zero.
    int m_day = 0;
    double m_offset = 0.0;
};

/// One series of an imported CSV, as far as giving its stream an order is concerned.
struct ImportedSeries
{
    MetricType  metricType = MetricType::SNR;
    int         sourceId = 0;
    std::string streamLabel;      ///< UTF-8.
    int         streamOrder = 0;  ///< In: what the file gave (the channel id, for an SNR series). Out: the order.
};

/**
 * @brief Gives each frame-sync series of an imported CSV the order of its stream.
 *
 * Receiver SNR columns carry the real PCM channel id; a stream's lock and missed-frames series share its
 * order so both tabs of the Customize dialog group them, taken from the SNR series of the same label in the
 * same source. A stream with none gets a fresh order above every one in the file. Keyed by source as well as
 * label, because two sources can have lock-only streams that share a label. SNR series are left as they are.
 */
void assignImportedStreamOrders(std::vector<ImportedSeries>& series);

#endif // SERIESBUILDER_H
