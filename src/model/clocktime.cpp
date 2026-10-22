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

    int day = baseDay + static_cast<int>(total / UIConstants::kSecondsPerDay);
    total = std::fmod(total, static_cast<double>(UIConstants::kSecondsPerDay));
    if (total < 0.0)
    {
        total += UIConstants::kSecondsPerDay;
        day--;
    }

    const int hours   = static_cast<int>(total) / UIConstants::kSecondsPerHour;
    const int minutes = (static_cast<int>(total) % UIConstants::kSecondsPerHour) / UIConstants::kSecondsPerMinute;
    const int seconds = static_cast<int>(total) % UIConstants::kSecondsPerMinute;

    std::string result = std::format("{:03}:{:02}:{:02}:{:02}", day, hours, minutes, seconds);

    if (includeMilliseconds)
    {
        // total's fractional part (below one second) survived the day/hour/minute/second extraction
        // above untouched, so it can be pulled out independently without disturbing that (tested)
        // integer-field logic.
        const int msec = static_cast<int>((total - std::floor(total)) * 1000.0 + 0.5);
        result += std::format(".{:03}", msec);
    }
    return result;
}

} // namespace clocktime
