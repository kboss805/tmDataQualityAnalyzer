using System.Collections.Generic;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// The recordings a plot holds, on their own (<see cref="PlotSources"/>): their names, the one shown alone, the list in the order
/// first met, the id a new one takes, and what removing some does to the names, the one shown and the title. The plot's own
/// cases (<c>RemoveFileTests</c>, <c>TemplateTests</c>) hold what it does with them.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class PlotSourcesTests
{
    public PlotSourcesTests() => NativeCore.EnsureLoaded();

    private static PlotSeries Of(int source, int id = 0) =>
        PlotFixtures.Make(id, "s" + id, MetricType.Snr, 1, id, new[] { 0.0, 1.0 }, new[] { 1.0, 2.0 }, source: source);

    [Fact]
    public void TheListIsEachRecordingOnceInTheOrderFirstMetNamedOrNumbered()
    {
        var series = new List<PlotSeries> { Of(3, 0), Of(1, 1), Of(3, 2) };
        var sources = new PlotSources(series);
        sources.Name(3, "first");

        Assert.Equal(new[] { (3, "first"), (1, "Source 1") }, sources.List());
        Assert.Equal("Source 9", sources.Label(9));   // a name before any series: a recording being added
        series.Add(Of(7, 3));                          // read as it is each time, never copied
        Assert.Equal(3, sources.List().Count);
    }

    [Fact]
    public void ANewRecordingTakesOnePastTheGreatestIdAndAnEmptyPlotStartsAtZero()
    {
        var series = new List<PlotSeries>();
        var sources = new PlotSources(series);
        Assert.Equal(0, sources.NextId);
        series.Add(Of(4));
        series.Add(Of(2));
        Assert.Equal(5, sources.NextId);
    }

    [Fact]
    public void ShowingOneNarrowsWhatIsShownAndMinusOneShowsThemAll()
    {
        var sources = new PlotSources(new List<PlotSeries>());
        Assert.Equal(-1, sources.Shown);
        Assert.True(sources.IsShown(Of(1)) && sources.IsShown(Of(2)));
        sources.Show(2);
        Assert.Equal((false, true), (sources.IsShown(Of(1)), sources.IsShown(Of(2))));
        sources.Show(-1);
        Assert.True(sources.IsShown(Of(1)));
    }

    [Fact]
    public void ForgettingRecordingsDropsTheirNamesAndSaysWhetherWhatIsDrawnChanged()
    {
        var sources = new PlotSources(new List<PlotSeries>());
        sources.Name(1, "a");
        sources.Name(2, "b");
        sources.Name(3, "c");

        Assert.True(sources.Forget(new[] { 1 }));                    // all overlaid: what is drawn changed
        Assert.Equal("Source 1", sources.Label(1));
        Assert.Equal("b", sources.Label(2));

        sources.Show(3);
        Assert.False(sources.Forget(new[] { 2 }));                   // another one shown: the view can stay
        Assert.Equal(3, sources.Shown);

        Assert.True(sources.Forget(new[] { 3 }));                    // the one shown went: all are overlaid again
        Assert.Equal(-1, sources.Shown);
    }

    [Fact]
    public void OnlyNamesGivenAreNamesAndATitleThatNamedOneRemovedNamesTheFirstLeft()
    {
        var series = new List<PlotSeries> { Of(0, 0), Of(1, 1) };
        var sources = new PlotSources(series);
        sources.Name(0, "flight 1");
        sources.Name(1, "flight 2");

        HashSet<string> names = sources.NamesOf(new[] { 1, 5 });
        Assert.Equal(new[] { "flight 2" }, names);                   // 5 was never named, so has none

        series.RemoveAt(1);
        sources.Forget(new[] { 1 });
        Assert.Equal("flight 1", sources.TitleAfterRemoving("flight 2", names));
        Assert.Equal("my own title", sources.TitleAfterRemoving("my own title", names));
    }

    [Fact]
    public void ResetForgetsEveryNameAndShowsThemAllAndForgetNamesKeepsTheOneShown()
    {
        var sources = new PlotSources(new List<PlotSeries>());
        sources.Name(1, "a");
        sources.Show(1);

        sources.ForgetNames();
        Assert.Equal(("Source 1", 1), (sources.Label(1), sources.Shown));

        sources.Name(1, "a");
        sources.Reset();
        Assert.Equal(("Source 1", -1), (sources.Label(1), sources.Shown));
    }
}
