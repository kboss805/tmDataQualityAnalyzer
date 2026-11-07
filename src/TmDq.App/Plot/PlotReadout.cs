using System;
using System.Collections.Generic;
using System.Globalization;
using TmDq.Core;

namespace TmDq.App.Plot
{
    /// <summary>What the crosshair's readout says (US4.1): the series, the clock time and the value of the sample under
    /// the cursor, or of the series it is pinned to.</summary>
    internal static class PlotReadout
    {
        /// <summary>How near the cursor, in pixels, an unpinned readout's sample must be.</summary>
        private const double ReachPixels = 10.0;

        /// <summary>
        /// The readout with the cursor at time <paramref name="x"/> (pixel column <paramref name="cursorPixel"/>): the
        /// nearest sample of any series <paramref name="shown"/>, if it lies within 10 px of the cursor; or, when
        /// <paramref name="pinnedId"/> names a series, that series' nearest sample wherever it is. Null when there is
        /// nothing to say.
        /// </summary>
        public static string? Describe(IReadOnlyList<PlotSeries> series, Func<PlotSeries, bool> shown, int? pinnedId,
                                       double x, double cursorPixel, Func<double, double> xToPixel,
                                       Func<double, string> formatTime)
        {
            int pinnedIndex = -1;
            if (pinnedId is int id)
                for (int i = 0; i < series.Count; i++)
                    if (series[i].Id == id) { pinnedIndex = i; break; }
            bool pinned = pinnedIndex >= 0;
            double bestDist = double.MaxValue, bestX = 0.0, bestY = 0.0;
            int best = -1;
            for (int i = 0; i < series.Count; i++)
            {
                if (pinned && i != pinnedIndex) continue;
                PlotSeries s = series[i];
                if (!shown(s) || s.X.Length == 0) continue;
                int at = LowerBound(s.X, x);
                for (int k = at; k < s.X.Length && k - at < 2; k++)
                    Consider(i, k);
                if (at > 0) Consider(i, at - 1);

                void Consider(int which, int index)
                {
                    double d = Math.Abs(series[which].X[index] - x);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        bestX = series[which].X[index];
                        bestY = series[which].Y[index];
                        best = which;
                    }
                }
            }
            if (best < 0) return null;
            if (!pinned && Math.Abs(xToPixel(bestX) - cursorPixel) > ReachPixels) return null;
            PlotSeries found = series[best];
            if (found.Name.Length == 0) return null;
            string unit = found.Metric switch
            {
                MetricType.FrameSyncLock => "%",
                MetricType.AccumulatedMissedFrames => " frames",
                _ => " dB",
            };
            return $"{found.Name}{(pinned ? "  (pinned)" : "")}\n{formatTime(bestX)}\n" +
                   bestY.ToString("F2", CultureInfo.InvariantCulture) + unit;
        }

        /// <summary>The first index whose time is not before <paramref name="value"/> (the times sorted).</summary>
        private static int LowerBound(double[] xs, double value)
        {
            int lo = 0, hi = xs.Length;
            while (lo < hi)
            {
                int mid = lo + (hi - lo) / 2;
                if (xs[mid] < value) lo = mid + 1;
                else hi = mid;
            }
            return lo;
        }
    }
}
