using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Media;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;
using Xunit;
using static TmDq.App.Tests.AroundTheChartSupport;

namespace TmDq.App.Tests;

/// <summary>
/// The plot's state behind the chart's legend, chips and menu (plan step 4.4): what they read of the series, the ranges and
/// the window, with no control shown.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class ChartPlotStateTests
{
    public ChartPlotStateTests() => NativeCore.EnsureLoaded();

    [Fact]
    public void TheLegendListsTheSeriesShownOfTheMetricInView()
    {
        var plot = new PlotViewModel();
        plot.SetSeries(LockStream(0, "CH-01 PRN 15 5M", 1, 0).Append(Snr(2, 40, 1, 3, 0, "40 - CH-01 2250.5MHZ AGC 800Kbps RNRZ-L L_RCVR3")));
        LegendEntry[] rows = plot.LegendEntries().ToArray();
        Assert.Equal(new[] { 0, 2 }, rows.Select(r => r.Id));
        Assert.Equal("CH-01 PRN 15 5M", rows[0].Label);
        // An SNR row is short (the channel and the receiver's code), its full name kept for the tooltip.
        Assert.Equal("CH40 L_RCVR3", rows[1].Label);
        Assert.Equal("40 - CH-01 2250.5MHZ AGC 800Kbps RNRZ-L L_RCVR3", rows[1].FullName);
        Assert.Equal(plot.Series[2].Color, rows[1].Color);
        // Missed frames made visible by hand in the lock view are not listed either.
        plot.Series[1].Visible = true;
        Assert.Equal(new[] { 0, 2 }, plot.LegendEntries().Select(r => r.Id));
        plot.Series[1].Visible = false;

        plot.SetLockAxisView(LockAxisView.MissedFrames);
        Assert.Equal(new[] { 1, 2 }, plot.LegendEntries().Select(r => r.Id));
        plot.SetSeriesVisibleQuiet(2, false); plot.CommitAppearance();
        Assert.Equal(new[] { 1 }, plot.LegendEntries().Select(r => r.Id));
        // A lock series made visible by hand in the wrong view is still not listed.
        plot.Series[0].Visible = true;
        Assert.Equal(new[] { 1 }, plot.LegendEntries().Select(r => r.Id));
        Assert.Empty(new PlotViewModel().LegendEntries());
    }

    [Theory]
    [InlineData(0, "L")]
    [InlineData(1, "R")]
    [InlineData(2, "C")]
    [InlineData(3, "4")]
    [InlineData(-1, "0")]
    public void AChannelIsNamedByItsLetterOrItsNumber(int channel, string expected)
    {
        Assert.Equal(expected, PlotViewModel.ChannelLabel(channel));
        Assert.Equal($"CH7 {expected}_RCVR2", PlotViewModel.LegendLabel(PlotFixtures.Snr(0, "long", 7, 0, 2, channel, X3, X3)));
    }

    [Fact]
    public void ATimeWindowIsClampedIntoTheRecordingWithAWarningForEachEnd()
    {
        PlotViewModel plot = LockPlot();
        var warnings = new List<string>();
        plot.Warning += (_, w) => warnings.Add(w);

        plot.ApplyTimeWindow(0.5, 1.5, "a", "b");
        Assert.Empty(warnings);
        Assert.Equal((0.5, 1.5), (plot.XViewMin, plot.XViewMax));
        Assert.True(plot.CanZoomBack);   // a discrete zoom, remembered

        plot.ApplyTimeWindow(-3, 9, "early", "late");
        Assert.Equal(new[]
        {
            "Start \"early\" is outside the file time range - clamped to file bounds.",
            "Stop \"late\" is outside the file time range - clamped to file bounds.",
        }, warnings);
        Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));

        warnings.Clear();
        plot.ApplyTimeWindow(1.5, 0.5, "x", "y");
        Assert.Equal(new[] { "Stop time is before Start - clamped to start time." }, warnings);
        Assert.Equal((1.5, 1.5), (plot.XViewMin, plot.XViewMax));
        // A stop equal to the start is not before it.
        warnings.Clear();
        plot.ApplyTimeWindow(1.0, 1.0, "x", "x");
        Assert.Empty(warnings);

        // Without data nothing happens.
        var empty = new PlotViewModel();
        empty.Warning += (_, w) => warnings.Add(w);
        warnings.Clear();
        empty.ApplyTimeWindow(-1, 5, "a", "b");
        Assert.Empty(warnings);
        Assert.False(empty.CanZoomBack);
    }

    [Fact]
    public void ATypedClockTimeIsElapsedTimeOnThePlotsAxis()
    {
        var plot = new PlotViewModel();
        Assert.Equal(86400 + 2 * 3600 + 3 * 60 + 4, plot.ParseTime("001:02:03:04"));
        Assert.Equal(-5, plot.ParseTime("000:00:00:-5"));
        Assert.Equal(10, plot.ParseTime(" 000:00:00:10 "));
        // A field that is not a whole number (a sign and spaces around it allowed) counts as 0; the wrong number of
        // fields is 0.
        Assert.Equal(3600, plot.ParseTime("000:01:xx:00"));
        Assert.Equal(60, plot.ParseTime("000:00:01:1.5"));
        Assert.Equal(0, plot.ParseTime("01:02:03"));
        Assert.Equal(0, plot.ParseTime("0:0:0:1:0"));

        // Against a recording's clock: a label of the axis reads back as its time.
        var real = new PlotViewModel();
        using RunOutcome outcome = Run();
        real.AddStream(outcome.Results[0]);
        double whole = Math.Ceiling(real.TimeOrigin) - real.TimeOrigin + 20.0;   // a time on a whole second
        Assert.Equal(whole, real.ParseTime(real.FormatTime(whole)), 6);
    }

    private static RunOutcome Run()
    {
        string path = Repo.Recording("prn_testfile, sample.ch10");
        Ch10File file = Ch10File.Open(path);
        StreamConfig stream = FirstChannelRun.DefaultStream(new ChannelInfo(13, "CH 13", 0));
        return StreamsRun.Run(new RunRequest(path, file.DefaultTimeChannel, new[] { stream }) { AppRoot = Repo.Root }, null);
    }

    [Fact]
    public void TheArrowsAndPlusAndMinusStepTheWindow()
    {
        PlotViewModel plot = LockPlot();
        var warnings = new List<string>();
        plot.Warning += (_, w) => warnings.Add(w);

        // The whole recording shown: nowhere to pan.
        plot.PanWindow(PlotViewModel.KeyPanFraction);
        Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
        Assert.False(plot.CanZoomBack);

        plot.SetXViewByUser(0.4, 0.6);
        plot.PanWindow(PlotViewModel.KeyPanFraction);
        Assert.Equal(0.42, plot.XViewMin, 12);
        Assert.Equal(0.62, plot.XViewMax, 12);
        plot.PanWindow(-PlotViewModel.KeyPanFraction);
        Assert.Equal(0.4, plot.XViewMin, 12);
        Assert.Equal(0.6, plot.XViewMax, 12);
        Assert.True(plot.CanZoomBack);   // each step is a discrete zoom

        plot.ZoomWindow(PlotViewModel.KeyZoomFactor);
        Assert.Equal(0.42, plot.XViewMin, 12);
        Assert.Equal(0.58, plot.XViewMax, 12);
        plot.ZoomWindow(1.0 / PlotViewModel.KeyZoomFactor);
        Assert.Equal(0.4, plot.XViewMin, 12);
        Assert.Equal(0.6, plot.XViewMax, 12);
        Assert.Empty(warnings);

        // Past the end, the window is clamped, and the log says so.
        plot.SetXViewByUser(1.9, 2.0);
        plot.PanWindow(PlotViewModel.KeyPanFraction);
        Assert.Equal(2.0, plot.XViewMax);
        Assert.Single(warnings);
        Assert.StartsWith("Stop \"", warnings[0]);

        // Never narrower than a band zoom may be.
        plot.SetXViewByUser(1.0, 1.0 + 1.1e-6);
        plot.ZoomWindow(PlotViewModel.KeyZoomFactor);
        Assert.Equal(1.0 + 1.1e-6, plot.XViewMax, 12);
    }

    [Fact]
    public void AZoomAboutAnAnchorKeepsTheAnchorWhereItIsAndBothGesturesRefuseTheSameNarrowWindow()
    {
        // One rule for the wheel (the anchor is the pointer's time) and the keys (the window's centre): the view model's.
        PlotViewModel plot = LockPlot();   // the window is the whole recording, 0 to 2 s
        Assert.Equal((0.25, 1.25), plot.WindowZoomedAbout(0.5, 0.5));    // in: the anchor's fraction of the window is kept
        Assert.Equal((-0.5, 3.5), plot.WindowZoomedAbout(0.5, 2.0));     // out: not clamped here, the gesture's setter does that
        Assert.Equal((0.0, 2.0), plot.WindowZoomedAbout(0.5, 1.0));

        // The keys are the same rule about the centre.
        plot.SetXViewByUser(0.4, 0.6);
        (double lower, double upper) = plot.WindowZoomedAbout(0.5, PlotViewModel.KeyZoomFactor)!.Value;
        plot.ZoomWindow(PlotViewModel.KeyZoomFactor);
        Assert.Equal((lower, upper), (plot.XViewMin, plot.XViewMax));

        // Neither may leave a window narrower than a band zoom may be; zooming out of one is allowed.
        plot.SetXViewByUser(1.0, 1.0 + 1.1e-6);
        Assert.Null(plot.WindowZoomedAbout(1.0, PlotViewModel.KeyZoomFactor));
        Assert.Null(plot.WindowZoomedAbout(1.0 + 1e-6, PlotViewModel.WheelZoomFactor));
        Assert.NotNull(plot.WindowZoomedAbout(1.0, 1.0 / PlotViewModel.WheelZoomFactor));
    }

    [Fact]
    public void TheViewModeSwitchesOnlyWhenBothMetricsExistAndResetViewResetsBothAxes()
    {
        PlotViewModel plot = LockPlot();
        Assert.True(plot.CanSwitchViewMode);
        plot.ToggleViewMode();
        Assert.Equal(LockAxisView.MissedFrames, plot.LockAxisView);
        plot.ToggleViewMode();
        Assert.Equal(LockAxisView.LockPercent, plot.LockAxisView);

        PlotViewModel snr = SnrOnly();
        Assert.False(snr.CanSwitchViewMode);
        snr.ToggleViewMode();
        Assert.Equal(LockAxisView.LockPercent, snr.LockAxisView);
        var lockOnly = new PlotViewModel();
        lockOnly.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, X3, X3) });
        Assert.False(lockOnly.CanSwitchViewMode);
        lockOnly.ToggleViewMode();
        Assert.Equal(LockAxisView.LockPercent, lockOnly.LockAxisView);
        Assert.False(new PlotViewModel().CanSwitchViewMode);
        // Missed frames alone (an imported CSV can carry them without the lock column): nothing to switch to either.
        var missedOnly = new PlotViewModel();
        missedOnly.SetSeries(new[] { PlotFixtures.Missed(0, "a", 1, 0, X3, X3) });
        Assert.False(missedOnly.HasLockSeries);
        Assert.True(missedOnly.HasMissedFramesSeries);
        Assert.False(missedOnly.CanSwitchViewMode);

        Assert.False(plot.CanResetView);
        // Zoomed at either end alone is zoomed.
        plot.SetXViewByUser(0.0, 1.5);
        Assert.True(plot.IsZoomed);
        plot.SetXViewByUser(0.5, 2.0);
        Assert.True(plot.IsZoomed);
        plot.ResetXRange();
        plot.SetXViewByUser(0.5, 1.5);
        Assert.True(plot.IsZoomed);
        Assert.True(plot.CanResetView);
        plot.ResetXRange();
        Assert.False(plot.CanResetView);
        plot.SetRightMaxOverride(24);
        Assert.False(plot.IsZoomed);
        Assert.True(plot.CanResetView);
        plot.SetXViewByUser(0.5, 1.5);
        plot.SetLeftMaxOverride(42);
        plot.ResetView();
        Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
        Assert.False(plot.HasRangeOverride);
        // A limit set with nothing shown is nothing to reset.
        var empty = new PlotViewModel();
        empty.SetRightMaxOverride(24);
        Assert.False(empty.CanResetView);
    }

    [Fact]
    public void ARenameOrRecolourReachesAStreamsOtherMetricAndNoOtherStream()
    {
        var plot = new PlotViewModel();
        PlotSeries otherSource = PlotFixtures.Make(6, "Ch 5", MetricType.FrameSyncLock, 5, 2, X3, X3, source: 1);
        PlotSeries sameNameElsewhere = PlotFixtures.Lock(7, "Ch 5", 7, 4, X3, X3);
        PlotSeries otherNameSameChannel = PlotFixtures.Missed(8, "Ch 5b", 5, 4, X3, X3);
        PlotSeries snrSameStream = PlotFixtures.Make(9, "Ch 5", MetricType.Snr, 5, 4, X3, X3);
        plot.SetSeries(LockStream(0, "Ch 5", 5, 0).Concat(LockStream(2, "Ch 6", 6, 1)).Append(Snr(4, 40, 3, 1, 0)).Append(Snr(5, 40, 3, 1, 1))
            .Append(otherSource).Append(sameNameElsewhere).Append(otherNameSameChannel).Append(snrSameStream));
        plot.RenameSeries(0, "renamed");
        plot.RecolorSeries(0, Colors.Teal);
        Assert.Equal(new[] { "renamed", "renamed", "Ch 6", "Ch 6" }, plot.Series.Take(4).Select(s => s.Name));
        Assert.Equal(new[] { Colors.Teal, Colors.Teal }, plot.Series.Take(2).Select(s => s.Color));
        Assert.NotEqual(Colors.Teal, plot.Series[2].Color);
        // A sibling is the same stream exactly: its label, its channel, its recording, and on the left axis.
        foreach ((int id, string name) in new[] { (6, "Ch 5"), (7, "Ch 5"), (8, "Ch 5b"), (9, "Ch 5") })
        {
            Assert.Equal(name, plot.SeriesById(id)!.Name);
            Assert.NotEqual(Colors.Teal, plot.SeriesById(id)!.Color);
        }

        // An SNR channel alone.
        plot.RenameSeries(4, "left");
        plot.RecolorSeries(4, Colors.Olive);
        Assert.Equal("left", plot.SeriesById(4)!.Name);
        Assert.NotEqual("left", plot.SeriesById(5)!.Name);
        Assert.NotEqual(Colors.Olive, plot.SeriesById(5)!.Color);

        // A series that is gone is left alone.
        plot.RenameSeries(99, "x");
        plot.RecolorSeries(99, Colors.Red);
        Assert.Null(plot.SeriesById(99));
    }

    [Fact]
    public void APinToASeriesHiddenOrGoneIsDropped()
    {
        PlotViewModel plot = LockPlot();
        plot.ReadoutSeriesId = 0;
        plot.DropStaleReadoutPin();
        Assert.Equal(0, plot.ReadoutSeriesId);
        plot.SetSeriesVisibleQuiet(0, false); plot.CommitAppearance();
        plot.DropStaleReadoutPin();
        Assert.Null(plot.ReadoutSeriesId);
        plot.ReadoutSeriesId = 42;
        plot.DropStaleReadoutPin();
        Assert.Null(plot.ReadoutSeriesId);
        plot.DropStaleReadoutPin();
        Assert.Null(plot.ReadoutSeriesId);
    }

    [Fact]
    public void AQuietBatchOfVisibilityIsShownByOneRefresh()
    {
        var plot = new PlotViewModel();
        plot.SetSeries(new[]
        {
            PlotFixtures.Snr(0, "low", 40, 0, 1, 0, X3, new[] { 10.0, 12.0, 14.0 }),
            PlotFixtures.Snr(1, "high", 40, 0, 2, 0, X3, new[] { 40.0, 45.0, 50.0 }),
        });
        double before = plot.RightMin;
        int series = 0, ranges = 0;
        plot.SeriesChanged += (_, _) => series++;
        plot.RangesChanged += (_, _) => ranges++;
        plot.SetSeriesVisibleQuiet(0, false);
        plot.SetSeriesVisibleQuiet(99, false);
        Assert.Equal((0, 0), (series, ranges));
        Assert.Equal(before, plot.RightMin);   // not yet recomputed
        Assert.True(before <= 10.0);
        plot.CommitAppearance();
        Assert.Equal((1, 1), (series, ranges));
        Assert.True(plot.RightMin > 14.0, $"the SNR range still starts at {plot.RightMin} without the hidden series");
        Assert.False(plot.Series[0].Visible);
    }
}
