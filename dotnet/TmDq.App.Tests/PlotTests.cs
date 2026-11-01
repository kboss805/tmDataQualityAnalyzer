using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
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
    /// The chart (plan step 4.3b): the plot's state (<see cref="PlotViewModel"/>, the C# half of the Qt PlotViewModel:
    /// series from the core on one time axis, colours, ranges and overrides, the view mode, the zoom history, the
    /// readout) and the control that draws it and takes the gestures (<see cref="PlotChart"/>, the Qt TmChart with the
    /// gestures PlotWidget adds). The drawing cases are TestTmChart's, read from rendered frames.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class PlotTests
    {
        private static string Prn => Repo.Recording("prn_testfile, sample.ch10");
        private static string StepCal => Repo.Recording("step_cal, example.ch10");

        public PlotTests() => NativeCore.EnsureLoaded();

        private static double[] Range(int n, Func<int, double> f) => Enumerable.Range(0, n).Select(f).ToArray();

        // ---- colours ------------------------------------------------------------------------------------------------

        public static IEnumerable<object[]> QtShades()
        {
            // What QColor printed for PlotViewModel::shadeOfColor (Qt 6.12.0), primary by primary, levels 0 to 6.
            string table = @"
106,13,173 0 106,13,173|106,13,173 1 135,60,193|106,13,173 2 171,116,213|106,13,173 3 211,182,233|106,13,173 4 236,213,253|106,13,173 5 238,215,255|106,13,173 6 238,215,255
67,97,238 0 67,97,238|67,97,238 1 132,155,255|67,97,238 2 192,204,255|67,97,238 3 215,222,255|67,97,238 4 215,222,255|67,97,238 5 215,222,255|67,97,238 6 215,222,255
46,184,92 0 46,184,92|46,184,92 1 99,204,134|46,184,92 2 162,224,182|46,184,92 3 206,244,218|46,184,92 4 215,255,228|46,184,92 5 215,255,228|46,184,92 6 215,255,228
204,0,0 0 204,0,0|204,0,0 1 224,53,53|204,0,0 2 244,115,115|204,0,0 3 255,180,180|204,0,0 4 255,215,215|204,0,0 5 255,215,215|204,0,0 6 255,215,215
255,140,0 0 255,140,0|255,140,0 1 255,164,60|255,140,0 2 255,192,120|255,140,0 3 255,220,180|255,140,0 4 255,236,215|255,140,0 5 255,236,215|255,140,0 6 255,236,215
230,200,30 0 230,200,30|230,200,30 1 250,226,91|230,200,30 2 255,240,153|230,200,30 3 255,249,213|230,200,30 4 255,249,215|230,200,30 5 255,249,215|230,200,30 6 255,249,215";
            foreach (string row in table.Split(new[] { '|', '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries))
            {
                string[] parts = row.Trim().Split(' ');
                yield return new object[] { parts[0], int.Parse(parts[1]), parts[2] };
            }
        }

        private static Color Rgb(string text)
        {
            byte[] c = text.Split(',').Select(byte.Parse).ToArray();
            return Color.FromRgb(c[0], c[1], c[2]);
        }

        [Theory]
        [MemberData(nameof(QtShades))]
        public void AShadeIsTheColourQtDraws(string primary, int level, string expected)
        {
            Assert.Equal(Rgb(expected), PlotColors.Shade(Rgb(primary), level));
        }

        public static IEnumerable<object[]> QtShadesAroundTheWheel()
        {
            // The same function, as QColor computed it, for colours the primaries do not reach: every sector of the hue
            // wheel and both sides of an edge, a negative raw hue (magenta), a grey (no hue at all) and near-greys.
            const string table = "255,0,128 1 255,60,161|255,0,128 2 255,120,190|255,0,128 3 255,180,219|200,30,90 1 220,85,134|200,30,90 2 240,149,182|200,30,90 3 255,215,230|128,255,0 1 161,255,60|128,255,0 2 190,255,120|128,255,0 3 219,255,180|90,200,20 1 132,220,73|90,200,20 2 178,240,136|90,200,20 3 225,255,205|0,255,255 1 60,255,255|0,255,255 2 120,255,255|0,255,255 3 180,255,255|20,120,200 1 73,156,220|20,120,200 2 136,195,240|20,120,200 3 205,233,255|255,255,0 1 255,255,60|255,255,0 2 255,255,120|255,255,0 3 255,255,180|140,0,255 1 164,60,255|140,0,255 2 192,120,255|140,0,255 3 220,180,255|255,0,255 1 255,60,255|255,0,255 2 255,120,255|255,0,255 3 255,180,255|1,2,3 1 13,18,23|1,2,3 2 35,39,43|1,2,3 3 53,58,63|128,128,128 1 148,148,148|128,128,128 2 168,168,168|128,128,128 3 188,188,188|250,251,249 1 235,255,215|250,251,249 2 235,255,215|250,251,249 3 235,255,215|17,200,201 1 71,221,221|17,200,201 2 134,241,241|17,200,201 3 202,255,255|201,17,200 1 221,71,221|201,17,200 2 241,134,241|201,17,200 3 255,202,255";
            foreach (string row in table.Split('|'))
            {
                string[] parts = row.Split(' ');
                yield return new object[] { parts[0], int.Parse(parts[1]), parts[2] };
            }
        }

        [Theory]
        [MemberData(nameof(QtShadesAroundTheWheel))]
        public void AShadeOfAnyColourIsTheColourQtComputes(string color, int level, string expected)
        {
            Assert.Equal(Rgb(expected), PlotColors.Shade(Rgb(color), level));
        }

        [Fact]
        public void TheShadesStartFromTheQtPrimaries()
        {
            Assert.Equal(new[] { "106,13,173", "67,97,238", "46,184,92" }, PlotColors.FrameSyncLock.Select(c => $"{c.R},{c.G},{c.B}"));
            Assert.Equal(new[] { "204,0,0", "255,140,0", "230,200,30" }, PlotColors.Snr.Select(c => $"{c.R},{c.G},{c.B}"));
        }

        [AvaloniaFact]
        public void EachStreamAndEachReceiverTakesItsColour()
        {
            // Four lock streams: the three primaries, then the first again a shade lighter; a stream's missed frames share
            // its lock colour. SNR: each receiver its primary (cycling, a shade lighter past the third), each further
            // channel a shade lighter again.
            double[] x = { 0, 1 }, y = { 1, 2 };
            var plot = new PlotViewModel();
            plot.SetSeries(new[]
            {
                PlotFixtures.Lock(0, "a", 10, 0, x, y), PlotFixtures.Missed(1, "a", 10, 0, x, y),
                PlotFixtures.Lock(2, "b", 11, 1, x, y), PlotFixtures.Lock(3, "c", 12, 2, x, y), PlotFixtures.Lock(4, "d", 13, 3, x, y),
                PlotFixtures.Snr(5, "r1c0", 20, 4, 1, 0, x, y), PlotFixtures.Snr(6, "r1c1", 20, 4, 1, 1, x, y),
                PlotFixtures.Snr(7, "r2c0", 20, 4, 2, 0, x, y), PlotFixtures.Snr(8, "r3c0", 20, 4, 3, 0, x, y),
                PlotFixtures.Snr(9, "r4c0", 20, 4, 4, 0, x, y), PlotFixtures.Snr(10, "r4c2", 20, 4, 4, 2, x, y),
            });
            Color ColorOf(int id) => plot.Series.Single(s => s.Id == id).Color;
            Assert.Equal(Rgb("106,13,173"), ColorOf(0));
            Assert.Equal(ColorOf(0), ColorOf(1));
            Assert.Equal(Rgb("67,97,238"), ColorOf(2));
            Assert.Equal(Rgb("46,184,92"), ColorOf(3));
            Assert.Equal(Rgb("135,60,193"), ColorOf(4));
            Assert.Equal(Rgb("204,0,0"), ColorOf(5));
            Assert.Equal(Rgb("224,53,53"), ColorOf(6));
            Assert.Equal(Rgb("255,140,0"), ColorOf(7));
            Assert.Equal(Rgb("230,200,30"), ColorOf(8));
            Assert.Equal(Rgb("224,53,53"), ColorOf(9));     // receiver 4: red again, a shade lighter
            Assert.Equal(Rgb("255,180,180"), ColorOf(10));  // and its third channel three shades
        }

        // ---- the plot's state, from runs --------------------------------------------------------------------------

        private static RunOutcome Run(string path, int sourceId, params StreamConfig[] streams)
        {
            Ch10File file = Ch10File.Open(path);
            return StreamsRun.Run(new RunRequest(path, file.DefaultTimeChannel, streams) { SourceId = sourceId, AppRoot = Repo.Root }, null);
        }

        private static StreamConfig PrnStream(int channel) => FirstChannelRun.DefaultStream(new ChannelInfo(channel, "CH " + channel, 0));

        private static StreamConfig StepCalSnr() => new()
        {
            PcmChannelId = 26, Label = "RCVR", Mode = StreamMode.ReceiverChannelInfo,
            SyncPattern = ReceiverSnrDefaults.Pattern, SyncMask = "FFFFFFFF", BitsInMinorFrame = ReceiverSnrDefaults.BitsInMinorFrame,
        };

        [AvaloniaFact]
        public void AStreamsSeriesComeFromTheCoreOnOneTimeAxis()
        {
            using RunOutcome outcome = Run(Prn, 0, PrnStream(13));
            using var plot = new PlotViewModel();
            plot.AddStream(outcome.Results[0]);

            // Lock percentage, then accumulated missed frames; only the metric of the view is shown.
            Assert.Equal(new[] { MetricType.FrameSyncLock, MetricType.AccumulatedMissedFrames }, plot.Series.Select(s => s.Metric));
            Assert.Equal(new[] { true, false }, plot.Series.Select(s => s.Visible));
            Assert.All(plot.Series, s => Assert.Equal("CH 13", s.Name));
            Assert.Same(plot.Series[0].X, plot.Series[1].X);   // one elapsed array per stream
            Assert.True(plot.Series[0].SortedX);               // so the chart walks only the samples in view
            Assert.Equal(116, plot.Series[0].X.Length);
            Assert.Equal(0.0, plot.Series[0].X[0]);
            Assert.Equal(outcome.Streams[0].LockPercent, plot.Series[0].Y);
            Assert.Equal(outcome.Streams[0].TimesSec[^1] - outcome.Streams[0].TimesSec[0], plot.XMax, 9);
            Assert.Equal((0.0, plot.XMax), (plot.XViewMin, plot.XViewMax));
            Assert.True(plot.LeftAxisVisible);
            Assert.False(plot.RightAxisVisible);
            Assert.Equal((0.0, 100.0), (plot.LeftMin, plot.LeftMax));
            Assert.Equal(PlotViewModel.LockAxisLabel, plot.LeftAxisLabel);
            Assert.Equal(PlotViewModel.DefaultTitle, plot.Title);

            // The X axis reads as the recording's clock, and the ticks are snapped to it.
            double first = outcome.Streams[0].TimesSec[0];
            int day = DateTime.UnixEpoch.AddSeconds(first).DayOfYear;   // IRIG time counts from 1970
            Assert.Equal(first % 86400.0, plot.TimeOrigin, 6);               // the clock time of x = 0
            Assert.Equal(ClockTime.Format(day, plot.TimeOrigin, 30.0), plot.FormatTime(30.0));
            Assert.Matches(@"^\d{3}:\d{2}:\d{2}:\d{2}$", plot.FormatTime(0.0));
        }

        [AvaloniaFact]
        public void StreamsKeepTheirJobOrderAndTheViewDecidesWhichMetricIsShown()
        {
            using RunOutcome outcome = Run(Prn, 0, PrnStream(13) with { Label = "first" }, PrnStream(13) with { Label = "second" });
            using var plot = new PlotViewModel();
            plot.SetLockAxisView(LockAxisView.MissedFrames);
            plot.AddStream(outcome.Results[1]);   // the second job first
            plot.AddStream(outcome.Results[0]);
            Assert.Equal(new[] { "first", "first", "second", "second" }, plot.Series.Select(s => s.StreamLabel));
            Assert.Equal(new[] { false, true, false, true }, plot.Series.Select(s => s.Visible));   // missed frames shown
        }

        [AvaloniaFact]
        public void AStreamProcessedAgainReplacesItsSeries()
        {
            using RunOutcome first = Run(Prn, 0, PrnStream(13));
            using RunOutcome again = Run(Prn, 0, PrnStream(13));
            using var plot = new PlotViewModel();
            plot.AddStream(first.Results[0]);
            int[] ids = plot.Series.Select(s => s.Id).ToArray();
            plot.AddStream(again.Results[0]);
            Assert.Equal(2, plot.Series.Count);
            Assert.DoesNotContain(plot.Series, s => ids.Contains(s.Id));
        }

        [AvaloniaFact]
        public void ASourceThatStartsEarlierMovesTheSharedBaseAndALateOneIsReported()
        {
            using RunOutcome prn = Run(Prn, 0, PrnStream(13));
            using RunOutcome cal = Run(StepCal, 1, StepCalSnr());
            double prnStart = prn.Streams[0].TimesSec[0], calStart = cal.Streams[0].TimesSec[0];
            (RunOutcome early, RunOutcome late) = prnStart < calStart ? (prn, cal) : (cal, prn);

            using var plot = new PlotViewModel();
            var reported = new List<int>();
            plot.NonOverlappingSource += (_, id) => reported.Add(id);
            plot.AddStream(late.Results[0]);
            plot.AddStream(early.Results[0]);

            // The later source now starts as far along as it started after the earlier one; the earlier one at zero.
            double gap = Math.Abs(prnStart - calStart);
            PlotSeries lateSeries = plot.Series.First(s => s.SourceId == late.Streams[0].SourceId);
            PlotSeries earlySeries = plot.Series.First(s => s.SourceId == early.Streams[0].SourceId);
            Assert.Equal(0.0, earlySeries.X[0], 9);
            Assert.Equal(gap, lateSeries.X[0], 6);
            Assert.True(lateSeries.Version > 0, "the moved series were not marked as moved");
            Assert.Equal(new[] { early.Streams[0].SourceId }, reported);   // the two recordings do not overlap
            Assert.True(plot.LeftAxisVisible && plot.RightAxisVisible);
        }

        // ---- the plot's state, by hand ----------------------------------------------------------------------------

        [AvaloniaFact]
        public void TheSnrAxisIsRoundedToFiveDecibelsAndAnOverrideNeverInvertsIt()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(new[] { PlotFixtures.Snr(0, "s", 1, 0, 1, 0, new[] { 0.0, 1.0 }, new[] { 12.3, 47.1 }) });
            Assert.Equal((10.0, 50.0), (plot.RightMin, plot.RightMax));
            plot.SetSeries(new[] { PlotFixtures.Snr(0, "s", 1, 0, 1, 0, new[] { 0.0, 1.0 }, new[] { -3.0, 4.0 }) });
            Assert.Equal((0.0, 5.0), (plot.RightMin, plot.RightMax));             // not below zero
            plot.SetSeries(new[] { PlotFixtures.Snr(0, "s", 1, 0, 1, 0, new[] { 0.0, 1.0 }, new[] { 20.0, 20.0 }) });
            Assert.Equal((20.0, 25.0), (plot.RightMin, plot.RightMax));           // a flat series still has a span

            plot.SetRightMinOverride(30.0);
            plot.SetRightMaxOverride(10.0);
            Assert.Equal((30.0, 31.0), (plot.RightMin, plot.RightMax));           // the minimum wins; the maximum yields
            Assert.True(plot.HasRangeOverride);
            plot.ResetYRange();
            Assert.Equal((20.0, 25.0), (plot.RightMin, plot.RightMax));
            Assert.False(plot.HasRangeOverride);

            plot.SetSeries(Array.Empty<PlotSeries>());
            Assert.Equal((0.0, 1.0), (plot.RightMin, plot.RightMax));

            // Only SNR series that are shown set it: not a lock series, not a hidden one.
            plot.SetSeries(new[]
            {
                PlotFixtures.Snr(0, "s", 1, 0, 1, 0, new[] { 0.0, 1.0 }, new[] { 12.0, 18.0 }),
                PlotFixtures.Lock(1, "l", 2, 1, new[] { 0.0, 1.0 }, new[] { 90.0, 100.0 }),
            });
            Assert.Equal((10.0, 20.0), (plot.RightMin, plot.RightMax));
            plot.SetSeriesVisible(0, false);
            Assert.Equal((0.0, 1.0), (plot.RightMin, plot.RightMax));
        }

        [AvaloniaFact]
        public void TheLeftAxisIsLockPercentOrMissedFramesAndTheViewCarriesTheStreamsShown()
        {
            double[] x = { 0, 1 };
            var plot = new PlotViewModel();
            plot.SetSeries(new[]
            {
                PlotFixtures.Lock(0, "a", 1, 0, x, new[] { 90.0, 95.0 }), PlotFixtures.Missed(1, "a", 1, 0, x, new[] { 0.0, 40.0 }),
                PlotFixtures.Lock(2, "b", 2, 1, x, new[] { 80.0, 85.0 }), PlotFixtures.Missed(3, "b", 2, 1, x, new[] { 0.0, 60.0 }),
            });
            plot.SetSeries(plot.Series.Select(s => { s.Visible = s.Metric == MetricType.FrameSyncLock; return s; }).ToArray());
            plot.SetSeriesVisible(2, false);   // stream b hidden
            plot.SetLeftMaxOverride(99.0);
            Assert.Equal(99.0, plot.LeftMax);

            plot.SetLockAxisView(LockAxisView.MissedFrames);
            Assert.Equal(PlotViewModel.MissedFramesAxisLabel, plot.LeftAxisLabel);
            Assert.Equal(new[] { false, true, false, false }, plot.Series.Select(s => s.Visible));   // b stays hidden
            Assert.Equal(40.0 * 1.05, plot.LeftMax, 9);    // the visible counts' maximum, plus 5 %; the override was dropped
            plot.SetSeriesVisible(1, false);
            Assert.Equal(10.0 * 1.05, plot.LeftMax, 9);    // nothing shown: a scale of 10 still

            plot.SetLeftMinOverride(50.0);
            Assert.Equal(51.0, plot.LeftMax);              // never closer than one to the minimum
            plot.SetLockAxisView(LockAxisView.LockPercent);
            Assert.Equal(PlotViewModel.LockAxisLabel, plot.LeftAxisLabel);
            Assert.Equal(new[] { false, false, false, false }, plot.Series.Select(s => s.Visible));   // a's missed frames were hidden
        }

        [AvaloniaFact]
        public void AnAxisIsDrawnOnlyWhenSomethingIsOnIt()
        {
            double[] x = { 0, 1 }, y = { 1, 2 };
            var plot = new PlotViewModel();
            Assert.True(plot.LeftAxisVisible);      // an empty chart keeps the left axis
            Assert.False(plot.RightAxisVisible);
            plot.SetSeries(new[] { PlotFixtures.Snr(0, "s", 1, 0, 1, 0, x, y) });
            Assert.False(plot.LeftAxisVisible);
            Assert.True(plot.RightAxisVisible);
            plot.SetSeries(new[] { PlotFixtures.Snr(0, "s", 1, 0, 1, 0, x, y), PlotFixtures.Lock(1, "l", 2, 1, x, y) });
            Assert.True(plot.LeftAxisVisible && plot.RightAxisVisible);
            plot.SetSeriesVisible(0, false);
            Assert.True(plot.LeftAxisVisible);
            Assert.False(plot.RightAxisVisible);    // hiding the last SNR series gives its axis back
        }

        private static PlotViewModel HundredSeconds()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(new[] { PlotFixtures.Lock(0, "s", 1, 0, Range(101, i => i), Range(101, _ => 50.0)) });
            return plot;
        }

        [AvaloniaFact]
        public void AGestureMovesTheWindowWithinTheDataAndIsNotRemembered()
        {
            PlotViewModel plot = HundredSeconds();
            plot.SetXViewByUser(-10.0, 20.0);
            Assert.Equal((0.0, 30.0), (plot.XViewMin, plot.XViewMax));    // moved inside, its width kept
            plot.SetXViewByUser(90.0, 120.0);
            Assert.Equal((70.0, 100.0), (plot.XViewMin, plot.XViewMax));
            plot.SetXViewByUser(-50.0, 150.0);
            Assert.Equal((0.0, 100.0), (plot.XViewMin, plot.XViewMax));   // wider than the data: all of it
            plot.SetXViewByUser(40.0, 60.0);
            Assert.Equal((40.0, 60.0), (plot.XViewMin, plot.XViewMax));
            Assert.False(plot.CanZoomBack);
        }

        [AvaloniaFact]
        public void ABandZoomIsRememberedAndZoomBackReturnsThroughEachWindow()
        {
            PlotViewModel plot = HundredSeconds();
            plot.ApplyBandZoom(60.0, 20.0);                                // dragged right to left
            Assert.Equal((20.0, 60.0), (plot.XViewMin, plot.XViewMax));
            plot.ApplyBandZoom(30.0, 30.0 + 1e-7);                         // a click, not a drag
            Assert.Equal((20.0, 60.0), (plot.XViewMin, plot.XViewMax));
            plot.ApplyBandZoom(-5.0, 30.0);                                // kept inside the data
            Assert.Equal((0.0, 30.0), (plot.XViewMin, plot.XViewMax));
            Assert.Equal(2, plot.ZoomDepth);
            plot.ZoomBack();
            Assert.Equal((20.0, 60.0), (plot.XViewMin, plot.XViewMax));
            plot.ZoomBack();
            Assert.Equal((0.0, 100.0), (plot.XViewMin, plot.XViewMax));
            Assert.False(plot.CanZoomBack);
            plot.ZoomBack();                                               // nothing left: nothing happens
            Assert.Equal((0.0, 100.0), (plot.XViewMin, plot.XViewMax));

            // At most 32 windows are remembered; the oldest go first.
            for (int i = 0; i < 40; i++) plot.PushXZoom(i, i + 10);
            Assert.Equal(PlotViewModel.MaxZoomDepth, plot.ZoomDepth);
            for (int i = 0; i < 32; i++) plot.ZoomBack();
            Assert.Equal((7.0, 17.0), (plot.XViewMin, plot.XViewMax));     // the view before the 9th push

            plot.PushXZoom(10, 20);
            plot.ResetXRange();
            Assert.Equal((0.0, 100.0), (plot.XViewMin, plot.XViewMax));
            Assert.False(plot.CanZoomBack);                                // the history goes with a reset
            plot.PushXZoom(10, 20);
            plot.SetSeries(plot.Series.ToArray());
            Assert.False(plot.CanZoomBack);                                // and with new series
        }

        [AvaloniaFact]
        public void TheReadoutIsTheNearestValueNearTheCursorOrThePinnedSeriesAnywhere()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(new[]
            {
                PlotFixtures.Lock(0, "Lock A", 1, 0, new[] { 0.0, 10.0, 20.0 }, new[] { 90.0, 95.5, 99.0 }),
                PlotFixtures.Snr(1, "SNR 1", 2, 1, 1, 0, new[] { 0.0, 12.0, 24.0 }, new[] { 30.0, 31.25, 32.0 }),
            });
            double Pixel(double x) => x * 10;   // 10 px a second
            string time10 = plot.FormatTime(10.0), time12 = plot.FormatTime(12.0);

            Assert.Equal($"Lock A\n{time10}\n95.50%", plot.Readout(10.4, 104, Pixel));
            Assert.Equal($"SNR 1\n{time12}\n31.25 dB", plot.Readout(11.7, 117, Pixel));
            Assert.Null(plot.Readout(5.0, 50, Pixel));                     // 5 s from any sample: 50 px, more than 10

            plot.ReadoutSeriesId = 1;
            Assert.Equal($"SNR 1  (pinned)\n{plot.FormatTime(0.0)}\n30.00 dB", plot.Readout(5.0, 50, Pixel));
            Assert.Equal($"SNR 1  (pinned)\n{time12}\n31.25 dB", plot.Readout(10.4, 104, Pixel));   // not the nearer lock value

            plot.ReadoutSeriesId = null;
            plot.SetSeriesVisible(0, false);
            Assert.Equal($"SNR 1\n{time12}\n31.25 dB", plot.Readout(11.0, 110, Pixel));             // a hidden series says nothing
            plot.SetSeries(new[] { PlotFixtures.Missed(0, "M", 1, 0, new[] { 0.0, 1.0 }, new[] { 0.0, 3.0 }) });
            Assert.Equal($"M\n{plot.FormatTime(1.0)}\n3.00 frames", plot.Readout(1.0, 10, Pixel));
            Assert.Null(new PlotViewModel().Readout(0, 0, Pixel));

            // Two series equally near: the first listed is the one read out. A series with no name says nothing.
            plot.SetSeries(new[]
            {
                PlotFixtures.Lock(0, "first", 1, 0, new[] { 0.0, 10.0 }, new[] { 1.0, 2.0 }),
                PlotFixtures.Lock(1, "second", 2, 1, new[] { 0.0, 10.0 }, new[] { 3.0, 4.0 }),
            });
            Assert.StartsWith("first\n", plot.Readout(10.0, 100, Pixel));
            plot.SetSeries(new[] { PlotFixtures.Lock(0, "", 1, 0, new[] { 0.0, 10.0 }, new[] { 1.0, 2.0 }) });
            Assert.Null(plot.Readout(10.0, 100, Pixel));
        }

        [AvaloniaFact]
        public void ClearingForgetsTheRecording()
        {
            PlotViewModel plot = HundredSeconds();
            plot.Title = "flight";
            plot.ApplyBandZoom(10, 20);
            plot.SetLeftMaxOverride(80);
            plot.ReadoutSeriesId = 0;
            plot.SetLockAxisView(LockAxisView.MissedFrames);
            plot.Clear();
            Assert.False(plot.HasData);
            Assert.Empty(plot.Series);
            Assert.Equal(PlotViewModel.DefaultTitle, plot.Title);
            Assert.False(plot.CanZoomBack);
            Assert.False(plot.HasRangeOverride);
            Assert.Null(plot.ReadoutSeriesId);
            Assert.Equal(LockAxisView.LockPercent, plot.LockAxisView);
            Assert.Equal((0.0, 0.0), (plot.XViewMin, plot.XViewMax));
        }

        // ---- the chart --------------------------------------------------------------------------------------------

        private sealed record Shown(PlotChart Chart, Window Window)
        {
            /// <summary>A point of the chart, in the window's coordinates (where the mouse is driven).</summary>
            public Point At(double x, double y) => Frames.InWindow(Chart, Window).TopLeft + new Point(x, y);

            public Rect Area
            {
                get
                {
                    Rect c = Frames.InWindow(Chart, Window), a = Chart.PlotArea;
                    return new Rect(c.X + a.X, c.Y + a.Y, a.Width, a.Height);
                }
            }

            public ILockedFramebuffer Frame() => Frames.Capture(Window).Lock();
        }

        private static Shown Show(PlotViewModel? plot, double width = 600, double height = 400)
        {
            var chart = new PlotChart { Width = width, Height = height, Plot = plot };
            var window = new Window { Content = chart, SizeToContent = SizeToContent.WidthAndHeight };
            TextOptions.SetTextRenderingMode(window, TextRenderingMode.Antialias);
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return new Shown(chart, window);
        }

        /// <summary>Pixels of the series' red in a part of the plot area, as fractions of its width and height.</summary>
        private static int RedIn(Shown s, double x0, double x1, double y0, double y1)
        {
            Rect a = s.Area;
            var part = new Rect(a.X + a.Width * x0, a.Y + a.Height * y0, a.Width * (x1 - x0), a.Height * (y1 - y0));
            using ILockedFramebuffer fb = s.Frame();
            int n = 0;
            for (int y = (int)part.Top; y < (int)part.Bottom; y++)
                for (int x = (int)part.Left; x < (int)part.Right; x++)
                {
                    uint p = Frames.Pixel(fb, x, y);
                    int r = (int)(p >> 16) & 0xFF, g = (int)(p >> 8) & 0xFF, b = (int)p & 0xFF;
                    if (r > g + 40 && r > b + 40) n++;
                }
            return n;
        }

        /// <summary>Red as the band's edge draws it (alpha 190), not as its fill alone does (alpha 90).</summary>
        private static int StrongRedIn(Shown s, double x0, double x1, double y0, double y1)
        {
            Rect a = s.Area;
            var part = new Rect(a.X + a.Width * x0, a.Y + a.Height * y0, a.Width * (x1 - x0), a.Height * (y1 - y0));
            using ILockedFramebuffer fb = s.Frame();
            int n = 0;
            for (int y = (int)part.Top; y < (int)part.Bottom; y++)
                for (int x = (int)part.Left; x < (int)part.Right; x++)
                {
                    uint p = Frames.Pixel(fb, x, y);
                    int r = (int)(p >> 16) & 0xFF, g = (int)(p >> 8) & 0xFF;
                    if (r - g > 110) n++;
                }
            return n;
        }

        [AvaloniaFact]
        public void AnEmptyChartIsAFrameWithNothingInsideAndIgnoresTheWheel()
        {
            var plot = new PlotViewModel();
            Shown s = Show(plot);
            using (ILockedFramebuffer fb = s.Frame())
            {
                Rect inside = s.Area.Deflate(2);
                Assert.Equal(0, Frames.CountDifferent(fb, inside, Frames.Pixel(fb, (int)inside.Center.X, (int)inside.Center.Y)));
            }
            s.Window.MouseWheel(s.At(300, 200), new Vector(0, 1), RawInputModifiers.None);
            s.Window.MouseDown(s.At(300, 200), MouseButton.Left, RawInputModifiers.None);
            s.Window.MouseMove(s.At(350, 200), RawInputModifiers.None);
            s.Window.MouseUp(s.At(350, 200), MouseButton.Left, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal((0.0, 0.0), (plot.XViewMin, plot.XViewMax));
            Assert.False(s.Chart.CrosshairVisible);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void AHiddenAxisGivesItsMarginBackAndTheTransformsFollowThePlotArea()
        {
            double[] x = { 0, 100 }, y = { 10, 20 };
            var plot = new PlotViewModel();
            plot.SetSeries(new[] { PlotFixtures.Lock(0, "l", 1, 0, x, y) });
            Shown s = Show(plot);
            Rect lockOnly = s.Chart.PlotArea;
            Assert.Equal(600 - PlotChart.OuterMargin, lockOnly.Right, 6);      // no right axis: only the outer margin
            Assert.True(lockOnly.Left > 40, "the left axis reserves its labels and title");

            plot.SetSeries(new[] { PlotFixtures.Snr(0, "s", 1, 0, 1, 0, x, y) });
            Dispatcher.UIThread.RunJobs();
            Rect snrOnly = s.Chart.PlotArea;
            Assert.Equal(PlotChart.OuterMargin, snrOnly.Left, 6);
            Assert.Equal(600 - lockOnly.Left, snrOnly.Right, 6);              // the same gutter, on the other side

            Assert.Equal(snrOnly.Left, s.Chart.XToPixel(plot.XViewMin), 6);
            Assert.Equal(snrOnly.Right, s.Chart.XToPixel(plot.XViewMax), 6);
            Assert.Equal(snrOnly.Bottom, s.Chart.RightToPixel(plot.RightMin), 6);
            Assert.Equal(snrOnly.Top, s.Chart.RightToPixel(plot.RightMax), 6);
            Assert.Equal(snrOnly.Bottom, s.Chart.LeftToPixel(0), 6);
            Assert.Equal(snrOnly.Top, s.Chart.LeftToPixel(100), 6);
            Assert.Equal(25.0, s.Chart.PixelToX(s.Chart.XToPixel(25.0)), 9);

            // Above the plot: the outer margin, the headroom kept for plan step 4.4's chip bar, the title and its gap.
            var text = new FormattedText("Xg", System.Globalization.CultureInfo.CurrentCulture, FlowDirection.LeftToRight,
                new Typeface(Avalonia.Controls.Documents.TextElement.GetFontFamily(s.Chart)), PlotChart.FontSize, null);
            Assert.Equal(PlotChart.OuterMargin + PlotChart.TopInset + Math.Ceiling(text.Height) + PlotChart.AxisLabelGap, snrOnly.Top, 6);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheWheelZoomsAboutTheCursor()
        {
            PlotViewModel plot = HundredSeconds();
            Shown s = Show(plot);
            Rect a = s.Chart.PlotArea;
            double px = a.Left + a.Width * 0.3;
            double under = s.Chart.PixelToX(px);
            s.Window.MouseWheel(s.At(px, a.Center.Y), new Vector(0, 1), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(100.0 * PlotChart.WheelZoomStep, plot.XViewMax - plot.XViewMin, 6);
            Assert.Equal(under, s.Chart.PixelToX(px), 6);                      // the time under the cursor stays put
            Assert.False(plot.CanZoomBack);                                     // a continuous gesture is not remembered
            s.Window.MouseWheel(s.At(px, a.Center.Y), new Vector(0, -1), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(100.0, plot.XViewMax - plot.XViewMin, 6);              // and out again
            for (int i = 0; i < 200; i++) s.Window.MouseWheel(s.At(px, a.Center.Y), new Vector(0, 1), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.True(plot.XViewMax - plot.XViewMin >= PlotViewModel.MinZoomSpanSeconds, "the wheel collapsed the view");
            s.Window.Close();
        }

        [AvaloniaFact]
        public void ADragPansKeepingTheTimeGrabbedUnderTheCursor()
        {
            PlotViewModel plot = HundredSeconds();
            plot.SetXViewByUser(40, 60);
            Shown s = Show(plot);
            Rect a = s.Chart.PlotArea;
            double from = a.Left + a.Width * 0.5, to = a.Left + a.Width * 0.25;
            double grabbed = s.Chart.PixelToX(from);
            s.Window.MouseDown(s.At(from, a.Center.Y), MouseButton.Left, RawInputModifiers.None);
            s.Window.MouseMove(s.At(to, a.Center.Y), RawInputModifiers.LeftMouseButton);
            s.Window.MouseUp(s.At(to, a.Center.Y), MouseButton.Left, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(20.0, plot.XViewMax - plot.XViewMin, 6);
            Assert.Equal(grabbed, s.Chart.PixelToX(to), 6);
            Assert.Equal(45.0, plot.XViewMin, 6);
            Assert.False(plot.CanZoomBack);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void ABandDraggedWithCtrlOrTheMiddleButtonZoomsIntoIt()
        {
            PlotViewModel plot = HundredSeconds();
            Shown s = Show(plot);
            Rect a = s.Chart.PlotArea;
            double p20 = s.Chart.XToPixel(20), p60 = s.Chart.XToPixel(60);

            s.Window.MouseMove(s.At(p60, a.Center.Y), RawInputModifiers.None);
            Assert.True(s.Chart.CrosshairVisible);
            s.Window.MouseDown(s.At(p60, a.Center.Y), MouseButton.Left, RawInputModifiers.Control);
            s.Window.MouseMove(s.At(p20, a.Center.Y), RawInputModifiers.LeftMouseButton | RawInputModifiers.Control);
            Dispatcher.UIThread.RunJobs();
            Assert.True(s.Chart.ZoomBandVisible);
            Assert.False(s.Chart.CrosshairVisible);                           // no crosshair under the band
            Assert.Equal((0.0, 100.0), (plot.XViewMin, plot.XViewMax));        // nothing moves until it is let go
            s.Window.MouseUp(s.At(p20, a.Center.Y), MouseButton.Left, RawInputModifiers.Control);
            Dispatcher.UIThread.RunJobs();
            Assert.False(s.Chart.ZoomBandVisible);
            Assert.Equal(20.0, plot.XViewMin, 6);
            Assert.Equal(60.0, plot.XViewMax, 6);
            Assert.Equal(1, plot.ZoomDepth);

            double q = s.Chart.XToPixel(30), r = s.Chart.XToPixel(40);
            s.Window.MouseDown(s.At(q, a.Center.Y), MouseButton.Middle, RawInputModifiers.None);
            s.Window.MouseMove(s.At(r, a.Center.Y), RawInputModifiers.MiddleMouseButton);
            s.Window.MouseUp(s.At(r, a.Center.Y), MouseButton.Middle, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(30.0, plot.XViewMin, 6);
            Assert.Equal(40.0, plot.XViewMax, 6);
            Assert.Equal(2, plot.ZoomDepth);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void ADoubleClickRestoresTheWholeSpan()
        {
            PlotViewModel plot = HundredSeconds();
            plot.ApplyBandZoom(20, 30);
            Shown s = Show(plot);
            Point p = s.At(s.Chart.PlotArea.Center.X, s.Chart.PlotArea.Center.Y);
            s.Window.MouseDown(p, MouseButton.Left, RawInputModifiers.None);
            s.Window.MouseUp(p, MouseButton.Left, RawInputModifiers.None);
            s.Window.MouseDown(p, MouseButton.Left, RawInputModifiers.None);
            s.Window.MouseUp(p, MouseButton.Left, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal((0.0, 100.0), (plot.XViewMin, plot.XViewMax));
            Assert.False(plot.CanZoomBack);
            s.Window.Close();
        }

        /// <summary>The window's pixels now, rendering it first (which is what makes the chart paint).</summary>
        private static uint[] Pixels(Shown s)
        {
            using ILockedFramebuffer fb = s.Frame();
            var all = new uint[fb.Size.Width * fb.Size.Height];
            for (int y = 0; y < fb.Size.Height; y++)
                for (int x = 0; x < fb.Size.Width; x++) all[y * fb.Size.Width + x] = Frames.Pixel(fb, x, y);
            return all;
        }

        [AvaloniaFact]
        public void TheCrosshairAndReadoutFollowTheCursorOverTheCachedPlot()
        {
            PlotViewModel plot = HundredSeconds();
            Shown s = Show(plot);
            uint[] before = Pixels(s);
            int renders = s.Chart.DataLayerRenders;
            Rect area = s.Area;
            double px = s.Chart.XToPixel(40);
            s.Window.MouseMove(s.At(px, s.Chart.PlotArea.Center.Y), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.True(s.Chart.CrosshairVisible);
            Assert.Equal(40.0, s.Chart.CrosshairX, 6);
            Assert.Equal($"s\n{plot.FormatTime(40)}\n50.00%", ToolTip.GetTip(s.Chart));
            Assert.True(ToolTip.GetIsOpen(s.Chart));

            // The dashed line is drawn down the cursor's column and nowhere else, over the data layer, which was not
            // painted again. (The readout is closed first: the headless platform draws a popup inside the window.)
            ToolTip.SetIsOpen(s.Chart, false);
            uint[] after = Pixels(s);
            int width = (int)s.Window.ClientSize.Width;
            int column = (int)(Frames.InWindow(s.Chart, s.Window).X + px);
            int inColumn = 0, elsewhere = 0;
            for (int i = 0; i < after.Length; i++)
            {
                if (after[i] == before[i]) continue;
                if (Math.Abs(i % width - column) <= 1) inColumn++;
                else elsewhere++;
            }
            Assert.True(inColumn > area.Height * 0.3, $"the crosshair changed only {inColumn} pixels of its column");
            Assert.Equal(0, elsewhere);
            Assert.Equal(renders, s.Chart.DataLayerRenders);

            // Away from the line's samples there is no readout (unpinned); off the chart, no crosshair either.
            plot.SetSeries(new[] { PlotFixtures.Lock(0, "s", 1, 0, new[] { 0.0, 100.0 }, new[] { 50.0, 50.0 }) });
            Dispatcher.UIThread.RunJobs();
            s.Window.MouseMove(s.At(s.Chart.XToPixel(50), s.Chart.PlotArea.Center.Y), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.False(ToolTip.GetIsOpen(s.Chart));
            Assert.True(s.Chart.CrosshairVisible);

            // A range or a data change does paint the data layer again: a stale picture still looks like a plot.
            Pixels(s);
            renders = s.Chart.DataLayerRenders;
            plot.SetXViewByUser(10, 30);
            Pixels(s);
            Assert.Equal(renders + 1, s.Chart.DataLayerRenders);
            plot.SetSeries(new[] { PlotFixtures.Lock(0, "s", 1, 0, new[] { 0.0, 100.0 }, new[] { 20.0, 20.0 }) });
            Pixels(s);
            Assert.Equal(renders + 2, s.Chart.DataLayerRenders);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void AnExportOmitsTheCursorsOverlays()
        {
            PlotViewModel plot = HundredSeconds();
            Shown s = Show(plot);
            double px = s.Chart.XToPixel(70);
            s.Window.MouseMove(s.At(px, s.Chart.PlotArea.Center.Y), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.True(s.Chart.CrosshairVisible);
            byte[] withCrosshair = Export(s.Chart);
            s.Window.MouseMove(s.At(-50, -50), RawInputModifiers.None);   // off the chart: the crosshair goes
            Dispatcher.UIThread.RunJobs();
            Assert.False(s.Chart.CrosshairVisible);
            Assert.Equal(Export(s.Chart), withCrosshair);
            s.Window.Close();
        }

        /// <summary>What the export path draws, as bytes.</summary>
        private static byte[] Export(PlotChart chart)
        {
            using var bitmap = new RenderTargetBitmap(new PixelSize(600, 400));
            using (DrawingContext dc = bitmap.CreateDrawingContext()) chart.RenderTo(dc, new Size(600, 400));
            var bytes = new byte[600 * 400 * 4];
            IntPtr buffer = System.Runtime.InteropServices.Marshal.AllocHGlobal(bytes.Length);
            try
            {
                bitmap.CopyPixels(new PixelRect(0, 0, 600, 400), buffer, bytes.Length, 600 * 4);
                System.Runtime.InteropServices.Marshal.Copy(buffer, bytes, 0, bytes.Length);
            }
            finally
            {
                System.Runtime.InteropServices.Marshal.FreeHGlobal(buffer);
            }
            Assert.True(bytes.Distinct().Count() > 2, "the export drew nothing");
            return bytes;
        }

        [AvaloniaFact]
        public void ADenseSeriesStillDrawsASingleSampleDropoutAndItsSpread()
        {
            // TestTmChart's two dense cases. 200,000 samples at 100 %, one mid-column at 0: the band reaches the bottom.
            double[] xs = Range(200000, i => i);
            double[] ys = Range(200000, _ => 100.0);
            var plot = new PlotViewModel();
            plot.SetSeries(new[] { PlotFixtures.Snr(0, "d", 1, 0, 1, 0, xs, ys) });
            plot.SetRightMinOverride(0);
            plot.SetRightMaxOverride(100);
            Shown s = Show(plot);
            int perColumn = 200000 / (int)s.Chart.PlotArea.Width;
            Assert.True(perColumn > 10);
            ys[100000 + perColumn / 2] = 0.0;
            plot.SetSeries(new[] { PlotFixtures.Snr(0, "d", 1, 0, 1, 0, xs, ys) });
            plot.SetRightMinOverride(0);
            plot.SetRightMaxOverride(100);
            Dispatcher.UIThread.RunJobs();
            Assert.True(RedIn(s, 0.0, 1.0, 0.75, 1.0) > 0, "a one-sample dropout mid-column was not drawn");
            Assert.True(StrongRedIn(s, 0.0, 1.0, 0.75, 1.0) > 0, "the dropout is only the band's faint fill, not its edge");
            Assert.Equal(0, RedIn(s, 0.0, 0.45, 0.10, 1.0));

            // Alternating 10 and 90: the band reaches both, and the mean runs through the middle.
            plot.SetSeries(new[] { PlotFixtures.Snr(0, "d", 1, 0, 1, 0, xs, Range(200000, i => i % 2 == 1 ? 90.0 : 10.0)) });
            plot.SetRightMinOverride(0);
            plot.SetRightMaxOverride(100);
            Dispatcher.UIThread.RunJobs();
            Assert.True(RedIn(s, 0, 1, 0.05, 0.20) > 0, "nothing near the series' maximum");
            Assert.True(RedIn(s, 0, 1, 0.80, 0.95) > 0, "nothing near the series' minimum");
            Assert.True(RedIn(s, 0, 1, 0.45, 0.55) > 0, "the mean line is missing from the middle");
            // Between its edges and the mean, the band is filled in the series' own colour, faded.
            using (ILockedFramebuffer fb = s.Frame())
            {
                Rect a = s.Area;
                var inside = new Rect(a.X + a.Width * 0.1, a.Y + a.Height * 0.25, a.Width * 0.8, a.Height * 0.15);
                Assert.True(Frames.Near(fb, inside, PlotColors.Snr[0]) > inside.Width * inside.Height * 0.5, "the band is not filled in the series' colour");
            }
            s.Window.Close();
        }

        [AvaloniaFact]
        public void EveryKindOfSeriesIsDrawnFromTheCoresGeometryInItsPlace()
        {
            // TestTmChart's hand-over cases: a series on its own axis's range, a segment's both ends, a band only where the
            // series is, an unordered series in a zoomed view, and a line through a view with no sample inside.
            var plot = new PlotViewModel();
            Shown s = Show(plot);
            void Only(PlotSeries series, double? rightMax = null, (double, double)? view = null)
            {
                plot.SetSeries(new[] { series });
                if (rightMax is double max)
                {
                    plot.SetRightMinOverride(0);
                    plot.SetRightMaxOverride(max);
                }
                if (view is (double lo, double hi)) plot.SetXViewByUser(lo, hi);
                Dispatcher.UIThread.RunJobs();
            }

            // At 10 on a right axis of 0 to 10: at the top.
            Only(PlotFixtures.Snr(0, "r", 1, 0, 1, 0, new[] { 0.0, 100.0 }, new[] { 10.0, 10.0 }), rightMax: 10);
            Assert.True(RedIn(s, 0.1, 0.9, 0.0, 0.05) > 100, "a right-axis series is not at the top of its own range");
            Assert.Equal(0, RedIn(s, 0.1, 0.9, 0.85, 0.95));

            // Corner to corner.
            Only(PlotFixtures.Snr(0, "r", 1, 0, 1, 0, new[] { 0.0, 100.0 }, new[] { 0.0, 100.0 }), rightMax: 100);
            Assert.True(RedIn(s, 0.85, 1.0, 0.0, 0.15) > 0, "a sloped segment does not reach its end");
            Assert.True(RedIn(s, 0.0, 0.15, 0.85, 1.0) > 0, "a sloped segment does not start where it should");

            // Dense and noisy over the right half only.
            double[] xs = new double[20001], ys = new double[20001];
            xs[0] = 0;
            ys[0] = double.NaN;   // the axis starts at 0; the series itself at 50
            for (int i = 1; i <= 20000; i++)
            {
                xs[i] = 50.0 + (i - 1) * 50.0 / 20000;
                ys[i] = i % 2 == 1 ? 95.0 : 5.0;
            }
            Only(PlotFixtures.Snr(0, "r", 1, 0, 1, 0, xs, ys), rightMax: 100);
            Assert.Equal(0, RedIn(s, 0.0, 0.45, 0.0, 1.0));
            Assert.True(RedIn(s, 0.55, 0.95, 0.0, 0.10) > 0, "the band does not reach the series' maximum");
            Assert.True(RedIn(s, 0.55, 0.95, 0.90, 1.0) > 0, "the band does not reach the series' minimum");

            // Times 50 to 99 then 0 to 49, seen from 40 to 60: the line crosses the whole view.
            Only(PlotFixtures.Snr(0, "r", 1, 0, 1, 0, Range(100, i => (i + 50) % 100), Range(100, _ => 50.0)), rightMax: 100, view: (40, 60));
            Assert.True(RedIn(s, 0.05, 0.45, 0.4, 0.6) > 50, "an unordered series is missing from the left of a zoomed view");
            Assert.True(RedIn(s, 0.55, 0.95, 0.4, 0.6) > 50, "an unordered series is missing from the right of a zoomed view");

            // Two samples far apart, drawn whole, then seen from between them: the line is drawn again for the new
            // range, not reused from the last.
            Only(PlotFixtures.Snr(0, "r", 1, 0, 1, 0, new[] { 0.0, 100.0 }, new[] { 0.0, 100.0 }), rightMax: 100);
            Assert.True(RedIn(s, 0.0, 0.1, 0.85, 1.0) > 0);
            plot.SetXViewByUser(45, 55);
            Dispatcher.UIThread.RunJobs();
            Assert.True(RedIn(s, 0.05, 0.95, 0.4, 0.6) > 100, "the line through a view with no sample inside is missing");
            Assert.Equal(0, RedIn(s, 0.0, 0.1, 0.85, 1.0));

            // A hidden series is not drawn.
            plot.SetSeriesVisible(0, false);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(0, RedIn(s, 0.0, 1.0, 0.0, 1.0));
            s.Window.Close();
        }

        [AvaloniaFact]
        public void HundredPercentIsAtTheTopAndTheTitleIsDrawn()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(new[] { PlotFixtures.Lock(0, "flat", 1, 0, new[] { 0.0, 1.0, 2.0 }, new[] { 100.0, 100.0, 100.0 }) });
            plot.Title = "Flight 7";
            Shown s = Show(plot);
            Color purple = PlotColors.FrameSyncLock[0];
            Rect a = s.Area;
            using (ILockedFramebuffer fb = s.Frame())
            {
                Assert.True(Frames.Near(fb, new Rect(a.Left + 10, a.Top - 3, a.Width - 20, 6), purple) > 100);
                Assert.Equal(0, Frames.Near(fb, new Rect(a.Left + 10, a.Bottom - 6, a.Width - 20, 6), purple));
                Assert.True(Frames.Near(fb, new Rect(a.Left, 0, a.Width, a.Top - PlotChart.TopInset), PlotChart.TitleColor) > 20, "no title");
            }
            s.Window.Close();
        }
    }
}
