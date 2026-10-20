/**
 * @file tst_eventreporter.h
 * @brief Tests for EventReporter, the null-safe forwarder every processing source reports through.
 */

#ifndef TST_EVENTREPORTER_H
#define TST_EVENTREPORTER_H

#include <QObject>

class TestEventReporter : public QObject
{
    Q_OBJECT

private slots:
    void withNoSinkEveryCallIsHarmless();
    void eachCallReachesItsOwnSinkMethodWithItsArgument();
    void callsArriveInTheOrderTheyWereMade();
    void aReplacedSinkReceivesLaterCallsAndTheOldOneNone();
    void clearingTheSinkSilencesTheReporter();
    void theReporterKeepsTheSinkAlive();
};

#endif // TST_EVENTREPORTER_H
