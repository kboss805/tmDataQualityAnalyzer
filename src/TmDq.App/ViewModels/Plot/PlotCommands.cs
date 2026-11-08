using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Services;

namespace TmDq.App.ViewModels.Plot
{
    /// <summary>
    /// What the controls around the chart bind to (plan step 4.4, US4.0): the right-click menu, the chips and the keys.
    /// Each command does nothing without data, as in v2.x, and says so (its CanExecute), which disables its menu item
    /// and lets its key go on unhandled. The items that ask the user (Set Plot Title, Set Time Window, the four axis
    /// limits and Customize View) ask through <see cref="IPlotPrompts"/>. It follows the plot's notices until
    /// <see cref="Dispose"/>.
    /// </summary>
    public sealed class PlotCommands : ObservableObject, IDisposable
    {
        private readonly PlotViewModel _plot;
        private readonly IPlotPrompts _prompts;
        private readonly IRelayCommand[] _all;
        private IReadOnlyList<MenuChoice> _plotFiles = Array.Empty<MenuChoice>();
        private IReadOnlyList<MenuChoice> _readoutChoices = Array.Empty<MenuChoice>();

        public PlotCommands(PlotViewModel plot, IPlotPrompts prompts)
        {
            _plot = plot;
            _prompts = prompts;
            Func<bool> hasData = () => _plot.HasData;
            SetTitleCommand = new AsyncRelayCommand(SetTitleAsync, hasData);
            CustomizeCommand = new AsyncRelayCommand(CustomizeAsync, hasData);
            SetTimeWindowCommand = new AsyncRelayCommand(SetTimeWindowAsync, hasData);
            SetLeftMinCommand = new AsyncRelayCommand(SetLeftMinAsync, hasData);
            SetLeftMaxCommand = new AsyncRelayCommand(SetLeftMaxAsync, hasData);
            SetRightMinCommand = new AsyncRelayCommand(SetRightMinAsync, hasData);
            SetRightMaxCommand = new AsyncRelayCommand(SetRightMaxAsync, hasData);
            ToggleLegendCommand = new RelayCommand(() => _plot.LegendVisible = !_plot.LegendVisible, hasData);
            ToggleViewModeCommand = new RelayCommand(_plot.ToggleViewMode, hasData);
            ShowLockPercentCommand = new RelayCommand(() => _plot.SetLockAxisView(LockAxisView.LockPercent), () => _plot.CanSwitchViewMode);
            ShowMissedFramesCommand = new RelayCommand(() => _plot.SetLockAxisView(LockAxisView.MissedFrames), () => _plot.CanSwitchViewMode);
            ZoomBackCommand = new RelayCommand(_plot.ZoomBack, () => _plot.HasData && _plot.CanZoomBack);
            ResetSpanCommand = new RelayCommand(_plot.ResetXRange, hasData);
            ResetYCommand = new RelayCommand(_plot.ResetYRange, hasData);
            ResetViewCommand = new RelayCommand(_plot.ResetView, hasData);
            PanEarlierCommand = new RelayCommand(() => _plot.PanWindow(-PlotViewModel.KeyPanFraction), hasData);
            PanLaterCommand = new RelayCommand(() => _plot.PanWindow(PlotViewModel.KeyPanFraction), hasData);
            ZoomInCommand = new RelayCommand(() => _plot.ZoomWindow(PlotViewModel.KeyZoomFactor), hasData);
            ZoomOutCommand = new RelayCommand(() => _plot.ZoomWindow(1.0 / PlotViewModel.KeyZoomFactor), hasData);
            ShowSourceCommand = new RelayCommand<int>(_plot.SetVisibleSource);
            PinReadoutCommand = new RelayCommand<int?>(id => _plot.ReadoutSeriesId = id);
            _all = new IRelayCommand[]
            {
                SetTitleCommand, CustomizeCommand, SetTimeWindowCommand, SetLeftMinCommand, SetLeftMaxCommand, SetRightMinCommand,
                SetRightMaxCommand, ToggleLegendCommand, ToggleViewModeCommand, ShowLockPercentCommand, ShowMissedFramesCommand,
                ZoomBackCommand, ResetSpanCommand, ResetYCommand, ResetViewCommand, PanEarlierCommand, PanLaterCommand,
                ZoomInCommand, ZoomOutCommand,
            };
            SyncChoices();
            _plot.SeriesChanged += OnSeriesChanged;
            _plot.RangesChanged += OnRangesChanged;
            _plot.PropertyChanged += OnPlotPropertyChanged;
        }

