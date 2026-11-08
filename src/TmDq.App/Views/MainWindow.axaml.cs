using System;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Models.Settings;
using TmDq.App.Services;
using TmDq.App.ViewModels;

namespace TmDq.App.Views
{
    /// <summary>
    /// The main window and its shell (plan step 4.8): a title bar of its own in place of the system's, the menu behind
    /// its hamburger button (v2.x's sections, Recent Files and Help), the log sidebar's toggle, and a
    /// recording or exported CSV dropped anywhere on the window opened as Recent Files opens one.
    /// </summary>
    public sealed partial class MainWindow : Window
    {
        public const string AboutTitle = "About";
        public const string NoRecentFiles = "(No recent files)";
        public const string ManualNotWritten = "Could not open the user manual.";

        private MainWindowViewModel? _watched;

        /// <summary>The menu behind the hamburger button.</summary>
        public MenuFlyout MainMenu { get; } = new() { Placement = PlacementMode.BottomEdgeAlignedLeft };

        private GridLength _sidebarWidth = new(400);

        public MainWindow()
        {
            InitializeComponent();
            MenuButton.Flyout = MainMenu;
            MainMenu.Opening += (_, _) => BuildMenu();
            AddHandler(DragDrop.DragOverEvent, OnDragOver);
            AddHandler(DragDrop.DropEvent, OnDrop);
        }

        private MainWindowViewModel? Vm => DataContext as MainWindowViewModel;

        /// <summary>What the window does with Help &gt; User Manual and Help &gt; About; the tests replace it.</summary>
        public IHelpActions Help { get; set; } = new WindowHelpActions();

        protected override void OnDataContextChanged(EventArgs e)
        {
            base.OnDataContextChanged(e);
            if (_watched is not null) _watched.PropertyChanged -= OnModelPropertyChanged;
            _watched = Vm;
            if (_watched is not null) _watched.PropertyChanged += OnModelPropertyChanged;
            ShowSidebar(_watched?.SidebarVisible ?? true);
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == WindowStateProperty) UpdateMaximizeButton();
        }

