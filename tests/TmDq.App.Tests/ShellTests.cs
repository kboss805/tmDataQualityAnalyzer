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
using Avalonia.VisualTree;
using TmDq.App.Models.Settings;
using TmDq.App.Platform;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.Views;
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
            // Each glyph is centred in its button: the sidebar toggle's sat in its top-left corner, its content stretched.
            Assert.All(new Control[] { window.MenuButton, window.SidebarButton, window.MinimizeButton, window.MaximizeButton, window.CloseButton },
                       b =>
                       {
                           TextBlock glyph = b.GetVisualDescendants().OfType<TextBlock>().Single();
                           Point centre = glyph.TranslatePoint(new Point(glyph.Bounds.Width / 2, glyph.Bounds.Height / 2), b)!.Value;
                           Assert.True(glyph.Bounds.Width < b.Bounds.Width && glyph.Bounds.Height < b.Bounds.Height,
                                       $"{b.Name}'s glyph is {glyph.Bounds.Size}, the button {b.Bounds.Size}");
                           Assert.InRange(centre.X, 14.0, 16.0);
                           Assert.InRange(centre.Y, 12.0, 14.0);
                       });
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
            var picker = new FakeRecordingPicker();
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(new MainWindowServices { Picker = picker }));
            window.Help = help;
            Menus.Open(window);
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
            static void Click(MenuItem item) => item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
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
            Click(Find(helpMenu.Items, "User Manual..."));
            Click(Find(helpMenu.Items, "About..."));
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(new[] { "manual", "about" }, help.Asked);

            // The theme item says where it goes, and goes there: the same item, renamed.
            MenuItem theme = Item("Switch to Light Theme");
            theme.Command!.Execute(null);
            Assert.Equal("Switch to Dark Theme", theme.Header);
            Assert.False(vm.IsDarkTheme);

            // Open is the view model's; Exit, the last entry, closes the window.
            Item("Open...").Command!.Execute(null);
            Assert.Equal(1, picker.Asked);
            bool closed = false;
            window.Closed += (_, _) => closed = true;
            Click(Item("Exit"));
            Assert.True(closed);
        }

        [AvaloniaFact]
        public void TheMenuIsDeclaredOnceAndItsHeadingsAreSmallAndBold()
        {
            (MainWindow window, MainWindowViewModel vm) = Show();
            // The same items each time it opens: bound, not built.
            List<object?> first = Menus.Open(window).Items.ToList();
            window.MainMenu.Hide();
            Assert.Equal(14, first.Count);
            Assert.Equal(first, Menus.Open(window).Items.ToList());
            window.MainMenu.Hide();

            // A section's heading is small and bold wherever the menu is shown (the style is the application's: a menu is
            // a popup of its own).
            var items = window.MainMenu.Items.ToList();
            window.MainMenu.Items.Clear();
            var host = new Window { Content = new MenuFlyoutPresenter { ItemsSource = items, DataContext = vm } };
            host.Show();
            Dispatcher.UIThread.RunJobs();
            var heading = (MenuItem)items[0]!;
            Assert.Equal((FontWeight.Bold, 11.0), (heading.FontWeight, heading.FontSize));
            Assert.NotEqual(FontWeight.Bold, ((MenuItem)items[1]!).FontWeight);
            host.Close();
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
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(new MainWindowServices { Preferences = store }));
            MenuItem recent = Find(Menus.Open(window).Items, "Recent Files");
            MenuItem none = Assert.IsType<MenuItem>(Assert.Single(Menus.Entries(recent)));
            Assert.Equal((MainWindowViewModel.NoRecentFiles, false), ((string)none.Header!, none.IsEnabled));
            window.Close();

            // Two files remembered: each by its name, newest first, and the menu follows a change while it is open.
            store.Update(p => p with { RecentFiles = new[] { @"C:\data\flight 2.ch10", @"D:\out\export.csv" } });
            (window, vm) = Show(new MainWindowViewModel(new MainWindowServices { Preferences = store }));
            recent = Find(Menus.Open(window).Items, "Recent Files");
            Assert.Equal(new[] { "flight 2.ch10", "export.csv", "-", "Clear Recent Files" }, Menus.Headers(recent));
            IReadOnlyList<object?> entries = Menus.Entries(recent);
            var first = (MenuItem)entries[0]!;
            Assert.Equal((@"C:\data\flight 2.ch10", vm.OpenRecentCommand, @"C:\data\flight 2.ch10"),
                         ((string)ToolTip.GetTip(first)!, first.Command, first.CommandParameter));
            Assert.Equal(@"D:\out\export.csv", ((MenuItem)entries[1]!).CommandParameter);
            MenuItem clear = Menus.Item(entries, "Clear Recent Files");
            Assert.Same(vm.ClearRecentFilesCommand, clear.Command);
            clear.Command!.Execute(null);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(new[] { MainWindowViewModel.NoRecentFiles }, Menus.Headers(recent));
            window.Close();
        }

        [AvaloniaFact]
        public void CtrlOOpensAndCtrlBTogglesTheSidebar()
        {
            var picker = new FakeRecordingPicker();
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(new MainWindowServices { Picker = picker }));
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
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(new MainWindowServices { Preferences = store }));
            Border log = window.FindControl<Border>("LogPanel")!;
            Assert.Equal((0.0, 0.0, false), (window.Body.ColumnDefinitions[0].Width.Value, window.Body.ColumnDefinitions[1].Width.Value, log.IsVisible));
            Assert.False(window.SidebarButton.IsChecked);

            window.SidebarButton.IsChecked = true;   // the toggle is the preference
            Dispatcher.UIThread.RunJobs();
            Assert.True(vm.SidebarVisible);
            Assert.Equal((400.0, MainWindow.SplitterWidth, true), (window.Body.ColumnDefinitions[0].Width.Value, window.Body.ColumnDefinitions[1].Width.Value, log.IsVisible));
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
            var other = new MainWindowViewModel(new MainWindowServices { Preferences = new PreferencesStore(new Preferences { SidebarVisible = false }) });
            window.DataContext = other;
            Assert.Equal(0, window.Body.ColumnDefinitions[0].Width.Value);
            vm.SidebarVisible = false;
            vm.SidebarVisible = true;
            Assert.Equal(0, window.Body.ColumnDefinitions[0].Width.Value);

            // Hidden twice over (one hidden view model for another), it still returns at the width it had.
            var hidden = new MainWindowViewModel(new MainWindowServices { Preferences = new PreferencesStore(new Preferences { SidebarVisible = false }) });
            window.DataContext = hidden;
            hidden.SidebarVisible = true;
            Assert.Equal(300, window.Body.ColumnDefinitions[0].Width.Value);
            window.Close();
        }

        [AvaloniaFact]
        public void TheLogWrapsToTheWidthTheDividerDragsItToButNoNarrowerThanItsMinimum()
        {
            var vm = new MainWindowViewModel();
            for (int i = 0; i < 6; i++)
                vm.Log.Add("[CH 13 CH-01 PRN 15 5M] 67191764 bytes processed, 16369 syncs found, 16331 frames extracted " + i);
            (MainWindow window, _) = Show(vm);
            ColumnDefinition column = window.Body.ColumnDefinitions[0];
            Border log = window.FindControl<Border>("LogPanel")!;
            SelectableTextBlock text = log.GetVisualDescendants().OfType<SelectableTextBlock>().Single();
            // Lines as laid out, not the text's bounds: in the scroll viewer it is stretched to the viewport's height.
            int Lines() => text.TextLayout.TextLines.Count;
            int at400 = Lines();

            // Dragged by its divider. A minimum on the panel rather than the column kept it 400 wide in a narrower column,
            // centred and cut off at both edges, its text wrapped as before.
            void Drag(double dx)
            {
                Point from = Frames.InWindow(window.Splitter, window).Center;
                window.MouseDown(from, MouseButton.Left, RawInputModifiers.None);
                window.MouseMove(from + new Point(dx / 2, 0), RawInputModifiers.LeftMouseButton);
                window.MouseMove(from + new Point(dx, 0), RawInputModifiers.LeftMouseButton);
                window.MouseUp(from + new Point(dx, 0), MouseButton.Left, RawInputModifiers.None);
                Dispatcher.UIThread.RunJobs();
            }
            void FitsItsColumn()
            {
                Rect panel = Frames.InWindow(log, window), t = Frames.InWindow(text, window);
                Assert.True(panel.X >= 0 && panel.Right <= column.ActualWidth + 0.5, $"panel {panel} in a {column.ActualWidth} column");
                Assert.True(t.X >= panel.X && t.Right <= panel.Right, $"text {t} in panel {panel}");
            }

            Drag(-150);
            Assert.Equal(250, column.ActualWidth, 0.5);
            FitsItsColumn();
            Assert.True(Lines() > at400, $"{Lines()} lines at 250, {at400} at 400: it did not wrap");

            Drag(-200);
            Assert.Equal(MainWindow.LogMinWidth, column.ActualWidth, 0.5);
            FitsItsColumn();

            Drag(500);
            Assert.Equal(700, column.ActualWidth, 0.5);
            FitsItsColumn();
            Assert.True(Lines() < at400, $"{Lines()} lines at 700, {at400} at 400: it did not unwrap");

            // Hidden, the column has no minimum to hold it open; shown, it has it again.
            vm.SidebarVisible = false;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal((0.0, 0.0), (column.MinWidth, column.ActualWidth));
            vm.SidebarVisible = true;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal((MainWindow.LogMinWidth, 700.0), (column.MinWidth, column.ActualWidth));
            window.Close();
        }

        [AvaloniaTheory]
        [InlineData(false)]
        [InlineData(true)]
        public void TheDividerShowsWhereToDragAndAnswersThePointer(bool dark)
        {
            var app = (App)Application.Current!;
            ThemeVariant? before = app.RequestedThemeVariant;
            app.ApplyTheme(dark);
            try
            {
                (MainWindow window, _) = Show();
                GridSplitter splitter = window.Splitter;
                Border grip = splitter.GetVisualDescendants().OfType<Border>().Single(b => b.Name == "Grip");
                Color Resource(string key)
                {
                    Assert.True(Application.Current!.TryFindResource(key, window.ActualThemeVariant, out object? b), key);
                    return ((ISolidColorBrush)b!).Color;
                }
                Color Of(IBrush? brush) => brush is ISolidColorBrush b ? b.Color : Colors.Transparent;

                // A short grip in the middle of the divider, in a shade off the window's background, drawn.
                Assert.Equal(new Size(2, 36), grip.Bounds.Size);
                Point centre = grip.TranslatePoint(new Point(1, 18), splitter)!.Value;
                Assert.Equal(splitter.Bounds.Width / 2, centre.X, 0.5);
                Assert.Equal(splitter.Bounds.Height / 2, centre.Y, 1.0);
                Assert.Equal(MainWindow.SplitterWidth, splitter.Bounds.Width);
                Assert.Equal(Resource("SplitterGrip"), Of(grip.Background));
                Assert.Equal(Colors.Transparent, Of(splitter.Background));
                using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                {
                    Rect g = Frames.InWindow(grip, window);
                    Color drawn = Color.FromUInt32(0xFF000000 | Frames.Pixel(fb, (int)g.Center.X, (int)g.Center.Y));
                    Assert.Equal(Resource("SplitterGrip"), drawn);
                }

                // Under the pointer the strip takes a shade and the grip brightens; away from it, both go back.
                Point over = Frames.InWindow(splitter, window).Center + new Point(0, 100);
                window.MouseMove(over, RawInputModifiers.None);
                Dispatcher.UIThread.RunJobs();
                Assert.Equal((Resource("SplitterHover"), Resource("SplitterGripHover")), (Of(splitter.Background), Of(grip.Background)));
                window.MouseMove(over + new Point(200, 0), RawInputModifiers.None);
                Dispatcher.UIThread.RunJobs();
                Assert.Equal((Colors.Transparent, Resource("SplitterGrip")), (Of(splitter.Background), Of(grip.Background)));
                window.Close();
            }
            finally
            {
                app.RequestedThemeVariant = before;
            }
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
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(new MainWindowServices { Configurator = FakeConfigurator.Cancelled() }));
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
                              "LogPanelBackground", "LogPanelBorder", "TitleBarHover", "SplitterGrip", "SplitterGripHover", "SplitterHover" };
            string[] Brushes(ThemeVariant theme) => keys.Select(k =>
            {
                Assert.True(Application.Current!.TryFindResource(k, theme, out object? b), k);
                Color c = ((ISolidColorBrush)b!).Color;
                return $"#{c.A:X2}{c.R:X2}{c.G:X2}{c.B:X2}";
            }).ToArray();
            // v2.x's, but for the divider's three (v2.x drew its splitter's dotted grip in the style's colour).
            Assert.Equal(new[] { "#B9FFFFFF", "#FFC8C8C8", "#FF1E1E1E", "#FF005FB8", "#E6FFFFFF", "#FFC8C8C8", "#FF1E1E1E", "#FFFFFFFF", "#FFE5E5E5", "#38808080",
                                       "#FFC2C2C2", "#FF707070", "#FFE0E0E0" },
                         Brushes(ThemeVariant.Light));
            Assert.Equal(new[] { "#B9202020", "#FF3C3C3C", "#FFDCDCDC", "#FF60CDFF", "#A0000000", "#FF3C3C3C", "#FFFFFFFF", "#FF202020", "#FF303030", "#38808080",
                                       "#FF505050", "#FF9A9A9A", "#FF2A2A2A" },
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
            Assert.Equal(File.ReadAllBytes(Path.Combine(Repo.Root, "assets", "usermanual.html")), File.ReadAllBytes(written));
            File.WriteAllText(written, "stale");   // written again each time
            Assert.Equal(new FileInfo(Path.Combine(Repo.Root, "assets", "usermanual.html")).Length, new FileInfo(HelpManual.PathToOpen(app, temp)!).Length);

            string full = Path.Combine(app, HelpManual.FullManualName);
            File.WriteAllText(full, "full");
            Assert.Equal(full, HelpManual.Installed(app));
            Assert.Equal(full, HelpManual.PathToOpen(app, Path.Combine(_dir, "no such temp")));
            File.Delete(full);
            Assert.Null(HelpManual.PathToOpen(app, Path.Combine(_dir, "no such temp")));
        }

        [Fact]
        public void TheFullManualIsTheFileTheInstallerAndThePortableCopyPutBesideTheApp()
        {
            // Help looks for it beside the application by this name; the installer's full-manual task and the portable ZIP
            // are what put it there, so a rename on either side would leave Help on the base manual without a word.
            Assert.Equal("UserManual.html", HelpManual.FullManualName);
            string setup = File.ReadAllText(Repo.File(Path.Combine("deploy", "avalonia", "tmDataQualityAnalyzer.iss")));
            Assert.Contains("#define ManualFile \"staging\\manual\\" + HelpManual.FullManualName + "\"", setup);
            Assert.Contains("Source: \"{#ManualFile}\"; DestDir: \"{app}\"", setup);
            string package = File.ReadAllText(Repo.File(Path.Combine("deploy", "avalonia", "build_avalonia_package.ps1")));
            Assert.Contains("Join-Path $Stage 'manual\\" + HelpManual.FullManualName + "'", package);
            Assert.Contains("Join-Path $portable '" + HelpManual.FullManualName + "'", package);
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
