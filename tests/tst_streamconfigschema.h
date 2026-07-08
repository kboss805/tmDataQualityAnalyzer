/**
 * @file tst_streamconfigschema.h
 * @brief Unit tests for StreamConfigSchema -- the StreamConfig JSON format and
 *        its inverse parser, pinned as a round-trip (session save/load, Phase 6).
 */

#ifndef TST_STREAMCONFIGSCHEMA_H
#define TST_STREAMCONFIGSCHEMA_H

#include <QObject>

class TestStreamConfigSchema : public QObject
{
    Q_OBJECT

private slots:
    void roundTripFrameSyncLockConfig();
    void roundTripReceiverChannelInfoConfig();
    void roundTripPreservesCalibrationInputReferences();
    void toJsonNeverIncludesCalibrationByWord();
    void toJsonOmitsCalibrationBlockWhenNoCalCh10Path();
    void fromJsonRejectsObjectMissingRequiredFields();
    void fromJsonLeavesDefaultsWhenOptionalFieldsMissing();
};

#endif // TST_STREAMCONFIGSCHEMA_H
