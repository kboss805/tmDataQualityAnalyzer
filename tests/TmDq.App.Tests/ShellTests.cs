using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Chrome;
using Avalonia.Controls.Documents;
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
using TmDq.App.Models.Processing;
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
            MenuItem m => (string)m.Header!,
            _ => "?",
        };

        private static MenuItem Find(IEnumerable<object?> items, string header) =>
            items.OfType<MenuItem>().Single(m => (string)m.Header! == header);

        /// <summary>An entry of the main menu by its path: its submenu, then the entry ("File", "Open...").</summary>
        private static MenuItem Entry(MainWindow window, string submenu, string header) => Find(Find(window.MainMenu.Items, submenu).Items, header);

        [AvaloniaFact]
        public async Task TheMenuHoldsFourSubmenusAndNoExit()
        {
            var help = new FakeHelp();
            var picker = new FakeRecordingPicker();
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(new MainWindowServices { Picker = picker }));
            window.Help = help;
            Menus.Open(window);
            // File, Import/Export, Settings, Help: each opens its own list; there is no Exit (the window's close button is it).
            Assert.Equal(new[] { "File", "Import/Export", "Settings", "Help" }, window.MainMenu.Items.Select(Header));
            Assert.Equal(new[] { "Open...", "Recent Files", "-", "Process Multiple Files...", "Save Template..." }, Menus.Headers(Find(window.MainMenu.Items, "File")));
            Assert.Equal(new[] { "Import from CSV...", "Export..." }, Menus.Headers(Find(window.MainMenu.Items, "Import/Export")));
            Assert.Equal(new[] { "Light Theme", "Dark Theme" }, Menus.Headers(Find(window.MainMenu.Items, "Settings")));
            Assert.Equal(new[] { "tmDataQualityAnalyzer Help", "-", "About" }, Menus.Headers(Find(window.MainMenu.Items, "Help")));
            static void Click(MenuItem item) => item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
            Assert.Equal((vm.OpenCommand, vm.ApplyTemplateCommand, vm.SaveTemplateCommand, vm.ImportCsvCommand, vm.ExportCommand),
                         (Entry(window, "File", "Open...").Command, Entry(window, "File", "Process Multiple Files...").Command,
                          Entry(window, "File", "Save Template...").Command, Entry(window, "Import/Export", "Import from CSV...").Command,
                          Entry(window, "Import/Export", "Export...").Command));
            Assert.Equal(new KeyGesture(Key.O, KeyModifiers.Control), Entry(window, "File", "Open...").InputGesture);
            Assert.Equal(("Import a previously exported CSV file", "Export plot data and images"),
                         ((string)ToolTip.GetTip(Entry(window, "Import/Export", "Import from CSV..."))!, (string)ToolTip.GetTip(Entry(window, "Import/Export", "Export..."))!));
            Assert.NotNull(ToolTip.GetTip(Entry(window, "File", "Save Template...")));
            Assert.NotNull(ToolTip.GetTip(Entry(window, "File", "Process Multiple Files...")));

            // The help and About are the window's: each asks the help service.
            MenuItem helpMenu = Find(window.MainMenu.Items, "Help");
            Click(Find(helpMenu.Items, "tmDataQualityAnalyzer Help"));
            Click(Find(helpMenu.Items, "About"));
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(new[] { "manual", "about" }, help.Asked);

            // Open is the view model's.
            Entry(window, "File", "Open...").Command!.Execute(null);
            Assert.Equal(1, picker.Asked);
            window.Close();
        }

        [AvaloniaFact]
        public void TheThemeIsTwoEntriesOfItsOwnAndTheOneInUseIsChecked()
        {
            var store = new PreferencesStore(new Preferences());
            (MainWindow window, MainWindowViewModel vm) = Show(new MainWindowViewModel(new MainWindowServices { Preferences = store }));
            Menus.Open(window);
            MenuItem light = Entry(window, "Settings", "Light Theme"), dark = Entry(window, "Settings", "Dark Theme");
            // Two radio entries (a check mark on one), bound to the view model's commands, not one entry that says where it goes.
            Assert.Equal((MenuItemToggleType.Radio, MenuItemToggleType.Radio), (light.ToggleType, dark.ToggleType));
            Assert.Equal((vm.UseLightThemeCommand, vm.UseDarkThemeCommand), (light.Command, dark.Command));
            // Dark is the default (v2.x's too).
            Assert.Equal((false, true), (light.IsChecked, dark.IsChecked));

            light.Command!.Execute(null);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal((true, false, false, "light"), (light.IsChecked, dark.IsChecked, vm.IsDarkTheme, store.Current.Theme));
            Assert.Equal(ThemeVariant.Light, window.ActualThemeVariant);   // the window follows

            dark.Command!.Execute(null);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal((false, true, true, "dark"), (light.IsChecked, dark.IsChecked, vm.IsDarkTheme, store.Current.Theme));

            // Choosing the one in use changes nothing.
            dark.Command.Execute(null);
            Assert.Equal((false, true), (light.IsChecked, dark.IsChecked));
            window.Close();
        }

        [AvaloniaFact]
        public void TheMenuIsDeclaredOnceAndOpensTheSameItemsEachTime()
        {
            (MainWindow window, _) = Show();
            // The same items each time it opens: bound, not built.
            List<object?> first = Menus.Open(window).Items.ToList();
            window.MainMenu.Hide();
            Assert.Equal(4, first.Count);
            Assert.Equal(first, Menus.Open(window).Items.ToList());
            window.MainMenu.Hide();
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
            MenuItem recent = Find(Find(Menus.Open(window).Items, "File").Items, "Recent Files");
            MenuItem none = Assert.IsType<MenuItem>(Assert.Single(Menus.Entries(recent)));
            Assert.Equal((MainWindowViewModel.NoRecentFiles, false), ((string)none.Header!, none.IsEnabled));
            window.Close();

            // Two files remembered: each by its name, newest first, and the menu follows a change while it is open.
            store.Update(p => p with { RecentFiles = new[] { @"C:\data\flight 2.ch10", @"D:\out\export.csv" } });
            (window, vm) = Show(new MainWindowViewModel(new MainWindowServices { Preferences = store }));
            recent = Find(Find(Menus.Open(window).Items, "File").Items, "Recent Files");
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

        private static Color BrushColor(Window window, string key)
        {
            Assert.True(Application.Current!.TryFindResource(key, window.ActualThemeVariant, out object? b), key);
            return ((ISolidColorBrush)b!).Color;
        }

        /// <summary>The colour drawn at a point of a window's rendered frame.</summary>
        private static Color DrawnAt(ILockedFramebuffer fb, double x, double y) =>
            Color.FromUInt32(0xFF000000 | Frames.Pixel(fb, (int)x, (int)y));

        [AvaloniaTheory]
        [InlineData(false)]
        [InlineData(true)]
        public void TheDividerIsAHairlineWithAWiderGrabAreaAndTakesTheAccentUnderThePointer(bool dark)
        {
            var app = (App)Application.Current!;
            ThemeVariant? before = app.RequestedThemeVariant;
            app.ApplyTheme(dark);
            try
            {
                (MainWindow window, _) = Show();
                GridSplitter splitter = window.Splitter;
                Border line = splitter.GetVisualDescendants().OfType<Border>().Single(b => b.Name == "Line");
                Color hairline = BrushColor(window, "DividerStrokeColorDefaultBrush"), accent = BrushColor(window, "AccentFillColorDefaultBrush");
                Rect log = Frames.InWindow(window.FindControl<Border>("LogPanel")!, window);
                double lineX = log.Right + MainWindow.SplitterWidth / 2;

                // As VS Code draws a sash: a one-pixel hairline in the divider's column, drawn, and a grab area that reaches a
                // few pixels into the log and the chart on each side of it.
                Rect grab = Frames.InWindow(splitter, window);
                Assert.Equal(MainWindow.SplitterWidth + 2 * MainWindow.SplitterReach, grab.Width);
                Assert.Equal(lineX, grab.Center.X, 0.01);
                Assert.Equal(new Size(1, grab.Height), line.Bounds.Size);
                Assert.Equal(hairline, ((ISolidColorBrush)line.Background!).Color);
                using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                    Assert.Equal(hairline, DrawnAt(fb, log.Right, grab.Center.Y));

                // The grab area answers the pointer across its whole width and not beyond it.
                double y = grab.Center.Y + 100;
                window.MouseMove(new Point(grab.Left + 0.5, y), RawInputModifiers.None);
                Dispatcher.UIThread.RunJobs();
                Assert.True(splitter.IsPointerOver);
                window.MouseMove(new Point(grab.Left - 2, y), RawInputModifiers.None);
                Dispatcher.UIThread.RunJobs();
                Assert.False(splitter.IsPointerOver);
                Assert.Equal(hairline, ((ISolidColorBrush)line.Background!).Color);

                // Under the pointer the line thickens and takes the accent, and is drawn so; away from it, both go back.
                window.MouseMove(new Point(lineX, y), RawInputModifiers.None);
                Dispatcher.UIThread.RunJobs();
                Assert.Equal((3.0, accent), (line.Bounds.Width, ((ISolidColorBrush)line.Background!).Color));
                using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                    Assert.Equal(accent, DrawnAt(fb, lineX, grab.Center.Y));
                window.MouseMove(new Point(lineX + 200, y), RawInputModifiers.None);
                Dispatcher.UIThread.RunJobs();
                Assert.Equal((1.0, hairline), (line.Bounds.Width, ((ISolidColorBrush)line.Background!).Color));
                window.Close();
            }
            finally
            {
                app.RequestedThemeVariant = before;
            }
        }

        [AvaloniaTheory]
        [InlineData(false)]
        [InlineData(true)]
        public void TheLogIsTheChromeTheChartTheContentFlushBesideOneAnotherAndDividedByHairlines(bool dark)
        {
            var app = (App)Application.Current!;
            ThemeVariant? before = app.RequestedThemeVariant;
            app.ApplyTheme(dark);
            try
            {
                (MainWindow window, MainWindowViewModel vm) = Show();
                Color chrome = BrushColor(window, "SolidBackgroundFillColorBaseBrush"), content = BrushColor(window, "LayerFillColorDefaultBrush"),
                      hairline = BrushColor(window, "DividerStrokeColorDefaultBrush");
                Assert.Equal(chrome, ((ISolidColorBrush)window.Background!).Color);
                Border logPanel = window.FindControl<Border>("LogPanel")!;
                Assert.Equal(chrome, ((ISolidColorBrush)logPanel.Background!).Color);
                Assert.Equal(content, ((ISolidColorBrush)window.PlotPanel.Background!).Color);
                // VS Code's way, in both themes: the content a shade lighter than the chrome around it.
                Assert.True(content.R > chrome.R && content.G > chrome.G && content.B > chrome.B, $"{content} is not lighter than {chrome}");

                // Flush: the title bar, its hairline, then the log, the divider's column and the chart side by side, filling
                // the window down to the status bar with no gap and no border of their own.
                Rect title = Frames.InWindow(window.TitleBar, window), rule = Frames.InWindow(window.TitleBarLine, window),
                     log = Frames.InWindow(logPanel, window), plot = Frames.InWindow(window.PlotPanel, window), body = Frames.InWindow(window.Body, window);
                Assert.Equal(1, rule.Height);
                Assert.Equal((title.Bottom, rule.Bottom), (rule.Top, body.Top));
                Assert.Equal((body.Left, body.Top, body.Bottom), (log.Left, log.Top, log.Bottom));
                Assert.Equal((body.Right, body.Top, body.Bottom), (plot.Right, plot.Top, plot.Bottom));
                Assert.Equal(plot.Left, log.Right + MainWindow.SplitterWidth);
                Assert.Equal(Frames.InWindow(window.StatusBar, window).Top, body.Bottom);
                Assert.Equal(window.ClientSize.Height, Frames.InWindow(window.StatusBar, window).Bottom);
                Assert.Equal(new Thickness(0), logPanel.BorderThickness);
                Assert.Equal(new Thickness(0), window.PlotPanel.BorderThickness);

                // Drawn: the chrome under the title bar's buttons and in the log, the hairline under the title bar and between
                // the log and the chart, the content in the chart's corner.
                using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                {
                    Assert.Equal(chrome, DrawnAt(fb, window.Caption.Bounds.Center.X, window.Caption.Bounds.Center.Y));
                    Assert.Equal(hairline, DrawnAt(fb, rule.Center.X, rule.Top));
                    Assert.Equal(chrome, DrawnAt(fb, log.Center.X, log.Bottom - 8));
                    Assert.Equal(hairline, DrawnAt(fb, log.Right, log.Center.Y));
                    Assert.Equal(content, DrawnAt(fb, plot.Left + 6, plot.Top + 6));
                    Assert.Equal(content, DrawnAt(fb, plot.Right - 30, plot.Bottom - 30));
                }

                // With the log hidden the chart fills the window to its left edge.
                vm.SidebarVisible = false;
                Dispatcher.UIThread.RunJobs();
                Assert.Equal((body.Left, body.Right), (Frames.InWindow(window.PlotPanel, window).Left, Frames.InWindow(window.PlotPanel, window).Right));
                window.Close();
            }
            finally
            {
                app.RequestedThemeVariant = before;
            }
        }

        [AvaloniaTheory]
        [InlineData(false)]
        [InlineData(true)]
        public void TheControlsFluentDrawsTakeVsCodesInputsButtonsAccentAndText(bool dark)
        {
            var app = (App)Application.Current!;
            ThemeVariant? before = app.RequestedThemeVariant;
            app.ApplyTheme(dark);
            try
            {
                var input = new TextBox { Width = 160 };
                var secondary = new Button { Content = "Cancel", Width = 80 };
                var primary = new Button { Content = "OK", Width = 80, Classes = { "accent" } };
                var box = new CheckBox { Content = "Option" };
                var window = new Window
                {
                    Width = 300, Height = 260, Content = new StackPanel { Margin = new Thickness(20), Spacing = 10, Children = { input, secondary, primary, box } },
                };
                window.Show();
                Dispatcher.UIThread.RunJobs();

                // Drawn, not only looked up: a pixel inside each control's padding, clear of its text and its border.
                using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                {
                    Color Inside(Control c) => DrawnAt(fb, Frames.InWindow(c, window).Left + 6, Frames.InWindow(c, window).Top + 4);
                    Assert.Equal(Color.Parse(dark ? "#313131" : "#FFFFFF"), Inside(input));                   // VS Code's inputs
                    Assert.Equal(Color.Parse(dark ? "#1F1F1F" : "#E5E5E5"), Inside(secondary));               // its secondary button: ghost, or grey
                    Assert.Equal(Color.Parse(dark ? "#0078D4" : "#005FB8"), Inside(primary));                 // its accent
                    Color empty = DrawnAt(fb, Frames.InWindow(box, window).Left + 10, Frames.InWindow(box, window).Top + 11);
                    Assert.Equal(Color.Parse(dark ? "#313131" : "#FFFFFF"), empty);                       // its checkbox
                }

                // And the text and the accent, and the popups' fills, as Fluent looks them up.
                string[] keys = { "SystemControlForegroundBaseHighBrush", "CheckBoxCheckBackgroundFillChecked", "TextControlBorderBrushFocused",
                                  "ComboBoxBackground", "ComboBoxBorderBrush", "ComboBoxDropDownBackground", "MenuFlyoutPresenterBackground", "ToolTipBackground" };
                string[] found = keys.Select(k => BrushColor(window, k)).Select(c => $"#{c.R:X2}{c.G:X2}{c.B:X2}").ToArray();
                Assert.Equal(dark ? new[] { "#CCCCCC", "#0078D4", "#0078D4", "#313131", "#3C3C3C", "#1F1F1F", "#1F1F1F", "#202020" }
                                  : new[] { "#3B3B3B", "#005FB8", "#005FB8", "#FFFFFF", "#CECECE", "#FFFFFF", "#FFFFFF", "#F8F8F8" }, found);
                window.Close();
            }
            finally
            {
                app.RequestedThemeVariant = before;
            }
        }

        [AvaloniaTheory]
        [InlineData(false)]
        [InlineData(true)]
        public void EveryDialogIsOnTheContentBackgroundAndTheMainWindowOnTheChrome(bool dark)
        {
            var app = (App)Application.Current!;
            ThemeVariant? before = app.RequestedThemeVariant;
            app.ApplyTheme(dark);
            try
            {
                foreach ((Type kind, Window window) in IconTests.EveryWindowKind())
                {
                    window.Show();
                    Dispatcher.UIThread.RunJobs();
                    try
                    {
                        // The dialogs are on the surface the chart is; the main window is the chrome around it.
                        Color expected = BrushColor(window, kind == typeof(MainWindow) ? "SolidBackgroundFillColorBaseBrush" : "LayerFillColorDefaultBrush");
                        Assert.True(window.Background is ISolidColorBrush, kind.Name + " has no plain background");
                        Assert.Equal((kind.Name, expected), (kind.Name, ((ISolidColorBrush)window.Background!).Color));
                        // Drawn: the window's top-left corner, inside every window's margins.
                        using ILockedFramebuffer fb = Frames.Capture(window).Lock();
                        Assert.Equal((kind.Name, expected), (kind.Name, DrawnAt(fb, 1, 1)));
                    }
                    finally
                    {
                        window.Close();
                    }
                }
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
            await Until(() => vm.Status.StartsWith("Cancelled.", StringComparison.Ordinal), () => vm.Status);
            Assert.Equal(new[] { Prn }, vm.RecentFiles);

            // An exported CSV is imported.
            vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, new[] { 0.0, 1.0 }, new[] { 50.0, 60.0 }) });
            string csv = Path.Combine(_dir, "dropped.csv");
            Assert.True(vm.Plot.ExportCsv(csv));
            window.DragDrop(at, RawDragEventType.DragEnter, await Files(window, csv), DragDropEffects.Copy, RawInputModifiers.None);
            window.DragDrop(at, RawDragEventType.Drop, await Files(window, csv), DragDropEffects.Copy, RawInputModifiers.None);
            await Until(() => vm.Status == "Imported dropped.csv.", () => vm.Status);

            // While a run is busy nothing is offered, and a drop is not taken (audit B11): the drop follows Recent Files'
            // rule, which is the command's.
            var answer = new TaskCompletionSource<StreamsChoice?>();
            (MainWindow busyWindow, MainWindowViewModel busy) = Show(new MainWindowViewModel(new MainWindowServices { Configurator = FakeConfigurator.Waiting(answer.Task) }));
            busyWindow.AddHandler(DragDrop.DragOverEvent, (_, e) => offered = e.DragEffects, RoutingStrategies.Bubble, handledEventsToo: true);
            Task running = busy.LoadAsync(Prn);
            await Until(() => busy.IsBusy, () => busy.Status);
            offered = DragDropEffects.Move;
            busyWindow.DragDrop(at, RawDragEventType.DragEnter, await Files(busyWindow, csv), DragDropEffects.Copy, RawInputModifiers.None);
            busyWindow.DragDrop(at, RawDragEventType.DragOver, await Files(busyWindow, csv), DragDropEffects.Copy, RawInputModifiers.None);
            Assert.Equal(DragDropEffects.None, offered);
            busyWindow.DragDrop(at, RawDragEventType.Drop, await Files(busyWindow, csv), DragDropEffects.Copy, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.DoesNotContain(csv, busy.RecentFiles);
            answer.SetResult(null);
            await running;
            busyWindow.Close();

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
            NativeCore.EnsureLoaded();   // the plot's series are made through the core
            (MainWindow window, MainWindowViewModel vm) = Show();
            vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, new[] { 0.0, 1.0 }, new[] { 50.0, 60.0 }) });
            vm.Plot.SetXViewByUser(0.2, 0.8);   // a chip to see
            Dispatcher.UIThread.RunJobs();
            PlotView view = window.FindControl<PlotView>("PlotView")!;
            Border log = window.FindControl<Border>("LogPanel")!;
            Frames.Capture(window).Dispose();   // the chart's layer painted in the theme it opened in

            foreach ((ThemeVariant theme, ChartPalette palette, string logBackground, string chip) in new[]
                     {
                         (ThemeVariant.Dark, ChartPalette.Dark, "#FF181818", "#B91F1F1F"),
                         (ThemeVariant.Light, ChartPalette.Light, "#FFF8F8F8", "#B9FFFFFF"),
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
        public void ThePalettesAreVsCodeModerns()
        {
            // VS Code's Light Modern and Dark Modern, as numbers: the chart the editor's colour, its text and axes the editor's
            // text, its title the links', its legend's border the inputs'.
            Assert.Equal(new[] { "#FFFFFFFF", "#FF3B3B3B", "#FFC8C8C8", "#FF005FB8", "#B9FFFFFF", "#FFCECECE", "#FF3B3B3B" }, Hex(ChartPalette.Light));
            Assert.Equal(new[] { "#FF1F1F1F", "#FFCCCCCC", "#FF3C3C3C", "#FF4DAAFC", "#B91F1F1F", "#FF3C3C3C", "#FFCCCCCC" }, Hex(ChartPalette.Dark));
        }

        private static string[] Hex(ChartPalette p) => new[] { p.Background, p.Foreground, p.Grid, p.Title, p.LegendBackground, p.LegendBorder, p.LegendText }
            .Select(c => $"#{c.A:X2}{c.R:X2}{c.G:X2}{c.B:X2}").ToArray();

        /// <summary>Fluent's semantic tokens, with VS Code's Light Modern and Dark Modern values (and Fluent's own for a status).</summary>
        private static readonly (string Token, string Light, string Dark)[] Tokens =
        {
            ("SolidBackgroundFillColorBase", "#FFF8F8F8", "#FF181818"),        // the window, title bar and log: VS Code's title bar and side bar
            ("LayerFillColorDefault", "#FFFFFFFF", "#FF1F1F1F"),               // the chart and every dialog: its editor
            ("CardBackgroundFillColorDefault", "#FFF8F8F8", "#FF202020"),      // its widgets: tooltips
            ("ControlFillColorDefault", "#FFFFFFFF", "#FF313131"),             // its inputs and dropdowns
            ("ButtonFillColorDefault", "#FFE5E5E5", "#00000000"),              // its secondary button: grey, or ghost
            ("ButtonFillColorHover", "#FFCCCCCC", "#FF2B2B2B"),
            ("SubtleFillColorSecondary", "#38808080", "#38808080"),            // a subtle control under the pointer: the title bar's buttons
            ("ControlStrokeColorDefault", "#FFCECECE", "#FF3C3C3C"),           // an input's border
            ("ButtonStrokeColorDefault", "#1A000000", "#1AFFFFFF"),
            ("DividerStrokeColorDefault", "#FFE5E5E5", "#FF2B2B2B"),           // the lines between the panes
            ("SurfaceStrokeColorDefault", "#FFE5E5E5", "#FF313131"),           // a widget's border
            ("TextFillColorPrimary", "#FF3B3B3B", "#FFCCCCCC"),
            ("TextFillColorSecondary", "#FF616161", "#FF9D9D9D"),
            ("AccentFillColorDefault", "#FF005FB8", "#FF0078D4"),
            ("AccentTextFillColorPrimary", "#FF005FB8", "#FF4DAAFC"),          // its links
            ("SystemFillColorSuccess", "#FF0F7B0F", "#FF6CCB5F"),              // what a status is painted in: the log, the dialogs' marks
            ("SystemFillColorCaution", "#FF9D5D00", "#FFFCE100"),
            ("SystemFillColorCritical", "#FFC42B1C", "#FFFF99A4"),
            ("SystemFillColorNeutral", "#72000000", "#9CFFFFFF"),
            ("TextOnAccentFillColorPrimary", "#FFFFFFFF", "#FFFFFFFF"),         // text on the accent: a hovered menu row
        };

        private static string Hex(Color c) => $"#{c.A:X2}{c.R:X2}{c.G:X2}{c.B:X2}";

        private static object Resource(string key, ThemeVariant theme)
        {
            Assert.True(Application.Current!.TryFindResource(key, theme, out object? value), key);
            return value!;
        }

        [AvaloniaFact]
        public void EachThemesTokensAreFluentsNamesInVsCodesValues()
        {
            foreach ((ThemeVariant theme, int which) in new[] { (ThemeVariant.Light, 0), (ThemeVariant.Dark, 1) })
                foreach ((string token, string light, string dark) in Tokens)
                {
                    string expected = which == 0 ? light : dark;
                    // The Color, and the Brush over it (resolved: a brush whose colour never arrived would read transparent).
                    Assert.Equal((token, theme.Key, expected), (token, theme.Key, Hex((Color)Resource(token, theme))));
                    Assert.Equal((token + "Brush", theme.Key, expected), (token + "Brush", theme.Key, Hex(((ISolidColorBrush)Resource(token + "Brush", theme)).Color)));
                }
            // What the chips and the "Loading..." label over the chart are drawn in, which no token has: a translucent layer.
            Assert.Equal(new[] { "#B9FFFFFF", "#F2F8F8F8" }, new[] { "ChipBackground", "LoadingBackground" }.Select(k => Hex(((ISolidColorBrush)Resource(k, ThemeVariant.Light)).Color)));
            Assert.Equal(new[] { "#B91F1F1F", "#F2202020" }, new[] { "ChipBackground", "LoadingBackground" }.Select(k => Hex(((ISolidColorBrush)Resource(k, ThemeVariant.Dark)).Color)));
        }

        [AvaloniaFact]
        public void EveryBrushFluentLooksItsControlsUpUnderIsAnAliasOfTheTokenForItsJob()
        {
            // The brushes Fluent's controls, and our chips and loading label, are drawn with are never colours of their own:
            // each is its token's, in both themes, so a token changed changes them all and a control is on the token for its job.
            var aliases = new (string Token, string[] Brushes)[]
            {
                ("ControlFillColorDefault", new[] { "TextControlBackground", "TextControlBackgroundPointerOver", "TextControlBackgroundFocused",
                    "ComboBoxBackground", "ComboBoxBackgroundPointerOver", "ComboBoxBackgroundPressed",
                    "CheckBoxCheckBackgroundFillUnchecked", "CheckBoxCheckBackgroundFillUncheckedPointerOver" }),
                ("ControlStrokeColorDefault", new[] { "TextControlBorderBrush", "TextControlBorderBrushPointerOver", "ComboBoxBorderBrush",
                    "ComboBoxBorderBrushPointerOver", "ComboBoxBorderBrushPressed", "ComboBoxDropDownBorderBrush", "CheckBoxCheckBackgroundStrokeUnchecked",
                    "CheckBoxCheckBackgroundStrokeUncheckedPointerOver", "MenuFlyoutPresenterBorderBrush", "ToolTipBorderBrush", "ChipBorder" }),
                ("LayerFillColorDefault", new[] { "ComboBoxDropDownBackground", "MenuFlyoutPresenterBackground" }),
                ("AccentFillColorDefault", new[] { "MenuFlyoutItemBackgroundPointerOver", "MenuFlyoutItemBackgroundPressed" }),
                ("TextOnAccentFillColorPrimary", new[] { "MenuFlyoutItemForegroundPointerOver", "MenuFlyoutItemForegroundPressed",
                    "MenuFlyoutItemKeyboardAcceleratorTextForegroundPointerOver", "MenuFlyoutItemKeyboardAcceleratorTextForegroundPressed",
                    "MenuFlyoutSubItemChevronPointerOver", "MenuFlyoutSubItemChevronPressed", "MenuFlyoutSubItemChevronSubMenuOpened" }),
                ("TextFillColorSecondary", new[] { "MenuFlyoutItemKeyboardAcceleratorTextForeground" }),
                ("CardBackgroundFillColorDefault", new[] { "ToolTipBackground" }),
                ("ButtonFillColorDefault", new[] { "ButtonBackground" }),
                ("ButtonFillColorHover", new[] { "ButtonBackgroundPointerOver", "ButtonBackgroundPressed" }),
                ("ButtonStrokeColorDefault", new[] { "ButtonBorderBrush", "ButtonBorderBrushPointerOver", "ButtonBorderBrushPressed" }),
                ("TextFillColorPrimary", new[] { "ChipForeground", "LoadingForeground" }),
                ("AccentTextFillColorPrimary", new[] { "ChipAccent" }),
                ("SurfaceStrokeColorDefault", new[] { "LoadingBorder" }),
            };
            foreach (ThemeVariant theme in new[] { ThemeVariant.Light, ThemeVariant.Dark })
                foreach ((string token, string[] brushes) in aliases)
                    foreach (string brush in brushes)
                        Assert.Equal((brush, theme.Key, Hex((Color)Resource(token, theme))), (brush, theme.Key, Hex(((ISolidColorBrush)Resource(brush, theme)).Color)));
        }

        [AvaloniaFact]
        public void TheFontTheTypeAndTheCornersAreFluents()
        {
            NativeCore.EnsureLoaded();   // the plot's series are made through the core
            ThemeVariant theme = ThemeVariant.Dark;
            double Double(string key) => (double)Resource(key, theme);
            CornerRadius Radius(string key) => (CornerRadius)Resource(key, theme);
            // Segoe UI Variable, which Windows 11's Fluent is drawn in (Avalonia's own Fluent theme asks for Inter, which is not
            // bundled, and so fell back to the system font); a caption of 12 and a body of 14; a control's corners 4 and an
            // overlay's 8 (Avalonia's are 3 and 5).
            Assert.Equal("Segoe UI Variable Text", ((FontFamily)Resource("ContentControlThemeFontFamily", theme)).FamilyNames[0]);
            Assert.Equal((12.0, 14.0), (Double("CaptionTextBlockFontSize"), Double("BodyTextBlockFontSize")));
            Assert.Equal((new CornerRadius(4), new CornerRadius(8)), (Radius("ControlCornerRadius"), Radius("OverlayCornerRadius")));

            // Fluent's own controls, and every window, take them.
            var input = new TextBox();
            var button = new Button { Content = "OK" };
            var plain = new Window { Content = new StackPanel { Children = { input, button } } };
            plain.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.All(new Control[] { plain, input, button }, c => Assert.Equal("Segoe UI Variable Text", TextElement.GetFontFamily(c).FamilyNames[0]));
            Assert.All(new[] { input.CornerRadius, button.CornerRadius }, r => Assert.Equal(new CornerRadius(4), r));
            plain.Close();

            // And what we draw ourselves: the chips and the title bar's buttons are controls, the legend and the loading label
            // are overlays; the chart's and the log's text are captions, the loading label's a body. The numbers in code are the
            // tokens'.
            (MainWindow window, MainWindowViewModel vm) = Show();
            vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, new[] { 0.0, 1.0 }, new[] { 50.0, 60.0 }) });
            vm.Plot.SetXViewByUser(0.2, 0.8);
            Dispatcher.UIThread.RunJobs();
            PlotView view = window.FindControl<PlotView>("PlotView")!;
            Assert.Equal((4, 4, 4, 4), (view.ResetChip.CornerRadius.TopLeft, window.MinimizeButton.CornerRadius.TopLeft, window.MenuButton.CornerRadius.TopLeft,
                                       window.SidebarButton.CornerRadius.TopLeft));
            Assert.Equal((8, 8), (view.Legend.CornerRadius.TopLeft, view.LoadingLabel.CornerRadius.TopLeft));
            Assert.Equal(PlotLegend.CornerRadiusPx, Radius("OverlayCornerRadius").TopLeft);
            Assert.Equal(PlotChart.FontSize, Double("CaptionTextBlockFontSize"));
            Assert.Equal(Double("CaptionTextBlockFontSize"), view.ResetChip.FontSize);
            Assert.Equal(Double("CaptionTextBlockFontSize"), window.FindControl<Border>("LogPanel")!.GetVisualDescendants().OfType<SelectableTextBlock>().Single().FontSize);
            Assert.Equal(Double("BodyTextBlockFontSize"), view.LoadingLabel.GetVisualDescendants().OfType<TextBlock>().Single().FontSize);
            window.Close();
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
