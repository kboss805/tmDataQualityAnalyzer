using System.Linq;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;

namespace Tmdq.AvaloniaSpike
{
    public partial class App : Application
    {
        public override void Initialize() => AvaloniaXamlLoader.Load(this);

        public override void OnFrameworkInitializationCompleted()
        {
            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                // --skia: the chart drawn by Skia (step 0.2 of docs/dotnet-migration/avalonia-plan.md);
                // otherwise the Direct2D chart imported through GPU interop.
                desktop.MainWindow = System.Environment.GetCommandLineArgs().Contains("--skia")
                    ? new SkiaWindow()
                    : new MainWindow();
            }
            base.OnFrameworkInitializationCompleted();
        }
    }
}
