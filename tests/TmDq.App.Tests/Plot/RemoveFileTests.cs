using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;
using static TmDq.App.Tests.AroundTheChartSupport;

namespace TmDq.App.Tests;

/// <summary>
/// Remove File: every series of one recording, batch file or imported CSV leaves the plot, Customize View, the readout and
/// the next CSV export, and the other recordings' stay. It is the plot's own (<see cref="PlotViewModel.RemoveSource"/>)
/// and the right-click menu's Remove File... (<see cref="PlotCommands.RemoveFileAsync"/>), which asks which one.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class RemoveFileTests : IDisposable
{
    private readonly string _dir = TempFiles.NewPath("removefile");

    public RemoveFileTests()
    {
        NativeCore.EnsureLoaded();
        Directory.CreateDirectory(_dir);
    }

    public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);

    /// <summary>Three recordings: 0 ("flight 1", 0 to 2 s), 1 ("flight 2", 5 to 9 s) and 2 ("imported", 0 to 20 s), one SNR
    /// series each.</summary>
    private static PlotViewModel ThreeRecordings()
    {
        var plot = new PlotViewModel();
        plot.SetSeries(new[]
        {
            PlotFixtures.Make(0, "a", MetricType.Snr, 1, 0, new[] { 0.0, 2.0 }, new[] { 10.0, 20.0 }, source: 0),
            PlotFixtures.Make(1, "b", MetricType.Snr, 1, 1, new[] { 5.0, 9.0 }, new[] { 40.0, 60.0 }, source: 1),
            PlotFixtures.Make(2, "c", MetricType.Snr, 1, 2, new[] { 0.0, 20.0 }, new[] { 12.0, 14.0 }, source: 2),
        });
        plot.SetSourceLabel(0, "flight 1");
        plot.SetSourceLabel(1, "flight 2");
        plot.SetSourceLabel(2, "imported");
        return plot;
    }

    [Fact]
    public void ARecordingsSeriesAndItsNameGoAndTheOthersStay()
    {
        PlotViewModel plot = ThreeRecordings();
        int told = 0;
        plot.SeriesChanged += (_, _) => told++;

        Assert.True(plot.RemoveSource(1));

        Assert.Equal(new[] { 0, 2 }, plot.Series.Select(s => s.SourceId));
        Assert.Equal(new[] { (0, "flight 1"), (2, "imported") }, plot.SourceList());
        Assert.True(plot.HasData);
        Assert.True(told > 0);   // the view and the menus are told

        // A recording that is not there changes nothing and tells no one.
        told = 0;
        Assert.False(plot.RemoveSource(1));
        Assert.False(plot.RemoveSource(99));
        Assert.Equal((0, 2), (told, plot.Series.Count));

        // Its name went with it: a recording that comes later by the same id is not called "flight 2".
        plot.SetSeries(plot.Series.Append(PlotFixtures.Make(5, "d", MetricType.Snr, 1, 3, new[] { 1.0, 2.0 }, new[] { 1.0, 2.0 }, source: 1)).ToList());
        Assert.Equal(new[] { (0, "flight 1"), (2, "imported"), (1, "Source 1") }, plot.SourceList());
    }

    [Fact]
    public void RemovingTheRecordingShownShowsEveryOtherOneAndFitsTheWindowToThem()
    {
        PlotViewModel plot = ThreeRecordings();
        plot.SetVisibleSource(1);
        Assert.Equal((5.0, 9.0), (plot.XViewMin, plot.XViewMax));
        int told = 0;
        plot.PropertyChanged += (_, e) => told += e.PropertyName == nameof(PlotViewModel.VisibleSource) ? 1 : 0;

        plot.RemoveSource(1);

        Assert.Equal(-1, plot.VisibleSource);   // none is left to show alone: every one is overlaid
        Assert.Equal(1, told);
        Assert.Equal((0.0, 20.0), (plot.XViewMin, plot.XViewMax));
        Assert.All(plot.Series, s => Assert.True(plot.EffectiveVisible(s)));
    }

    [Fact]
    public void RemovingAnotherRecordingLeavesTheOneShownAndItsView()
    {
        PlotViewModel plot = ThreeRecordings();
        plot.SetVisibleSource(0);
        plot.ApplyTimeWindow(0.5, 1.5, "0.5", "1.5");
        (double min, double max) = (plot.XViewMin, plot.XViewMax);
        Assert.NotEqual((0.0, 2.0), (min, max));   // a zoomed view, which a refit would lose

        plot.RemoveSource(2);

        Assert.Equal(0, plot.VisibleSource);
        Assert.Equal((min, max), (plot.XViewMin, plot.XViewMax));
        Assert.Equal(new[] { (0, "flight 1"), (1, "flight 2") }, plot.SourceList());
    }

    [Fact]
    public void WithEveryRecordingOverlaidTheWindowSpansThoseLeft()
    {
        PlotViewModel plot = ThreeRecordings();
        Assert.Equal((0.0, 20.0), (plot.XViewMin, plot.XViewMax));

        plot.RemoveSource(2);   // the widest one

        Assert.Equal(-1, plot.VisibleSource);
        Assert.Equal((0.0, 9.0), (plot.XViewMin, plot.XViewMax));
    }

    [Fact]
    public void TheOnlyRecordingLeavesAnEmptyPlotLikeANewOne()
    {
        var plot = new PlotViewModel();
        plot.SetSeries(new[] { PlotFixtures.Make(0, "a", MetricType.Snr, 1, 0, new[] { 0.0, 2.0 }, new[] { 10.0, 20.0 }, source: 3) });
        plot.SetSourceLabel(3, "only");
        string untitled = plot.Title;
        plot.Title = "my flight";
        plot.ReadoutSeriesId = 0;

        Assert.True(plot.RemoveSource(3));

        Assert.False(plot.HasData);
        Assert.Empty(plot.Series);
        Assert.Empty(plot.SourceList());
        Assert.Equal(-1, plot.VisibleSource);
        Assert.Equal(untitled, plot.Title);   // as Clear leaves it: the title, the view and the readout pin start again
        Assert.Null(plot.ReadoutSeriesId);
    }

    [Fact]
    public void TheNextCsvExportHasOnlyWhatWasKept()
    {
        PlotViewModel plot = ThreeRecordings();
        string before = Path.Combine(_dir, "before.csv"), after = Path.Combine(_dir, "after.csv");
        Assert.True(plot.ExportCsv(before));
        Assert.Equal(3, File.ReadAllLines(before)[0].Split(',').Length - 1);

        plot.RemoveSource(1);
        Assert.True(plot.ExportCsv(after));

        string[] header = File.ReadAllLines(after)[0].Split(',');
        Assert.Equal(2, header.Length - 1);   // the time column and two series
        Assert.Contains("40", File.ReadAllText(before), StringComparison.Ordinal);   // flight 2's values were in it
        Assert.DoesNotContain("40", File.ReadAllText(after), StringComparison.Ordinal);   // flight 2's values are nowhere in it
    }

    [Fact]
    public void AReadoutPinnedToARemovedSeriesIsDropped()
    {
        PlotViewModel plot = ThreeRecordings();
        plot.ReadoutSeriesId = 1;

        plot.RemoveSource(1);

        Assert.Null(plot.ReadoutSeriesId);

        plot.ReadoutSeriesId = 2;
        plot.RemoveSource(0);
        Assert.Equal(2, plot.ReadoutSeriesId);   // a pin on a series that stays is kept
    }

    // ---- the menu item ----------------------------------------------------------------------------------------------

    [Fact]
    public async Task RemoveFileAsksWhichRecordingAndRemovesTheOneChosen()
    {
        PlotViewModel plot = ThreeRecordings();
        var prompts = new FakePrompts();
        prompts.Answers.Enqueue(1);   // "flight 2"
        using var commands = new PlotCommands(plot, prompts);

        await commands.RemoveFileCommand.ExecuteAsync(null);

        Assert.Equal("Remove File", prompts.LastChoice.Title);
        Assert.Equal(new[] { "flight 1", "flight 2", "imported" }, prompts.LastChoice.Choices);
        Assert.Contains("keep the others", prompts.LastChoice.Label, StringComparison.Ordinal);
        Assert.Equal(new[] { (0, "flight 1"), (2, "imported") }, plot.SourceList());
    }

    [Fact]
    public async Task CancelRemovesNothingAndOneRecordingIsAskedAboutAsTheWholePlot()
    {
        PlotViewModel plot = ThreeRecordings();
        var prompts = new FakePrompts();
        prompts.Answers.Enqueue(null);   // Cancel
        using (var commands = new PlotCommands(plot, prompts))
        {
            await commands.RemoveFileCommand.ExecuteAsync(null);
        }
        Assert.Equal(3, plot.Series.Count);

        var single = new PlotViewModel();
        single.SetSeries(new[] { PlotFixtures.Make(0, "a", MetricType.Snr, 1, 0, new[] { 0.0, 2.0 }, new[] { 10.0, 20.0 }) });
        var singlePrompts = new FakePrompts();
        singlePrompts.Answers.Enqueue(0);
        using var singleCommands = new PlotCommands(single, singlePrompts);
        await singleCommands.RemoveFileCommand.ExecuteAsync(null);
        Assert.Contains("everything of this file", singlePrompts.LastChoice.Label, StringComparison.Ordinal);
        Assert.False(single.HasData);
    }

    [Fact]
    public async Task ClearPlotAsksOnceWithNothingChosenAndOnYesEmptiesThePlotWithItsTitle()
    {
        PlotViewModel plot = ThreeRecordings();
        plot.Title = "three";
        var prompts = new FakePrompts();
        prompts.Answers.Enqueue(0);   // "Everything on the plot"
        using var commands = new PlotCommands(plot, prompts);

        await commands.ClearPlotCommand.ExecuteAsync(null);

        Assert.Equal("Clear Plot", prompts.LastChoice.Title);
        Assert.Equal(new[] { "Everything on the plot" }, prompts.LastChoice.Choices);
        Assert.Equal(-1, prompts.LastChoice.Selected);          // nothing to press Enter on: it removes every series
        Assert.Contains("All 3 files", prompts.LastChoice.Label, StringComparison.Ordinal);
        Assert.False(plot.HasData);
        Assert.Empty(plot.SourceList());
        Assert.Equal(PlotViewModel.DefaultTitle, plot.Title);   // the title goes with what it named
    }

    [Fact]
    public async Task CancellingClearPlotChangesNothingAndOneFileIsAskedAboutInTheSingular()
    {
        PlotViewModel plot = ThreeRecordings();
        var prompts = new FakePrompts();
        prompts.Answers.Enqueue(null);   // Cancel
        using (var commands = new PlotCommands(plot, prompts))
        {
            await commands.ClearPlotCommand.ExecuteAsync(null);
        }
        Assert.Equal(3, plot.Series.Count);
        Assert.Equal(3, plot.SourceList().Count);

        var single = new PlotViewModel();
        single.SetSeries(new[] { PlotFixtures.Make(0, "a", MetricType.Snr, 1, 0, new[] { 0.0, 2.0 }, new[] { 10.0, 20.0 }) });
        var singlePrompts = new FakePrompts();
        singlePrompts.Answers.Enqueue(0);
        using var singleCommands = new PlotCommands(single, singlePrompts);
        await singleCommands.ClearPlotCommand.ExecuteAsync(null);
        Assert.Contains("Its one file", singlePrompts.LastChoice.Label, StringComparison.Ordinal);
        Assert.False(single.HasData);
    }

    [Fact]
    public async Task ClearPlotIsUnavailableWithoutData()
    {
        var prompts = new FakePrompts();
        using var commands = new PlotCommands(new PlotViewModel(), prompts);
        Assert.False(commands.ClearPlotCommand.CanExecute(null));
        await commands.ClearPlotAsync();   // asked directly, it still asks nothing

        Assert.Empty(prompts.Asked);
    }

    [Fact]
    public async Task RemoveFileIsUnavailableWithoutData()
    {
        var prompts = new FakePrompts();
        using var commands = new PlotCommands(new PlotViewModel(), prompts);
        Assert.False(commands.RemoveFileCommand.CanExecute(null));
        await commands.RemoveFileAsync();   // asked directly, it still asks nothing

        Assert.Empty(prompts.Asked);
    }

    // ---- what keeps a record of the recordings, the title, and what Remove File starts on ----------------------------------

    [Fact]
    public void SeveralRecordingsRemovedAtOnceAreRedrawnOnceAndEachIsTold()
    {
        PlotViewModel plot = ThreeRecordings();
        int redrawn = 0;
        var told = new List<int>();
        plot.SeriesChanged += (_, _) => redrawn++;
        plot.SourceRemoved += (_, id) => told.Add(id);

        Assert.True(plot.RemoveSources(new[] { 1, 0, 1, 99 }));   // a repeat and one that is not there are passed over

        Assert.Equal(1, redrawn);                                 // one redraw for both, not one each
        Assert.Equal(new[] { 1, 0 }, told);
        Assert.Equal(new[] { (2, "imported") }, plot.SourceList());
        Assert.Equal((0.0, 20.0), (plot.XViewMin, plot.XViewMax));
        Assert.False(plot.RemoveSources(new[] { 98, 99 }));
    }

    [Fact]
    public void ThePlotTellsWhichRecordingWasRemovedOnlyWhenOneWas()
    {
        PlotViewModel plot = ThreeRecordings();
        var told = new List<int>();
        plot.SourceRemoved += (_, id) => told.Add(id);

        plot.RemoveSource(99);
        Assert.Empty(told);

        plot.RemoveSource(1);
        plot.RemoveSource(0);
        plot.RemoveSource(2);   // the last one: the plot is cleared, and it is told as well

        Assert.Equal(new[] { 1, 0, 2 }, told);
    }

    [Fact]
    public void TheTitleThatNamedARemovedRecordingNamesTheFirstOneLeftAndAWrittenTitleStays()
    {
        PlotViewModel plot = ThreeRecordings();
        plot.Title = "flight 2";   // as a recording's title is: its name

        plot.RemoveSource(1);

        Assert.Equal("flight 1", plot.Title);

        plot.Title = "my own title";
        plot.RemoveSource(0);

        Assert.Equal("my own title", plot.Title);
        Assert.Equal("imported", Assert.Single(plot.SourceList()).Label);
    }

    [Fact]
    public async Task RemoveFileStartsOnNothingChosenWithSeveralOverlaidOnTheOneShownAndOnTheOnlyOne()
    {
        PlotViewModel plot = ThreeRecordings();
        var prompts = new FakePrompts();
        prompts.Answers.Enqueue(null);
        prompts.Answers.Enqueue(null);
        prompts.Answers.Enqueue(null);
        using var commands = new PlotCommands(plot, prompts);

        await commands.RemoveFileCommand.ExecuteAsync(null);
        Assert.Equal(-1, prompts.LastChoice.Selected);          // Enter must not remove the first by accident

        plot.SetVisibleSource(1);
        await commands.RemoveFileCommand.ExecuteAsync(null);
        Assert.Equal(1, prompts.LastChoice.Selected);           // the one being looked at

        var single = new PlotViewModel();
        single.SetSeries(new[] { PlotFixtures.Make(0, "a", MetricType.Snr, 1, 0, new[] { 0.0, 2.0 }, new[] { 10.0, 20.0 }) });
        var singlePrompts = new FakePrompts();
        singlePrompts.Answers.Enqueue(null);
        using var singleCommands = new PlotCommands(single, singlePrompts);
        await singleCommands.RemoveFileCommand.ExecuteAsync(null);
        Assert.Equal(0, singlePrompts.LastChoice.Selected);     // the only choice there is
    }

    [Avalonia.Headless.XUnit.AvaloniaFact]
    public void TheChoiceWindowHoldsOkUntilSomethingIsChosenWhenNothingStartsChosen()
    {
        var none = new TmDq.App.Views.Dialogs.ChoiceWindow("Remove File", "Which?", new[] { "a", "b" }, -1);
        Assert.Equal(-1, none.Choices.SelectedIndex);
        Assert.False(none.OkButton.IsEnabled);
        none.Choices.SelectedIndex = 1;
        Assert.True(none.OkButton.IsEnabled);

        var first = new TmDq.App.Views.Dialogs.ChoiceWindow("Import CSV", "Which?", new[] { "a", "b" });   // as every other use
        Assert.Equal(0, first.Choices.SelectedIndex);
        Assert.True(first.OkButton.IsEnabled);

        var empty = new TmDq.App.Views.Dialogs.ChoiceWindow("Nothing", "Which?", System.Array.Empty<string>());
        Assert.False(empty.OkButton.IsEnabled);
    }
}
