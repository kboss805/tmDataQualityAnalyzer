/**
 * @file clocktime.h
 * @brief Formatting of an elapsed time as the "DDD:HH:MM:SS[.mmm]" clock stamp.
 *
 * The plot's X axis, its readouts and the CSV export all show a time as the day-of-year and time of
 * day it falls on, counted from the recording's first sample. The CSV export writes the stamp and the
 * importer reads it back, so the formatting lives in the core beside them; the C# `ClockTime`
 * (`Formats/ClockTime.cs`) is a thin wrapper over it, through the C ABI.
 */

#ifndef CLOCKTIME_H
#define CLOCKTIME_H

#include <cstdint>
#include <string>

namespace clocktime {

/// @p elapsed seconds after the recording's first sample as "DDD:HH:MM:SS", with ".mmm" appended when
/// @p includeMilliseconds. The first sample is at day @p baseDay, @p baseTimeOffset seconds after
/// midnight. Fields are zero-padded to 3, 2, 2, 2 and 3 digits and grow if the value is wider; a time
/// before the first day borrows from the day count, so the clock never reads negative.
std::string format(int baseDay, double baseTimeOffset, double elapsed, bool includeMilliseconds);

/// The stamp "DDD:HH:MM:SS.mmm" of @p milliseconds after midnight of day @p baseDay, worked in whole integers (no
/// floating-point sum to land a millisecond short); a negative count borrows from the day count as `format` does.
std::string formatMilliseconds(int baseDay, std::int64_t milliseconds);

} // namespace clocktime

#endif // CLOCKTIME_H
