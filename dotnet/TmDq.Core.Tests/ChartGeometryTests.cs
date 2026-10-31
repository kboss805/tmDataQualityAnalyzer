using System;
using System.Collections.Generic;
using System.Linq;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// The chart's geometry through the binding (plan step 4.3a). The core's own suite compares it with the Qt chart's code;
/// these hold the binding to it: every field of the view reaching the core in its place, the line and every band copied
/// out whole, the transforms and the ticks. The expected values are worked out by hand, in a view whose eight fields
/// are all different, so a field passed in another's place changes an answer.
/// </summary>
public sealed class ChartGeometryTests
{
    public ChartGeometryTests() => Env.EnsureLoaded();

    /// <summary>x 0..128 over 128 px from column 0, y 0..100 over 100 px from row 0: data and pixels are the same numbers.</summary>
    private static readonly ChartView Unit = new(0, 128, 0, 100, 0, 0, 128, 100);

    private static (double[] Xs, double[] Ys) Dense(Func<int, double> y)
    {
        double[] xs = Enumerable.Range(0, 1024).Select(i => i * 0.125).ToArray();
        double[] ys = Enumerable.Range(0, 1024).Select(y).ToArray();
        return (xs, ys);
    }

    [Fact]
    public void ADenseSeriesStillReachesASingleSampleDropout()
    {
        // Eight samples a column, all at 100 but one, in column 65, at 0. The band reaches the bottom there, and the line
        // is that column's mean: (7 x 0 px + 100 px) / 8 = 12.5 px.
        (double[] xs, double[] ys) = Dense(i => i == 520 ? 0.0 : 100.0);
        SeriesGeometry g = ChartGeometry.Build(xs, ys, true, Unit);
        Assert.True(g.Decimated);
        Assert.Equal(127, g.SegmentCount);
        double[] band = Assert.Single(g.Bands);
        Assert.Equal(512, band.Length);   // 256 points
        for (int c = 0; c < 128; c++)
        {
            Assert.Equal(c, band[2 * c]);
            Assert.Equal(0.0, band[2 * c + 1]);
            Assert.Equal(c, band[2 * (255 - c)]);
            Assert.Equal(c == 65 ? 100.0 : 0.0, band[2 * (255 - c) + 1]);
        }
        for (int c = 0; c < 127; c++)
        {
            Assert.Equal(c, g.Segments[4 * c]);
            Assert.Equal(c == 65 ? 12.5 : 0.0, g.Segments[4 * c + 1]);
            Assert.Equal(c + 1, g.Segments[4 * c + 2]);
        }
    }

    [Fact]
    public void ADenseNoisySeriesShowsItsSpreadWithItsMeanThroughTheMiddle()
    {
        (double[] xs, double[] ys) = Dense(i => i % 2 == 1 ? 40.0 : 60.0);
        SeriesGeometry g = ChartGeometry.Build(xs, ys, true, Unit);
        double[] band = Assert.Single(g.Bands);
        for (int c = 0; c < 128; c++)
        {
            Assert.Equal(40.0, band[2 * c + 1]);
            Assert.Equal(60.0, band[2 * (255 - c) + 1]);
        }
        for (int s = 0; s < g.SegmentCount; s++)
        {
            Assert.Equal(50.0, g.Segments[4 * s + 1]);
            Assert.Equal(50.0, g.Segments[4 * s + 3]);
        }
    }

    [Fact]
    public void EveryFieldOfTheViewIsUsedInItsPlace()
    {
        // x 10..30 over 400 px from column 50, y -5..15 over 200 px from row 20 (bottom 220). Three samples, drawn one by
        // one: x 10, 20, 30 at columns 50, 250, 450; y -5, 5, 15 at rows 220, 120, 20.
        var view = new ChartView(10, 30, -5, 15, 50, 20, 400, 200);
        SeriesGeometry g = ChartGeometry.Build(new[] { 10.0, 20.0, 30.0 }, new[] { -5.0, 5.0, 15.0 }, true, view);
        Assert.False(g.Decimated);
        Assert.Empty(g.Bands);
        Assert.Equal(new[] { 50.0, 220.0, 250.0, 120.0, 250.0, 120.0, 450.0, 20.0 }, g.Segments);

        Assert.Equal(250.0, ChartGeometry.XToPixel(view, 20.0));
        Assert.Equal(20.0, ChartGeometry.PixelToX(view, 250.0));
        Assert.Equal(120.0, ChartGeometry.YToPixel(view, 5.0));
        Assert.Equal(5.0, ChartGeometry.PixelToY(view, 120.0));
    }

    [Fact]
    public void ALineCrossesAViewNoSampleIsInAndAGapBreaksIt()
    {
        var narrow = Unit with { XLower = 40, XUpper = 60 };
        SeriesGeometry g = ChartGeometry.Build(new[] { 0.0, 100.0 }, new[] { 50.0, 50.0 }, true, narrow);
        Assert.Equal(new[] { -256.0, 50.0, 384.0, 50.0 }, g.Segments);

        g = ChartGeometry.Build(new[] { 0.0, 1, 2, 3, 4 }, new[] { 10.0, 20, double.NaN, 40, 50 }, true, Unit);
        Assert.Equal(new[] { 0.0, 90, 1, 80, 3, 60, 4, 50 }, g.Segments);
    }

