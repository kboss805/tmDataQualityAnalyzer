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
    static constexpr int kMinor = 7;   ///< Minor version number.
    static constexpr int kPatch = 0;   ///< Patch version number.

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
    inline constexpr const char* kDefaultFrameSyncLockPattern   = "A345CA5C"; ///< Default Frame Sync Lock pattern (PRN11).
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
    inline constexpr int kDefaultFrameSyncLockBits   = 2047; ///< Default Frame Sync Lock frame length in bits (PRN11).
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
    /// Fine output sample period (seconds) used when extracting raw calibration
    /// data, so dwell plateaus are resolved with many samples each. This is the
    /// FLOOR; the actual period is sized up to hold ~kFramesPerExtractWindow
    /// frames per window (see below) so low-frame-rate recordings don't produce
    /// sparse/zero-laced windows.
    inline constexpr double kExtractSamplePeriodSec = 0.01; // 10 ms (100 Hz)

    /// Target number of decoded frames to average into each extraction output
    /// sample. The extractor derives its sample period from the resolved bit
    /// rate so each window holds roughly this many frames: too few (e.g. ~1 on
    /// an ~8000-bit/100 Hz stream at 10 ms) yields a noisy, gap-laced series the
    /// step detector cannot read; too many over-coarsens the plateaus.
    inline constexpr int kFramesPerExtractWindow = 10;

    /// Lower bound (seconds) on the adaptively-sized extraction sample period.
    /// Distinct from kExtractSamplePeriodSec (the raw default): on fast frames
    /// (e.g. 800-bit minor frames at ~1000 Hz) the "~kFramesPerExtractWindow
    /// frames/window" sizing collapses to a few milliseconds, which over-resolves
    /// each dwell plateau into many samples. StepDetector then sees fine
    /// settling/noise as extra sub-plateaus and mis-pairs small steps (a 3 dB
    /// first step gets dropped, drifting every later step). Flooring the period at
    /// ~kStepConfirmSeconds / kFramesPerExtractWindow keeps each confirm window to
    /// a moderate sample count, which detects cleanly across slow and fast frames.
    inline constexpr double kMinAdaptiveExtractPeriodSec = 0.1; // 100 ms

    /// Upper bound (seconds) on the adaptively-sized extraction sample period,
    /// so that a very low frame rate can't coarsen the window past the point of
    /// leaving enough samples per dwell plateau to confirm a step.
    inline constexpr double kMaxExtractSamplePeriodSec = 0.2; // 200 ms

    /// Edge-detection threshold as a multiple of the robust (MAD-based) standard
    /// deviation of the in-plateau sample-to-sample derivative. A derivative
    /// larger than this is treated as a step transition.
    inline constexpr double kEdgeSigmaMultiple = 6.0;

    /// Absolute floor on the edge threshold in raw counts, so flat/noise-free
    /// data does not produce a degenerate (near-zero) threshold.
    inline constexpr double kMinEdgeRawCounts = 2.0;

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
    inline constexpr int kPlotMinChartHeight = 250;   ///< Minimum height for the chart area (QCustomPlot) within the plot widget.
    inline constexpr double kAxisMarginFactor = 0.05; ///< Y-axis padding as fraction of data range.
    inline constexpr double kMinAxisSpan      = 1.0;  ///< Minimum span enforced so a user max override can't invert/collapse an axis.
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

    inline constexpr QColor kFrameSyncLockColor {106, 13, 173};           ///< Distinctive purple for lock series.

    /// @brief Primary colors for frame sync lock series (purple, blue, green), one per stream.
    /// Additional streams reuse these primaries with a runtime-computed shade (see
    /// PlotViewModel::shadeOfColor). kFrameSyncLockPrimaryColors[0] equals kFrameSyncLockColor
    /// for single-stream compatibility.
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
    inline constexpr double kGraphPenWidth   = 1.5;              ///< Width of series graph pen.
    inline constexpr int kTitleFontSize      = 10;               ///< Plot title font size in points.
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
    inline constexpr int    kOverlayChipSpacingPx = 4;   ///< Gap between chips in the on-chart overlay bar (px).
    inline constexpr int    kCrosshairAlpha      = 140;  ///< Alpha of the cursor-following crosshair line (0-255).
    inline constexpr int    kZoomBandAlpha       = 45;   ///< Alpha of the drag-to-zoom rubber band fill (0-255).
    inline constexpr double kMinBandZoomSpanSec  = 1e-6; ///< Ignore band-zoom drags narrower than this (a click, not a drag).
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
