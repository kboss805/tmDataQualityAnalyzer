using System;
using System.ComponentModel;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Plot;
using TmDq.App.Services;

namespace TmDq.App.Views
{
    /// <summary>
    /// The plot window's whole content (plan step 4.4), the Qt <c>PlotWidget</c> less the drawing (4.3b's
    /// <see cref="PlotChart"/>) and export (4.5): the chart, with nothing around it (US4.1), and over it the chip bar at the
    /// top left and the legend at the top right. Every control is in the right-click menu, rebuilt each time it opens so
    /// it is always current, or on a chip; the most used have single keys, which act only while the chart has focus (a
    /// bare letter taken application-wide would be lost to every text box).
    /// </summary>
    public sealed partial class PlotView : UserControl
    {
        public static readonly StyledProperty<PlotViewModel?> PlotProperty =
            AvaloniaProperty.Register<PlotView, PlotViewModel?>(nameof(Plot));

        private PlotViewModel? _watched;
        private IPlotPrompts? _prompts;

        public PlotView()
        {
            InitializeComponent();
            Chart.ContextRequested += OnContextRequested;
            ViewModeChip.Click += (_, _) => Plot?.ToggleViewMode();
            ResetChip.Click += (_, _) => Plot?.ResetView();
            Overlay.SizeChanged += (_, e) => Legend.HostResized(e.NewSize);
        }

        public PlotViewModel? Plot
        {
            get => GetValue(PlotProperty);
            set => SetValue(PlotProperty, value);
        }

        /// <summary>The questions the menu asks; the windows unless a test sets its own.</summary>
        public IPlotPrompts Prompts
        {
            get => _prompts ??= new WindowPlotPrompts(this);
            set => _prompts = value;
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property != PlotProperty) return;
            if (_watched is not null)
            {
                _watched.SeriesChanged -= OnPlotChanged;
                _watched.RangesChanged -= OnPlotChanged;
                _watched.PropertyChanged -= OnPlotPropertyChanged;
            }
            _watched = Plot;
            Chart.Plot = Plot;
            if (_watched is not null)
            {
                _watched.SeriesChanged += OnPlotChanged;
                _watched.RangesChanged += OnPlotChanged;
                _watched.PropertyChanged += OnPlotPropertyChanged;
            }
            Refresh();
        }

        private void OnPlotPropertyChanged(object? sender, PropertyChangedEventArgs e) => Refresh();

        private void OnPlotChanged(object? sender, EventArgs e) => Refresh();

        /// <summary>The chips and the legend for the plot as it is now.</summary>
        private void Refresh()
        {
            PlotViewModel? vm = Plot;
            bool hasData = vm?.HasData == true;

            // The View Mode chip only when there is another metric to switch to, named for the one it switches TO; the
            // Reset chip only once the view differs from the default. With neither, the bar has no size.
            bool switchable = vm?.CanSwitchViewMode == true;
            ViewModeChip.IsVisible = switchable;
            if (switchable)
            {
                bool lockShown = vm!.LockAxisView == LockAxisView.LockPercent;
                ViewModeChip.Content = lockShown ? "Show Accumulation" : "Show Lock %";
                ToolTip.SetTip(ViewModeChip, lockShown
                    ? "Switch the left axis to Accumulated Missed Frames"
                    : "Switch the left axis to Frame Sync Lock %");
            }
            ResetChip.IsVisible = vm?.CanResetView == true;
            ChipBar.IsVisible = hasData;
            LoadingLabel.IsVisible = vm?.IsLoading == true;

            Legend.Show(hasData ? vm!.LegendEntries() : Array.Empty<LegendEntry>(), vm?.LegendVisible ?? true, Overlay.Bounds.Size);
        }

        // ---- the context menu ---------------------------------------------------------------------------------------

        private void OnContextRequested(object? sender, ContextRequestedEventArgs e)
        {
            ContextMenu? menu = BuildContextMenu();
            if (menu is null) return;
            Chart.ContextMenu = menu;
            menu.Open(Chart);
            e.Handled = true;
        }

