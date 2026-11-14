using System;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using TmDq.App.Models.Settings;
using TmDq.App.Services;
using TmDq.App.ViewModels;

namespace TmDq.App.Views
{
    /// <summary>
    /// The main window and its shell (plan step 4.8): a title bar of its own in place of the system's, the menu behind
    /// its hamburger button (v2.x's sections, Recent Files and Help, declared in the AXAML and bound to the view model
    /// but for Help and Exit, which are the window's), the log sidebar's toggle, and a
    /// recording or exported CSV dropped anywhere on the window opened as Recent Files opens one.
    /// </summary>
    public sealed partial class MainWindow : Window
    {
        public const string AboutTitle = "About";
        public const string ManualNotWritten = "Could not open the user manual.";

        /// <summary>The narrowest the divider drags the log to, and the divider's width.</summary>
        public const double LogMinWidth = 200, SplitterWidth = 6;

        private MainWindowViewModel? _watched;

        /// <summary>The menu behind the hamburger button.</summary>
        public MenuFlyout MainMenu => (MenuFlyout)MenuButton.Flyout!;

        private GridLength _sidebarWidth = new(400);

        /// <summary>The size the window opens at, as v2.x's did: a standard high-definition screen's.</summary>
        public static readonly Size DefaultSize = new(1920, 1080);

        public MainWindow()
        {
            InitializeComponent();
            if (Screens.Primary is { } screen)
            {
                Size fitted = FitDefaultSize(screen.WorkingArea, screen.Scaling);
                Width = Math.Max(fitted.Width, MinWidth);
                Height = Math.Max(fitted.Height, MinHeight);
            }
            AddHandler(DragDrop.DragOverEvent, OnDragOver);
            AddHandler(DragDrop.DropEvent, OnDrop);
        }

        /// <summary>
        /// <see cref="DefaultSize"/>, no larger than a screen's working area (<paramref name="workingArea"/>, in device
        /// pixels, at <paramref name="scaling"/>): on a 1920 x 1080 screen the taskbar takes some of the height, and with
        /// the screen scaled to 125 % there are 1536 x 864 units to fill, so v2.x's window opened under the taskbar or
        /// past the screen's edge.
        /// </summary>
        public static Size FitDefaultSize(PixelRect workingArea, double scaling)
        {
            if (scaling <= 0 || workingArea.Width <= 0 || workingArea.Height <= 0) return DefaultSize;
            return new Size(Math.Min(DefaultSize.Width, Math.Floor(workingArea.Width / scaling)),
                            Math.Min(DefaultSize.Height, Math.Floor(workingArea.Height / scaling)));
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
        // the width the user left them when it returns. The log's minimum goes with it: a hidden column has none.
        private void ShowSidebar(bool shown)
        {
            ColumnDefinition log = Body.ColumnDefinitions[0], splitter = Body.ColumnDefinitions[1];
            if (shown)
            {
                log.Width = _sidebarWidth;
                log.MinWidth = LogMinWidth;
                splitter.Width = new GridLength(SplitterWidth);
            }
            else
            {
                if (log.Width.Value > 0) _sidebarWidth = log.Width;
                log.MinWidth = 0;
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

        // ---- the menu (declared in the AXAML) ------------------------------------------------------------------------

        private async void OnUserManual(object? sender, RoutedEventArgs e) => await Help.OpenManualAsync(this);

        private async void OnAbout(object? sender, RoutedEventArgs e) => await Help.ShowAboutAsync(this);

        private void OnExit(object? sender, RoutedEventArgs e) => Close();

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
