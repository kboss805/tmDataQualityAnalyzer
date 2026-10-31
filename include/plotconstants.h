/**
 * @file plotconstants.h
 * @brief Presentation constants for the plot window - geometry, labels and colours.
 *
 * Split out of constants.h because this is the only part of it that needs **QtGui**:
 * the QColor palettes below. While they lived together, every translation unit that
 * included constants.h pulled in <QColor> - which meant the whole MODEL layer did,
 * since constants.h is where PCMConstants lives.
 *
 * That was found by trying to build the model layer as a standalone native DLL with
 * `QT -= gui` (see dotnet/chart-spike on explore/dotnet-migration): the build died on
 *
 *     constants.h(11): fatal error C1083: Cannot open include file: 'QColor'
 *
 * Nothing in the model references a colour. The coupling was purely the shared header.
 *
 * The CSV header tokens that used to sit here moved to SeriesColumnSchema, which is the
 * documented single source of truth for the CSV column format - they are a
 * serialization contract rather than plot presentation, and keeping them here is what
 * made the model layer depend on a QtGui header.
 *
 * Include this from the View and ViewModel layers. The Model must not need it.
 */

#ifndef PLOTCONSTANTS_H
#define PLOTCONSTANTS_H

#include <array>
#include <cstdint>

#include <QColor>
#include <QString>

/// @brief Constants for the AGC signal plot window.
namespace PlotConstants {
    inline constexpr int kPlotDockMinWidth    = 1024;  ///< Minimum plot dock width in pixels.
    inline constexpr int kPlotMinChartHeight = 250;   ///< Minimum height for the chart area within the plot widget.
    inline constexpr double kAxisMarginFactor = 0.05; ///< Y-axis padding as fraction of data range.
    inline constexpr double kMinAxisSpan      = 1.0;  ///< Minimum span enforced so a user max override can't invert/collapse an axis.
    // The decimation threshold, ChartConstants::kMaxSamplesPerPixelColumn, is the core's (constants.h): the chart's
    // geometry is built there (plan step 4.3a).

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

    /// Lock-% axis bounds. Lock percentage is a fixed 0-100 quantity, not a
    /// data-driven range, and three places need to agree on it: the chart's
    /// initial left range, the ViewModel's lock-axis accessors, and the upper
    /// limit the Set Left Max dialog will accept.
    inline constexpr double kLockAxisMin      = 0.0;
    inline constexpr double kLockAxisMax      = 100.0;
    inline constexpr const char* kXAxisLabel        = "Time (DDD:HH:MM:SS)"; ///< X axis label.
    inline constexpr const char* kYAxisLabel        = "Framesync Lock (%)"; ///< Left Y axis label (lock-% mode).
    inline constexpr const char* kMissedFramesAxisLabel = "Accumulated Missed Frames"; ///< Left Y axis label (missed frames mode).
    inline constexpr const char* kSnrAxisLabel      = "Receiver SNR (dB)"; ///< Right Y axis label.
    inline constexpr const char* kDefaultPlotTitle  = "Framesync/SNR Plot"; ///< Default chart title.


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

#endif // PLOTCONSTANTS_H
