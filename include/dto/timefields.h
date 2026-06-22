/**
 * @file timefields.h
 * @brief Lightweight value type for day-of-year time components (DDD:HH:MM:SS).
 */

#ifndef TIMEFIELDS_H
#define TIMEFIELDS_H

/// @brief Day-of-year time components (DDD:HH:MM:SS).
struct TimeFields {
    int ddd = 0;  ///< Day of year (1-366).
    int hh  = 0;  ///< Hour (0-23).
    int mm  = 0;  ///< Minute (0-59).
    int ss  = 0;  ///< Second (0-59).
};

#endif // TIMEFIELDS_H