        // ---- what the menu and the chips show -----------------------------------------------------------------------

        public bool HasData => _plot.HasData;

        /// <summary>Whether there is another left-axis metric to switch to: the View Mode menu and its chip.</summary>
        public bool CanSwitchViewMode => _plot.CanSwitchViewMode;

        /// <summary>The Reset view chip: only once the view differs from the default.</summary>
        public bool CanResetView => _plot.CanResetView;

        /// <summary>Plot File: something to choose between only with more than one recording, which batch runs bring.</summary>
        public bool CanChoosePlotFile => _plot.HasData && _plot.SourceList().Count > 1;

        public bool LegendVisible => _plot.LegendVisible;

        public bool IsLockPercentView => _plot.LockAxisView == LockAxisView.LockPercent;

        public bool IsMissedFramesView => _plot.LockAxisView == LockAxisView.MissedFrames;

        /// <summary>The View Mode chip is named for the metric it switches TO.</summary>
        public string ViewModeChipText => IsLockPercentView ? "Show Accumulation" : "Show Lock %";

        public string ViewModeChipTip => IsLockPercentView
            ? "Switch the left axis to Accumulated Missed Frames"
            : "Switch the left axis to Frame Sync Lock %";

        /// <summary>Whether a CSV is being read for the plot (the "Loading..." overlay).</summary>
        public bool IsLoading => _plot.IsLoading;

        /// <summary>Plot File's entries: every recording overlaid, then each one.</summary>
        public IReadOnlyList<MenuChoice> PlotFiles => _plotFiles;

        /// <summary>Readout's entries, with data: the nearest series, then each series shown, to pin it to.</summary>
        public IReadOnlyList<MenuChoice> ReadoutChoices => _readoutChoices;

        /// <summary>The menu is opening: a readout pin whose series is gone or hidden is dropped, so the Readout menu never
        /// opens with nothing chosen.</summary>
        public void MenuOpening()
        {
            if (_plot.HasData) _plot.DropStaleReadoutPin();
        }

        // ---- the commands --------------------------------------------------------------------------------------------

        public IAsyncRelayCommand SetTitleCommand { get; }
        public IAsyncRelayCommand CustomizeCommand { get; }
        public IAsyncRelayCommand SetTimeWindowCommand { get; }
        public IAsyncRelayCommand SetLeftMinCommand { get; }
        public IAsyncRelayCommand SetLeftMaxCommand { get; }
        public IAsyncRelayCommand SetRightMinCommand { get; }
        public IAsyncRelayCommand SetRightMaxCommand { get; }

        /// <summary>Show Legend, and L.</summary>
        public IRelayCommand ToggleLegendCommand { get; }

        /// <summary>The View Mode chip, and V.</summary>
        public IRelayCommand ToggleViewModeCommand { get; }

        public IRelayCommand ShowLockPercentCommand { get; }
        public IRelayCommand ShowMissedFramesCommand { get; }

        /// <summary>One zoom back: X Axis &gt; Zoom Back, and Backspace.</summary>
        public IRelayCommand ZoomBackCommand { get; }

        /// <summary>The whole span, a Y limit kept: X Axis &gt; Reset Span, and Home.</summary>
        public IRelayCommand ResetSpanCommand { get; }

        /// <summary>Both axes scaled to the data again, the window kept: Y Axes &gt; Reset.</summary>
        public IRelayCommand ResetYCommand { get; }

        /// <summary>Both at once: the Reset view chip, Reset View, and R.</summary>
        public IRelayCommand ResetViewCommand { get; }

        /// <summary>The arrow keys: a tenth of the window earlier or later.</summary>
        public IRelayCommand PanEarlierCommand { get; }
        public IRelayCommand PanLaterCommand { get; }

