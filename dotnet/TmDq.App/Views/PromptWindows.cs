using System;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using TmDq.App.Plot;

namespace TmDq.App.Views
{
    /// <summary>A small modal question with OK and Cancel (Enter and Escape), closing with its answer or null: the base of the
    /// plot's prompts (plan step 4.4), the Qt <c>QInputDialog</c> and <c>QColorDialog</c>.</summary>
    public abstract class PromptWindow<T> : Window
    {
        protected PromptWindow(string title)
        {
            Title = title;
            SizeToContent = SizeToContent.WidthAndHeight;
            CanResize = false;
            ShowInTaskbar = false;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            OkButton = new Button { Content = "OK", IsDefault = true, MinWidth = 80, HorizontalContentAlignment = HorizontalAlignment.Center };
            CancelButton = new Button { Content = "Cancel", IsCancel = true, MinWidth = 80, HorizontalContentAlignment = HorizontalAlignment.Center };
            OkButton.Click += (_, _) => Close(Answer());
            CancelButton.Click += (_, _) => Close(null);
        }

        public Button OkButton { get; }

        public Button CancelButton { get; }

        /// <summary>The answer OK gives.</summary>
        protected abstract T Answer();

        /// <summary>The window's content: <paramref name="body"/> above the buttons, at the right.</summary>
        protected void Lay(Control body)
        {
            var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right };
            buttons.Children.Add(OkButton);
            buttons.Children.Add(CancelButton);
            var root = new StackPanel { Margin = new Thickness(16), Spacing = 12 };
            root.Children.Add(body);
            root.Children.Add(buttons);
            Content = root;
        }
    }

    /// <summary>A line of text.</summary>
    public sealed class TextPromptWindow : PromptWindow<string?>
    {
        public TextPromptWindow(string title, string label, string initial) : base(title)
        {
            Field = new TextBox { Text = initial, MinWidth = 300 };
            var body = new StackPanel { Spacing = 6 };
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
            Field = new NumericUpDown
            {
                Minimum = (decimal)_min,
                Maximum = (decimal)_max,
                Value = (decimal)PlotCommands.SpinValue(value, min, max, decimals),
                Increment = 1,
                FormatString = "F" + decimals.ToString(CultureInfo.InvariantCulture),
                MinWidth = 200,
            };
            var body = new StackPanel { Spacing = 6 };
            body.Children.Add(new TextBlock { Text = label });
            body.Children.Add(Field);
            Lay(body);
            Opened += (_, _) => Field.Focus();
        }

        public NumericUpDown Field { get; }

        protected override double? Answer() => PlotCommands.SpinValue((double)(Field.Value ?? (decimal)_min), _min, _max, _decimals);
    }

    /// <summary>The Set Time Window dialog: a start and a stop as DDD:HH:MM:SS.</summary>
    public sealed class TimeWindowPromptWindow : PromptWindow<(string Start, string Stop)?>
    {
        public TimeWindowPromptWindow(string start, string stop) : base("Set Time Window")
        {
            StartField = new TextBox { Text = start, PlaceholderText = "DDD:HH:MM:SS", MinWidth = 180 };
            StopField = new TextBox { Text = stop, PlaceholderText = "DDD:HH:MM:SS", MinWidth = 180 };
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
}
