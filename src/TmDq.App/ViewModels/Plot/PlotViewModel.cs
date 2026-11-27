using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using TmDq.App.Models.Export;
using TmDq.Core;

namespace TmDq.App.ViewModels.Plot;

/// <summary>Which metric the left axis shows: lock percentage or accumulated missed frames (US3.1).</summary>
public enum LockAxisView
{
    LockPercent,
    MissedFrames,
}

/// <summary>
/// The plot's state, held in C# (plan decision D3): the series, built by the core from each processed stream on one
/// shared time axis; both Y axes' ranges and overrides; which metric the left axis shows; the recordings and the Plot
/// File selector; the readout's pin; and what the legend, the chips, the menu, the keys and Customize View act on
/// (plan step 4.4). The chart (<c>PlotChart</c>) draws it and reports the user's gestures back; <c>PlotView</c> holds
/// the controls around it. Used on the UI thread.
/// <para>Four jobs are done by their own types, and reached through this one: the time window and its zoom history
/// (<see cref="PlotTimeWindow"/>), the readout's text (<see cref="PlotReadout"/>), CSV export and import
/// (<see cref="PlotCsv"/>), and the colours (<see cref="PlotColors.Assign(System.Collections.Generic.IEnumerable{PlotSeries})"/>).</para>
/// </summary>
public sealed class PlotViewModel : ObservableObject, IDisposable
{
    public const string DefaultTitle = "Framesync/SNR Plot";
    public const string XAxisLabel = "Time (DDD:HH:MM:SS)";
    public const string LockAxisLabel = "Framesync Lock (%)";
    public const string MissedFramesAxisLabel = "Accumulated Missed Frames";
    public const string SnrAxisLabel = "Receiver SNR (dB)";
    public const double LockAxisMin = 0.0, LockAxisMax = 100.0;
    public const double AxisMarginFactor = 0.05, MinAxisSpan = 1.0;
    public const int MaxZoomDepth = 32;
    public const double MinZoomSpanSeconds = 1e-6;
    public const double KeyPanFraction = 0.10, KeyZoomFactor = 0.80, WheelZoomFactor = 0.85;
    public const double MissedFramesOverrideMax = 10000000.0, SnrOverrideLimit = 999.0;
    private static readonly string[] ChannelPrefixes = { "L", "R", "C" };

    private readonly List<PlotSeries> _series = new();
    // The Frame Sync Lock colour each stream order was given (PlotColors.Assign): kept until the plot starts again, so a stream
    // added later takes the next colour and never recolours the series already shown.
    private readonly Dictionary<int, int> _lockColours = new();
    private readonly PlotTimeWindow _window;
    // Made with the first stream: the core is loaded only once a recording has been opened.
    private TimeBase? _timeBase;

    /// <summary>The plot's clock was set by an imported CSV, which states a day and a time of day but no absolute time: the time
    /// base's absolute seconds are not those of the clock. The next recording added resolves it (<see cref="AddStream"/>).</summary>
    private bool _clockOnly;
    private int _nextId;
    private string _title = DefaultTitle;
    private double _snrMin, _snrMax = 1.0;
    private double? _leftMinOverride, _leftMaxOverride, _rightMinOverride, _rightMaxOverride;
    private LockAxisView _lockAxisView = LockAxisView.LockPercent;
    private int? _readoutSeriesId;
    private bool _legendVisible = true;
    private bool _isLoading;

    /// <summary>Raised when the series, their samples or their visibility change (the chart redraws them).</summary>
    public event EventHandler? SeriesChanged;

    /// <summary>Raised when the visible window or an axis range changes.</summary>
    public event EventHandler? RangesChanged;

    /// <summary>Raised when a source's recording does not overlap what is already loaded (informational: they share one
    /// time axis with a gap between them).</summary>
    public event EventHandler<int>? NonOverlappingSource;

    /// <summary>Raised with the id of a recording whose series <see cref="RemoveSource"/> has taken out of the plot, once it is
    /// out, so that what keeps a record of the recordings (the main window's, for Save Template) does not keep this one.</summary>
    public event EventHandler<int>? SourceRemoved;

    /// <summary>Raised with a warning for the log: a time window entered outside the recording, and clamped.</summary>
    public event EventHandler<string>? Warning;

    public PlotViewModel()
    {
        Sources = new PlotSources(_series);
        _window = new PlotTimeWindow(() => HasData, FormatTime);
        _window.Changed += (_, _) =>
        {
            RangesChanged?.Invoke(this, EventArgs.Empty);
            OnPropertyChanged(nameof(CanZoomBack));
        };
        _window.Warning += (_, warning) => Warning?.Invoke(this, warning);
    }

    public IReadOnlyList<PlotSeries> Series => _series;

    public bool HasData => _series.Count > 0;

    /// <summary>Whether Export Data has anything to write: a plot of an antenna's log alone has not (US10.0).</summary>
    public bool HasExportableData => _series.Any(PlotCsv.IsExported);

    public string Title
    {
        get => _title;
        set => SetProperty(ref _title, value);
    }

    public LockAxisView LockAxisView => _lockAxisView;

    public string LeftAxisLabel => _lockAxisView == LockAxisView.MissedFrames ? MissedFramesAxisLabel : LockAxisLabel;

    // ---- the series ---------------------------------------------------------------------------------------------

