using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.Core;

namespace TmDq.App.Plot
{
    /// <summary>
    /// The Customize View dialog's state (plan step 4.4), the Qt <c>PlotCustomizationDialog</c> without its widgets: which
    /// series are shown, and their names and colours, edited as a copy of the plot and applied only on OK. Rows hold
    /// series ids, not positions, so a list that changed while the dialog was open cannot send an edit to another series.
    /// </summary>
    public sealed partial class CustomizeViewModel : ObservableObject
    {
        public const string Title = "Customize Plot Series";
        public const string LockDescription =
            "Select the streams to display Frame Sync Lock data for.\n(This toggles both Lock % and Missed Frames plots)";

        private readonly PlotViewModel _plot;
        private IPlotPrompts _prompts;
        private SnrStreamGroup? _selectedSnrStream;

        public CustomizeViewModel(PlotViewModel plot, IPlotPrompts prompts)
        {
            _plot = plot;
            _prompts = prompts;

            // Grouped by PCM channel, in its order, as the Qt dialog's QMaps group them.
            var labels = new Dictionary<int, string>();
            foreach (PlotSeries s in plot.Series)
                labels.TryAdd(s.StreamOrder, s.StreamLabel);

            foreach (IGrouping<int, PlotSeries> stream in plot.Series.Where(s => s.IsLeftAxis).GroupBy(s => s.StreamOrder).OrderBy(g => g.Key))
            {
                PlotSeries first = stream.First();
                LockRows.Add(new LockStreamRow(this, stream.Key, stream.Select(s => s.Id).ToList(), stream.Any(s => s.Visible),
                                               first.Name, first.Color));
            }

            foreach (IGrouping<int, PlotSeries> stream in plot.Series.Where(s => s.Metric == MetricType.Snr).GroupBy(s => s.StreamOrder).OrderBy(g => g.Key))
            {
                List<ReceiverGroup> receivers = stream.GroupBy(s => s.ReceiverIndex).OrderBy(g => g.Key)
                    .Select(r => new ReceiverGroup(r.Key, r.OrderBy(s => s.ChannelIndex).Select(s => new ChannelItem(this, s)).ToList()))
                    .ToList();
                // The receivers in two columns, the first taking the odd one.
                int split = (receivers.Count + 1) / 2;
                SnrStreams.Add(new SnrStreamGroup(
                    string.Create(CultureInfo.InvariantCulture, $"CH {stream.Key} — {labels.GetValueOrDefault(stream.Key, "")}"),
                    receivers.Take(split).ToList(), receivers.Skip(split).ToList()));
            }
            _selectedSnrStream = SnrStreams.FirstOrDefault();
        }

        public ObservableCollection<LockStreamRow> LockRows { get; } = new();

        public ObservableCollection<SnrStreamGroup> SnrStreams { get; } = new();

        public bool HasSnrStreams => SnrStreams.Count > 0;

        /// <summary>The stream whose receivers the SNR tab shows.</summary>
        public SnrStreamGroup? SelectedSnrStream
        {
            get => _selectedSnrStream;
            set
            {
                if (SetProperty(ref _selectedSnrStream, value)) OnPropertyChanged(nameof(ExpandText));
            }
        }

        /// <summary>"Expand All" while any receiver of the stream shown is collapsed, else "Collapse All".</summary>
        public string ExpandText => SelectedSnrStream?.Receivers.Any(r => !r.IsExpanded) != false ? "Expand All" : "Collapse All";

        [RelayCommand]
        private void SelectAllLock()
        {
            foreach (LockStreamRow row in LockRows) row.IsChecked = true;
        }

        [RelayCommand]
        private void SelectNoneLock()
        {
            foreach (LockStreamRow row in LockRows) row.IsChecked = false;
        }

        /// <summary>Every channel of the stream shown; the other streams are left as they are.</summary>
        [RelayCommand]
        private void SelectAllSnr() => SetAllSnr(true);

        [RelayCommand]
        private void SelectNoneSnr() => SetAllSnr(false);

        private void SetAllSnr(bool value)
        {
            if (SelectedSnrStream is null) return;
            foreach (ReceiverGroup r in SelectedSnrStream.Receivers) r.IsChecked = value;
        }

