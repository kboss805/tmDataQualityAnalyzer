using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Styling;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Settings;
using TmDq.App.Platform;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.App.Views.Plot;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// <see cref="App.Compose"/>, where the application is put together: the platform's side of every service the main
/// window's view model is given, the preferences it remembers, the theme they ask for and the theme's switch. Every other
/// test builds its view model with fakes, so nothing else notices a service left out (a menu entry that does nothing) or a
/// theme toggle that changes no window.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public class CompositionRootTests : IDisposable
{
    private const string Csv =
        "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\r\n" +
        "001:23:59:59.000,90.000000,0.000000\r\n" +
        "002:00:00:01.000,95.000000,1.000000\r\n";

    private readonly App _app = (App)Application.Current!;
    private readonly ThemeVariant? _themeBefore;
    private readonly string _root = TempFiles.NewPath("compose-root");
    private readonly string _appData = TempFiles.NewPath("compose-appdata");

    public CompositionRootTests()
    {
        _themeBefore = _app.RequestedThemeVariant;
        Directory.CreateDirectory(_root);
        Directory.CreateDirectory(_appData);
        NativeCore.EnsureLoaded();
    }

    public void Dispose()
    {
        _app.RequestedThemeVariant = _themeBefore;
        TempFiles.TryDeleteDirectory(_root);
        TempFiles.TryDeleteDirectory(_appData);
        GC.SuppressFinalize(this);
    }

    private string PreferencesFolder => PreferencesStore.FolderFor(_root, _appData);

    [AvaloniaFact]
    public async Task EveryServiceTheMenuNeedsIsWired()
    {
        var window = new MainWindow();
        MainWindowViewModel vm = _app.Compose(window, null, _root, _appData);

        // Each entry is unavailable without the service behind it (the view model's own tests hold that), so one that is
        // available has its service. Export also needs data, and a CSV import is the way to have it with no recording.
        Assert.Equal((true, true, true, true, true),
                     (vm.OpenCommand.CanExecute(null), vm.ImportCsvCommand.CanExecute(null), vm.ApplyTemplateCommand.CanExecute(null),
                      vm.OpenManualCommand.CanExecute(null), vm.ShowAboutCommand.CanExecute(null)));
        Assert.False(vm.ExportCommand.CanExecute(null));
        string csv = TempFiles.NewPath("compose", ".csv");
        File.WriteAllText(csv, Csv, new UTF8Encoding(false));
        try
        {
            await vm.ImportCsvAsync(csv);
            Assert.True(vm.ExportCommand.CanExecute(null));
        }
        finally
        {
            TempFiles.Delete(csv);
        }
    }

    [AvaloniaFact]
    public void TheWindowHasTheViewModelAndItsPlotHasItsPrompts()
    {
        var window = new MainWindow();
        MainWindowViewModel vm = _app.Compose(window, null, _root, _appData);
        Assert.Same(vm, window.DataContext);
        Assert.IsType<WindowPlotPrompts>(window.FindControl<PlotView>("PlotView")!.Prompts);
    }

    [AvaloniaTheory]
    [InlineData(Preferences.LightTheme, false)]
    [InlineData(Preferences.DarkTheme, true)]
    public void ThePreferencesAreReadFromTheirFolderAndTheThemeFollowsThem(string theme, bool dark)
    {
        Directory.CreateDirectory(PreferencesFolder);
        File.WriteAllText(Path.Combine(PreferencesFolder, PreferencesStore.FileName),
                          "{ \"Theme\": \"" + theme + "\", \"SidebarVisible\": false }");
        // The other way first, so that a theme the file's value does not set shows.
        _app.RequestedThemeVariant = dark ? ThemeVariant.Light : ThemeVariant.Dark;

        MainWindowViewModel vm = _app.Compose(new MainWindow(), null, _root, _appData);

        Assert.Equal((dark, false), (vm.IsDarkTheme, vm.SidebarVisible));
        Assert.Equal(dark ? ThemeVariant.Dark : ThemeVariant.Light, _app.RequestedThemeVariant);
    }

    [AvaloniaFact]
    public void ThePreferencesAreWrittenBesideTheExecutableForAPortableCopyAndInAppDataOtherwise()
    {
        _app.Compose(new MainWindow(), null, _root, _appData);
        Assert.True(File.Exists(Path.Combine(_appData, "tmDataQualityAnalyzer", PreferencesStore.FileName)));

        string portableRoot = TempFiles.NewPath("compose-portable"), otherData = TempFiles.NewPath("compose-appdata-2");
        Directory.CreateDirectory(portableRoot);
        File.WriteAllText(Path.Combine(portableRoot, PreferencesStore.PortableMarker), "");
        try
        {
            _app.Compose(new MainWindow(), null, portableRoot, otherData);
            Assert.True(File.Exists(Path.Combine(portableRoot, "tmDataQualityAnalyzer", PreferencesStore.FileName)));
            Assert.False(Directory.Exists(otherData));
        }
        finally
        {
            TempFiles.TryDeleteDirectory(portableRoot);
        }
    }

    [AvaloniaFact]
    public void TheThemeMenuChangesTheThemeOfEveryWindowAndIsRemembered()
    {
        MainWindowViewModel vm = _app.Compose(new MainWindow(), null, _root, _appData);
        string file = Path.Combine(PreferencesFolder, PreferencesStore.FileName);

        vm.UseLightThemeCommand.Execute(null);
        Assert.Equal(ThemeVariant.Light, _app.RequestedThemeVariant);
        Assert.False(PreferencesStore.Load(PreferencesFolder).Current.IsDark);

        vm.UseDarkThemeCommand.Execute(null);
        Assert.Equal(ThemeVariant.Dark, _app.RequestedThemeVariant);
        Assert.True(PreferencesStore.Load(PreferencesFolder).Current.IsDark);
        Assert.True(File.Exists(file));
    }

    [AvaloniaFact]
    public async Task TheSmokeTestKeepsNothingAndRunsTheDefaultsWithNoOneToAsk()
    {
        string report = TempFiles.NewPath("compose-smoke", ".txt");
        var window = new MainWindow();
        MainWindowViewModel vm = _app.Compose(window, report, _root, _appData);
        try
        {
            // Nothing is read from, or written to, the preferences' folder: the smoke test must not leave its recording
            // in anyone's Recent Files, nor create a settings folder on the machine it checks.
            Assert.False(Directory.Exists(PreferencesFolder));

            // And a recording is processed with no Configure Streams to answer (a dialog would never be answered).
            window.Show();
            Assert.Equal(Startup.ExitProcessed, await Startup.RunSmokeAsync(vm, Repo.Recording("prn_testfile, sample.ch10"), report));
            Assert.StartsWith("ok samples=116 ", File.ReadAllText(report));
            Assert.False(Directory.Exists(PreferencesFolder));   // the recording was remembered in memory only
        }
        finally
        {
            TempFiles.Delete(report);
        }
    }
}
