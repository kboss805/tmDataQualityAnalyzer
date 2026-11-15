using System.Collections.Generic;
using Avalonia.Controls;

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
            OkButton = new Button { Content = "OK", IsDefault = true };
            CancelButton = new Button { Content = "Cancel", IsCancel = true };
            OkButton.Click += (_, _) => Close(Choices.SelectedIndex >= 0 ? Choices.SelectedIndex : (int?)null);
            CancelButton.Click += (_, _) => Close((int?)null);
            var body = new StackPanel { Spacing = 8 };
            body.Children.Add(new TextBlock { Text = label });
            body.Children.Add(Choices);
            Content = DialogFooter.Compose(body, OkButton, CancelButton);
        }

        public ComboBox Choices { get; }

        public Button OkButton { get; }

        public Button CancelButton { get; }
    }
}
