using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Styling;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Settings;
using TmDq.App.Platform;
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
                // What is remembered between sessions (plan step 4.8). The installer's smoke test keeps nothing: it must
                // not leave its recording in anyone's Recent Files.
                PreferencesStore preferences = Startup.SmokeReportPath() is null
                    ? PreferencesStore.Load(PreferencesStore.FolderFor(AppContext.BaseDirectory,
                                                                        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData)))
                    : new PreferencesStore(new Preferences());
                ApplyTheme(preferences.Current.IsDark);
                // The installer's smoke test has no one to answer Configure Streams, so it runs the defaults.
                IStreamConfigurator configurator = Startup.SmokeReportPath() is null
                    ? new DialogConfigurator(new WindowStreamDialogs(window), new StorageSettingsFilePicker(window), AppContext.BaseDirectory,
                                             new StorageCalibrationFilePicker(window), preferences)
                    : new FirstChannelRun();
                var vm = new MainWindowViewModel(new MainWindowServices
                {
                    Picker = new StorageRecordingPicker(window),
                    Configurator = configurator,
                    ProcessingDialog = new WindowProcessingDialog(window),
                    Files = new StorageDataFilePicker(window),
                    ExportDialog = new WindowExportDialog(window),
                    ImageExporter = new PlotViewImageExporter(window.FindControl<PlotView>("PlotView")!),
                    TemplateFiles = new StorageTemplateFilePicker(window),
                    TemplateDialogs = new WindowTemplateDialogs(window),
                    AppRoot = AppContext.BaseDirectory,
                    Preferences = preferences,
                });
                vm.PropertyChanged += (_, e) =>
                {
                    if (e.PropertyName == nameof(MainWindowViewModel.IsDarkTheme)) ApplyTheme(vm.IsDarkTheme);
                };
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
                            desktop.Shutdown(Startup.FirstLockSeries(vm.Plot) is null ? 1 : 0);
                        }
                    };
                }
            }
            base.OnFrameworkInitializationCompleted();
        }

        /// <summary>The theme of every window, dialogs and their frames included (US9.0).</summary>
        public void ApplyTheme(bool dark) => RequestedThemeVariant = dark ? ThemeVariant.Dark : ThemeVariant.Light;
    }
}
