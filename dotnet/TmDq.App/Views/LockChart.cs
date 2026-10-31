using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using TmDq.App.Processing;

namespace TmDq.App.Views
{
    /// <summary>
    /// The walking skeleton's chart: lock percentage against time, 0 to 100 % top to bottom, a polyline per stream in a
    /// framed plot area, on one time axis. Deliberately bare (no axis labels, zoom or legend); the real chart is plan
    /// step 4.3 (D1). Draws <see cref="AllSeries"/> when it holds any, else <see cref="Series"/>, and nothing at all
    /// without either.
    /// </summary>
    public sealed class LockChart : Control
    {
        public static readonly StyledProperty<LockSeries?> SeriesProperty =
            AvaloniaProperty.Register<LockChart, LockSeries?>(nameof(Series));

        public static readonly StyledProperty<IReadOnlyList<LockSeries>?> AllSeriesProperty =
            AvaloniaProperty.Register<LockChart, IReadOnlyList<LockSeries>?>(nameof(AllSeries));

        /// <summary>Inset of the plot area from the control's edges.</summary>
        public const double PlotInset = 24;

        /// <summary>The Frame Sync Lock primaries (US4.0): purple, blue, green, in turn.</summary>
        public static readonly IReadOnlyList<Color> Colors = new[]
        {
            Color.FromRgb(0x7B, 0x3F, 0xE4),
            Color.FromRgb(0x1F, 0x6F, 0xEB),
            Color.FromRgb(0x2E, 0x9E, 0x44),
        };

        static LockChart() => AffectsRender<LockChart>(SeriesProperty, AllSeriesProperty);

        public LockSeries? Series
        {
            get => GetValue(SeriesProperty);
            set => SetValue(SeriesProperty, value);
        }

        public IReadOnlyList<LockSeries>? AllSeries
        {
            get => GetValue(AllSeriesProperty);
            set => SetValue(AllSeriesProperty, value);
        }

        /// <summary>What is drawn: <see cref="AllSeries"/>, or <see cref="Series"/> alone, keeping only series with at
        /// least two points.</summary>
        public IReadOnlyList<LockSeries> Drawn
        {
            get
            {
                IEnumerable<LockSeries> all = AllSeries is { Count: > 0 } list ? list : Series is { } one ? new[] { one } : Array.Empty<LockSeries>();
                return all.Where(s => s.TimesSec.Length >= 2).ToList();
            }
        }

        public override void Render(DrawingContext context)
        {
            IReadOnlyList<LockSeries> drawn = Drawn;
            Rect plot = new Rect(Bounds.Size).Deflate(PlotInset);
            if (drawn.Count == 0 || plot.Width <= 0 || plot.Height <= 0) return;

            context.DrawRectangle(new Pen(Brushes.Gray, 1), plot);
            double t0 = drawn.Min(s => s.TimesSec[0]), t1 = drawn.Max(s => s.TimesSec[^1]);
            double span = t1 > t0 ? t1 - t0 : 1;
            for (int n = 0; n < drawn.Count; n++)
            {
                LockSeries series = drawn[n];
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
                context.DrawGeometry(null, new Pen(new SolidColorBrush(Colors[n % Colors.Count]), 1.5), geometry);
            }
        }
    }
}
