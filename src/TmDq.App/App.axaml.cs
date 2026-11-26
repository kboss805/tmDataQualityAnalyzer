using System;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Styling;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Settings;
using TmDq.App.Platform;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Dialogs;
using TmDq.App.Views;

namespace TmDq.App;

public sealed class App : Application
{
    // Held for the life of the application, which is what the title bars following the theme lasts for.
    private IDisposable? _titleBars;

    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        _titleBars = TitleBarTheme.Install();   // before the first window: a dialog's title bar follows the theme on Windows 10 too
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var window = new MainWindow();
            string? smokeReport = Startup.SmokeReportPath();
            MainWindowViewModel vm = Compose(window, smokeReport, AppContext.BaseDirectory,
                                             Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData));
            desktop.MainWindow = window;

            // A recording on the command line opens once the window is up, and an exported CSV is imported (Startup says what the
            // line may hold).
            if (Startup.RecordingFromArguments(desktop.Args) is { } recording)
            {
                window.Opened += async (_, _) =>
                {
                    if (smokeReport is not null) desktop.Shutdown(await Startup.RunSmokeAsync(vm, recording, smokeReport));
                    else await Startup.OpenAsync(vm, recording);
                };
            }
        }
        base.OnFrameworkInitializationCompleted();
    }

    /// <summary>
    /// The one place the application is put together: the platform's side of every service the main window's view model is
    /// given, over <paramref name="window"/>, the preferences it remembers, the theme they ask for and the theme's switch.
    /// Public so that a test can hold the wiring: a service left out is a menu entry that does nothing, and the application's
    /// own tests build their view models with fakes.
    /// </summary>
    /// <param name="window">The main window; it is given the view model as its data context and its plot its prompts.</param>
    /// <param name="smokeReport">The installer's smoke test report file, or null for a normal run. The smoke test keeps nothing
    /// (it must not leave its recording in anyone's Recent Files) and has no one to answer Configure Streams, so it runs the
    /// defaults.</param>
    /// <param name="appRoot">The folder of the executable: where the settings folder is, and where a "portable" file says the
    /// preferences are kept.</param>
    /// <param name="appData">The folder the preferences go in when the copy is not portable (<c>%APPDATA%</c>).</param>
    /// <param name="crash">The crash handling to give the session's log to, or null for the one <c>Main</c> installed (none in a test).</param>
    public MainWindowViewModel Compose(MainWindow window, string? smokeReport, string appRoot, string appData, CrashHandling? crash = null)
    {
        window.PlotView.Prompts = new WindowPlotPrompts(window.PlotView);
        // What is remembered between sessions (plan step 4.8).
        PreferencesStore preferences = smokeReport is null
            ? PreferencesStore.Load(PreferencesStore.FolderFor(appRoot, appData))
            : new PreferencesStore(new Preferences());
        ApplyTheme(preferences.Current.IsDark);
        IStreamConfigurator configurator = smokeReport is null
            ? new DialogConfigurator(new WindowStreamDialogs(window), new StorageSettingsFilePicker(window), appRoot,
                                     new StorageCalibrationFilePicker(window), preferences)
            : new FirstChannelRun();
        var vm = new MainWindowViewModel(new MainWindowServices
        {
            Picker = new StorageRecordingPicker(window),
            Configurator = configurator,
            ProcessingDialog = new WindowProcessingDialog(window),
            Files = new StorageDataFilePicker(window),
            CsvKinds = new WindowCsvKindDialog(window),
            ExportDialog = new WindowExportDialog(window),
            ImageExporter = new PlotViewImageExporter(window.PlotView),
            TemplateFiles = new StorageTemplateFilePicker(window),
            TemplateDialogs = new WindowTemplateDialogs(window),
            Help = new WindowHelpActions(window),
            AppRoot = appRoot,
            Preferences = preferences,
        });
        vm.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(MainWindowViewModel.IsDarkTheme)) ApplyTheme(vm.IsDarkTheme);
        };
        window.DataContext = vm;
        (crash ?? CrashHandling.Current)?.AttachLog(() => vm.Log.PlainText);   // what the user was doing, in a crash report
        return vm;
    }

    /// <summary>The theme of every window, dialogs and their frames included (US9.0).</summary>
    public void ApplyTheme(bool dark) => RequestedThemeVariant = dark ? ThemeVariant.Dark : ThemeVariant.Light;
}
