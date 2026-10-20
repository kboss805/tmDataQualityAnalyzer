/**
 * @file tst_ch10packetreader.h
 * @brief Tests for Ch10PacketReader, the single pass that feeds every stream's queue.
 *
 * The reader had no tests of its own: it was exercised only by whole-pipeline runs, where a
 * wrong message, a missing progress value or a missing end marker shows up as something
 * else failing far away. These cases pin what it reports and what it hands to the queues,
 * directly. They were written against the signal-based reader first and kept, assertion
 * for assertion, across the move to a plain class that reports through ProcessingEvents.
 */

#ifndef TST_CH10PACKETREADER_H
#define TST_CH10PACKETREADER_H

#include <QObject>

class TestCh10PacketReader : public QObject
{
    Q_OBJECT

private slots:
    void prepareReportsWhyItFailed_data();
    void prepareReportsWhyItFailed();
    void aFullRunReportsItsStartAndEndsAtOneHundredPercent();
    void cancelReportsItAndStillEndsTheStream();
    void aTruncatedFileReportsAReadErrorAndStillEndsTheStream_data();
    void aTruncatedFileReportsAReadErrorAndStillEndsTheStream();
    void aDamagedHeaderStopsTheReadOrCostsOnePacket_data();
    void aDamagedHeaderStopsTheReadOrCostsOnePacket();
    void everyQueueGetsEveryPacketAndTheyShareOneCopy();
};

#endif // TST_CH10PACKETREADER_H
