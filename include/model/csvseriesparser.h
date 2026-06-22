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
};

/**
 * @brief Parses FrameProcessor "Day,Time,param..." CSV output into PlotSeriesData.
 */
class CsvSeriesParser
{
public:
    /// Parses the CSV file. Pure function — safe to run on any thread.
    static CsvParseResult parse(const QString& filepath);

private:
    /// Parses the CSV data rows into the series structure.
    static void parseDataRows(QTextStream& stream, int param_count,
                              QVector<PlotSeriesData>& series,
                              int& out_base_day, double& out_base_time_offset);
    /// Parses a "HH:MM:SS.mmm" time string to seconds since midnight.
    static double parseTimeToSeconds(const QString& time_str);
};

#endif // CSVSERIESPARSER_H