    [Fact]
    public void OnlyTheSamplesInViewAreWalkedWhenTheSeriesIsOrdered()
    {
        double[] xs = Enumerable.Range(0, 10000).Select(i => (double)i).ToArray();
        double[] ys = Enumerable.Repeat(1.0, 10000).ToArray();
        var view = Unit with { XLower = 10.5, XUpper = 20.5 };
        Assert.True(ChartGeometry.IsSortedX(xs));
        SeriesGeometry g = ChartGeometry.Build(xs, ys, true, view);
        Assert.False(g.Decimated);
        Assert.Equal(11, g.SegmentCount);   // samples 10 to 21
        Assert.True(ChartGeometry.Build(xs, ys, false, view).Decimated);   // walked whole
    }

    [Fact]
    public void OnlyThePairedSamplesCountAndNoSamplesIsNoLine()
    {
        SeriesGeometry g = ChartGeometry.Build(new[] { 0.0, 10, 20, 30 }, new[] { 1.0, 2.0 }, true, Unit);
        Assert.Equal(1, g.SegmentCount);
        g = ChartGeometry.Build(ReadOnlySpan<double>.Empty, ReadOnlySpan<double>.Empty, true, Unit);
        Assert.Empty(g.Segments);
        Assert.Empty(g.Bands);
        Assert.False(g.Decimated);
    }

    [Fact]
    public void SortednessAndUsableRangesAreTheCores()
    {
        Assert.True(ChartGeometry.IsSortedX(Array.Empty<double>()));
        Assert.True(ChartGeometry.IsSortedX(new[] { double.NaN, 1.0, 2.0 }));   // the first is not checked
        Assert.True(ChartGeometry.IsSortedX(new[] { 1.0, 1.0 }));
        Assert.False(ChartGeometry.IsSortedX(new[] { 1.0, double.NaN }));
        Assert.False(ChartGeometry.IsSortedX(new[] { 2.0, 1.0 }));

        Assert.True(ChartGeometry.UsableRange(0, 1e-300));
        Assert.False(ChartGeometry.UsableRange(1, 1));
        Assert.False(ChartGeometry.UsableRange(2, 1));
        Assert.False(ChartGeometry.UsableRange(double.NaN, 1));
        Assert.False(ChartGeometry.UsableRange(0, double.PositiveInfinity));

        var bad = new ChartView(5, 5, 0, double.NaN, 50, 20, 0, 0);
        Assert.Equal(50.0, ChartGeometry.XToPixel(bad, 7));
        Assert.Equal(20.0, ChartGeometry.YToPixel(bad, 7));   // the bottom: top + a height of 0
        Assert.Equal(5.0, ChartGeometry.PixelToX(bad, 99));
        Assert.Equal(0.0, ChartGeometry.PixelToY(bad, 99));
    }

    [Fact]
    public void TimeTicksLandOnRoundTimesOfTheClock()
    {
        Assert.Equal(new[] { 0.0, 30, 60, 90 }, ChartGeometry.TimeTicks(0, 116, 5));
        Assert.Equal(new[] { 1020.0, 1050, 1080, 1110 }, ChartGeometry.TimeTicks(1000, 1116, 5));
        Assert.Equal(new[] { 0.0, 8 * 86400, 16 * 86400, 24 * 86400 }, ChartGeometry.TimeTicks(0, 30 * 86400, 5));
        Assert.Equal(new[] { 0.0, 0.125, 0.25, 0.375, 0.5 }, ChartGeometry.TimeTicks(0, 0.5, 5));

        // 10:30:17.046 as the clock of x = 0: the first tick is at 10:30:30, 12.954 s in.
        double[] onTheClock = ChartGeometry.TimeTicks(0, 116, 5, 37817.046);
        Assert.Equal(4, onTheClock.Length);
        Assert.Equal(12.954, onTheClock[0], 9);
        Assert.All(onTheClock, t => Assert.Equal(0.0, Math.IEEERemainder(t + 37817.046, 30), 6));
        Assert.Empty(ChartGeometry.TimeTicks(5, 5, 5));
    }

    [Fact]
    public void PlainTicksDivideTheRangeEvenly()
    {
        Assert.Equal(new[] { 0.0, 25, 50, 75, 100 }, ChartGeometry.Ticks(0, 100, 5));
        Assert.Equal(new[] { -1.0, 1 }, ChartGeometry.Ticks(-1, 1, 0));
        Assert.Empty(ChartGeometry.Ticks(1, 1, 5));
        Assert.Equal(26, ChartGeometry.Ticks(0, 1, 26).Length);   // more than any first guess at a buffer
    }

    [Fact]
    public void AGapInADenseSeriesMakesTwoBands()
    {
        (double[] xs, double[] ys) = Dense(_ => 50.0);
        xs[512] = double.NaN;
        SeriesGeometry g = ChartGeometry.Build(xs, ys, false, Unit);
        Assert.True(g.Decimated);
        Assert.Equal(2, g.Bands.Count);
        Assert.Equal(126, g.SegmentCount);
        Assert.All(g.Bands, b => Assert.Equal(256, b.Length));   // 64 columns, a top and a bottom point each, two values a point
        Assert.Equal(64.125, g.Bands[1][0]);
    }
}
