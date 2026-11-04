# Parity review: the Avalonia application against the Qt one (plan step 5.1)

Plan step 5.1 asks for two things before the switch-over: every acceptance criterion in `docs/claude.md` checked
against the new application, and every Qt test either ported or recorded as deliberately dropped, with a reason. This is
that record, made on 2026-11-02 at `b16ad0a` (step 4.9 merged), with the tests this step added marked **(5.1)**.

**How it was made.** Four independent mappings, one per group of Qt suites and one for the criteria, each reading the
Qt test or the criterion and finding what holds it in the new code: the app tests (`TmDq.App.Tests`), the managed
tests (`TmDq.Core.Tests`), the native tests (`native/tmdq_core_tests`), and, for the installer, the package and Sandbox
scripts. Every gap, and every claim that something was met without a test, was then checked by hand, and the ported
claims spot-checked. A case counts as ported when a test fails if the behaviour it held goes, wherever that test lives:
much of what the Qt suites held through a dialog is now the core's and is held natively, which the table says.

## What it found

- **Criteria (157).** 153 are held by a test. 2 are met and checked by hand: the installer's per-user directory and
  its progress page are Inno Setup's own, and the clean-machine Sandbox installs elevated and silently, so step 5.3
  checks them on the release build. **2 were never true of either application** and are removed from the stories, at the
  user's choice:
  - **US4.0 "Frame Sync Lock curves are drawn with a visually highlighted style":** every curve has had the same 1.5 px
    pen since the QCustomPlot days; the colours tell the two kinds apart.
  - **US5.2 "a Reset button to reset the key parameters to their default values":** neither Receiver SNR dialog has
    one. Loading the shipped `settings/receiver_params/default.toml` puts the defaults back.
