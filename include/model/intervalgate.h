/**
 * @file intervalgate.h
 * @brief A "has this much time passed?" gate on a monotonic clock, for throttling.
 *
 * Ch10PacketReader::run() uses it to throttle its progress reporting to one update per
 * PCMConstants::kProgressReportIntervalMs. Pulled out into its own type rather than
 * inlined at the call site for one reason: the throttle is the only logic there that
 * can go wrong SILENTLY - seconds where milliseconds
 * were meant, or `>` where `>=` was - and a time-based check in the middle of a
 * per-packet loop cannot be tested. The recordings in the test suite read too quickly to
 * observe a 100 ms throttle reliably. With the time point injectable the whole contract
 * can be pinned deterministically, with no sleeps and no flakiness
 * (native/tmdq_core_tests/test_intervalgate.cpp).
 *
 * `<chrono>` only, so it can live in the native core.
 *
 * `due()` is true when at least @c interval has elapsed since construction or the last
 * reset(), compared at the clock's full resolution. For a whole-millisecond interval that
 * fires at exactly the instant a comparison of the elapsed time truncated to whole
 * milliseconds would, because a truncated elapsed time reaches N ms exactly when the real
 * one does.
 */

#ifndef INTERVALGATE_H
#define INTERVALGATE_H

#include <chrono>

class IntervalGate
{
public:
    /// steady_clock, never system_clock: a throttle on elapsed time must not jump when the
    /// wall clock is adjusted.
    using Clock = std::chrono::steady_clock;

    explicit IntervalGate(std::chrono::milliseconds interval,
                          Clock::time_point start = Clock::now())
        : m_interval(interval), m_last(start)
    {
    }

    /// @return true once at least the interval has elapsed since the last reset().
    /// Reads the clock once, which is the whole per-call cost on the reader's hot path.
    bool due(Clock::time_point now = Clock::now()) const
    {
        return now - m_last >= m_interval;
    }

    /// Restarts the interval from @p now.
    void reset(Clock::time_point now = Clock::now())
    {
        m_last = now;
    }

private:
    std::chrono::milliseconds m_interval;
    Clock::time_point         m_last;
};

#endif // INTERVALGATE_H
