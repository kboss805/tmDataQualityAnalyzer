/**
 * @file constants.h
 * @brief Application-wide constants for PCM frame parameters and UI defaults.
 */

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <array>
#include <cstdint>
#include <QColor>
#include <QString>

/// @brief Application version information.
struct AppVersion {
    static constexpr int kMajor = 2;   ///< Major version number.
    static constexpr int kMinor = 13;   ///< Minor version number.
    static constexpr int kPatch = 1;   ///< Patch version number.

    /// @return Version string in "major.minor.patch" format.
    static QString toString() { return QString("%1.%2.%3").arg(kMajor).arg(kMinor).arg(kPatch); }
};

/// @brief Constants for PCM frame structure and channel type identifiers.
namespace PCMConstants {
    inline constexpr int kCommonWordLen        = 16;    ///< Bits per word.
    inline constexpr int kSecondsPerMinute     = 60;    ///< Seconds in a minute.
    inline constexpr int kNumMinorFrames       = 1;     ///< Minor frames per major frame.
    inline constexpr int kMaxChannelCount      = 0x10000; ///< Maximum channel ID range.
    inline constexpr const char* kDefaultFrameSyncMask          = "FFFFFFFF"; ///< Default frame sync mask (all bits active).
    /// Default Frame Sync Lock pattern (PRN15). Paired with kDefaultFrameSyncLockBits
    /// below - a pattern and a frame length are only meaningful together, and a
    /// PRN15 pattern with a PRN11 frame length can never lock. Change both or neither.
    inline constexpr const char* kDefaultFrameSyncLockPattern   = "334AABBF";
    inline constexpr const char* kDefaultReceiverSNRPattern     = "FE6B2840"; ///< Default Receiver SNR frame sync pattern.
    // kDefaultFrameSync kept as an alias so existing call sites compile unchanged.
    inline constexpr const char* kDefaultFrameSync              = kDefaultFrameSyncLockPattern;

    /// Maximum raw 16-bit sample value for calibration math.
    inline constexpr uint16_t kMaxRawSampleValue = 0xFFFF;

    /// Default initial buffer size for CH10 packet reading (64 KB).
    inline constexpr unsigned long kDefaultBufferSize = 65536;

    /// Maximum allowed packet buffer size to guard against malformed file headers (100 MB).
    inline constexpr qsizetype kMaxPacketBufferSize = 100 * 1024 * 1024;

    /// Minimum milliseconds between progress position queries (time-based throttle).
    inline constexpr int kProgressReportIntervalMs = 100;

    /// @name Channel type identifiers from TMATS records
    /// @{
    inline constexpr const char* kChannelTypeTime = "TIMEIN"; ///< TMATS type for time channels.
    inline constexpr const char* kChannelTypePcm  = "PCMIN";  ///< TMATS type for PCM channels.
    /// @}

    /// Regex pattern for validating hexadecimal input strings (e.g., frame sync).
    inline constexpr const char* kFrameSyncHexPattern = "^[0-9A-Fa-f]+$";

    /// @name Frame length and sync bounds (US2.1, US7.0)
    /// @{
    inline constexpr int kMaxSyncPatternBits  = 64;    ///< Maximum frame sync pattern length in bits (16 hex chars).
    inline constexpr int kBitsPerHexDigit     = 4;     ///< Bits encoded by one hex character.
    /// Maximum hex characters accepted in a sync pattern/mask field, derived from
    /// the bit limit above so the input validator can't drift from it (US2.0/US7.0).
    inline constexpr int kMaxSyncPatternHexChars = kMaxSyncPatternBits / kBitsPerHexDigit;
    inline constexpr int kMinFrameLengthBits  = 64;    ///< Minimum total frame length in bits.
    inline constexpr int kMaxFrameLengthBits  = 65536; ///< Maximum total frame length in bits.
    /// Default Frame Sync Lock frame length in bits (PRN15 = 2^15-1). Paired with
    /// kDefaultFrameSyncLockPattern; see the note there.
    inline constexpr int kDefaultFrameSyncLockBits   = 32767;
    inline constexpr int kDefaultReceiverSNRBits     = 800;  ///< Default Receiver SNR frame length in bits.
    inline constexpr int kDefaultBitsPerFrame        = kDefaultFrameSyncLockBits; ///< Alias; matches default mode (Frame Sync Lock).
    /// @}