- **Qt test cases (278, in the 19 suites' UI and Qt-bound halves).** 254 ported, 23 dropped with a reason, 1 not held
  (below).
- **Five criteria were met but untested, and three of them now have tests;** the third of those found a difference:
  the new Open offered "All files" beside Chapter 10 recordings, so a CSV could be chosen there and fail as a recording.
  Open now offers recordings only, as the Qt Open does (US6.3: Open stays Chapter-10-only).
- **Twelve Qt cases had no counterpart, and eleven now have one:**
  - **the plot:** a left minimum set, moving the drawn line; nothing around the chart but its overlay; the
    title's notice, once a change; three recordings each earlier than the last, moving the time base twice; the axis
    titles' own words, and the right axis's title drawn only while it is shown;
  - **Configure Streams:** a long channel name trimmed with its whole name as the tooltip; the column headers in
    either theme; the Average Period, polarity and voltage-range lists as the Qt dialog has them;
  - **the batch** with no files doing nothing; **progress** reaching the dialog on the UI thread;
  - **the native projects' source lists against the files on disk** (`NativeProjectTests`, in the managed tests): every
    core and ABI source in the DLL, every test file in the native tests, and the tests building exactly the DLL's
    sources. The Qt suite's `TestCoreQtFree` checks the core's list against the `.pro`, and goes with it in 6.1; these
    checks do not need Qt, so a test file left out of the project can no longer silently never run.
- **One is not held:** `AddStream` ignoring a stream with no samples. Reaching it takes a stream that extracts frames
  yet completes no sample window, which no committed recording does (a stream that finds no sync, or no frames, is an
  error and never reaches the plot), so the guard is defensive, as the Qt one was.

**Noted, not changed:**
- **Qt upper-cased "ß" to "SS" in a pattern file's text; C#'s `ToUpperInvariant` keeps it.** A pattern or mask is
  hexadecimal, so either form is refused as invalid; nothing reaches the core differently.
- **A non-ASCII channel name** is held natively (the TMATS fixture with a patched name) and through the managed
  library's series labels ("Ch 26 é"), not by an app test reading one from a recording.
- **The legend in an exported SVG** is drawn by the renderer the PNG's test reads back pixel by pixel; the SVG's own
  test checks its size, not the legend.

## The acceptance criteria

Verdicts: **HELD** (a test fails without it), **BY HAND** (met; checked by hand, see above), **REMOVED** (never true;
the story now says what ships). Tests are in `TmDq.App.Tests` unless named as native (`native/tmdq_core_tests`, doctest
names in quotes) or managed (`TmDq.Core.Tests`).

| Story | Criterion | Verdict | Held by | Note |
|---|---|---|---|---|
| US1.0 | A dialog window is presented when the user opens… | HELD | TwoStreamsConfiguredAndProcessedAreBothDrawn, CancellingConfigureStreamsKeepsTheLastResult |  |
| US1.0 | The dialog window lists all streams identified in the ch10 | HELD | EveryPcmChannelIsARowAndNothingIsCheckedYet, ConfigureStreamsShowsARowPerChannelAndTheRecordingsSettings |  |
| US1.0 | Allows the user to select streams for SNR and/or framesync | HELD | TheReadyColumnAndProcessFollowTheRows, AReceiverSnrStreamIsProcessedAndCounted |  |
| US1.0 | Each stream row shows a Ready indicator (checkmark or X) | HELD | ACheckedRowWaitsForItsGear, TheGearOpensTheRowsDialogAndOkMakesItReady |  |
| US1.0 | An "All" toggle checks or unchecks every stream's Process | HELD | TheAllBoxSetsEveryRow, TheAllBoxAndLegacyChapter10ReachTheViewModel |  |
| US1.0 | Specify framesync pattern, frame length, mask, bitrate per stream | HELD | TheDialogOpensWithTheRowAndIsNamedForItsMode, WhatTheDialogHoldsIsTheSetupItGivesBack |  |
| US1.0 | Recall/restore previous frame parameters per stream using config files | HELD | LoadingAPatternFileShowsItAndRemembersTheFolder, SavingAPatternFileWritesWhatIsShown, APatternFileRoundTripsTheFrameSyncAndBothFlags | Both flags round-trip |
| US1.0 | Specify SNR signal data parameters for SNR streams | HELD | TheReceiverSnrDialogShowsTheReceiverSettingsLaidOut |  |
| US1.0 | Recall/restore previous SNR parameters using configuration files | HELD | LoadingReceiverParametersShowsThemAndTheWordMap, SavingReceiverParametersWritesWhatIsShown |  |
| US1.0 | Dialog closes when the user clicks "Process" | HELD | TheDialogsOpenModallyAndCloseOnOkOrCancel |  |
| US1.0 | Dialog closes when the user clicks "Cancel" | HELD | TheDialogsOpenModallyAndCloseOnOkOrCancel |  |
| US1.0 | Once "Process" selected, main window updated with processed data | HELD | TwoStreamsConfiguredAndProcessedAreBothDrawn, EveryLockStreamIsDrawnInItsOwnColourOnOneTimeAxis |  |
| US1.0 | Progress shown in a modal dialog, separate from main window | HELD | TheProgressDialogIsUpForTheRunAndShowsTheCoresProgress, TheProgressDialogIsShownOverTheMainWindowAndClosedWhenTold, TheProgressDialogShowsTheRunAndCancelOnlyAsks | IsDialog asserted |
| US1.0 | User can cancel; Cancel stops every stream being decommutated | HELD | ACancelWhileTheFileIsReadStopsTheReaderAndEveryStream, CancelInTheProgressDialogStopsTheRunAndShowsNothing, TheProgressDialogCannotBeClosedOrEscapedWhileTheRunGoesOn |  |
| US1.1 | Batch-process via Apply Template to Files…, multi-select files | HELD | ApplyTemplateReadsTheTemplateChecksTheFilesAndRunsTheOnesThatMatchOnceConfirmed, TheMenuHoldsItsSectionsHelpAndExit | Top-bar test replaced by menu test |
| US1.1 | Configured once at template creation (Save as Template) | HELD | SaveAsTemplateCapturesTheRecordingJustProcessed, WithSeveralRecordingsSaveAsTemplateAsksWhichAndAnOpenForgetsThem, ATemplateIsCapturedFromOneRecordingAndItsSeriesAsTheyLook |  |
| US1.1 | Matched by PCM channel ID; mismatch skipped, missing/extra told | HELD | AFileMatchesOnItsChannelIdsAloneAndAMismatchIsSpelledOut, TheConfirmationSaysHowManyMatchAndNeedsAFolderOnlyToExport |  |
| US1.1 | All files retained; optional CSV + images per file | HELD | ABatchRunsEachFileInTurnKeepsEachAndExportsEachAsV214Did |  |
| US1.1 | Progress shown; summary of processed vs skipped | HELD | ABatchRunsEachFileInTurn…, ATemplatesNamesAndColoursGoOnEveryFileWhenAskedAndAFailedFileIsSaidSo |  |
| US1.1 | Template round-trips per-stream settings and optional names/colors | HELD | TheV214TemplatesReadAsTheyWereWritten, ATemplateIsWrittenAsV214WroteIt, ATemplatesLooksGoBackOnOneRecordingsMatchingSeriesInOneRefresh | Byte-for-byte against Qt-written templates |
| US2.0 | Define frame sync pattern and frame length per stream | HELD | TheDialogOpensWithTheRowAndIsNamedForItsMode, WhatTheDialogHoldsIsTheSetupItGivesBack |  |
| US2.0 | Accepts frame sync pattern sizes up to 64 bits | HELD | ASyncFieldTakesOneToSixteenHexDigits, AFrameSyncProblemIsNamed |  |
| US2.0 | Accepts frame sync masks up to 64 bits | HELD | ASyncFieldTakesOneToSixteenHexDigits, AFrameSyncProblemIsNamed |  |
| US2.0 | Accepts frame lengths up to 65,536 bits | HELD | AFrameSyncProblemIsNamed | 63/65537 rejected |
| US2.0 | Select PCM code format NRZ-L, RNRZ-L | HELD | APatternFileRoundTripsTheFrameSyncAndBothFlags; native "FrameProcessor: derandomize undoes the IRIG 106 randomizer…" | Derandomize checkbox |
| US2.0 | Calculate lock statistics for streams present, no count cap | HELD | TwoStreamsConfiguredAndProcessedAreBothDrawn |  |
| US2.1 | Define frame sync pattern and frame length (AGC stream) | HELD | TheDialogOpensWithTheRowAndIsNamedForItsMode, TheReceiverSnrDialogShowsTheReceiverSettingsLaidOut | Same setup window |
| US2.1 | Accepts frame sync pattern sizes up to 64 bits | HELD | ASyncFieldTakesOneToSixteenHexDigits |  |
| US2.1 | Accepts frame sync masks up to 64 bits | HELD | AFrameSyncProblemIsNamed |  |
| US2.1 | Accepts frame lengths up to 65,536 bits | HELD | AFrameSyncProblemIsNamed |  |
| US2.1 | Select PCM code format NRZ-L, RNRZ-L | HELD | APatternFileRoundTripsTheFrameSyncAndBothFlags |  |
| US2.1 | Define number of receivers and channels per receiver | HELD | TheReceiverSnrDialogShowsTheReceiverSettingsLaidOut, TheReceiverSettingsHaveTheDialogsRanges |  |
| US2.1 | Define scale of AGC sample data in dB/V | HELD | TheReceiverSettingsHaveTheDialogsRanges |  |
| US2.1 | Voltage range: 0-5V, 0-10V, +/-5V, +/-10V | HELD | TheReceiverSettingsHaveTheDialogsRanges; native "Constants: uiSlopeVoltageBounds", TheChoicesAreUs21sAndUs30sInTheirOrder (5.1) |  |
| US2.1 | Define polarity: positive or negative | HELD | TheReceiverSettingsHaveTheDialogsRanges; native "Constants: uiPolarityConstants", TheChoicesAreUs21sAndUs30sInTheirOrder (5.1) |  |
| US2.2 | Each setup dialog has "Apply to all <mode>" toggle beside OK | HELD | TheDialogOpensWithTheRowAndIsNamedForItsMode |  |
| US2.2 | On OK, settings copied to selected streams of same mode | HELD | ApplyToAllCopiesToTheOtherCheckedRowsOfTheMode, ApplyToAllCopiesSettingsToSameModeStreams |  |
| US2.2 | Streams of a different mode left unchanged | HELD | ApplyToAllLeavesDifferentModeStreamsUnchanged |  |
| US2.2 | Each affected stream marked configured/ready, indicator updates | HELD | ApplyToAllCopiesToTheOtherCheckedRowsOfTheMode |  |
| US3.0 | View lock statistics (left y-axis) vs time in plot | HELD | AStreamsSeriesComeFromTheCoreOnOneTimeAxis, EveryKindOfSeriesIsDrawnFromTheCoresGeometryInItsPlace |  |
| US3.0 | Select which streams' lock statistics to view | HELD | CustomizeViewHasARowPerLockStreamAndTheSnrStreamsByReceiver, OkShowsOnlyTheMetricInViewAndAppliesTheNamesAndColours |  |
| US3.0 | Select the time window to view lock statistics | HELD | ATimeWindowIsClampedIntoTheRecordingWithAWarningForEachEnd, TheWheelZoomsAboutTheCursor |  |
| US3.0 | Sample windows 10ms, 100ms, 1s | HELD | native "Constants: uiSamplePeriods", "FrameProcessor: a shorter period gives more samples", TheChoicesAreUs21sAndUs30sInTheirOrder (5.1) |  |
| US3.0 | Progress bar updates while processing lock statistics | HELD | TheProgressDialogIsUpForTheRunAndShowsTheCoresProgress |  |
| US3.1 | View accumulated missed frames (left y-axis) vs time | HELD | TheLeftAxisIsLockPercentOrMissedFramesAndTheViewCarriesTheStreamsShown, StreamsKeepTheirJobOrderAndTheViewDecidesWhichMetricIsShown |  |
| US3.1 | Switch left-axis view via context menu > View Mode | HELD | TheMenuSetsTheViewModeAndTheLegend |  |
| US3.1 | Select which stream's missed frames to view | HELD | CustomizeViewHasARowPerLockStream…, OkShowsOnlyTheMetricInView… |  |
| US3.1 | Missed frames accumulated per stream against own frame parameters | HELD | native "FrameProcessor: unsynced bits miss one frame per frame length…", "…accumulated frame sync errors never decrease" | Core-level |
| US3.2 | View SNR (right y-axis) vs time in plot | HELD | AStreamsSeriesComeFromTheCoreOnOneTimeAxis, AHiddenAxisGivesItsMarginBackAndTheTransformsFollowThePlotArea |  |
| US3.2 | Select which receiver/channels SNR to view | HELD | AReceiversBoxFollowsItsChannelsAndSetsThemAll, CustomizeViewsSnrTabShowsTheStreamChosenByReceiverInTwoColumns |  |
| US3.2 | Select time window to view SNR data | HELD | ATimeWindowIsClampedIntoTheRecordingWithAWarningForEachEnd |  |
| US3.2 | Averaging periods 100 Hz, 10 Hz, 1 Hz | HELD | native "Constants: uiSamplePeriods", "FrameProcessor: a shorter period gives more samples", TheChoicesAreUs21sAndUs30sInTheirOrder (5.1) | Same Average Period |
| US3.2 | Raw values converted to Volts using range and polarity | HELD | native "FrameProcessor: the slope scales the values", "Constants: uiSlopeVoltageBounds" | Core |
| US3.2 | Volts converted to dB using scale (dB/V) | HELD | native "FrameProcessor: only enabled words become channels, each calibrated as (raw + offset) x slope" | Core |
| US3.2 | Progress bar updates while processing SNR | HELD | TheProgressDialogFollowsAReceiverSnrRunToo |  |
| US4.0 | Plot fills central area; log in toggleable left sidebar | HELD | TheMainWindowShowsTheLogBesideTheChart, TheSidebarHidesGivingItsWidthToTheChartAndReturnsAsItWas |  |
| US4.0 | Chart occupies entire plot area; context menu disabled until data | HELD | TheMenusDataItemsWaitForData, ARightClickOnTheChartOpensTheMenu, ThePlotViewIsTheChartWithItsControlsOverItAndNothingAround (5.1) |  |
| US4.0 | Context menu provides Plot File, View Mode, Customize… etc. | HELD | TheMenuLeadsWithTheTitleAndHoldsEveryPlotControlAndNoExport | Asserts submenus and no Export |
| US4.0 | Custom plot title via Set Plot Title… | HELD | TheTitleAndTheTimeWindowAreAskedSeededWithWhatIsShown, HundredPercentIsAtTheTopAndTheTitleIsDrawn |  |
| US4.0 | Left Y axis labeled average framesync lock percent | HELD | TheLeftAxisIsLockPercentOrMissedFramesAndTheViewCarriesTheStreamsShown, TheAxesAndTheTitleAreWordedAsV214WordedThem (5.1) |  |
| US4.0 | Right Y axis labeled SNR in decibels | HELD | TheRightAxisIsTitledOnlyWhileItIsShown (5.1), TheAxesAndTheTitleAreWordedAsV214WordedThem (5.1) | Was untested |
| US4.0 | Bottom X axis labeled elapsed time DDD:HH:MM:SS | HELD | AStreamsSeriesComeFromTheCoreOnOneTimeAxis | Regex ^\d{3}:\d{2}:\d{2}:\d{2}$ |
| US4.0 | Y axis automatically scales to data min/max | HELD | TheSnrAxisIsRoundedToFiveDecibelsAndAnOverrideNeverInvertsIt, TheLeftAxisIsLockPercentOrMissedFrames… |  |
| US4.0 | X axis automatically scales to full time span | HELD | AGestureMovesTheWindowWithinTheDataAndIsNotRemembered, ADoubleClickRestoresTheWholeSpan |  |
| US4.0 | Set Time Window zooms/pans X; clamped with warning; Reset Span | HELD | ATimeWindowIsClampedIntoTheRecordingWithAWarningForEachEnd, AClampedTimeWindowIsAWarningInTheLog, TheMenusAxisItemsResetZoomBackAndAsk |  |
| US4.0 | Manually override either end of either Y axis | HELD | EachAxisLimitIsAskedWithinBoundsThatCannotInvertTheAxis, TheSnrAxisIsRoundedToFiveDecibelsAndAnOverrideNeverInvertsIt |  |
| US4.0 | Mouse wheel zooms the X axis | HELD | TheWheelZoomsAboutTheCursor |  |
| US4.0 | Click and hold pans the X axis | HELD | ADragPansKeepingTheTimeGrabbedUnderTheCursor |  |
| US4.0 | Show or hide individual Lock and SNR series | HELD | OkShowsOnlyTheMetricInViewAndAppliesTheNamesAndColours, AReceiversBoxFollowsItsChannelsAndSetsThemAll |  |
| US4.0 | Movable legend overlay floats inside chart with swatches | HELD | TheLegendFloatsAtTheTopRightWithARowPerSeriesShown |  |
| US4.0 | Click-drag the legend anywhere inside the plot area | HELD | TheLegendIsDraggedAnywhereInsideTheChartAndKeptThere |  |
| US4.0 | Legend defaults top-right, resets there each session | HELD | TheLegendFloatsAtTheTopRightWithARowPerSeriesShown, AnUndraggedLegendFollowsTheTopRightWhenTheChartIsResized | Position never persisted |
| US4.0 | Show/hide legend from menu or L; persists; excluded from export | HELD | TheMenuSetsTheViewModeAndTheLegend, TheMainWindowRemembersItsFolderItsRecentFilesAndItsChoices, APngIsTheChartAtItsSizeWithTheLegendWhereItIs |  |
| US4.0 | Legend background is translucent | HELD | TheLegendIsTranslucentSoTheDataBehindItShows |  |
| US4.0 | Legend height capped with vertical scrollbar for dense plots | HELD | ADenseLegendIsCappedAndScrollsWithoutCoveringALabel |  |
| US4.0 | SNR legend rows show short label; full name via tooltip/CSV | HELD | TheLegendListsTheSeriesShownOfTheMetricInView, EveryRecordingExportsTheCsvV214ExportedByteForByte |  |
| US4.0 | Legend composited into exported PNG and SVG at position | HELD | APngIsTheChartAtItsSizeWithTheLegendWhereItIs | SVG legend not asserted (same renderer) |
| US4.0 | Rename or recolor a series from Customize dialog | HELD | ARenameOrRecolourReachesAStreamsOtherMetricAndNoOtherStream, OkShowsOnlyTheMetricInView…, EachPromptAnswersOnOkAndNothingOnCancel |  |
| US4.0 | Lock curves purple, blue, green primaries, one per stream | HELD | EachStreamAndEachReceiverTakesItsColour, AShadeIsTheColourV214Drew |  |
| US4.0 | SNR curves red, orange, yellow primaries, one per receiver | HELD | EachStreamAndEachReceiverTakesItsColour |  |
| US4.0 | Additional series use progressively lighter shades of primary | HELD | AShadeIsTheColourV214Drew, EachStreamAndEachReceiverTakesItsColour | 84 Qt-printed shades |
| US4.0 | Lock curves drawn with visually highlighted style, SNR not | REMOVED | — | Never true of either app (one 1.5 px pen for every curve); the story reworded at the user's choice |
| US4.1 | Plot window has no external control rows | HELD | ThePlotViewIsTheChartWithItsControlsOverItAndNothingAround (5.1) | Was untested |
| US4.1 | Every plot control via context menu or on-chart overlay | HELD | TheMenuLeadsWithTheTitleAndHoldsEveryPlotControlAndNoExport, TheChipBarIsOverTheChartAtTheTopLeftAndOnlyWithData |  |
| US4.1 | Menu leads with Set Plot Title…, then Plot File… order | HELD | TheMenuLeadsWithTheTitleAndHoldsEveryPlotControlAndNoExport, TheMenusDataItemsWaitForData | Exact header order asserted (plus Readout) |
| US4.1 | Legend shown/hidden from menu or L, persists | HELD | TheMenuSetsTheViewModeAndTheLegend, TheKeysDriveTheViewWhileTheChartHasFocus, TheMainWindowRemembersItsFolderItsRecentFilesAndItsChoices |  |
| US4.1 | Chips gathered in a top-left chip bar, self-hiding | HELD | TheChipBarIsOverTheChartAtTheTopLeftAndOnlyWithData, TheResetChipAppearsOnlyOnceTheViewHasChanged |  |
| US4.1 | Gestures: Ctrl/middle-drag band zoom, double-click, crosshair | HELD | ABandDraggedWithCtrlOrTheMiddleButtonZoomsIntoIt, ADoubleClickRestoresTheWholeSpan, TheCrosshairAndReadoutFollowTheCursorOverTheCachedPlot |  |
| US4.1 | Wheel zoom and click-drag pan retained | HELD | TheWheelZoomsAboutTheCursor, ADragPansKeepingTheTimeGrabbedUnderTheCursor |  |
| US4.1 | Log sidebar toggleable, open by default | HELD | TheSidebarHidesGivingItsWidthToTheChartAndReturnsAsItWas, CtrlOOpensAndCtrlBTogglesTheSidebar, WithNothingToReadThePreferencesAreTheDefaultsAndWritten |  |
| US4.1 | Each on-chart chip labelled with a tooltip | HELD | TheViewModeChipSwitchesTheLeftAxisAndIsNamedForWhereItGoes, TheResetChipAppearsOnlyOnceTheViewHasChanged | Tooltips asserted |
| US4.1 | Crosshair readout pinnable via context menu > Readout | HELD | TheReadoutMenuListsTheSeriesShownAndPinsOne, APinToASeriesHiddenOrGoneIsDropped, TheReadoutIsTheNearestValueNearTheCursorOrThePinnedSeriesAnywhere |  |
| US4.1 | Single-key shortcuts while chart focused; menu advertises keys | HELD | TheKeysDriveTheViewWhileTheChartHasFocus, TheKeysAreTheChartsAndNotTheWindows, AClickOnTheChartGivesItTheKeys, TheMenuAdvertisesEveryKeyOfThePlot |  |
| US5.0 | Configuration files include framesync pattern and frame length | HELD | APatternFileRoundTripsTheFrameSyncAndBothFlags (StreamFilesTests) |  |
| US5.0 | User can save pattern and frame length to config file | HELD | SavingAPatternFileWritesWhatIsShown |  |
| US5.0 | User can recall pattern and frame length from config | HELD | LoadingAPatternFileShowsItAndRemembersTheFolder |  |
| US5.0 | Configuration file stored in a user specified location | HELD | SavingAPatternFileWritesWhatIsShown | Via picker (faked in test) |
| US5.0 | Configuration file stored in TOML format | HELD | APatternFileRoundTripsTheFrameSyncAndBothFlags; native test_framesyncfile | Save adds .toml |
| US5.1 | Config files include default SNR processing parameters | HELD | LoadingReceiverParametersShowsThemAndTheWordMap; ReceiverParamsTests (Core) |  |
| US5.1 | User can save default parameters to config file | HELD | SavingReceiverParametersWritesWhatIsShown | Bytes = core's writer |
| US5.1 | User can recall default parameters from config file | HELD | LoadingReceiverParametersShowsThemAndTheWordMap |  |
| US5.1 | Configuration file stored in user specified location | HELD | SavingReceiverParametersWritesWhatIsShown |  |
| US5.1 | Configuration file stored in TOML format | HELD | ReceiverParamsTests; native "Abi" receiver-params cases |  |
| US5.2 | Dialog includes receivers, channels, scale, voltage range, slope | HELD | TheReceiverSnrDialogShowsTheReceiverSettingsLaidOut | "Slope" combo is the voltage range |
| US5.2 | Dialog includes "Reset" button to restore defaults | REMOVED | — | Neither app's dialog has one; Load with the shipped default.toml does its job; the story reworded at the user's choice |
| US5.2 | Dialog includes "Save" button to save to config file | HELD | SavingReceiverParametersWritesWhatIsShown |  |
| US5.2 | Dialog includes "Cancel" button | HELD | TheDialogsOpenModallyAndCloseOnOkOrCancel |  |
| US5.3 | Enable non-linear step cal per SNR stream in setup dialog | HELD | ApplyCalIsForAReceiverSnrStreamWithATimeChannelAndNoProblem, TheSetupWindowHasApplyCalWithItsCalibrationBesideIt |  |
| US5.3 | Load a TOML file defining expected step values | HELD | TheDialogOpensOnTheStreamsStepFileElseTheShippedOne, BrowseStartsInTheShippedStepFilesThenWhereItLastWent |  |
| US5.3 | Load a Calibration CH10 file with recorded step data | HELD | BrowseStartsInTheShippedStepFilesThenWhereItLastWent, TheCalibrationRecordingIsExtractedWithTheProgressDialogUp |  |
| US5.3 | Apply Cal dialog has optional Clip Start/Clip End | HELD | ClipsBeyondTheRecordingAreIgnoredAndSaySoAtTheStreamsDataRate, TheExtractCalibrationWindowShowsTheDialogsState |  |
| US5.3 | Automatically extracts "ideal" steps from CAL file | HELD | TheCalibrationRecordingIsExtractedWithTheProgressDialogUp |  |
| US5.3 | Evaluates extraction per receiver channel (up to 48) | HELD | TheCalibrationRecordingIsExtractedWithTheProgressDialogUp | 12 of 48 on step_cal |
| US5.3 | Successful channels get a non-linear profile | HELD | ApplyCalExtractsWithTheSettingsAsTheyStandAndOkKeepsItsCalibration, OnlyTheRecordingsOwnByteOrderInversionAndRandomizationCalibrate |  |
| US5.3 | Failed channels fall back to linear calibration | HELD | TheCalibrationRecordingIsExtractedWithTheProgressDialogUp; native "StepDetector: a flat channel reports no signal…" |  |
| US5.3 | Summary message box: which succeeded, which fell back | HELD | ApplyCalExtractsWithTheSettingsAsTheyStandAndOkKeepsItsCalibration, ExtractCalibrationAndItsReportSitOverTheSetupDialogAndClose; native "…the summary names receivers and why they fell back" |  |
| US5.3 | Summary flags extra levels and swept/flat channel mismatch | HELD | native "CalibrationExtractor: the summary flags a short step file and a mismatched word map" | Wording is core's |
| US5.3 | Clip values exceeding recording reported, not ignored | HELD | ClipsBeyondTheRecordingAreIgnoredAndSaySoAtTheStreamsDataRate; native "…clips beyond the recording are reported" |  |
| US5.3 | Step-config TOML preloaded; status says loaded automatically | HELD | TheDialogOpensOnTheStreamsStepFileElseTheShippedOne |  |
| US5.3 | Processing applies profile via piece-wise linear interpolation | HELD | ACalibrationAppliedInTheDialogsReachesTheRun; native "StepDetector: interpolate across several segments…" |  |
| US5.3 | Out-of-range raw values clamp to nearest end-step dB | HELD | native "StepDetector: interpolate below/above the … point clamps" | Core |
| US6.0 | Image is exactly what chart shows (series, metric, window, legend) | HELD | AnImageShowsOnlyWhatTheChartShows, TheCursorsCrosshairIsNotExported |  |
| US6.0 | Image format svg, png, pdf | HELD | AnSvgAndAPdfAreTheChartsSizeAndAnyOtherNameIsAPdf, AnImagesNameDecidesItsFormat |  |
| US6.0 | Image filename/location via export dialog | HELD | BrowseStartsAtTheRowsPathAndANameWithoutASuffixGetsTheRowsOwn, TheExportWindowHasItsThreeRowsAndEachFollowsItsBox |  |
| US6.0 | Movable legend rendered into image at on-plot position | HELD | APngIsTheChartAtItsSizeWithTheLegendWhereItIs |  |
| US6.0 | Single export dialog for any combination, one action | HELD | ExportWritesTheDataThenTheImageThenTheLogAndTheLogSaysWhere, AFailedExportSaysSoAndTheRestStillRun, TheExportDialogsAnswerIsCarriedOut |  |
| US6.1 | Single CSV covering every series, shown or hidden | HELD | AnExportHoldsEverySeriesShownOrNotAndNothingToWriteWritesNoFile, EveryRecordingExportsTheCsvV214ExportedByteForByte |  |
| US6.1 | CSV has timestamp column and one column per series | HELD | EveryRecordingExportsTheCsvV214ExportedByteForByte | Byte-identical to Qt |
| US6.1 | Column headers identify stream and receiver/channel | HELD | EveryRecordingExportsTheCsvV214ExportedByteForByte, ALaterSourcesColumnsAreQualifiedAndASourceCanBeExportedAlone |  |
| US6.1 | CSV filename/location via export dialog | HELD | BrowseStartsAtTheRowsPathAndANameWithoutASuffixGetsTheRowsOwn |  |
| US6.2 | Log contents exported to plain-text file | HELD | ExportWritesTheDataThenTheImageThenTheLogAndTheLogSaysWhere |  |
| US6.2 | Log filename/location via export dialog | HELD | BrowseStartsAtTheRowsPathAndANameWithoutASuffixGetsTheRowsOwn, AFailedExportSaysSoAndTheRestStillRun |  |
| US6.3 | Open CSV via Import CSV…, drag-and-drop, or Recent Files | HELD | TheMenuHoldsItsSectionsHelpAndExit, ARecordingOrCsvDroppedOnTheWindowOpensAsRecentFilesOpensIt, ARecentFileOpensByItsSuffixAndNotWhileBusy |  |
| US6.3 | Import CSV… opens CSV-only dialog, disabled while processing | HELD | ImportCsvAsksInTheLastFolderAndAnImportOfSkippedRowsWarnsBeforeItSucceeds, OpenOffersRecordingsOnlyAndImportCsvExportedCsvsOnly (5.1) | Busy-disable and filter tested |
| US6.3 | Open… remains Chapter-10-only | HELD | OpenOffersRecordingsOnlyAndImportCsvExportedCsvsOnly (5.1) | Was untested, and Open also offered "All files"; now Chapter 10 only, as the Qt Open |
| US6.3 | Import bypasses Configure Streams, straight into plot | HELD | ImportCsvShowsTheFileTitledByItsNameAndSaysSo |  |
| US6.3 | Importer accepts own format: Time (DOY:…) column first | HELD | EveryExportReadsBackAsItsSeriesAndExportsAgainByteForByte |  |
| US6.3 | Lock/Missed/SNR headers routed to axis, metric, color | HELD | AnImportedPlotIsShownAsARunsWouldBe, EveryExportReadsBack… |  |
| US6.3 | Imported series populate plot, legend, Customize dialog | HELD | AnImportedPlotIsShownAsARunsWouldBe, LoadingIsShownOverTheChartWhileACsvIsRead |  |
| US6.3 | Imported data re-exportable; recent-files records imported CSV | HELD | EveryExportReadsBackAsItsSeriesAndExportsAgainByteForByte, TheMainWindowRemembersItsFolderItsRecentFilesAndItsChoices |  |
| US6.3 | Non-format CSV rejected with clear error in log | HELD | AFileThatIsNotAnExportedCsvChangesNothing, ACsvThatIsNotAnExportIsAnErrorAndLeavesThePlotEmpty |  |
| US6.3 | Malformed rows skipped, user told how many | HELD | MalformedRowsAreSkippedWithAWarningThatCountsThem |  |
| US7.0 | Ensure all input fields are validated | HELD | TheProblemsFollowTheFieldsAndHoldOk, TheReceiverSettingsHaveTheDialogsRanges, EachAxisLimitIsAskedWithinBoundsThatCannotInvertTheAxis |  |
| US7.0 | Error messages displayed when invalid values entered | HELD | TypingAnInvalidPatternShowsTheProblemAndHoldsOk, AFrameSyncProblemIsNamed | Inline, not message box |
| US7.0 | Frame sync pattern only hexadecimal | HELD | ASyncFieldTakesOneToSixteenHexDigits |  |
| US7.0 | Pattern no larger than user-specified frame length | HELD | TheLongestPatternFitsTheShortestFrame | Holds by limits (64 bits ≤ 64-bit min frame) |
| US8.0 | Installer produces a code-signed application executable | HELD | build_avalonia_package.ps1 (sign + verify steps 5-6); verify_avalonia_install.ps1 "every PE file validly signed" | Passed on the v3.0.0 build |
| US8.0 | Installs to "Program Files" for admin users | HELD | deploy/avalonia/tmDataQualityAnalyzer.iss DefaultDirName={autopf}; verify_avalonia_install.ps1 /ALLUSERS install and upgrade | Passed on the v3.0.0 build |
| US8.0 | Installs to user-selected dir for non-admins | BY HAND | .iss PrivilegesRequiredOverridesAllowed=dialog | Inno Setup's own dialog (PrivilegesRequiredOverridesAllowed=dialog); the Sandbox installs elevated, so checked by hand at 5.3. **Checked by hand on the v3.0.0 build: passed.** |
| US8.0 | Installer shows progress while installing | BY HAND | .iss WizardStyle=modern (Inno default progress page) | Inno Setup's own page (WizardStyle=modern); the Sandbox installs silently, so checked by hand at 5.3. **Checked by hand on the v3.0.0 build: passed.** |
| US8.0 | Never overwrites user TOML; left by uninstall | HELD | .iss line 62 onlyifdoesntexist uninsneveruninstall; verifier "the user's edited settings file survived the upgrade", "a reinstall keeps an edited settings file", "settings left for the user" | Passed on the v3.0.0 build, the upgrade from v2.14.2 included |
| US8.0 | Older settings file stays usable; missing keys default | HELD | TheV214TemplatesReadAsTheyWereWritten, APatternFileRoundTripsTheFrameSyncAndBothFlags, ReceiverParamsTests, TheFirstRunImportsTheV2SettingsOnceAndThenKeepsItsOwn | Reader tolerance |
| US8.0 | New default fields reach fresh install only; upgrade keeps file | HELD | .iss onlyifdoesntexist; verify_avalonia_install.ps1 upgrade and reinstall checks | Passed on the v3.0.0 build |
| US9.0 | Toggle light/dark theme via a menu action | HELD | TheMenuHoldsItsSectionsHelpAndExit |  |
| US9.0 | Selected theme persists across restarts | HELD | TheMainWindowRemembersItsFolderItsRecentFilesAndItsChoices, TheFirstRunImportsTheV2SettingsOnceAndThenKeepsItsOwn |  |
| US9.0 | All windows, dialogs, plot update immediately | HELD | EachThemeDrawsTheChartTheLegendTheChipsAndTheLogInItsColours, TheApplicationsThemeIsEveryWindowsAndRemembered, ThePalettesAreV214s | Native frames checked by hand only |
| US9.0 | Menu and title-bar icons swap with theme | HELD | EachThemesBrushesAreV214s | Caption glyphs are font glyphs, follow foreground |

## The Qt test cases

Verdicts: **PORTED** (a test in the new code fails without the behaviour), **DROPPED** (the reason given),
**NOT HELD** (the reason given). A test named by its file is an app test; a doctest name in quotes is native.
`test_seriesbuild.cpp`'s scenarios run on a host written in the test, which copies the Qt `PlotViewModel`'s
behaviour, so a row that leans on it holds the core functions (`buildStreamSeries`, `TimeBase`, `StreamKey`), not the
application's `AddStream`.

### The plot widget, Customize View, Export and the main view

`tst_plotwidget`, `tst_plotcustomizationdialog`, `tst_exportdialog`, `tst_mainview`, `tst_mainviewmodel_helpers`.

| Qt case | Verdict | Held by | Note |
|---|---|---|---|
| tst_plotwidget::constructsWithoutCrash | PORTED | AroundTheChartTests.cs::TheChipBarIsOverTheChartAtTheTopLeftAndOnlyWithData (and every Show()) | Implicit: every view test constructs a PlotView |
| tst_plotwidget::setViewModelWithNullDoesNotCrash | PORTED | AroundTheChartTests.cs::TheMenuLeadsWithTheTitleAndHoldsEveryPlotControlAndNoExport | Weak: only checks `new PlotView().BuildContextMenu()` is null; no plot-less render |
| tst_plotwidget::setViewModelConnectsWithoutCrash | PORTED | AroundTheChartTests.cs (all Show(plot) tests) | Implicit |
| tst_plotwidget::applyThemeDarkDoesNotCrash | PORTED | ShellTests.cs::EachThemeDrawsTheChartTheLegendTheChipsAndTheLogInItsColours | Checks the dark palette on the chart, legend and chips |
| tst_plotwidget::applyThemeLightDoesNotCrash | PORTED | ShellTests.cs::EachThemeDrawsTheChartTheLegendTheChipsAndTheLogInItsColours | Same test, light theme |
| tst_plotwidget::legendOverlayPopulatesFromData | PORTED | AroundTheChartTests.cs::TheLegendFloatsAtTheTopRightWithARowPerSeriesShown, ::TheLegendListsTheSeriesShownOfTheMetricInView | Hidden without data, then one row |
| tst_plotwidget::legendOverlaySizesCorrectlyAfterRebuild | PORTED | AroundTheChartTests.cs::TheLegendFloatsAtTheTopRightWithARowPerSeriesShown | Size checked after first build; after rebuild only row count and spacing |
| tst_plotwidget::legendUsesShortNameForSnrSeries | PORTED | AroundTheChartTests.cs::TheLegendListsTheSeriesShownOfTheMetricInView, ::TheLegendFloatsAtTheTopRightWithARowPerSeriesShown | "CH40 L_RCVR3"; full name kept as tooltip |
| tst_plotwidget::legendReservesScrollbarGutter | PORTED | AroundTheChartTests.cs::ADenseLegendIsCappedAndScrollsWithoutCoveringALabel | Gutter width and no label under the scroll bar |
| tst_plotwidget::legendRowsOverrideGlobalWidgetBackground | DROPPED | (outcome partly held by AroundTheChartTests.cs::TheLegendIsTranslucentSoTheDataBehindItShows) | Qt-only stylesheet selector trap (QWidget objectName) |
| tst_plotwidget::exportImageWritesPngHeadlessly | PORTED | ExportImportTests.cs::APngIsTheChartAtItsSizeWithTheLegendWhereItIs | Writes the PNG and reads its pixels back |
| tst_plotwidget::exportImageWritesSvgHeadlessly | PORTED | ExportImportTests.cs::AnSvgAndAPdfAreTheChartsSizeAndAnyOtherNameIsAPdf |  |
| tst_plotwidget::exportImageDefaultsUnknownSuffixToPdf | PORTED | ExportImportTests.cs::AnSvgAndAPdfAreTheChartsSizeAndAnyOtherNameIsAPdf, ::AnImagesNameDecidesItsFormat | ".bogus" becomes ".bogus.pdf" |
| tst_plotwidget::contextMenuListsExpectedTopLevelItems | PORTED | AroundTheChartTests.cs::TheMenuLeadsWithTheTitleAndHoldsEveryPlotControlAndNoExport | Order, title first, no Export |
| tst_plotwidget::contextMenuItemsDisabledUntilDataLoads | PORTED | AroundTheChartTests.cs::TheMenusDataItemsWaitForData |  |
| tst_plotwidget::contextMenuPlotFileSubmenuTracksSources | PORTED | TemplateTests.cs::PlotFileListsTheRecordingsAndShowsTheOneChosen | Disabled for one source; "All files" checked; choosing one isolates it |
| tst_plotwidget::contextMenuViewModeReflectsAndSetsMode | PORTED | AroundTheChartTests.cs::TheMenuSetsTheViewModeAndTheLegend |  |
| tst_plotwidget::contextMenuResetActionsClearAxisOverrides | PORTED | AroundTheChartTests.cs::TheMenusAxisItemsResetZoomBackAndAsk | Reset Span and Y Reset checked separately |
| tst_plotwidget::yMinOverrideReachesTheChartAxis | PORTED | PlotTests.cs::ALeftMinimumSetMovesTheChartsBottom (5.1) | Set to 40: the 50 % line drops to a sixth of the way up, and Reset puts it back |
| tst_plotwidget::contextMenuOffersBothYLimits | PORTED | AroundTheChartTests.cs::TheMenuLeadsWithTheTitleAndHoldsEveryPlotControlAndNoExport | All four Y limits plus Reset |
| tst_plotwidget::contextMenuOffersZoomBack | PORTED | AroundTheChartTests.cs::TheMenusAxisItemsResetZoomBackAndAsk | Disabled with no history; steps back |
| tst_plotwidget::contextMenuAdvertisesEveryPlotShortcut | PORTED | AroundTheChartTests.cs::TheMenuAdvertisesEveryKeyOfThePlot | V in title; L, R, Home and Back gestures |
| tst_plotwidget::contextMenuResetViewResetsBothAxes | PORTED | AroundTheChartTests.cs::TheMenusAxisItemsResetZoomBackAndAsk, ::TheMenusDataItemsWaitForData |  |
| tst_plotwidget::contextMenuSetTitleAppliesToViewModel | PORTED | AroundTheChartTests.cs::TheTitleAndTheTimeWindowAreAskedSeededWithWhatIsShown; PlotTests.cs::HundredPercentIsAtTheTopAndTheTitleIsDrawn |  |
| tst_plotwidget::wheelZoomAndDragPanRemainEnabled | PORTED | PlotTests.cs::AnEmptyChartIsAFrameWithNothingInsideAndIgnoresTheWheel, ::TheWheelZoomsAboutTheCursor, ::ADragPansKeepingTheTimeGrabbedUnderTheCursor | "Y unchanged" not asserted; there is no vertical gesture to break it |
| tst_plotwidget::noExternalControlWidgetsRemain | PORTED | AroundTheChartTests.cs::ThePlotViewIsTheChartWithItsControlsOverItAndNothingAround (5.1) | Every templated control in the view is in the overlay over the chart |
| tst_plotwidget::legendVisibilitySurvivesRebuilds | PORTED | AroundTheChartTests.cs::AHiddenLegendStaysHiddenUntilShownAgain; ExportImportTests.cs::APngIsTheChartAtItsSizeWithTheLegendWhereItIs | Also checks a hidden legend is not exported |
| tst_plotwidget::emptyChartKeepsLeftAxis | PORTED | PlotTests.cs::AnAxisIsDrawnOnlyWhenSomethingIsOnIt |  |
| tst_plotwidget::lockOnlyDataHidesRightAxis | PORTED | PlotTests.cs::AHiddenAxisGivesItsMarginBackAndTheTransformsFollowThePlotArea, ::AnAxisIsDrawnOnlyWhenSomethingIsOnIt |  |
| tst_plotwidget::snrOnlyDataHidesLeftAxis | PORTED | PlotTests.cs::AnAxisIsDrawnOnlyWhenSomethingIsOnIt, ::AHiddenAxisGivesItsMarginBackAndTheTransformsFollowThePlotArea |  |
| tst_plotwidget::hidingLastSnrSeriesReclaimsRightAxis | PORTED | PlotTests.cs::AnAxisIsDrawnOnlyWhenSomethingIsOnIt | Showing the series again is not re-asserted |
| tst_plotwidget::overlayBarAppearsOnlyWithData | PORTED | AroundTheChartTests.cs::TheChipBarIsOverTheChartAtTheTopLeftAndOnlyWithData | Includes the SNR-only case where the bar is 0×0 |
| tst_plotwidget::contextMenuShowLegendDrivesTheLegend | PORTED | AroundTheChartTests.cs::TheMenuSetsTheViewModeAndTheLegend |  |
| tst_plotwidget::overlayBarHoldsChipsAndIsChartParented | PORTED | AroundTheChartTests.cs::TheChipBarIsOverTheChartAtTheTopLeftAndOnlyWithData |  |
| tst_plotwidget::viewModeChipSwitchesLeftAxisMetric | PORTED | AroundTheChartTests.cs::TheViewModeChipSwitchesTheLeftAxisAndIsNamedForWhereItGoes |  |
| tst_plotwidget::resetChipAppearsOnlyWhenViewChanged | PORTED | AroundTheChartTests.cs::TheResetChipAppearsOnlyOnceTheViewHasChanged |  |
| tst_plotwidget::bandZoomAppliesDraggedRange | PORTED | PlotTests.cs::ABandZoomIsRememberedAndZoomBackReturnsThroughEachWindow, ::ABandDraggedWithCtrlOrTheMiddleButtonZoomsIntoIt | Right-to-left drag; a click does nothing |
| tst_plotwidget::doubleClickResetsSpanViaViewModel | PORTED | PlotTests.cs::ADoubleClickRestoresTheWholeSpan |  |
| tst_plotwidget::readoutMenuListsVisibleSeriesAndPins | PORTED | AroundTheChartTests.cs::TheReadoutMenuListsTheSeriesShownAndPinsOne |  |
| tst_plotwidget::readoutPinDropsWhenSeriesGoesAway | PORTED | AroundTheChartTests.cs::APinToASeriesHiddenOrGoneIsDropped, ::TheReadoutMenuListsTheSeriesShownAndPinsOne |  |
| tst_plotwidget::keyboardShortcutsDriveTheView | PORTED | AroundTheChartTests.cs::TheKeysDriveTheViewWhileTheChartHasFocus, ::TheArrowsAndPlusAndMinusStepTheWindow |  |
| tst_plotwidget::keyboardShortcutsIgnoredWithoutData | PORTED | AroundTheChartTests.cs::TheKeysDoNothingWithoutData |  |
| tst_plotwidget::keyboardShortcutsAreWidgetScopedNotApplicationWide | PORTED | AroundTheChartTests.cs::TheKeysAreTheChartsAndNotTheWindows | Typing "Lock" into a TextBox |
| tst_plotcustomizationdialog::lockTabOneCheckboxPerStream | PORTED | AroundTheChartTests.cs::CustomizeViewHasARowPerLockStreamAndTheSnrStreamsByReceiver |  |
| tst_plotcustomizationdialog::selectAllNoneLock | PORTED | AroundTheChartTests.cs::SelectAllAndNoneAndExpandActOnTheirTabAndTheStreamShown |  |
| tst_plotcustomizationdialog::applyChangesLockVisibility | PORTED | AroundTheChartTests.cs::OkShowsOnlyTheMetricInViewAndAppliesTheNamesAndColours, ::CustomizeViewAppliesOnlyOnOk | The sibling in the other view stays hidden |
| tst_plotcustomizationdialog::snrTabTreePerStream | PORTED | AroundTheChartTests.cs::CustomizeViewHasARowPerLockStreamAndTheSnrStreamsByReceiver | Receivers and their channels |
| tst_plotcustomizationdialog::snrTristateCascade | PORTED | AroundTheChartTests.cs::AReceiversBoxFollowsItsChannelsAndSetsThemAll |  |
| tst_plotcustomizationdialog::selectAllNoneSnr | PORTED | AroundTheChartTests.cs::SelectAllAndNoneAndExpandActOnTheirTabAndTheStreamShown |  |
| tst_plotcustomizationdialog::applyChangesSnrVisibility | PORTED | AroundTheChartTests.cs::OkShowsOnlyTheMetricInViewAndAppliesTheNamesAndColours |  |
| tst_plotcustomizationdialog::expandCollapseTogglesButton | PORTED | AroundTheChartTests.cs::SelectAllAndNoneAndExpandActOnTheirTabAndTheStreamShown | Expand All / Collapse All text |
| tst_plotcustomizationdialog::lockRenameAppliesToViewModel | PORTED | AroundTheChartTests.cs::OkShowsOnlyTheMetricInViewAndAppliesTheNamesAndColours, ::ARenameOrRecolourReachesAStreamsOtherMetricAndNoOtherStream | Both siblings renamed |
| tst_plotcustomizationdialog::lockRecolorAppliesToViewModel | PORTED | AroundTheChartTests.cs::OkShowsOnlyTheMetricInViewAndAppliesTheNamesAndColours, ::ARenameOrRecolourReachesAStreamsOtherMetricAndNoOtherStream |  |
| tst_plotcustomizationdialog::snrRenameRecolorAppliesToViewModel | PORTED | AroundTheChartTests.cs::OkShowsOnlyTheMetricInViewAndAppliesTheNamesAndColours |  |
| tst_plotcustomizationdialog::applyChangesEmitsAppearanceSignal | PORTED | AroundTheChartTests.cs::OkShowsOnlyTheMetricInViewAndAppliesTheNamesAndColours, ::AQuietBatchOfVisibilityIsShownByOneRefresh | One refresh per OK |
| tst_plotcustomizationdialog::applyChangesRobustToSeriesListChange | PORTED | AroundTheChartTests.cs::SeriesGoneWhileTheDialogIsOpenAreSkipped |  |
| tst_exportdialog::testCheckboxTogglesFields | PORTED | ExportImportTests.cs::TheExportWindowHasItsThreeRowsAndEachFollowsItsBox, ::TheDialogStartsWithTheImageTickedAndEachRowFollowsItsBox |  |
| tst_exportdialog::testExportButtonValidation | PORTED | ExportImportTests.cs::ExportWaitsForATickedRowAndAPathForEach, ::TheExportWindowHasItsThreeRowsAndEachFollowsItsBox |  |
| tst_exportdialog::testLogRowDefaultsAndAccessors | PORTED | ExportImportTests.cs::TheDialogStartsWithTheImageTickedAndEachRowFollowsItsBox, ::TheExportWindowHasItsThreeRowsAndEachFollowsItsBox |  |
| tst_exportdialog::testLogOnlyExportValidation | PORTED | ExportImportTests.cs::ExportWaitsForATickedRowAndAPathForEach |  |
| tst_mainview::constructsAndDestroysWithoutCrash | PORTED | MainWindowTests.cs::TheWindowIsTitledTmDataQualityAnalyzer (and every MainWindow Show/Close) | Implicit |
| tst_mainview::windowTitleIsNonEmpty | PORTED | MainWindowTests.cs::TheWindowIsTitledTmDataQualityAnalyzer | Checks the exact title |
| tst_mainview::titleBarExists | PORTED | ShellTests.cs::TheTitleBarIsTheApplicationsOwnAndTellsWindowsWhatEachPartIs |  |
| tst_mainview::supportedFileDetection | PORTED | PreferencesTests.cs::ARecordingOrAnExportedCsvIsOpenedByItsSuffix; ShellTests.cs::TheFirstRecordingOrCsvAmongTheFilesIsTheOneTaken | Same inputs, case-insensitive |
| tst_mainview::openPathImportsCsv | PORTED | PreferencesTests.cs::ARecentFileOpensByItsSuffixAndNotWhileBusy; ShellTests.cs::ARecordingOrCsvDroppedOnTheWindowOpensAsRecentFilesOpensIt |  |
| tst_mainview::importValidCsvPopulatesPlot | PORTED | ExportImportTests.cs::ImportCsvShowsTheFileTitledByItsNameAndSaysSo |  |
| tst_mainview::importInvalidCsvIsRejected | PORTED | ExportImportTests.cs::ACsvThatIsNotAnExportIsAnErrorAndLeavesThePlotEmpty; PreferencesTests.cs::TheMainWindowRemembersItsFolderItsRecentFilesAndItsChoices | Not logged as a success; not added to recent files |
| tst_mainview::fullManualPathEmptyWhenNotInstalled | PORTED | ShellTests.cs::TheManualIsTheFullOneWhenInstalledElseTheBaseOneCompiledIn | The null/empty-root case is not asserted |
| tst_mainview::fullManualPathFoundWhenInstalled | PORTED | ShellTests.cs::TheManualIsTheFullOneWhenInstalledElseTheBaseOneCompiledIn |  |
| tst_mainview::helpSubmenuHoldsManualAndAbout | PORTED | ShellTests.cs::TheMenuHoldsItsSectionsHelpAndExit |  |
| tst_mainview::exitIsTheLastMenuEntry | PORTED | ShellTests.cs::TheMenuHoldsItsSectionsHelpAndExit | Full menu order checked, Exit last |
| tst_mainview::lightThemeReachesNativeWindowFrames | PORTED | ShellTests.cs::TheApplicationsThemeIsEveryWindowsAndRemembered | App-level theme variant; claude.md says the real frames were checked on the running app |
| tst_mainview::openAndSaveTemplateCarryIcons | PORTED | ShellTests.cs::TheMenuHoldsItsSectionsHelpAndExit, ::EachThemesBrushesAreV214s | Icon source non-null; no check that the glyph actually draws |
| tst_mainview::menuSectionHeadersSurviveTheStylesheet | PORTED | ShellTests.cs::TheMenuHoldsItsSectionsHelpAndExit, ::TheMenuIsBuiltAsItOpensAndIsEmptyWithoutAViewModel | Disabled headers; bold 11pt in the flyout |
| tst_mainview::aFinishedStreamIsLoggedUnderItsLabel | PORTED | ProcessingTests.cs::TheLogTellsTheStoryOfARunAsV214Did; Core SeriesTests.cs ("Ch 26 é" round trip); ArenaTests.cs | Pieced together: the log line is only asserted with ASCII labels |
| tst_mainview::logPreservesMultiLineMessages | PORTED | ProcessingTests.cs::AMultiLineMessageKeepsItsLinesAndTheirIndent, ::EachLineIsTimedByTheClockAndKeepsItsOwnLineBreaks |  |
| tst_mainviewmodel_helpers::classifyLogMessageSeverity | PORTED | ProcessingTests.cs::ALinesLevelIsDecidedByItsText | Same cases plus edge cases |
| tst_mainviewmodel_helpers::buildStreamJobAttachesCalibrationByWord | PORTED | test_streamjob.cpp::"every configuration builds, or is refused, as recorded"; test_abi.cpp::"aNonLinearProfileReachesTheJob" | Rows for attached, missing word and invalid profile, plus the log line |
| tst_mainviewmodel_helpers::buildStreamJobDecodesTheSyncPatternAndMask | PORTED | test_framesync.cpp::"parseUnsignedHex agrees with Qt on awkward inputs", ::"resolveFrameSync decodes pattern, mask and length" |  |
| tst_mainviewmodel_helpers::buildStreamJobBuildsTheAcquisitionParameters | PORTED | test_framesync.cpp::"applyAcquisition …" cases; test_streamjob.cpp recorded rows; test_abi.cpp::"everySettingOfAStreamReachesTheJob", ::"theByteOrderOfTheFileReachesTheJob" | Sample periods, rate, words, swap, "Ch N" label |
| tst_mainviewmodel_helpers::buildStreamJobReportsEachWayAConfigIsRejected | PORTED | test_streamjob.cpp::"the builder makes every generated job as the code it replaced did"; test_framesync.cpp::"resolveFrameSync reports the first problem…"; test_abi.cpp::"settingsTheCoreRejectsAreRefusedWithTheCoresMessage" | Empty-pattern message held only through the oracle comparison |
| tst_mainviewmodel_helpers::openFileListsItsChannelsAndOffersThemAsStreams | PORTED | ConfigureStreamsTests.cs::EveryPcmChannelIsARowAndNothingIsCheckedYet; Core FileTests.cs::EachRecordingListsItsChannelsExactly; test_chapter10reader.cpp::"a combo-box index selects the channel at that position" |  |
| tst_mainviewmodel_helpers::openFileOnAFileThatCannotBeReadReportsOneErrorAndKeepsNoChannels | PORTED | test_chapter10reader.cpp::"a failed load reports its reason and leaves no channels"; ProcessingTests.cs::AFileThatCannotBeOpenedIsLoggedInRed; WalkingSkeletonTests.cs::AFailedLoadClearsThePreviousRecordingsSeries | New tests use a junk file, not a missing one |
| tst_mainviewmodel_helpers::openFileShowsANonAsciiChannelNameIntact | PORTED | test_chapter10reader.cpp::"TMATS text decides which channels are listed…" (row name-non-ascii) | Same patched fixture, but native only; no .NET test reads a non-ASCII channel name |

### The plot's view model, the chart, CSV and series building

`tst_plotviewmodel`, `tst_tmchart`, `tst_csvseries`, `tst_seriesbuild` (the Qt-bound halves). The five `removeSource` cases are dropped because nothing in the Qt application calls `removeSource`: it is defined in `PlotViewModel` and `MainViewModel` and has no caller in `src/`, so no user could reach it.

| Qt case | Verdict | Held by | Note |
|---|---|---|---|
| plotviewmodel::defaultState | PORTED | PlotTests.cs::ClearingForgetsTheRecording | Cleared state checked (no data, default title, no override); a fresh VM only in parts |
| plotviewmodel::loadCsvFile | PORTED | ExportImportTests.cs::AnImportedPlotIsShownAsARunsWouldBe | Names, count, one SeriesChanged |
| plotviewmodel::csvTimeConversion | PORTED | ExportImportTests.cs::AnImportedPlotIsShownAsARunsWouldBe | Elapsed X across midnight, view 0..XMax |
| plotviewmodel::seriesColorAssignment | PORTED | PlotTests.cs::EachStreamAndEachReceiverTakesItsColour | Exact RGB per receiver and channel shade |
| plotviewmodel::yAutoRange | PORTED | PlotTests.cs::TheSnrAxisIsRoundedToFiveDecibelsAndAnOverrideNeverInvertsIt | 12.3/47.1 gives 10..50 |
| plotviewmodel::snrYMaxOverride | PORTED | AroundTheChartTests.cs::EachAxisLimitIsAskedWithinBoundsThatCannotInvertTheAxis; PlotTests.cs::TheSnrAxisIsRounded… | Override and Reset checked; range-changed signal count not |
| plotviewmodel::yMinOverridesAndTheSpanGuard | PORTED | PlotTests.cs::TheSnrAxisIsRounded…, ::TheLeftAxisIsLockPercentOrMissedFrames…; AroundTheChartTests.cs::EachAxisLimitIsAsked…, ::TheViewModeSwitches…ResetViewResetsBothAxes | Minimum wins, maximum yields; reset clears all |
| plotviewmodel::zoomStackRemembersDiscreteZoomsOnly | PORTED | PlotTests.cs::ABandZoomIsRememberedAndZoomBackReturnsThroughEachWindow, ::AGestureMovesTheWindowWithinTheDataAndIsNotRemembered | Cap, oldest dropped, reset clears |
| plotviewmodel::xTimeWindow | PORTED | PlotTests.cs::AGestureMovesTheWindow…, ::ABandZoomIsRemembered… | Set and ResetXRange; signal count not checked |
| plotviewmodel::seriesVisibility | PORTED | PlotTests.cs::AnAxisIsDrawnOnlyWhenSomethingIsOnIt; AroundTheChartTests.cs::AQuietBatchOfVisibilityIsShownByOneRefresh | Unknown id is a no-op only via SetSeriesVisibleQuiet |
| plotviewmodel::clearData | PORTED | PlotTests.cs::ClearingForgetsTheRecording |  |
| plotviewmodel::plotTitleDefault | PORTED | PlotTests.cs::AStreamsSeriesComeFromTheCoreOnOneTimeAxis, ::ClearingForgetsTheRecording |  |
| plotviewmodel::plotTitleChange | PORTED | PlotTests.cs::TheTitleIsAnnouncedWhenItChangesAndOnlyThen (5.1), ::ATitleSetWhileShownIsDrawn (5.1) | The notice, the same title again telling nobody, and the chart redrawing |
| plotviewmodel::loadInvalidFile | PORTED | ExportImportTests.cs::AFileThatIsNotAnExportedCsvChangesNothing | Missing-file case |
| plotviewmodel::loadEmptyFile | PORTED | ExportImportTests.cs::AFileThatIsNotAnExportedCsvChangesNothing | "" row |
| plotviewmodel::formatTimeZeroElapsed | PORTED | ExportImportTests.cs::AnImportedPlotIsShownAsARunsWouldBe | FormatTime(0) is the file's first time |
| plotviewmodel::formatTimeDayBoundary | PORTED | ExportImportTests.cs::AnImportedPlotIsShownAsARunsWouldBe | 001:23:59:59 then 002:00:00:01 |
| plotviewmodel::formatTimeNegativeElapsed | PORTED | test_csvseries.cpp "csvseries: the clock stamp is what v2.14 wrote, for every generated time" | Hash oracle with 4154 negatives; no explicit 044:23:59:59 case |
| plotviewmodel::loadCsvHeaderOnly | PORTED | ExportImportTests.cs::AFileThatIsNotAnExportedCsvChangesNothing | Header-only is rejected |
| plotviewmodel::loadCsvMalformedRows | PORTED | ExportImportTests.cs::MalformedRowsAreSkippedWithAWarningThatCountsThem |  |
| plotviewmodel::lockSeriesMetricType | PORTED | ExportImportTests.cs::AnImportedPlotIsShownAsARunsWouldBe, ::EveryExportReadsBackAsItsSeries…; FormatsTests.cs::EveryCsvIsParsedAsTheCoreParsesIt | Suffix stripped, metric typed |
| plotviewmodel::lockSeriesColor | PORTED | PlotTests.cs::EachStreamAndEachReceiverTakesItsColour | First lock is purple primary, SNR red |
| plotviewmodel::importMultiStreamColorsAndVisibility | PORTED | ExportImportTests.cs::AnImportedPlotIsShownAsARunsWouldBe |  |
| plotviewmodel::importSnrStreamGrouping | PORTED | test_csvseries.cpp "csvseries: every import case reads as recorded"; ExportImportTests.cs::AnImportedPlotIsShownAsARunsWouldBe | channel-index-counts-per-receiver-and-stream row |
| plotviewmodel::importExportRoundTrip | PORTED | ExportImportTests.cs::EveryExportReadsBackAsItsSeriesAndExportsAgainByteForByte |  |
| plotviewmodel::lockAxisRange | PORTED | PlotTests.cs::AStreamsSeriesComeFromTheCoreOnOneTimeAxis | LeftMin/LeftMax 0..100 |
| plotviewmodel::hasLockSeriesTrue | PORTED | ExportImportTests.cs::AnImportedPlotIsShownAsARunsWouldBe; AroundTheChartTests.cs::TheViewModeSwitches… | True only shown through CanSwitchViewMode |
| plotviewmodel::hasLockSeriesFalse | PORTED | AroundTheChartTests.cs::TheViewModeSwitchesOnlyWhenBothMetricsExist… | Missed-only plot gives HasLockSeries false |
| plotviewmodel::yAutoRangeIgnoresLockSeries | PORTED | PlotTests.cs::TheSnrAxisIsRounded… | Lock 90..100 does not move the SNR range |
| plotviewmodel::loadCsvFileAsyncEmitsDataChanged | PORTED | ExportImportTests.cs::ACsvIsReadOffTheUiThreadWithLoadingShownAndASecondRequestDropped |  |
| plotviewmodel::loadCsvFileAsyncEmitsLoadFailed | PORTED | ExportImportTests.cs::ACsvIsReadOffTheUiThread… | Bad file gives Failed, data kept |
| plotviewmodel::addStreamDataLockSeriesCreated | PORTED | PlotTests.cs::AStreamsSeriesComeFromTheCoreOnOneTimeAxis | X[0]=0, lock metric |
| plotviewmodel::addStreamDataSNRSeriesCreated | PORTED | test_seriesbuild.cpp "seriesbuild: every scenario reads as recorded"; SeriesTests.cs::TheSeriesOfRealStreamsAreWhatTheCoreMakes | a-receiver-stream row |
| plotviewmodel::addStreamDataMultipleStreamsAccumulate | PORTED | PlotTests.cs::ASourceThatStartsEarlierMovesTheSharedBaseAndALateOneIsReported | Later source X = start gap |
| plotviewmodel::addStreamDataEmptyDataNoOp | NOT HELD | — (the native host's "seriesbuild" rows) | AddStream ignores a stream with no samples. Reaching it takes a stream that extracts frames yet completes no sample window, which no committed recording does; a stream that finds no sync, or no frames, is an error and never reaches the plot. Defensive, as the Qt guard was |
| plotviewmodel::addStreamDataErrorSeriesCreated | PORTED | PlotTests.cs::AStreamsSeriesComeFromTheCoreOnOneTimeAxis; SeriesTests.cs::TheValuesOfASeriesAreTheStreamsOwnData | Lock shown, missed hidden, values |
| plotviewmodel::seriesOfOneStreamShareOneTimeAxis | PORTED | PlotTests.cs::AStreamsSeriesComeFromTheCoreOnOneTimeAxis | Assert.Same(X) |
| plotviewmodel::aNonAsciiStreamLabelReachesEverySeries | PORTED | SeriesTests.cs::TheValuesOfASeriesAreTheStreamsOwnData, ::TheKeyOfAStreamIsItsSourceItsLabelAndItsChannel; test_seriesbuild.cpp "…every scenario reads as recorded" | "Ch 26 é" |
| plotviewmodel::csvImportKeepsNonAsciiSeriesNames | PORTED | FormatsTests.cs::EveryCsvIsParsedAsTheCoreParsesIt; test_csvseries.cpp "…every import case reads as recorded" | non-ascii-names file |
| plotviewmodel::csvExportWritesNonAsciiNamesAsUtf8 | PORTED | test_csvseries.cpp "csvseries: every export case writes the bytes recorded"; FormatsTests.cs::EveryCsvIsWrittenWithTheBytesTheCoreWrites | an-empty-and-a-non-ascii-label row |
| plotviewmodel::csvExportQualifiesEverySeriesOfALaterSource | PORTED | ExportImportTests.cs::ALaterSourcesColumnsAreQualifiedAndASourceCanBeExportedAlone | Prefix "S2\ |
| plotviewmodel::errorSeriesSharesLockColor | PORTED | PlotTests.cs::EachStreamAndEachReceiverTakesItsColour |  |
| plotviewmodel::setLockAxisViewTogglesVisibility | PORTED | PlotTests.cs::TheLeftAxisIsLockPercentOrMissedFrames…, ::StreamsKeepTheirJobOrderAndTheViewDecidesWhichMetricIsShown | Same-view no-op and signal counts not checked |
| plotviewmodel::setLockAxisViewPreservesStreamSelection | PORTED | PlotTests.cs::TheLeftAxisIsLockPercentOrMissedFrames… |  |
| plotviewmodel::frameSyncErrorMaxReflectsData | PORTED | PlotTests.cs::TheLeftAxisIsLockPercentOrMissedFrames… | Now max×1.05, floor 10×1.05 |
| plotviewmodel::reprocessOnlySameStreamOrderReplaced | PORTED | PlotTests.cs::AStreamProcessedAgainReplacesItsSeries; SeriesTests.cs::TwoStreamsAreTheSameOnlyIfSourceLabelAndChannelAllAgree | Pieces tested apart; app survival of the other stream not checked |
| plotviewmodel::renameSeriesOnlySameStreamOrderSiblingRenamed | PORTED | AroundTheChartTests.cs::ARenameOrRecolourReachesAStreamsOtherMetricAndNoOtherStream |  |
| plotviewmodel::recolorSeriesOnlySameStreamOrderSiblingRecolored | PORTED | AroundTheChartTests.cs::ARenameOrRecolour… |  |
| plotviewmodel::crossSourceReprocessDoesNotEraseOtherSource | PORTED | SeriesTests.cs::TwoStreamsAreTheSameOnly…, ::TheKeyOfAStreamIs…; PlotTests.cs::AStreamProcessedAgain… | Same split as the row above |
| plotviewmodel::crossSourceRenameDoesNotAffectOtherSource | PORTED | AroundTheChartTests.cs::ARenameOrRecolour… | otherSource (source 1) untouched |
| plotviewmodel::crossSourceRecolorDoesNotAffectOtherSource | PORTED | AroundTheChartTests.cs::ARenameOrRecolour… |  |
| plotviewmodel::addStreamDataRebasesWhenLaterSourceStartsEarlier | PORTED | PlotTests.cs::ASourceThatStartsEarlierMovesTheSharedBase… |  |
| plotviewmodel::addStreamDataNoRebaseWhenLaterSourceStartsAfter | PORTED | SeriesTests.cs::ATimeBaseWorksByHandForAFirstAnEarlierAndALaterStream | Core Adopt returns 0; no app-level check |
| plotviewmodel::addStreamDataRebasesTwiceForSuccessivelyEarlierSources | PORTED | TemplateBatchTests.cs::EachRecordingEarlierThanTheLastMovesTheTimeBaseAgain (5.1) | step_cal two days later, a day later, then as recorded: the files land a day apart |
| plotviewmodel::nonOverlappingSourceEmitsWarning | PORTED | PlotTests.cs::ASourceThatStartsEarlier…; TemplateBatchTests.cs::ARecordingThatSharesNoTimeWithTheOthersIsNamedInAWarning | "Not again for the same source" not checked in app |
| plotviewmodel::overlappingSourceDoesNotEmitWarning | PORTED | SeriesTests.cs::ATimeBaseWorksByHand… (Overlaps) | Core rule only; "no warning" not checked in app |
| plotviewmodel::removeSourceDropsOnlyThatSourcesSeries | DROPPED | — | New app has no RemoveSource; Qt API had no UI caller |
| plotviewmodel::removeSourceRebasesLeftWhenRemovedSourceWasEarliest | DROPPED | (core Advance: SeriesTests.cs::ATimeBaseWorksByHand…) | No RemoveSource in the new app |
| plotviewmodel::removeSourceNoRebaseWhenRemovedSourceWasNotEarliest | DROPPED | — | No RemoveSource in the new app |
| plotviewmodel::removeLastSourceClearsAllData | DROPPED | — | No RemoveSource in the new app |
| plotviewmodel::removeUnknownSourceIsNoOp | DROPPED | — | No RemoveSource in the new app |
| plotviewmodel::setVisibleSourceIsolatesSource | PORTED | TemplateTests.cs::ThePlotListsItsRecordingsAndShowsOneOrAll | EffectiveVisible; Visible untouched |
| plotviewmodel::sourceListListsDistinctLabeledSources | PORTED | TemplateTests.cs::ThePlotListsItsRecordingsAndShowsOneOrAll |  |
| plotviewmodel::exportCsvSourceFilterWritesOnlyThatSource | PORTED | ExportImportTests.cs::ALaterSourcesColumnsAreQualifiedAndASourceCanBeExportedAlone |  |
| plotviewmodel::exportCsvCreatesFile | PORTED | ExportImportTests.cs::AnExportHoldsEverySeriesShownOrNotAndNothingToWriteWritesNoFile |  |
| plotviewmodel::exportCsvHeaderAndData | PORTED | ExportImportTests.cs::EveryRecordingExportsTheCsvV214ExportedByteForByte, ::AnExportHoldsEverySeries… |  |
| plotviewmodel::exportCsvEmptyNoFile | PORTED | ExportImportTests.cs::AnExportHoldsEverySeries… |  |
| plotviewmodel::exportCsvIncludesErrorColumn | PORTED | ExportImportTests.cs::AnExportHoldsEverySeries… | Exact Lock/Missed header |
| plotviewmodel::exportCsvRoundTripsThroughLoad | PORTED | ExportImportTests.cs::EveryExportReadsBackAsItsSeriesAndExportsAgainByteForByte |  |
| tmchart::constructsEmpty | PORTED | PlotTests.cs::AnEmptyChartIsAFrameWithNothingInsideAndIgnoresTheWheel |  |
| tmchart::seriesAddRemoveAndClear | DROPPED | — | PlotChart has no series index API; it draws the VM's series |
| tmchart::seriesDataTruncatesMismatchedLengths | PORTED | ChartGeometryTests.cs::OnlyThePairedSamplesCountAndNoSamplesIsNoLine; test_chartgeometry.cpp "ChartGeometry: a run of one column has no band, and only the paired samples count" |  |
| tmchart::rejectsDegenerateAndNonFiniteRanges | PORTED | ChartGeometryTests.cs::SortednessAndUsableRangesAreTheCores | Core UsableRange; app clamps windows |
| tmchart::xTransformRoundTrips | PORTED | PlotTests.cs::AHiddenAxisGivesItsMarginBackAndTheTransformsFollowThePlotArea; ChartGeometryTests.cs::EveryFieldOfTheViewIsUsedInItsPlace |  |
| tmchart::yTransformsAreOrientedAndIndependent | PORTED | PlotTests.cs::AHiddenAxisGivesItsMarginBack…; test_chartgeometry.cpp "ChartGeometry: the transforms orient Y upward, round-trip, and fall back to an edge" |  |
| tmchart::tickValuesSpanRangeInclusive | PORTED | ChartGeometryTests.cs::PlainTicksDivideTheRangeEvenly; test_chartgeometry.cpp "ChartGeometry: plain ticks divide the range evenly, both ends included" |  |
| tmchart::timeTicksLandOnRoundTimes | PORTED | ChartGeometryTests.cs::TimeTicksLandOnRoundTimesOfTheClock; test_chartgeometry.cpp "ChartGeometry: time ticks land on round times of the clock" |  |
| tmchart::timeTicksAreRoundOnTheClockNotInElapsedSeconds | PORTED | ChartGeometryTests.cs::TimeTicksLandOnRoundTimesOfTheClock; test_chartgeometry.cpp same | Origin 37817.046 |
| tmchart::wheelZoomAnchorsUnderCursorAndReportsRange | PORTED | PlotTests.cs::TheWheelZoomsAboutTheCursor |  |
| tmchart::interactionsDisabledIgnoresWheel | PORTED | PlotTests.cs::AnEmptyChartIsAFrameWithNothingInsideAndIgnoresTheWheel | Empty chart stands in for "disabled" |
| tmchart::hiddenAxisReclaimsItsMargin | PORTED | PlotTests.cs::AHiddenAxisGivesItsMarginBackAndTheTransformsFollowThePlotArea |  |
| tmchart::renderToPaintsData | PORTED | PlotTests.cs::AnExportOmitsTheCursorsOverlays; ExportImportTests.cs::APngIsTheChartAtItsSizeWithTheLegendWhereItIs | Export helper asserts something drawn |
| tmchart::denseSeriesStillDrawsASingleSampleDropout | PORTED | PlotTests.cs::ADenseSeriesStillDrawsASingleSampleDropoutAndItsSpread; ChartGeometryTests.cs::ADenseSeriesStillReachesASingleSampleDropout |  |
| tmchart::denseSeriesShowsItsSpreadNotJustItsMean | PORTED | PlotTests.cs::ADenseSeriesStillDraws…AndItsSpread; ChartGeometryTests.cs::ADenseNoisySeriesShowsItsSpreadWithItsMeanThroughTheMiddle |  |
| tmchart::linePersistsWhenNoSampleIsInsideTheView | PORTED | PlotTests.cs::EveryKindOfSeriesIsDrawnFromTheCoresGeometryInItsPlace; ChartGeometryTests.cs::ALineCrossesAViewNoSampleIsInAndAGapBreaksIt |  |
| tmchart::drawsTheCoresGeometryForEveryKindOfSeries | PORTED | PlotTests.cs::EveryKindOfSeriesIsDrawnFromTheCoresGeometryInItsPlace |  |
| tmchart::rangeChangeRedrawsInsteadOfReusingGeometry | PORTED | PlotTests.cs::EveryKindOfSeriesIsDrawn…, ::TheCrosshairAndReadoutFollowTheCursorOverTheCachedPlot | Redraw counted on range change |
| tmchart::cachedPaintStillFollowsTheData | PORTED | PlotTests.cs::TheCrosshairAndReadoutFollowTheCursorOverTheCachedPlot | DataLayerRenders count on range and data change |
| tmchart::crosshairDrawsOverTheCachedPlot | PORTED | PlotTests.cs::TheCrosshairAndReadoutFollowTheCursorOverTheCachedPlot | Only the column changes; no data re-render |
| tmchart::exportOmitsCursorOverlays | PORTED | PlotTests.cs::AnExportOmitsTheCursorsOverlays; ExportImportTests.cs::TheCursorsCrosshairIsNotExported | Crosshair only; zoom band not checked |
| csvseries::importEdgeCasesKeepTheirRecordedBehaviour | PORTED | test_csvseries.cpp "csvseries: every import case reads as recorded"; SeriesTests.cs::AParsedCsvsStreamsAreOrderedAsV214OrderedThem; ExportImportTests.cs::MalformedRowsAreSkipped… | All 10 rows in native |
| csvseries::exportEdgeCasesKeepTheirRecordedBehaviour | PORTED | test_csvseries.cpp "csvseries: every export case writes the bytes recorded"; ExportImportTests.cs::ALaterSourcesColumnsAreQualified… | All 9 rows in native |
| csvseries::csvPathsMayBeNonAscii | PORTED | test_csvseries.cpp "csvseries: a CSV in a folder with a non-ASCII name is written and read back" | Managed CSV path not checked; pattern-file analogue is |
| csvseries::exportOfNothingWritesNothing | PORTED | ExportImportTests.cs::AnExportHoldsEverySeries…, ::ALaterSourcesColumnsAreQualified… |  |
| csvseries::exportToAnUnwritablePathFails | PORTED | ExportImportTests.cs::AnExportHoldsEverySeries…; test_csvseries.cpp "csvseries: writing nothing writes no file, and an unwritable path fails" |  |
| csvseries::exportThenImportThenExportIsByteIdentical | PORTED | ExportImportTests.cs::EveryExportReadsBackAsItsSeriesAndExportsAgainByteForByte; test_csvseries.cpp "csvseries: export, import and export again is byte for byte the same" |  |
| seriesbuild::scenariosKeepTheirRecordedBehaviour | PORTED | test_seriesbuild.cpp "seriesbuild: every scenario reads as recorded"; PlotTests.cs (AStreams…, StreamsKeepTheirJobOrder…, AStreamProcessedAgain…, ASourceThatStartsEarlier…); ExportImportTests.cs::AnImportedPlotIsShownAsARunsWouldBe | Remove-source rows exist only in the native host |

### Configure Streams, templates, the batch, processing, pattern files and the guards

`tst_streamconfigdialog`, `tst_streamconfigschema`, `tst_processingtemplateschema`, `tst_batchcontroller`, `tst_processingcoordinator`, `tst_framesyncfile`, `tst_qtprocessingevents`, `tst_coreqtfree`, `tst_constants`, `tst_streamjob`.

| Qt case | Verdict | Held by | Note |
|---|---|---|---|
| tst_streamconfigdialog::constructionWithEmptyConfigs | PORTED | WalkingSkeletonTests.ARecordingWithNoPcmChannelSaysSo | Behaviour changed: a recording with zero channels never reaches Configure Streams; the status line says so. |
| tst_streamconfigdialog::constructionWithOneStream | PORTED | ConfigureStreamsTests.EveryPcmChannelIsARowAndNothingIsCheckedYet; StreamWindowsTests.ConfigureStreamsShowsARowPerChannelAndTheRecordingsSettings |  |
| tst_streamconfigdialog::configsRoundtripDefaultValues | PORTED | StreamSetupViewModelTests.TheDialogOpensWithTheRowAndIsNamedForItsMode (`ToSetup()==row`); ConfigureStreamsTests.EveryPcmChannelIsARow… | Label and mode survive. |
| tst_streamconfigdialog::configsRoundtripFrameSyncFields | PORTED | StreamSetupViewModelTests.TheDialogOpensWithTheRow…, WhatTheDialogHoldsIsTheSetupItGivesBack |  |
| tst_streamconfigdialog::configsRoundtripWordsInFrame | PORTED | same two | Bits per frame 1600 and 800 both round-trip. |
| tst_streamconfigdialog::configsRoundtripProcessFlag | PORTED | ConfigureStreamsTests.EveryPcmChannelIsARow…; StreamRulesTests.AcceptingConfiguresTheRowInItsModeWithWhatTheDialogHeld | Process is never taken from the dialog. |
| tst_streamconfigdialog::subDialogsRoundTripAStreamConfig | PORTED | StreamSetupViewModelTests.TheDialogOpensWithTheRow…, WhatTheDialogHolds…; StreamRulesTests.TheCoreGetsTheRowWithItsHexTrimmedAndUpperCased | Apply to all off at first: TheDialogOpensWithTheRowAndIsNamedForItsMode (5.1) |
| tst_streamconfigdialog::calibrationDialogPreloadsTheStepFile | PORTED | CalibrationDialogTests.TheDialogOpensOnTheStreamsStepFileElseTheShippedOne | Default file, stream's own file, missing file, empty cal path and zero clips all covered. |
| tst_streamconfigdialog::calibrationDialogReportsAnUnusableSyncWithoutExtracting | PORTED | CalibrationDialogTests.ASettingThatCannotBeReadIsACrossBeforeAnythingStarts, AFileThatIsNotARecordingOrNoSyncPatternIsACross | An empty pattern now gets its own refusal message. |
| tst_streamconfigdialog::calibrationDialogExtractsThroughItsProgressDialog | PORTED | CalibrationDialogTests.TheCalibrationRecordingIsExtractedWithTheProgressDialogUp | 12 of 48 channels, the report text, the ✓ mark. |
| tst_streamconfigdialog::calibrationDialogHandsTheRawAffectingSettingsToTheExtraction | PORTED | CalibrationDialogTests.OnlyTheRecordingsOwnByteOrderInversionAndRandomizationCalibrate | Same four rows. |
| tst_streamconfigdialog::calibrationDialogHandsTheDataRateToTheExtraction | PORTED | CalibrationDialogTests.ClipsBeyondTheRecordingAreIgnoredAndSaySoAtTheStreamsDataRate | Half the rate gives about twice the recording length. |
| tst_streamconfigdialog::tomlFrameSyncSaveRoundtrip | PORTED | StreamFilesTests.APatternFileRoundTripsTheFrameSyncAndBothFlags; native test_tomldocument.cpp | The Qt case only exercised TomlDocument directly. |
| tst_streamconfigdialog::tomlFrameSyncLoadPopulatesThreeFields | PORTED | native "framesyncfile: each file says what it says, case by case"; StreamFilesTests.APatternFileInLowerCaseLoadsUpperCased | The old WordsInMinorFrame key is a native row. |
| tst_streamconfigdialog::tomlFrameSyncRoundTripsStreamFormatFlags | PORTED | StreamFilesTests.APatternFileRoundTripsTheFrameSyncAndBothFlags, AnOlderPatternFileLeavesDerandomizeAloneAndClearsInvert |  |
| tst_streamconfigdialog::tomlFrameSyncOmitsPerSessionFields | PORTED | StreamFilesTests.ThePatternFileLeavesOutThePerSessionFields; native "writing replaces the whole file, and Data Rate and Sample Rate are not in it" |  |
| tst_streamconfigdialog::validateRejectsCheckedStreamWithEmptyFrameSync | PORTED | StreamRulesTests.ACheckedRowIsReadyOnceConfiguredWithAPattern, ProcessWaitsUntilEveryCheckedRowIsReady, WhyNotProcessNamesTheFirstRowInTheWay |  |
| tst_streamconfigdialog::testFrameSyncValidation | PORTED | StreamRulesTests.ASyncFieldTakesOneToSixteenHexDigits (A34F/ZXY rows); StreamWindowsTests.TypingAnInvalidPatternShowsTheProblemAndHoldsOk |  |
| tst_streamconfigdialog::testFrameMaskValidation | PORTED | StreamRulesTests.ASyncFieldTakesOneToSixteenHexDigits (FFFF/FFZZ rows), AFrameSyncProblemIsNamed |  |
| tst_streamconfigdialog::testDataRateLimits | PORTED | StreamRulesTests.TheDataRateIsFromZeroToAThousand |  |
| tst_streamconfigdialog::testDefaultSampleRate | PORTED | StreamWindowsTests.TheFrameSyncLockDialogHidesTheReceiverSettings ("1 s"); StreamSetupViewModelTests index→period; StreamSetupViewModelTests.TheChoicesAreUs21sAndUs30sInTheirOrder (5.1) | The three labels in order |
| tst_streamconfigdialog::applyToAllCopiesSettingsToSameModeStreams | PORTED | StreamRulesTests.ApplyToAllCopiesSettingsToSameModeStreams; ConfigureStreamsTests.ApplyToAllCopiesToTheOtherCheckedRowsOfTheMode |  |
| tst_streamconfigdialog::applyToAllLeavesDifferentModeStreamsUnchanged | PORTED | StreamRulesTests.ApplyToAllLeavesDifferentModeStreamsUnchanged; ConfigureStreamsTests.ApplyToAllCopies… |  |
| tst_streamconfigdialog::applyToAllUncheckedDoesNotAffectOtherStreams | PORTED | ConfigureStreamsTests.TwoStreamsConfiguredAndProcessedAreBothDrawn (implicit) | ApplyToAllLeavesUncheckedStreamsUnchanged tests unchecked rows, not the Apply-to-all box left off. |
| tst_streamconfigdialog::channelLabelShortNameShownInFull | PORTED | StreamWindowsTests.ConfigureStreamsShowsARowPerChannelAndTheRecordingsSettings; StreamWindowsTests.AChannelNameTooLongForItsColumnIsTrimmedWithTheWholeNameAsItsTooltip (5.1) | Untrimmed, measured from the text layout |
| tst_streamconfigdialog::channelLabelLongNameElidedWithFullTooltip | PORTED | StreamWindowsTests.AChannelNameTooLongForItsColumnIsTrimmedWithTheWholeNameAsItsTooltip (5.1) | Trimmed within its 260 px column; the whole name as the tooltip |
| tst_streamconfigdialog::headerLabelsUseThemeableObjectNames | PORTED | StreamWindowsTests.TheColumnHeadersReadInEitherTheme (5.1) | Light on the dark theme, dark on the light |
| tst_streamconfigdialog::channelLabelHasComboBoxStyledObjectName | DROPPED | none | A Qt QSS hook to make the label look like the combo; the Avalonia cell is a plain TextBlock. |
| tst_streamconfigdialog::modeComboDisplaysTextLeftJustified | DROPPED | none | A Qt workaround (editable read-only combo); there is nothing like it in Avalonia. |
| tst_streamconfigdialog::modeComboSelectionStillTracksIndexChange | PORTED | StreamWindowsTests.TheReadyColumnAndProcessFollowTheRows; ConfigureStreamsTests.TheDialogOpensOnTheOtherModesDefaultsAfterAModeChange |  |
| tst_streamconfigdialog::byteOrderToggleIsFileLevel | PORTED | StreamWindowsTests.ConfigureStreamsShowsARowPerChannel…, TheAllBoxAndLegacyChapter10ReachTheViewModel; ConfigureStreamsTests.TheRunIsTheCheckedRowsWithTheRecordingsSettings | One named control; the byte swap lives on the RunRequest only. |
| tst_streamconfigdialog::legacyToggleIsInverseOfByteSwap | PORTED | StreamRulesTests.LegacyChapter10IsTheInverseOfTheByteSwap; ConfigureStreamsTests.TheRunIsTheCheckedRows…; StreamWindowsTests (unticked at open) |  |
| tst_streamconfigschema::fromJsonClampsOutOfRangeComboIndices | PORTED | TemplateTests.AValueOfTheWrongTypeKeepsItsDefaultAndIndicesAreClamped | High and negative values clamp; in-range values come from the Qt fixtures. |
| tst_streamconfigschema::roundTripFrameSyncLockConfig | PORTED | TemplateTests.TheV214TemplatesReadAsTheyWereWritten, ATemplateIsWrittenAsV214WroteIt | Qt-written fixtures are read and rewritten byte for byte. |
| tst_streamconfigschema::roundTripReceiverChannelInfoConfig | PORTED | same (calibrated.json) |  |
| tst_streamconfigschema::roundTripPreservesCalibrationInputReferences | PORTED | same (calibrated.json) |  |
| tst_streamconfigschema::toJsonNeverIncludesCalibrationByWord | PORTED | TemplateTests.WhatATemplateLeavesOutIsLeftOutAndAControlCharacterIsEscaped |  |
| tst_streamconfigschema::toJsonOmitsCalibrationBlockWhenNoCalCh10Path | PORTED | same |  |
| tst_streamconfigschema::fromJsonRejectsObjectMissingRequiredFields | PORTED | TemplateTests.AMissingKeyKeepsItsDefaultAndAnEntryNeedsAChannelAndALabel | The entry is skipped. |
| tst_streamconfigschema::fromJsonLeavesDefaultsWhenOptionalFieldsMissing | PORTED | same |  |
| tst_processingtemplateschema::roundTripFrameSyncLockEntry | PORTED | TemplateTests.TheV214TemplatesReadAsTheyWereWritten, ATemplateIsWrittenAsV214WroteIt |  |
| tst_processingtemplateschema::roundTripReceiverChannelInfoEntry | PORTED | same |  |
| tst_processingtemplateschema::roundTripPreservesAppearance | PORTED | same; AnAppearanceReadsItsMetricAndColourAsV214ReadThem |  |
| tst_processingtemplateschema::toJsonOmitsAppearanceWhenEmpty | PORTED | TemplateTests.WhatATemplateLeavesOutIsLeftOut… |  |
| tst_processingtemplateschema::roundTripPreservesCalibrationInputReferences | PORTED | TemplateTests.TheQtApplicationsTemplatesRead… (calibrated.json) |  |
| tst_processingtemplateschema::toJsonNeverIncludesCalibrationByWord | PORTED | TemplateTests.WhatATemplateLeavesOutIsLeftOut… |  |
| tst_processingtemplateschema::fromJsonAcceptsCurrentSchemaVersion | PORTED | TemplateTests.ATemplateIsRefusedForWhatV214Refused (v1 row); WhatATemplateLeavesOut… (writes v1) |  |
| tst_processingtemplateschema::fromJsonRejectsNewerSchemaVersion | PORTED | TemplateTests.ATemplateIsRefusedForWhatV214Refused |  |
| tst_processingtemplateschema::fromJsonRejectsMissingSchemaVersion | PORTED | same |  |
| tst_processingtemplateschema::fromJsonRejectsMissingEntriesArray | PORTED | same |  |
| tst_processingtemplateschema::fromJsonRejectsNonObjectDocument | PORTED | same ("[]" row) |  |
| tst_processingtemplateschema::swapBytesRoundTripsAndDefaultsTrue | PORTED | TemplateTests.TheQtApplicationsTemplatesRead… (SwapBytes false), AMissingKeyKeepsItsDefault… (true) |  |
| tst_batchcontroller::buildTemplateCapturesConfigsAndAppearance | PORTED | TemplateTests.ATemplateIsCapturedFromOneRecordingAndItsSeriesAsTheyLook |  |
| tst_batchcontroller::applyAppearanceRenamesMatchingSeries | PORTED | TemplateTests.ATemplatesLooksGoBackOnOneRecordingsMatchingSeriesInOneRefresh |  |
| tst_batchcontroller::missingFilesAreSkippedAndReported | PORTED | TemplateBatchTests.ABatchRunsEachFileInTurnKeepsEachAndExportsEachAsV214Did | One missing file among good ones, not an all-missing batch; skip warning and counts asserted. |
| tst_batchcontroller::validateComparesEachFilesChannelsToTheTemplate | PORTED | TemplateTests.AFileMatchesOnItsChannelIdsAloneAndAMismatchIsSpelledOut |  |
| tst_batchcontroller::matchFileComparesChannelIdsAndNothingElse | PORTED | same; native test_templatematcher.cpp | "Ids only" holds because Match takes a list of ids. |
| tst_batchcontroller::aFileWhoseChannelsNoLongerMatchIsSkippedAtItsTurn | PORTED | TemplateBatchTests.ABatchRunsEachFileInTurn… (step_cal skipped at its turn) |  |
| tst_batchcontroller::startWithNoFilesDoesNothing | PORTED | TemplateBatchTests.ATemplatesNamesAndColoursGoOnEveryFileWhenAskedAndAFailedFileIsSaidSo (5.1) | Nothing logged, the status and the plot as they were |
| tst_processingcoordinator::constructorDefaults | DROPPED | none | Qt adapter state flags; there is no idle run object in .NET. |
| tst_processingcoordinator::aRecordingUnderANonAsciiFolderProcessesEndToEnd | PORTED | RunTests.ANonAsciiFolderRunsUnderTheUtf8CodePage; FileTests.ANonAsciiFolderOpensWhenTheHostRunsUnderTheUtf8CodePage |  |
| tst_processingcoordinator::workerReportsPrecedeTheResultAndFinishComesLast | PORTED | native "PipelineRunner: a single stream delivers its result, then the end"; RunTests.AnObserverIsToldInTheDocumentedOrder | The Qt coordinator's own second 100% is gone; the reader's 100 arrives once. |
| tst_processingcoordinator::resetClearsState | DROPPED | none | There is no reset(); each run is a fresh ProcessingRun. |
| tst_processingcoordinator::cancelProcessingNoRunNoOp | PORTED | RunTests.DisposingARunInFlightCancelsAndWaits, CancellingEndsARunUnsuccessfullyAndDeliversNothing | Cancel on an ended or disposed run is harmless; there is no idle state to cancel. |
| tst_processingcoordinator::cancelProcessingEndsTheRunWithoutAResult | PORTED | RunTests.CancellingEndsARun…; native "cancel ends the run unsuccessfully with no result"; ProcessingTests.CancelInTheProgressDialogStopsTheRunAndShowsNothing |  |
| tst_processingcoordinator::destroyingTheCoordinatorMidRunIsSafe | PORTED | RunTests.DisposingARunInFlightCancelsAndWaits; native "destroying a running runner stops it and reports the end" |  |
| tst_processingcoordinator::startProcessingEmptyReturnsFalse | PORTED | native "PipelineRunner: no jobs is refused"; RunTests.AnIncompleteRequestIsABadArgument |  |
| tst_processingcoordinator::startProcessingEmitsProcessingState | PORTED | RunTests.ARecordingThatCannotBeUsedIsAPrepareError; native "a recording that cannot be opened fails before anything runs" |  |
| tst_processingcoordinator::benchmarkSingleVsMultiStreamThroughput | PORTED | native "several streams on one channel each deliver their own result", "PipelineRunner throughput…" | The Qt case asserted only completion; the native throughput case is opt-in. |
| tst_framesyncfile::loadEdgeCasesKeepTheirRecordedBehaviour | PORTED | native "each file says what it says"; StreamFilesTests (LowerCase, WithoutAPatternOrMask, FrameLengthOutsideRange, MissingPatternFile, OlderPatternFile) | Qt upper-cased ß to SS; C# ToUpperInvariant leaves ß as is, and no test asserts it. |
| tst_framesyncfile::loadingWithoutTheFlagBoxesFillsTheEditorsTheSame | DROPPED | none | Covered the Qt function's optional null flag-box parameters; ApplyFrameSyncFile has no such variant. |
| tst_framesyncfile::saveEdgeCasesKeepTheirRecordedBehaviour | PORTED | native "writing puts exactly these bytes in the file"; StreamFilesTests.APatternFileIsWrittenTrimmedAndUpperCased | Deliberate change: Qt wrote text as typed; the new app trims and upper-cases. |
| tst_framesyncfile::receiverDefaultsEdgeCasesKeepTheirRecordedBehaviour | PORTED | StreamFilesTests.TheReceiverSnrDefaultsFileIsReadFieldByField, WhatTheReceiverSnrDefaultsFileDoesNotSay…, AFrameLengthFromTheOlderKey…, NoReceiverSnrDefaultsFile… | The below-64, 70000 and other-section rows rest on native read rows. |
| tst_framesyncfile::theCoreReadsEveryGeneratedFileLikeTheOldDialogCode | PORTED | native "the core reads every generated file like the code it replaced"; FormatsTests corpus | How the dialog applies a read file is not compared against the old code. |
| tst_framesyncfile::theCoreWritesEveryGeneratedFileLikeTheOldDialogCode | PORTED | native "the core writes every generated file like the code it replaced" |  |
| tst_framesyncfile::aFileWrittenByTheDialogLoadsBackIntoIt | PORTED | StreamFilesTests.APatternFileRoundTrips…; StreamSetupViewModelTests.LoadingAPatternFileShowsItAndRemembersTheFolder, SavingAPatternFileWritesWhatIsShown |  |
| tst_qtprocessingevents::deliversOnTheTargetsThreadInOrder | PORTED | ProcessingTests.TheLogTellsTheStoryOfARunAsV214Did, TheJobLinesComeFirstAndWhatTheThreadsSayFollowsTheSummary; RunTests.AnObserverIsToldInTheDocumentedOrder | The Qt bridge is gone; UI-thread delivery and ordering are held by these. |
| tst_qtprocessingevents::progressArrivesOnTheTargetsThreadInOrder | PORTED | ProcessingTests.TheProgressDialogIsUpForTheRunAndShowsTheCoresProgress (5.1) | In order, and every report on the UI thread |
| tst_qtprocessingevents::messagesAreDecodedFromUtf8 | PORTED | ProcessingTests.TheRunLogsTheJobLinesThenTheSummaryThenWhatTheRunSays (core's em dash); native EventReporter "arguments pass through unchanged" |  |
| tst_qtprocessingevents::missingHandlersAreSkipped | PORTED | RunTests.ARunWithNoObserverJustRuns, AnObserverThatDoesNotOverrideCompletionStillReleasesTheResult; native EventReporter "with no sink…" |  |
| tst_qtprocessingevents::eventsForADestroyedTargetAreDropped | DROPPED | (related: RunTests.DisposingARunInFlightCancelsAndWaits) | A claim about Qt's event queue; .NET's Dispose cancels and waits instead. |
| tst_coreqtfree::theProcessingCoreIncludesNoQtHeader | PORTED | CI core-without-qt job; build_core_dll.ps1 import allow-list | A build guard, not a test. A forward-declared Q class would no longer be caught (harmless). |
| tst_coreqtfree::theProcessingCoreDoesNotReachIntoTheViewModelOrView | DROPPED | none | include/viewmodel and include/view are deleted with Qt. |
| tst_coreqtfree::everyModelSourceIsClassifiedAsCoreOrUiSession | DROPPED | none | The UI-session sources (the Qt JSON schemas) go with Qt. |
| tst_coreqtfree::theGuardSeesQtThroughSharedHeaders | DROPPED | none | Self-test of the deleted include walker. |
| tst_coreqtfree::outsideTheViewQtIsConfinedToTheListedFiles | DROPPED | none | The list empties; the core-without-qt build holds "no Qt" for compiled code. |
| tst_coreqtfree::everyOtherHeaderIsQtFreeThroughAndThrough | DROPPED | none | The no-Qt CI build covers any header a compiled source includes; uncompiled headers go unchecked. |
| tst_coreqtfree::theConfinementGuardSeesAnUnlistedAndAStaleFile | DROPPED | none | Self-test of the deleted guard. |
| tst_coreqtfree::theNativeProjectCompilesExactlyTheCoreSources | PORTED | TmDq.Core.Tests NativeProjectTests.TheDllCompilesEveryCoreAndAbiSource (5.1), ::EveryFileAProjectCompilesExistsAndIsListedOnce (5.1) | Against the files on disk, not the .pro, so it outlives Qt; the two Qt model sources are named and must be dropped from the list when 6.1 deletes them |
| tst_coreqtfree::theNativeProjectGuardReadsProjectFilesCorrectly | DROPPED | none | Self-test of the deleted guard's parser. |
| tst_coreqtfree::theNativeProjectGuardSeesEveryKindOfDrift | DROPPED | none | Self-test of the deleted guard. |
| tst_coreqtfree::theAbiSourcesIncludeNoQtAndReachNoHigherLayer | PORTED | CI core-without-qt job; build_core_dll.ps1 allow-list | The higher layers no longer exist. |
| tst_coreqtfree::theAbiGuardSeesQtAndHigherLayers | DROPPED | none | Self-test of the deleted guard. |
| tst_coreqtfree::theNativeTestsAreQtFreeAndCompileOnlyCoreSources | PORTED | TmDq.Core.Tests NativeProjectTests.TheNativeTestsCompileEveryTestFileAndTheDllsOwnSources (5.1); test_native.ps1 dumpbin check | Every test file compiled, and the DLL's sources and nothing else; no Qt import is the script's check |
| tst_coreqtfree::theNativeTestsGuardSeesEveryKindOfDrift | DROPPED | none | Self-test of the deleted guard. |
| tst_constants::plotConstants | PORTED | PlotTests.cs::TheAxesAndTheTitleAreWordedAsV214WordedThem (5.1); ShellTests.ThePalettesAreV214s; PlotTests (0-100) | The texts themselves now |
| tst_streamjob::jobsKeepTheirRecordedBehaviour | PORTED | native test_streamjob.cpp; RunTests.EverySettingOfAStreamReachesTheJob, TheByteOrderOfTheFileReachesTheJob, TheApplicationRootsOwnWordMap…, AWordMapFileNamedOnTheStream…, CalibrationProfilesAreCarried…, AStreamTheCoreRejectsIsAJobError…; StreamRulesTests.TheCoreGetsTheRow… |  |
