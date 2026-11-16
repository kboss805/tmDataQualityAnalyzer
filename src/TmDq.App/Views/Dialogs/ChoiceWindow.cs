using System.Collections.Generic;
using Avalonia.Controls;

namespace TmDq.App.Views.Dialogs;

/// <summary>One of a list (not editable): a label, the list with the first chosen,
/// OK and Cancel. Closes with the index chosen, or null on Cancel.</summary>
public sealed class ChoiceWindow : PromptWindow<int?>
{
    public ChoiceWindow(string title, string label, IReadOnlyList<string> choices) : base(title)
    {
        Choices = new ComboBox { ItemsSource = choices, SelectedIndex = choices.Count > 0 ? 0 : -1, MinWidth = 320 };
        var body = new StackPanel { Spacing = 8 };
        body.Children.Add(new TextBlock { Text = label });
        body.Children.Add(Choices);
        Lay(body);
    }

    public ComboBox Choices { get; }

    protected override int? Answer() => Choices.SelectedIndex >= 0 ? Choices.SelectedIndex : null;
}
