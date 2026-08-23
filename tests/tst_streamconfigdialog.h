/**
 * @file tst_streamconfigdialog.h
 * @brief Unit tests for StreamConfigDialog — construction, configs() roundtrip,
 *        and the frame-sync TOML save/load contract (US5.0, US1.0, US7.0).
 */

#ifndef TST_STREAMCONFIGDIALOG_H
#define TST_STREAMCONFIGDIALOG_H

#include <QObject>

class TestStreamConfigDialog : public QObject
{
    Q_OBJECT

private slots:
    // Construction smoke tests
    void constructionWithEmptyConfigs();
    void constructionWithOneStream();

    // configs() roundtrip — values entered survive the getter
    void configsRoundtripDefaultValues();
    void configsRoundtripFrameSyncFields();
    void configsRoundtripWordsInFrame();
    void configsRoundtripProcessFlag();


    // Frame-sync TOML scope boundary (US5.0 / US1.0):
    // Save writes exactly FrameSync, FrameSyncMask, WordsInMinorFrame under [Frame].
    // Load reads back those same three keys.
    void tomlFrameSyncSaveRoundtrip();
    void tomlFrameSyncLoadPopulatesThreeFields();
    void tomlFrameSyncSaveDoesNotWriteRandomized();
    void tomlFrameSyncSaveDoesNotWriteDataRate();

    // validateAndAccept rejects a checked stream with no frame sync pattern (US7.0)
    void validateRejectsCheckedStreamWithEmptyFrameSync();

    // Data type validation and limits
    void testFrameSyncValidation();
    void testFrameMaskValidation();
    void testDataRateLimits();
    void testDefaultSampleRate();

    // "Apply to all" fan-out (US2.2)
    void applyToAllCopiesSettingsToSameModeStreams();
    void applyToAllLeavesDifferentModeStreamsUnchanged();
    void applyToAllUncheckedDoesNotAffectOtherStreams();

    // Channel column label (elides long names, keeps the full name in the tooltip)
    void channelLabelShortNameShownInFull();
    void channelLabelLongNameElidedWithFullTooltip();

    // Table header colors come from the theme QSS (object names), not hard-coded
    // inline stylesheets that only worked on the dark theme.
    void headerLabelsUseThemeableObjectNames();

    // Channel name cell is styled (via object name) to mimic the Mode combo's
    // box, and the Mode combo's displayed text is right-justified.
    void channelLabelHasComboBoxStyledObjectName();
    void modeComboDisplaysTextLeftJustified();
    void modeComboSelectionStillTracksIndexChange();
    void byteOrderToggleIsFileLevel();
    void legacyToggleIsInverseOfByteSwap();
};

#endif // TST_STREAMCONFIGDIALOG_H
