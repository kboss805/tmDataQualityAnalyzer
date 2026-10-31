using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.Views;

namespace TmDq.App
{
    public sealed class App : Application
    {
        public override void Initialize() => AvaloniaXamlLoader.Load(this);

        public override void OnFrameworkInitializationCompleted()
        {
            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                var window = new MainWindow();
                var vm = new MainWindowViewModel(new StorageRecordingPicker(window));
                window.DataContext = vm;
                desktop.MainWindow = window;

                // A recording on the command line opens once the window is up (Startup says what the line may hold).
                if (Startup.RecordingFromArguments(desktop.Args) is { } recording)
                {
                    window.Opened += async (_, _) =>
                    {
                        await vm.LoadAsync(recording);
                        if (Startup.SmokeReportPath() is { } report)
                        {
                            Startup.WriteSmokeReport(report, vm);
                            desktop.Shutdown(vm.Series is null ? 1 : 0);
                        }
                    };
                }
            }
            base.OnFrameworkInitializationCompleted();
        }
    }
}
