/**
 * @file clocktime.cpp
 * @brief Implementation of clocktime::format — the "DDD:HH:MM:SS[.mmm]" stamp.
 */

#include "clocktime.h"

#include <cmath>
#include <format>

#include "constants.h"

namespace clocktime {

std::string format(int baseDay, double baseTimeOffset, double elapsed, bool includeMilliseconds)
{
    double total = baseTimeOffset + elapsed;

    int day = baseDay + static_cast<int>(total / TimeConstants::kSecondsPerDay);
    total = std::fmod(total, static_cast<double>(TimeConstants::kSecondsPerDay));
    if (total < 0.0)
    {
        total += TimeConstants::kSecondsPerDay;
        day--;
    }

    const int hours   = static_cast<int>(total) / TimeConstants::kSecondsPerHour;
    const int minutes = (static_cast<int>(total) % TimeConstants::kSecondsPerHour) / TimeConstants::kSecondsPerMinute;
    const int seconds = static_cast<int>(total) % TimeConstants::kSecondsPerMinute;

    std::string result = std::format("{:03}:{:02}:{:02}:{:02}", day, hours, minutes, seconds);

    if (includeMilliseconds)
    {
        // total's fractional part (below one second) survived the day/hour/minute/second extraction
        // above untouched, so it can be pulled out independently without disturbing that (tested)
        // integer-field logic.
        // NOLINTNEXTLINE(bugprone-incorrect-roundings): the fraction is not negative, and v2.14.2 rounded it this way
        const int msec = static_cast<int>((total - std::floor(total)) * 1000.0 + 0.5);
        result += std::format(".{:03}", msec);
    }
    return result;
}

std::string formatMilliseconds(int baseDay, std::int64_t milliseconds)
{
    constexpr std::int64_t kMsPerDay = 86'400'000;
    std::int64_t days = milliseconds / kMsPerDay;
    std::int64_t rest = milliseconds % kMsPerDay;
    if (rest < 0)
    {
        rest += kMsPerDay;
        days--;
    }
    return std::format("{:03}:{:02}:{:02}:{:02}.{:03}", baseDay + static_cast<int>(days), rest / 3'600'000,
                       rest / 60'000 % 60, rest / 1000 % 60, rest % 1000);
}

} // namespace clocktime
