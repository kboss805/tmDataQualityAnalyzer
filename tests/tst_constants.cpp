#include "tst_constants.h"

#include <QtTest>

#include "constants.h"

void TestConstants::pcmMaxChannelCount()
{
    QCOMPARE(PCMConstants::kMaxChannelCount, 0x10000);
}

void TestConstants::pcmDefaultFrameSync()
{
    QCOMPARE(QString(PCMConstants::kDefaultFrameSync), QString("A345CA5C"));
}

void TestConstants::pcmCommonWordLen()
{
    QCOMPARE(PCMConstants::kCommonWordLen, 16);
}

void TestConstants::pcmNumMinorFrames()
{
    QCOMPARE(PCMConstants::kNumMinorFrames, 1);
}

void TestConstants::pcmTimeRoundingOffset()
{
    QCOMPARE(PCMConstants::kTimeRoundingOffset, 0.0005);
}

void TestConstants::pcmChannelTypeIdentifiers()
{
    QCOMPARE(QString(PCMConstants::kChannelTypeTime), QString("TIMEIN"));
    QCOMPARE(QString(PCMConstants::kChannelTypePcm), QString("PCMIN"));
}

void TestConstants::uiDefaultSlopeIndex()
{
    QCOMPARE(UIConstants::kDefaultSlopeIndex, 3);
}

void TestConstants::uiDefaultScale()
{
    QCOMPARE(QString(UIConstants::kDefaultScale), QString("20"));
}

void TestConstants::uiTimeValidationLimits()
{
    QCOMPARE(UIConstants::kMinDayOfYear, 1);
    QCOMPARE(UIConstants::kMaxDayOfYear, 366);
    QCOMPARE(UIConstants::kMaxHour, 23);
    QCOMPARE(UIConstants::kMaxMinute, 59);
    QCOMPARE(UIConstants::kMaxSecond, 59);
}

void TestConstants::uiSamplePeriods()
{
    QCOMPARE(UIConstants::kSamplePeriod1s,    1.0);
    QCOMPARE(UIConstants::kSamplePeriod100ms, 0.1);
    QCOMPARE(UIConstants::kSamplePeriod10ms,  0.01);
}

void TestConstants::uiMaxSamplePeriodIndex()
{
    QCOMPARE(UIConstants::kMaxSamplePeriodIndex, 2);
}

void TestConstants::uiChannelPrefixes()
{
    QCOMPARE(QString(UIConstants::kChannelPrefixes[0]), QString("L"));
    QCOMPARE(QString(UIConstants::kChannelPrefixes[1]), QString("R"));
    QCOMPARE(QString(UIConstants::kChannelPrefixes[2]), QString("C"));
}

void TestConstants::uiNumKnownPrefixes()
{
    QCOMPARE(UIConstants::kNumKnownPrefixes, 3);
}

// v2.0 additions

void TestConstants::appVersionValues()
{
    // Verify the version components are non-negative and consistent
    QVERIFY(AppVersion::kMajor >= 0);
    QVERIFY(AppVersion::kMinor >= 0);
    QVERIFY(AppVersion::kPatch >= 0);
}

void TestConstants::appVersionToString()
{
    // Verify toString() formats the three constants correctly — no hardcoded version
    QString expected = QString("%1.%2.%3")
        .arg(AppVersion::kMajor)
        .arg(AppVersion::kMinor)
        .arg(AppVersion::kPatch);
    QCOMPARE(AppVersion::toString(), expected);
}

void TestConstants::pcmMaxRawSampleValue()
{
    QCOMPARE(PCMConstants::kMaxRawSampleValue, static_cast<uint16_t>(0xFFFF));
}

void TestConstants::pcmDefaultBufferSize()
{
    QCOMPARE(PCMConstants::kDefaultBufferSize, 65536UL);
}

void TestConstants::pcmProgressReportInterval()
{
    QCOMPARE(PCMConstants::kProgressReportIntervalMs, 100);
}

void TestConstants::uiQSettingsKeys()
{
    QCOMPARE(QString(UIConstants::kOrganizationName), QString("tmDataQualityAnalyzer"));
    QCOMPARE(QString(UIConstants::kApplicationName), QString("tmDataQualityAnalyzer"));
    QCOMPARE(QString(UIConstants::kSettingsKeyTheme), QString("Theme"));
    QCOMPARE(QString(UIConstants::kSettingsKeyLastCh10Dir), QString("LastCh10Directory"));
    QCOMPARE(QString(UIConstants::kSettingsKeyLastTomlDir), QString("LastTomlDirectory"));
}

void TestConstants::uiThemeIdentifiers()
{
    QCOMPARE(QString(UIConstants::kThemeDark), QString("dark"));
    QCOMPARE(QString(UIConstants::kThemeLight), QString("light"));
}

void TestConstants::uiLayoutConstants()
{
    QCOMPARE(UIConstants::kFlatButtonMinWidth, 90);
    QCOMPARE(UIConstants::kLogPreviewHeight, 80);
}

void TestConstants::uiTimeConversionConstants()
{
    QCOMPARE(UIConstants::kSecondsPerDay, 86400);
    QCOMPARE(UIConstants::kSecondsPerHour, 3600);
    QCOMPARE(UIConstants::kSecondsPerMinute, 60);
}

void TestConstants::uiPolarityConstants()
{
    QCOMPARE(UIConstants::kDefaultPolarityIndex, 0);
}

// v2.2 additions

void TestConstants::uiRecentFilesConstants()
{
    QCOMPARE(QString(UIConstants::kSettingsKeyRecentFiles), QString("RecentFiles"));
    QCOMPARE(UIConstants::kMaxRecentFiles, 5);
}

