/**
 * @file tst_qtprocessingevents.h
 * @brief Tests for QtProcessingEvents, the hand-off from a processing thread to Qt.
 *
 * The core calls ProcessingEvents on whatever thread is processing; the UI wants those
 * calls on its own thread, in order. That used to be Qt's queued-signal machinery; it is
 * now this class, so the three properties the pipeline depends on are pinned here
 * directly rather than only incidentally through a whole run: the right thread, the
 * right order, and nothing delivered to a target that has gone.
 */

#ifndef TST_QTPROCESSINGEVENTS_H
#define TST_QTPROCESSINGEVENTS_H

#include <QObject>

class TestQtProcessingEvents : public QObject
{
    Q_OBJECT

private slots:
    void deliversOnTheTargetsThreadInOrder();
    void progressArrivesOnTheTargetsThreadInOrder();
    void messagesAreDecodedFromUtf8();
    void missingHandlersAreSkipped();
    void eventsForADestroyedTargetAreDropped();
};

#endif // TST_QTPROCESSINGEVENTS_H