    /// <summary>Makes the time base's absolute seconds those of its clock (day and time of day), in the year of
    /// <paramref name="firstAbsSeconds"/>: what an imported CSV could not say. Leaves a base with no clock as it was.</summary>
    private void AbsoluteFromClock(double firstAbsSeconds)
    {
        _clockOnly = false;
        TimeBaseInfo clock = _timeBase!.Info;
        if (clock.BaseDay <= 0 || !double.IsFinite(firstAbsSeconds) || Math.Abs(firstAbsSeconds) > 1.0e11) return;
        int year = DateTimeOffset.FromUnixTimeSeconds((long)firstAbsSeconds).Year;
        double newYear = new DateTimeOffset(year, 1, 1, 0, 0, 0, TimeSpan.Zero).ToUnixTimeSeconds();
        _timeBase.Adopt(newYear + (clock.BaseDay - 1) * 86400.0 + clock.BaseTimeOffset, hasSeries: false);
    }

    /// <summary>
    /// Adds a finished stream's series (the core's <c>buildStreamSeries</c>), as v2.x added them: a
    /// stream processed again replaces its earlier series; the shared time base moves earlier for a source that starts
    /// earlier (every existing series shifting right with it); the series keep job order; the whole span is shown.
    /// </summary>
    public void AddStream(StreamResult result)
    {
        ReadOnlySpan<double> times = result.TimesSec;
        if (times.Length == 0) return;
        _timeBase ??= new TimeBase();

        StreamKey key = result.Key;
        _series.RemoveAll(s => new StreamKey(s.SourceId, s.StreamLabel, s.StreamOrder).IsSameStream(key));

        bool hadSeries = _series.Count > 0;
        // An import set the clock and left the absolute base unset: placed by it, a recording would land at its epoch seconds, a
        // lifetime after the import. Give the base the clock's moment, in the year this recording says (an import has no year).
        if (!hadSeries) _clockOnly = false;
        else if (_clockOnly) AbsoluteFromClock(times[0]);
        int sourceId = result.SourceId;
        if (hadSeries && !_series.Any(s => s.SourceId == sourceId) && !_timeBase.Overlaps(times[0], times[^1], _window.XMax))
        {
            NonOverlappingSource?.Invoke(this, sourceId);
        }

        double shift = _timeBase.Adopt(times[0], hadSeries);
        if (shift > 0.0)
        {
            foreach (double[] xs in _series.Select(s => s.X).Distinct())
                for (int i = 0; i < xs.Length; i++) xs[i] += shift;
            foreach (PlotSeries s in _series) s.Version++;
            // The history holds views in the old elapsed seconds, now `shift` too early: Zoom Back would land off by that much.
            _window.ClearHistory();
        }

        List<PlotSeries> added;
        using (SeriesSet set = result.BuildSeries(_timeBase.Info.BaseAbsSeconds))
        {
            double[] elapsed = set.ElapsedSeconds.ToArray();
            bool sorted = ChartGeometry.IsSortedX(elapsed);
            added = new List<PlotSeries>(set.Count);
            for (int i = 0; i < set.Count; i++)
            {
                SeriesSpec spec = set.Specs[i];
                var series = new PlotSeries
                {
                    Id = _nextId++,
                    Name = spec.Name,
                    StreamLabel = spec.StreamLabel,
                    Metric = spec.Metric,
                    ReceiverIndex = spec.ReceiverIndex,
                    ChannelIndex = spec.ChannelIndex,
                    StreamOrder = spec.StreamOrder,
                    StreamSequence = spec.StreamSequence,
                    SourceId = spec.SourceId,
                    X = elapsed,
                    Y = set.Values(i).ToArray(),
                    YMin = spec.YMin,
                    YMax = spec.YMax,
                    SortedX = sorted,
                    Visible = ShownInView(spec.Metric, _lockAxisView),
                };
                added.Add(series);
                _series.Add(series);
            }
            ShowOnlyTheCombinedChannels(added);
        }

        // A stable sort by job, so streams keep the order they were configured in whichever finished first.
        List<PlotSeries> ordered = _series.OrderBy(s => s.StreamSequence).ToList();
        _series.Clear();
        _series.AddRange(ordered);

        _window.ShowSpan(SpanOf(_series));

        // Only the stream just added is coloured: the others keep the colours they have, which the user, Customize View or a
        // template may have given them (a batch's earlier files, a recording added beside another).
        PlotColors.Assign(added, _lockColours);
        ComputeSnrRange();
        SeriesChanged?.Invoke(this, EventArgs.Empty);
        RangesChanged?.Invoke(this, EventArgs.Empty);
        OnPropertyChanged(nameof(HasData));
    }

    /// <summary>Shows <paramref name="series"/> in place of whatever was shown: series made other than by a run (the
    /// tests build their plots this way; an imported CSV goes through <see cref="LoadCsv"/>). They are coloured as a
    /// run's would be, and the whole span is shown.</summary>
    public void SetSeries(IEnumerable<PlotSeries> series)
    {
        _series.Clear();
        _series.AddRange(series.OrderBy(s => s.StreamSequence));
        _nextId = _series.Count == 0 ? 0 : _series.Max(s => s.Id) + 1;
        _window.ShowSpan(SpanOf(_series));
        _window.ClearHistory();
        _lockColours.Clear();
        PlotColors.Assign(_series, _lockColours);
        ComputeSnrRange();
        SeriesChanged?.Invoke(this, EventArgs.Empty);
        RangesChanged?.Invoke(this, EventArgs.Empty);
        OnPropertyChanged(nameof(HasData));
    }

    // ---- CSV, export and import (plan step 4.5) ---------------------------------------------------------------------

    /// <summary>
    /// Writes every series, shown or not, as the core's CSV (US6.1): the writer owns the format, and
    /// the plot hands it each series' identity and samples with the recording's clock. A <paramref name="sourceId"/> of
    /// 0 or more keeps that source's series only (per-file batch export, 4.7). False, with no file written, when there
    /// is nothing to write; false when the file cannot be written.
    /// </summary>
    public bool ExportCsv(string path, int sourceId = -1)
    {
        (List<CsvColumn> columns, TimeBaseInfo clock) = CsvSnapshot(sourceId);
        // No columns at all is the writer's to refuse, and it writes no file.
        return PlotCsv.Write(path, columns, clock);
    }

