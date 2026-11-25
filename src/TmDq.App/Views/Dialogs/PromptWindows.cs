using System;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Markup.Xaml.MarkupExtensions;
using Avalonia.Media;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Plot;
using TmDq.App.Views.Controls;

namespace TmDq.App.Views.Dialogs;

/// <summary>A small modal question with OK and Cancel (Enter and Escape), closing with its answer or null: the base of the
/// plot's prompts (plan step 4.4): text, a number, a time window and a colour.</summary>
public abstract class PromptWindow<T> : DialogWindow
{
    protected PromptWindow(string title) : base(title)
    {
        OkButton = new Button { Content = "OK", IsDefault = true };
        CancelButton = new Button { Content = "Cancel", IsCancel = true };
        OkButton.Click += (_, _) => Close(Answer());
        CancelButton.Click += (_, _) => Close(null);
    }

    public Button OkButton { get; }

    public Button CancelButton { get; }

    /// <summary>The answer OK gives.</summary>
    protected abstract T Answer();

    /// <summary>The window's content: <paramref name="body"/> above the footer with the buttons.</summary>
    protected void Lay(Control body) => Content = DialogFooter.Compose(body, OkButton, CancelButton);
}

/// <summary>A line of text.</summary>
public sealed class TextPromptWindow : PromptWindow<string?>
{
    public TextPromptWindow(string title, string label, string initial) : base(title)
    {
        Field = new TextBox { Text = initial, MinWidth = 300 };
        AutomationProperties.SetName(Field, label.TrimEnd(':', ' '));
        var body = new StackPanel { Spacing = 8 };
        body.Children.Add(new TextBlock { Text = label });
        body.Children.Add(Field);
        Lay(body);
        Opened += (_, _) =>
        {
            Field.Focus();
            Field.SelectAll();
        };
    }

    public TextBox Field { get; }

    protected override string? Answer() => Field.Text ?? "";
}

/// <summary>A number in a range, to a number of decimals.</summary>
public sealed class NumberPromptWindow : PromptWindow<double?>
{
    private readonly double _min, _max;
    private readonly int _decimals;

    public NumberPromptWindow(string title, string label, double value, double min, double max, int decimals) : base(title)
    {
        _min = min;
        _max = Math.Max(min, max);
        _decimals = decimals;
        // A plain text box, no up/down arrows: text that is not a number (a whole number, when there are no decimals) is
        // said so and holds OK. A number outside the range is kept inside it, and rounded to the decimals, by SpinValue.
        Field = new TextBox { Text = NumberText.FormatFixed(PlotCommands.SpinValue(value, min, max, decimals), decimals), MinWidth = 200 };
        AutomationProperties.SetName(Field, label.TrimEnd(':', ' '));
        Problem = new TextBlock { IsVisible = false };
        Problem[!TextBlock.ForegroundProperty] = new DynamicResourceExtension("SystemFillColorCriticalBrush");
        Field.PropertyChanged += (_, e) =>
        {
            if (e.Property == TextBox.TextProperty) Check();
        };
        var body = new StackPanel { Spacing = 8 };
        body.Children.Add(new TextBlock { Text = label });
        body.Children.Add(Field);
        body.Children.Add(Problem);
        Lay(body);
        Opened += (_, _) =>
        {
            Field.Focus();
            Field.SelectAll();
        };
    }

    public TextBox Field { get; }

    /// <summary>Why OK is held, or hidden while the text is a number.</summary>
    public TextBlock Problem { get; }

    private double? Entered => _decimals == 0 ? NumberText.ParseInteger(Field.Text ?? "") : NumberText.ParseNumber(Field.Text ?? "");

    private void Check()
    {
        string text = Field.Text ?? "";
        string? problem = text.Trim().Length == 0 ? "Enter a " + (_decimals == 0 ? "whole " : "") + "number."
            : Entered is null ? "Not a " + (_decimals == 0 ? "whole " : "") + "number."
            : null;
        Problem.Text = problem ?? "";
        Problem.IsVisible = problem is not null;
        OkButton.IsEnabled = problem is null;
    }

    protected override double? Answer() => Entered is double v ? PlotCommands.SpinValue(v, _min, _max, _decimals) : null;
}

/// <summary>The Set Time Window dialog: a start and a stop as DDD:HH:MM:SS.</summary>
public sealed class TimeWindowPromptWindow : PromptWindow<(string Start, string Stop)?>
{
    public TimeWindowPromptWindow(string start, string stop) : base("Set Time Window")
    {
        StartField = new TextBox { Text = start, PlaceholderText = "DDD:HH:MM:SS", MinWidth = 180 };
        StopField = new TextBox { Text = stop, PlaceholderText = "DDD:HH:MM:SS", MinWidth = 180 };
        AutomationProperties.SetName(StartField, "Start");
        AutomationProperties.SetName(StopField, "Stop");
        var form = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*"), RowDefinitions = new RowDefinitions("Auto,Auto"), ColumnSpacing = 8, RowSpacing = 8 };
        Place(form, new TextBlock { Text = "Start:", VerticalAlignment = VerticalAlignment.Center }, 0, 0);
        Place(form, StartField, 0, 1);
        Place(form, new TextBlock { Text = "Stop:", VerticalAlignment = VerticalAlignment.Center }, 1, 0);
        Place(form, StopField, 1, 1);
        Lay(form);
        Opened += (_, _) => StartField.Focus();
    }

    public TextBox StartField { get; }

    public TextBox StopField { get; }

    protected override (string Start, string Stop)? Answer() => (StartField.Text ?? "", StopField.Text ?? "");

    private static void Place(Grid grid, Control c, int row, int column)
    {
        Grid.SetRow(c, row);
        Grid.SetColumn(c, column);
        grid.Children.Add(c);
    }
}

/// <summary>A colour: Avalonia's colour view (spectrum, palette, components and hex), without transparency.</summary>
public sealed class ColorPromptWindow : PromptWindow<Color?>
{
    public ColorPromptWindow(Color initial) : base("Choose Series Color")
    {
        Picker = new ColorView
        {
            Color = initial,
            IsAlphaEnabled = false,
            IsAlphaVisible = false,
            Width = 360,
            Height = 420,
        };
        Lay(Picker);
    }

    public ColorView Picker { get; }

    protected override Color? Answer() => Color.FromRgb(Picker.Color.R, Picker.Color.G, Picker.Color.B);
}
