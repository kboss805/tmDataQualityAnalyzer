/**
 * @file seriesrange.h
 * @brief The running minimum and maximum a series' cached range is kept with.
 *
 * Not `std::min` / `std::max`, and not mirror images of each other, because of how each treats a NaN (every
 * comparison with a NaN is false):
 *  - the minimum takes the new value unless the range so far is strictly lower, so a NaN sample makes the minimum
 *    NaN, and the next sample after it replaces that NaN;
 *  - the maximum keeps the range so far unless the new value is strictly higher, so a NaN sample leaves the
 *    maximum as it was, and a NaN maximum stays NaN.
 *
 * The cached ranges of series with NaNs in them depend on exactly this, and they are held to the ones v2.14.2
 * produced (test_seriesbuild.cpp, test_csvseries.cpp).
 */

#ifndef SERIESRANGE_H
#define SERIESRANGE_H

/// The range's new minimum: `value`, unless `current` is strictly lower.
inline double runningMin(double current, double value) { return (current < value) ? current : value; }

/// The range's new maximum: `current`, unless `value` is strictly higher.
inline double runningMax(double current, double value) { return (current < value) ? value : current; }

#endif // SERIESRANGE_H
