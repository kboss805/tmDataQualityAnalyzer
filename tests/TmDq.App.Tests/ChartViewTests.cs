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
using Avalonia.Layout;
using Avalonia.LogicalTree;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.Platform;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels.Plot;
using TmDq.App.Views.Plot;
using Xunit;
using static TmDq.App.Tests.AroundTheChartSupport;

namespace TmDq.App.Tests;

/// <summary>
/// The chart's view (plan step 4.4): the legend, the chip bar, the right-click menu and the keys, read from the controls
/// and, where it is drawing that matters, from rendered frames.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class ChartViewTests
{
    public ChartViewTests() => NativeCore.EnsureLoaded();

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
    public void ThePlotViewIsTheChartWithItsControlsOverItAndNothingAround()
    {
        // No control row around the chart (US4.0, US4.1). The chart
        // fills the view, and every control the view holds is in the overlay over it.
        PlotViewModel plot = LockPlot();
        Shown s = Show(plot);
        Grid root = Assert.IsType<Grid>(s.View.Content);
        Assert.Equal(new Control[] { s.View.Chart, s.View.Overlay, s.View.LoadingLabel }, root.Children);
        Assert.Equal(s.View.Bounds.Size, s.View.Chart.Bounds.Size);
        Assert.Equal(s.View.Bounds.Size, s.View.Overlay.Bounds.Size);

        TemplatedControl[] controls = s.View.GetLogicalDescendants().OfType<TemplatedControl>().ToArray();
        Assert.Contains(s.View.ViewModeChip, controls);   // the walk reaches the chips
        Assert.All(controls, c => Assert.True(c.GetLogicalAncestors().Contains(s.View.Overlay), c.GetType().Name + " is outside the overlay"));
        Assert.False(s.View.LoadingLabel.IsHitTestVisible);
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
        Assert.Equal(3, Frames.InWindow(legend.Rows[1], s.Window).Top - Frames.InWindow(legend.Rows[0], s.Window).Bottom, 6);

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
    public void TheLegendIsBuiltAgainForWhatItListsAndNotWhenTheWindowOrAnAxisMoves()
    {
        PlotViewModel plot = LockPlot();
        Shown s = Show(plot);
        PlotLegend legend = s.View.Legend;
        Control row = legend.Rows[0];
        string Label() => ((Panel)legend.Rows[0]).Children.OfType<TextBlock>().Single().Text!;
        Assert.Equal("Ch 5", Label());

        // The window and the axes have nothing to do with the rows: a pan step must not rebuild them (twice a step, it
        // once did: every row, for the notifications of the window and of the properties it raised).
        double span = plot.XViewMax - plot.XViewMin;
        plot.SetXViewByUser(plot.XViewMin + span * 0.1, plot.XViewMax - span * 0.1);
        plot.ApplyTimeWindow(plot.XViewMin + span * 0.05, plot.XViewMax - span * 0.05, "a", "b");
        plot.ZoomBack();
        plot.SetLeftMinOverride(10.0);
        plot.ResetXRange();
        Dispatcher.UIThread.RunJobs();
        Assert.Same(row, legend.Rows[0]);

        // What it lists does rebuild them: a rename, the metric in view, a series hidden, the legend switched off.
        plot.RenameSeries(0, "Renamed");
        plot.CommitAppearance();
        Dispatcher.UIThread.RunJobs();
        Assert.NotSame(row, legend.Rows[0]);
        Assert.Equal("Renamed", Label());

        plot.SetLockAxisView(LockAxisView.MissedFrames);
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(1, legend.RowCount);
        Assert.Equal("Renamed", Label());   // the missed-frames series: a stream's two are renamed together

        plot.SetSeriesVisibleQuiet(1, false);
        plot.CommitAppearance();
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(0, legend.RowCount);
        Assert.False(legend.IsVisible);

        plot.SetSeriesVisibleQuiet(1, true);
        plot.CommitAppearance();
        Dispatcher.UIThread.RunJobs();
        Assert.True(legend.IsVisible);
        plot.LegendVisible = false;
        Dispatcher.UIThread.RunJobs();
        Assert.False(legend.IsVisible);
        plot.LegendVisible = true;
        Dispatcher.UIThread.RunJobs();
        Assert.True(legend.IsVisible);
        s.Window.Close();
    }

    [AvaloniaFact]
    public void TheDataLayerIsPaintedAgainForAWindowOrATitleAndNotForAPropertyItDoesNotDraw()
    {
        PlotViewModel plot = LockPlot();
        Shown s = Show(plot);
        PlotChart chart = s.View.Chart;
        Frames.Capture(s.Window);
        int renders = chart.DataLayerRenders;

        // The legend's switch is a property of the plot the chart does not draw, and so is a zoom's history: the cached
        // layer stays (a toggle with L on a heavy plot repainted every line).
        plot.LegendVisible = false;
        Frames.Capture(s.Window);
        plot.LegendVisible = true;
        Frames.Capture(s.Window);
        Assert.Equal(renders, chart.DataLayerRenders);

        // What it does draw: the window, and the title.
        double span = plot.XViewMax - plot.XViewMin;
        plot.SetXViewByUser(plot.XViewMin + span * 0.1, plot.XViewMax - span * 0.1);
        Frames.Capture(s.Window);
        Assert.Equal(renders + 1, chart.DataLayerRenders);
        plot.Title = "A different title";
        Frames.Capture(s.Window);
        Assert.Equal(renders + 2, chart.DataLayerRenders);
        s.Window.Close();
    }

    [AvaloniaFact]
    public void ADenseLegendIsCappedAndScrollsWithoutCoveringALabel()
    {
        PlotViewModel plot = SnrOnly(48);
        Shown s = Show(plot);
        PlotLegend legend = s.View.Legend;
        Assert.Equal(48, legend.RowCount);
        Assert.Equal(240, legend.Bounds.Height, 6);   // 60 % of the chart's 400
        Assert.True(legend.Scroll.Extent.Height > legend.Scroll.Viewport.Height);
        ScrollBar bar = legend.GetVisualDescendants().OfType<ScrollBar>().Single(b => b.Orientation == Orientation.Vertical);
        Assert.True(bar.IsVisible);
        Assert.False(legend.Scroll.AllowAutoHide);   // always shown, as v2.14.2's was
        Rect barBox = Frames.InWindow(bar, s.Window);
        Assert.Equal(PlotLegend.ScrollGutter, barBox.Width, 6);   // the theme's bar fills the gutter left for it
        foreach (Control row in legend.Rows)
        {
            Rect text = Frames.InWindow(((StackPanel)row).Children[1], s.Window);
            Assert.True(text.Right <= barBox.Left, $"a label ends at {text.Right}, under the scroll bar at {barBox.Left}");
        }

        // A drag on the scroll bar scrolls the rows and leaves the legend where it is.
        Point at = legend.Position;
        Rect thumb = Frames.InWindow(bar.GetVisualDescendants().OfType<Thumb>().Single(), s.Window);
        s.Window.MouseDown(thumb.Center, MouseButton.Left, RawInputModifiers.None);
        s.Window.MouseMove(thumb.Center + new Point(-100, 40), RawInputModifiers.LeftMouseButton);
        s.Window.MouseUp(thumb.Center + new Point(-100, 40), MouseButton.Left, RawInputModifiers.None);
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(at, legend.Position);
        Assert.False(legend.UserMoved);
        Assert.True(legend.Scroll.Offset.Y > 0);

        // So wide a name is capped too, at a fraction of the chart's width.
        plot.SetSeries(new[] { Snr(0, 40, 0, 1, 0, new string('W', 300)) });
        plot.SetSeries(new[] { PlotFixtures.Lock(0, new string('W', 300), 1, 0, X3, X3) });
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(270, legend.Bounds.Width, 6);   // 45 % of the chart's 600
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

        // Off the other corner, it stops a margin inside that one too.
        grab = s.At(legend);
        s.Window.MouseDown(grab, MouseButton.Left, RawInputModifiers.None);
        s.Window.MouseMove(grab + new Point(2000, -2000), RawInputModifiers.LeftMouseButton);
        s.Window.MouseUp(grab + new Point(2000, -2000), MouseButton.Left, RawInputModifiers.None);
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(new Point(600 - legend.Bounds.Width - PlotLegend.EdgeMargin, PlotLegend.EdgeMargin), legend.Position);
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
    public void ALegendSwatchIsInTheColourTheLineIsDrawnInAndFollowsAChangeOfTheme()
    {
        Color yellow = PlotColors.Snr[2];
        var plot = new PlotViewModel();
        plot.SetSeries(new[] { PlotFixtures.Snr(0, "yellow", 1, 0, 1, 0, new[] { 0.0, 2.0 }, new[] { 50.0, 50.0 }) });
        plot.RecolorSeries(0, yellow);
        Shown s = Show(plot);
        Color Swatch() => ((ImmutableSolidColorBrush)((Border)((Panel)s.View.Legend.Rows[0]).Children[0]).Background!).Color;
        Assert.Equal(ChartPalette.Light.SeriesColor(yellow), Swatch());
        Assert.NotEqual(yellow, Swatch());
        // A row built after the view is shown (a recolour rebuilds them) is in the theme's version of its colour from the start.
        Color pale = Color.FromRgb(250, 250, 150);
        plot.RecolorSeries(0, pale);
        plot.CommitAppearance();
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(ChartPalette.Light.SeriesColor(pale), Swatch());
        Assert.NotEqual(pale, Swatch());
        plot.RecolorSeries(0, yellow);
        plot.CommitAppearance();
        Dispatcher.UIThread.RunJobs();
        s.Window.RequestedThemeVariant = ThemeVariant.Dark;
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(yellow, Swatch());
        s.Window.RequestedThemeVariant = ThemeVariant.Light;
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(ChartPalette.Light.SeriesColor(yellow), Swatch());
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
        Assert.Equal(ChartPalette.Light.LegendBackground, ((ImmutableSolidColorBrush)legend.Background!).Color);
        Assert.Equal(185, ChartPalette.Light.LegendBackground.A);

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

    private static MenuItem Item(ItemsControl menu, string header) => Menus.Item(menu, header);

    private static string[] Headers(ItemsControl menu) => Menus.Headers(menu);

    // What a click on the item runs, with its parameter; an item that asks the user is waited for.
    private static async Task Choose(MenuItem item)
    {
        Assert.True(item.Command!.CanExecute(item.CommandParameter), item.Header + " cannot run");
        if (item.Command is IAsyncRelayCommand asyncCommand) await asyncCommand.ExecuteAsync(item.CommandParameter);
        else item.Command.Execute(item.CommandParameter);
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void TheMenuLeadsWithTheTitleAndHoldsEveryPlotControlAndNoExport()
    {
        Shown s = Show(LockPlot(), prompts: new FakePrompts());
        ContextMenu menu = Menus.Open(s.View);
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
        s.Window.Close();

        // Without a plot there is nothing to bind to, and no menu.
        Shown bare = Show(null);
        Assert.Null(bare.View.Commands);
        Assert.False(bare.View.ChipBar.IsVisible);
        Assert.False(bare.View.LoadingLabel.IsVisible);
        Point p = bare.At(300, 200);
        bare.Window.MouseDown(p, MouseButton.Right, RawInputModifiers.None);
        bare.Window.MouseUp(p, MouseButton.Right, RawInputModifiers.None);
        Dispatcher.UIThread.RunJobs();
        Assert.False(bare.View.ChartMenu.IsOpen);
        bare.Window.Close();
    }

    [AvaloniaFact]
    public void TheMenuAdvertisesEveryKeyOfThePlot()
    {
        Shown s = Show(LockPlot(), prompts: new FakePrompts());
        ContextMenu menu = Menus.Open(s.View);
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
        ContextMenu empty = Menus.Open(s.View);
        foreach (string h in new[] { "Set Plot Title...", "Plot File", "View Mode  (V)", "Customize View...", "Show Legend", "Readout", "X Axis", "Y Axes", "Reset View" })
            Assert.False(Item(empty, h).IsEffectivelyEnabled, h + " is enabled without data");
        Assert.Empty(Item(empty, "Readout").Items);

        plot.SetSeries(LockStream(0, "Ch 5", 5, 0));
        ContextMenu menu = Menus.Open(s.View);
        foreach (string h in new[] { "Set Plot Title...", "View Mode  (V)", "Customize View...", "Show Legend", "Readout", "X Axis", "Y Axes", "Reset View" })
            Assert.True(Item(menu, h).IsEffectivelyEnabled, h + " is disabled with data");
        // One metric only: nothing to switch to.
        plot.SetSeries(SnrOnly().Series.ToArray());
        Assert.False(Item(Menus.Open(s.View), "View Mode  (V)").IsEffectivelyEnabled);
        s.Window.Close();
    }

    [AvaloniaFact]
    public async Task TheMenuSetsTheViewModeAndTheLegend()
    {
        PlotViewModel plot = LockPlot();
        Shown s = Show(plot, prompts: new FakePrompts());
        ContextMenu menu = Menus.Open(s.View);
        MenuItem lockItem = Item(Item(menu, "View Mode  (V)"), "Lock Percentage");
        MenuItem accum = Item(Item(menu, "View Mode  (V)"), "Accumulation");
        Assert.Equal(MenuItemToggleType.Radio, accum.ToggleType);
        Assert.True(lockItem.IsChecked);
        Assert.False(accum.IsChecked);
        await Choose(accum);
        Assert.Equal(LockAxisView.MissedFrames, plot.LockAxisView);
        Assert.True(Item(Item(Menus.Open(s.View), "View Mode  (V)"), "Accumulation").IsChecked);
        await Choose(Item(Item(Menus.Open(s.View), "View Mode  (V)"), "Lock Percentage"));
        Assert.Equal(LockAxisView.LockPercent, plot.LockAxisView);

        MenuItem legend = Item(menu, "Show Legend");
        Assert.Equal(MenuItemToggleType.CheckBox, legend.ToggleType);
        Assert.True(legend.IsChecked);
        await Choose(legend);
        Assert.False(plot.LegendVisible);
        Assert.False(s.View.Legend.IsVisible);
        Assert.False(Item(Menus.Open(s.View), "Show Legend").IsChecked);
        await Choose(Item(Menus.Open(s.View), "Show Legend"));
        Assert.True(s.View.Legend.IsVisible);
        s.Window.Close();
    }

    [AvaloniaFact]
    public async Task TheMenusAxisItemsResetZoomBackAndAsk()
    {
        PlotViewModel plot = LockPlot();
        var prompts = new FakePrompts();
        Shown s = Show(plot, prompts: prompts);

        Assert.False(Item(Item(Menus.Open(s.View), "X Axis"), "Zoom Back").IsEffectivelyEnabled);
        plot.ApplyBandZoom(0.2, 1.8);
        MenuItem back = Item(Item(Menus.Open(s.View), "X Axis"), "Zoom Back");
        Assert.True(back.IsEffectivelyEnabled);
        await Choose(back);
        Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
        Assert.False(plot.CanZoomBack);

        plot.SetXViewByUser(0.5, 1.5);
        plot.SetLeftMinOverride(10);
        plot.SetLeftMaxOverride(42);
        plot.SetRightMinOverride(2);
        plot.SetRightMaxOverride(24);
        ContextMenu menu = Menus.Open(s.View);
        await Choose(Item(Item(menu, "X Axis"), "Reset Span"));
        Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
        Assert.True(plot.HasRangeOverride);   // the span only
        plot.SetXViewByUser(0.5, 1.5);
        await Choose(Item(Item(menu, "Y Axes"), "Reset"));
        Assert.False(plot.HasRangeOverride);
        Assert.Equal((0.5, 1.5), (plot.XViewMin, plot.XViewMax));   // the limits only

        plot.SetXViewByUser(0.5, 1.5);
        plot.SetRightMaxOverride(24);
        await Choose(Item(Menus.Open(s.View), "Reset View"));
        Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
        Assert.False(plot.HasRangeOverride);

        // Each asking item reaches its question.
        menu = Menus.Open(s.View);
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
        MenuItem readout = Item(Menus.Open(s.View), "Readout");
        Assert.Equal(new[] { "Nearest series (automatic)", "-", "Ch 5", plot.Series[2].Name }, Headers(readout));
        Assert.True(Item(readout, "Nearest series (automatic)").IsChecked);
        Assert.All(readout.Items.OfType<MenuItem>(), i => Assert.Equal(MenuItemToggleType.Radio, i.ToggleType));

        await Choose(Item(readout, plot.Series[2].Name));
        Assert.Equal(2, plot.ReadoutSeriesId);
        readout = Item(Menus.Open(s.View), "Readout");
        Assert.True(Item(readout, plot.Series[2].Name).IsChecked);
        Assert.False(Item(readout, "Nearest series (automatic)").IsChecked);
        await Choose(Item(readout, "Nearest series (automatic)"));
        Assert.Null(plot.ReadoutSeriesId);

        // A pin to a series since hidden is dropped when the menu opens, so it opens with automatic chosen.
        plot.ReadoutSeriesId = 2;
        plot.SetSeriesVisibleQuiet(2, false); plot.CommitAppearance();
        readout = Item(Menus.Open(s.View), "Readout");
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
        int reachedWindow = 0;
        s.Window.KeyDown += (_, _) => reachedWindow++;   // keys the plot did not take

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

        // Backspace steps back one zoom: to the window before the last key's.
        s.Key(Key.Back);
        Assert.Equal(0.16, plot.XViewMax - plot.XViewMin, 12);

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

        // Each key the plot used stopped there; one it has no use for goes on.
        Assert.Equal(0, reachedWindow);
        s.Key(Key.Q);
        Assert.Equal(1, reachedWindow);

        // "+" is Shift with = on most keyboards; any other key with a modifier is not the plot's (Ctrl+R is not R).
        plot.SetXViewByUser(0.4, 0.6);
        s.Key(Key.OemPlus, RawInputModifiers.Shift);
        Assert.Equal(0.16, plot.XViewMax - plot.XViewMin, 12);
        Assert.Equal(1, reachedWindow);
        s.Key(Key.R, RawInputModifiers.Control);
        s.Key(Key.L, RawInputModifiers.Control);
        Assert.Equal(0.16, plot.XViewMax - plot.XViewMin, 12);
        Assert.True(plot.LegendVisible);
        Assert.Equal(3, reachedWindow);
        s.Window.Close();
    }

    [AvaloniaFact]
    public void ThePlotsCommandsAreItsOwnAndLetGoOfAPlotReplaced()
    {
        PlotViewModel first = LockPlot();
        Shown s = Show(first, prompts: new FakePrompts());
        PlotCommands old = s.View.Commands!;
        Assert.Same(old, s.View.Root.DataContext);
        Assert.True(old.CanSwitchViewMode);

        PlotViewModel second = LockPlot();
        s.View.Plot = second;
        Assert.NotSame(old, s.View.Commands);
        Assert.Same(s.View.Commands, s.View.Root.DataContext);
        var told = new List<string?>();
        old.PropertyChanged += (_, e) => told.Add(e.PropertyName);
        first.SetLockAxisView(LockAxisView.MissedFrames);
        first.SetXViewByUser(0.5, 1.5);
        Assert.Empty(told);   // the old plot's changes reach nothing

        // The chip follows the plot shown, not the one replaced.
        Assert.Equal("Show Accumulation", s.View.ViewModeChip.Content);
        second.SetLockAxisView(LockAxisView.MissedFrames);
        Dispatcher.UIThread.RunJobs();
        Assert.Equal("Show Lock %", s.View.ViewModeChip.Content);

        // Prompts set later are the ones asked.
        var prompts = new FakePrompts();
        s.View.Prompts = prompts;
        s.View.Commands!.SetTitleCommand.Execute(null);
        Assert.Equal(new[] { "Set Plot Title" }, prompts.Asked);
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
}