    /// @name Receiver SNR calibration defaults
    /// @{
    inline constexpr double kDefaultScaleDdBPerV     = 20.0; ///< Default calibration scale (dB/V).
    inline constexpr int    kDefaultNumReceivers     = 16;   ///< Default number of receivers.
    inline constexpr int    kDefaultReceiverChannels = 3;    ///< Default receiver channels per receiver.
    /// @}
}

/// @brief Constants for UI configuration, validation limits, and output formatting.
namespace UIConstants {
    /// @name QSettings keys and theme identifiers
    /// @{
    inline constexpr const char* kOrganizationName  = "tmDataQualityAnalyzer"; ///< QSettings organization name.
    inline constexpr const char* kApplicationName   = "tmDataQualityAnalyzer"; ///< QSettings application name.
    inline constexpr const char* kSettingsKeyTheme  = "Theme";        ///< QSettings key for theme preference.
    inline constexpr const char* kSettingsKeyLastCh10Dir = "LastCh10Directory"; ///< QSettings key for last Ch10 file dialog directory.
    inline constexpr const char* kSettingsKeyLastTomlDir  = "LastTomlDirectory";  ///< QSettings key for last TOML file dialog directory.
    /// @name Status glyph colours
    /// Shared by every dialog that shows a pass/fail mark, so they cannot drift
    /// apart: the Configure Streams Ready column and the Batch Process Files list
    /// both read from here. Colour only — each dialog sizes its own glyph.
    /// @{
    inline constexpr const char* kStatusOkColor     = "green";        ///< Ready / matched.
    inline constexpr const char* kStatusFailColor   = "red";          ///< Not ready / rejected.
    inline constexpr const char* kStatusIdleColor   = "gray";         ///< Not applicable yet.
    /// @}

    inline constexpr const char* kThemeDark         = "dark";         ///< Dark theme identifier.
    inline constexpr const char* kThemeLight        = "light";        ///< Light theme identifier.
    inline constexpr const char* kSettingsKeyRecentFiles = "RecentFiles"; ///< QSettings key for recent files list.
    inline constexpr const char* kSettingsKeySidebarVisible = "SidebarVisible"; ///< QSettings key for sidebar (log) visibility.
    inline constexpr const char* kSettingsKeyLegendVisible  = "LegendVisible";  ///< QSettings key for plot legend visibility.
    inline constexpr int kMaxRecentFiles            = 5;              ///< Maximum number of recent files to remember.
    /// @}

    /// @name Layout
    /// @{
    inline constexpr int kFlatButtonMinWidth    = 90;  ///< Minimum width for flat action buttons (accommodates "Collapse All").
    inline constexpr int kLogPreviewHeight      = 80;  ///< Fixed height for the log preview panel.
    /// @}

    /// @name Time conversion
    /// @{
    inline constexpr int kSecondsPerDay    = 86400; ///< Seconds in a day.
    inline constexpr int kSecondsPerHour   = 3600;  ///< Seconds in an hour.
    inline constexpr int kSecondsPerMinute = 60;    ///< Seconds in a minute.
    /// @}
    inline constexpr int kDefaultSlopeIndex           = 3;     ///< Default voltage slope index (0-5V range).
    inline constexpr int kMaxSlopeIndex               = 3;     ///< Maximum valid voltage slope index.
    inline constexpr int kMaxSamplePeriodIndex          = 2;     ///< Maximum valid sample period combo index.
    inline constexpr std::array<const char*, 3> kChannelPrefixes = {"L", "R", "C"}; ///< Channel prefix labels (L/R/C).
    inline constexpr int kNumKnownPrefixes            = 3;     ///< Number of known channel prefixes.


    /// @name Sample period options (seconds)
    /// @{
    inline constexpr double kSamplePeriod1s    = 1.0;  ///< 1 second sample period.
    inline constexpr double kSamplePeriod100ms = 0.1;  ///< 100 ms sample period.
    inline constexpr double kSamplePeriod10ms  = 0.01; ///< 10 ms sample period.
    inline constexpr int kDefaultSamplePeriodIndex = 0; ///< Default sample period combo index (1 s).
    /// @}

    /// @name Voltage scale bounds (indexed by scale combo box)
    /// @{
    inline constexpr std::array<double, 4> kSlopeVoltageLower = {-10.0, -5.0, 0.0, 0.0}; ///< Lower voltage bound per scale option.
    inline constexpr std::array<double, 4> kSlopeVoltageUpper = {10.0, 5.0, 10.0, 5.0};  ///< Upper voltage bound per scale option.
    /// @}


