/**
 * @file tst_intervalgate.h
 * @brief Tests for IntervalGate, the progress throttle that replaced QElapsedTimer.
 *
 * The throttle sits in the middle of Ch10PacketReader's per-packet loop, where it cannot
 * be observed: the recordings in the suite read faster than the 100 ms interval it
 * enforces. So the contract is pinned here instead, with injected time points - no
 * sleeps for anything that matters, and therefore nothing to flake on a loaded runner.
 */

#ifndef TST_INTERVALGATE_H
#define TST_INTERVALGATE_H

#include <QObject>

class TestIntervalGate : public QObject
{
    Q_OBJECT

private slots:
    void notDueBeforeTheIntervalHasElapsed();
    void dueAtExactlyTheInterval();
    void dueLongAfterTheInterval();
    void intervalIsMillisecondsNotSecondsOrMicroseconds();
    void resetRestartsTheInterval();
    void subMillisecondPrecisionDecidesTheBoundary();
    void defaultClockIsNotDueImmediatelyThenBecomesDue();
};

#endif // TST_INTERVALGATE_H