        /// <summary>Expands every receiver of the stream shown if any is collapsed, else collapses them all.</summary>
        [RelayCommand]
        private void ToggleExpand()
        {
            if (SelectedSnrStream is null) return;
            bool expand = SelectedSnrStream.Receivers.Any(r => !r.IsExpanded);
            foreach (ReceiverGroup r in SelectedSnrStream.Receivers) r.IsExpanded = expand;   // each says ExpandText changed
        }

        /// <summary>Asks the dialog's own questions (a colour, a name) through <paramref name="prompts"/>: the dialog's window
        /// sets this, so they sit over it rather than over the main window.</summary>
        public void AskThrough(IPlotPrompts prompts) => _prompts = prompts;

        internal void ExpansionChanged() => OnPropertyChanged(nameof(ExpandText));

        internal Task<Color?> PickColorAsync(Color initial) => _prompts.PickColorAsync(initial);

        internal Task<string?> AskNameAsync(string initial) => _prompts.AskTextAsync("Rename Series", "Series name:", initial);

        /// <summary>
        /// OK: each Frame Sync Lock stream's box shows or hides its series, but only the metric of the view in use is made
        /// visible (the other is on another scale); each SNR channel's box its series; then each stream's name and colour
        /// and each channel's pending name and colour; then one refresh. Series gone since the dialog opened are skipped.
        /// </summary>
        public void Apply()
        {
            bool missedView = _plot.LockAxisView == LockAxisView.MissedFrames;
            foreach (LockStreamRow row in LockRows)
                foreach (int id in row.SeriesIds)
                {
                    PlotSeries? s = _plot.SeriesById(id);
                    if (s is null) continue;
                    bool activeMetric = (s.Metric == MetricType.AccumulatedMissedFrames) == missedView;
                    _plot.SetSeriesVisibleQuiet(id, row.IsChecked && activeMetric);
                }
            foreach (ChannelItem ch in AllChannels())
                _plot.SetSeriesVisibleQuiet(ch.SeriesId, ch.IsChecked);

            foreach (LockStreamRow row in LockRows)
            {
                if (row.SeriesIds.Count == 0) continue;
                _plot.RenameSeries(row.SeriesIds[0], row.Name);
                _plot.RecolorSeries(row.SeriesIds[0], row.Color);
            }
            foreach (ChannelItem ch in AllChannels())
            {
                if (ch.PendingName is not null) _plot.RenameSeries(ch.SeriesId, ch.PendingName);
                if (ch.PendingColor is Color c) _plot.RecolorSeries(ch.SeriesId, c);
            }
            _plot.CommitAppearance();
        }

        private IEnumerable<ChannelItem> AllChannels() => SnrStreams.SelectMany(s => s.Receivers).SelectMany(r => r.Channels);

        internal PlotSeries? SeriesById(int id) => _plot.SeriesById(id);
    }

    /// <summary>A Frame Sync Lock stream's row: its box (both metrics), its colour and its name.</summary>
    public sealed partial class LockStreamRow : ObservableObject
    {
        private readonly CustomizeViewModel _owner;

