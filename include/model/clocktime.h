/**
 * @file clocktime.h
 * @brief Qt-free formatting of an elapsed time as the "DDD:HH:MM:SS[.mmm]" clock stamp.
 *
 * The plot's X axis, its readouts and the CSV export all show a time as the day-of-year and time of
 * day it falls on, counted from the recording's first sample. The CSV export writes the stamp and the
 * importer reads it back, so the formatting lives in the core beside them; `PlotViewModel::formatTime`
 * is a thin wrapper.
 */

#ifndef CLOCKTIME_H
#define CLOCKTIME_H

#include <string>

namespace clocktime {

/// @p elapsed seconds after the recording's first sample as "DDD:HH:MM:SS", with ".mmm" appended when
/// @p includeMilliseconds. The first sample is at day @p baseDay, @p baseTimeOffset seconds after
/// midnight. Fields are zero-padded to 3, 2, 2, 2 and 3 digits and grow if the value is wider; a time
/// before the first day borrows from the day count, so the clock never reads negative.
std::string format(int baseDay, double baseTimeOffset, double elapsed, bool includeMilliseconds);

} // namespace clocktime

#endif // CLOCKTIME_H
