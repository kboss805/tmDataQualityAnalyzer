using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;

namespace TmDq.App.Views
{
    /// <summary>One of a list (not editable): a label, the list with the first chosen,
    /// OK and Cancel. Closes with the index chosen, or null on Cancel.</summary>
    public sealed class ChoiceWindow : Window
    {
        public ChoiceWindow(string title, string label, IReadOnlyList<string> choices)
        {
            Title = title;
            SizeToContent = SizeToContent.WidthAndHeight;
            CanResize = false;
            ShowInTaskbar = false;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            Choices = new ComboBox { ItemsSource = choices, SelectedIndex = choices.Count > 0 ? 0 : -1, MinWidth = 320 };
            OkButton = new Button { Content = "OK", IsDefault = true, MinWidth = 80, HorizontalContentAlignment = HorizontalAlignment.Center };
            CancelButton = new Button { Content = "Cancel", IsCancel = true, MinWidth = 80, HorizontalContentAlignment = HorizontalAlignment.Center };
            OkButton.Click += (_, _) => Close(Choices.SelectedIndex >= 0 ? Choices.SelectedIndex : (int?)null);
            CancelButton.Click += (_, _) => Close((int?)null);
            var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right };
            buttons.Children.Add(OkButton);
            buttons.Children.Add(CancelButton);
            var body = new StackPanel { Margin = new Thickness(16), Spacing = 10 };
            body.Children.Add(new TextBlock { Text = label });
            body.Children.Add(Choices);
            body.Children.Add(buttons);
            Content = body;
        }

        public ComboBox Choices { get; }

        public Button OkButton { get; }

        public Button CancelButton { get; }
    }
}
