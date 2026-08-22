/**
 * @file tst_processingtemplateschema.h
 * @brief Tests for ProcessingTemplateSchema (Batch Apply).
 */

#ifndef TST_PROCESSINGTEMPLATESCHEMA_H
#define TST_PROCESSINGTEMPLATESCHEMA_H

#include <QObject>

class TestProcessingTemplateSchema : public QObject
{
    Q_OBJECT

private slots:
    void roundTripFrameSyncLockEntry();
    void roundTripReceiverChannelInfoEntry();
    void roundTripPreservesAppearance();
    void toJsonOmitsAppearanceWhenEmpty();
    void roundTripPreservesCalibrationInputReferences();
    void toJsonNeverIncludesCalibrationByWord();
    void fromJsonAcceptsCurrentSchemaVersion();
    void fromJsonRejectsNewerSchemaVersion();
    void fromJsonRejectsMissingSchemaVersion();
    void fromJsonRejectsMissingEntriesArray();
    void fromJsonRejectsNonObjectDocument();
    void swapBytesRoundTripsAndDefaultsTrue();
};

#endif // TST_PROCESSINGTEMPLATESCHEMA_H
