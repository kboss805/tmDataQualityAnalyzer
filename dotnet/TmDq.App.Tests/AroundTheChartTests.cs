using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.LogicalTree;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.Platform;
using Avalonia.Threading;
using Avalonia.VisualTree;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Logging;
using TmDq.App.Plot;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// Around the chart (plan step 4.4): the legend, the chip bar, the right-click menu and its prompts, the keys, and
    /// Customize View. The Qt TestPlotWidget's and TestPlotCustomizationDialog's cases are ported among them, read from
    /// the controls and, where it is drawing that matters, from rendered frames.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class AroundTheChartTests
    {
        public AroundTheChartTests() => NativeCore.EnsureLoaded();

        private static readonly double[] X3 = { 0.0, 1.0, 2.0 };

        /// <summary>A Frame Sync Lock stream as a run gives it: its lock series shown, its missed frames hidden.</summary>
        private static PlotSeries[] LockStream(int firstId, string label, int order, int sequence, double[]? x = null)
        {
            x ??= X3;
            PlotSeries missed = PlotFixtures.Missed(firstId + 1, label, order, sequence, x, x.Select(v => v).ToArray());
            missed.Visible = false;
            return new[] { PlotFixtures.Lock(firstId, label, order, sequence, x, x.Select(v => 90 + v).ToArray()), missed };
        }

        private static PlotSeries Snr(int id, int order, int sequence, int receiver, int channel, string? name = null) =>
            PlotFixtures.Snr(id, name ?? $"{order} - CH-01 2250.5MHZ AGC {"LRC"[Math.Min(channel, 2)]}_RCVR{receiver}", order, sequence,
                             receiver, channel, X3, new[] { 40.0 + receiver, 45.0, 50.0 });

        private static PlotViewModel LockPlot()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(LockStream(0, "Ch 5", 5, 0));
            return plot;
        }

        private static PlotViewModel SnrOnly(int receivers = 1)
        {
            var plot = new PlotViewModel();
            plot.SetSeries(Enumerable.Range(0, receivers).Select(r => Snr(r, 40, 0, r + 1, 0)));
            return plot;
        }

        // ---- the plot's state for them ------------------------------------------------------------------------------

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

            plot.SetLockAxisView(LockAxisView.MissedFrames);
            Assert.Equal(new[] { 1, 2 }, plot.LegendEntries().Select(r => r.Id));
            plot.SetSeriesVisible(2, false);
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
            // A field that is not a whole number counts as 0, as Qt's toInt gives; the wrong number of fields is 0.
            Assert.Equal(3600, plot.ParseTime("000:01:xx:00"));
            Assert.Equal(60, plot.ParseTime("000:00:01:1.5"));
            Assert.Equal(0, plot.ParseTime("01:02:03"));
            Assert.Equal(0, plot.ParseTime("0:0:0:0:0"));

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

            Assert.False(plot.CanResetView);
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
            Assert.False(new PlotViewModel().CanResetView);
        }

        [Fact]
        public void ARenameOrRecolourReachesAStreamsOtherMetricAndNoOtherStream()
        {
            var plot = new PlotViewModel();
            PlotSeries otherSource = PlotFixtures.Make(6, "Ch 5", MetricType.FrameSyncLock, 5, 2, X3, X3, source: 1);
            plot.SetSeries(LockStream(0, "Ch 5", 5, 0).Concat(LockStream(2, "Ch 6", 6, 1)).Append(Snr(4, 40, 3, 1, 0)).Append(Snr(5, 40, 3, 1, 1)).Append(otherSource));
            plot.RenameSeries(0, "renamed");
            plot.RecolorSeries(0, Colors.Teal);
            Assert.Equal(new[] { "renamed", "renamed", "Ch 6", "Ch 6" }, plot.Series.Take(4).Select(s => s.Name));
            Assert.Equal(new[] { Colors.Teal, Colors.Teal }, plot.Series.Take(2).Select(s => s.Color));
            Assert.NotEqual(Colors.Teal, plot.Series[2].Color);
            Assert.Equal("Ch 5", plot.SeriesById(6)!.Name);   // the same stream of another recording
            Assert.NotEqual(Colors.Teal, plot.SeriesById(6)!.Color);

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
            plot.SetSeriesVisible(0, false);
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
            PlotViewModel plot = SnrOnly(2);
            int series = 0, ranges = 0;
            plot.SeriesChanged += (_, _) => series++;
            plot.RangesChanged += (_, _) => ranges++;
            plot.SetSeriesVisibleQuiet(0, false);
            plot.SetSeriesVisibleQuiet(99, false);
            Assert.Equal((0, 0), (series, ranges));
            Assert.Equal(40.0, plot.RightMin);   // not yet recomputed
            plot.CommitAppearance();
            Assert.Equal((1, 1), (series, ranges));
            Assert.Equal(40.0, plot.RightMin);
            Assert.False(plot.Series[0].Visible);
            Assert.Equal(50.0, plot.RightMax);
        }

        // ---- the commands behind the menu ---------------------------------------------------------------------------

        private sealed class FakePrompts : IPlotPrompts
        {
            public readonly List<string> Asked = new();
            public readonly Queue<object?> Answers = new();
            public (string Title, string Label, string Initial) LastText;
            public (string Title, string Label, double Value, double Min, double Max, int Decimals) LastNumber;
            public (string Start, string Stop) LastWindow;
            public Color LastColor;
            public Func<CustomizeViewModel, bool>? OnCustomize;

            private object? Next() => Answers.Count > 0 ? Answers.Dequeue() : null;

            public Task<string?> AskTextAsync(string title, string label, string initial)
            {
                Asked.Add(title);
                LastText = (title, label, initial);
                return Task.FromResult((string?)Next());
            }

            public Task<double?> AskNumberAsync(string title, string label, double value, double min, double max, int decimals)
            {
                Asked.Add(title);
                LastNumber = (title, label, value, min, max, decimals);
                return Task.FromResult((double?)Next());
            }

            public Task<(string Start, string Stop)?> AskTimeWindowAsync(string start, string stop)
            {
                Asked.Add("Set Time Window");
                LastWindow = (start, stop);
                return Task.FromResult(((string, string)?)Next());
            }

            public Task<Color?> PickColorAsync(Color initial)
            {
                Asked.Add("color");
                LastColor = initial;
                return Task.FromResult((Color?)Next());
            }

            public Task<bool> CustomizeAsync(CustomizeViewModel customize)
            {
                Asked.Add("Customize");
                return Task.FromResult(OnCustomize?.Invoke(customize) ?? false);
            }
        }

        [Fact]
        public async Task TheTitleAndTheTimeWindowAreAskedSeededWithWhatIsShown()
        {
            PlotViewModel plot = LockPlot();
            plot.Title = "now";
            var prompts = new FakePrompts();
            var commands = new PlotCommands(plot, prompts);

            prompts.Answers.Enqueue("Flight 12 - Lock");
            await commands.SetTitleAsync();
            Assert.Equal(("Set Plot Title", "Plot title:", "now"), prompts.LastText);
            Assert.Equal("Flight 12 - Lock", plot.Title);
            await commands.SetTitleAsync();   // cancelled
            Assert.Equal("Flight 12 - Lock", plot.Title);
            prompts.Answers.Enqueue("");
            await commands.SetTitleAsync();   // an empty title is a title
            Assert.Equal("", plot.Title);

            var warnings = new List<string>();
            plot.Warning += (_, w) => warnings.Add(w);
            plot.SetXViewByUser(0.5, 1.5);
            prompts.Answers.Enqueue(("000:00:00:00", "000:00:00:09"));
            await commands.SetTimeWindowAsync();
            Assert.Equal((plot.FormatTime(0.5), plot.FormatTime(1.5)), prompts.LastWindow);
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
            Assert.Equal(new[] { "Stop \"000:00:00:09\" is outside the file time range - clamped to file bounds." }, warnings);
            Assert.Equal(1, plot.ZoomDepth);
            await commands.SetTimeWindowAsync();   // cancelled
            Assert.Equal(1, plot.ZoomDepth);
        }

        [Fact]
        public async Task EachAxisLimitIsAskedWithinBoundsThatCannotInvertTheAxis()
        {
            PlotViewModel plot = new();
            plot.SetSeries(LockStream(0, "Ch 5", 5, 0).Append(Snr(2, 40, 1, 1, 0)));
            var prompts = new FakePrompts();
            var commands = new PlotCommands(plot, prompts);

            prompts.Answers.Enqueue(98.0);
            await commands.SetLeftMinAsync();
            Assert.Equal(("Set Left Y-Axis Minimum", "Minimum lock %:", 0.0, 0.0, 99.0, 0), prompts.LastNumber);
            Assert.Equal(98.0, plot.LeftMin);

            prompts.Answers.Enqueue(99.5);
            await commands.SetLeftMaxAsync();
            Assert.Equal(("Set Left Y-Axis Maximum", "Maximum lock %:", 100.0, 99.0, 100.0, 0), prompts.LastNumber);
            Assert.Equal(99.5, plot.LeftMax);

            plot.ResetYRange();
            plot.SetLockAxisView(LockAxisView.MissedFrames);
            await commands.SetLeftMinAsync();
            Assert.Equal("Minimum accumulated missed frames:", prompts.LastNumber.Label);
            Assert.Equal(plot.LeftMax - 1.0, prompts.LastNumber.Max);
            await commands.SetLeftMaxAsync();
            Assert.Equal(("Set Left Y-Axis Maximum", "Maximum accumulated missed frames:", plot.LeftMax, 1.0, 10000000.0, 0), prompts.LastNumber);
            Assert.False(plot.HasRangeOverride);   // both cancelled

            prompts.Answers.Enqueue(-12.5);
            await commands.SetRightMinAsync();
            Assert.Equal(("Set Right Y-Axis Minimum", "Minimum SNR (dB):", 40.0, -999.0, 49.0, 1), prompts.LastNumber);
            Assert.Equal(-12.5, plot.RightMin);
            prompts.Answers.Enqueue(60.0);
            await commands.SetRightMaxAsync();
            Assert.Equal(("Set Right Y-Axis Maximum", "Maximum SNR (dB):", 50.0, -11.5, 999.0, 1), prompts.LastNumber);
            Assert.Equal(60.0, plot.RightMax);
        }

        [Fact]
        public async Task NothingIsAskedWithoutData()
        {
            var plot = new PlotViewModel();
            var prompts = new FakePrompts { OnCustomize = _ => true };
            var commands = new PlotCommands(plot, prompts);
            await commands.SetTitleAsync();
            await commands.SetTimeWindowAsync();
            await commands.SetLeftMinAsync();
            await commands.SetLeftMaxAsync();
            await commands.SetRightMinAsync();
            await commands.SetRightMaxAsync();
            await commands.CustomizeAsync();
            Assert.Empty(prompts.Asked);
        }

        [Theory]
        [InlineData(5.4, 0, 10, 0, 5.0)]
        [InlineData(5.5, 0, 10, 0, 6.0)]
        [InlineData(-3.25, -10, 10, 1, -3.3)]
        [InlineData(15, 0, 10, 0, 10.0)]
        [InlineData(-1, 0, 10, 0, 0.0)]
        [InlineData(7, 5, 2, 0, 5.0)]
        public void ANumberIsKeptAsASpinBoxKeepsIt(double value, double min, double max, int decimals, double expected) =>
            Assert.Equal(expected, PlotCommands.SpinValue(value, min, max, decimals));

        [Fact]
        public async Task CustomizeViewAppliesOnlyOnOk()
        {
            PlotViewModel plot = LockPlot();
            var prompts = new FakePrompts { OnCustomize = c => { c.LockRows[0].IsChecked = false; return false; } };
            var commands = new PlotCommands(plot, prompts);
            await commands.CustomizeAsync();
            Assert.True(plot.Series[0].Visible);

            prompts.OnCustomize = c => { c.LockRows[0].IsChecked = false; return true; };
            await commands.CustomizeAsync();
            Assert.False(plot.Series[0].Visible);
        }

        // ---- Customize View's state ---------------------------------------------------------------------------------

        private static PlotViewModel Mixed()
        {
            var plot = new PlotViewModel();
            var series = new List<PlotSeries>();
            series.AddRange(LockStream(0, "Ch 6", 6, 1));
            series.AddRange(LockStream(2, "Ch 5", 5, 0));
            // Stream 40: three receivers of three channels, given out of channel order.
            int id = 10;
            foreach (int r in new[] { 1, 2, 3 })
                foreach (int c in new[] { 2, 0, 1 })
                    series.Add(Snr(id++, 40, 2, r, c));
            series.Add(Snr(id++, 41, 3, 1, 0));
            series[2].Visible = false;   // Ch 5's lock series: Ch 5 still counts as shown through nothing else
            plot.SetSeries(series);
            return plot;
        }

        [Fact]
        public void CustomizeViewHasARowPerLockStreamAndTheSnrStreamsByReceiver()
        {
            PlotViewModel plot = Mixed();
            var c = new CustomizeViewModel(plot, new FakePrompts());

            Assert.Equal(new[] { "CH 5", "CH 6" }, c.LockRows.Select(r => r.Label));
            Assert.False(c.LockRows[0].IsChecked);   // neither of Ch 5's series is visible
            Assert.True(c.LockRows[1].IsChecked);
            Assert.Equal(new[] { 2, 3 }, c.LockRows[0].SeriesIds);
            Assert.Equal("Ch 5", c.LockRows[0].Name);
            Assert.Equal(plot.SeriesById(2)!.Color, c.LockRows[0].Color);

            Assert.True(c.HasSnrStreams);
            Assert.Equal(new[] { "CH 40 — 40 - CH-01 2250.5MHZ AGC C_RCVR1", "CH 41 — 41 - CH-01 2250.5MHZ AGC L_RCVR1" },
                         c.SnrStreams.Select(s => s.Label));
            SnrStreamGroup s40 = c.SnrStreams[0];
            Assert.Same(s40, c.SelectedSnrStream);
            // Three receivers: two in the first column, one in the second; each in channel order, collapsed.
            Assert.Equal(new[] { "RCVR 1", "RCVR 2" }, s40.LeftReceivers.Select(r => r.Label));
            Assert.Equal(new[] { "RCVR 3" }, s40.RightReceivers.Select(r => r.Label));
            Assert.Equal(new[] { "L", "R", "C" }, s40.LeftReceivers[0].Channels.Select(ch => ch.Label));
            Assert.All(s40.Receivers, r => Assert.False(r.IsExpanded));
            Assert.Equal(plot.SeriesById(11)!.Color, ((ISolidColorBrush)s40.LeftReceivers[0].Brush).Color);   // channel L of RCVR 1
            Assert.Equal(true, s40.LeftReceivers[0].IsChecked);
            Assert.Equal("CH 40 — 40 - CH-01 2250.5MHZ AGC C_RCVR1", s40.ToString());

            var none = new CustomizeViewModel(LockPlot(), new FakePrompts());
            Assert.False(none.HasSnrStreams);
            Assert.Null(none.SelectedSnrStream);
            Assert.Equal("Expand All", none.ExpandText);
            none.ToggleExpandCommand.Execute(null);
            none.SelectAllSnrCommand.Execute(null);
        }

        [Fact]
        public void AReceiversBoxFollowsItsChannelsAndSetsThemAll()
        {
            var c = new CustomizeViewModel(Mixed(), new FakePrompts());
            ReceiverGroup r1 = c.SnrStreams[0].LeftReceivers[0];
            var changed = new List<string?>();
            r1.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

            r1.Channels[1].IsChecked = false;
            Assert.Null(r1.IsChecked);   // partly
            Assert.Contains(nameof(ReceiverGroup.IsChecked), changed);
            r1.IsChecked = false;        // a click on a partly checked box checks them all, as Qt's does
            Assert.Equal(true, r1.IsChecked);
            Assert.All(r1.Channels, ch => Assert.True(ch.IsChecked));
            r1.IsChecked = false;
            Assert.All(r1.Channels, ch => Assert.False(ch.IsChecked));
            r1.IsChecked = null;
            Assert.Equal(true, r1.IsChecked);
            foreach (ChannelItem ch in r1.Channels) ch.IsChecked = false;
            Assert.Equal(false, r1.IsChecked);
        }

        [Fact]
        public void SelectAllAndNoneAndExpandActOnTheirTabAndTheStreamShown()
        {
            var c = new CustomizeViewModel(Mixed(), new FakePrompts());
            c.SelectNoneLockCommand.Execute(null);
            Assert.All(c.LockRows, r => Assert.False(r.IsChecked));
            c.SelectAllLockCommand.Execute(null);
            Assert.All(c.LockRows, r => Assert.True(r.IsChecked));

            c.SelectNoneSnrCommand.Execute(null);
            Assert.All(c.SnrStreams[0].Receivers.SelectMany(r => r.Channels), ch => Assert.False(ch.IsChecked));
            Assert.All(c.SnrStreams[1].Receivers.SelectMany(r => r.Channels), ch => Assert.True(ch.IsChecked));
            c.SelectedSnrStream = c.SnrStreams[1];
            c.SelectNoneSnrCommand.Execute(null);
            Assert.All(c.SnrStreams[1].Receivers.SelectMany(r => r.Channels), ch => Assert.False(ch.IsChecked));
            c.SelectAllSnrCommand.Execute(null);
            Assert.All(c.SnrStreams[1].Receivers.SelectMany(r => r.Channels), ch => Assert.True(ch.IsChecked));
            Assert.All(c.SnrStreams[0].Receivers.SelectMany(r => r.Channels), ch => Assert.False(ch.IsChecked));

            c.SelectedSnrStream = c.SnrStreams[0];
            var said = new List<string?>();
            c.PropertyChanged += (_, e) => said.Add(e.PropertyName);
            Assert.Equal("Expand All", c.ExpandText);
            c.ToggleExpandCommand.Execute(null);
            Assert.All(c.SnrStreams[0].Receivers, r => Assert.True(r.IsExpanded));
            Assert.All(c.SnrStreams[1].Receivers, r => Assert.False(r.IsExpanded));
            Assert.Equal("Collapse All", c.ExpandText);
            Assert.Contains(nameof(CustomizeViewModel.ExpandText), said);
            c.SnrStreams[0].LeftReceivers[0].IsExpanded = false;   // one collapsed by hand
            Assert.Equal("Expand All", c.ExpandText);
            c.ToggleExpandCommand.Execute(null);
            Assert.All(c.SnrStreams[0].Receivers, r => Assert.True(r.IsExpanded));
            c.ToggleExpandCommand.Execute(null);
            Assert.All(c.SnrStreams[0].Receivers, r => Assert.False(r.IsExpanded));
            said.Clear();
            c.SelectedSnrStream = c.SnrStreams[1];
            Assert.Contains(nameof(CustomizeViewModel.ExpandText), said);
        }

        [Fact]
        public async Task OkShowsOnlyTheMetricInViewAndAppliesTheNamesAndColours()
        {
            PlotViewModel plot = Mixed();
            plot.SetLockAxisView(LockAxisView.MissedFrames);
            var prompts = new FakePrompts();
            var c = new CustomizeViewModel(plot, prompts);
            c.LockRows[0].IsChecked = true;    // Ch 5
            c.LockRows[1].IsChecked = false;   // Ch 6
            c.LockRows[0].Name = "Five";
            prompts.Answers.Enqueue(Colors.Teal);
            await c.LockRows[0].PickColorCommand.ExecuteAsync(null);
            Assert.Equal(plot.SeriesById(2)!.Color, prompts.LastColor);   // the dialog opens on the stream's colour
            Assert.Equal(Colors.Teal, ((ISolidColorBrush)c.LockRows[0].Brush).Color);
            await c.LockRows[1].PickColorCommand.ExecuteAsync(null);   // cancelled
            Assert.Equal(plot.SeriesById(0)!.Color, c.LockRows[1].Color);

            ChannelItem ch = c.SnrStreams[0].LeftReceivers[0].Channels[0];
            ch.IsChecked = false;
            prompts.Answers.Enqueue("renamed");
            await ch.RenameCommand.ExecuteAsync(null);
            Assert.Equal("renamed", ch.ToolTip);
            prompts.Answers.Enqueue(Colors.Olive);
            await ch.RecolorCommand.ExecuteAsync(null);
            Assert.Equal(Colors.Olive, ((ISolidColorBrush)ch.Brush).Color);
            // A second rename is seeded with the first.
            await ch.RenameCommand.ExecuteAsync(null);
            Assert.Equal("renamed", prompts.LastText.Initial);
            Assert.Equal(("Rename Series", "Series name:"), (prompts.LastText.Title, prompts.LastText.Label));
            await ch.RecolorCommand.ExecuteAsync(null);
            Assert.Equal(Colors.Olive, prompts.LastColor);

            // Nothing reaches the plot before OK.
            Assert.Equal("Ch 5", plot.SeriesById(2)!.Name);
            Assert.True(plot.SeriesById(ch.SeriesId)!.Visible);

            int refreshes = 0;
            plot.SeriesChanged += (_, _) => refreshes++;
            c.Apply();
            Assert.Equal(1, refreshes);
            // Ch 5: only its missed frames shown (the view in use); Ch 6 neither.
            Assert.False(plot.SeriesById(2)!.Visible);
            Assert.True(plot.SeriesById(3)!.Visible);
            Assert.False(plot.SeriesById(0)!.Visible);
            Assert.False(plot.SeriesById(1)!.Visible);
            Assert.Equal(new[] { "Five", "Five" }, new[] { plot.SeriesById(2)!.Name, plot.SeriesById(3)!.Name });
            Assert.Equal(Colors.Teal, plot.SeriesById(3)!.Color);
            PlotSeries s = plot.SeriesById(ch.SeriesId)!;
            Assert.False(s.Visible);
            Assert.Equal("renamed", s.Name);
            Assert.Equal(Colors.Olive, s.Color);
            // A channel left alone keeps its name and colour.
            ChannelItem other = c.SnrStreams[0].LeftReceivers[0].Channels[1];
            Assert.Equal(Snr(0, 40, 0, 1, 1).Name, plot.SeriesById(other.SeriesId)!.Name);
            Assert.Null(other.PendingName);
            Assert.Null(other.PendingColor);
        }

        [Fact]
        public async Task SeriesGoneWhileTheDialogIsOpenAreSkipped()
        {
            PlotViewModel plot = Mixed();
            var prompts = new FakePrompts();
            var c = new CustomizeViewModel(plot, prompts);
            ChannelItem ch = c.SnrStreams[0].LeftReceivers[0].Channels[0];
            plot.SetSeries(LockStream(50, "Ch 9", 9, 0));   // every series replaced

            await ch.RenameCommand.ExecuteAsync(null);
            await ch.RecolorCommand.ExecuteAsync(null);
            Assert.Empty(prompts.Asked);
            c.LockRows[0].IsChecked = false;
            c.Apply();
            Assert.True(plot.Series[0].Visible);
            Assert.Equal("Ch 9", plot.Series[0].Name);
        }

        // ---- the view: chips, legend, menu, keys --------------------------------------------------------------------

        private sealed record Shown(PlotView View, Window Window)
        {
            public Point At(Control c) => Frames.InWindow(c, Window).Center;
            public Point At(double x, double y) => Frames.InWindow(View, Window).TopLeft + new Point(x, y);
            public void Click(Control c)
            {
                Window.MouseDown(At(c), MouseButton.Left, RawInputModifiers.None);
                Window.MouseUp(At(c), MouseButton.Left, RawInputModifiers.None);
                Dispatcher.UIThread.RunJobs();
            }
            public void Key(Key key)
            {
                Window.KeyPress(key, RawInputModifiers.None, PhysicalKey.None, null);
                Window.KeyRelease(key, RawInputModifiers.None, PhysicalKey.None, null);
                Dispatcher.UIThread.RunJobs();
            }
        }

        private static Shown Show(PlotViewModel? plot, double width = 600, double height = 400, IPlotPrompts? prompts = null)
        {
            var view = new PlotView { Width = width, Height = height, Plot = plot };
            if (prompts is not null) view.Prompts = prompts;
            var window = new Window { Content = view, SizeToContent = SizeToContent.WidthAndHeight };
            TextOptions.SetTextRenderingMode(window, TextRenderingMode.Antialias);
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return new Shown(view, window);
        }

        [AvaloniaFact]
        public void TheChipBarIsOverTheChartAtTheTopLeftAndOnlyWithData()
        {
            var plot = new PlotViewModel();
            Shown s = Show(plot);
            Assert.False(s.View.ChipBar.IsVisible);
            Assert.Same(s.View.Overlay, s.View.ChipBar.Parent);
            Assert.Same(s.View.ChipBar, s.View.ViewModeChip.Parent);
            Assert.Same(s.View.ChipBar, s.View.ResetChip.Parent);

            plot.SetSeries(LockStream(0, "Ch 5", 5, 0));
            Dispatcher.UIThread.RunJobs();
            Assert.True(s.View.ChipBar.IsVisible);
            Rect chip = Frames.InWindow(s.View.ViewModeChip, s.Window), view = Frames.InWindow(s.View, s.Window);
            Assert.Equal(view.X + 8, chip.X, 6);
            Assert.Equal(view.Y + 8, chip.Y, 6);
            Assert.Equal(26, chip.Height, 6);
            // The legend, top right, does not meet it.
            Assert.False(chip.Intersects(Frames.InWindow(s.View.Legend, s.Window)));

            // Neither chip has anything to do for one SNR stream at rest: the bar takes no room.
            plot.SetSeries(SnrOnly().Series.ToArray());
            Dispatcher.UIThread.RunJobs();
            Assert.True(s.View.ChipBar.IsVisible);
            Assert.False(s.View.ViewModeChip.IsVisible);
            Assert.False(s.View.ResetChip.IsVisible);
            Assert.Equal(new Size(0, 0), s.View.ChipBar.Bounds.Size);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheViewModeChipSwitchesTheLeftAxisAndIsNamedForWhereItGoes()
        {
            PlotViewModel plot = LockPlot();
            Shown s = Show(plot);
            Button chip = s.View.ViewModeChip;
            Assert.True(chip.IsVisible);
            Assert.Equal("Show Accumulation", chip.Content);
            Assert.Equal("Switch the left axis to Accumulated Missed Frames", ToolTip.GetTip(chip));

            s.Click(chip);
            Assert.Equal(LockAxisView.MissedFrames, plot.LockAxisView);
            Assert.Equal("Show Lock %", chip.Content);
            Assert.Equal("Switch the left axis to Frame Sync Lock %", ToolTip.GetTip(chip));
            // The click was the chip's: the chart under it did not pan or zoom.
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
            s.Click(chip);
            Assert.Equal(LockAxisView.LockPercent, plot.LockAxisView);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheResetChipAppearsOnlyOnceTheViewHasChanged()
        {
            PlotViewModel plot = LockPlot();
            Shown s = Show(plot);
            Button chip = s.View.ResetChip;
            Assert.False(chip.IsVisible);
            Assert.Equal("Reset view", chip.Content);
            Assert.Equal("Restore the full time span and automatic axis scaling (or double-click the chart)", ToolTip.GetTip(chip));

            plot.SetXViewByUser(0.5, 1.5);
            Dispatcher.UIThread.RunJobs();
            Assert.True(chip.IsVisible);
            s.Click(chip);
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
            Assert.False(chip.IsVisible);

            plot.SetLeftMaxOverride(42);
            Dispatcher.UIThread.RunJobs();
            Assert.True(chip.IsVisible);
            s.Click(chip);
            Assert.False(plot.HasRangeOverride);
            Assert.False(chip.IsVisible);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheLegendFloatsAtTheTopRightWithARowPerSeriesShown()
        {
            var plot = new PlotViewModel();
            Shown s = Show(plot);
            PlotLegend legend = s.View.Legend;
            Assert.False(legend.IsVisible);

            plot.SetSeries(LockStream(0, "CH-01 PRN 15 5M", 1, 0));
            Dispatcher.UIThread.RunJobs();
            Assert.True(legend.IsVisible);
            Assert.Equal(1, legend.RowCount);
            Rect box = Frames.InWindow(legend, s.Window), view = Frames.InWindow(s.View, s.Window);
            Assert.Equal(view.Right - PlotLegend.EdgeMargin, box.Right, 6);
            Assert.Equal(view.Top + PlotLegend.EdgeMargin, box.Top, 6);
            // A real row, well beyond the frame's own padding.
            Assert.True(box.Width > 2 * PlotLegend.ContentMargin + 40, $"the legend is {box.Width} wide");
            Assert.True(box.Height > 2 * PlotLegend.ContentMargin + 8, $"the legend is {box.Height} high");

            plot.SetSeries(LockStream(0, "CH-01 PRN 15 5M", 1, 0).Concat(LockStream(2, "CH-02 PRN 15 20M", 2, 1)));
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(2, legend.RowCount);
            // A row: the swatch in the series' colour, then the label.
            var row = (StackPanel)legend.Rows[1];
            Border swatch = Assert.IsType<Border>(row.Children[0]);
            Assert.Equal((PlotLegend.SwatchLength, PlotLegend.SwatchThickness), (swatch.Width, swatch.Height));
            Assert.Equal(plot.Series[2].Color, ((ISolidColorBrush)swatch.Background!).Color);
            Assert.Equal("CH-02 PRN 15 20M", Assert.IsType<TextBlock>(row.Children[1]).Text);
            Assert.Null(ToolTip.GetTip(row));   // its label is its whole name

            plot.SetSeries(new[] { Snr(0, 40, 0, 3, 0, "40 - CH-01 2250.5MHZ AGC 800Kbps RNRZ-L L_RCVR3") });
            Dispatcher.UIThread.RunJobs();
            row = (StackPanel)legend.Rows[0];
            Assert.Equal("CH40 L_RCVR3", ((TextBlock)row.Children[1]).Text);
            Assert.Equal("40 - CH-01 2250.5MHZ AGC 800Kbps RNRZ-L L_RCVR3", ToolTip.GetTip(row));

            plot.Clear();
            Dispatcher.UIThread.RunJobs();
            Assert.False(legend.IsVisible);
            Assert.Equal(0, legend.RowCount);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void ADenseLegendIsCappedAndScrollsWithoutCoveringALabel()
        {
            PlotViewModel plot = SnrOnly(48);
            Shown s = Show(plot);
            PlotLegend legend = s.View.Legend;
            Assert.Equal(48, legend.RowCount);
            Assert.Equal(400 * PlotLegend.MaxHeightFraction, legend.Bounds.Height, 6);
            Assert.True(legend.Scroll.Extent.Height > legend.Scroll.Viewport.Height);
            ScrollBar bar = legend.GetVisualDescendants().OfType<ScrollBar>().Single(b => b.Orientation == Orientation.Vertical);
            Assert.True(bar.IsVisible);
            Rect barBox = Frames.InWindow(bar, s.Window);
            Assert.True(barBox.Width > 0);
            foreach (Control row in legend.Rows)
            {
                Rect text = Frames.InWindow(((StackPanel)row).Children[1], s.Window);
                Assert.True(text.Right <= barBox.Left, $"a label ends at {text.Right}, under the scroll bar at {barBox.Left}");
            }

            // So wide a name is capped too, at a fraction of the chart's width.
            plot.SetSeries(new[] { Snr(0, 40, 0, 1, 0, new string('W', 300)) });
            plot.SetSeries(new[] { PlotFixtures.Lock(0, new string('W', 300), 1, 0, X3, X3) });
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(600 * PlotLegend.MaxWidthFraction, legend.Bounds.Width, 6);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheLegendIsDraggedAnywhereInsideTheChartAndKeptThere()
        {
            PlotViewModel plot = LockPlot();
            Shown s = Show(plot);
            PlotLegend legend = s.View.Legend;
            Point start = legend.Position;
            Assert.False(legend.UserMoved);

            Point grab = s.At(legend);
            s.Window.MouseDown(grab, MouseButton.Left, RawInputModifiers.None);
            s.Window.MouseMove(grab + new Point(-200, 100), RawInputModifiers.LeftMouseButton);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(start + new Point(-200, 100), legend.Position);
            Assert.True(legend.UserMoved);
            // Dragged off the chart, it stops a margin inside it.
            s.Window.MouseMove(grab + new Point(-2000, 2000), RawInputModifiers.LeftMouseButton);
            s.Window.MouseUp(grab + new Point(-2000, 2000), MouseButton.Left, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(new Point(PlotLegend.EdgeMargin, 400 - legend.Bounds.Height - PlotLegend.EdgeMargin), legend.Position);
            // The drag was the legend's: the chart under it did not pan.
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
            // Moving the mouse after the release moves nothing.
            s.Window.MouseMove(grab, RawInputModifiers.None);
            Assert.Equal(PlotLegend.EdgeMargin, legend.Position.X);

            // A smaller chart keeps it inside; a new series leaves it where it was put.
            s.View.Height = 200;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(200 - legend.Bounds.Height - PlotLegend.EdgeMargin, legend.Position.Y, 6);
            plot.SetSeries(LockStream(0, "Ch 6", 6, 0));
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(PlotLegend.EdgeMargin, legend.Position.X);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void AnUndraggedLegendFollowsTheTopRightWhenTheChartIsResized()
        {
            PlotViewModel plot = LockPlot();
            Shown s = Show(plot);
            s.View.Width = 800;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(800 - s.View.Legend.Bounds.Width - PlotLegend.EdgeMargin, s.View.Legend.Position.X, 6);
            Assert.Equal(PlotLegend.EdgeMargin, s.View.Legend.Position.Y);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheLegendIsTranslucentSoTheDataBehindItShows()
        {
            var plot = new PlotViewModel();
            // A red series flat across the middle of the plot.
            plot.SetSeries(new[] { PlotFixtures.Snr(0, "red", 40, 0, 1, 0, new[] { 0.0, 2.0 }, new[] { 50.0, 50.0 }) });
            plot.RecolorSeries(0, Color.FromRgb(204, 0, 0));
            plot.SetRightMinOverride(0);
            plot.SetRightMaxOverride(100);
            Shown s = Show(plot);
            PlotLegend legend = s.View.Legend;
            Assert.Equal(PlotLegend.BackgroundColor, ((ImmutableSolidColorBrush)legend.Background!).Color);
            Assert.Equal(185, PlotLegend.BackgroundColor.A);

            // Dragged so that it lies across the line.
            double lineY = s.View.Chart.RightToPixel(50);
            Point grab = s.At(legend);
            Point to = s.At(300, lineY);
            s.Window.MouseDown(grab, MouseButton.Left, RawInputModifiers.None);
            s.Window.MouseMove(to, RawInputModifiers.LeftMouseButton);
            s.Window.MouseUp(to, MouseButton.Left, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Rect box = Frames.InWindow(legend, s.Window).Deflate(2);
            Assert.True(box.Top < s.At(0, lineY).Y && box.Bottom > s.At(0, lineY).Y);
            using ILockedFramebuffer fb = Frames.Capture(s.Window).Lock();
            int reddish = 0;
            for (int y = (int)box.Top; y < (int)box.Bottom; y++)
                for (int x = (int)box.Left; x < (int)box.Right; x++)
                {
                    uint p = Frames.Pixel(fb, x, y);
                    int r = (int)(p >> 16) & 0xFF, g = (int)(p >> 8) & 0xFF, b = (int)p & 0xFF;
                    if (r > g + 40 && r > b + 40) reddish++;
                }
            Assert.True(reddish > 10, $"only {reddish} pixels of the line show through the legend");
            s.Window.Close();
        }

        [AvaloniaFact]
        public void AHiddenLegendStaysHiddenUntilShownAgain()
        {
            PlotViewModel plot = LockPlot();
            Shown s = Show(plot);
            Assert.True(s.View.Legend.IsVisible);
            plot.LegendVisible = false;
            Dispatcher.UIThread.RunJobs();
            Assert.False(s.View.Legend.IsVisible);
            plot.SetSeries(LockStream(0, "Ch 5", 5, 0).Concat(LockStream(2, "Ch 6", 6, 1)));
            Dispatcher.UIThread.RunJobs();
            Assert.False(s.View.Legend.IsVisible);
            Assert.Equal(2, s.View.Legend.RowCount);
            plot.Clear();   // a new recording keeps the choice
            plot.SetSeries(LockStream(0, "Ch 5", 5, 0));
            Dispatcher.UIThread.RunJobs();
            Assert.False(s.View.Legend.IsVisible);
            plot.LegendVisible = true;
            Dispatcher.UIThread.RunJobs();
            Assert.True(s.View.Legend.IsVisible);
            s.Window.Close();
        }

        private static MenuItem Item(ItemsControl menu, string header) =>
            menu.Items.OfType<MenuItem>().Single(i => (string?)i.Header == header);

        private static string[] Headers(ItemsControl menu) =>
            menu.Items.Select(i => i is MenuItem m ? (string)m.Header! : "-").ToArray();

        private static async Task Choose(MenuItem item)
        {
            if (item.Command is IAsyncRelayCommand asyncCommand) await asyncCommand.ExecuteAsync(null);
            else item.Command!.Execute(item.CommandParameter);
            Dispatcher.UIThread.RunJobs();
        }

        [AvaloniaFact]
        public void TheMenuLeadsWithTheTitleAndHoldsEveryPlotControlAndNoExport()
        {
            Shown s = Show(LockPlot(), prompts: new FakePrompts());
            ContextMenu menu = s.View.BuildContextMenu()!;
            Assert.Equal(new[]
            {
                "Set Plot Title...", "-", "Plot File", "View Mode  (V)", "-", "Customize View...", "Show Legend", "Readout", "-",
                "X Axis", "Y Axes", "Reset View",
            }, Headers(menu));
            Assert.Equal(new[] { "Set Time Window...", "Zoom Back", "Reset Span" }, Headers(Item(menu, "X Axis")));
            Assert.Equal(new[] { "Set Left Min...", "Set Left Max...", "-", "Set Right Min...", "Set Right Max...", "-", "Reset" },
                         Headers(Item(menu, "Y Axes")));
            Assert.Equal(new[] { "Lock Percentage", "Accumulation" }, Headers(Item(menu, "View Mode  (V)")));
            Assert.DoesNotContain(Headers(menu), h => h.Contains("Export"));
            // Plot File waits for batch runs (plan step 4.7): one recording has nothing to switch between.
            Assert.False(Item(menu, "Plot File").IsEnabled);
            Assert.Null(new PlotView().BuildContextMenu());
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheMenuAdvertisesEveryKeyOfThePlot()
        {
            Shown s = Show(LockPlot(), prompts: new FakePrompts());
            ContextMenu menu = s.View.BuildContextMenu()!;
            Assert.Contains("(V)", (string)Item(menu, "View Mode  (V)").Header!);
            Assert.Equal(new KeyGesture(Key.L), Item(menu, "Show Legend").InputGesture);
            Assert.Equal(new KeyGesture(Key.R), Item(menu, "Reset View").InputGesture);
            Assert.Equal(new KeyGesture(Key.Home), Item(Item(menu, "X Axis"), "Reset Span").InputGesture);
            Assert.Equal(new KeyGesture(Key.Back), Item(Item(menu, "X Axis"), "Zoom Back").InputGesture);
            // Advertised only: the keys are the plot's, so a menu item's gesture must not be a hot key of its own.
            Assert.All(new[] { Item(menu, "Show Legend"), Item(menu, "Reset View") }, i => Assert.Null(i.HotKey));
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheMenusDataItemsWaitForData()
        {
            var plot = new PlotViewModel();
            Shown s = Show(plot, prompts: new FakePrompts());
            ContextMenu empty = s.View.BuildContextMenu()!;
            foreach (string h in new[] { "Set Plot Title...", "Plot File", "View Mode  (V)", "Customize View...", "Show Legend", "Readout", "X Axis", "Y Axes", "Reset View" })
                Assert.False(Item(empty, h).IsEnabled, h + " is enabled without data");
            Assert.Empty(Item(empty, "Readout").Items);

            plot.SetSeries(LockStream(0, "Ch 5", 5, 0));
            ContextMenu menu = s.View.BuildContextMenu()!;
            foreach (string h in new[] { "Set Plot Title...", "View Mode  (V)", "Customize View...", "Show Legend", "Readout", "X Axis", "Y Axes", "Reset View" })
                Assert.True(Item(menu, h).IsEnabled, h + " is disabled with data");
            // One metric only: nothing to switch to.
            plot.SetSeries(SnrOnly().Series.ToArray());
            Assert.False(Item(s.View.BuildContextMenu()!, "View Mode  (V)").IsEnabled);
            s.Window.Close();
        }

        [AvaloniaFact]
        public async Task TheMenuSetsTheViewModeAndTheLegend()
        {
            PlotViewModel plot = LockPlot();
            Shown s = Show(plot, prompts: new FakePrompts());
            ContextMenu menu = s.View.BuildContextMenu()!;
            MenuItem lockItem = Item(Item(menu, "View Mode  (V)"), "Lock Percentage");
            MenuItem accum = Item(Item(menu, "View Mode  (V)"), "Accumulation");
            Assert.Equal(MenuItemToggleType.Radio, accum.ToggleType);
            Assert.True(lockItem.IsChecked);
            Assert.False(accum.IsChecked);
            await Choose(accum);
            Assert.Equal(LockAxisView.MissedFrames, plot.LockAxisView);
            Assert.True(Item(Item(s.View.BuildContextMenu()!, "View Mode  (V)"), "Accumulation").IsChecked);
            await Choose(Item(Item(s.View.BuildContextMenu()!, "View Mode  (V)"), "Lock Percentage"));
            Assert.Equal(LockAxisView.LockPercent, plot.LockAxisView);

            MenuItem legend = Item(menu, "Show Legend");
            Assert.Equal(MenuItemToggleType.CheckBox, legend.ToggleType);
            Assert.True(legend.IsChecked);
            await Choose(legend);
            Assert.False(plot.LegendVisible);
            Assert.False(s.View.Legend.IsVisible);
            Assert.False(Item(s.View.BuildContextMenu()!, "Show Legend").IsChecked);
            await Choose(Item(s.View.BuildContextMenu()!, "Show Legend"));
            Assert.True(s.View.Legend.IsVisible);
            s.Window.Close();
        }

        [AvaloniaFact]
        public async Task TheMenusAxisItemsResetZoomBackAndAsk()
        {
            PlotViewModel plot = LockPlot();
            var prompts = new FakePrompts();
            Shown s = Show(plot, prompts: prompts);

            Assert.False(Item(Item(s.View.BuildContextMenu()!, "X Axis"), "Zoom Back").IsEnabled);
            plot.PushXZoom(0.2, 1.8);
            MenuItem back = Item(Item(s.View.BuildContextMenu()!, "X Axis"), "Zoom Back");
            Assert.True(back.IsEnabled);
            await Choose(back);
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
            Assert.False(plot.CanZoomBack);

            plot.SetXViewByUser(0.5, 1.5);
            plot.SetLeftMinOverride(10);
            plot.SetLeftMaxOverride(42);
            plot.SetRightMinOverride(2);
            plot.SetRightMaxOverride(24);
            ContextMenu menu = s.View.BuildContextMenu()!;
            await Choose(Item(Item(menu, "X Axis"), "Reset Span"));
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
            Assert.True(plot.HasRangeOverride);   // the span only
            await Choose(Item(Item(menu, "Y Axes"), "Reset"));
            Assert.False(plot.HasRangeOverride);

            plot.SetXViewByUser(0.5, 1.5);
            plot.SetRightMaxOverride(24);
            await Choose(Item(s.View.BuildContextMenu()!, "Reset View"));
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
            Assert.False(plot.HasRangeOverride);

            // Each asking item reaches its question.
            menu = s.View.BuildContextMenu()!;
            await Choose(Item(menu, "Set Plot Title..."));
            await Choose(Item(Item(menu, "X Axis"), "Set Time Window..."));
            await Choose(Item(Item(menu, "Y Axes"), "Set Left Min..."));
            await Choose(Item(Item(menu, "Y Axes"), "Set Left Max..."));
            await Choose(Item(Item(menu, "Y Axes"), "Set Right Min..."));
            await Choose(Item(Item(menu, "Y Axes"), "Set Right Max..."));
            await Choose(Item(menu, "Customize View..."));
            Assert.Equal(new[]
            {
                "Set Plot Title", "Set Time Window", "Set Left Y-Axis Minimum", "Set Left Y-Axis Maximum",
                "Set Right Y-Axis Minimum", "Set Right Y-Axis Maximum", "Customize",
            }, prompts.Asked);
            s.Window.Close();
        }

        [AvaloniaFact]
        public async Task TheReadoutMenuListsTheSeriesShownAndPinsOne()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(LockStream(0, "Ch 5", 5, 0).Append(Snr(2, 40, 1, 1, 0)));
            Shown s = Show(plot, prompts: new FakePrompts());
            MenuItem readout = Item(s.View.BuildContextMenu()!, "Readout");
            Assert.Equal(new[] { "Nearest series (automatic)", "-", "Ch 5", plot.Series[2].Name }, Headers(readout));
            Assert.True(Item(readout, "Nearest series (automatic)").IsChecked);
            Assert.All(readout.Items.OfType<MenuItem>(), i => Assert.Equal(MenuItemToggleType.Radio, i.ToggleType));

            await Choose(Item(readout, plot.Series[2].Name));
            Assert.Equal(2, plot.ReadoutSeriesId);
            readout = Item(s.View.BuildContextMenu()!, "Readout");
            Assert.True(Item(readout, plot.Series[2].Name).IsChecked);
            Assert.False(Item(readout, "Nearest series (automatic)").IsChecked);
            await Choose(Item(readout, "Nearest series (automatic)"));
            Assert.Null(plot.ReadoutSeriesId);

            // A pin to a series since hidden is dropped when the menu opens, so it opens with automatic chosen.
            plot.ReadoutSeriesId = 2;
            plot.SetSeriesVisible(2, false);
            readout = Item(s.View.BuildContextMenu()!, "Readout");
            Assert.Null(plot.ReadoutSeriesId);
            Assert.True(Item(readout, "Nearest series (automatic)").IsChecked);
            Assert.Equal(new[] { "Nearest series (automatic)", "-", "Ch 5" }, Headers(readout));
            s.Window.Close();
        }

        [AvaloniaFact]
        public void ARightClickOnTheChartOpensTheMenu()
        {
            PlotViewModel plot = LockPlot();
            Shown s = Show(plot, prompts: new FakePrompts());
            Point p = s.At(300, 200);
            s.Window.MouseDown(p, MouseButton.Right, RawInputModifiers.None);
            s.Window.MouseUp(p, MouseButton.Right, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            ContextMenu? menu = s.View.Chart.ContextMenu;
            Assert.NotNull(menu);
            Assert.True(menu!.IsOpen);
            Assert.Equal("Set Plot Title...", ((MenuItem)menu.Items[0]!).Header);
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));   // no pan from the right button
            menu.Close();
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheKeysDriveTheViewWhileTheChartHasFocus()
        {
            PlotViewModel plot = LockPlot();
            Shown s = Show(plot);
            s.View.Chart.Focus();
            Dispatcher.UIThread.RunJobs();

            s.Key(Key.L);
            Assert.False(plot.LegendVisible);
            s.Key(Key.L);
            Assert.True(plot.LegendVisible);

            s.Key(Key.V);
            Assert.Equal(LockAxisView.MissedFrames, plot.LockAxisView);
            s.Key(Key.V);
            Assert.Equal(LockAxisView.LockPercent, plot.LockAxisView);

            plot.SetXViewByUser(0.4, 0.6);
            s.Key(Key.Right);
            Assert.Equal(0.42, plot.XViewMin, 12);
            s.Key(Key.Left);
            Assert.Equal(0.4, plot.XViewMin, 12);
            s.Key(Key.OemPlus);
            Assert.Equal(0.16, plot.XViewMax - plot.XViewMin, 12);
            s.Key(Key.OemMinus);
            Assert.Equal(0.2, plot.XViewMax - plot.XViewMin, 12);
            s.Key(Key.Add);
            Assert.Equal(0.16, plot.XViewMax - plot.XViewMin, 12);
            s.Key(Key.Subtract);
            Assert.Equal(0.2, plot.XViewMax - plot.XViewMin, 12);
            Assert.Equal(0.5, (plot.XViewMin + plot.XViewMax) / 2, 12);

            // Backspace steps back one zoom.
            int depth = plot.ZoomDepth;
            s.Key(Key.Back);
            Assert.Equal(depth - 1, plot.ZoomDepth);

            // Home: the whole span, a Y limit kept. R: both.
            plot.SetXViewByUser(0.4, 0.6);
            plot.SetLeftMaxOverride(42);
            s.Key(Key.Home);
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
            Assert.True(plot.HasRangeOverride);
            plot.SetXViewByUser(0.4, 0.6);
            s.Key(Key.R);
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
            Assert.False(plot.HasRangeOverride);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheKeysDoNothingWithoutData()
        {
            var plot = new PlotViewModel();
            Shown s = Show(plot);
            s.View.Chart.Focus();
            foreach (Key k in new[] { Key.L, Key.V, Key.R, Key.Home, Key.Back, Key.Left, Key.Right, Key.OemPlus, Key.OemMinus })
                s.Key(k);
            Assert.True(plot.LegendVisible);
            Assert.False(plot.HasData);
            s.Window.Close();
        }

        [AvaloniaFact]
        public void TheKeysAreTheChartsAndNotTheWindows()
        {
            // A bare letter taken application-wide would be lost to every text box: typing "Lock" must reach the box.
            PlotViewModel plot = LockPlot();
            var view = new PlotView { Width = 400, Height = 300, Plot = plot };
            var box = new TextBox();
            var window = new Window { Content = new StackPanel { Children = { view, box } }, SizeToContent = SizeToContent.WidthAndHeight };
            window.Show();
            box.Focus();
            Dispatcher.UIThread.RunJobs();
            foreach (char ch in "Lock")
            {
                window.KeyPress(Enum.Parse<Key>(char.ToUpperInvariant(ch).ToString()), RawInputModifiers.None, PhysicalKey.None, ch.ToString());
                window.KeyTextInput(ch.ToString());
                window.KeyRelease(Enum.Parse<Key>(char.ToUpperInvariant(ch).ToString()), RawInputModifiers.None, PhysicalKey.None, ch.ToString());
            }
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("Lock", box.Text);
            Assert.True(plot.LegendVisible);
            window.Close();
        }

        [AvaloniaFact]
        public void AClickOnTheChartGivesItTheKeys()
        {
            PlotViewModel plot = LockPlot();
            var view = new PlotView { Width = 400, Height = 300, Plot = plot };
            var box = new TextBox();
            var window = new Window { Content = new StackPanel { Children = { view, box } }, SizeToContent = SizeToContent.WidthAndHeight };
            window.Show();
            box.Focus();
            Dispatcher.UIThread.RunJobs();
            Point p = Frames.InWindow(view, window).Center;
            window.MouseDown(p, MouseButton.Left, RawInputModifiers.None);
            window.MouseUp(p, MouseButton.Left, RawInputModifiers.None);
            window.KeyPress(Key.L, RawInputModifiers.None, PhysicalKey.None, null);
            Dispatcher.UIThread.RunJobs();
            Assert.False(plot.LegendVisible);
            window.Close();
        }

        // ---- the windows of the prompts -----------------------------------------------------------------------------

        private static Window Owner()
        {
            var owner = new Window { Width = 400, Height = 300, Content = new Border() };
            owner.Show();
            Dispatcher.UIThread.RunJobs();
            return owner;
        }

        private static void Press(Button b)
        {
            // A bound command runs from the button's own click handling, which a raised Click event does not reach.
            if (b.Command is not null) b.Command.Execute(b.CommandParameter);
            else b.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
        }

        private static async Task<T> Closed<T>(Task<T> dialog, string what)
        {
            Assert.True(dialog.IsCompleted, what + " is still open.");
            return await dialog;
        }

        [AvaloniaFact]
        public async Task EachPromptAnswersOnOkAndNothingOnCancel()
        {
            Window owner = Owner();
            var prompts = new WindowPlotPrompts((Control)owner.Content!);

            Task<string?> text = prompts.AskTextAsync("Set Plot Title", "Plot title:", "old");
            Dispatcher.UIThread.RunJobs();
            var tw = Assert.IsType<TextPromptWindow>(owner.OwnedWindows.Single());
            Assert.Equal("Set Plot Title", tw.Title);
            Assert.Contains(tw.GetLogicalDescendants().OfType<TextBlock>(), t => t.Text == "Plot title:");
            Assert.Equal("old", tw.Field.Text);
            Assert.True(tw.OkButton.IsDefault);
            Assert.True(tw.CancelButton.IsCancel);
            tw.Field.Text = "new";
            Press(tw.OkButton);
            Assert.Equal("new", await Closed(text, "the text prompt"));

            text = prompts.AskTextAsync("t", "l", "old");
            Dispatcher.UIThread.RunJobs();
            Press(((TextPromptWindow)owner.OwnedWindows.Single()).CancelButton);
            Assert.Null(await Closed(text, "the text prompt (Cancel)"));

            Task<double?> number = prompts.AskNumberAsync("Set Left Y-Axis Minimum", "Minimum lock %:", 12.4, 0, 99, 0);
            Dispatcher.UIThread.RunJobs();
            var nw = Assert.IsType<NumberPromptWindow>(owner.OwnedWindows.Single());
            Assert.Equal(12m, nw.Field.Value);
            Assert.Equal((0m, 99m), (nw.Field.Minimum, nw.Field.Maximum));
            Assert.Equal("F0", nw.Field.FormatString);
            nw.Field.Value = 150;   // past its maximum
            Press(nw.OkButton);
            Assert.Equal(99.0, await Closed(number, "the number prompt"));

            number = prompts.AskNumberAsync("t", "l", 1, -999, 49, 1);
            Dispatcher.UIThread.RunJobs();
            nw = (NumberPromptWindow)owner.OwnedWindows.Single();
            Assert.Equal("F1", nw.Field.FormatString);
            nw.Field.Value = -12.54m;
            Press(nw.OkButton);
            Assert.Equal(-12.5, await Closed(number, "the number prompt (decimals)"));
            number = prompts.AskNumberAsync("t", "l", 1, 0, 10, 0);
            Dispatcher.UIThread.RunJobs();
            Press(((NumberPromptWindow)owner.OwnedWindows.Single()).CancelButton);
            Assert.Null(await Closed(number, "the number prompt (Cancel)"));

            Task<(string Start, string Stop)?> window = prompts.AskTimeWindowAsync("001:00:00:00", "001:00:01:00");
            Dispatcher.UIThread.RunJobs();
            var ww = Assert.IsType<TimeWindowPromptWindow>(owner.OwnedWindows.Single());
            Assert.Equal("Set Time Window", ww.Title);
            Assert.Equal(("001:00:00:00", "001:00:01:00"), (ww.StartField.Text, ww.StopField.Text));
            Assert.Equal("DDD:HH:MM:SS", ww.StartField.PlaceholderText);
            ww.StopField.Text = "001:00:00:30";
            Press(ww.OkButton);
            Assert.Equal(("001:00:00:00", "001:00:00:30"), await Closed(window, "the time window"));

            Task<Color?> colour = prompts.PickColorAsync(Colors.Teal);
            Dispatcher.UIThread.RunJobs();
            var cw = Assert.IsType<ColorPromptWindow>(owner.OwnedWindows.Single());
            Assert.Equal("Choose Series Color", cw.Title);
            Assert.Equal(Colors.Teal, cw.Picker.Color);
            cw.Picker.Color = Color.FromArgb(10, 1, 2, 3);
            Press(cw.OkButton);
            Assert.Equal(Color.FromRgb(1, 2, 3), await Closed(colour, "the colour prompt"));
            owner.Close();
        }

        [AvaloniaFact]
        public async Task APromptWithNoWindowToSitOverIsCancelled()
        {
            var prompts = new WindowPlotPrompts(new Border());
            Assert.Null(await prompts.AskTextAsync("t", "l", "x"));
            Assert.Null(await prompts.AskNumberAsync("t", "l", 1, 0, 2, 0));
            Assert.Null(await prompts.AskTimeWindowAsync("a", "b"));
            Assert.Null(await prompts.PickColorAsync(Colors.Red));
            Assert.False(await prompts.CustomizeAsync(new CustomizeViewModel(LockPlot(), prompts)));
        }

        [AvaloniaFact]
        public async Task CustomizeViewShowsItsRowsAndClosesTrueOnOkFalseOnCancel()
        {
            Window owner = Owner();
            var prompts = new WindowPlotPrompts((Control)owner.Content!);
            var c = new CustomizeViewModel(Mixed(), prompts);
            Task<bool> shown = prompts.CustomizeAsync(c);
            Dispatcher.UIThread.RunJobs();
            var w = Assert.IsType<CustomizeViewWindow>(owner.OwnedWindows.Single());
            Assert.Equal("Customize Plot Series", w.Title);
            Assert.Equal(new[] { "Frame Sync Lock Streams", "Receiver SNR Streams" },
                         w.FindControl<TabControl>("Tabs")!.Items.OfType<TabItem>().Select(t => (string)t.Header!));
            Assert.Contains(w.GetLogicalDescendants().OfType<TextBlock>(), t => t.Text == CustomizeViewModel.LockDescription);
            CheckBox[] boxes = w.FindControl<ItemsControl>("LockRows")!.GetLogicalDescendants().OfType<CheckBox>().ToArray();
            Assert.Equal(new[] { "CH 5", "CH 6" }, boxes.Select(b => (string)b.Content!));
            Assert.Equal(new[] { false, true }, boxes.Select(b => b.IsChecked == true));
            Assert.Equal(new[] { "Ch 5", "Ch 6" },
                         w.FindControl<ItemsControl>("LockRows")!.GetLogicalDescendants().OfType<TextBox>().Select(t => t.Text));
            boxes[1].IsChecked = false;
            Assert.False(c.LockRows[1].IsChecked);
            Press(w.FindControl<Button>("OkButton")!);
            Assert.True(await Closed(shown, "Customize View"));

            shown = prompts.CustomizeAsync(new CustomizeViewModel(Mixed(), prompts));
            Dispatcher.UIThread.RunJobs();
            Press(owner.OwnedWindows.OfType<CustomizeViewWindow>().Single().FindControl<Button>("CancelButton")!);
            Assert.False(await Closed(shown, "Customize View (Cancel)"));
            owner.Close();
        }

        [AvaloniaFact]
        public void CustomizeViewsSnrTabShowsTheStreamChosenByReceiverInTwoColumns()
        {
            Window owner = Owner();
            var c = new CustomizeViewModel(Mixed(), new FakePrompts());
            var w = new CustomizeViewWindow { DataContext = c };
            _ = w.ShowDialog<bool>(owner);
            Dispatcher.UIThread.RunJobs();
            var tabs = w.FindControl<TabControl>("Tabs")!;
            tabs.SelectedIndex = 1;
            Dispatcher.UIThread.RunJobs();
            ComboBox stream = w.FindControl<ComboBox>("SnrStream")!;
            Assert.Equal(2, stream.ItemCount);
            Assert.Same(c.SnrStreams[0], stream.SelectedItem);
            Expander[] left = w.FindControl<ItemsControl>("LeftReceivers")!.GetLogicalDescendants().OfType<Expander>().ToArray();
            Expander[] right = w.FindControl<ItemsControl>("RightReceivers")!.GetLogicalDescendants().OfType<Expander>().ToArray();
            Assert.Equal(2, left.Length);
            Assert.Single(right);
            Assert.Equal("Expand All", w.FindControl<Button>("SnrExpand")!.Content);
            CheckBox header = (CheckBox)left[0].Header!;
            Assert.Equal("RCVR 1", header.Content);
            header.IsChecked = false;
            Assert.All(c.SnrStreams[0].LeftReceivers[0].Channels, ch => Assert.False(ch.IsChecked));
            Press(w.FindControl<Button>("SnrExpand")!);
            Assert.True(left[0].IsExpanded);
            Assert.Equal("Collapse All", w.FindControl<Button>("SnrExpand")!.Content);
            CheckBox[] channels = left[0].GetLogicalDescendants().OfType<CheckBox>().Skip(1).ToArray();
            Assert.Equal(new[] { "L", "R", "C" }, channels.Select(b => (string)b.Content!));
            Assert.Equal(new[] { "Rename series…", "Change color…" },
                         channels[0].ContextMenu!.Items.OfType<MenuItem>().Select(i => (string)i.Header!));

            stream.SelectedIndex = 1;
            Dispatcher.UIThread.RunJobs();
            Assert.Same(c.SnrStreams[1], c.SelectedSnrStream);
            Assert.Single(w.FindControl<ItemsControl>("LeftReceivers")!.GetLogicalDescendants().OfType<Expander>());
            w.Close();
            owner.Close();
        }

        [AvaloniaFact]
        public void AClampedTimeWindowIsAWarningInTheLog()
        {
            var vm = new MainWindowViewModel();
            vm.Plot.SetSeries(LockStream(0, "Ch 5", 5, 0));
            vm.Plot.ApplyTimeWindow(-1, 1, "early", "b");
            LogEntry last = vm.Log.Entries[^1];
            Assert.Equal(LogLevel.Warning, last.Level);
            Assert.Equal("Start \"early\" is outside the file time range - clamped to file bounds.", last.Text);
        }

        [AvaloniaFact]
        public void TheMainWindowHoldsThePlotView()
        {
            var vm = new MainWindowViewModel();
            var window = new MainWindow { DataContext = vm };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            PlotView view = window.FindControl<PlotView>("PlotView")!;
            Assert.Same(vm.Plot, view.Plot);
            Assert.Same(vm.Plot, view.Chart.Plot);
            window.Close();
        }
    }
}
