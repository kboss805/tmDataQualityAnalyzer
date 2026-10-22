/**
 * @file csvserieswriter.h
 * @brief Qt-free writer of the application's CSV export; the inverse of CsvSeriesParser.
 *
 * Both directions of the format live in the core (plan step 1.2) so the Qt application and the
 * future C# one write and read the same bytes through one implementation. The plot hands over the
 * series it holds as a list of `CsvColumn`s (viewing its own storage, copying nothing).
 */

#ifndef CSVSERIESWRITER_H
#define CSVSERIESWRITER_H

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "metrictype.h"

/// One column to write: the series' identity (which decides its header) and its samples. The
/// spans only need to outlive the call.
struct CsvColumn
{
    MetricType metricType = MetricType::SNR;
    std::string name;                     ///< The series' name (UTF-8), as the plot shows it.
    int sourceId = 0;                     ///< Source the series came from; 0 is written unqualified.
    std::span<const double> xValues;      ///< Elapsed seconds from the recording's first sample.
    std::span<const double> yValues;      ///< One value per x.
};

/// Where elapsed time zero falls on the clock, so the time column can read as a day and time of day.
struct CsvTimeBase
{
    int baseDay = 0;
    double baseTimeOffset = 0.0;
};

/**
 * @brief Writes columns as the CSV the importer reads.
 *
 * Layout: a header line "Time (DOY:HH:MM:SS.mmm)" followed by each column's header
 * (`SeriesColumnSchema::columnHeader`), then one line per distinct millisecond of any column,
 * in time order: the stamp, then each column's value to six decimals, or nothing where that column
 * has no sample at that millisecond. A time is rounded to the millisecond with halves away from zero;
 * where one column has several samples in a millisecond the last wins. Lines end in CR LF and the
 * text is UTF-8 without a byte-order mark, as the application has always written it on Windows.
 */
class CsvSeriesWriter
{
public:
    /// @return false if there are no columns or the file cannot be created or fully written.
    static bool write(const std::filesystem::path& path, std::span<const CsvColumn> columns, const CsvTimeBase& base);
};

#endif // CSVSERIESWRITER_H
