using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using TmDq.App.Processing;

namespace TmDq.App.Views
{
    /// <summary>
    /// The walking skeleton's chart: one series' lock percentage against time, 0 to 100 % top to bottom, as a polyline
    /// in a framed plot area. Deliberately bare (no axes labels, zoom or legend); the real chart is plan step 4.3 (D1).
    /// Draws nothing at all without a series.
    /// </summary>
    public sealed class LockChart : Control
    {
        public static readonly StyledProperty<LockSeries?> SeriesProperty =
            AvaloniaProperty.Register<LockChart, LockSeries?>(nameof(Series));

        /// <summary>Inset of the plot area from the control's edges.</summary>
        public const double PlotInset = 24;

        static LockChart() => AffectsRender<LockChart>(SeriesProperty);

        public LockSeries? Series
        {
            get => GetValue(SeriesProperty);
            set => SetValue(SeriesProperty, value);
        }

        public override void Render(DrawingContext context)
        {
            LockSeries? series = Series;
            Rect plot = new Rect(Bounds.Size).Deflate(PlotInset);
            if (series is null || series.TimesSec.Length < 2 || plot.Width <= 0 || plot.Height <= 0) return;

            context.DrawRectangle(new Pen(Brushes.Gray, 1), plot);
            double t0 = series.TimesSec[0], t1 = series.TimesSec[^1];
            double span = t1 > t0 ? t1 - t0 : 1;
            var geometry = new StreamGeometry();
            using (StreamGeometryContext g = geometry.Open())
            {
                for (int i = 0; i < series.TimesSec.Length; i++)
                {
                    double x = plot.Left + (series.TimesSec[i] - t0) / span * plot.Width;
                    double y = plot.Bottom - Math.Clamp(series.LockPercent[i], 0, 100) / 100 * plot.Height;
                    if (i == 0) g.BeginFigure(new Point(x, y), false);
                    else g.LineTo(new Point(x, y));
                }
                g.EndFigure(false);
            }
            context.DrawGeometry(null, new Pen(new SolidColorBrush(Color.FromRgb(0x7B, 0x3F, 0xE4)), 1.5), geometry);
        }
    }
}
