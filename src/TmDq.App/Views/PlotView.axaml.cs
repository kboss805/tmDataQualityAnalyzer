using System;
using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using TmDq.App.Platform;
using TmDq.App.Services;
using TmDq.App.ViewModels.Plot;

namespace TmDq.App.Views
{
    /// <summary>
    /// The plot window's whole content (plan step 4.4), less the drawing (4.3b's
    /// <see cref="PlotChart"/>) and export (4.5): the chart, with nothing around it (US4.1), and over it the chip bar at the
    /// top left and the legend at the top right. Every control is in the right-click menu or on a chip, and the most used
    /// have single keys: all of them declared in the AXAML and bound to the plot's <see cref="PlotCommands"/>, which this
    /// view makes for its plot with its <see cref="Prompts"/>.
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
            // Without a plot there is no menu; with one, a readout pin gone stale is dropped first.
            ChartMenu.Opening += (_, e) =>
            {
                if (Commands is { } commands) commands.MenuOpening();
                else e.Cancel = true;
            };
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
            set
            {
                _prompts = value;
                MakeCommands();
            }
        }

        /// <summary>What the menu, the chips and the keys are bound to; null without a plot.</summary>
        public PlotCommands? Commands => Root.DataContext as PlotCommands;

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
            MakeCommands();
            ShowLegend();
        }

        private void MakeCommands()
        {
            Commands?.Dispose();
            Root.DataContext = Plot is { } plot ? new PlotCommands(plot, Prompts) : null;
        }

        private void OnPlotPropertyChanged(object? sender, PropertyChangedEventArgs e) => ShowLegend();

        private void OnPlotChanged(object? sender, EventArgs e) => ShowLegend();

        /// <summary>The legend for the plot as it is now.</summary>
        private void ShowLegend()
        {
            PlotViewModel? vm = Plot;
            bool hasData = vm?.HasData == true;
            Legend.Show(hasData ? vm!.LegendEntries() : Array.Empty<LegendEntry>(), vm?.LegendVisible ?? true, Overlay.Bounds.Size);
        }
    }
}