    /// @name Custom title bar (frameless window)
    /// Moved here from mainview.cpp when the title bar and the hamburger menu
    /// were split into their own translation unit: both files need them, and a
    /// file-local copy in each is how two sources of truth start.
    /// @{
    inline constexpr int kTitleBarHeight = 40;  ///< Custom title-bar height (logical px).
    /// @name Title-bar button metrics
    /// EVERY title-bar button uses these: the hamburger, the sidebar toggle, and the
    /// minimize/maximize/close captions. Each carries a small glyph, so the hover
    /// highlight is sized to hug it rather than filling a full-height cell. At the
    /// original metrics the highlight was several times the glyph's visual area and
    /// read as a large block floating in the bar; a compact, rounded box matches how
    /// editors (VS Code, Antigravity) highlight their own title-bar controls.
    ///
    /// Kept >= 24 px in both axes: that is the usual minimum comfortable pointer
    /// target, so tightening the box does not make the buttons fiddly to hit.
    ///
    /// Note this is a deliberate departure from the Windows caption convention, where
    /// close spans the top-right corner (making it a Fitts's-law "infinite" target at
    /// a maximized window's edge). Consistency across the whole bar was preferred; the
    /// corner is now draggable caption instead.
    /// @{
    inline constexpr int kToolGlyphButtonW = 30;
    inline constexpr int kToolGlyphButtonH = 26;
    inline constexpr int kToolGlyphIconPx  = 24;
    /// Gap between the last caption button and the window edge. The compact
    /// buttons no longer span the corner, so without this their rounded hover
    /// box would sit flush against the frame.
    inline constexpr int kTitleBarEdgeGap  = 6;
    /// @}

    /// @name Caption-button glyphs from the Windows icon font
    /// Segoe Fluent Icons (Windows 11), falling back to Segoe MDL2 Assets — the
    /// same glyphs Windows draws for its own window buttons, so minimize,
    /// maximize/restore and close share one set of metrics. Ordinary Unicode
    /// look-alikes (U+2212, U+25A1, U+2715) do NOT: the UI font draws them at
    /// noticeably different optical sizes, which is what made these buttons look
    /// mismatched.
    ///
    /// Written as codepoints rather than literal characters on purpose: they live
    /// in the Unicode private use area, so a literal in the source is fragile —
    /// editors and tooling can silently drop it, leaving a blank button.
    /// @{
    inline constexpr char16_t kGlyphMinimize = 0xE921; ///< ChromeMinimize
    inline constexpr char16_t kGlyphMaximize = 0xE922; ///< ChromeMaximize
    inline constexpr char16_t kGlyphRestore  = 0xE923; ///< ChromeRestore
    inline constexpr char16_t kGlyphClose    = 0xE8BB; ///< ChromeClose
    /// @}
    /// @}

    inline constexpr int kDefaultPolarityIndex = 0;  ///< Default polarity value (0 = Positive).

    /// @name Progress / layout
    /// @{
    inline constexpr int kProgressBarMax                 = 100;  ///< Maximum value for the progress bar.
    inline constexpr int kFileListMinHeight              = 180;  ///< Minimum height for the file list tree widget (px).
    inline constexpr int kLayoutSpacingSmall             = 8;                            ///< Small layout spacing (px).
    inline constexpr int kAboutIconSize                  = 64;                           ///< About dialog icon size (px).
    inline constexpr int kSidebarMinWidth                 = 400;                          ///< Minimum width for the sidebar panel (file name column + margins).
    inline constexpr int kInitialWindowWidth              = 1920;                         ///< Initial main window width at launch (px).
    inline constexpr int kInitialWindowHeight             = 1080;                         ///< Initial main window height at launch (px).
    inline constexpr int kHexBase                        = 16;                           ///< Hexadecimal (base-16) radix for string parsing.
    inline constexpr int kBytesPerKB                     = 1024;                         ///< Bytes per kilobyte.
    inline constexpr int kBytesPerMB                     = 1048576;                      ///< Bytes per megabyte.
    /// @}