// v2.3 additions

void TestConstants::uiDeploymentConstants()
{
    QCOMPARE(QString(UIConstants::kPortableMarkerFilename), QString("portable"));
    QCOMPARE(QString(UIConstants::kSettingsDirName), QString("settings"));
    QCOMPARE(QString(UIConstants::kDefaultTomlFilename), QString("default.toml"));
    QCOMPARE(QString(UIConstants::kReceiverParamsDirName), QString("receiver_params"));
    QCOMPARE(QString(UIConstants::kRcvrCalsDirName), QString("rcvr_cals"));
    QCOMPARE(QString(UIConstants::kFramesyncPatternsDirName), QString("framesync_patterns"));
}

// v2.4 additions

void TestConstants::uiOutputFilenameConstants()
{
    QCOMPARE(UIConstants::kFileListMinHeight, 180);
}

// v3.0 additions

void TestConstants::plotConstants()
{
    QCOMPARE(PlotConstants::kPlotDockMinWidth, 1024);
    QCOMPARE(PlotConstants::kAxisMarginFactor, 0.05);
    QCOMPARE(QString(PlotConstants::kDefaultPlotTitle), QString("Framesync/SNR Plot"));
    QCOMPARE(QString(PlotConstants::kYAxisLabel),  QString("Framesync Lock (%)"));
    QCOMPARE(QString(PlotConstants::kMissedFramesAxisLabel), QString("Accumulated Missed Frames"));
    QCOMPARE(QString(PlotConstants::kSnrAxisLabel), QString("Receiver SNR (dB)"));
    QCOMPARE(QString(PlotConstants::kXAxisLabel), QString("Elapsed Time (DDD:HH:MM:SS)"));
    QCOMPARE(PlotConstants::kZoomFactor, 0.1);
    QCOMPARE(PlotConstants::kNumSnrPrimaryColors, 3);

    // Theme colors
    QCOMPARE(PlotConstants::kDarkBackground, QColor(32, 32, 32));
    QCOMPARE(PlotConstants::kLightBackground, QColor(255, 255, 255));
    QCOMPARE(PlotConstants::kDarkForeground, QColor(220, 220, 220));
    QCOMPARE(PlotConstants::kLightForeground, QColor(30, 30, 30));
    QCOMPARE(PlotConstants::kDarkGridColor, QColor(60, 60, 60));
    QCOMPARE(PlotConstants::kLightGridColor, QColor(200, 200, 200));

    // Plot widget parameters
    QCOMPARE(PlotConstants::kTickCount, 10);
    QCOMPARE(PlotConstants::kGraphPenWidth, 1.5);
    QCOMPARE(PlotConstants::kTitleFontSize, 10);
    QCOMPARE(PlotConstants::kSpinBoxMaxRange, 1e9);
    QCOMPARE(PlotConstants::kYSpinBoxMax, 999.0);
}

// v3.2 additions

void TestConstants::pcmFrameSyncHexPattern()
{
    QCOMPARE(QString(PCMConstants::kFrameSyncHexPattern), QString("^[0-9A-Fa-f]+$"));
}

// v3.3 additions (US1.1 / US2.1 — frame sync mask)

void TestConstants::pcmFrameSyncMaskConstants()
{
    QCOMPARE(QString(PCMConstants::kDefaultFrameSyncMask), QString("FFFFFFFF"));
    QCOMPARE(PCMConstants::kMaxSyncPatternBits, 64);
}

void TestConstants::pcmFrameLengthBoundsConstants()
{
    QCOMPARE(PCMConstants::kMinFrameLengthBits,   64);
    QCOMPARE(PCMConstants::kMaxFrameLengthBits, 65536);
}

// v3.4 additions — previously untested constants

void TestConstants::pcmMaxPacketBufferSize()
{
    // Guard against malformed CH10 headers; 100 MB cap.
    QCOMPARE(PCMConstants::kMaxPacketBufferSize, static_cast<qsizetype>(100 * 1024 * 1024));
}

void TestConstants::uiDefaultSamplePeriodIndex()
{
    // Default is 1 s (index 0 in the 1s/100ms/10ms combo).
    QCOMPARE(UIConstants::kDefaultSamplePeriodIndex, 0);
}

void TestConstants::uiMaxSlopeIndex()
{
    // Four voltage-range options (0-indexed); max valid index is 3.
    QCOMPARE(UIConstants::kMaxSlopeIndex, 3);
}

void TestConstants::uiSlopeVoltageBounds()
{
    // Voltage bounds for each slope index (US1.1 — voltage range options).
    QCOMPARE(UIConstants::kSlopeVoltageLower[0], -10.0);
    QCOMPARE(UIConstants::kSlopeVoltageLower[1],  -5.0);
    QCOMPARE(UIConstants::kSlopeVoltageLower[2],   0.0);
    QCOMPARE(UIConstants::kSlopeVoltageLower[3],   0.0);

    QCOMPARE(UIConstants::kSlopeVoltageUpper[0], 10.0);
    QCOMPARE(UIConstants::kSlopeVoltageUpper[1],  5.0);
    QCOMPARE(UIConstants::kSlopeVoltageUpper[2], 10.0);
    QCOMPARE(UIConstants::kSlopeVoltageUpper[3],  5.0);
}

void TestConstants::plotFrameSyncLockColor()
{
    // Distinctive purple used to render framesync lock series (US6.0).
    QCOMPARE(PlotConstants::kFrameSyncLockColor, QColor(106, 13, 173));
}
