/**
 * @file csvserieswriter.cpp
 * @brief Implementation of CsvSeriesWriter — writes the app's CSV export. Matched pair with
 *        CsvSeriesParser.
 */

#include "csvserieswriter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

#include "clocktime.h"
#include "seriescolumnschema.h"
#include "textformat.h"

namespace {

/// One sample, placed on the millisecond grid.
struct Cell
{
    std::int64_t ms;
    int column;
    double value;
};

/// A time rounded to whole milliseconds, halves away from zero, computed the way `qRound` does it
/// (add or subtract a half and truncate) so even the values where that differs from `std::round` agree:
/// -62.5 ms is -63, not -62, and 0.49999999999999994 is 1.
std::int64_t toMilliseconds(double seconds)
{
    const double ms = seconds * 1000.0;
    return ms >= 0.0 ? static_cast<std::int64_t>(ms + 0.5) : static_cast<std::int64_t>(ms - 0.5);
}

} // namespace

bool CsvSeriesWriter::write(const std::filesystem::path& path, std::span<const CsvColumn> columns, const CsvTimeBase& base)
{
    if (columns.empty())
    {
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    constexpr const char* kLineEnd = "\r\n";

    // Header. Frame-sync series names are stored bare (no metric suffix) for the plot legend;
    // SeriesColumnSchema::columnHeader() qualifies them so the CSV columns are self-describing (a
    // stream's lock and missed-frames series share the same bare name and would otherwise export as
    // two identical headers) and stays the inverse of the import parser.
    std::string out = SeriesColumnSchema::kTimeHeader;
    for (const CsvColumn& c : columns)
    {
        out += ',';
        out += SeriesColumnSchema::columnHeader(c.metricType, c.name, c.sourceId);
    }
    out += kLineEnd;

    // Place every sample on the millisecond grid, in column order and then sample order, so that a
    // stable sort leaves a column's samples in one millisecond in the order they were given and the
    // last of them wins.
    std::size_t total = 0;
    for (const CsvColumn& c : columns)
    {
        total += std::min(c.xValues.size(), c.yValues.size());
    }
    std::vector<Cell> cells;
    cells.reserve(total);
    for (std::size_t i = 0; i < columns.size(); ++i)
    {
        const CsvColumn& c = columns[i];
        const std::size_t n = std::min(c.xValues.size(), c.yValues.size());
        for (std::size_t j = 0; j < n; ++j)
        {
            cells.push_back({toMilliseconds(c.xValues[j]), static_cast<int>(i), c.yValues[j]});
        }
    }
    std::stable_sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
        return a.ms != b.ms ? a.ms < b.ms : a.column < b.column;
    });

    // One line per distinct millisecond.
    std::size_t at = 0;
    while (at < cells.size())
    {
        const std::int64_t ms = cells[at].ms;
        out += clocktime::format(base.baseDay, base.baseTimeOffset, static_cast<double>(ms) / 1000.0, true);

        int column = 0;
        while (at < cells.size() && cells[at].ms == ms)
        {
            // The last sample this column has in the millisecond.
            const int col = cells[at].column;
            std::size_t last = at;
            while (last + 1 < cells.size() && cells[last + 1].ms == ms && cells[last + 1].column == col)
            {
                ++last;
            }
            for (; column < col; ++column)
            {
                out += ',';
            }
            out += ',';
            out += textformat::formatFixed(cells[last].value, 6);
            ++column;
            at = last + 1;
        }
        for (; column < static_cast<int>(columns.size()); ++column)
        {
            out += ',';
        }
        out += kLineEnd;

        if (out.size() > (1u << 20))
        {
            file.write(out.data(), static_cast<std::streamsize>(out.size()));
            out.clear();
        }
    }

    file.write(out.data(), static_cast<std::streamsize>(out.size()));
    file.flush();
    return static_cast<bool>(file);
}
