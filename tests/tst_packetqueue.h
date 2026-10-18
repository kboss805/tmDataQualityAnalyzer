/**
 * @file tst_packetqueue.h
 * @brief Direct tests for PacketQueue, the bounded reader-to-worker hand-off.
 *
 * Until this suite existed the queue was only exercised incidentally, through the full
 * reader + FrameProcessor pipeline. That was tolerable while it was built on Qt's
 * QMutex / QWaitCondition / QQueue and became not tolerable the moment those were
 * swapped for std primitives: a lost wakeup or a wrong fullness test shows up in the
 * pipeline as a hang or a silently shortened stream, far from its cause.
 */

#ifndef TST_PACKETQUEUE_H
#define TST_PACKETQUEUE_H

#include <QObject>

class TestPacketQueue : public QObject
{
    Q_OBJECT

private slots:
    void itemsComeOutInTheOrderTheyWentIn();
    void everyFieldOfAnItemSurvivesTheQueue();
    void oneItemOnSeveralQueuesSharesItsBytes();
    void producerBlocksWhenFullAndResumesAfterADequeue();
    void consumerBlocksWhenEmptyAndResumesAfterAnEnqueue();
    void closeReleasesABlockedProducer();
    void closeReleasesABlockedConsumerWithAnEndOfStreamSentinel();
    void closedQueueDrainsItsItemsBeforeReportingEndOfStream();
    void enqueueAfterCloseDiscardsAndReportsFalse();
    void nonPositiveDepthFallsBackToTheDefault();
    void queueNeverHoldsMoreThanItsDepth();
    void oneProducerOneConsumerKeepEveryItemInOrder();
};

#endif // TST_PACKETQUEUE_H