    /// <summary>
    /// <see cref="ExportCsv"/> with the file written off the UI thread (the export's matched partner of
    /// <see cref="LoadCsvAsync"/>): what is written is taken on the calling thread, before the first await, so a
    /// plot cleared or replaced meanwhile does not change the file. The snapshot shares the series' arrays: their values are
    /// never written once a series exists, and their times are moved in place only by a run or an import (a recording that
    /// starts earlier, <see cref="AddStream"/> and <see cref="ShowCsv"/>), which the main window, busy while it writes an
    /// export, does not start meanwhile; Remove File, which the plot's menu offers during an export, gives the series it
    /// moves new arrays (<see cref="AdvanceTimeBase"/>) and leaves these as they were.
    /// </summary>
    public Task<bool> ExportCsvAsync(string path, int sourceId = -1)
    {
        (List<CsvColumn> columns, TimeBaseInfo clock) = CsvSnapshot(sourceId);
        return Task.Run(() => PlotCsv.Write(path, columns, clock));
    }

    private (List<CsvColumn> Columns, TimeBaseInfo Clock) CsvSnapshot(int sourceId) =>
        (PlotCsv.Columns(_series, sourceId), _timeBase?.Info ?? new TimeBaseInfo(0, 0.0, 0.0));

    /// <summary>Whether a CSV is being read for the plot (the "Loading..." overlay).</summary>
    public bool IsLoading
    {
        get => _isLoading;
        private set => SetProperty(ref _isLoading, value);
    }

    /// <summary>Reads a CSV, an exported one or an antenna's log (told apart by the file, <see cref="PlotCsv.Parse"/>), and, with
    /// <paramref name="add"/> (and always for an antenna's log, US10.0), adds it to what is shown as a recording of its own; without,
    /// it is the plot's only recording (US6.3). The main window passes <c>add</c> whenever the plot holds something, so what a
    /// user imports always stacks, and the replacing form is what an empty plot does and what a caller gets by not asking.
    /// False, with nothing changed, for a file that is neither kind.</summary>
    public bool LoadCsv(string path, string? sourceLabel = null, bool add = false)
    {
        if (PlotCsv.Parse(path) is not { } parsed) return false;
        ShowCsv(parsed.Data, parsed.Kind, sourceLabel, add);
        return true;
    }

    /// <summary>
    /// <see cref="LoadCsv"/> with the file read off the UI thread and <see cref="IsLoading"/> set meanwhile. A
    /// request while one is being read is dropped, as v2.x dropped it.
    /// </summary>
    public async Task<CsvLoad> LoadCsvAsync(string path, string? sourceLabel = null, bool add = false)
    {
        if (IsLoading) return CsvLoad.Dropped;
        IsLoading = true;
        try
        {
            if (await Task.Run(() => PlotCsv.Parse(path)) is not { } parsed) return CsvLoad.Failed;
            ShowCsv(parsed.Data, parsed.Kind, sourceLabel, add);
            return CsvLoad.Loaded;
        }
        finally
        {
            IsLoading = false;
        }
    }

    /// <summary>
    /// Shows a parsed CSV as a run's series would be shown (<see cref="PlotCsv.Series"/>), with the clock from the
    /// file; then a warning for any rows skipped, since a partly loaded file must not pass for a whole one.
    /// </summary>
    private void ShowCsv(CsvData data, CsvKind kind, string? sourceLabel, bool add)
    {
        // An antenna's log is always added to what the plot shows, to be compared with it (US10.0); any other CSV is when the
        // caller asks (the main window does, whenever the plot holds something); without, the file is the plot's only recording.
        bool added = (add || kind == CsvKind.PhasedArrayAntenna) && _series.Count > 0;
        _timeBase ??= new TimeBase();
        int? sourceId = null;
        Dictionary<int, int>? sourceMap = null;
        double shift = 0.0;
        if (added)
        {
            // By the clock, as the axis labels it (day of year and time of day): the log's first sample comes this many
            // seconds after elapsed zero, which is before it when the log starts earlier than everything shown.
            TimeBaseInfo clock = _timeBase.Info;
            double start = (data.BaseDay - clock.BaseDay) * 86400.0 + (data.BaseTimeOffset - clock.BaseTimeOffset);
            double end = SpanOf(_series);
            if (start > end || start + data.XMax < 0.0)
            {
                Warning?.Invoke(this, "The log's recording time does not overlap the data already shown. " +
                                      "They will share one time axis with a large gap between them.");
            }
            if (start < 0.0)
            {
                foreach (double[] xs in _series.Select(s => s.X).Distinct())
                    for (int i = 0; i < xs.Length; i++) xs[i] -= start;
                foreach (PlotSeries s in _series) s.Version++;
                // The base moves earlier by the same seconds, so a recording added next is placed against the log's clock.
                if (!_clockOnly) _timeBase.Adopt(_timeBase.Info.BaseAbsSeconds + start, hasSeries: true);
                _timeBase.SetClock(data.BaseDay, data.BaseTimeOffset);
                start = 0.0;
            }
            shift = start;
            int first = Sources.NextId;
            if (kind == CsvKind.PhasedArrayAntenna)
            {
                sourceId = first;   // a log is one recording, whatever its columns say
                if (sourceLabel is not null) SetSourceLabel(sourceId.Value, sourceLabel);
            }
            else
            {
                // An export holds one recording or several (a batch's, each column qualified "S<n>| "): each of its numbers
                // is given the next free one, so that none is a recording already on the plot, and is named for the file.
                List<int> inFile = data.Series.Select(s => s.SourceId).Distinct().Order().ToList();
                sourceMap = new Dictionary<int, int>();
                for (int i = 0; i < inFile.Count; i++)
                {
                    sourceMap[inFile[i]] = first + i;
                    if (sourceLabel is not null)
                        SetSourceLabel(first + i, inFile.Count == 1 ? sourceLabel : string.Create(CultureInfo.InvariantCulture, $"{sourceLabel}, Source {inFile[i]}"));
                }
            }
        }
        else
        {
            _series.Clear();
            _lockColours.Clear();
            Sources.ForgetNames();   // the new file is the only recording; the earlier names went with it
            _timeBase.SetClock(data.BaseDay, data.BaseTimeOffset);
            _clockOnly = true;   // the file has a clock and no absolute time
        }
        _window.ClearHistory();
        _readoutSeriesId = null;
        List<PlotSeries> imported = PlotCsv.Series(data, _lockAxisView, ref _nextId, kind, sourceId, shift, sourceMap);
        ShowOnlyTheCombinedChannels(imported);   // as a run's are: the rest are a click away in Customize View
        _series.AddRange(imported);
        // A file of one recording is named for it in Plot File and Remove File; one that holds several (a batch's export) keeps "Source N".
        if (!added && sourceLabel is not null && imported.Select(s => s.SourceId).Distinct().ToList() is [int only]) SetSourceLabel(only, sourceLabel);
        _window.ShowSpan(added ? SpanOf(_series) : data.XMax);
        PlotColors.Assign(imported, _lockColours);   // what was shown already keeps its colours
        ComputeSnrRange();
        SeriesChanged?.Invoke(this, EventArgs.Empty);
        RangesChanged?.Invoke(this, EventArgs.Empty);
        OnPropertyChanged(nameof(HasData));
        if (data.SkippedRows > 0)
            Warning?.Invoke(this, string.Create(CultureInfo.InvariantCulture,
                $"Skipped {data.SkippedRows} malformed row(s) while loading the CSV."));
    }

