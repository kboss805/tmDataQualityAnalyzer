using System.Collections.Generic;
using Avalonia.Automation;
using Avalonia.Controls;

namespace TmDq.App.Views.Dialogs;

/// <summary>One of a list (not editable): a label, the list with the first chosen,
/// OK and Cancel. Closes with the index chosen, or null on Cancel.</summary>
public sealed class ChoiceWindow : PromptWindow<int?>
{
    /// <param name="selected">The index that starts chosen: the first by default; -1 for none, and OK is held until one is.</param>
    public ChoiceWindow(string title, string label, IReadOnlyList<string> choices, int selected = 0) : base(title)
    {
        Choices = new ComboBox { ItemsSource = choices, SelectedIndex = selected < choices.Count ? selected : -1, MinWidth = 320 };
        OkButton.IsEnabled = Choices.SelectedIndex >= 0;
        Choices.SelectionChanged += (_, _) => OkButton.IsEnabled = Choices.SelectedIndex >= 0;
        AutomationProperties.SetName(Choices, label.TrimEnd(':', ' '));
        var body = new StackPanel { Spacing = 8 };
        body.Children.Add(new TextBlock { Text = label });
        body.Children.Add(Choices);
        Lay(body);
    }

    public ComboBox Choices { get; }

    protected override int? Answer() => Choices.SelectedIndex >= 0 ? Choices.SelectedIndex : null;
}