        /// <summary>+ and -: the window scaled about its centre.</summary>
        public IRelayCommand ZoomInCommand { get; }
        public IRelayCommand ZoomOutCommand { get; }

        /// <summary>Plot File's entries: a recording's id, or -1 for every one.</summary>
        public IRelayCommand<int> ShowSourceCommand { get; }

        /// <summary>Readout's entries: a series' id, or null for the nearest.</summary>
        public IRelayCommand<int?> PinReadoutCommand { get; }

        public async Task SetTitleAsync()
        {
            if (!_plot.HasData) return;
            string? title = await _prompts.AskTextAsync("Set Plot Title", "Plot title:", _plot.Title);
            if (title is not null) _plot.Title = title;
        }

        /// <summary>Two fields seeded with the window shown; what is entered is clamped into the recording, with a warning
        /// for each end that had to be.</summary>
        public async Task SetTimeWindowAsync()
        {
            if (!_plot.HasData) return;
            var entered = await _prompts.AskTimeWindowAsync(_plot.FormatTime(_plot.XViewMin), _plot.FormatTime(_plot.XViewMax));
            if (entered is not (string start, string stop)) return;
            _plot.ApplyTimeWindow(_plot.ParseTime(start), _plot.ParseTime(stop), start, stop);
        }

        /// <summary>The left minimum, bounded above by the maximum less the least span, so the dialog cannot invert the axis.</summary>
        public async Task SetLeftMinAsync()
        {
            if (!_plot.HasData) return;
            bool lockView = _plot.LockAxisView == LockAxisView.LockPercent;
            double? value = await _prompts.AskNumberAsync("Set Left Y-Axis Minimum",
                lockView ? "Minimum lock %:" : "Minimum accumulated missed frames:",
                _plot.LeftMin, 0.0, _plot.LeftMax - PlotViewModel.MinAxisSpan, 0);
            if (value is double v) _plot.SetLeftMinOverride(v);
        }

        /// <summary>The left maximum: up to 100 for lock percentage, which is a percentage; far higher for missed frames.</summary>
        public async Task SetLeftMaxAsync()
        {
            if (!_plot.HasData) return;
            bool lockView = _plot.LockAxisView == LockAxisView.LockPercent;
            double? value = await _prompts.AskNumberAsync("Set Left Y-Axis Maximum",
                lockView ? "Maximum lock %:" : "Maximum accumulated missed frames:",
                _plot.LeftMax, _plot.LeftMin + PlotViewModel.MinAxisSpan,
                lockView ? PlotViewModel.LockAxisMax : PlotViewModel.MissedFramesOverrideMax, 0);
            if (value is double v) _plot.SetLeftMaxOverride(v);
        }

        /// <summary>The right minimum: SNR is negative on a weak link, so it is not floored at zero.</summary>
        public async Task SetRightMinAsync()
        {
            if (!_plot.HasData) return;
            double? value = await _prompts.AskNumberAsync("Set Right Y-Axis Minimum", "Minimum SNR (dB):",
                _plot.RightMin, -PlotViewModel.SnrOverrideLimit, _plot.RightMax - PlotViewModel.MinAxisSpan, 1);
            if (value is double v) _plot.SetRightMinOverride(v);
        }

        public async Task SetRightMaxAsync()
        {
            if (!_plot.HasData) return;
            double? value = await _prompts.AskNumberAsync("Set Right Y-Axis Maximum", "Maximum SNR (dB):",
                _plot.RightMax, _plot.RightMin + PlotViewModel.MinAxisSpan, PlotViewModel.SnrOverrideLimit, 1);
            if (value is double v) _plot.SetRightMaxOverride(v);
        }

        /// <summary>Customize View: the dialog edits a copy, and only OK applies it, in one refresh.</summary>
        public async Task CustomizeAsync()
        {
            if (!_plot.HasData) return;
            var customize = new CustomizeViewModel(_plot, _prompts);
            if (await _prompts.CustomizeAsync(customize)) customize.Apply();
        }

        /// <summary>A number field's value: clamped to its range (a maximum below the minimum taken as the
        /// minimum), then rounded to its decimals, half away from zero. AroundTheChartTests holds it.</summary>
        public static double SpinValue(double value, double min, double max, int decimals) =>
            Math.Round(Math.Clamp(value, min, Math.Max(min, max)), decimals, MidpointRounding.AwayFromZero);

