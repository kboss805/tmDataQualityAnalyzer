using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels.Plot;
using Xunit;
using static TmDq.App.Tests.AroundTheChartSupport;

namespace TmDq.App.Tests;

/// <summary>
/// Customize View's state (plan step 4.4): its rows, the order they are given in and what a choice in it does to the plot.
/// </summary>
public sealed class CustomizeViewStateTests
{
    public CustomizeViewStateTests() => NativeCore.EnsureLoaded();

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
        Assert.Equal(plot.SeriesById(11)!.Color, s40.LeftReceivers[0].Color);   // channel L of RCVR 1
        Assert.Equal(true, s40.LeftReceivers[0].IsChecked);
        Assert.Equal("CH 40 — 40 - CH-01 2250.5MHZ AGC C_RCVR1", s40.ToString());

        // A stream is checked when any of its series is shown, its missed frames as much as its lock; a hidden SNR
        // channel is unchecked, and its receiver partly checked.
        plot.SetLockAxisView(LockAxisView.MissedFrames);   // Ch 6 now shows only its missed frames
        plot.SetSeriesVisibleQuiet(18, false); plot.CommitAppearance();                  // RCVR 3's channel R
        var again = new CustomizeViewModel(plot, new FakePrompts());
        Assert.True(again.LockRows[1].IsChecked);
        Assert.False(again.SnrStreams[0].RightReceivers[0].Channels[1].IsChecked);
        Assert.Null(again.SnrStreams[0].RightReceivers[0].IsChecked);

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
        r1.IsChecked = false;        // a click on a partly checked box checks them all, as v2.14.2's did
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
    public void SelectNoneClearsPartlyCheckedReceiversInOnePressAndSelectAllChecksThem()
    {
        // Some receivers partly checked, one wholly, one not at all: the states a person leaves them in. Select None used the
        // receiver's own box, whose click on a partly checked one CHECKS it, so those came out fully checked and it took a
        // second press to clear them.
        var c = new CustomizeViewModel(Mixed(), new FakePrompts());
        SnrStreamGroup stream = c.SnrStreams[0];
        ReceiverGroup[] receivers = stream.Receivers.ToArray();
        Assert.True(receivers.Length >= 3);
        receivers[0].Channels[1].IsChecked = false;          // partly
        receivers[1].SetAll(false);                          // none
        receivers[2].SetAll(true);                           // all
        Assert.Equal(new bool?[] { null, false, true }, receivers.Take(3).Select(r => r.IsChecked));

        // Each receiver says its box changed, or the box in the window would keep showing the dash.
        var said = new List<string?>();
        receivers[0].PropertyChanged += (_, e) => said.Add(e.PropertyName);
        c.SelectNoneSnrCommand.Execute(null);
        Assert.Contains(nameof(ReceiverGroup.IsChecked), said);
        Assert.All(receivers, r => Assert.Equal(false, r.IsChecked));
        Assert.All(receivers.SelectMany(r => r.Channels), ch => Assert.False(ch.IsChecked));

        receivers[0].Channels[0].IsChecked = true;           // partly again, the other way round
        Assert.Null(receivers[0].IsChecked);
        c.SelectAllSnrCommand.Execute(null);
        Assert.All(receivers, r => Assert.Equal(true, r.IsChecked));

        // The group's own box keeps v2.x's rule for a click.
        receivers[0].Channels[1].IsChecked = false;
        receivers[0].IsChecked = false;
        Assert.Equal(true, receivers[0].IsChecked);
    }

