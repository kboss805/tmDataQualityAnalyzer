/**
 * @file csvseriesparser.h
 * @brief Model-layer parser that turns a FrameProcessor CSV file into plot series.
 *
 * Pure data ingestion: no UI, no Qt widgets, no signals. Extracted out of
 * PlotViewModel so the ViewModel keeps only presentation state (axis ranges,
 * visibility, colors) while file parsing lives in the Model layer. Every method
 * is static and thread-safe, so PlotViewModel can run parse() on a worker thread.
 */

#ifndef CSVSERIESPARSER_H
#define CSVSERIESPARSER_H

#include <QString>
#include <QVector>

#include "plotseriesdata.h"

class QTextStream;

/**
 * @brief Result of a CSV parse operation.
 *
 * Returned by CsvSeriesParser::parse() and carried across the thread boundary by
 * PlotViewModel's QFutureWatcher.
 */
struct CsvParseResult
{
    bool success = false;
    QVector<PlotSeriesData> series;
    int baseDay = 0;
    double baseTimeOffset = 0.0;
    double xMax = 0.0;
    int skippedRows = 0;   ///< Count of malformed data rows skipped (too few columns or an unparseable timestamp).
};

/**
 * @brief Parses the application's CSV export back into PlotSeriesData.
 *
 * The format is the one PlotViewModel::exportCsv writes (the two are a matched
 * pair, see PlotConstants::kCsv* tokens): a single combined timestamp column
 * "Time (DOY:HH:MM:SS.mmm)" (values "DDD:HH:MM:SS.mmm"), followed by one column
 * per series whose header is qualified per metric — "<name> Lock (%)",
 * "<name> Accumulated Missed Frames", or a bare SNR name. The legacy two-column
 * "Day,Time,..." format is no longer read.
 */
class CsvSeriesParser
{
public:
    /// Parses the CSV file. Pure function — safe to run on any thread.
    static CsvParseResult parse(const QString& filepath);

private:
    /// Parses the CSV data rows into the series structure, counting malformed rows.
    static void parseDataRows(QTextStream& stream, int param_count,
                              QVector<PlotSeriesData>& series,
                              int& out_base_day, double& out_base_time_offset,
                              int& out_skipped_rows);
    /// Parses a combined "DDD:HH:MM:SS.mmm" stamp into its day and seconds-since-
    /// midnight parts. Returns false (leaving outputs untouched) if malformed.
    static bool parseCombinedTimestamp(const QString& stamp, int& out_day, double& out_seconds);
};

#endif // CSVSERIESPARSER_H
