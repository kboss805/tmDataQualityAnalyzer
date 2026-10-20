/**
 * @file irig106lock.h
 * @brief One process-wide lock for the part of the vendored irig106 library that is not thread-safe.
 *
 * The library keeps global state it never locks: the table that hands out file handles (claimed
 * with a check-then-set, so two simultaneous opens can be given the same slot), the one-time
 * initialisation of that table, and the TMATS decoder's static pointers. Reading packets from a
 * handle that is already open touches only that handle, so it needs no lock - which is why this
 * covers each reader's SETUP (open, time sync, TMATS read and decode) and nothing after it.
 *
 * The library may not be edited (lib/irig106 is third-party), so the lock lives here, in the
 * code that calls it. Both readers take it; anything new that opens a recording must too.
 */

#ifndef IRIG106LOCK_H
#define IRIG106LOCK_H

#include <mutex>

/// The lock itself, for a test that needs to hold it or to see that someone else does.
[[nodiscard]] inline std::mutex& irig106SetupMutex()
{
    static std::mutex mutex;   // one instance across translation units (inline function)
    return mutex;
}

/// Holds the lock until the returned object is destroyed. Take it for a whole setup phase.
[[nodiscard]] inline std::unique_lock<std::mutex> lockIrig106Setup()
{
    return std::unique_lock<std::mutex>(irig106SetupMutex());
}

#endif // IRIG106LOCK_H
