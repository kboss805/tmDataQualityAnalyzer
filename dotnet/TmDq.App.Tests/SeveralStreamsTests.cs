using System;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Threading;
using TmDq.App.Plot;
using TmDq.App.Processing;
using TmDq.App.Views;
using TmDq.Core;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// What several streams need beyond the walking skeleton (plan step 4.1b): the chart draws every Frame Sync Lock
    /// stream on one time axis, each in its own primary (US4.0; the chart is plan step 4.3b's), and the build puts the
    /// shipped settings beside the application, where Configure Streams and the core look for them.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class SeveralStreamsTests
    {
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
            NativeCore.EnsureLoaded();
            var plot = new PlotViewModel();
            plot.SetSeries(new[]
            {
                PlotFixtures.Lock(0, "first", 13, 0, new[] { 0.0, 10.0 }, new[] { 75.0, 75.0 }),
                PlotFixtures.Lock(1, "second", 15, 1, new[] { 5.0, 20.0 }, new[] { 25.0, 25.0 }),
            });
            var chart = new PlotChart { Width = 600, Height = 400, Plot = plot };
            var window = new Window { Content = chart, SizeToContent = SizeToContent.WidthAndHeight };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Rect area = Frames.InWindow(chart, window);
            Rect plotArea = new Rect(area.X + chart.PlotArea.X, area.Y + chart.PlotArea.Y, chart.PlotArea.Width, chart.PlotArea.Height);
            double y75 = plotArea.Bottom - 0.75 * plotArea.Height, y25 = plotArea.Bottom - 0.25 * plotArea.Height;
            Rect Band(double y, double from, double to) => new(plotArea.Left + from * plotArea.Width, y - 3, (to - from) * plotArea.Width, 6);

            using ILockedFramebuffer fb = Frames.Capture(window).Lock();
            Color first = PlotColors.FrameSyncLock[0], second = PlotColors.FrameSyncLock[1];
            Assert.True(Frames.Near(fb, Band(y75, 0.05, 0.45), first) > 80, "the first stream, in the first colour, over its first half: " + Seen(fb, Band(y75, 0.05, 0.45)));
            Assert.Equal(0, Frames.Near(fb, Band(y75, 0.55, 0.95), first));     // it ends at 10 s, half way
            Assert.Equal(0, Frames.Near(fb, Band(y75, 0.05, 0.45), second));
            Assert.True(Frames.Near(fb, Band(y25, 0.30, 0.95), second) > 80, "the second stream, in the second colour, from a quarter in");
            Assert.Equal(0, Frames.Near(fb, Band(y25, 0.02, 0.20), second));    // it starts at 5 s, a quarter of the way
            Assert.Equal(0, Frames.Near(fb, Band(y25, 0.30, 0.95), first));
            window.Close();
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
