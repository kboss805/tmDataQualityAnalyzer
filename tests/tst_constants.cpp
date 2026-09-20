#include "tst_constants.h"

#include <QtTest>

#include "constants.h"

void TestConstants::pcmMaxChannelCount()
{
    QCOMPARE(PCMConstants::kMaxChannelCount, 0x10000);
}

void TestConstants::pcmDefaultFrameSync()
{
    // PRN15. Both halves are pinned because they are only meaningful as a pair: a
    // pattern with the wrong frame length never locks, and nothing else in the build
    // would catch them drifting apart.
    QCOMPARE(QString(PCMConstants::kDefaultFrameSync), QString("334AABBF"));
    QCOMPARE(PCMConstants::kDefaultBitsPerFrame, 32767);
}

void TestConstants::pcmCommonWordLen()
{
    QCOMPARE(PCMConstants::kCommonWordLen, 16);
}

void TestConstants::pcmNumMinorFrames()
{
    QCOMPARE(PCMConstants::kNumMinorFrames, 1);
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
    QCOMPARE(PlotConstants::kLockAxisMin, 0.0);
    QCOMPARE(PlotConstants::kLockAxisMax, 100.0);
    QCOMPARE(QString(PlotConstants::kDefaultPlotTitle), QString("Framesync/SNR Plot"));
    QCOMPARE(QString(PlotConstants::kYAxisLabel),  QString("Framesync Lock (%)"));
    QCOMPARE(QString(PlotConstants::kMissedFramesAxisLabel), QString("Accumulated Missed Frames"));
    QCOMPARE(QString(PlotConstants::kSnrAxisLabel), QString("Receiver SNR (dB)"));
    QCOMPARE(QString(PlotConstants::kXAxisLabel), QString("Time (DDD:HH:MM:SS)"));

    // The sync-pattern field's length limit is derived from the bit limit, not
    // spelled out separately - raising kMaxSyncPatternBits must widen the field.
    QCOMPARE(PCMConstants::kBitsPerHexDigit, 4);
    QCOMPARE(PCMConstants::kMaxSyncPatternHexChars,
             PCMConstants::kMaxSyncPatternBits / PCMConstants::kBitsPerHexDigit);
    QCOMPARE(PCMConstants::kMaxSyncPatternHexChars, 16);
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
}

// v3.2 additions

void TestConstants::pcmFrameSyncHexPattern()
{
    QCOMPARE(QString(PCMConstants::kFrameSyncHexPattern), QString("^[0-9A-Fa-f]+$"));
}

// v3.3 additions (US2.1 / US2.0 — frame sync mask)

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
    // Voltage bounds for each slope index (US2.1 — voltage range options).
    QCOMPARE(UIConstants::kSlopeVoltageLower[0], -10.0);
    QCOMPARE(UIConstants::kSlopeVoltageLower[1],  -5.0);
    QCOMPARE(UIConstants::kSlopeVoltageLower[2],   0.0);
    QCOMPARE(UIConstants::kSlopeVoltageLower[3],   0.0);

    QCOMPARE(UIConstants::kSlopeVoltageUpper[0], 10.0);
    QCOMPARE(UIConstants::kSlopeVoltageUpper[1],  5.0);
    QCOMPARE(UIConstants::kSlopeVoltageUpper[2], 10.0);
    QCOMPARE(UIConstants::kSlopeVoltageUpper[3],  5.0);
}