        // ---- following the plot --------------------------------------------------------------------------------------

        private void OnRangesChanged(object? sender, EventArgs e)
        {
            OnPropertyChanged(nameof(CanResetView));
            ZoomBackCommand.NotifyCanExecuteChanged();
        }

        private void OnSeriesChanged(object? sender, EventArgs e) => Refresh();

        private void OnPlotPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            switch (e.PropertyName)
            {
                // Nothing here shows the title; and CanZoomBack changes with every move of the window, which RangesChanged
                // brings too (and tells Zoom Back), so a full refresh for it would be one per step of a drag.
                case nameof(PlotViewModel.Title):
                case nameof(PlotViewModel.CanZoomBack):
                    return;
                case nameof(PlotViewModel.LegendVisible):
                    OnPropertyChanged(nameof(LegendVisible));
                    return;
                case nameof(PlotViewModel.IsLoading):
                    OnPropertyChanged(nameof(IsLoading));
                    return;
                default:
                    Refresh();
                    return;
            }
        }

        private void Refresh()
        {
            SyncChoices();
            foreach (string name in new[]
            {
                nameof(HasData), nameof(CanSwitchViewMode), nameof(CanResetView), nameof(CanChoosePlotFile), nameof(LegendVisible),
                nameof(IsLockPercentView), nameof(IsMissedFramesView), nameof(ViewModeChipText), nameof(ViewModeChipTip),
                nameof(IsLoading),
            })
                OnPropertyChanged(name);
            foreach (IRelayCommand command in _all) command.NotifyCanExecuteChanged();
        }

        /// <summary>
        /// Plot File's and Readout's entries for the plot as it is now. A list is made again only when its entries
        /// change (a recording or a series comes or goes); otherwise only which one is chosen moves.
        /// </summary>
        private void SyncChoices()
        {
            var files = new List<(string Header, object? Parameter, bool Checked)> { ("All files (overlaid)", -1, _plot.VisibleSource < 0) };
            foreach ((int id, string label) in _plot.SourceList()) files.Add((label, id, _plot.VisibleSource == id));
            if (Sync(ref _plotFiles, files, ShowSourceCommand)) OnPropertyChanged(nameof(PlotFiles));

            var readout = new List<(string, object?, bool)>();
            if (_plot.HasData)
            {
                readout.Add(("Nearest series (automatic)", null, _plot.ReadoutSeriesId is null));
                foreach (PlotSeries s in _plot.Series)
                    if (_plot.EffectiveVisible(s)) readout.Add((s.Name, s.Id, _plot.ReadoutSeriesId == s.Id));
            }
            if (Sync(ref _readoutChoices, readout, PinReadoutCommand)) OnPropertyChanged(nameof(ReadoutChoices));
        }

        // The first entry, then a separator, then the rest; none at all for none. True when the list was made again.
        private static bool Sync(ref IReadOnlyList<MenuChoice> current, List<(string Header, object? Parameter, bool Checked)> wanted,
                                 System.Windows.Input.ICommand command)
        {
            MenuChoice[] entries = current.Where(c => !c.IsSeparator).ToArray();
            bool same = entries.Length == wanted.Count &&
                        entries.Zip(wanted).All(p => p.First.Header == p.Second.Header && Equals(p.First.Parameter, p.Second.Parameter));
            if (same)
            {
                for (int i = 0; i < entries.Length; i++) entries[i].IsChecked = wanted[i].Checked;
                return false;
            }
            var list = new List<MenuChoice>();
            for (int i = 0; i < wanted.Count; i++)
            {
                list.Add(new MenuChoice(wanted[i].Header, command, wanted[i].Parameter) { IsChecked = wanted[i].Checked });
                if (i == 0) list.Add(MenuChoice.Separator());
            }
            current = list;
            return true;
        }

        public void Dispose()
        {
            _plot.SeriesChanged -= OnSeriesChanged;
            _plot.RangesChanged -= OnRangesChanged;
            _plot.PropertyChanged -= OnPlotPropertyChanged;
        }
    }
}
