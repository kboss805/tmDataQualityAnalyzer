using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Services;
using TmDq.Core;

namespace TmDq.App.ViewModels.Plot;

/// <summary>
/// The Customize View dialog's state (plan step 4.4, US4.0) without its widgets: which
/// series are shown, and their names and colours, edited as a copy of the plot and applied only on OK. Rows hold
/// series ids, not positions, so a list that changed while the dialog was open cannot send an edit to another series.
/// </summary>
public sealed class CustomizeViewModel : ObservableObject
{
    public const string Title = "Customize View";
    public const string LockDescription =
        "Select the streams to display Frame Sync Lock data for.\n(This toggles both Lock % and Missed Frames plots)";

    private readonly PlotViewModel _plot;
    private IPlotPrompts _prompts;
    private SnrStreamGroup? _selectedSnrStream;

    public CustomizeViewModel(PlotViewModel plot, IPlotPrompts prompts)
    {
        _plot = plot;
        _prompts = prompts;
        SelectAllLockCommand = new RelayCommand(SelectAllLock);
        SelectNoneLockCommand = new RelayCommand(SelectNoneLock);
        SelectAllSnrCommand = new RelayCommand(() => SetAllSnr(true));
        SelectNoneSnrCommand = new RelayCommand(() => SetAllSnr(false));
        ToggleExpandCommand = new RelayCommand(ToggleExpand);

        // Grouped by PCM channel, the groups in ascending stream order, as v2.x's dialog grouped them; with several recordings on
        // the plot (several recordings opened, a batch, an antenna's log beside a recording), by recording first, in Plot File's order, each
        // row and group named for its recording, so that one recording's CH 1 is never another's.
        IReadOnlyList<(int Id, string Label)> recordings = plot.SourceList();
        var place = new Dictionary<int, int>();
        foreach ((int id, _) in recordings) place.TryAdd(id, place.Count);
        string Recording(int sourceId) =>
            recordings.Count > 1 ? recordings.First(r => r.Id == sourceId).Label + ": " : string.Empty;

        var labels = new Dictionary<(int Source, int Stream), string>();
        foreach (PlotSeries s in plot.Series)
            labels.TryAdd((s.SourceId, s.StreamOrder), s.StreamLabel);

        foreach (IGrouping<(int Source, int Stream), PlotSeries> stream in plot.Series.Where(s => s.IsLeftAxis)
                     .GroupBy(s => (Source: s.SourceId, Stream: s.StreamOrder)).OrderBy(g => place[g.Key.Source]).ThenBy(g => g.Key.Stream))
        {
            PlotSeries first = stream.First();
            LockRows.Add(new LockStreamRow(this, stream.Key.Stream, stream.Select(s => s.Id).ToList(), stream.Any(s => s.Visible),
                                           first.Name, first.Color, Recording(stream.Key.Source)));
        }

        foreach (IGrouping<(int Source, int Stream), PlotSeries> stream in plot.Series.Where(s => s.Metric == MetricType.Snr)
                     .GroupBy(s => (Source: s.SourceId, Stream: s.StreamOrder)).OrderBy(g => place[g.Key.Source]).ThenBy(g => g.Key.Stream))
        {
            // A phased array antenna's series (US10.0) are grouped the same way, a group to a channel of the antenna, but read
            // as the antenna's: "Stream n" with H, V and C, and the unit's name for the group, not CH, RCVR, L, R and C.
            bool phasedArray = stream.First().IsPhasedArray;
            List<ReceiverGroup> receivers = stream.GroupBy(s => s.ReceiverIndex).OrderBy(g => g.Key)
                .Select(r => new ReceiverGroup(r.Key, r.OrderBy(s => s.ChannelIndex).Select(s => new ChannelItem(this, s)).ToList(), phasedArray))
                .ToList();
            // The receivers in two columns, the first taking the odd one.
            int split = (receivers.Count + 1) / 2;
            // A recording's stream by its TMATS title, or the name it was given; an antenna's by its unit.
            PlotSeries first = stream.First();
            string label = !phasedArray && PlotViewModel.IsSnrStreamRenamed(first)
                ? PlotViewModel.SnrStreamName(first)
                : labels.GetValueOrDefault(stream.Key, "");
            string streamLabel = Recording(stream.Key.Source) + (phasedArray
                ? label is { Length: > 0 } unit ? unit : "Phased array antenna"
                : string.Create(CultureInfo.InvariantCulture, $"CH {stream.Key.Stream} — {label}"));
            SnrStreams.Add(new SnrStreamGroup(streamLabel, receivers.Take(split).ToList(), receivers.Skip(split).ToList(),
                                              stream.Key.Source, stream.Key.Stream, phasedArray ? label : PlotViewModel.SnrStreamName(first),
                                              canRename: !phasedArray));
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

    public IRelayCommand SelectAllLockCommand { get; }

    public IRelayCommand SelectNoneLockCommand { get; }

    /// <summary>Every channel of the stream shown; the other streams are left as they are.</summary>
    public IRelayCommand SelectAllSnrCommand { get; }

    public IRelayCommand SelectNoneSnrCommand { get; }

    /// <summary>Expands every receiver of the stream shown if any is collapsed, else collapses them all.</summary>
    public IRelayCommand ToggleExpandCommand { get; }

    private void SelectAllLock()
    {
        foreach (LockStreamRow row in LockRows) row.IsChecked = true;
    }

    private void SelectNoneLock()
    {
        foreach (LockStreamRow row in LockRows) row.IsChecked = false;
    }

    private void SetAllSnr(bool value)
    {
        if (SelectedSnrStream is null) return;
        // Through SetAll, not IsChecked: a click on a partly checked receiver's box checks it, which is right for a click and
        // wrong for Select None, which made every partly checked receiver fully checked and needed a second press.
        foreach (ReceiverGroup r in SelectedSnrStream.Receivers) r.SetAll(value);
    }

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

    /// <summary>
    /// OK: each Frame Sync Lock stream's box shows or hides its series, but only the metric of the view in use is made
    /// visible (the other is on another scale); each SNR channel's box its series; then each Frame Sync Lock stream's name and
    /// colour, each SNR stream's new name and each receiver's new colour (its channels' shades of it); then one refresh. Series
    /// gone since the dialog opened are skipped.
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

        // Every series of the row, which are its recording's stream's only (its lock and its missed frames).
        foreach (LockStreamRow row in LockRows)
            foreach (int id in row.SeriesIds)
            {
                _plot.RenameSeries(id, row.Name);
                _plot.RecolorSeries(id, row.Color);
            }
        foreach (SnrStreamGroup stream in SnrStreams)
            if (stream.IsRenamed) _plot.RenameSnrStream(stream.SourceId, stream.StreamOrder, stream.Name);
        foreach (ReceiverGroup receiver in SnrStreams.SelectMany(s => s.Receivers))
        {
            if (receiver.PendingColor is null) continue;
            foreach (ChannelItem ch in receiver.Channels) _plot.RecolorSeries(ch.SeriesId, ch.Color);
        }
        _plot.CommitAppearance();
    }

    private IEnumerable<ChannelItem> AllChannels() => SnrStreams.SelectMany(s => s.Receivers).SelectMany(r => r.Channels);
}

/// <summary>A Frame Sync Lock stream's row: its box (both metrics), its colour and its name.</summary>
public sealed class LockStreamRow : ObservableObject
{
    private readonly CustomizeViewModel _owner;
    private readonly string _recording;

