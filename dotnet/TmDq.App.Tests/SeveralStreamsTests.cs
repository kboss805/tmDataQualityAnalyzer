using System;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Threading;
using TmDq.App.Processing;
using TmDq.App.Views;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// What several streams need beyond the walking skeleton (plan step 4.1b): the chart draws every Frame Sync Lock
    /// stream on one time axis, each in its own primary (US4.0), and the build puts the shipped settings beside the
    /// application, where Configure Streams and the core look for them.
    /// </summary>
    public sealed class SeveralStreamsTests
    {
        /// <summary>Pixels of <paramref name="area"/> drawn in <paramref name="colour"/> over the white background. An
        /// antialiased line is the colour blended with white, so a pixel is counted when its departure from white points
        /// the same way as the colour's (within a few degrees), not when it is the colour itself.</summary>
        private static int Near(ILockedFramebuffer fb, Rect area, Color colour)
        {
            double cr = 255 - colour.R, cg = 255 - colour.G, cb = 255 - colour.B;
            double cn = Math.Sqrt(cr * cr + cg * cg + cb * cb);
            int count = 0;
            for (int y = (int)area.Top; y < (int)area.Bottom; y++)
                for (int x = (int)area.Left; x < (int)area.Right; x++)
                {
                    uint p = Frames.Pixel(fb, x, y);
                    double r = 255 - (int)((p >> 16) & 0xFF), g = 255 - (int)((p >> 8) & 0xFF), b = 255 - (int)(p & 0xFF);
                    double n = Math.Sqrt(r * r + g * g + b * b);
                    if (n > 40 && (r * cr + g * cg + b * cb) / (n * cn) > 0.995) count++;
                }
            return count;
        }

        /// <summary>The commonest colours in an area, for a failure message.</summary>
        private static string Seen(ILockedFramebuffer fb, Rect area)
        {
            var counts = new System.Collections.Generic.Dictionary<uint, int>();
            for (int y = (int)area.Top; y < (int)area.Bottom; y++)
                for (int x = (int)area.Left; x < (int)area.Right; x++)
                {
                    uint p = Frames.Pixel(fb, x, y);
                    counts[p] = counts.TryGetValue(p, out int n) ? n + 1 : 1;
                }
            return string.Join(", ", counts.OrderByDescending(kv => kv.Value).Take(4).Select(kv => $"{kv.Key:X6} x{kv.Value}")) + $" in {area}";
        }

        [AvaloniaFact]
        public void EveryLockStreamIsDrawnInItsOwnColourOnOneTimeAxis()
        {
            // The first stream spans 0 to 10 s at 75 %, the second 5 to 20 s at 25 %: on one axis of 0 to 20 s the second
            // starts a quarter of the way in, in the second primary, and the first ends half way, in the first.
            var chart = new LockChart { Width = 400, Height = 300 };
            var window = new Window { Content = chart, SizeToContent = SizeToContent.WidthAndHeight };
            window.Show();
            chart.AllSeries = new[]
            {
                new LockSeries("first", 13, new[] { 0.0, 10.0 }, new[] { 75.0, 75.0 }),
                new LockSeries("second", 15, new[] { 5.0, 20.0 }, new[] { 25.0, 25.0 }),
            };
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(2, chart.Drawn.Count);
            Rect plot = Frames.InWindow(chart, window).Deflate(LockChart.PlotInset);
            double y75 = plot.Bottom - 0.75 * plot.Height, y25 = plot.Bottom - 0.25 * plot.Height;
            Rect Band(double y, double from, double to) => new(plot.Left + from * plot.Width, y - 3, (to - from) * plot.Width, 6);

            using ILockedFramebuffer fb = Frames.Capture(window).Lock();
            Color first = LockChart.Colors[0], second = LockChart.Colors[1];
            Assert.True(Near(fb, Band(y75, 0.05, 0.45), first) > 80, "the first stream, in the first colour, over its first half: " + Seen(fb, Band(y75, 0.05, 0.45)));
            Assert.Equal(0, Near(fb, Band(y75, 0.55, 0.95), first));     // it ends at 10 s, half way
            Assert.Equal(0, Near(fb, Band(y75, 0.05, 0.45), second));
            Assert.True(Near(fb, Band(y25, 0.30, 0.95), second) > 80, "the second stream, in the second colour, from a quarter in");
            Assert.Equal(0, Near(fb, Band(y25, 0.02, 0.20), second));    // it starts at 5 s, a quarter of the way
            Assert.Equal(0, Near(fb, Band(y25, 0.30, 0.95), first));
            window.Close();
        }

        [Fact]
        public void TheChartDrawsTheListWhenItHasOneAndTheSingleSeriesOtherwise()
        {
            var one = new LockSeries("one", 1, new[] { 0.0, 1.0 }, new[] { 50.0, 50.0 });
            var two = new LockSeries("two", 2, new[] { 0.0, 1.0 }, new[] { 50.0, 50.0 });
            var tooShort = new LockSeries("short", 3, new[] { 0.0 }, new[] { 50.0 });
            Assert.Empty(new LockChart().Drawn);
            Assert.Equal(new[] { one }, new LockChart { Series = one }.Drawn);
            Assert.Equal(new[] { one }, new LockChart { Series = one, AllSeries = Array.Empty<LockSeries>() }.Drawn);
            Assert.Equal(new[] { two, one }, new LockChart { Series = one, AllSeries = new[] { two, tooShort, one } }.Drawn);
        }

        [Fact]
        public void TheShippedSettingsAreBesideTheApplication()
        {
            string repoSettings = Path.Combine(Repo.Root, "settings");
            string[] shipped = Directory.GetFiles(repoSettings, "*.toml", SearchOption.AllDirectories)
                                        .Select(f => Path.GetRelativePath(repoSettings, f)).OrderBy(f => f, StringComparer.Ordinal).ToArray();
            Assert.Contains(Path.Combine("framesync_patterns", "framesync_rcvr_default.toml"), shipped);
            Assert.Contains(Path.Combine("receiver_params", "default.toml"), shipped);
            string beside = Path.Combine(Repo.AppOutput, "settings");
            foreach (string f in shipped)
                Assert.Equal(File.ReadAllBytes(Path.Combine(repoSettings, f)), File.ReadAllBytes(Path.Combine(beside, f)));
        }
    }
}
