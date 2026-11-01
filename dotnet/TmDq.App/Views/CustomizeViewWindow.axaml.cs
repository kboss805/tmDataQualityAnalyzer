using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using TmDq.App.Plot;
using TmDq.App.Services;

namespace TmDq.App.Views
{
    /// <summary>Customize View (plan step 4.4), the Qt <c>PlotCustomizationDialog</c>: closes true on OK, false on Cancel.
    /// Its own questions (a colour, a name) are asked over it.</summary>
    public sealed partial class CustomizeViewWindow : Window
    {
        public CustomizeViewWindow()
        {
            AvaloniaXamlLoader.Load(this);
            this.FindControl<Button>("OkButton")!.Click += (_, _) => Close(true);
            this.FindControl<Button>("CancelButton")!.Click += (_, _) => Close(false);
            DataContextChanged += (_, _) => (DataContext as CustomizeViewModel)?.AskThrough(new WindowPlotPrompts(this));
        }
    }
}