    private bool _isChecked;
    private Color _color;
    private string _name;

    internal LockStreamRow(CustomizeViewModel owner, int order, IReadOnlyList<int> seriesIds, bool isChecked, string name, Color color,
                           string recording = "")
    {
        _owner = owner;
        Order = order;
        _recording = recording;
        SeriesIds = seriesIds;
        _isChecked = isChecked;
        _name = name;
        _color = color;
        PickColorCommand = new AsyncRelayCommand(PickColorAsync);
    }

    public int Order { get; }

    /// <summary>"CH n", after its recording's name ("flight 2: CH n") when the plot holds more than one.</summary>
    public string Label => string.Create(CultureInfo.InvariantCulture, $"{_recording}CH {Order}");

    public IReadOnlyList<int> SeriesIds { get; }

    public bool IsChecked { get => _isChecked; set => SetProperty(ref _isChecked, value); }

    public Color Color { get => _color; set => SetProperty(ref _color, value); }

    public string Name { get => _name; set => SetProperty(ref _name, value); }

    public IAsyncRelayCommand PickColorCommand { get; }

    private async Task PickColorAsync()
    {
        if (await _owner.PickColorAsync(Color) is Color picked) Color = picked;
    }
}

/// <summary>One Receiver SNR stream in the SNR tab: its name, which a recording's stream may be given, and its receivers in two
/// columns.</summary>
public sealed class SnrStreamGroup : ObservableObject
{
    private readonly string _originalName;
    private string _name;

    public SnrStreamGroup(string label, IReadOnlyList<ReceiverGroup> left, IReadOnlyList<ReceiverGroup> right,
                          int sourceId = 0, int streamOrder = 0, string name = "", bool canRename = false)
    {
        Label = label;
        LeftReceivers = left;
        RightReceivers = right;
        SourceId = sourceId;
        StreamOrder = streamOrder;
        _originalName = name;
        _name = name;
        CanRename = canRename;
    }

    public string Label { get; }

    public int SourceId { get; }

    /// <summary>The stream's PCM channel id: which of its recording's streams it is.</summary>
    public int StreamOrder { get; }

    /// <summary>Whether the stream can be renamed: a recording's can; an antenna's (US10.0) is named for its unit.</summary>
    public bool CanRename { get; }

    /// <summary>The stream's name: the TMATS title, or the name it was given; what OK gives it when changed.</summary>
    public string Name { get => _name; set => SetProperty(ref _name, value); }

