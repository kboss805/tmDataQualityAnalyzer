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
/// The commands behind the chart's right-click menu (plan step 4.4), asked through a fake of the prompts.
/// </summary>
public sealed class ChartMenuCommandTests
{
    public ChartMenuCommandTests() => NativeCore.EnsureLoaded();

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
        await commands.SetTimeWindowAsync();   // cancelled
        plot.ZoomBack();                       // the one window remembered is the one before the entry
        Assert.Equal((0.5, 1.5), (plot.XViewMin, plot.XViewMax));
        Assert.False(plot.CanZoomBack);
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

    [Fact]
    public async Task TheDialogsOwnQuestionsAreAskedThroughWhatItIsGiven()
    {
        var c = new CustomizeViewModel(LockPlot(), new FakePrompts());
        var own = new FakePrompts();
        own.Answers.Enqueue(Colors.Teal);
        c.AskThrough(own);
        await c.LockRows[0].PickColorCommand.ExecuteAsync(null);
        Assert.Equal(Colors.Teal, c.LockRows[0].Color);
    }
}