    [Fact]
    public void SelectAllAndNoneAndExpandActOnTheirTabAndTheStreamShown()
    {
        var c = new CustomizeViewModel(Mixed(), new FakePrompts());
        Assert.Equal(new[] { false, true }, c.LockRows.Select(r => r.IsChecked));   // each way to start with
        c.SelectAllLockCommand.Execute(null);
        Assert.All(c.LockRows, r => Assert.True(r.IsChecked));
        c.SelectNoneLockCommand.Execute(null);
        Assert.All(c.LockRows, r => Assert.False(r.IsChecked));

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
        said.Clear();
        c.SnrStreams[0].LeftReceivers[0].IsExpanded = false;   // one collapsed by hand, as its expander does
        Assert.Equal("Expand All", c.ExpandText);
        Assert.Contains(nameof(CustomizeViewModel.ExpandText), said);
        c.ToggleExpandCommand.Execute(null);
        Assert.All(c.SnrStreams[0].Receivers, r => Assert.True(r.IsExpanded));
        c.ToggleExpandCommand.Execute(null);
        Assert.All(c.SnrStreams[0].Receivers, r => Assert.False(r.IsExpanded));
        said.Clear();
        c.SelectedSnrStream = c.SnrStreams[1];
        Assert.Contains(nameof(CustomizeViewModel.ExpandText), said);
    }