    /// @name Deployment / portable mode
    /// @{
    inline constexpr const char* kPortableMarkerFilename = "portable";      ///< Marker file name for portable mode detection.
    inline constexpr const char* kSettingsDirName         = "settings";     ///< Settings directory name relative to app root.
    /// Optional full user manual, installed beside the app root.
    ///
    /// The BASE manual is compiled in as a Qt resource, so it can never be missing.
    /// This one is an installer option and may legitimately be absent — its absence
    /// is a normal state, not an error, and Help falls back to the built-in manual.
    inline constexpr const char* kFullManualFilename      = "UserManual.html";
    inline constexpr const char* kDefaultTomlFilename     = "default.toml"; ///< Default TOML configuration filename, used in each settings subdirectory below.
    inline constexpr const char* kReceiverParamsDirName   = "receiver_params";   ///< Receiver Parameters subdirectory name (relative to settings dir).
    inline constexpr const char* kRcvrCalsDirName         = "rcvr_cals";         ///< Receiver/SNR step calibration subdirectory name (relative to settings dir).
    inline constexpr const char* kFramesyncPatternsDirName = "framesync_patterns"; ///< Frame sync pattern subdirectory name (relative to settings dir).
    /// Frame sync pattern file the app loads as the default for Receiver SNR
    /// streams (under kFramesyncPatternsDirName), so the receiver frame sync,
    /// mask, and bits-per-frame are user-editable rather than hard-coded.
    inline constexpr const char* kDefaultReceiverFrameSyncFilename = "framesync_rcvr_default.toml";
    /// @}
}

/// @brief Constants for non-linear step calibration extraction (US5.3).
namespace CalibrationConstants {
    /// Output sample period (seconds) assumed for a calibration extraction until
    /// the reader resolves the recording's real bit rate, at which point the
    /// period is sized from the frame rate (see kFramesPerExtractWindow).
    inline constexpr double kExtractSamplePeriodSec = 0.01; // 10 ms (100 Hz)

    /// Decoded frames averaged into each extraction output sample.
    ///
    /// Sampling once per frame sounds strictly better - it is the finest a
    /// recording can give - but it is not: the AGC word updates more slowly than
    /// the frame rate, so consecutive frames repeat the same count and the series
    /// becomes a quantised staircase whose median absolute derivative is exactly
    /// zero. The edge threshold then collapses to kMinEdgeRawCounts and every
    /// real jitter step reads as an edge, shattering every dwell.
    inline constexpr int kFramesPerExtractWindow = 10;

    /// Lower bound (seconds) on the adaptively-sized extraction sample period.
    ///
    /// Finer is NOT better here, which is worth stating because it is the
    /// intuitive assumption. A window smaller than this resolves the settling
    /// ramp at the start of each dwell (~1 s on a real receiver) into sub-plateaus
    /// the detector reads as extra levels. Measured on the step_cal recording, whose
    /// 1 ms frames make every option available: one sample per frame calibrates
    /// 0 of 48 channels, 10 ms calibrates 10, and 100 ms calibrates 12.
    ///
    /// The window is also the resolution of every dwell BOUNDARY - one window
    /// straddles each transition and reads as a level belonging to neither step -
    /// but that costs nothing: those samples are marked unstable and excluded, and
    /// each level is taken from the END of its dwell. The boundary smear an
    /// operator sees BETWEEN plateaus on the plot comes from the main run's own
    /// sample period, not from this.
    inline constexpr double kMinAdaptiveExtractPeriodSec = 0.1; // 100 ms

    /// Upper bound (seconds) on the adaptively-sized extraction sample period,
    /// so that a very low frame rate can not coarsen the window past the point of
    /// leaving enough samples per dwell plateau to confirm a step.
    inline constexpr double kMaxExtractSamplePeriodSec = 0.2; // 200 ms

    /// Edge-detection threshold as a multiple of the robust (MAD-based) standard
    /// deviation of the in-plateau sample-to-sample derivative. A derivative
    /// larger than this is treated as a step transition.
    inline constexpr double kEdgeSigmaMultiple = 6.0;

    /// Absolute floor on the edge threshold in raw counts, so flat/noise-free
    /// data does not produce a degenerate (near-zero) threshold.
    inline constexpr double kMinEdgeRawCounts = 2.0;

