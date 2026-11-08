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

        private MainWindowViewModel? _watched;

        /// <summary>The menu behind the hamburger button.</summary>
        public MenuFlyout MainMenu => (MenuFlyout)MenuButton.Flyout!;

        private GridLength _sidebarWidth = new(400);

        public MainWindow()
        {
            InitializeComponent();
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