    /// <summary>Removes everything, as a new recording does: the series, the time base, the zoom history, the title,
    /// the view mode and the overrides.</summary>
    public void Clear()
    {
        _series.Clear();
        _lockColours.Clear();
        _window.Clear();
        _timeBase?.Reset();
        _clockOnly = false;
        _snrMin = 0.0;
        _snrMax = 1.0;
        _lockAxisView = LockAxisView.LockPercent;
        _leftMinOverride = _leftMaxOverride = _rightMinOverride = _rightMaxOverride = null;
        _readoutSeriesId = null;
        Title = DefaultTitle;
        Sources.Reset();
        SeriesChanged?.Invoke(this, EventArgs.Empty);
        RangesChanged?.Invoke(this, EventArgs.Empty);
        OnPropertyChanged(nameof(HasData));
        OnPropertyChanged(nameof(LeftAxisLabel));
    }

    /// <summary>Whether a series is drawn: shown, and of the recording the Plot File selector shows (US1.1), or of any
    /// when it shows them all.</summary>
    public bool EffectiveVisible(PlotSeries s) => s.Visible && Sources.IsShown(s);

    // ---- the recordings, and the Plot File selector (plan step 4.7) --------------------------------------------------

    /// <summary>The recordings the plot holds: their names, the one shown alone, their list and the id a new one takes. The
    /// members below that name a recording are this, kept on the plot for the menus and the main window.</summary>
    public PlotSources Sources { get; }

    /// <summary>The recording the plot shows, or -1 for every one overlaid.</summary>
    public int VisibleSource => Sources.Shown;

    /// <summary>Each recording the series come from, once, in the order first met, with its label: its file's name as
    /// given (<see cref="SetSourceLabel"/>), else "Source N".</summary>
    public IReadOnlyList<(int Id, string Label)> SourceList() => Sources.List();

    /// <summary>The id a recording added now would take: one past the greatest the plot holds, or 0 for an empty plot. A run is
    /// numbered with it, so that it never shares an id with data already shown (an imported CSV).</summary>
    public int NextSourceId => Sources.NextId;

    /// <summary>Names a recording in the Plot File selector.</summary>
    public void SetSourceLabel(int sourceId, string label) => Sources.Name(sourceId, label);

