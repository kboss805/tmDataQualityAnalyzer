/**
 * @file phasedarraylogparser.cpp
 * @brief Implementation of PhasedArrayLogParser: a phased array antenna's SNR log into plot series (US10.0).
 */

#include "phasedarraylogparser.h"

#include <algorithm>
#include <string>
#include <vector>

#include "constants.h"
#include "linesource.h"
#include "seriesrange.h"
#include "textparse.h"

using csvlines::LineSource;
using csvlines::splitKeepEmpty;

namespace {

constexpr std::size_t kLeadingColumns = 3;   // Time, Name, Polarity

/// Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's days_from_civil).
long long daysFromCivil(int year, int month, int day)
{
    year -= month <= 2 ? 1 : 0;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const long long year_of_era = year - era * 400;
    const long long day_of_year = (153LL * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const long long day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

bool isLeapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int daysInMonth(int year, int month)
{
    static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && isLeapYear(year) ? 29 : kDays[month - 1];
}

/// @p count ASCII digits at @p at, as a number; false when any is not a digit.
bool digitsAt(std::string_view s, std::size_t at, std::size_t count, int& out)
{
    if (at + count > s.size())
    {
        return false;
    }
    int value = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const char c = s[at + i];
        if (c < '0' || c > '9')
        {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

/// 0, 1 or 2 for HPol, VPol or Comb (any case), -1 for anything else.
int polarizationIndex(const std::string& text)
{
    const std::string lower = textparse::asciiLower(text);
    if (lower == "hpol") { return 0; }
    if (lower == "vpol") { return 1; }
    if (lower == "comb") { return 2; }
    return -1;
}

} // namespace

bool PhasedArrayLogParser::parseIsoUtc(std::string_view stamp, long long& out_days, int& out_day_of_year, double& out_seconds)
{
    // "YYYY-MM-DDTHH:MM:SS" then an optional ".fff..." then "Z".
    constexpr std::size_t kShortest = 20;
    if (stamp.size() < kShortest || stamp.back() != 'Z' || stamp[4] != '-' || stamp[7] != '-' || stamp[10] != 'T' ||
        stamp[13] != ':' || stamp[16] != ':')
    {
        return false;
    }
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!digitsAt(stamp, 0, 4, year) || !digitsAt(stamp, 5, 2, month) || !digitsAt(stamp, 8, 2, day) ||
        !digitsAt(stamp, 11, 2, hour) || !digitsAt(stamp, 14, 2, minute) || !digitsAt(stamp, 17, 2, second))
    {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month) || hour > 23 || minute > 59 || second > 59)
    {
        return false;
    }
    double fraction = 0.0;
    const std::string_view rest = stamp.substr(19, stamp.size() - 20);   // between the seconds and the Z
    if (!rest.empty())
    {
        if (rest.size() < 2 || rest[0] != '.')
        {
            return false;
        }
        double scale = 0.1;
        for (std::size_t i = 1; i < rest.size(); ++i)
        {
            if (rest[i] < '0' || rest[i] > '9')
            {
                return false;
            }
            fraction += (rest[i] - '0') * scale;
            scale /= 10.0;
        }
    }
    const long long days = daysFromCivil(year, month, day);
    out_days = days;
    out_day_of_year = static_cast<int>(days - daysFromCivil(year, 1, 1)) + 1;
    out_seconds = hour * TimeConstants::kSecondsPerHour + minute * TimeConstants::kSecondsPerMinute + second + fraction;
    return true;
}

CsvParseResult PhasedArrayLogParser::parse(const std::filesystem::path& path)
{
    CsvParseResult result;

    LineSource source(path);
    if (!source.isOpen())
    {
        return result;
    }

    std::string header_line;
    if (!source.next(header_line) || header_line.empty())
    {
        return result;
    }
    std::vector<std::string_view> columns;
    splitKeepEmpty(header_line, ',', columns);
    if (columns.size() <= kLeadingColumns || textparse::trimmed(std::string(columns[0])) != "Time" ||
        textparse::trimmed(std::string(columns[1])) != "Name" || textparse::trimmed(std::string(columns[2])) != "Polarity")
    {
        return result;
    }
    const std::size_t stream_count = columns.size() - kLeadingColumns;
    std::vector<std::string> stream_names(stream_count);
    for (std::size_t j = 0; j < stream_count; ++j)
    {
        stream_names[j] = textparse::trimmed(std::string(columns[kLeadingColumns + j]));
        if (stream_names[j].empty())
        {
            stream_names[j] = "Stream " + std::to_string(j + 1);
        }
    }

    // One block of three series per stream, made when a unit's first row is read, so a log of several units keeps them apart.
    std::vector<std::string> unit_names;
    std::vector<std::vector<CsvSeries>> unit_series;   // [unit][stream * 3 + polarization]

    bool have_base = false;
    long long first_day = 0;
    double first_seconds = 0.0;
    double x_max = 0.0;
    std::string line;
    std::vector<std::string_view> fields;
    while (source.next(line))
    {
        if (line.empty())
        {
            continue;
        }
        splitKeepEmpty(line, ',', fields);
        if (fields.size() < kLeadingColumns + stream_count)
        {
            ++result.skippedRows;
            continue;
        }
        long long day = 0;
        int day_of_year = 0;
        double seconds = 0.0;
        const int polarization = polarizationIndex(textparse::trimmed(std::string(fields[2])));
        if (polarization < 0 || !parseIsoUtc(textparse::trimmed(std::string(fields[0])), day, day_of_year, seconds))
        {
            ++result.skippedRows;
            continue;
        }
        if (!have_base)
        {
            have_base = true;
            first_day = day;
            first_seconds = seconds;
            result.baseDay = day_of_year;
            result.baseTimeOffset = seconds;
        }
        const double elapsed = static_cast<double>(day - first_day) * TimeConstants::kSecondsPerDay + (seconds - first_seconds);

        const std::string unit = textparse::trimmed(std::string(fields[1]));
        std::size_t unit_index = 0;
        while (unit_index < unit_names.size() && unit_names[unit_index] != unit)
        {
            ++unit_index;
        }
        if (unit_index == unit_names.size())
        {
            unit_names.push_back(unit);
            unit_series.emplace_back(stream_count * 3);
            for (std::size_t j = 0; j < stream_count; ++j)
            {
                for (int p = 0; p < 3; ++p)
                {
                    CsvSeries& s = unit_series[unit_index][j * 3 + static_cast<std::size_t>(p)];
                    s.metricType = MetricType::SNR;
                    s.streamLabel = unit;
                    s.receiverIndex = static_cast<int>(j) + 1;
                    s.channelIndex = p;
                    s.streamOrder = static_cast<int>(unit_index) + 1;
                }
            }
        }
        for (std::size_t j = 0; j < stream_count; ++j)
        {
            double value = 0.0;
            if (!textparse::parseDouble(std::string(fields[kLeadingColumns + j]), value))
            {
                continue;   // an empty cell is no sample of that series
            }
            CsvSeries& s = unit_series[unit_index][j * 3 + static_cast<std::size_t>(polarization)];
            s.xValues.push_back(elapsed);
            s.yValues.push_back(value);
            s.yMin = runningMin(s.yMin, value);
            s.yMax = runningMax(s.yMax, value);
            x_max = std::max(x_max, elapsed);
        }
    }

    // Names last, once it is known whether the log has more than one unit to tell apart; series with no sample are left out.
    const bool several_units = unit_names.size() > 1;
    for (std::size_t u = 0; u < unit_series.size(); ++u)
    {
        for (std::size_t j = 0; j < stream_count; ++j)
        {
            for (std::size_t p = 0; p < 3; ++p)
            {
                CsvSeries& s = unit_series[u][j * 3 + p];
                if (s.xValues.empty())
                {
                    continue;
                }
                s.name = (several_units ? unit_names[u] + " " : std::string()) + stream_names[j] + " " + kPolarizationLetters[p];
                result.series.push_back(std::move(s));
            }
        }
    }
    if (result.series.empty())
    {
        return result;
    }
    result.xMax = x_max;
    result.success = true;
    return result;
}