    /// <summary>Whether OK renames the stream: it can be, and its name, trimmed, is another and not empty.</summary>
    public bool IsRenamed => CanRename && Name.Trim() is { Length: > 0 } n && n != _originalName;
    public IReadOnlyList<ReceiverGroup> LeftReceivers { get; }
    public IReadOnlyList<ReceiverGroup> RightReceivers { get; }
    public IEnumerable<ReceiverGroup> Receivers => LeftReceivers.Concat(RightReceivers);
    public override string ToString() => Label;
}

/// <summary>A receiver's group: a box for all its channels (partly checked when only some are), collapsed at first, and its
/// colour, which its channels follow.</summary>
public sealed class ReceiverGroup : ObservableObject
{
    private bool _isExpanded;

    private readonly bool _phasedArray;

    public ReceiverGroup(int index, IReadOnlyList<ChannelItem> channels, bool phasedArray = false)
    {
        Index = index;
        Channels = channels;
        _phasedArray = phasedArray;
        foreach (ChannelItem ch in channels) ch.Group = this;
        PickColorCommand = new AsyncRelayCommand(PickColorAsync, () => Channels.Count > 0);
    }

    public int Index { get; }

    /// <summary>"RCVR n" for a receiver, "Stream n" for a channel of a phased array antenna (US10.0).</summary>
    public string Label => string.Create(CultureInfo.InvariantCulture, $"{(_phasedArray ? "Stream" : "RCVR")} {Index}");

    public IReadOnlyList<ChannelItem> Channels { get; }

    /// <summary>The receiver's colour: its combined channel's (C), the full colour its other channels are lighter shades of;
    /// without a combined channel, its first channel's. The group's text and its swatch are in it.</summary>
    public Color Color =>
        Channels.FirstOrDefault(c => c.ChannelIndex == PlotColors.CombinedChannelIndex) is { } combined ? combined.Color
        : Channels.Count > 0 ? Channels[0].Color : Colors.Gray;

    /// <summary>The colour chosen for the receiver, given on OK; null until one is.</summary>
    public Color? PendingColor { get; private set; }

    /// <summary>Asks for the receiver's colour, starting from the one it has.</summary>
    public IAsyncRelayCommand PickColorCommand { get; }

    /// <summary>Makes <paramref name="color"/> the receiver's: its combined channel takes it, and each other channel the
    /// lighter shade its place gives (<see cref="PlotColors.Shade"/>, <see cref="PlotColors.Weakness"/>), as a receiver's
    /// colours are made when it is first drawn. Given to the series on OK.</summary>
    public void SetColor(Color color)
    {
        PendingColor = color;
        foreach (ChannelItem ch in Channels) ch.SetColor(PlotColors.Shade(color, PlotColors.Weakness(ch.ChannelIndex)));
        OnPropertyChanged(nameof(Color));
    }

    private async Task PickColorAsync()
    {
        if (await Channels[0].Owner.PickColorAsync(Color) is Color picked) SetColor(picked);
    }

    /// <summary>True when every channel is checked, false when none is, null when some are. Setting it checks or clears
    /// them all; a click on a partly checked box checks them all, as v2.x's did (AroundTheChartTests).</summary>
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

    /// <summary>Checks or clears every channel, whatever they were: what Select All and Select None do, which a click on the
    /// group's box (<see cref="IsChecked"/>) does not when the group is partly checked.</summary>
    public void SetAll(bool value)
    {
        foreach (ChannelItem ch in Channels) ch.SetChecked(value);
        OnPropertyChanged(nameof(IsChecked));
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

/// <summary>One SNR channel: its box, in its colour, which follows its receiver's (<see cref="ReceiverGroup.SetColor"/>).</summary>
public sealed class ChannelItem : ObservableObject
{
    private bool _isChecked;
    private Color _color;

    internal ChannelItem(CustomizeViewModel owner, PlotSeries s)
    {
        Owner = owner;
        SeriesId = s.Id;
        ChannelIndex = s.ChannelIndex;
        Label = s.IsPhasedArray ? PlotViewModel.PolarizationLabel(s.ChannelIndex) : PlotViewModel.ChannelLabel(s.ChannelIndex);
        _isChecked = s.Visible;
        _color = s.Color;
    }

    internal CustomizeViewModel Owner { get; }

    internal ReceiverGroup? Group { get; set; }

    public int SeriesId { get; }

    /// <summary>The channel's place in its receiver: 0 (L or H), 1 (R or V), 2 (C, the combined one).</summary>
    public int ChannelIndex { get; }

    public string Label { get; }

    /// <summary>What a screen reader says for the channel's box: its label and its receiver's ("L of RCVR 3"), since every
    /// receiver has an "L".</summary>
    public string AccessibleName => Group is { } g ? $"{Label} of {g.Label}" : Label;

    /// <summary>The channel's colour: its series', or its shade of the colour chosen for its receiver.</summary>
    public Color Color => _color;

    internal void SetColor(Color color)
    {
        _color = color;
        OnPropertyChanged(nameof(Color));
    }

    public bool IsChecked
    {
        get => _isChecked;
        set
        {
            if (SetChecked(value)) Group?.ChildChanged();
        }
    }

    internal bool SetChecked(bool value) => SetProperty(ref _isChecked, value, nameof(IsChecked));
}
