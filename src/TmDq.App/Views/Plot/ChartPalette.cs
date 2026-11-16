using System;
using Avalonia.Media;
using Avalonia.Styling;

namespace TmDq.App.Views.Plot;

/// <summary>
/// The plot's colours in each theme (US9.0): the chart's background, axes and text, grid, title, and the
/// translucent legend over it (the chips' brushes are App.axaml's). A series keeps its own colour in both.
/// They are VS Code's Dark Modern and Light Modern: the chart is the editor's colour (#1F1F1F and white, a shade lighter than the log beside it in the
/// dark theme), its axes and text the editor's (#CCCCCC and #3B3B3B), its title the links' (#4DAAFC and #005FB8).
/// <para>A series keeps its own colour in both themes, except that the light theme draws one too pale to read on its white
/// darker (<see cref="SeriesColor"/>): the yellow of the Receiver SNR primaries, and the paler shades beyond the primaries.</para>
/// </summary>
public sealed record ChartPalette(Color Background, Color Foreground, Color Grid, Color Title,
                                  Color LegendBackground, Color LegendBorder, Color LegendText)
{
    /// <summary>The lowest contrast ratio (WCAG's, 1 to 21) against <see cref="Background"/> a series may be drawn at; 0, no
    /// least. 2.0 leaves every Frame Sync Lock and Receiver SNR primary as it is except the yellow (1.7 on white; the orange,
    /// the lowest of the rest, is 2.3).</summary>
    public double MinSeriesContrast { get; init; }

    /// <summary>The colour <paramref name="series"/> is drawn in: itself, or, when its contrast against the background is
    /// below <see cref="MinSeriesContrast"/>, the same colour, its channels scaled down by the least that reaches it (the same
    /// hue and saturation, darker). The stored colour, the Customize View swatch and the template are not touched.</summary>
    public Color SeriesColor(Color series)
    {
        if (Contrast(series, Background) >= MinSeriesContrast) return series;
        for (int percent = 99; percent > 0; percent--)
        {
            var darker = Color.FromRgb(Scale(series.R, percent), Scale(series.G, percent), Scale(series.B, percent));
            if (Contrast(darker, Background) >= MinSeriesContrast) return darker;
        }
        return Colors.Black;
    }

    private static byte Scale(byte channel, int percent) => (byte)Math.Round(channel * percent / 100.0, MidpointRounding.AwayFromZero);

    /// <summary>WCAG's contrast ratio of two opaque colours: (lighter + 0.05) / (darker + 0.05), in relative luminance.</summary>
    public static double Contrast(Color a, Color b)
    {
        double la = Luminance(a), lb = Luminance(b);
        return (Math.Max(la, lb) + 0.05) / (Math.Min(la, lb) + 0.05);
    }

    private static double Luminance(Color c)
    {
        static double Linear(byte v) { double s = v / 255.0; return s <= 0.04045 ? s / 12.92 : Math.Pow((s + 0.055) / 1.055, 2.4); }
        return 0.2126 * Linear(c.R) + 0.7152 * Linear(c.G) + 0.0722 * Linear(c.B);
    }

    public static readonly ChartPalette Light = new(
        Colors.White, Color.FromRgb(59, 59, 59), Color.FromRgb(200, 200, 200), Color.FromRgb(0x00, 0x5F, 0xB8),
        Color.FromArgb(PlotLegend.BackgroundAlpha, 255, 255, 255), Color.FromRgb(206, 206, 206), Color.FromRgb(59, 59, 59))
    {
        MinSeriesContrast = 2.0,
    };

    public static readonly ChartPalette Dark = new(
        Color.FromRgb(31, 31, 31), Color.FromRgb(204, 204, 204), Color.FromRgb(60, 60, 60), Color.FromRgb(0x4D, 0xAA, 0xFC),
        Color.FromArgb(PlotLegend.BackgroundAlpha, 31, 31, 31), Color.FromRgb(60, 60, 60), Color.FromRgb(204, 204, 204));

    /// <summary>The palette of <paramref name="theme"/>: the dark one for the dark theme, else the light one.</summary>
    public static ChartPalette For(ThemeVariant? theme) => theme == ThemeVariant.Dark ? Dark : Light;
}