        /// <summary>
        /// The right-click menu, as the Qt <c>buildContextMenu</c> has it: Set Plot Title first; Plot File; View Mode, its
        /// key in its title (a submenu can show no key of its own); Customize View; Show Legend; Readout; X Axis; Y Axes;
        /// Reset View. Everything that acts on data is disabled until there is some. Export is not here: it is a file
        /// operation, in the main menu (plan step 4.5). Null with no plot.
        /// </summary>
        public ContextMenu? BuildContextMenu()
        {
            PlotViewModel? vm = Plot;
            if (vm is null) return null;
            bool hasData = vm.HasData;
            var commands = new PlotCommands(vm, Prompts);
            var menu = new ContextMenu();

            menu.Items.Add(Item("Set Plot Title...", commands.SetTitleAsync, hasData));
            menu.Items.Add(new Separator());

            // Which processed file to view: only with more than one, which batch runs bring (plan step 4.7).
            menu.Items.Add(new MenuItem { Header = "Plot File", IsEnabled = false });

            var viewMode = new MenuItem { Header = "View Mode  (V)", IsEnabled = vm.CanSwitchViewMode };
            viewMode.Items.Add(Radio("Lock Percentage", "viewmode", vm.LockAxisView == LockAxisView.LockPercent,
                                     () => vm.SetLockAxisView(LockAxisView.LockPercent)));
            viewMode.Items.Add(Radio("Accumulation", "viewmode", vm.LockAxisView == LockAxisView.MissedFrames,
                                     () => vm.SetLockAxisView(LockAxisView.MissedFrames)));
            menu.Items.Add(viewMode);
            menu.Items.Add(new Separator());

            menu.Items.Add(Item("Customize View...", commands.CustomizeAsync, hasData));
            var legend = new MenuItem
            {
                Header = "Show Legend",
                ToggleType = MenuItemToggleType.CheckBox,
                IsChecked = vm.LegendVisible,
                IsEnabled = hasData,
                InputGesture = new KeyGesture(Key.L),
                Command = new RelayCommand(() => vm.LegendVisible = !vm.LegendVisible),
            };
            menu.Items.Add(legend);

            // The readout follows the nearest series unless pinned to one of those shown.
            var readout = new MenuItem { Header = "Readout", IsEnabled = hasData };
            if (hasData)
            {
                vm.DropStaleReadoutPin();
                readout.Items.Add(Radio("Nearest series (automatic)", "readout", vm.ReadoutSeriesId is null,
                                        () => vm.ReadoutSeriesId = null));
                readout.Items.Add(new Separator());
                foreach (PlotSeries s in vm.Series)
                {
                    if (!vm.EffectiveVisible(s)) continue;
                    int id = s.Id;
                    readout.Items.Add(Radio(s.Name, "readout", vm.ReadoutSeriesId == id, () => vm.ReadoutSeriesId = id));
                }
            }
            menu.Items.Add(readout);
            menu.Items.Add(new Separator());

            // Zoom Back above Reset Span: one level out, then every level at once.
            var xAxis = new MenuItem { Header = "X Axis", IsEnabled = hasData };
            xAxis.Items.Add(Item("Set Time Window...", commands.SetTimeWindowAsync));
            xAxis.Items.Add(Item("Zoom Back", vm.ZoomBack, hasData && vm.CanZoomBack, new KeyGesture(Key.Back)));
            xAxis.Items.Add(Item("Reset Span", vm.ResetXRange, true, new KeyGesture(Key.Home)));
            menu.Items.Add(xAxis);

            // Each axis's two limits together, minimum first.
            var yAxes = new MenuItem { Header = "Y Axes", IsEnabled = hasData };
            yAxes.Items.Add(Item("Set Left Min...", commands.SetLeftMinAsync));
            yAxes.Items.Add(Item("Set Left Max...", commands.SetLeftMaxAsync));
            yAxes.Items.Add(new Separator());
            yAxes.Items.Add(Item("Set Right Min...", commands.SetRightMinAsync));
            yAxes.Items.Add(Item("Set Right Max...", commands.SetRightMaxAsync));
            yAxes.Items.Add(new Separator());
            yAxes.Items.Add(Item("Reset", vm.ResetYRange));
            menu.Items.Add(yAxes);

            // Both axes at once: the menu's Reset view chip, and the item R is shown on.
            menu.Items.Add(Item("Reset View", vm.ResetView, hasData, new KeyGesture(Key.R)));
            return menu;
        }

        private static MenuItem Item(string header, Action act, bool enabled = true, KeyGesture? gesture = null) =>
            new() { Header = header, IsEnabled = enabled, InputGesture = gesture, Command = new RelayCommand(act) };

        private static MenuItem Item(string header, Func<Task> act, bool enabled = true) =>
            new() { Header = header, IsEnabled = enabled, Command = new AsyncRelayCommand(act) };

        private static MenuItem Radio(string header, string group, bool isChecked, Action act) => new()
        {
            Header = header,
            ToggleType = MenuItemToggleType.Radio,
            GroupName = group,
            IsChecked = isChecked,
            Command = new RelayCommand(act),
        };

        // ---- the keys -----------------------------------------------------------------------------------------------

        /// <summary>
        /// L the legend, V the metric, R both axes, Backspace one zoom back, Home the whole span (keeping a Y limit set),
        /// the arrows a tenth of the window, + and - the window about its centre. Every one acts on data, so without any
        /// the key goes on unhandled. They reach here only from inside this control, which the chart's click focuses.
        /// </summary>
        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            PlotViewModel? vm = Plot;
            if (e.Handled || vm?.HasData != true) return;
            switch (e.Key)
            {
                case Key.L: vm.LegendVisible = !vm.LegendVisible; break;
                case Key.V: vm.ToggleViewMode(); break;
                case Key.R: vm.ResetView(); break;
                case Key.Back: vm.ZoomBack(); break;
                case Key.Home: vm.ResetXRange(); break;
                case Key.Left: vm.PanWindow(-PlotViewModel.KeyPanFraction); break;
                case Key.Right: vm.PanWindow(PlotViewModel.KeyPanFraction); break;
                case Key.OemPlus:
                case Key.Add: vm.ZoomWindow(PlotViewModel.KeyZoomFactor); break;
                case Key.OemMinus:
                case Key.Subtract: vm.ZoomWindow(1.0 / PlotViewModel.KeyZoomFactor); break;
                default: return;
            }
            e.Handled = true;
        }
    }
}