    /// The most permissive edge threshold the detector will relax to, as a
    /// multiple of the same robust sigma kEdgeSigmaMultiple scales.
    ///
    /// A receiver far out of tolerance is compressed at one end of its range, so
    /// its small steps move fewer raw counts than kEdgeSigmaMultiple * sigma and
    /// merge into their neighbours — the channel then detects too few plateaus and
    /// loses its profile. StepDetector therefore retries with progressively lower
    /// thresholds and keeps the first that yields a clean sweep. Below roughly
    /// this multiple, ordinary sample-to-sample noise clears the threshold on its
    /// own and plateaus disintegrate instead of merging, so relaxing further
    /// cannot help.
    inline constexpr double kMinEdgeSigmaMultiple = 1.5;

    /// Ratio between successive threshold attempts in that search (each attempt is
    /// this fraction of the previous). Small enough to land near the largest
    /// workable threshold, large enough to reach the floor in a few tries.
    inline constexpr double kEdgeRelaxFactor = 0.7;

    /// Cap on threshold attempts, so a pathological channel cannot spin: with the
    /// factor above this spans roughly a 12x reduction, well past the floor for
    /// any realistic sigma.
    inline constexpr int kMaxEdgeRelaxAttempts = 8;

    /// Adjacent plateau levels closer than this fraction of the channel's median
    /// step are one physical level split by noise, and are merged before sweep
    /// selection. Complements the noise-based edge threshold, which cannot size
    /// itself for noise that grows with signal level. On the step_cal recording the
    /// splits run 0.5-2% of a step and the smallest genuine step ~30%, so 10%
    /// sits well clear of both; a synthetic compressed receiver's smallest real
    /// step is ~16% (TestStepDetector pins that it survives).
    inline constexpr double kSameLevelFractionOfStep = 0.1;

    /// A plateau within this fraction of either end of the converter's range is
    /// PINNED there rather than measured: a receiver driven onto its rail (or
    /// under its floor) reports the same count whatever the signal does, so such
    /// a dwell carries no calibration information and must not be paired with a
    /// step. 0.98 of full scale is 64224 counts; the real railed dwells on
    /// the step_cal recording sit at 65472.
    inline constexpr double kSaturatedRawFraction = 0.98;

    /// How far (in dwells) a plateau's end may sit off the regular dwell grid
    /// before the saturated-top fallback refuses to infer step indices from
    /// timing. A recording whose steps were not each held for the same length of
    /// time fails this and keeps the linear fallback, rather than being given a
    /// silently mis-numbered profile.
    inline constexpr double kMaxDwellSlotError = 0.25;

    /// Fewest measured steps a partial (saturated-top) profile may be built from;
    /// it must also cover at least half the expected steps. Below that the
    /// profile would describe so little of the sweep that linear is the more
    /// honest answer.
    inline constexpr int kMinPartialProfileSteps = 3;

    /// A derivative at least this fraction of the channel's median step is a dwell
    /// TRANSITION rather than in-dwell noise. The saturated-top fallback counts
    /// these to number the steps, so it wants a threshold well above the noise
    /// (tens of counts) and well below the smallest real step - including the
    /// compressed ones an out-of-tolerance receiver produces, which on
    /// the step_cal recording run to a third of the median step.
    inline constexpr double kMajorEdgeFractionOfStep = 0.25;

    /// Extra levels a sweep may carry ahead of the step file's before the step file
    /// itself is suspected. One is the ordinary signal-generator turn-on transient,
    /// which the detector drops as lead-in by design. More than that usually means
    /// the step file lists fewer steps than were injected - and then EVERY step is
    /// paired with the wrong level while the channel still reports calibrated.
    /// the step_cal recording's eleven-level sweep extracted with the 8-step RASA file did
    /// exactly that on all twelve channels.
    inline constexpr int kMaxLeadInLevels = 1;

    /// Minimum stable-run duration (seconds) required to confirm a level as a
    /// genuine step rather than a transient/partial-jump blip. The detector
    /// requires a run to be stable for at least this long (in addition to the
    /// kMinConfirmSamples floor below) before it is even considered a candidate
    /// step; the calibration average is then taken from exactly this many
    /// samples at the start of the confirmed run.
    inline constexpr double kStepConfirmSeconds = 1.0;

    /// Absolute floor on the confirmation window, in samples, regardless of
    /// sample period: a run must hold for at least this many consecutive
    /// samples so a single noisy sample can't be mistaken for a settled step.
    /// Matters when samplePeriodSec is coarse enough that kStepConfirmSeconds
    /// alone would be only one or two samples.
    inline constexpr int kMinConfirmSamples = 3;
}

