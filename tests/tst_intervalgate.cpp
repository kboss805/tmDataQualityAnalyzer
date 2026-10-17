#include "tst_intervalgate.h"

#include <chrono>
#include <thread>

#include <QtTest>

#include "intervalgate.h"

namespace {

using Clock = IntervalGate::Clock;
using namespace std::chrono_literals;

/// A fixed base so every case reasons in exact offsets from it. Real clock reads are
/// reserved for the one case that is about the default clock.
Clock::time_point base()
{
    return Clock::time_point{} + std::chrono::hours(1);
}

} // namespace

void TestIntervalGate::notDueBeforeTheIntervalHasElapsed()
{
    const auto t0 = base();
    const IntervalGate gate(100ms, t0);

    QVERIFY(!gate.due(t0));
    QVERIFY(!gate.due(t0 + 1ms));
    QVERIFY(!gate.due(t0 + 99ms));
}

void TestIntervalGate::dueAtExactlyTheInterval()
{
    // `>=`, not `>`: QElapsedTimer::elapsed() >= interval is what this replaced, so the
    // boundary instant itself must count. A throttle that needs one extra tick drifts
    // late on every cycle.
    const auto t0 = base();
    const IntervalGate gate(100ms, t0);

    QVERIFY(gate.due(t0 + 100ms));
}

void TestIntervalGate::dueLongAfterTheInterval()
{
    const auto t0 = base();
    IntervalGate gate(100ms, t0);

    QVERIFY(gate.due(t0 + 10s));
    // due() only REPORTS; it must not restart the interval. The caller decides when to
    // reset(), and the reader does so only after it has actually emitted progress.
    QVERIFY(gate.due(t0 + 10s));
    QVERIFY(gate.due(t0 + 10s + 1ms));
}

void TestIntervalGate::intervalIsMillisecondsNotSecondsOrMicroseconds()
{
    // kProgressReportIntervalMs is an int of milliseconds. Reading it as seconds makes
    // the throttle effectively never fire; as microseconds, effectively every packet.
    // Neither is caught by a build or by the pipeline tests.
    const auto t0 = base();
    const IntervalGate gate(100ms, t0);

    QVERIFY2(!gate.due(t0 + 99'999us), "99.999 ms must not be enough for a 100 ms interval");
    QVERIFY2(gate.due(t0 + 100'000us), "100,000 us is exactly 100 ms");
    QVERIFY2(!gate.due(t0 + 100us), "100 us must not satisfy a 100 ms interval");
}

void TestIntervalGate::resetRestartsTheInterval()
{
    const auto t0 = base();
    IntervalGate gate(100ms, t0);
    QVERIFY(gate.due(t0 + 150ms));

    gate.reset(t0 + 150ms);

    // Measured from the reset, not from construction.
    QVERIFY(!gate.due(t0 + 150ms));
    QVERIFY(!gate.due(t0 + 249ms));
    QVERIFY(gate.due(t0 + 250ms));
}

void TestIntervalGate::subMillisecondPrecisionDecidesTheBoundary()
{
    // QElapsedTimer truncated to whole milliseconds before comparing, so 99.999999 ms
    // read as 99 and was not due. Comparing at full resolution must agree on every side
    // of the boundary, or the two implementations would disagree by up to a millisecond.
    const auto t0 = base();
    const IntervalGate gate(100ms, t0);

    QVERIFY(!gate.due(t0 + 100ms - 1ns));
    QVERIFY(gate.due(t0 + 100ms));
    QVERIFY(gate.due(t0 + 100ms + 1ns));
}

void TestIntervalGate::defaultClockIsNotDueImmediatelyThenBecomesDue()
{
    // The only case that touches the real clock, and built to be insensitive to scheduling:
    // the "not due" claims use a 5 s interval (a stall that long would be a broken
    // machine, not a flaky test) and the "due" claim sleeps three times its interval.

    // A gate with no explicit start must begin NOW. A default of the clock's epoch would
    // make it due the instant it exists - and a throttle that is always due throttles
    // nothing.
    IntervalGate fresh(5s);
    QVERIFY2(!fresh.due(), "a gate with no explicit start must begin at the current time");

    IntervalGate soon(20ms);
    std::this_thread::sleep_for(60ms);
    QVERIFY(soon.due());

    // And reset() with no argument also means "now". Started STALE (the clock's epoch is
    // long past), so it is due until reset() runs - which makes reset() the only thing
    // that can make the second check pass. Starting it at "now" would pass even if
    // reset() did nothing.
    IntervalGate stale(5s, Clock::time_point{});
    QVERIFY(stale.due());
    stale.reset();
    QVERIFY2(!stale.due(), "reset() with no argument must restart the interval from now");
}
