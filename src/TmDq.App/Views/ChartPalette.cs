using Avalonia.Media;
using Avalonia.Styling;

namespace TmDq.App.Views
{
    /// <summary>
    /// The plot's colours in each theme (US9.0), v2.x's for the chart and its legend: the chart's
    /// background, axes and text, grid, title, and the translucent legend and chips over it. A series keeps its own
    /// colour in both. The dark chart's background is a shade darker than the log panel's (#1C1C1C, v2.x's darkest
    /// grey, against #202020), and the light chart's a shade lighter (white against #FBFBFB), to set the chart off.
    /// </summary>
    public sealed record ChartPalette(Color Background, Color Foreground, Color Grid, Color Title,
                                      Color LegendBackground, Color LegendBorder, Color LegendText)
    {
        public static readonly ChartPalette Light = new(
            Colors.White, Color.FromRgb(30, 30, 30), Color.FromRgb(200, 200, 200), Color.FromRgb(0x00, 0x5F, 0xB8),
            Color.FromArgb(PlotLegend.BackgroundAlpha, 255, 255, 255), Color.FromRgb(170, 170, 170), Color.FromRgb(30, 30, 30));

        public static readonly ChartPalette Dark = new(
            Color.FromRgb(28, 28, 28), Color.FromRgb(220, 220, 220), Color.FromRgb(60, 60, 60), Color.FromRgb(0x60, 0xCD, 0xFF),
            Color.FromArgb(PlotLegend.BackgroundAlpha, 28, 28, 28), Color.FromRgb(90, 90, 90), Color.FromRgb(230, 230, 230));

        /// <summary>The palette of <paramref name="theme"/>: the dark one for the dark theme, else the light one.</summary>
        public static ChartPalette For(ThemeVariant? theme) => theme == ThemeVariant.Dark ? Dark : Light;
    }
}
