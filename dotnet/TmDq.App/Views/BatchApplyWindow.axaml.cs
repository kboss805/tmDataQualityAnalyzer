using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace TmDq.App.Views
{
    /// <summary>The batch confirmation (plan step 4.7), the Qt <c>BatchApplyDialog</c>, over
    /// <see cref="ViewModels.BatchApplyViewModel"/>.</summary>
    public sealed partial class BatchApplyWindow : Window
    {
        public BatchApplyWindow() => AvaloniaXamlLoader.Load(this);
    }
}
