using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace TmDq.App.Views
{
    /// <summary>Extract Calibration (plan step 4.6), the Qt <c>CalibrationSetupDialog</c>, over
    /// <see cref="ViewModels.ExtractCalibrationViewModel"/>.</summary>
    public sealed partial class ExtractCalibrationWindow : Window
    {
        public ExtractCalibrationWindow() => AvaloniaXamlLoader.Load(this);
    }
}
