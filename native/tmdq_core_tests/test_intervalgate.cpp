// IntervalGate: the progress throttle that replaced QElapsedTimer in Ch10PacketReader::run(), pinned with injected time
// points so nothing depends on a sleep (one case touches the real clock, built to be insensitive to scheduling). No Qt;
// ported from the Qt TestIntervalGate, whose cases are all here under the same descriptions.

#include <chrono>
#include <thread>

#include "doctest.h"
#include "intervalgate.h"

namespace {

using Clock = IntervalGate::Clock;
using namespace std::chrono_literals;

/// A fixed base so every case reasons in exact offsets from it. Real clock reads are reserved for the one case that is
/// about the default clock.
Clock::time_point base()
{
    return Clock::time_point{} + std::chrono::hours(1);
}

} // namespace

TEST_CASE("IntervalGate: not due before the interval has elapsed")
{
    const auto t0 = base();
    const IntervalGate gate(100ms, t0);

    CHECK(!gate.due(t0));
    CHECK(!gate.due(t0 + 1ms));
    CHECK(!gate.due(t0 + 99ms));
}

TEST_CASE("IntervalGate: due at exactly the interval")
{
    // `>=`, not `>`: QElapsedTimer::elapsed() >= interval is what this replaced, so the boundary instant itself must
    // count. A throttle that needs one extra tick drifts late on every cycle.
    const auto t0 = base();
    const IntervalGate gate(100ms, t0);

    CHECK(gate.due(t0 + 100ms));
}

TEST_CASE("IntervalGate: due long after the interval")
{
    const auto t0 = base();
    IntervalGate gate(100ms, t0);

    CHECK(gate.due(t0 + 10s));
    // due() only REPORTS; it must not restart the interval. The caller decides when to reset(), and the reader does so
    // only after it has actually emitted progress.
    CHECK(gate.due(t0 + 10s));
    CHECK(gate.due(t0 + 10s + 1ms));
}

TEST_CASE("IntervalGate: interval is milliseconds, not seconds or microseconds")
{
    // kProgressReportIntervalMs is an int of milliseconds. Reading it as seconds makes the throttle effectively never
    // fire; as microseconds, effectively every packet. Neither is caught by a build or by the pipeline tests.
    const auto t0 = base();
    const IntervalGate gate(100ms, t0);

    CHECK_MESSAGE(!gate.due(t0 + 99'999us), "99.999 ms must not be enough for a 100 ms interval");
    CHECK_MESSAGE(gate.due(t0 + 100'000us), "100,000 us is exactly 100 ms");
    CHECK_MESSAGE(!gate.due(t0 + 100us), "100 us must not satisfy a 100 ms interval");
}

TEST_CASE("IntervalGate: the interval is the one it was given")
{
    // Every other case uses 100 ms, the reader's own figure, so a gate that ignored its argument and used 100 ms would
    // pass them all. Other intervals, each at its boundary.
    const auto t0 = base();
    for (const std::chrono::milliseconds interval : {1ms, 7ms, 250ms, 5000ms})
    {
        INFO(interval.count() << " ms");
        const IntervalGate gate(interval, t0);
        CHECK(!gate.due(t0 + interval - 1ns));
        CHECK(gate.due(t0 + interval));
    }
}

TEST_CASE("IntervalGate: a zero interval is always due, and a time before the start never is")
{
    const auto t0 = base();
    const IntervalGate zero(0ms, t0);
    CHECK(zero.due(t0));
    CHECK(zero.due(t0 + 1ns));

    // A reading from before the start (it cannot happen with a steady clock, but the comparison must not wrap if it is
    // ever handed one) is a negative elapsed time, which is never at least the interval.
    const IntervalGate gate(100ms, t0);
    CHECK(!gate.due(t0 - 1ms));
    CHECK(!gate.due(t0 - 10s));
}

TEST_CASE("IntervalGate: reset restarts the interval")
{
    const auto t0 = base();
    IntervalGate gate(100ms, t0);
    CHECK(gate.due(t0 + 150ms));

    gate.reset(t0 + 150ms);

    // Measured from the reset, not from construction.
    CHECK(!gate.due(t0 + 150ms));
    CHECK(!gate.due(t0 + 249ms));
    CHECK(gate.due(t0 + 250ms));

    // And again: each reset moves the start, it does not add to it.
    gate.reset(t0 + 400ms);
    CHECK(!gate.due(t0 + 499ms));
    CHECK(gate.due(t0 + 500ms));
}

TEST_CASE("IntervalGate: sub-millisecond precision decides the boundary")
{
    // QElapsedTimer truncated to whole milliseconds before comparing, so 99.999999 ms read as 99 and was not due.
    // Comparing at full resolution must agree on every side of the boundary, or the two implementations would disagree
    // by up to a millisecond.
    const auto t0 = base();
    const IntervalGate gate(100ms, t0);

    CHECK(!gate.due(t0 + 100ms - 1ns));
    CHECK(gate.due(t0 + 100ms));
    CHECK(gate.due(t0 + 100ms + 1ns));
}

TEST_CASE("IntervalGate: default clock is not due immediately, then becomes due")
{
    // The only case that touches the real clock, and built to be insensitive to scheduling: the "not due" claims use a
    // 5 s interval (a stall that long would be a broken machine, not a flaky test) and the "due" claim sleeps three
    // times its interval.

    // A gate with no explicit start must begin NOW. A default of the clock's epoch would make it due the instant it
    // exists - and a throttle that is always due throttles nothing.
    const IntervalGate fresh(5s);
    CHECK_MESSAGE(!fresh.due(), "a gate with no explicit start must begin at the current time");

    const IntervalGate soon(20ms);
    std::this_thread::sleep_for(60ms);
    CHECK(soon.due());

    // And reset() with no argument also means "now". Started STALE (the clock's epoch is long past), so it is due until
    // reset() runs - which makes reset() the only thing that can make the second check pass. Starting it at "now" would
    // pass even if reset() did nothing.
    IntervalGate stale(5s, Clock::time_point{});
    CHECK(stale.due());
    stale.reset();
    CHECK_MESSAGE(!stale.due(), "reset() with no argument must restart the interval from now");
}
