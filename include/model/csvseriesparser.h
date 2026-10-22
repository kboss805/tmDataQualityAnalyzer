/**
 * @file csvseriesparser.h
 * @brief Qt-free parser that turns the application's CSV export back into plot series.
 *
 * Pure data ingestion: no UI, no Qt, no signals. Every method is static and thread-safe, so
 * `PlotViewModel` runs `parse()` on a worker thread. Text is UTF-8 `std::string` and the path a
 * `std::filesystem::path`; the plot converts to its own `QString`/`QVector` series at the boundary
 * (de-Qt, plan step 1.2). Its inverse is `CsvSeriesWriter`.
 */

#ifndef CSVSERIESPARSER_H
#define CSVSERIESPARSER_H

#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "metrictype.h"

/**
 * @brief One column of the CSV: a series' identity and its samples.
 *
 * What the plot's `PlotSeriesData` carries that the file determines; presentation (colour,
 * visibility, id) is the plot's to add.
 */
struct CsvSeries
{
    MetricType  metricType    = MetricType::SNR;
    std::string name;                 ///< Column header, with any source qualifier removed (UTF-8).
    std::string streamLabel;          ///< Stream the series belongs to (UTF-8); empty if the header names none.
    int         receiverIndex = 0;    ///< 1-based receiver from "_RCVR<N>"; 0 for the frame-sync metrics.
    int         channelIndex  = 0;    ///< 0-based running count within (source, stream, receiver), SNR only.
    int         streamOrder   = 0;    ///< PCM channel id for an SNR series; 0 for the others (the plot assigns it).
    int         sourceId      = 0;    ///< From the "S<n>| " qualifier; 0 when there is none.
    std::vector<double> xValues;      ///< Elapsed seconds from the first row.
    std::vector<double> yValues;      ///< One value per x.
    double yMin = std::numeric_limits<double>::max();     ///< Smallest value, `max()` if there is none.
    double yMax = std::numeric_limits<double>::lowest();  ///< Largest value, `lowest()` if there is none.
};

/// Result of a CSV parse operation.
struct CsvParseResult
{
    bool success = false;
    std::vector<CsvSeries> series;
    int baseDay = 0;            ///< Day of the first parsed row.
    double baseTimeOffset = 0.0; ///< Seconds after midnight of the first parsed row.
    double xMax = 0.0;
    int skippedRows = 0;        ///< Malformed data rows skipped (too few columns or an unparseable timestamp).
};

/**
 * @brief Parses the application's CSV export back into series.
 *
 * The format is the one `CsvSeriesWriter` writes (the two are a matched pair, see
 * `SeriesColumnSchema`): a single combined timestamp column "Time (DOY:HH:MM:SS.mmm)" (values
 * "DDD:HH:MM:SS.mmm"), followed by one column per series whose header is qualified per metric -
 * "<name> Lock (%)", "<name> Accumulated Missed Frames", or a bare SNR name. The legacy two-column
 * "Day,Time,..." format is not read.
 *
 * The file is read as UTF-8, with a UTF-8 or UTF-16 (little or big endian) byte-order mark honoured;
 * a line ends at a line feed, and carriage returns are ignored wherever they are (the importer used
 * to read through a Windows text-mode file, which drops them). Bytes that are not valid UTF-8 are
 * kept as they are.
 */
class CsvSeriesParser
{
public:
    /// Parses the CSV file. Pure function - safe to run on any thread. @c success is false for an
    /// unreadable file, a wrong header, or a file with no parseable sample.
    static CsvParseResult parse(const std::filesystem::path& path);

private:
    /// Parses a combined "DDD:HH:MM:SS.mmm" stamp into its day and seconds-since-midnight parts.
    /// Returns false (leaving outputs untouched) if malformed.
    static bool parseCombinedTimestamp(std::string_view stamp, int& out_day, double& out_seconds);
};

#endif // CSVSERIESPARSER_H