    /// <summary>
    /// Shows <paramref name="sourceId"/>'s series only, or every recording's for -1 (Plot File):
    /// the window is the span of what is shown, so browsing one file fills the plot, and the axes are ranged to it.
    /// It narrows what is drawn, never what a CSV holds.
    /// </summary>
    public void SetVisibleSource(int sourceId)
    {
        Sources.Show(sourceId);
        double lo = double.MaxValue, hi = double.MinValue;
        foreach (PlotSeries s in _series)
        {
            if (!EffectiveVisible(s) || s.X.Length == 0) continue;
            lo = Math.Min(lo, s.X[0]);
            hi = Math.Max(hi, s.X[^1]);
        }
        if (lo <= hi) _window.Show(lo, hi);
        ComputeSnrRange();
        OnPropertyChanged(nameof(VisibleSource));
        SeriesChanged?.Invoke(this, EventArgs.Empty);
        RangesChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>
    /// Removes every series of <paramref name="sourceId"/> (a recording, a batch file or an imported CSV) from the plot, and
    /// with them its name in Plot File, so what is kept is the other recordings' and nothing of this one: the plot, Customize
    /// View, the readout and the next CSV export no longer have it, and its memory is free. Process the recording again to
    /// bring it back. Removing the only recording leaves the plot as a new one starts (<see cref="Clear"/>). What is drawn is
    /// fitted again only when it changed: with the recording shown gone, or with every recording overlaid (the window then
    /// spans the ones that are left); with another one shown, the view is left where it is.
    /// </summary>
    /// <returns>False, with nothing changed, when no series comes from <paramref name="sourceId"/>.</returns>
    public bool RemoveSource(int sourceId) => RemoveSources(new[] { sourceId });

    /// <summary>
    /// <see cref="RemoveSource"/> for several recordings at once (Clear Plot): the window, the axes
    /// and the chart are worked out and redrawn once for all of them, not once for each. <see cref="SourceRemoved"/> is raised
    /// for each one removed, in the order given.
    /// </summary>
    /// <returns>False, with nothing changed, when no series comes from any of <paramref name="sourceIds"/>.</returns>
    public bool RemoveSources(IEnumerable<int> sourceIds)
    {
        List<int> removing = sourceIds.Distinct().Where(id => _series.Any(s => s.SourceId == id)).ToList();
        if (removing.Count == 0) return false;
        var gone = new HashSet<int>(removing);
        HashSet<string> removedNames = Sources.NamesOf(removing);
        if (_series.All(s => gone.Contains(s.SourceId)))
        {
            Clear();
        }
        else
        {
            _series.RemoveAll(s => gone.Contains(s.SourceId));
            AdvanceTimeBase();
            bool drawnChanged = Sources.Forget(gone);
            if (_readoutSeriesId is int pin && _series.All(s => s.Id != pin)) _readoutSeriesId = null;
            Title = Sources.TitleAfterRemoving(Title, removedNames);
            if (drawnChanged)
            {
                SetVisibleSource(Sources.Shown);   // the window and the axes follow what is left; it tells the view
            }
            else
            {
                SeriesChanged?.Invoke(this, EventArgs.Empty);
                RangesChanged?.Invoke(this, EventArgs.Empty);   // the same window, but at other elapsed times if zero moved
            }
        }
        foreach (int id in removing) SourceRemoved?.Invoke(this, id);
        return true;
    }

    /// <summary>
    /// The earliest recording gone, elapsed zero moves up to the earliest sample left, as adding one that starts earlier moves
    /// it back (<see cref="AddStream"/>): every series left, the window and the zoom history move earlier by as much, so each
    /// sample keeps its clock time and the axis starts at 0 again. The clock is moved by the same seconds here rather than
    /// recomputed from the absolute base, which a plot whose clock an imported CSV set does not keep. Nothing on a plot made
    /// with no clock (<see cref="SetSeries"/>).
    /// </summary>
    private void AdvanceTimeBase()
    {
        if (_timeBase is null) return;
        double? earliest = null;
        foreach (PlotSeries s in _series)
        {
            if (s.X.Length == 0) continue;
            double first = s.SortedX ? s.X[0] : s.X.Min();
            earliest = earliest is double e ? Math.Min(e, first) : first;
        }
        TimeBaseInfo clock = _timeBase.Info;
        double shift = _timeBase.Advance(earliest);
        if (shift <= 0.0) return;
        double offset = clock.BaseTimeOffset + shift;
        int days = (int)Math.Floor(offset / 86400.0);
        _timeBase.SetClock(clock.BaseDay + days, offset - days * 86400.0);
        // New arrays, one for each the series share, rather than the old ones moved: an export being written (Remove File is on
        // the plot's menu meanwhile) holds the old ones.
        var moved = new Dictionary<double[], double[]>(ReferenceEqualityComparer.Instance);
        foreach (PlotSeries s in _series)
        {
            if (!moved.TryGetValue(s.X, out double[]? xs))
            {
                xs = new double[s.X.Length];
                for (int i = 0; i < xs.Length; i++) xs[i] = s.X[i] - shift;
                moved[s.X] = xs;
            }
            s.MoveTimes(xs);
        }
        _window.Shift(shift);
    }

    /// <summary>
    /// What shows when Receiver SNR series arrive: of each stream's channels (a receiver's L, R and C, an antenna's H, V and C)
    /// only the combined one, which keeps a plot of many receivers readable; Customize View shows the others. A group with no
    /// combined channel (a receiver set up with fewer than three) keeps all of its series, so nothing arrives hidden entirely.
    /// </summary>
    public static void ShowOnlyTheCombinedChannels(IEnumerable<PlotSeries> series)
    {
        foreach (IGrouping<(int Source, int Stream, int Receiver), PlotSeries> group in
                 series.Where(s => s.Metric == MetricType.Snr).GroupBy(s => (s.SourceId, s.StreamOrder, s.ReceiverIndex)))
        {
            if (!group.Any(s => s.ChannelIndex == PlotColors.CombinedChannelIndex)) continue;
            foreach (PlotSeries s in group) s.Visible = s.ChannelIndex == PlotColors.CombinedChannelIndex;
        }
    }

    /// <summary>Whether a series of <paramref name="metric"/> is shown when it arrives: the left-axis metrics are shown
    /// one at a time, by the view.</summary>
    internal static bool ShownInView(MetricType metric, LockAxisView view) => metric switch
    {
        MetricType.FrameSyncLock => view == LockAxisView.LockPercent,
        MetricType.AccumulatedMissedFrames => view == LockAxisView.MissedFrames,
        _ => true,
    };

    /// <summary>Where the data ends: the latest last sample of any series, or 0.</summary>
    private static double SpanOf(IEnumerable<PlotSeries> series)
    {
        double xMax = 0.0;
        foreach (PlotSeries s in series)
            if (s.X.Length > 0) xMax = Math.Max(xMax, s.X[^1]);
        return xMax;
    }

    // ---- the axes -----------------------------------------------------------------------------------------------

    /// <summary>The left axis is drawn when something is on it, or when nothing is plotted at all (so an empty chart
    /// still reads as a chart); the right only when something is on it.</summary>
    public bool LeftAxisVisible => HasVisible(left: true) || !HasVisible(left: false);

    public bool RightAxisVisible => HasVisible(left: false);

    private bool HasVisible(bool left) => _series.Any(s => s.IsLeftAxis == left && EffectiveVisible(s));

    public double XMax => _window.XMax;
    public double XViewMin => _window.ViewMin;
    public double XViewMax => _window.ViewMax;

    /// <summary>The left axis's lower end: the override, else 0.</summary>
    public double LeftMin => _leftMinOverride ?? LockAxisMin;

    /// <summary>The left axis's upper end: the override; else 100 for lock percentage, or the largest visible
    /// missed-frames count with a 5 % margin; never closer to the lower end than <see cref="MinAxisSpan"/>.</summary>
    public double LeftMax
    {
        get
        {
            double max = _leftMaxOverride ?? (_lockAxisView == LockAxisView.MissedFrames
                ? MissedFramesMax() * (1.0 + AxisMarginFactor)
                : LockAxisMax);
            return Math.Max(max, LeftMin + MinAxisSpan);
        }
    }

    /// <summary>The right (SNR) axis's lower end: the override, else the visible SNR data's minimum rounded down to
    /// 5 dB, not below 0.</summary>
    public double RightMin => _rightMinOverride ?? _snrMin;

    /// <summary>The right axis's upper end: the override, else the visible data's maximum rounded up to 5 dB; never
    /// closer to the lower end than <see cref="MinAxisSpan"/>.</summary>
    public double RightMax => Math.Max(_rightMaxOverride ?? _snrMax, RightMin + MinAxisSpan);

    public bool HasRangeOverride => _leftMinOverride.HasValue || _leftMaxOverride.HasValue ||
                                    _rightMinOverride.HasValue || _rightMaxOverride.HasValue;

    public void SetLeftMinOverride(double? value) { _leftMinOverride = value; RangesChanged?.Invoke(this, EventArgs.Empty); }

    public void SetLeftMaxOverride(double? value) { _leftMaxOverride = value; RangesChanged?.Invoke(this, EventArgs.Empty); }

    public void SetRightMinOverride(double? value) { _rightMinOverride = value; RangesChanged?.Invoke(this, EventArgs.Empty); }

    public void SetRightMaxOverride(double? value) { _rightMaxOverride = value; RangesChanged?.Invoke(this, EventArgs.Empty); }

    /// <summary>Clears all four overrides: both axes scale to the data again.</summary>
    public void ResetYRange()
    {
        _leftMinOverride = _leftMaxOverride = _rightMinOverride = _rightMaxOverride = null;
        ComputeSnrRange();
        RangesChanged?.Invoke(this, EventArgs.Empty);
    }

    private double MissedFramesMax()
    {
        double max = 0.0;
        foreach (PlotSeries s in _series)
            if (s.Metric == MetricType.AccumulatedMissedFrames && EffectiveVisible(s)) max = Math.Max(max, s.YMax);
        // A stream with no missed frames still needs a scale.
        return max <= 0.0 ? 10.0 : max;
    }

    private void ComputeSnrRange()
    {
        double min = double.MaxValue, max = double.MinValue;
        bool any = false;
        foreach (PlotSeries s in _series)
        {
            if (!EffectiveVisible(s) || s.Y.Length == 0 || s.IsLeftAxis) continue;
            any = true;
            min = Math.Min(min, s.YMin);
            max = Math.Max(max, s.YMax);
        }
        if (!any)
        {
            _snrMin = 0.0;
            _snrMax = 1.0;
            return;
        }
        const double step = 5.0;
        _snrMin = Math.Max(0.0, Math.Floor(min / step) * step);
        _snrMax = Math.Ceiling(max / step) * step;
        if (_snrMax <= _snrMin) _snrMax = _snrMin + step;
    }

    /// <summary>Switches the left axis between lock percentage and missed frames. The streams shown carry over: a stream
    /// whose metric was hidden stays hidden in the other. A left maximum the user set is dropped, since it belonged to
    /// the other metric.</summary>
    public void SetLockAxisView(LockAxisView view)
    {
        if (_lockAxisView == view) return;
        LockAxisView old = _lockAxisView;
        _lockAxisView = view;
        _leftMaxOverride = null;
        static bool Matches(MetricType m, LockAxisView v) =>
            v == LockAxisView.LockPercent ? m == MetricType.FrameSyncLock : m == MetricType.AccumulatedMissedFrames;
        // By recording and stream: two recordings' CH 1 are two streams, each shown or hidden on its own.
        var selected = new Dictionary<(int Source, int Stream), bool>();
        foreach (PlotSeries s in _series)
            if (Matches(s.Metric, old)) selected[(s.SourceId, s.StreamOrder)] = s.Visible;
        foreach (PlotSeries s in _series)
            if (s.IsLeftAxis) s.Visible = Matches(s.Metric, view) && selected.GetValueOrDefault((s.SourceId, s.StreamOrder), true);
        SeriesChanged?.Invoke(this, EventArgs.Empty);
        RangesChanged?.Invoke(this, EventArgs.Empty);
        OnPropertyChanged(nameof(LockAxisView));
        OnPropertyChanged(nameof(LeftAxisLabel));
    }

    // ---- the time window ----------------------------------------------------------------------------------------

    public bool CanZoomBack => _window.CanZoomBack;

    /// <summary>A continuous gesture (wheel zoom, drag pan) moved the window: kept inside the data, its width kept, and
    /// not remembered in the zoom history.</summary>
    public void SetXViewByUser(double lower, double upper) => _window.SetByUser(lower, upper);

    /// <summary>The window a zoom by <paramref name="factor"/> (below 1 zooms in) about <paramref name="anchorSeconds"/>
    /// gives, the time at the anchor staying put; null when it would be narrower than <see cref="MinZoomSpanSeconds"/>.
    /// The wheel's rule (<see cref="WheelZoomFactor"/> a notch, the pointer's time the anchor): what the view then does with
    /// it is its own, a continuous gesture (<see cref="SetXViewByUser"/>).</summary>
    public (double Lower, double Upper)? WindowZoomedAbout(double anchorSeconds, double factor) =>
        _window.ZoomedAbout(anchorSeconds, factor);


    public void ZoomBack() => _window.Back();

    /// <summary>The whole recording again; the zoom history goes with it.</summary>
    public void ResetXRange() => _window.Reset();

    /// <summary>A band dragged out between two times: ignored when narrower than <see cref="MinZoomSpanSeconds"/> (a
    /// click, not a drag), otherwise kept inside the data and remembered as a discrete zoom.</summary>
    public void ApplyBandZoom(double from, double to) => _window.Band(from, to);

    // ---- the clock ----------------------------------------------------------------------------------------------

    /// <summary>The clock time of x = 0, in seconds after midnight: where the time ticks are snapped.</summary>
    public double TimeOrigin => _timeBase?.Info.BaseTimeOffset ?? 0.0;

    /// <summary>An elapsed time as the X axis labels it, DDD:HH:MM:SS.</summary>
    public string FormatTime(double elapsed)
    {
        TimeBaseInfo b = _timeBase?.Info ?? new TimeBaseInfo(0, 0.0, 0.0);
        return ClockTime.Format(b.BaseDay, b.BaseTimeOffset, elapsed);
    }

    // ---- the readout --------------------------------------------------------------------------------------------

    /// <summary>The series the readout is pinned to, or null for whichever is nearest the cursor.</summary>
    public int? ReadoutSeriesId
    {
        get => _readoutSeriesId;
        set => SetProperty(ref _readoutSeriesId, value);
    }

    /// <summary>
    /// What the readout says with the cursor at time <paramref name="x"/> (pixel column <paramref name="cursorPixel"/>):
    /// the nearest sample of any series shown, if it lies within 10 px of the cursor; or, when the readout is pinned,
    /// that series' nearest sample wherever it is. Null when there is nothing to say.
    /// </summary>
    public string? Readout(double x, double cursorPixel, Func<double, double> xToPixel) =>
        HasData ? PlotReadout.Describe(_series, EffectiveVisible, _readoutSeriesId, x, cursorPixel, xToPixel, FormatTime) : null;

    /// <summary>Drops a pin whose series is gone (processed again, which gives new ids) or hidden, so the Readout menu
    /// never opens with nothing chosen. The readout itself already falls back to the nearest series then.</summary>
    public void DropStaleReadoutPin()
    {
        if (_readoutSeriesId is not int id) return;
        PlotSeries? s = _series.Find(x => x.Id == id);
        if (s is null || !EffectiveVisible(s)) ReadoutSeriesId = null;
    }

    // ---- around the chart (plan step 4.4) -----------------------------------------------------------------------

    public bool HasLockSeries => _series.Any(s => s.Metric == MetricType.FrameSyncLock);

    public bool HasMissedFramesSeries => _series.Any(s => s.Metric == MetricType.AccumulatedMissedFrames);

    /// <summary>Whether there is another left-axis metric to switch to: the View Mode menu, its chip and V need both.</summary>
    public bool CanSwitchViewMode => HasData && HasLockSeries && HasMissedFramesSeries;

    /// <summary>Whether the legend is wanted. It is still drawn only when it has a row; a hidden legend stays out of
    /// exported images too. Remembered between sessions by the main window's view model, in the preferences.</summary>
    public bool LegendVisible
    {
        get => _legendVisible;
        set => SetProperty(ref _legendVisible, value);
    }

    /// <summary>Whether the window is narrower than the whole recording.</summary>
    public bool IsZoomed => _window.IsZoomed;

    /// <summary>Whether the view differs from the default (zoomed, or an axis limit set): the Reset view chip's rule.</summary>
    public bool CanResetView => HasData && (IsZoomed || HasRangeOverride);

    /// <summary>The whole span and automatic scaling again: the Reset view chip, the menu's Reset View and R.</summary>
    public void ResetView()
    {
        ResetXRange();
        ResetYRange();
    }

    /// <summary>Switches the left axis to the other metric; nothing unless both exist.</summary>
    public void ToggleViewMode()
    {
        if (!HasLockSeries || !HasMissedFramesSeries) return;
        SetLockAxisView(_lockAxisView == LockAxisView.LockPercent ? LockAxisView.MissedFrames : LockAxisView.LockPercent);
    }

    /// <summary>Slides the window by <paramref name="fraction"/> of its width (negative is earlier): the arrow keys.
    /// Nothing when the whole recording is shown, since there is nowhere to go.</summary>
    public void PanWindow(double fraction) => _window.Pan(fraction);

    /// <summary>Scales the window about its centre (below 1 zooms in): + and -. Never narrower than a band zoom may be.</summary>
    public void ZoomWindow(double factor) => _window.Zoom(factor);

    /// <summary>
    /// A time window entered or stepped to: each end clamped into the recording, with a warning naming the value as
    /// entered; the stop pulled up to the start if it is before it; then remembered as a discrete zoom.
    /// </summary>
    public void ApplyTimeWindow(double rawStart, double rawStop, string enteredStart, string enteredStop) =>
        _window.Apply(rawStart, rawStop, enteredStart, enteredStop);

    /// <summary>A clock time typed as DDD:HH:MM:SS, as elapsed seconds on the plot's axis; 0 for text not in that form,
    /// and 0 for a field that is not a whole number (a sign and spaces around it are allowed), as v2.x
    /// read it.</summary>
    public double ParseTime(string text)
    {
        string[] parts = text.Split(':');
        if (parts.Length != 4) return 0.0;
        static int Field(string s) =>
            int.TryParse(s, NumberStyles.AllowLeadingSign | NumberStyles.AllowLeadingWhite | NumberStyles.AllowTrailingWhite,
                         CultureInfo.InvariantCulture, out int v) ? v : 0;
        double entered = Field(parts[0]) * 86400.0 + Field(parts[1]) * 3600.0 + Field(parts[2]) * 60.0 + Field(parts[3]);
        TimeBaseInfo b = _timeBase?.Info ?? new TimeBaseInfo(0, 0.0, 0.0);
        return entered - (b.BaseDay * 86400.0 + b.BaseTimeOffset);
    }

    /// <summary>The rows the legends list: each series shown, of the left-axis metric in view, in plot order. A Receiver SNR
    /// series is marked <see cref="LegendEntry.IsSnr"/>: the view lists it in the SNR legend, and the rest in the Framesync one.</summary>
    public IReadOnlyList<LegendEntry> LegendEntries()
    {
        var entries = new List<LegendEntry>();
        foreach (PlotSeries s in _series)
        {
            if (!EffectiveVisible(s)) continue;
            if (s.Metric == MetricType.FrameSyncLock && _lockAxisView != LockAxisView.LockPercent) continue;
            if (s.Metric == MetricType.AccumulatedMissedFrames && _lockAxisView != LockAxisView.MissedFrames) continue;
            entries.Add(new LegendEntry(s.Id, LegendLabel(s), s.Name, s.Color, s.Dotted, s.Strong, s.Metric == MetricType.Snr));
        }
        return entries;
    }

    /// <summary>A series as the legend names it. An SNR series' name carries the whole TMATS stream title (the CSV
    /// columns need it), which crowds the legend, so its row is "CH&lt;channel&gt; &lt;L|R|C&gt;_RCVR&lt;n&gt;", made from the
    /// series' own numbers rather than parsed out of its name.</summary>
    public static string LegendLabel(PlotSeries s)
    {
        if (s.Metric != MetricType.Snr || s.IsPhasedArray) return s.Name;   // an antenna's series are named for their stream and polarization
        return string.Create(CultureInfo.InvariantCulture, $"CH{s.StreamOrder} {ChannelLabel(s.ChannelIndex)}_RCVR{s.ReceiverIndex}");
    }

    /// <summary>A phased array antenna's polarization letter as Customize View shows it: H, V or C (US10.0), the file's own
    /// language and not a receiver's L, R and C.</summary>
    public static string PolarizationLabel(int channelIndex) => channelIndex switch
    {
        0 => "H",
        1 => "V",
        2 => "C",
        _ => (channelIndex + 1).ToString(CultureInfo.InvariantCulture),
    };

    /// <summary>A channel's letter as Customize View shows it: L, R or C, else its number from 1.</summary>
    public static string ChannelLabel(int channelIndex) =>
        channelIndex >= 0 && channelIndex < ChannelPrefixes.Length
            ? ChannelPrefixes[channelIndex]
            : (channelIndex + 1).ToString(CultureInfo.InvariantCulture);

    // ---- appearance, from Customize View ------------------------------------------------------------------------

    /// <summary>Renames a series; a Frame Sync Lock stream's lock and missed-frames series are renamed together, so the
    /// name survives a switch of the view. Nothing for an id that is gone.</summary>
    public void RenameSeries(int id, string name)
    {
        PlotSeries? s = _series.Find(x => x.Id == id);
        if (s is null) return;
        s.Name = name;
        if (s.IsLeftAxis)
            foreach (PlotSeries sibling in _series)
                if (IsSibling(s, sibling)) sibling.Name = name;
    }

    /// <summary>Recolours a series, and a Frame Sync Lock stream's other metric with it.</summary>
    public void RecolorSeries(int id, Color color)
    {
        PlotSeries? s = _series.Find(x => x.Id == id);
        if (s is null) return;
        s.Color = color;
        if (s.IsLeftAxis)
            foreach (PlotSeries sibling in _series)
                if (IsSibling(s, sibling)) sibling.Color = color;
    }

    /// <summary>Shows or hides a series without telling the chart: Customize View sets many at once, then calls
    /// <see cref="CommitAppearance"/>.</summary>
    public void SetSeriesVisibleQuiet(int id, bool visible)
    {
        PlotSeries? s = _series.Find(x => x.Id == id);
        if (s is not null) s.Visible = visible;
    }

    /// <summary>One refresh after a batch of quiet edits: the SNR range again, then the chart and the legend.</summary>
    public void CommitAppearance()
    {
        ComputeSnrRange();
        SeriesChanged?.Invoke(this, EventArgs.Empty);
        RangesChanged?.Invoke(this, EventArgs.Empty);
    }

    public PlotSeries? SeriesById(int id) => _series.Find(x => x.Id == id);

    private static bool IsSibling(PlotSeries of, PlotSeries candidate) =>
        candidate.IsLeftAxis && candidate.StreamLabel == of.StreamLabel && candidate.StreamOrder == of.StreamOrder &&
        candidate.SourceId == of.SourceId;

    public void Dispose() => _timeBase?.Dispose();
}

/// <summary>What became of a request to read a CSV for the plot.</summary>
public enum CsvLoad
{
    /// <summary>Read and shown.</summary>
    Loaded,
    /// <summary>Not an exported CSV (or unreadable): nothing changed.</summary>
    Failed,
    /// <summary>Ignored, because another was being read.</summary>
    Dropped,
}

/// <summary>One legend row: the series it stands for, its label, its full name (the row's tooltip) and its colour.</summary>
public sealed record LegendEntry(int Id, string Label, string FullName, Color Color, bool Dotted = false, bool Strong = false, bool IsSnr = false);
