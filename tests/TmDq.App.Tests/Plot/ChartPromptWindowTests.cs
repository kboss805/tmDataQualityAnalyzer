using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Headless.XUnit;
using Avalonia.LogicalTree;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using TmDq.App.Models.Logging;
using TmDq.App.Models.Processing;
using TmDq.App.Platform;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Plot;
using TmDq.App.Views;
using TmDq.App.Views.Dialogs;
using TmDq.App.Views.Plot;
using Xunit;
using static TmDq.App.Tests.AroundTheChartSupport;

namespace TmDq.App.Tests;

/// <summary>
/// The windows of the chart's prompts (plan step 4.4): each answers on OK and nothing on Cancel.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class ChartPromptWindowTests
{
    public ChartPromptWindowTests() => NativeCore.EnsureLoaded();

    [AvaloniaTheory]
    [InlineData(0, "12", true, null)]
    [InlineData(0, " -3 ", true, null)]
    [InlineData(0, "12.5", false, "Not a whole number.")]
    [InlineData(0, "1e1", false, "Not a whole number.")]
    [InlineData(0, "twelve", false, "Not a whole number.")]
    [InlineData(0, "", false, "Enter a whole number.")]
    [InlineData(1, "12.54", true, null)]
    [InlineData(1, ".5", true, null)]
    [InlineData(1, "1e1", true, null)]
    [InlineData(1, "1,5", false, "Not a number.")]
    [InlineData(1, "NaN", false, "Not a number.")]
    [InlineData(1, "-", false, "Not a number.")]
    public void TheNumberPromptHasNoArrowsAndRefusesTextThatIsNotANumber(int decimals, string typed, bool accepted, string? problem)
    {
        var w = Frames.Shown(new NumberPromptWindow("Set", "Value:", 5, 0, 99, decimals));
        Assert.Empty(w.GetVisualDescendants().OfType<NumericUpDown>());
        Assert.Empty(w.GetVisualDescendants().OfType<ButtonSpinner>());
        Assert.True(w.OkButton.IsEnabled);
        Assert.False(w.Problem.IsVisible);
        w.Field.Text = typed;
        Assert.Equal(accepted, w.OkButton.IsEnabled);
        Assert.Equal(!accepted, w.Problem.IsVisible);
        if (!accepted) Assert.Equal(problem, w.Problem.Text);   // the words, as given: not rebuilt from the rule they come from
        w.Field.Text = "7";
        Assert.True(w.OkButton.IsEnabled);
        Assert.False(w.Problem.IsVisible);
        w.Close();
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
        Assert.Equal("12", nw.Field.Text);
        nw.Field.Text = "150";   // past its maximum: kept inside it
        Press(nw.OkButton);
        Assert.Equal(99.0, await Closed(number, "the number prompt"));

        number = prompts.AskNumberAsync("t", "l", 1, -999, 49, 1);
        Dispatcher.UIThread.RunJobs();
        nw = (NumberPromptWindow)owner.OwnedWindows.Single();
        Assert.Equal("1.0", nw.Field.Text);
        nw.Field.Text = "-12.54";
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
        // The picker is drawn by its own theme, which the application includes: without it there is nothing to pick.
        Assert.NotEmpty(cw.Picker.GetVisualDescendants().OfType<ColorSpectrum>());
        cw.Picker.Color = Color.FromArgb(10, 1, 2, 3);
        Press(cw.OkButton);
        Assert.Equal(Color.FromRgb(1, 2, 3), await Closed(colour, "the colour prompt"));
        owner.Close();
    }

    [AvaloniaFact]
    public async Task ThePlotViewAsksInWindowsOverItsOwn()
    {
        PlotViewModel plot = LockPlot();
        Shown s = Show(plot);
        // What the application hands the view: until it does, every question is answered "cancel".
        Assert.IsType<NoPlotPrompts>(s.View.Prompts);
        s.View.Prompts = new WindowPlotPrompts(s.View);
        Task title = new PlotCommands(plot, s.View.Prompts).SetTitleAsync();
        Dispatcher.UIThread.RunJobs();
        var w = Assert.IsType<TextPromptWindow>(s.Window.OwnedWindows.Single());
        Press(w.CancelButton);
        Assert.True(title.IsCompleted, "the title prompt is still open.");
        await title;
        s.Window.Close();
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
        Assert.Equal("Customize View", w.Title);
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
        Assert.All(w.FindControl<TabControl>("Tabs")!.Items.OfType<TabItem>(), t => Assert.Equal(13, t.FontSize));

        // A row's swatch is in its colour, and opens the colour dialog over this one rather than the main window.
        Button swatch = w.FindControl<ItemsControl>("LockRows")!.GetLogicalDescendants().OfType<Button>().First(b => b is not ToggleButton);
        Assert.Equal(c.LockRows[0].Color, ((ISolidColorBrush)swatch.Background!).Color);
        Assert.Same(c.LockRows[0].PickColorCommand, swatch.Command);
        Press(swatch);
        var picker = Assert.IsType<ColorPromptWindow>(w.OwnedWindows.Single());
        Press(picker.CancelButton);
        Assert.Empty(w.OwnedWindows);
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
        // Each receiver's name, and each channel's, written in its series' colour.
        Assert.Equal(c.SnrStreams[0].LeftReceivers[0].Color, ((ISolidColorBrush)header.Foreground!).Color);
        Assert.Equal(c.SnrStreams[0].LeftReceivers[1].Channels[0].Color, ((ISolidColorBrush)((CheckBox)left[1].Header!).Foreground!).Color);
        header.IsChecked = false;
        Assert.All(c.SnrStreams[0].LeftReceivers[0].Channels, ch => Assert.False(ch.IsChecked));
        Press(w.FindControl<Button>("SnrExpand")!);
        Assert.True(left[0].IsExpanded);
        Assert.Equal("Collapse All", w.FindControl<Button>("SnrExpand")!.Content);
        CheckBox[] channels = left[0].GetLogicalDescendants().OfType<CheckBox>().Skip(1).ToArray();
        Assert.Equal(new[] { "L", "R", "C" }, channels.Select(b => (string)b.Content!));
        MenuItem[] items = channels[0].ContextMenu!.Items.OfType<MenuItem>().ToArray();
        Assert.Equal(new[] { "Rename series…", "Change color…" }, items.Select(i => (string)i.Header!));
        channels[0].ContextMenu!.Open(channels[0]);
        Dispatcher.UIThread.RunJobs();
        var channel = (ChannelItem)channels[0].DataContext!;
        Assert.Same(channel.RenameCommand, items[0].Command);
        Assert.Same(channel.RecolorCommand, items[1].Command);
        channels[0].ContextMenu!.Close();

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
