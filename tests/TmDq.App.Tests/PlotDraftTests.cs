using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels.Plot;
using TmDq.App.Views;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// A large plot kept responsive: each long series' index in the core, and the draft a gesture paints on a heavy plot
    /// (an outline of each band without its fill, lines a device pixel wide) until it settles. The draft must still show
    /// a one-sample dropout, the full picture must come back when the gesture stops, a light plot is never drafted, and
    /// an export is never a draft.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class PlotDraftTests
    {
        public PlotDraftTests() => NativeCore.EnsureLoaded();

        private const int Samples = 100_000;      // 0.01 s apart: 1000 s, about 200 samples a pixel column
        private const int DropoutAt = 61_234;     // one sample at 0 where the rest are 30 to 70

        private sealed record Shown(PlotChart Chart, Window Window)
        {
            public Point At(double x, double y) => Frames.InWindow(Chart, Window).TopLeft + new Point(x, y);

            public Rect Area
            {
                get
                {
                    Rect c = Frames.InWindow(Chart, Window), a = Chart.PlotArea;
                    return new Rect(c.X + a.X, c.Y + a.Y, a.Width, a.Height);
                }
            }

            /// <summary>Renders the window (which is what makes the chart paint) and reads it.</summary>
            public ILockedFramebuffer Frame() => Frames.Capture(Window).Lock();
        }

        /// <summary>A dense noisy lock series with a single-sample dropout, and, when asked, more series like it.</summary>
        private static PlotViewModel Dense(int series = 1)
        {
            var rng = new Random(17);
            double[] xs = Enumerable.Range(0, Samples).Select(i => i * 0.01).ToArray();
            var plot = new PlotViewModel();
            plot.SetSeries(Enumerable.Range(0, series).Select(k =>
            {
                double[] ys = Enumerable.Range(0, Samples).Select(_ => 50.0 + rng.NextDouble() * 40.0 - 20.0).ToArray();
                if (k == 0) ys[DropoutAt] = 0.0;
                return PlotFixtures.Lock(k, "s" + k, k + 1, k, xs, ys);
            }).ToList());
            return plot;
        }

        private static Shown Show(PlotViewModel plot)
        {
            var chart = new PlotChart { Width = 600, Height = 400, Plot = plot };
            var window = new Window { Content = chart, SizeToContent = SizeToContent.WidthAndHeight };
            TextOptions.SetTextRenderingMode(window, TextRenderingMode.Antialias);
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using (new Shown(chart, window).Frame()) { }   // the first, full, paint
            return new Shown(chart, window);
        }

        /// <summary>Pixels of the first series' colour (blended with white, as its fill and antialiased edges are) in a
        /// rectangle given in the plot area's own pixels.</summary>
        private static int Ink(Shown s, PlotViewModel plot, Rect inArea)
        {
            Rect a = s.Area;
            using ILockedFramebuffer fb = s.Frame();
            return Frames.Near(fb, new Rect(a.X + inArea.X, a.Y + inArea.Y, inArea.Width, inArea.Height), plot.Series[0].Color);
        }

        private static int InkInArea(Shown s, PlotViewModel plot) => Ink(s, plot, new Rect(0, 0, s.Area.Width, s.Area.Height));

        /// <summary>The first series' ink just above the bottom of the plot, in the dropout's column and away from it:
        /// only the dropout reaches down there.</summary>
        private static (int AtDropout, int Elsewhere) DropoutInk(Shown s, PlotViewModel plot)
        {
            double px = s.Chart.XToPixel(DropoutAt * 0.01) - s.Chart.PlotArea.X;
            double h = s.Area.Height;
            var low = new Rect(px - 3, h * 0.8, 6, h * 0.2 - 3);
            int at = Ink(s, plot, low);
            int elsewhere = Ink(s, plot, new Rect(5, h * 0.8, px - 15, h * 0.2 - 3)) +
                            Ink(s, plot, new Rect(px + 10, h * 0.8, s.Area.Width - px - 15, h * 0.2 - 3));
            return (at, elsewhere);
        }

        [AvaloniaFact]
        public void AHeavyPlotIsDraftedWhileTheWheelMovesItAndDrawnInFullWhenItSettles()
        {
            PlotViewModel plot = Dense();
            Shown s = Show(plot);
            s.Chart.DraftThresholdMs = 0;   // every plot is heavy
            int full = InkInArea(s, plot);
            Assert.False(s.Chart.DraftShown);
            double fullPaint = s.Chart.LastFullPaintMs;
            Assert.True(fullPaint > 0);

            s.Window.MouseWheel(s.At(s.Chart.PlotArea.Center.X, s.Chart.PlotArea.Center.Y), new Vector(0, 1), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            int draft = InkInArea(s, plot);
            Assert.True(s.Chart.DraftShown);
            Assert.Equal(1, s.Chart.DraftLayerRenders);
            // What a draft costs says nothing about whether the plot is heavy: only a full paint is timed.
            Assert.Equal(fullPaint, s.Chart.LastFullPaintMs);
            // No fill: the band's inside is background, its outline and the line hairlines.
            Assert.True(draft * 3 < full, $"the draft drew {draft} pixels of the series, the full picture {full}");
            // The dropout still reaches the bottom of the plot, and nothing else does.
            (int atDropout, int elsewhere) = DropoutInk(s, plot);
            Assert.True(atDropout >= 10, $"the dropout's column has {atDropout} pixels of the series near the bottom");
            Assert.Equal(0, elsewhere);

            // A cursor move is neither a step of the gesture nor a repaint.
            int renders = s.Chart.DataLayerRenders;
            s.Window.MouseMove(s.At(100, 100), RawInputModifiers.None);
            using (s.Frame()) { }
            Assert.Equal(renders, s.Chart.DataLayerRenders);

            s.Chart.Settle();
            int settled = InkInArea(s, plot);
            Assert.False(s.Chart.DraftShown);
            Assert.Equal(1, s.Chart.DraftLayerRenders);
            Assert.True(settled * 2 > full * 1.5, $"settled the plot drew {settled} pixels of the series; before the gesture, {full}");
            (atDropout, elsewhere) = DropoutInk(s, plot);
            Assert.True(atDropout >= 10);
            Assert.Equal(0, elsewhere);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void ADragIsDraftedAndItsReleaseIsTheFullPictureAtOnce()
        {
            PlotViewModel plot = Dense();
            plot.SetXViewByUser(200, 600);
            Shown s = Show(plot);
            s.Chart.DraftThresholdMs = 0;
            s.Chart.RefineDelay = TimeSpan.FromHours(1);   // only the release can settle it here
            Point grab = s.At(s.Chart.PlotArea.Center.X, s.Chart.PlotArea.Center.Y);
            s.Window.MouseDown(grab, MouseButton.Left, RawInputModifiers.None);
            s.Window.MouseMove(grab + new Point(-40, 0), RawInputModifiers.LeftMouseButton);
            Dispatcher.UIThread.RunJobs();
            using (s.Frame()) { }
            Assert.True(plot.XViewMin > 200, "the drag moved the window");
            Assert.True(s.Chart.DraftShown);
            s.Window.MouseMove(grab + new Point(-80, 0), RawInputModifiers.LeftMouseButton);
            using (s.Frame()) { }
            Assert.True(s.Chart.DraftShown);
            Assert.Equal(2, s.Chart.DraftLayerRenders);

            s.Window.MouseUp(grab + new Point(-80, 0), MouseButton.Left, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            using (s.Frame()) { }
            Assert.False(s.Chart.DraftShown);
            Assert.Equal(2, s.Chart.DraftLayerRenders);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheWheelSettlesOnItsOwnAfterTheRefineDelay()
        {
            PlotViewModel plot = Dense();
            Shown s = Show(plot);
            s.Chart.DraftThresholdMs = 0;
            s.Chart.RefineDelay = TimeSpan.FromMilliseconds(30);
            Assert.Equal(TimeSpan.FromMilliseconds(150), PlotChart.DefaultRefineDelay);
            s.Window.MouseWheel(s.At(300, 200), new Vector(0, 1), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            using (s.Frame()) { }
            Assert.True(s.Chart.DraftShown);
            var clock = Stopwatch.StartNew();
            while (s.Chart.DraftShown && clock.Elapsed < TimeSpan.FromSeconds(5))
            {
                Thread.Sleep(10);
                Dispatcher.UIThread.RunJobs();
                using (s.Frame()) { }
            }
            Assert.False(s.Chart.DraftShown, "the full picture did not come back after the gesture stopped");
            Assert.True(clock.Elapsed >= TimeSpan.FromMilliseconds(20), $"settled after {clock.Elapsed.TotalMilliseconds} ms");
            s.Window.Close();
        }

        [AvaloniaFact]
        public void APlotThatPaintsQuicklyIsNeverDrafted()
        {
            // The rule: a draft only when the last full paint took longer than the threshold.
            Assert.Equal(30.0, PlotChart.DefaultDraftThresholdMs);
            PlotViewModel plot = Dense();
            Shown s = Show(plot);
            s.Chart.DraftThresholdMs = double.MaxValue;
            s.Window.MouseWheel(s.At(300, 200), new Vector(0, 1), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            using (s.Frame()) { }
            Assert.False(s.Chart.DraftShown);
            Assert.Equal(0, s.Chart.DraftLayerRenders);

            // A small plot at the default threshold: it paints in a few milliseconds, so it is not drafted.
            var small = new PlotViewModel();
            small.SetSeries(new[] { PlotFixtures.Lock(0, "s", 1, 0, Enumerable.Range(0, 200).Select(i => (double)i).ToArray(),
                                                      Enumerable.Range(0, 200).Select(i => 50.0 + i % 7).ToArray()) });
            Shown t = Show(small);
            Assert.True(t.Chart.LastFullPaintMs < PlotChart.DefaultDraftThresholdMs, $"a 200-sample plot took {t.Chart.LastFullPaintMs} ms");
            t.Window.MouseWheel(t.At(300, 200), new Vector(0, 1), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            using (t.Frame()) { }
            Assert.False(t.Chart.DraftShown);
            Assert.Equal(0, t.Chart.DraftLayerRenders);
            s.Window.Close();
            t.Window.Close();
        }

        [AvaloniaFact]
        public void AnExportDuringADraftIsTheFullPicture()
        {
            PlotViewModel plot = Dense();
            Shown s = Show(plot);
            s.Chart.DraftThresholdMs = 0;
            s.Chart.RefineDelay = TimeSpan.FromHours(1);
            s.Window.MouseWheel(s.At(300, 200), new Vector(0, 1), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            using (s.Frame()) { }
            Assert.True(s.Chart.DraftShown);
            byte[] during = Export(s.Chart);
            s.Chart.Settle();
            using (s.Frame()) { }
            Assert.False(s.Chart.DraftShown);
            Assert.Equal(Export(s.Chart), during);

            static byte[] Export(PlotChart chart)
            {
                using var bitmap = new RenderTargetBitmap(new PixelSize(600, 400));
                using (DrawingContext dc = bitmap.CreateDrawingContext()) chart.RenderTo(dc, chart.Bounds.Size);
                var pixels = new byte[600 * 400 * 4];
                GCHandle pin = GCHandle.Alloc(pixels, GCHandleType.Pinned);
                try
                {
                    bitmap.CopyPixels(new PixelRect(0, 0, 600, 400), pin.AddrOfPinnedObject(), pixels.Length, 600 * 4);
                }
                finally
                {
                    pin.Free();
                }
                return pixels;
            }
            s.Window.Close();
        }

        [AvaloniaFact]
        public void ADraftsLinesAreAPixelWide()
        {
            // A sparse zigzag (no bands, only the line): steep segments whose ink is about as wide as the pen. The draft
            // draws them a device pixel wide, the full picture at the chart's 1.5 px.
            double[] xs = Enumerable.Range(0, 60).Select(i => (double)i).ToArray();
            double[] ys = xs.Select(x => (int)x % 2 == 0 ? 10.0 : 90.0).ToArray();
            var plot = new PlotViewModel();
            plot.SetSeries(new[] { PlotFixtures.Lock(0, "zigzag", 1, 0, xs, ys) });
            Shown s = Show(plot);
            s.Chart.DraftThresholdMs = 0;
            s.Chart.RefineDelay = TimeSpan.FromHours(1);
            s.Window.MouseWheel(s.At(300, 200), new Vector(0, 1), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            int draft = InkInArea(s, plot);
            Assert.True(s.Chart.DraftShown);
            s.Chart.Settle();
            int full = InkInArea(s, plot);
            Assert.False(s.Chart.DraftShown);
            Assert.True(full > draft * 1.2, $"the full picture drew {full} pixels of the line, the draft {draft}");
            s.Window.Close();
        }

        [AvaloniaFact]
        public void OnlyTheLastStepsDelaySettlesTheGesture()
        {
            // Two wheel notches 300 ms apart with a 400 ms delay: the first notch's delay runs out 100 ms after the second
            // notch, and must not settle the gesture the second one is still part of. (If the machine is so slow that the
            // first delay has run out before the second notch, the plot is drafted again by the second, and the check
            // still holds.)
            PlotViewModel plot = Dense();
            Shown s = Show(plot);
            s.Chart.DraftThresholdMs = 0;
            s.Chart.RefineDelay = TimeSpan.FromMilliseconds(400);
            s.Window.MouseWheel(s.At(300, 200), new Vector(0, 1), RawInputModifiers.None);
            Pump(s, TimeSpan.FromMilliseconds(300));
            s.Window.MouseWheel(s.At(300, 200), new Vector(0, 1), RawInputModifiers.None);
            var sinceSecond = Stopwatch.StartNew();
            Pump(s, TimeSpan.FromMilliseconds(200));
            Assert.True(sinceSecond.Elapsed < TimeSpan.FromMilliseconds(400), "too slow to tell: the second delay may have run out too");
            Assert.True(s.Chart.DraftShown, "an earlier step's delay settled the gesture");
            while (s.Chart.DraftShown && sinceSecond.Elapsed < TimeSpan.FromSeconds(5)) Pump(s, TimeSpan.FromMilliseconds(10));
            Assert.False(s.Chart.DraftShown);
            Assert.True(sinceSecond.Elapsed >= TimeSpan.FromMilliseconds(390), $"settled {sinceSecond.Elapsed.TotalMilliseconds} ms after the last step");
            s.Window.Close();

            static void Pump(Shown s, TimeSpan time)
            {
                var clock = Stopwatch.StartNew();
                do
                {
                    Thread.Sleep(5);
                    Dispatcher.UIThread.RunJobs();
                    using (s.Frame()) { }
                } while (clock.Elapsed < time);
            }
        }

        [AvaloniaFact]
        public void ALongSeriesIsDrawnFromItsIndex()
        {
            // A probe, not a use: a series never changes its values, so the index built from them stays right. Here they
            // are changed after it was built - from 95 to 0 - to show what the chart draws from. The whole blocks inside
            // each column come from the index (95, near the top); walked, every sample would be at the bottom.
            double[] xs = Enumerable.Range(0, Samples).Select(i => i * 0.01).ToArray();
            double[] ys = Enumerable.Repeat(95.0, Samples).ToArray();
            var plot = new PlotViewModel();
            plot.SetSeries(new[] { PlotFixtures.Lock(0, "s", 1, 0, xs, ys) });
            Shown s = Show(plot);
            Assert.Equal(1, s.Chart.IndexCount);
            var top = new Rect(5, 0, s.Area.Width - 10, s.Area.Height * 0.1);
            Assert.True(Ink(s, plot, top) > 0);
            Array.Fill(ys, 0.0);
            plot.SetXViewByUser(1, 999);   // a new view, so the geometry is built again
            using (s.Frame()) { }
            Assert.True(Ink(s, plot, top) > 100, "the band's top is not the index's 95: the chart walked the samples");
            s.Window.Close();
        }

        [AvaloniaFact]
        public void ALongOrderedSeriesHasAnIndexUntilItGoes()
        {
            // Two long ordered series get one; a short one and one whose times are out of order do not.
            PlotViewModel plot = Dense(2);
            double[] shortXs = Enumerable.Range(0, 100).Select(i => i * 10.0).ToArray();
            double[] unordered = Enumerable.Range(0, Samples).Select(i => i * 0.01).ToArray();
            (unordered[10], unordered[20]) = (unordered[20], unordered[10]);
            plot.SetSeries(plot.Series.Concat(new[]
            {
                PlotFixtures.Lock(5, "short", 6, 5, shortXs, shortXs.Select(_ => 40.0).ToArray()),
                PlotFixtures.Lock(6, "unordered", 7, 6, unordered, unordered.Select(_ => 60.0).ToArray()),
            }).ToList());
            Assert.Equal(4, plot.Series.Count);
            Shown s = Show(plot);
            Assert.Equal(2, s.Chart.IndexCount);

            // A shift of the times keeps it (it depends on the values alone); a new set of series replaces it; none, none.
            plot.SetXViewByUser(100, 200);
            using (s.Frame()) { }
            Assert.Equal(2, s.Chart.IndexCount);
            plot.SetSeries(Dense(1).Series.ToList());
            using (s.Frame()) { }
            Assert.Equal(1, s.Chart.IndexCount);
            plot.Clear();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(0, s.Chart.IndexCount);
            s.Window.Close();
        }
    }
}