/// @brief Constants for the AGC signal plot window.
namespace PlotConstants {
    inline constexpr int kPlotDockMinWidth    = 1024;  ///< Minimum plot dock width in pixels.
    inline constexpr int kPlotMinChartHeight = 250;   ///< Minimum height for the chart area within the plot widget.
    inline constexpr double kAxisMarginFactor = 0.05; ///< Y-axis padding as fraction of data range.
    inline constexpr double kMinAxisSpan      = 1.0;  ///< Minimum span enforced so a user max override can't invert/collapse an axis.
    /// Lock-% axis bounds. Lock percentage is a fixed 0-100 quantity, not a
    /// data-driven range, and three places need to agree on it: the chart's
    /// initial left range, the ViewModel's lock-axis accessors, and the upper
    /// limit the Set Left Max dialog will accept.
    /// Samples per pixel column above which a series is drawn from its envelope
    /// rather than sample by sample. One column can only show one vertical run of
    /// pixels, so beyond a couple of samples per column the extra points cost time
    /// and change nothing on screen. Measured on a 1200 px plot: 12 channels of one
    /// hour at 10 ms (360k samples each) took 102 s per repaint drawing every
    /// point.
    inline constexpr int kMaxSamplesPerPixelColumn = 2;

    /// Alpha for the min/max band drawn behind a decimated series. Translucent so
    /// the mean line drawn over it stays legible, but solid enough that a one-sample
    /// excursion - the event this plot exists to show - is unmistakable against the
    /// background on both themes.
    inline constexpr int kEnvelopeBandAlpha = 90;

    /// Alpha for the band's outline - the min/max envelope itself. Stronger than the
    /// fill because those edges ARE the extremes: a one-sample dropout is a single
    /// pixel column reaching the axis, and with the fill alone it rendered as a faint
    /// smudge where it used to be a solid line. The outline restores it.
    inline constexpr int kEnvelopeEdgeAlpha = 190;

    /// How many zoom levels the X axis remembers. Deep enough that an operator
    /// drilling into an event never runs out on the way back, bounded so a long
    /// session cannot accumulate history without limit.
    inline constexpr int kMaxZoomDepth = 32;

    inline constexpr double kLockAxisMin      = 0.0;
    inline constexpr double kLockAxisMax      = 100.0;
    inline constexpr const char* kXAxisLabel        = "Time (DDD:HH:MM:SS)"; ///< X axis label.
    inline constexpr const char* kYAxisLabel        = "Framesync Lock (%)"; ///< Left Y axis label (lock-% mode).
    inline constexpr const char* kMissedFramesAxisLabel = "Accumulated Missed Frames"; ///< Left Y axis label (missed frames mode).
    inline constexpr const char* kSnrAxisLabel      = "Receiver SNR (dB)"; ///< Right Y axis label.
    inline constexpr const char* kDefaultPlotTitle  = "Framesync/SNR Plot"; ///< Default chart title.

    /// CSV export/import header tokens. The exporter (PlotViewModel::exportCsv) and
    /// the importer (CsvSeriesParser) MUST agree on these so an exported file can be
    /// re-loaded. The first column is the combined day+time stamp; frame-sync series
    /// names are qualified per metric (the bare name is shared by a stream's lock and
    /// missed-frames series and would otherwise export as two identical headers).
    inline constexpr const char* kCsvTimeHeader          = "Time (DOY:HH:MM:SS.mmm)"; ///< First CSV column header (combined DDD:HH:MM:SS.mmm stamp).
    inline constexpr const char* kCsvLockSuffix          = " Lock (%)";                ///< Suffix marking a FrameSyncLock column.
    inline constexpr const char* kCsvMissedFramesSuffix  = " Accumulated Missed Frames"; ///< Suffix marking an AccumulatedMissedFrames column.

    /// @brief Primary colors for frame sync lock series (purple, blue, green), one per stream.
    /// Additional streams reuse these primaries with a runtime-computed shade (see
    /// PlotViewModel::shadeOfColor).
    inline constexpr int kNumFrameSyncLockPrimaryColors = 3;
    inline constexpr std::array<QColor, kNumFrameSyncLockPrimaryColors> kFrameSyncLockPrimaryColors = {
        QColor(106, 13, 173),   ///< Purple
        QColor(67, 97, 238),    ///< Blue
        QColor(46, 184, 92),    ///< Green
    };

