/**
 * @file csvserieswriter.cpp
 * @brief Implementation of CsvSeriesWriter — writes the app's CSV export. Matched pair with
 *        CsvSeriesParser.
 */

#include "csvserieswriter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

#include "clocktime.h"
#include "seriescolumnschema.h"
#include "textformat.h"

namespace {

/// One sample, placed on the time grid: milliseconds after midnight of the base day.
struct Cell
{
    std::int64_t ms;
    int column;
    double value;
};

/// The slowest sample period the export snaps a column to; a column slower than this is written to the millisecond.
constexpr std::int64_t kLongestPeriodMs = 100'000;

/// How far from exactly halfway between two slots, as a share of a period, a time still counts as halfway, and goes the
/// way halfway goes (up, away from zero). A column's times are made by adding its period over and over, so a time that is
/// exactly halfway in the recording is halfway only to within the noise of those sums (about 1e-8 of a millisecond a sample,
/// more over a long run): without this, such a column's samples fell to either side of the halfway point by chance, two
/// to one slot and none to the next, and the second of the two won. A time less than this from halfway is rounded as
/// halfway is; the width is far above the noise and far below any difference between two real times (a millisecond is
/// 0.01 of a 100 ms period).
constexpr double kTieTolerance = 1e-4;

/// A column's sample period in whole milliseconds: the median gap between its successive distinct times, snapped to
/// the nearest of 1, 2, 5, 10, 20, 50 ... ms (nearest on a log scale, so 0.96 s and 1.04 s are both 1 s), at least
/// 1 ms and at most `kLongestPeriodMs`. A column with no steady rate to read gets 1 ms, the grid that moves a time the
/// least: one with fewer than three distinct times, or fewer than two thirds of whose gaps lie within half a median of
/// the median, or whose median gap is longer than `kLongestPeriodMs`.
std::int64_t samplePeriodMs(std::span<const double> xs)
{
    std::vector<double> times;
    times.reserve(xs.size());
    for (const double x : xs)
    {
        if (std::isfinite(x))
        {
            times.push_back(x);
        }
    }
    if (!std::is_sorted(times.begin(), times.end()))
    {
        std::sort(times.begin(), times.end());
    }
    std::vector<double> gaps;
    gaps.reserve(times.size());
    for (std::size_t i = 1; i < times.size(); ++i)
    {
        if (times[i] > times[i - 1])
        {
            gaps.push_back(times[i] - times[i - 1]);
        }
    }
    if (gaps.size() < 2)
    {
        return 1;
    }
    const auto middle = gaps.begin() + static_cast<std::ptrdiff_t>(gaps.size() / 2);
    std::nth_element(gaps.begin(), middle, gaps.end());
    const double median = *middle;
    const auto steady = std::count_if(gaps.begin(), gaps.end(), [median](double g) { return g >= 0.5 * median && g <= 1.5 * median; });
    if (static_cast<std::size_t>(steady) * 3 < gaps.size() * 2)
    {
        return 1;
    }
    const double medianMs = median * 1000.0;
    if (medianMs > static_cast<double>(kLongestPeriodMs))
    {
        return 1;
    }

    std::int64_t best = 1;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::int64_t decade = 1; decade <= kLongestPeriodMs; decade *= 10)
    {
        for (const std::int64_t step : { 1, 2, 5 })
        {
            const std::int64_t period = decade * step;
            if (period > kLongestPeriodMs)
            {
                continue;
            }
            const double distance = std::fabs(std::log(medianMs / static_cast<double>(period)));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = period;
            }
        }
    }
    return best;
}

/// @p timeMs (milliseconds after midnight of the base day) moved to the nearest multiple of @p periodMs, halves away
/// from zero, by adding or subtracting a half and truncating - as v2.14's export rounded to the millisecond, so with a
/// period of 1 even the values where that differs from `std::round` agree: -62.5 ms is -63, not -62.
std::int64_t onGrid(double timeMs, std::int64_t periodMs)
{
    const double periods = timeMs / static_cast<double>(periodMs);
    // NOLINTNEXTLINE(bugprone-incorrect-roundings): half away from zero, as v2.14.2 wrote it
    const std::int64_t whole = periods >= 0.0 ? static_cast<std::int64_t>(periods + 0.5 + kTieTolerance)
                                              : static_cast<std::int64_t>(periods - 0.5 - kTieTolerance);
    return whole * periodMs;
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

    // Place every sample on its column's own time grid (its sample period, on the clock: a 1 s column on whole
    // seconds, a 100 ms one on tenths), in column order and then sample order, so that a stable sort leaves a
    // column's samples in one slot in the order they were given and the last of them wins. Columns that sample at
    // the same rate share their rows, however far apart their samples fell within a period.
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
        const std::int64_t period = samplePeriodMs(c.xValues.first(n));
        for (std::size_t j = 0; j < n; ++j)
        {
            const double clockMs = (base.baseTimeOffset + c.xValues[j]) * 1000.0;
            cells.push_back({onGrid(clockMs, period), static_cast<int>(i), c.yValues[j]});
        }
    }
    std::stable_sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
        return a.ms != b.ms ? a.ms < b.ms : a.column < b.column;
    });

    // One line per distinct time on the grid.
    std::size_t at = 0;
    while (at < cells.size())
    {
        const std::int64_t ms = cells[at].ms;
        out += clocktime::formatMilliseconds(base.baseDay, ms);

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