        private void OnModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(MainWindowViewModel.SidebarVisible)) ShowSidebar(((MainWindowViewModel)sender!).SidebarVisible);
        }

        // The log's column (and the splitter's) give their width back to the chart while the sidebar is hidden, and get
        // the width the user left them when it returns.
        private void ShowSidebar(bool shown)
        {
            ColumnDefinition log = Body.ColumnDefinitions[0], splitter = Body.ColumnDefinitions[1];
            if (shown)
            {
                log.Width = _sidebarWidth;
                splitter.Width = new GridLength(4);
            }
            else
            {
                if (log.Width.Value > 0) _sidebarWidth = log.Width;
                log.Width = new GridLength(0);
                splitter.Width = new GridLength(0);
            }
        }

        // ---- the window's buttons ------------------------------------------------------------------------------------

        private void OnMinimize(object? sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

        private void OnMaximize(object? sender, RoutedEventArgs e) =>
            WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

        private void OnClose(object? sender, RoutedEventArgs e) => Close();

        /// <summary>The maximize button shows restore, and says so, while the window is maximized.</summary>
        private void UpdateMaximizeButton()
        {
            bool maximized = WindowState == WindowState.Maximized;
            MaximizeButton.Content = maximized ? "" : "";
            ToolTip.SetTip(MaximizeButton, maximized ? "Restore" : "Maximize");
        }

        // ---- the menu ------------------------------------------------------------------------------------------------

        /// <summary>
        /// The menu, rebuilt each time it opens, as v2.x's: the Process section (Open, Recent Files, Save
        /// as Template, Apply Template to Files), the Import/Export section (Import CSV, Export), the Settings section
        /// (the theme), then Help (User Manual, About) and Exit, each set off by a separator. Every entry is available
        /// as its command is.
        /// </summary>
        public void BuildMenu()
        {
            MainMenu.Items.Clear();
            if (Vm is not { } vm) return;
            MainMenu.Items.Add(Section("Process"));
            MainMenu.Items.Add(Item("Open...", vm.OpenCommand, icon: "OpenIcon", gesture: new KeyGesture(Key.O, KeyModifiers.Control)));
            MainMenu.Items.Add(RecentFilesMenu(vm));
            MainMenu.Items.Add(Item("Save as Template...", vm.SaveTemplateCommand, icon: "SaveIcon",
                                    tip: "Save the processed recording's stream settings, names and colors as a template"));
            MainMenu.Items.Add(Item("Apply Template to Files...", vm.ApplyTemplateCommand,
                                    tip: "Process several recordings with a saved template"));
            MainMenu.Items.Add(Section("Import/Export"));
            MainMenu.Items.Add(Item("Import CSV...", vm.ImportCsvCommand, icon: "ImportIcon", tip: "Import a previously exported CSV file"));
            MainMenu.Items.Add(Item("Export...", vm.ExportCommand, icon: "ExportIcon", tip: "Export plot data and images"));
            MainMenu.Items.Add(Section("Settings"));
            MainMenu.Items.Add(Item(vm.ThemeMenuText, vm.ToggleThemeCommand));
            MainMenu.Items.Add(new Separator());
            var help = new MenuItem { Header = "Help" };
            help.Items.Add(Item("User Manual...", new AsyncRelayCommand(() => Help.OpenManualAsync(this))));
            help.Items.Add(Item("About...", new AsyncRelayCommand(() => Help.ShowAboutAsync(this))));
            MainMenu.Items.Add(help);
            MainMenu.Items.Add(new Separator());
            MainMenu.Items.Add(Item("Exit", new RelayCommand(Close)));
        }

        // A section's heading: a disabled item in the "section" style, as v2.x's menu drew one.
        private static MenuItem Section(string title) => new() { Header = title, IsEnabled = false, Classes = { "section" } };

        private MenuItem Item(string header, System.Windows.Input.ICommand command, string? icon = null, string? tip = null,
                              KeyGesture? gesture = null)
        {
            var item = new MenuItem { Header = header, Command = command, InputGesture = gesture };
            if (icon is not null && this.TryFindResource(icon, ActualThemeVariant, out object? image) && image is Avalonia.Media.IImage source)
                item.Icon = new Image { Source = source, Width = 16, Height = 16 };
            if (tip is not null) ToolTip.SetTip(item, tip);
            return item;
        }

        /// <summary>Recent Files: each by its name, its path as the tooltip, newest first, then Clear Recent Files; or
        /// "(No recent files)", disabled.</summary>
        private MenuItem RecentFilesMenu(MainWindowViewModel vm)
        {
            var recent = new MenuItem { Header = "Recent Files" };
            if (vm.RecentFiles.Count == 0)
            {
                recent.Items.Add(new MenuItem { Header = NoRecentFiles, IsEnabled = false });
                return recent;
            }
            foreach (string path in vm.RecentFiles)
            {
                var item = new MenuItem { Header = Path.GetFileName(path), Command = vm.OpenRecentCommand, CommandParameter = path };
                ToolTip.SetTip(item, path);
                recent.Items.Add(item);
            }
            recent.Items.Add(new Separator());
            recent.Items.Add(new MenuItem { Header = "Clear Recent Files", Command = vm.ClearRecentFilesCommand });
            return recent;
        }

        // ---- drag and drop -------------------------------------------------------------------------------------------

        /// <summary>The first recording or exported CSV among the files dragged, by suffix; null when there is none.</summary>
        public static string? FirstSupported(IDataTransfer? data) =>
            data?.TryGetFiles()?.Select(f => f.TryGetLocalPath()).FirstOrDefault(p => p is not null && RecentFiles.IsSupported(p));

        private void OnDragOver(object? sender, DragEventArgs e) =>
            e.DragEffects = FirstSupported(e.DataTransfer) is not null && Vm is not null ? DragDropEffects.Copy : DragDropEffects.None;

        private async void OnDrop(object? sender, DragEventArgs e)
        {
            if (FirstSupported(e.DataTransfer) is not { } path || Vm is not { } vm) return;
            await vm.OpenPathAsync(path);
        }
    }

    /// <summary>Help &gt; User Manual and Help &gt; About.</summary>
    public interface IHelpActions
    {
        Task OpenManualAsync(Window owner);

        Task ShowAboutAsync(Window owner);
    }

    /// <summary>The manual opened in the browser, About in a box over the window.</summary>
    public sealed class WindowHelpActions : IHelpActions
    {
        public static string AboutText =>
            "TM Data Quality Analyzer\n\nVersion " + AppInfo.Version + "\n\n" +
            "Analyzes framesync lock statistics and receiver SNR data from IRIG 106 Chapter 10 PCM recordings and plots the " +
            "results over time.";

        public async Task OpenManualAsync(Window owner)
        {
            string? path = HelpManual.PathToOpen(AppContext.BaseDirectory, Path.GetTempPath());
            if (path is null || !await owner.Launcher.LaunchFileInfoAsync(new FileInfo(path)))
                await new ReportWindow("User Manual", MainWindow.ManualNotWritten).ShowDialog(owner);
        }

        public Task ShowAboutAsync(Window owner) => new ReportWindow(MainWindow.AboutTitle, AboutText).ShowDialog(owner);
    }
}