    /// @name Theme colors
    /// @{
    inline constexpr QColor kDarkBackground  {32, 32, 32};       ///< Dark theme chart background.
    inline constexpr QColor kLightBackground {255, 255, 255};    ///< Light theme chart background.
    inline constexpr QColor kDarkForeground  {220, 220, 220};    ///< Dark theme axis/text color.
    inline constexpr QColor kLightForeground {30, 30, 30};       ///< Light theme axis/text color.
    inline constexpr QColor kDarkGridColor   {60, 60, 60};       ///< Dark theme grid line color.
    inline constexpr QColor kLightGridColor  {200, 200, 200};    ///< Light theme grid line color.
    /// @}

    /// @name Plot widget parameters
    /// @{
    inline constexpr int kTickCount          = 10;               ///< Number of major tick marks on X axis.
    inline constexpr int kYTickCount         = 6;                ///< Number of major tick marks on each Y axis.
    inline constexpr double kGraphPenWidth   = 1.5;              ///< Width of series graph pen.
    inline constexpr double kYSpinBoxMax     = 999.0;            ///< Maximum range for Y axis spinboxes.
    /// @}

    /// @name Legend overlay (movable, draggable box floating inside the plot)
    /// @{
    inline constexpr int    kLegendMarginPx      = 8;    ///< Gap kept between the legend and the plot edges.
    inline constexpr int    kLegendContentMargin = 6;    ///< Inner padding inside the legend frame (px).
    inline constexpr int    kLegendRowSpacing    = 3;    ///< Vertical gap between legend rows (px).
    inline constexpr int    kLegendSwatchLen     = 22;   ///< Length of the matplotlib-style line swatch (px).
    inline constexpr int    kLegendSwatchThick   = 3;    ///< Thickness of the line swatch (px).
    inline constexpr int    kLegendCornerRadius  = 6;    ///< Rounded-corner radius of the legend frame (px).
    inline constexpr int    kLegendBgAlpha       = 185;  ///< Alpha of the translucent legend background (0-255).
    inline constexpr double kLegendMaxHeightFrac = 0.60; ///< Cap the legend height to this fraction of the chart.
    inline constexpr double kLegendMaxWidthFrac  = 0.45; ///< Cap the legend width to this fraction of the chart.
    inline constexpr int    kOverlayChipHeightPx = 26;   ///< Height of every chip in the on-chart chip bar (px).
    /// Extra headroom reserved at the top of the plot area for the on-chart chip bar.
    ///
    /// The chips are overlaid at the chart's top-left, so without this the plot area
    /// starts at the outer margin and they sit directly on the axis line and the top
    /// Y tick label. This does not fully clear the bar (that would need roughly
    /// kLegendMarginPx + kOverlayChipHeightPx); it is a tuned visual gap so the chips
    /// stop touching the axis while keeping the wasted headroom small.
    inline constexpr int    kOverlayHeadroomPx   = 12;
    inline constexpr int    kOverlayChipSpacingPx = 4;   ///< Gap between chips in the on-chart overlay bar (px).
    inline constexpr int    kCrosshairAlpha      = 140;  ///< Alpha of the cursor-following crosshair line (0-255).
    inline constexpr int    kZoomBandAlpha       = 45;   ///< Alpha of the drag-to-zoom rubber band fill (0-255).
    inline constexpr double kMinBandZoomSpanSec  = 1e-6; ///< Ignore band-zoom drags narrower than this (a click, not a drag).
    inline constexpr double kKeyPanFraction      = 0.10; ///< Left/Right arrow slides the X window by this fraction of the visible span.
    inline constexpr double kKeyZoomFactor       = 0.80; ///< '+' scales the X window by this ('-' by its reciprocal).
    /// @}

    /// @brief Primary colors for SNR receiver series (red, orange, yellow), one per receiver.
    /// Additional receivers/channels reuse these primaries with a runtime-computed shade
    /// (see PlotViewModel::shadeOfColor).
    inline constexpr int kNumSnrPrimaryColors = 3;
    inline constexpr std::array<QColor, kNumSnrPrimaryColors> kSnrPrimaryColors = {
        QColor(204, 0, 0),      ///< Red
        QColor(255, 140, 0),    ///< Orange
        QColor(230, 200, 30),   ///< Yellow
    };
}

#endif // CONSTANTS_H
