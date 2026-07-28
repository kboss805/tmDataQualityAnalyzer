#ifndef TST_CONSTANTS_H
#define TST_CONSTANTS_H

#include <QObject>

class TestConstants : public QObject
{
    Q_OBJECT

private slots:
    void pcmMaxChannelCount();
    void pcmDefaultFrameSync();
    void pcmCommonWordLen();
    void pcmNumMinorFrames();
    void pcmChannelTypeIdentifiers();
    void uiDefaultSlopeIndex();
    void uiSamplePeriods();
    void uiMaxSamplePeriodIndex();
    void uiChannelPrefixes();
    void uiNumKnownPrefixes();

    // v2.0 additions
    void appVersionValues();
    void appVersionToString();
    void pcmMaxRawSampleValue();
    void pcmDefaultBufferSize();
    void pcmProgressReportInterval();
    void uiQSettingsKeys();
    void uiThemeIdentifiers();
    void uiLayoutConstants();
    void uiTimeConversionConstants();
    void uiPolarityConstants();

    // v2.2 additions
    void uiRecentFilesConstants();

    // v2.3 additions
    void uiDeploymentConstants();

    // v2.4 additions
    void uiOutputFilenameConstants();

    // v3.0 additions
    void plotConstants();

    // v3.2 additions
    void pcmFrameSyncHexPattern();

    // v3.3 additions (US2.1 / US2.0 — frame sync mask)
    void pcmFrameSyncMaskConstants();
    void pcmFrameLengthBoundsConstants();

    // v3.4 additions — previously untested constants
    void pcmMaxPacketBufferSize();
    void uiDefaultSamplePeriodIndex();
    void uiMaxSlopeIndex();
    void uiSlopeVoltageBounds();
    void plotFrameSyncLockColor();
};

#endif // TST_CONSTANTS_H
