using Avalonia.Media;
using Avalonia.Styling;

namespace TmDq.App.Views
{
    /// <summary>
    /// The plot's colours in each theme (US9.0): the chart's background, axes and text, grid, title, and the
    /// translucent legend over it (the chips' brushes are App.axaml's). A series keeps its own colour in both.
    /// They are VS Code's Dark Modern and Light Modern: the chart is the editor's colour (#1F1F1F and white, a shade lighter than the log beside it in the
    /// dark theme), its axes and text the editor's (#CCCCCC and #3B3B3B), its title the links' (#4DAAFC and #005FB8).
    /// </summary>
    public sealed record ChartPalette(Color Background, Color Foreground, Color Grid, Color Title,
                                      Color LegendBackground, Color LegendBorder, Color LegendText)
    {
        public static readonly ChartPalette Light = new(
            Colors.White, Color.FromRgb(59, 59, 59), Color.FromRgb(200, 200, 200), Color.FromRgb(0x00, 0x5F, 0xB8),
            Color.FromArgb(PlotLegend.BackgroundAlpha, 255, 255, 255), Color.FromRgb(206, 206, 206), Color.FromRgb(59, 59, 59));

        public static readonly ChartPalette Dark = new(
            Color.FromRgb(31, 31, 31), Color.FromRgb(204, 204, 204), Color.FromRgb(60, 60, 60), Color.FromRgb(0x4D, 0xAA, 0xFC),
            Color.FromArgb(PlotLegend.BackgroundAlpha, 31, 31, 31), Color.FromRgb(60, 60, 60), Color.FromRgb(204, 204, 204));

        /// <summary>The palette of <paramref name="theme"/>: the dark one for the dark theme, else the light one.</summary>
        public static ChartPalette For(ThemeVariant? theme) => theme == ThemeVariant.Dark ? Dark : Light;
    }
}
