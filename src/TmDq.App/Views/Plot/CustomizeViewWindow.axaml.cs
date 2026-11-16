using Avalonia.Controls;

namespace TmDq.App.Views.Plot;

/// <summary>Customize View (plan step 4.4, US4.0): closes true on OK, false on Cancel.
/// Its own questions (a colour, a name) are asked over it.</summary>
public sealed partial class CustomizeViewWindow : Window
{
    public CustomizeViewWindow()
    {
        InitializeComponent();
        OkButton.Click += (_, _) => Close(true);
        CancelButton.Click += (_, _) => Close(false);
    }
}
