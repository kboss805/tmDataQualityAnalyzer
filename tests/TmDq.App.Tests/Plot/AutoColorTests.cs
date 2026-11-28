using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels.Plot;
using TmDq.App.Views.Plot;
using TmDq.Core;
using Xunit;
using static TmDq.App.Tests.AroundTheChartSupport;

namespace TmDq.App.Tests;

/// <summary>
/// The context menu's Auto-Color Visible Series and Reset Colors... (US4.0): the hues Auto-Color gives out and their order
/// (<see cref="PlotColors.AutoColorHues"/>), what it colours as one (<see cref="PlotViewModel.AutoColorVisible"/>), and
/// Reset Colors, which asks and gives every series its first colour again (<see cref="PlotCommands.ResetColorsAsync"/>).
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class AutoColorTests
{
    public AutoColorTests() => NativeCore.EnsureLoaded();

    private static string Rgb(Color c) => $"{c.R},{c.G},{c.B}";

    // ---- the hues --------------------------------------------------------------------------------------------------

    [Fact]
    public void AutoColorGivesTheFourteenHuesFarthestFirstAndNeverTheGold()
    {
        // The palette less the gold, the red first, then each time the hue farthest (in OKLab) from those already taken.
        Assert.Equal(new[]
        {
            "204,0,0", "70,160,255", "10,100,50", "120,40,190", "130,170,20", "255,140,0", "210,30,140",
            "30,90,220", "0,140,120", "160,80,20", "240,80,120", "0,170,200", "20,150,60", "70,50,200",
        }, PlotColors.AutoColorHues.Select(Rgb));
        Assert.Equal(PlotColors.StreamHues.Count - 1, PlotColors.AutoColorHues.Distinct().Count());
        Assert.DoesNotContain(PlotColors.AutoColorExcluded, PlotColors.AutoColorHues);
        Assert.Contains(PlotColors.AutoColorExcluded, PlotColors.StreamHues);   // the one left out is the palette's gold
        // The 15th unit takes the first hue again, the 16th the second.
        Assert.Equal(PlotColors.AutoColorHues[0], PlotColors.AutoHue(14));
        Assert.Equal(PlotColors.AutoColorHues[1], PlotColors.AutoHue(15));
        Assert.Equal(PlotColors.AutoColorHues[13], PlotColors.AutoHue(13));
    }

    [Fact]
    public void EachHueTakenIsTheFarthestFromThoseBeforeIt()
    {
        // Checked from the definition, not from the list above: hue k's least distance to hues 0..k-1 is at least that of
        // every hue after it, and a later hue that is as far comes later in the palette.
        IReadOnlyList<Color> order = PlotColors.AutoColorHues;
        List<Color> pool = PlotColors.StreamHues.Where(h => h != PlotColors.AutoColorExcluded).ToList();
        Assert.Equal(pool[0], order[0]);
        for (int k = 1; k < order.Count; k++)
        {
            double Least(Color c) => order.Take(k).Min(t => PlotColors.OkDistance(c, t));
            for (int later = k + 1; later < order.Count; later++)
            {
                Assert.True(Least(order[k]) >= Least(order[later]),
                            $"hue {k} ({Rgb(order[k])}) is nearer those before it than hue {later} ({Rgb(order[later])})");
                if (Least(order[k]) == Least(order[later])) Assert.True(pool.IndexOf(order[k]) < pool.IndexOf(order[later]));
            }
        }
        // The first two are far apart and every hue is clearly apart from those before it.
        Assert.All(Enumerable.Range(1, order.Count - 1),
                   k => Assert.True(order.Take(k).Min(t => PlotColors.OkDistance(order[k], t)) > 0.05, $"hue {k} is too near another"));
    }

    [Fact]
    public void EveryHueAutoColorGivesReadsOnBothThemesAndTheGoldDoesNot()
    {
        // "Work on both themes": each hue is drawn as it is on the light theme's white and on the dark theme's grey, at least
        // the contrast the light theme holds a series to; the gold is not on the white, which is why it is left out, and it
        // is the only one of the palette that is not.
        double least = ChartPalette.Light.MinSeriesContrast;
        Assert.Equal(2.0, least);
        foreach (Color hue in PlotColors.AutoColorHues)
        {
            Assert.True(ChartPalette.Contrast(hue, ChartPalette.Light.Background) >= least, Rgb(hue) + " on the light theme");
            Assert.True(ChartPalette.Contrast(hue, ChartPalette.Dark.Background) >= least, Rgb(hue) + " on the dark theme");
            Assert.Equal(hue, ChartPalette.Light.SeriesColor(hue));   // drawn as given, not darkened
        }
        Assert.True(ChartPalette.Contrast(PlotColors.AutoColorExcluded, ChartPalette.Light.Background) < least);
        Assert.NotEqual(PlotColors.AutoColorExcluded, ChartPalette.Light.SeriesColor(PlotColors.AutoColorExcluded));
    }

    // ---- what it colours ---------------------------------------------------------------------------------------------

    /// <summary>Two lock streams (Ch 6 shown; Ch 5 with nothing shown), then stream 40's three receivers of three channels
    /// (only each one's combined channel shown, receiver 2 not at all) and stream 41's one receiver of one channel.</summary>
    private static PlotViewModel Plot()
    {
        var plot = new PlotViewModel();
        var series = new List<PlotSeries>();
        series.AddRange(LockStream(0, "Ch 6", 6, 0));    // ids 0 (lock, shown) and 1 (missed, hidden)
        series.AddRange(LockStream(2, "Ch 5", 5, 1));    // ids 2 and 3
        int id = 10;
        foreach (int r in new[] { 1, 2, 3 })
            foreach (int c in new[] { 0, 1, 2 })
                series.Add(Snr(id++, 40, 2, r, c));      // receiver 1: 10..12, receiver 2: 13..15, receiver 3: 16..18
        series.Add(Snr(id, 41, 3, 1, 0));                // 19
        plot.SetSeries(series);
        foreach (PlotSeries s in plot.Series)
            if (s.Metric == MetricType.Snr && s.StreamOrder == 40) s.Visible = s.ChannelIndex == PlotColors.CombinedChannelIndex && s.ReceiverIndex != 2;
        plot.SeriesById(2)!.Visible = false;            // Ch 5: nothing drawn
        return plot;
    }

    private static Color ColorOf(PlotViewModel plot, int id) => plot.SeriesById(id)!.Color;

    [Fact]
    public void TheUnitsDrawnTakeTheHuesInPlotOrderAndTheRestKeepTheirs()
    {
        PlotViewModel plot = Plot();
        Dictionary<int, Color> before = plot.Series.ToDictionary(s => s.Id, s => s.Color);
        Dictionary<int, (bool, bool)> lines = plot.Series.ToDictionary(s => s.Id, s => (s.Dotted, s.Strong));
        int told = 0;
        plot.SeriesChanged += (_, _) => told++;

        plot.AutoColorVisible();

        Assert.Equal(1, told);   // one refresh, as Customize View's OK
        // Drawn, in plot order: Ch 6, receiver 1 of stream 40, its receiver 3, stream 41's receiver.
        Color ch6 = PlotColors.AutoHue(0), r1 = PlotColors.AutoHue(1), r3 = PlotColors.AutoHue(2), s41 = PlotColors.AutoHue(3);
        Assert.Equal(ch6, ColorOf(plot, 0));
        Assert.Equal(ch6, ColorOf(plot, 1));   // its missed frames, not shown, with it: the two are one stream
        // A receiver: the combined channel the hue, the others its lighter shades, shown or not.
        Assert.Equal(r1, ColorOf(plot, 12));
        Assert.Equal(PlotColors.Shade(r1, 1), ColorOf(plot, 10));
        Assert.Equal(PlotColors.Shade(r1, 2), ColorOf(plot, 11));
        Assert.Equal(r3, ColorOf(plot, 18));
        Assert.Equal(PlotColors.Shade(r3, 1), ColorOf(plot, 16));
        Assert.Equal(PlotColors.Shade(s41, 1), ColorOf(plot, 19));   // its one channel is its first (L)
        // Nothing drawn: Ch 5 and receiver 2 keep the colours they had, and take no hue from the others.
        foreach (int kept in new[] { 2, 3, 13, 14, 15 }) Assert.Equal(before[kept], ColorOf(plot, kept));
        // Solid and dotted, and the combined channel's weight, stay as they were.
        Assert.Equal(lines, plot.Series.ToDictionary(s => s.Id, s => (s.Dotted, s.Strong)));
    }

    [Fact]
    public void TwoRecordingsStreamsOfOneNumberAreTwoUnitsAndOnlyTheRecordingShownIsColoured()
    {
        // Stacked recordings: each one's stream 5 is first given the same colour (the colour goes by stream number), and
        // Auto-Color tells them apart.
        double[] x = { 0, 1 }, y = { 90, 91 };
        var plot = new PlotViewModel();
        plot.SetSeries(new[]
        {
            PlotFixtures.Make(0, "Ch 5", MetricType.FrameSyncLock, 5, 0, x, y, source: 0),
            PlotFixtures.Make(1, "Ch 5", MetricType.FrameSyncLock, 5, 1, x, y, source: 1),
            PlotFixtures.Make(2, "rx", MetricType.Snr, 40, 2, x, y, receiver: 1, channel: 2, source: 0),
            PlotFixtures.Make(3, "rx", MetricType.Snr, 40, 3, x, y, receiver: 1, channel: 2, source: 1),
        });
        Assert.Equal(ColorOf(plot, 0), ColorOf(plot, 1));
        Assert.Equal(ColorOf(plot, 2), ColorOf(plot, 3));

        plot.AutoColorVisible();
        Assert.Equal(new[] { PlotColors.AutoHue(0), PlotColors.AutoHue(1), PlotColors.AutoHue(2), PlotColors.AutoHue(3) },
                     plot.Series.OrderBy(s => s.Id).Select(s => s.Color));

        // With Plot File showing recording 1 alone, only its units are drawn: they take the first hues, recording 0's keep theirs.
        plot.SetVisibleSource(1);
        plot.AutoColorVisible();
        Assert.Equal(new[] { PlotColors.AutoHue(0), PlotColors.AutoHue(0), PlotColors.AutoHue(2), PlotColors.AutoHue(1) },
                     plot.Series.OrderBy(s => s.Id).Select(s => s.Color));
    }

    [Fact]
    public void WithNothingDrawnAutoColorChangesNothingAndTellsNoOne()
    {
        PlotViewModel plot = Plot();
        foreach (PlotSeries s in plot.Series) s.Visible = false;
        Dictionary<int, Color> before = plot.Series.ToDictionary(s => s.Id, s => s.Color);
        int told = 0;
        plot.SeriesChanged += (_, _) => told++;

        Assert.False(plot.CanAutoColor);
        plot.AutoColorVisible();

        Assert.Equal(0, told);
        Assert.Equal(before, plot.Series.ToDictionary(s => s.Id, s => s.Color));
        Assert.False(new PlotViewModel().CanAutoColor);
    }

    [Fact]
    public void TheMenusAutoColorAsksNothingAndWaitsForASeriesDrawn()
    {
        var prompts = new FakePrompts();
        using (var empty = new PlotCommands(new PlotViewModel(), prompts))
            Assert.False(empty.AutoColorCommand.CanExecute(null));

        PlotViewModel plot = Plot();
        using var commands = new PlotCommands(plot, prompts);
        Assert.True(commands.AutoColorCommand.CanExecute(null));
        commands.AutoColorCommand.Execute(null);
        Assert.Equal(PlotColors.AutoHue(0), ColorOf(plot, 0));
        Assert.Empty(prompts.Asked);

        // Hiding every series (Customize View's OK) disables it, and the menu is told so, as of every command it holds:
        // a bound item asks again only when told.
        int autoTold = 0, resetTold = 0;
        commands.AutoColorCommand.CanExecuteChanged += (_, _) => autoTold++;
        commands.ResetColorsCommand.CanExecuteChanged += (_, _) => resetTold++;
        foreach (PlotSeries s in plot.Series) plot.SetSeriesVisibleQuiet(s.Id, false);
        plot.CommitAppearance();
        Assert.False(commands.AutoColorCommand.CanExecute(null));
        Assert.True(autoTold > 0, "Auto-Color is not told that what is drawn changed");
        Assert.True(resetTold > 0, "Reset Colors is not told that the plot changed");
    }

    // ---- Reset Colors ---------------------------------------------------------------------------------------------------

    [Fact]
    public async Task ResetColorsAsksAndOnOkGivesEverySeriesItsFirstColour()
    {
        PlotViewModel plot = Plot();
        Dictionary<int, Color> first = plot.Series.ToDictionary(s => s.Id, s => s.Color);
        plot.AutoColorVisible();
        plot.RecolorSeries(2, Colors.Black);   // Ch 5, not drawn, by hand
        Assert.NotEqual(first, plot.Series.ToDictionary(s => s.Id, s => s.Color));
        var prompts = new FakePrompts();
        prompts.Answers.Enqueue(0);
        using var commands = new PlotCommands(plot, prompts);
        int told = 0;
        plot.SeriesChanged += (_, _) => told++;

        await commands.ResetColorsCommand.ExecuteAsync(null);

        Assert.Equal("Reset Colors", prompts.LastChoice.Title);
        Assert.Equal(new[] { "Every series on the plot" }, prompts.LastChoice.Choices);
        Assert.Equal(0, prompts.LastChoice.Selected);   // chosen: Enter confirms
        Assert.Contains("Customize View", prompts.LastChoice.Label, StringComparison.Ordinal);
        Assert.Equal(first, plot.Series.ToDictionary(s => s.Id, s => s.Color));   // hidden ones too
        Assert.Equal(1, told);
    }

    [Fact]
    public async Task CancellingResetColorsChangesNothingAndWithoutDataItIsUnavailable()
    {
        PlotViewModel plot = Plot();
        plot.AutoColorVisible();
        Dictionary<int, Color> auto = plot.Series.ToDictionary(s => s.Id, s => s.Color);
        var prompts = new FakePrompts();
        prompts.Answers.Enqueue(null);   // Cancel
        using (var commands = new PlotCommands(plot, prompts))
            await commands.ResetColorsCommand.ExecuteAsync(null);
        Assert.Equal(auto, plot.Series.ToDictionary(s => s.Id, s => s.Color));

        var none = new FakePrompts();
        using var empty = new PlotCommands(new PlotViewModel(), none);
        Assert.False(empty.ResetColorsCommand.CanExecute(null));
        await empty.ResetColorsAsync();   // asked directly, it still asks nothing
        Assert.Empty(none.Asked);
    }

    [Fact]
    public void ResetColorsGivesTheColoursAFreshPlotOfTheSeriesIsGiven()
    {
        // Whatever came before: a plot whose streams were recoloured is reset to what SetSeries gives the same series.
        PlotViewModel plot = Plot();
        foreach (PlotSeries s in plot.Series) plot.RecolorSeries(s.Id, Colors.Gray);
        plot.ResetColors();

        var fresh = new PlotViewModel();
        fresh.SetSeries(Plot().Series.Select(s => PlotFixtures.Make(s.Id, s.Name, s.Metric, s.StreamOrder, s.StreamSequence,
                                                                     s.X, s.Y, s.ReceiverIndex, s.ChannelIndex, s.SourceId)));
        Assert.Equal(fresh.Series.Select(s => (s.Id, s.Color, s.Dotted, s.Strong)), plot.Series.Select(s => (s.Id, s.Color, s.Dotted, s.Strong)));
        Assert.Equal(PlotColors.StreamHues[0], ColorOf(plot, 0));   // Ch 6, the first lock stream
    }
}
