using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Chrome;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Input.Raw;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Platform.Storage;
using Avalonia.Styling;
using Avalonia.Threading;
using TmDq.App.Plot;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.App.Settings;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The shell (plan step 4.8): the title bar of the application's own with each part's role for Windows (snap
    /// layouts on the maximize button), the menu with v2.14.2's sections, Recent Files and Help, the keys,
    /// the log sidebar's toggle, a file dropped on the window, the light and dark themes, and the manual. v2.x's
    /// main-window shell cases are ported among them.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class ShellTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-shell-" + Guid.NewGuid().ToString("N"));

        public ShellTests() => Directory.CreateDirectory(_dir);

        public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);

        private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

        private static (MainWindow Window, MainWindowViewModel Vm) Show(MainWindowViewModel? vm = null)
        {
            vm ??= new MainWindowViewModel();
            var window = new MainWindow { DataContext = vm };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return (window, vm);
        }

        private sealed class Picker : IRecordingPicker
        {
            public int Asked;
            public Task<string?> PickRecordingAsync()
            {
                Asked++;
                return Task.FromResult<string?>(null);
            }
        }

        private sealed class Cancelled : IStreamConfigurator
        {
            public Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file) => Task.FromResult<StreamsChoice?>(null);
        }

        private sealed class FakeHelp : IHelpActions
        {
            public readonly List<string> Asked = new();
            public Task OpenManualAsync(Window owner) { Asked.Add("manual"); return Task.CompletedTask; }
            public Task ShowAboutAsync(Window owner) { Asked.Add("about"); return Task.CompletedTask; }
        }

        // ---- the title bar -------------------------------------------------------------------------------------------

        [AvaloniaFact]
        public void TheTitleBarIsTheApplicationsOwnAndTellsWindowsWhatEachPartIs()
        {
            (MainWindow window, _) = Show();
            Assert.True(window.ExtendClientAreaToDecorationsHint);
            Assert.Equal(WindowDecorations.BorderOnly, window.WindowDecorations);   // a resizable frame, no drawn title bar
            Assert.Equal(40, window.ExtendClientAreaTitleBarHeightHint);
            Assert.Equal(40, window.TitleBar.Bounds.Height);
            Assert.Equal(new[]
            {
                WindowDecorationsElementRole.User, WindowDecorationsElementRole.User, WindowDecorationsElementRole.TitleBar,
                WindowDecorationsElementRole.MinimizeButton, WindowDecorationsElementRole.MaximizeButton, WindowDecorationsElementRole.CloseButton,
            }, new Visual[] { window.MenuButton, window.SidebarButton, window.Caption, window.MinimizeButton, window.MaximizeButton, window.CloseButton }
                   .Select(WindowDecorationProperties.GetElementRole));
            // Left to right: the menu, the sidebar toggle, the caption taking the rest, then the window's buttons.
            Assert.Equal(new[] { 0, 1, 2, 3, 4, 5 }, new Control[] { window.MenuButton, window.SidebarButton, window.Caption, window.MinimizeButton,
                                                                     window.MaximizeButton, window.CloseButton }.Select(Grid.GetColumn));
            Assert.True(window.Caption.Bounds.Width > 1000);
            Assert.Equal(("Menu", "Toggle sidebar (Ctrl+B)", "Minimize", "Maximize", "Close"),
                         ((string)ToolTip.GetTip(window.MenuButton)!, (string)ToolTip.GetTip(window.SidebarButton)!, (string)ToolTip.GetTip(window.MinimizeButton)!,
                          (string)ToolTip.GetTip(window.MaximizeButton)!, (string)ToolTip.GetTip(window.CloseButton)!));
            Assert.Equal(("\uE700", "\uE89F", "\uE921", "\uE922", "\uE8BB"),
                         ((string)window.MenuButton.Content!, (string)window.SidebarButton.Content!, (string)window.MinimizeButton.Content!,
                          (string)window.MaximizeButton.Content!, (string)window.CloseButton.Content!));
            Assert.All(new Control[] { window.MenuButton, window.SidebarButton, window.MinimizeButton, window.MaximizeButton, window.CloseButton },
                       b => Assert.Equal((30.0, 26.0), (b.Bounds.Width, b.Bounds.Height)));
            Assert.Same(window.MainMenu, window.MenuButton.Flyout);
            window.Close();
        }

        [AvaloniaFact]
        public void TheWindowsButtonsMinimizeMaximizeRestoreAndClose()
        {
            (MainWindow window, _) = Show();
            window.MaximizeButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Assert.Equal(WindowState.Maximized, window.WindowState);
            Assert.Equal(("\uE923", "Restore"), ((string)window.MaximizeButton.Content!, (string)ToolTip.GetTip(window.MaximizeButton)!));
            window.MaximizeButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Assert.Equal(WindowState.Normal, window.WindowState);
            Assert.Equal(("\uE922", "Maximize"), ((string)window.MaximizeButton.Content!, (string)ToolTip.GetTip(window.MaximizeButton)!));
            window.MinimizeButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Assert.Equal(WindowState.Minimized, window.WindowState);
            window.WindowState = WindowState.Maximized;   // however it is maximized, the button follows
            Assert.Equal("\uE923", (string)window.MaximizeButton.Content!);
            bool closed = false;
            window.Closed += (_, _) => closed = true;
            window.CloseButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Assert.True(closed);
        }

        // ---- the menu ------------------------------------------------------------------------------------------------

        private static string Header(object? item) => item switch
        {
            Separator => "-",
            MenuItem { Classes: var c } m when c.Contains("section") => "[" + m.Header + "]",
            MenuItem m => (string)m.Header!,
            _ => "?",
        };

        private static MenuItem Find(IEnumerable<object?> items, string header) =>
            items.OfType<MenuItem>().Single(m => (string)m.Header! == header);

        [AvaloniaFact]
        public async Task TheMenuHoldsItsSectionsHelpAndExit()
        {
            var help = new FakeHelp();
            var picker = new Picker();
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(picker));
            window.Help = help;
            window.BuildMenu();
            Assert.Equal(new[]
            {
                "[Process]", "Open...", "Recent Files", "Save as Template...", "Apply Template to Files...",
                "[Import/Export]", "Import CSV...", "Export...",
                "[Settings]", "Switch to Light Theme",
                "-", "Help", "-", "Exit",
            }, window.MainMenu.Items.Select(Header));
            // A section is a heading: never something to click.
            Assert.All(window.MainMenu.Items.OfType<MenuItem>().Where(m => m.Classes.Contains("section")), s => Assert.False(s.IsEnabled));
            MenuItem Item(string h) => window.MainMenu.Items.OfType<MenuItem>().Single(m => (string)m.Header! == h);
            Assert.Equal((vm.OpenCommand, vm.SaveTemplateCommand, vm.ApplyTemplateCommand, vm.ImportCsvCommand, vm.ExportCommand, vm.ToggleThemeCommand),
                         (Item("Open...").Command, Item("Save as Template...").Command, Item("Apply Template to Files...").Command,
                          Item("Import CSV...").Command, Item("Export...").Command, Item("Switch to Light Theme").Command));
            Assert.Equal(new KeyGesture(Key.O, KeyModifiers.Control), Item("Open...").InputGesture);
            Assert.Equal(("Import a previously exported CSV file", "Export plot data and images"),
                         ((string)ToolTip.GetTip(Item("Import CSV..."))!, (string)ToolTip.GetTip(Item("Export..."))!));
            Assert.NotNull(ToolTip.GetTip(Item("Save as Template...")));
            Assert.NotNull(ToolTip.GetTip(Item("Apply Template to Files...")));
            // v2.14.2's menu icons: the folder, the floppy disk, the import and export arrows.
            foreach (string h in new[] { "Open...", "Save as Template...", "Import CSV...", "Export..." })
                Assert.NotNull(Assert.IsType<Image>(Item(h).Icon).Source);
            Assert.Null(Item("Apply Template to Files...").Icon);

            // Help holds the manual and About, and neither is at the top.
            MenuItem helpMenu = Item("Help");
            Assert.Equal(new[] { "User Manual...", "About..." }, helpMenu.Items.Select(Header));
            await ((CommunityToolkit.Mvvm.Input.IAsyncRelayCommand)Find(helpMenu.Items, "User Manual...").Command!).ExecuteAsync(null);
            await ((CommunityToolkit.Mvvm.Input.IAsyncRelayCommand)Find(helpMenu.Items, "About...").Command!).ExecuteAsync(null);
            Assert.Equal(new[] { "manual", "about" }, help.Asked);

            // The theme item says where it goes, and goes there.
            Item("Switch to Light Theme").Command!.Execute(null);
            window.BuildMenu();
            Assert.Contains(window.MainMenu.Items, m => Header(m) == "Switch to Dark Theme");
            Assert.False(vm.IsDarkTheme);

            // Open is the view model's; Exit, the last entry, closes the window.
            Item("Open...").Command!.Execute(null);
            Assert.Equal(1, picker.Asked);
            bool closed = false;
            window.Closed += (_, _) => closed = true;
            Item("Exit").Command!.Execute(null);
            Assert.True(closed);
        }

        [AvaloniaFact]
        public void TheMenuIsBuiltAsItOpensAndIsEmptyWithoutAViewModel()
        {
            (MainWindow window, MainWindowViewModel vm) = Show();
            Assert.Empty(window.MainMenu.Items);
            window.MainMenu.ShowAt(window.MenuButton);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(14, window.MainMenu.Items.Count);
            window.MainMenu.Hide();

            // A section's heading is small and bold wherever the menu is shown (the style is the application's: a menu is
            // a popup of its own).
            window.BuildMenu();
            var items = window.MainMenu.Items.ToList();
            window.MainMenu.Items.Clear();
            var host = new Window { Content = new MenuFlyoutPresenter { ItemsSource = items } };
            host.Show();
            Dispatcher.UIThread.RunJobs();
            var heading = (MenuItem)items[0]!;
            Assert.Equal((FontWeight.Bold, 11.0), (heading.FontWeight, heading.FontSize));
            Assert.NotEqual(FontWeight.Bold, ((MenuItem)items[1]!).FontWeight);
            host.Close();
            window.DataContext = null;
            window.BuildMenu();
            Assert.Empty(window.MainMenu.Items);
            window.Close();
        }

        [Fact]
        public void OpenOffersRecordingsOnlyAndImportCsvExportedCsvsOnly()
        {
            // US6.3: Open stays Chapter-10-only, Import CSV is CSV-only; v2.x's filters, with no "All files" to
            // reach a CSV through Open.
            FilePickerOpenOptions open = StorageRecordingPicker.Options;
            Assert.False(open.AllowMultiple);
            FilePickerFileType recordings = Assert.Single(open.FileTypeFilter!);
            Assert.Equal(new[] { "*.ch10" }, recordings.Patterns);

            FilePickerOpenOptions csv = StorageDataFilePicker.CsvOptions(null);
            Assert.Equal("Import CSV File", csv.Title);
            Assert.False(csv.AllowMultiple);
            FilePickerFileType exported = Assert.Single(csv.FileTypeFilter!);
            Assert.Equal("Exported CSV", exported.Name);
            Assert.Equal(new[] { "*.csv" }, exported.Patterns);
        }

        [AvaloniaFact]
        public void RecentFilesListsEachByItsNameNewestFirstAndClears()
        {
            var store = new PreferencesStore(new Preferences());
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(preferences: store));
            window.BuildMenu();
            MenuItem recent = Find(window.MainMenu.Items, "Recent Files");
            MenuItem none = Assert.IsType<MenuItem>(Assert.Single(recent.Items));
            Assert.Equal((MainWindow.NoRecentFiles, false), ((string)none.Header!, none.IsEnabled));

            store.Update(p => p with { RecentFiles = new[] { @"C:\data\flight 2.ch10", @"D:\out\export.csv" } });
            window.BuildMenu();
            recent = Find(window.MainMenu.Items, "Recent Files");
            Assert.Equal(new[] { "flight 2.ch10", "export.csv", "-", "Clear Recent Files" }, recent.Items.Select(Header));
            var first = (MenuItem)recent.Items[0]!;
            Assert.Equal((@"C:\data\flight 2.ch10", vm.OpenRecentCommand, @"C:\data\flight 2.ch10"),
                         ((string)ToolTip.GetTip(first)!, first.Command, first.CommandParameter));
            Assert.Equal(@"D:\out\export.csv", ((MenuItem)recent.Items[1]!).CommandParameter);
            Assert.Same(vm.ClearRecentFilesCommand, Find(recent.Items, "Clear Recent Files").Command);
            window.Close();
        }

        [AvaloniaFact]
        public void CtrlOOpensAndCtrlBTogglesTheSidebar()
        {
            var picker = new Picker();
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(picker));
            window.KeyPress(Key.O, RawInputModifiers.Control, PhysicalKey.O, "o");
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(1, picker.Asked);
            window.KeyPress(Key.B, RawInputModifiers.Control, PhysicalKey.B, "b");
            Assert.False(vm.SidebarVisible);
            window.KeyPress(Key.B, RawInputModifiers.Control, PhysicalKey.B, "b");
            Assert.True(vm.SidebarVisible);
            window.Close();
        }

        // ---- the sidebar ---------------------------------------------------------------------------------------------

        [AvaloniaFact]
        public void TheSidebarHidesGivingItsWidthToTheChartAndReturnsAsItWas()
        {
            var store = new PreferencesStore(new Preferences { SidebarVisible = false });
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(preferences: store));
            Border log = window.FindControl<Border>("LogPanel")!;
            Assert.Equal((0.0, 0.0, false), (window.Body.ColumnDefinitions[0].Width.Value, window.Body.ColumnDefinitions[1].Width.Value, log.IsVisible));
            Assert.False(window.SidebarButton.IsChecked);

            window.SidebarButton.IsChecked = true;   // the toggle is the preference
            Dispatcher.UIThread.RunJobs();
            Assert.True(vm.SidebarVisible);
            Assert.Equal((400.0, 4.0, true), (window.Body.ColumnDefinitions[0].Width.Value, window.Body.ColumnDefinitions[1].Width.Value, log.IsVisible));
            Assert.True(window.FindControl<GridSplitter>("Splitter")!.IsVisible);

            // A width the user dragged it to is the one it returns at.
            window.Body.ColumnDefinitions[0].Width = new GridLength(300);
            vm.SidebarVisible = false;
            Dispatcher.UIThread.RunJobs();
            Assert.False(window.SidebarButton.IsChecked);
            Assert.False(window.FindControl<GridSplitter>("Splitter")!.IsVisible);
            Assert.Equal(0, window.Body.ColumnDefinitions[0].Width.Value);
            vm.SidebarVisible = true;
            Assert.Equal(300, window.Body.ColumnDefinitions[0].Width.Value);

            // A new view model is followed, and the old one let go.
            var other = new MainWindowViewModel(preferences: new PreferencesStore(new Preferences { SidebarVisible = false }));
            window.DataContext = other;
            Assert.Equal(0, window.Body.ColumnDefinitions[0].Width.Value);
            vm.SidebarVisible = false;
            vm.SidebarVisible = true;
            Assert.Equal(0, window.Body.ColumnDefinitions[0].Width.Value);

            // Hidden twice over (one hidden view model for another), it still returns at the width it had.
            var hidden = new MainWindowViewModel(preferences: new PreferencesStore(new Preferences { SidebarVisible = false }));
            window.DataContext = hidden;
            hidden.SidebarVisible = true;
            Assert.Equal(300, window.Body.ColumnDefinitions[0].Width.Value);
            window.Close();
        }

        // ---- drag and drop -------------------------------------------------------------------------------------------

        /// <summary>The files as a drag carries them: storage items from the platform (user code cannot make its own).</summary>
        private static async Task<DataTransfer> Files(TopLevel top, params string[] paths)
        {
            var data = new DataTransfer();
            foreach (string p in paths)
                data.Add(DataTransferItem.CreateFile((await top.StorageProvider.TryGetFileFromPathAsync(p))
                                                     ?? throw new InvalidOperationException("no storage item for " + p)));
            return data;
        }
        private static async Task Until(Func<bool> done, Func<string>? why = null)
        {
            for (int i = 0; i < 500 && !done(); i++)
            {
                Dispatcher.UIThread.RunJobs();
                await Task.Delay(10);
            }
            Assert.True(done(), "it never happened: " + why?.Invoke());
        }

        [AvaloniaFact]
        public async Task TheFirstRecordingOrCsvAmongTheFilesIsTheOneTaken()
        {
            var top = new Window();
            string a = Path.Combine(_dir, "a.txt"), b = Path.Combine(_dir, "b.csv"), c = Path.Combine(_dir, "c.CH10"), zip = Path.Combine(_dir, "archive.csv.zip");
            foreach (string p in new[] { a, b, c, zip }) File.WriteAllText(p, "");
            Assert.Equal(b, MainWindow.FirstSupported(await Files(top, a, b, c)));
            Assert.Equal(c, MainWindow.FirstSupported(await Files(top, a, c)));
            Assert.Null(MainWindow.FirstSupported(await Files(top, a, zip)));
            Assert.Null(MainWindow.FirstSupported(new DataTransfer()));
            Assert.Null(MainWindow.FirstSupported(null));
        }

        [AvaloniaFact]
        public async Task ARecordingOrCsvDroppedOnTheWindowOpensAsRecentFilesOpensIt()
        {
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(configurator: new Cancelled()));
            var at = new Point(600, 300);
            DragDropEffects offered = DragDropEffects.Move;
            window.AddHandler(DragDrop.DragOverEvent, (_, e) => offered = e.DragEffects, RoutingStrategies.Bubble, handledEventsToo: true);
            string notes = Path.Combine(_dir, "notes.txt");
            File.WriteAllText(notes, "");
            window.DragDrop(at, RawDragEventType.DragEnter, await Files(window, notes), DragDropEffects.Copy, RawInputModifiers.None);
            window.DragDrop(at, RawDragEventType.DragOver, await Files(window, notes), DragDropEffects.Copy, RawInputModifiers.None);
            Assert.Equal(DragDropEffects.None, offered);
            window.DragDrop(at, RawDragEventType.Drop, await Files(window, notes), DragDropEffects.Copy, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("Open a Chapter 10 recording.", vm.Status);

            // A recording, after a file that is neither: offered as a copy, and opened.
            offered = DragDropEffects.Move;
            window.DragDrop(at, RawDragEventType.DragEnter, await Files(window, notes, Prn), DragDropEffects.Copy, RawInputModifiers.None);
            window.DragDrop(at, RawDragEventType.DragOver, await Files(window, notes, Prn), DragDropEffects.Copy, RawInputModifiers.None);
            Assert.Equal(DragDropEffects.Copy, offered);
            window.DragDrop(at, RawDragEventType.Drop, await Files(window, notes, Prn), DragDropEffects.Copy, RawInputModifiers.None);
            await Until(() => vm.Status.Contains("Configure Streams was cancelled", StringComparison.Ordinal), () => vm.Status);
            Assert.Equal(new[] { Prn }, vm.RecentFiles);

            // An exported CSV is imported.
            vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, new[] { 0.0, 1.0 }, new[] { 50.0, 60.0 }) });
            string csv = Path.Combine(_dir, "dropped.csv");
            Assert.True(vm.Plot.ExportCsv(csv));
            window.DragDrop(at, RawDragEventType.DragEnter, await Files(window, csv), DragDropEffects.Copy, RawInputModifiers.None);
            window.DragDrop(at, RawDragEventType.Drop, await Files(window, csv), DragDropEffects.Copy, RawInputModifiers.None);
            await Until(() => vm.Status == "Imported CSV: dropped.csv", () => vm.Status);

            // Without a view model nothing is offered.
            window.DataContext = null;
            offered = DragDropEffects.Move;
            window.DragDrop(at, RawDragEventType.DragEnter, await Files(window, Prn), DragDropEffects.Copy, RawInputModifiers.None);
            window.DragDrop(at, RawDragEventType.DragOver, await Files(window, Prn), DragDropEffects.Copy, RawInputModifiers.None);
            Assert.Equal(DragDropEffects.None, offered);
            window.DragDrop(at, RawDragEventType.Drop, await Files(window, Prn), DragDropEffects.Copy, RawInputModifiers.None);
            window.Close();
        }

        // ---- the themes ----------------------------------------------------------------------------------------------

        [AvaloniaFact]
        public void EachThemeDrawsTheChartTheLegendTheChipsAndTheLogInItsColours()
        {
            (MainWindow window, MainWindowViewModel vm) = Show();
            vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, new[] { 0.0, 1.0 }, new[] { 50.0, 60.0 }) });
            vm.Plot.SetXViewByUser(0.2, 0.8);   // a chip to see
            Dispatcher.UIThread.RunJobs();
            PlotView view = window.FindControl<PlotView>("PlotView")!;
            Border log = window.FindControl<Border>("LogPanel")!;
            Frames.Capture(window).Dispose();   // the chart's layer painted in the theme it opened in

            foreach ((ThemeVariant theme, ChartPalette palette, string logBackground, string chip) in new[]
                     {
                         (ThemeVariant.Dark, ChartPalette.Dark, "#FF202020", "#B9202020"),
                         (ThemeVariant.Light, ChartPalette.Light, "#FFFFFFFF", "#B9FFFFFF"),
                     })
            {
                window.RequestedThemeVariant = theme;
                Dispatcher.UIThread.RunJobs();
                Assert.Same(palette, view.Chart.Palette);
                Assert.Same(palette, view.Legend.Palette);
                Assert.Equal(palette.LegendBackground, ((ISolidColorBrush)view.Legend.Background!).Color);
                Assert.Equal(palette.LegendBorder, ((ISolidColorBrush)view.Legend.BorderBrush!).Color);
                Assert.All(view.Legend.Rows, r => Assert.Equal(palette.LegendText,
                    ((ISolidColorBrush)((Panel)r).Children.OfType<TextBlock>().Single().Foreground!).Color));
                // Drawn, not only chosen: the chart's own background, read from a rendered frame, before anything but the
                // theme has changed (the layer painted in the other theme is not reused).
                using (WriteableBitmap frame = Frames.Capture(window))
                using (ILockedFramebuffer fb = frame.Lock())
                {
                    Point inside = view.Chart.TranslatePoint(new Point(view.Chart.PlotArea.Right - 20, view.Chart.PlotArea.Bottom - 5), window)!.Value;
                    Assert.Equal((uint)(palette.Background.R << 16 | palette.Background.G << 8 | palette.Background.B), Frames.Pixel(fb, (int)inside.X, (int)inside.Y));
                }
                Assert.Equal(Color.Parse(logBackground), ((ISolidColorBrush)log.Background!).Color);
                Assert.Equal(Color.Parse(chip), ((ISolidColorBrush)view.ResetChip.Background!).Color);

                // Rows made in this theme are in its colours too.
                vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, new[] { 0.0, 1.0 }, new[] { 50.0, 60.0 }) });
                vm.Plot.SetXViewByUser(0.2, 0.8);
                Dispatcher.UIThread.RunJobs();
                Assert.All(view.Legend.Rows, r => Assert.Equal(palette.LegendText,
                    ((ISolidColorBrush)((Panel)r).Children.OfType<TextBlock>().Single().Foreground!).Color));
            }
            window.Close();
        }

        [Fact]
        public void ThePalettesAreV214s()
        {
            // v2.14.2's chart and legend colours for each theme, as numbers.
            Assert.Equal(new[] { "#FFFFFFFF", "#FF1E1E1E", "#FFC8C8C8", "#FF005FB8", "#B9FFFFFF", "#FFAAAAAA", "#FF1E1E1E" }, Hex(ChartPalette.Light));
            Assert.Equal(new[] { "#FF202020", "#FFDCDCDC", "#FF3C3C3C", "#FF60CDFF", "#B9202020", "#FF5A5A5A", "#FFE6E6E6" }, Hex(ChartPalette.Dark));
        }

        private static string[] Hex(ChartPalette p) => new[] { p.Background, p.Foreground, p.Grid, p.Title, p.LegendBackground, p.LegendBorder, p.LegendText }
            .Select(c => $"#{c.A:X2}{c.R:X2}{c.G:X2}{c.B:X2}").ToArray();

        [AvaloniaFact]
        public void EachThemesBrushesAreV214s()
        {
            string[] keys = { "ChipBackground", "ChipBorder", "ChipForeground", "ChipAccent", "LoadingBackground", "LoadingBorder", "LoadingForeground",
                              "LogPanelBackground", "LogPanelBorder", "TitleBarHover" };
            string[] Brushes(ThemeVariant theme) => keys.Select(k =>
            {
                Assert.True(Application.Current!.TryFindResource(k, theme, out object? b), k);
                Color c = ((ISolidColorBrush)b!).Color;
                return $"#{c.A:X2}{c.R:X2}{c.G:X2}{c.B:X2}";
            }).ToArray();
            Assert.Equal(new[] { "#B9FFFFFF", "#FFC8C8C8", "#FF1E1E1E", "#FF005FB8", "#E6FFFFFF", "#FFC8C8C8", "#FF1E1E1E", "#FFFFFFFF", "#FFE5E5E5", "#38808080" },
                         Brushes(ThemeVariant.Light));
            Assert.Equal(new[] { "#B9202020", "#FF3C3C3C", "#FFDCDCDC", "#FF60CDFF", "#A0000000", "#FF3C3C3C", "#FFFFFFFF", "#FF202020", "#FF303030", "#38808080" },
                         Brushes(ThemeVariant.Dark));
            // The export and import arrows take each theme's shade; the folder and the floppy disk are the same in both.
            foreach (string icon in new[] { "ExportIcon", "ImportIcon" })
            {
                Assert.True(Application.Current!.TryFindResource(icon, ThemeVariant.Light, out object? light));
                Assert.True(Application.Current!.TryFindResource(icon, ThemeVariant.Dark, out object? dark));
                Assert.NotSame(light, dark);
            }
            Assert.True(Application.Current!.TryFindResource("OpenIcon", ThemeVariant.Dark, out object? open));
            Assert.True(Application.Current!.TryFindResource("SaveIcon", ThemeVariant.Dark, out object? save));
            Assert.IsType<DrawingImage>(open);
            Assert.IsType<DrawingImage>(save);
        }
        [AvaloniaFact]
        public void TheApplicationsThemeIsEveryWindowsAndRemembered()
        {
            var app = (App)Application.Current!;
            ThemeVariant? before = app.RequestedThemeVariant;
            try
            {
                app.ApplyTheme(true);
                Assert.Equal(ThemeVariant.Dark, app.RequestedThemeVariant);
                app.ApplyTheme(false);
                Assert.Equal(ThemeVariant.Light, app.RequestedThemeVariant);
            }
            finally
            {
                app.RequestedThemeVariant = before;
            }
            Assert.Same(ChartPalette.Dark, ChartPalette.For(ThemeVariant.Dark));
            Assert.Same(ChartPalette.Light, ChartPalette.For(ThemeVariant.Light));
            Assert.Same(ChartPalette.Light, ChartPalette.For(null));
        }

        // ---- Help ----------------------------------------------------------------------------------------------------

        [Fact]
        public void TheManualIsTheFullOneWhenInstalledElseTheBaseOneCompiledIn()
        {
            string app = Path.Combine(_dir, "app"), temp = Path.Combine(_dir, "temp");
            Directory.CreateDirectory(app);
            Directory.CreateDirectory(temp);
            Assert.Null(HelpManual.Installed(app));
            string written = HelpManual.PathToOpen(app, temp)!;
            Assert.Equal(Path.Combine(temp, HelpManual.TempName), written);
            Assert.Equal(File.ReadAllBytes(Path.Combine(Repo.Root, "resources", "usermanual.html")), File.ReadAllBytes(written));
            File.WriteAllText(written, "stale");   // written again each time
            Assert.Equal(new FileInfo(Path.Combine(Repo.Root, "resources", "usermanual.html")).Length, new FileInfo(HelpManual.PathToOpen(app, temp)!).Length);

            string full = Path.Combine(app, HelpManual.FullManualName);
            File.WriteAllText(full, "full");
            Assert.Equal(full, HelpManual.Installed(app));
            Assert.Equal(full, HelpManual.PathToOpen(app, Path.Combine(_dir, "no such temp")));
            File.Delete(full);
            Assert.Null(HelpManual.PathToOpen(app, Path.Combine(_dir, "no such temp")));
        }

        [AvaloniaFact]
        public async Task AboutSaysTheVersionAndWhatTheApplicationDoes()
        {
            Assert.Contains("TM Data Quality Analyzer", WindowHelpActions.AboutText);
            Assert.Contains("Version " + AppInfo.Version, WindowHelpActions.AboutText);
            Assert.Contains("IRIG 106 Chapter 10 PCM recordings", WindowHelpActions.AboutText);
            (MainWindow window, _) = Show();
            Assert.IsType<WindowHelpActions>(window.Help);
            Task about = window.Help.ShowAboutAsync(window);
            Dispatcher.UIThread.RunJobs();
            var box = Assert.IsType<ReportWindow>(Assert.Single(window.OwnedWindows));
            Assert.Equal((MainWindow.AboutTitle, WindowHelpActions.AboutText), (box.Title, box.Body.Text));
            box.Close();
            Dispatcher.UIThread.RunJobs();
            Assert.True(about.IsCompleted);

            // The headless platform has no browser: a manual that cannot be shown is said so.
            Task manual = window.Help.OpenManualAsync(window);
            Dispatcher.UIThread.RunJobs();
            await Until(() => window.OwnedWindows.Count == 1);
            var failed = Assert.IsType<ReportWindow>(Assert.Single(window.OwnedWindows));
            Assert.Equal(("User Manual", MainWindow.ManualNotWritten), (failed.Title, failed.Body.Text));
            failed.Close();
            Dispatcher.UIThread.RunJobs();
            await manual;
            window.Close();
        }
    }
}
