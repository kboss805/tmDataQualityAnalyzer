using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace TmDq.App.Views
{
    /// <summary>The Export dialog (plan step 4.5), the Qt <c>ExportDialog</c>, over <see cref="ViewModels.ExportViewModel"/>.</summary>
    public sealed partial class ExportWindow : Window
    {
        public ExportWindow() => AvaloniaXamlLoader.Load(this);
    }
}
