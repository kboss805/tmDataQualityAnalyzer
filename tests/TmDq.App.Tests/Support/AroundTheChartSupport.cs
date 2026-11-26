using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;
using TmDq.App.Services;
using TmDq.App.ViewModels.Plot;
using TmDq.App.Views.Plot;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// What the tests around the chart share: plots built from the fixtures, the prompts' fake, the shown view and the
/// windows the prompts open. Each of the classes that use them imports this one statically.
/// </summary>
internal static class AroundTheChartSupport
{
    internal static readonly double[] X3 = { 0.0, 1.0, 2.0 };

    /// <summary>A Frame Sync Lock stream as a run gives it: its lock series shown, its missed frames hidden.</summary>
    internal static PlotSeries[] LockStream(int firstId, string label, int order, int sequence, double[]? x = null)
    {
        x ??= X3;
        PlotSeries missed = PlotFixtures.Missed(firstId + 1, label, order, sequence, x, x.Select(v => v).ToArray());
        missed.Visible = false;
        return new[] { PlotFixtures.Lock(firstId, label, order, sequence, x, x.Select(v => 90 + v).ToArray()), missed };
    }

    internal static PlotSeries Snr(int id, int order, int sequence, int receiver, int channel, string? name = null) =>
        PlotFixtures.Snr(id, name ?? $"{order} - CH-01 2250.5MHZ AGC {"LRC"[Math.Min(channel, 2)]}_RCVR{receiver}", order, sequence,
                         receiver, channel, X3, new[] { 40.0 + receiver, 45.0, 50.0 });

    internal static PlotViewModel LockPlot()
    {
        var plot = new PlotViewModel();
        plot.SetSeries(LockStream(0, "Ch 5", 5, 0));
        return plot;
    }

    internal static PlotViewModel SnrOnly(int receivers = 1)
    {
        var plot = new PlotViewModel();
        plot.SetSeries(Enumerable.Range(0, receivers).Select(r => Snr(r, 40, 0, r + 1, 0)));
        return plot;
    }

    internal sealed class FakePrompts : IPlotPrompts
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

        public (string Title, string Label, IReadOnlyList<string> Choices, int Selected) LastChoice;

        public Task<int?> ChooseAsync(string title, string label, IReadOnlyList<string> choices, int selected)
        {
            Asked.Add(title);
            LastChoice = (title, label, choices, selected);
            return Task.FromResult((int?)Next());
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

    internal static PlotViewModel Mixed()
    {
        var plot = new PlotViewModel();
        var series = new List<PlotSeries>();
        // Ch 6 comes first in the plot, but its channel is after Ch 5's.
        series.AddRange(LockStream(0, "Ch 6", 6, 0));
        series.AddRange(LockStream(2, "Ch 5", 5, 1));
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

    internal sealed record Shown(PlotView View, Window Window)
    {
        public Point At(Control c) => Frames.InWindow(c, Window).Center;
        public Point At(double x, double y) => Frames.InWindow(View, Window).TopLeft + new Point(x, y);
        public void Click(Control c)
        {
            Window.MouseDown(At(c), MouseButton.Left, RawInputModifiers.None);
            Window.MouseUp(At(c), MouseButton.Left, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
        }
        public void Key(Key key, RawInputModifiers modifiers = RawInputModifiers.None)
        {
            Window.KeyPress(key, modifiers, PhysicalKey.None, null);
            Window.KeyRelease(key, modifiers, PhysicalKey.None, null);
            Dispatcher.UIThread.RunJobs();
        }
    }

    internal static Shown Show(PlotViewModel? plot, double width = 600, double height = 400, IPlotPrompts? prompts = null)
    {
        var view = new PlotView { Width = width, Height = height, Plot = plot };
        if (prompts is not null) view.Prompts = prompts;
        var window = new Window { Content = view, SizeToContent = SizeToContent.WidthAndHeight };
        TextOptions.SetTextRenderingMode(window, TextRenderingMode.Antialias);
        window.Show();
        Dispatcher.UIThread.RunJobs();
        return new Shown(view, window);
    }

    internal static Window Owner()
    {
        var owner = new Window { Width = 400, Height = 300, Content = new Border() };
        owner.Show();
        Dispatcher.UIThread.RunJobs();
        return owner;
    }

    internal static void Press(Button b)
    {
        // A bound command runs from the button's own click handling, which a raised Click event does not reach.
        if (b.Command is not null) b.Command.Execute(b.CommandParameter);
        else b.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Dispatcher.UIThread.RunJobs();
    }

    internal static async Task<T> Closed<T>(Task<T> dialog, string what)
    {
        Assert.True(dialog.IsCompleted, what + " is still open.");
        return await dialog;
    }
}