    [Fact]
    public void AStreamRowsBoxAndNameEachSayTheyChanged()
    {
        var c = new CustomizeViewModel(Mixed(), new FakePrompts());
        var said = new List<string?>();
        c.LockRows[0].PropertyChanged += (_, e) => said.Add(e.PropertyName);
        c.LockRows[0].IsChecked = !c.LockRows[0].IsChecked;
        c.LockRows[0].Name = "Five";
        Assert.Equal(new[] { nameof(LockStreamRow.IsChecked), nameof(LockStreamRow.Name) }, said);
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
        var rowSaid = new List<string?>();
        c.LockRows[0].PropertyChanged += (_, e) => rowSaid.Add(e.PropertyName);
        prompts.Answers.Enqueue(Colors.Teal);
        await c.LockRows[0].PickColorCommand.ExecuteAsync(null);
        Assert.Equal(plot.SeriesById(2)!.Color, prompts.LastColor);   // the dialog opens on the stream's colour
        Assert.Equal(Colors.Teal, c.LockRows[0].Color);
        Assert.Contains(nameof(LockStreamRow.Color), rowSaid);   // the swatch repaints
        await c.LockRows[1].PickColorCommand.ExecuteAsync(null);   // cancelled
        Assert.Equal(plot.SeriesById(0)!.Color, c.LockRows[1].Color);

        ChannelItem ch = c.SnrStreams[0].LeftReceivers[0].Channels[0];
        ch.IsChecked = false;
        var chSaid = new List<string?>();
        ch.PropertyChanged += (_, e) => chSaid.Add(e.PropertyName);
        prompts.Answers.Enqueue("renamed");
        await ch.RenameCommand.ExecuteAsync(null);
        Assert.Equal("renamed", ch.ToolTip);
        Assert.Contains(nameof(ChannelItem.ToolTip), chSaid);
        prompts.Answers.Enqueue(Colors.Olive);
        await ch.RecolorCommand.ExecuteAsync(null);
        Assert.Equal(Colors.Olive, ch.Color);
        Assert.Contains(nameof(ChannelItem.Color), chSaid);   // the name repaints
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

    /// <summary>Two recordings of the same streams, as Add Recording or a batch leaves them: each has CH 5 (its lock series shown,
    /// its missed frames hidden) and CH 40's receiver 1 (L, R and C).</summary>
    private static PlotViewModel TwoRecordings()
    {
        var series = new List<PlotSeries>();
        foreach ((int source, int first) in new[] { (0, 0), (1, 5) })
        {
            series.Add(PlotFixtures.Make(first, "Ch 5", TmDq.Core.MetricType.FrameSyncLock, 5, 0, X3, new[] { 90.0, 91.0, 92.0 }, source: source));
            PlotSeries missed = PlotFixtures.Make(first + 1, "Ch 5", TmDq.Core.MetricType.AccumulatedMissedFrames, 5, 0, X3, new[] { 0.0, 1.0, 2.0 }, source: source);
            missed.Visible = false;
            series.Add(missed);
            for (int c = 0; c < 3; c++)
                series.Add(PlotFixtures.Make(first + 2 + c, "40 - RX", TmDq.Core.MetricType.Snr, 40, 1, X3, new[] { 40.0, 45.0, 50.0 }, receiver: 1, channel: c, source: source));
        }
        var plot = new PlotViewModel();
        plot.SetSeries(series);
        plot.SetSourceLabel(0, "flight 1");
        plot.SetSourceLabel(1, "flight 2");
        return plot;
    }

    [Fact]
    public void WithSeveralRecordingsEachRecordingsStreamsAreRowsAndGroupsOfTheirOwnNamedForIt()
    {
        var c = new CustomizeViewModel(TwoRecordings(), new FakePrompts());

        Assert.Equal(new[] { "flight 1: CH 5", "flight 2: CH 5" }, c.LockRows.Select(r => r.Label));
        Assert.Equal(new[] { 0, 1 }, c.LockRows[0].SeriesIds);
        Assert.Equal(new[] { 5, 6 }, c.LockRows[1].SeriesIds);
        Assert.Equal(new[] { "flight 1: CH 40 — 40 - RX", "flight 2: CH 40 — 40 - RX" }, c.SnrStreams.Select(s => s.Label));
        Assert.Equal(new[] { 2, 3, 4 }, c.SnrStreams[0].Receivers.SelectMany(r => r.Channels).Select(ch => ch.SeriesId));
        Assert.Equal(new[] { 7, 8, 9 }, c.SnrStreams[1].Receivers.SelectMany(r => r.Channels).Select(ch => ch.SeriesId));

        // One recording left: the dialog reads as it always did, with no recording's name.
        PlotViewModel one = TwoRecordings();
        one.RemoveSource(1);
        var alone = new CustomizeViewModel(one, new FakePrompts());
        Assert.Equal(new[] { "CH 5" }, alone.LockRows.Select(r => r.Label));
        Assert.Equal(new[] { "CH 40 — 40 - RX" }, alone.SnrStreams.Select(s => s.Label));
    }

    [Fact]
    public void ARowsNameColourAndBoxReachOnlyItsOwnRecordingsStream()
    {
        PlotViewModel plot = TwoRecordings();
        Color before = plot.SeriesById(0)!.Color;
        var c = new CustomizeViewModel(plot, new FakePrompts());
        c.LockRows[0].IsChecked = false;      // flight 1's CH 5 hidden
        c.LockRows[1].Name = "Second CH 5";   // flight 2's renamed and recoloured
        c.LockRows[1].Color = Colors.Teal;
        c.SnrStreams[1].Receivers.First().Channels[0].IsChecked = false;   // flight 2's L

        c.Apply();

        Assert.False(plot.SeriesById(0)!.Visible);
        Assert.True(plot.SeriesById(5)!.Visible);
        Assert.Equal(new[] { "Ch 5", "Ch 5", "Second CH 5", "Second CH 5" }, new[] { 0, 1, 5, 6 }.Select(id => plot.SeriesById(id)!.Name));
        Assert.Equal(new[] { before, before, Colors.Teal, Colors.Teal }, new[] { 0, 1, 5, 6 }.Select(id => plot.SeriesById(id)!.Color));
        Assert.True(plot.SeriesById(2)!.Visible);    // flight 1's L stays
        Assert.False(plot.SeriesById(7)!.Visible);
    }

    [Fact]
    public void SwitchingTheViewCarriesEachRecordingsStreamOverOnItsOwn()
    {
        PlotViewModel plot = TwoRecordings();
        plot.SetSeriesVisibleQuiet(5, false);   // flight 2's CH 5 hidden; flight 1's, of the same channel, shown

        plot.SetLockAxisView(LockAxisView.MissedFrames);

        Assert.True(plot.SeriesById(1)!.Visible);    // flight 1's missed frames
        Assert.False(plot.SeriesById(6)!.Visible);   // flight 2's stay hidden with its lock
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
}