        [ObservableProperty]
        private bool _isChecked;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(Brush))]
        private Color _color;

        [ObservableProperty]
        private string _name;

        internal LockStreamRow(CustomizeViewModel owner, int order, IReadOnlyList<int> seriesIds, bool isChecked, string name, Color color)
        {
            _owner = owner;
            Order = order;
            SeriesIds = seriesIds;
            _isChecked = isChecked;
            _name = name;
            _color = color;
        }

        public int Order { get; }

        public string Label => string.Create(CultureInfo.InvariantCulture, $"CH {Order}");

        public IReadOnlyList<int> SeriesIds { get; }

        public IBrush Brush => new SolidColorBrush(Color);

        [RelayCommand]
        private async Task PickColor()
        {
            if (await _owner.PickColorAsync(Color) is Color picked) Color = picked;
        }
    }

    /// <summary>One Receiver SNR stream in the SNR tab: its receivers in two columns.</summary>
    public sealed class SnrStreamGroup
    {
        public SnrStreamGroup(string label, IReadOnlyList<ReceiverGroup> left, IReadOnlyList<ReceiverGroup> right)
        {
            Label = label;
            LeftReceivers = left;
            RightReceivers = right;
        }

        public string Label { get; }
        public IReadOnlyList<ReceiverGroup> LeftReceivers { get; }
        public IReadOnlyList<ReceiverGroup> RightReceivers { get; }
        public IEnumerable<ReceiverGroup> Receivers => LeftReceivers.Concat(RightReceivers);
        public override string ToString() => Label;
    }

    /// <summary>A receiver's group: a box for all its channels (partly checked when only some are), collapsed at first.</summary>
    public sealed partial class ReceiverGroup : ObservableObject
    {
        private bool _isExpanded;

        public ReceiverGroup(int index, IReadOnlyList<ChannelItem> channels)
        {
            Index = index;
            Channels = channels;
            foreach (ChannelItem ch in channels) ch.Group = this;
        }

        public int Index { get; }

        public string Label => string.Create(CultureInfo.InvariantCulture, $"RCVR {Index}");

        public IReadOnlyList<ChannelItem> Channels { get; }

        /// <summary>The group's text is in its first channel's colour.</summary>
        public IBrush Brush => Channels.Count > 0 ? Channels[0].Brush : Brushes.Gray;

        /// <summary>True when every channel is checked, false when none is, null when some are. Setting it checks or clears
        /// them all; a click on a partly checked box checks them all, as a Qt tristate item does.</summary>
        public bool? IsChecked
        {
            get
            {
                int n = Channels.Count(c => c.IsChecked);
                return n == 0 ? false : n == Channels.Count ? true : null;
            }
            set
            {
                bool check = value ?? true;
                if (value == false && IsChecked is null) check = true;
                foreach (ChannelItem ch in Channels) ch.SetChecked(check);
                OnPropertyChanged();
            }
        }

        public bool IsExpanded
        {
            get => _isExpanded;
            set
            {
                if (SetProperty(ref _isExpanded, value) && Channels.Count > 0) Channels[0].Owner.ExpansionChanged();
            }
        }

        internal void ChildChanged() => OnPropertyChanged(nameof(IsChecked));
    }

    /// <summary>One SNR channel: its box, in its colour, with a rename and a recolour kept until OK.</summary>
    public sealed partial class ChannelItem : ObservableObject
    {
        private bool _isChecked;
        private Color _color;

        internal ChannelItem(CustomizeViewModel owner, PlotSeries s)
        {
            Owner = owner;
            SeriesId = s.Id;
            Label = PlotViewModel.ChannelLabel(s.ChannelIndex);
            _isChecked = s.Visible;
            _color = s.Color;
        }

        internal CustomizeViewModel Owner { get; }

        internal ReceiverGroup? Group { get; set; }

        public int SeriesId { get; }

        public string Label { get; }

        public string? PendingName { get; private set; }

        public Color? PendingColor { get; private set; }

        /// <summary>The name to be given on OK, as the channel's tooltip once one is chosen.</summary>
        public string? ToolTip => PendingName;

        public IBrush Brush => new SolidColorBrush(_color);

        public bool IsChecked
        {
            get => _isChecked;
            set
            {
                if (SetChecked(value)) Group?.ChildChanged();
            }
        }

        internal bool SetChecked(bool value) => SetProperty(ref _isChecked, value, nameof(IsChecked));

        [RelayCommand]
        private async Task Rename()
        {
            PlotSeries? s = Owner.SeriesById(SeriesId);
            if (s is null) return;   // gone since the dialog opened
            if (await Owner.AskNameAsync(PendingName ?? s.Name) is string name)
            {
                PendingName = name;
                OnPropertyChanged(nameof(ToolTip));
            }
        }

        [RelayCommand]
        private async Task Recolor()
        {
            PlotSeries? s = Owner.SeriesById(SeriesId);
            if (s is null) return;
            if (await Owner.PickColorAsync(PendingColor ?? s.Color) is Color picked)
            {
                PendingColor = picked;
                _color = picked;
                OnPropertyChanged(nameof(Brush));
            }
        }
    }
}
