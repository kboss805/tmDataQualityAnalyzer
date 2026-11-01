using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using TmDq.Core;

namespace TmDq.App.Plot
{
    /// <summary>One plotted series: its identity, its samples on the shared elapsed axis, and how it is shown.</summary>
    public sealed class PlotSeries
    {
        public required int Id { get; init; }
        public required string Name { get; set; }
        public required string StreamLabel { get; init; }
        public required MetricType Metric { get; init; }
        public int ReceiverIndex { get; init; }
        public int ChannelIndex { get; init; }
        public int StreamOrder { get; init; }
        public int StreamSequence { get; init; }
        public int SourceId { get; init; }

        /// <summary>Seconds from the shared base; one array per stream, shared by its series, and moved in place when
        /// the base moves.</summary>
        public required double[] X { get; init; }

        public required double[] Y { get; init; }
        public double YMin { get; init; }
        public double YMax { get; init; }
        public Color Color { get; set; }
        public bool Visible { get; set; } = true;

        /// <summary>Whether the times are ordered, so the chart can walk only those in view (computed once).</summary>
        public bool SortedX { get; init; }

        /// <summary>Bumped when the samples move, so a drawing made from them is not reused.</summary>
        public int Version { get; internal set; }

        public bool IsLeftAxis => Metric is MetricType.FrameSyncLock or MetricType.AccumulatedMissedFrames;
    }

    /// <summary>Which metric the left axis shows: lock percentage or accumulated missed frames (US3.1).</summary>
    public enum LockAxisView
    {
        LockPercent,
        MissedFrames,
    }

    /// <summary>
    /// The plot's state, the C# half of the Qt <c>PlotViewModel</c> (plan decision D3): the series, built by the core from
    /// each processed stream on one shared time axis; their colours (US4.0); the visible time window with its zoom history;
    /// both Y axes' ranges and overrides; which metric the left axis shows; and the readout under the cursor. The chart
    /// (<c>PlotChart</c>) draws it and reports the user's gestures back; the menus and dialogs that set the rest are plan
    /// step 4.4's. Used on the UI thread.
    /// </summary>
    public sealed partial class PlotViewModel : ObservableObject, IDisposable
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

        private readonly List<PlotSeries> _series = new();
        private readonly List<(double Min, double Max)> _zoomStack = new();
        // Made with the first stream: the core is loaded only once a recording has been opened.
        private TimeBase? _timeBase;
        private int _nextId;
        private string _title = DefaultTitle;
        private double _xMax, _xViewMin, _xViewMax;
        private double _snrMin, _snrMax = 1.0;
        private double? _leftMinOverride, _leftMaxOverride, _rightMinOverride, _rightMaxOverride;
        private LockAxisView _lockAxisView = LockAxisView.LockPercent;
        private int? _readoutSeriesId;

        /// <summary>Raised when the series, their samples or their visibility change (the chart redraws them).</summary>
        public event EventHandler? SeriesChanged;

        /// <summary>Raised when the visible window or an axis range changes.</summary>
        public event EventHandler? RangesChanged;

        /// <summary>Raised when a source's recording does not overlap what is already loaded (informational: they share one
        /// time axis with a gap between them).</summary>
        public event EventHandler<int>? NonOverlappingSource;

        public IReadOnlyList<PlotSeries> Series => _series;

        public bool HasData => _series.Count > 0;

        public string Title
        {
            get => _title;
            set => SetProperty(ref _title, value);
        }

        public LockAxisView LockAxisView => _lockAxisView;

        public string LeftAxisLabel => _lockAxisView == LockAxisView.MissedFrames ? MissedFramesAxisLabel : LockAxisLabel;

        // ---- the series ---------------------------------------------------------------------------------------------

        /// <summary>
        /// Adds a finished stream's series (the core's <c>buildStreamSeries</c>), as the Qt <c>addStreamData</c> does: a
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
            int sourceId = result.SourceId;
            if (hadSeries && !_series.Any(s => s.SourceId == sourceId) && !_timeBase.Overlaps(times[0], times[^1], _xMax))
            {
                NonOverlappingSource?.Invoke(this, sourceId);
            }

            double shift = _timeBase.Adopt(times[0], hadSeries);
            if (shift > 0.0)
            {
                foreach (double[] xs in _series.Select(s => s.X).Distinct())
                    for (int i = 0; i < xs.Length; i++) xs[i] += shift;
                foreach (PlotSeries s in _series) s.Version++;
            }

            using (SeriesSet set = result.BuildSeries(_timeBase.Info.BaseAbsSeconds))
            {
                double[] elapsed = set.ElapsedSeconds.ToArray();
                bool sorted = ChartGeometry.IsSortedX(elapsed);
                for (int i = 0; i < set.Count; i++)
                {
                    SeriesSpec spec = set.Specs[i];
                    _series.Add(new PlotSeries
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
                        // The left-axis metrics are shown one at a time, by the active view.
                        Visible = spec.Metric switch
                        {
                            MetricType.FrameSyncLock => _lockAxisView == LockAxisView.LockPercent,
                            MetricType.AccumulatedMissedFrames => _lockAxisView == LockAxisView.MissedFrames,
                            _ => true,
                        },
                    });
                }
            }

            // A stable sort by job, so streams keep the order they were configured in whichever finished first.
            List<PlotSeries> ordered = _series.OrderBy(s => s.StreamSequence).ToList();
            _series.Clear();
            _series.AddRange(ordered);

            _xMax = 0.0;
            foreach (PlotSeries s in _series)
                if (s.X.Length > 0) _xMax = Math.Max(_xMax, s.X[^1]);
            _xViewMin = 0.0;
            _xViewMax = _xMax;

            AssignColors();
            ComputeSnrRange();
            SeriesChanged?.Invoke(this, EventArgs.Empty);
            RangesChanged?.Invoke(this, EventArgs.Empty);
            OnPropertyChanged(nameof(HasData));
        }

        /// <summary>Shows <paramref name="series"/> in place of whatever was shown: series that come from somewhere other
        /// than a run (an imported CSV, plan step 4.5; a test). They are coloured as a run's would be, and the whole span
        /// is shown.</summary>
        public void SetSeries(IEnumerable<PlotSeries> series)
        {
            _series.Clear();
            _series.AddRange(series.OrderBy(s => s.StreamSequence));
            _nextId = _series.Count == 0 ? 0 : _series.Max(s => s.Id) + 1;
            _xMax = 0.0;
            foreach (PlotSeries s in _series)
                if (s.X.Length > 0) _xMax = Math.Max(_xMax, s.X[^1]);
            _xViewMin = 0.0;
            _xViewMax = _xMax;
            _zoomStack.Clear();
            AssignColors();
            ComputeSnrRange();
            SeriesChanged?.Invoke(this, EventArgs.Empty);
            RangesChanged?.Invoke(this, EventArgs.Empty);
            OnPropertyChanged(nameof(HasData));
        }

        /// <summary>Removes everything, as a new recording does: the series, the time base, the zoom history, the title,
        /// the view mode and the overrides.</summary>
        public void Clear()
        {
            _series.Clear();
            _zoomStack.Clear();
            _timeBase?.Reset();
            _xMax = _xViewMin = _xViewMax = 0.0;
            _snrMin = 0.0;
            _snrMax = 1.0;
            _lockAxisView = LockAxisView.LockPercent;
            _leftMinOverride = _leftMaxOverride = _rightMinOverride = _rightMaxOverride = null;
            _readoutSeriesId = null;
            Title = DefaultTitle;
            SeriesChanged?.Invoke(this, EventArgs.Empty);
            RangesChanged?.Invoke(this, EventArgs.Empty);
            OnPropertyChanged(nameof(HasData));
            OnPropertyChanged(nameof(LeftAxisLabel));
        }

        public void SetSeriesVisible(int id, bool visible)
        {
            PlotSeries? s = _series.Find(x => x.Id == id);
            if (s is null || s.Visible == visible) return;
            s.Visible = visible;
            ComputeSnrRange();
            SeriesChanged?.Invoke(this, EventArgs.Empty);
            RangesChanged?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>Whether a series is drawn. The Plot File selector (US1.1) narrows this further in plan step 4.7.</summary>
        public bool EffectiveVisible(PlotSeries s) => s.Visible;

        /// <summary>Left-axis metrics share one colour per stream (keyed by PCM channel), so a stream's lock and missed
        /// frames curves match; an SNR series takes its receiver's primary, shaded by its receiver cycle and channel.</summary>
        private void AssignColors()
        {
            var leftIndex = new Dictionary<int, int>();
            foreach (PlotSeries s in _series)
            {
                if (s.IsLeftAxis)
                {
                    if (!leftIndex.TryGetValue(s.StreamOrder, out int idx))
                    {
                        idx = leftIndex.Count;
                        leftIndex[s.StreamOrder] = idx;
                    }
                    int n = PlotColors.FrameSyncLock.Count;
                    s.Color = PlotColors.Shade(PlotColors.FrameSyncLock[idx % n], idx / n);
                    continue;
                }
                int primaries = PlotColors.Snr.Count;
                int receiver = Math.Max(0, s.ReceiverIndex - 1);
                s.Color = PlotColors.Shade(PlotColors.Snr[receiver % primaries], receiver / primaries + s.ChannelIndex);
            }
        }

        // ---- the axes -----------------------------------------------------------------------------------------------

        /// <summary>The left axis is drawn when something is on it, or when nothing is plotted at all (so an empty chart
        /// still reads as a chart); the right only when something is on it.</summary>
        public bool LeftAxisVisible => HasVisible(left: true) || !HasVisible(left: false);

        public bool RightAxisVisible => HasVisible(left: false);

        private bool HasVisible(bool left) => _series.Any(s => s.IsLeftAxis == left && EffectiveVisible(s));

        public double XMin => 0.0;
        public double XMax => _xMax;
        public double XViewMin => _xViewMin;
        public double XViewMax => _xViewMax;

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
            var selected = new Dictionary<int, bool>();
            foreach (PlotSeries s in _series)
                if (Matches(s.Metric, old)) selected[s.StreamOrder] = s.Visible;
            foreach (PlotSeries s in _series)
                if (s.IsLeftAxis) s.Visible = Matches(s.Metric, view) && selected.GetValueOrDefault(s.StreamOrder, true);
            SeriesChanged?.Invoke(this, EventArgs.Empty);
            RangesChanged?.Invoke(this, EventArgs.Empty);
            OnPropertyChanged(nameof(LockAxisView));
            OnPropertyChanged(nameof(LeftAxisLabel));
        }

        // ---- the time window ----------------------------------------------------------------------------------------

        public bool CanZoomBack => _zoomStack.Count > 0;
        public int ZoomDepth => _zoomStack.Count;

        /// <summary>A continuous gesture (wheel zoom, drag pan) moved the window: kept inside the data, its width kept, and
        /// not remembered in the zoom history. The Qt <c>handlePlotXRangeChanged</c>.</summary>
        public void SetXViewByUser(double lower, double upper)
        {
            if (!HasData) return;
            double width = upper - lower;
            if (lower < 0.0)
            {
                lower = 0.0;
                upper = width;
            }
            if (upper > _xMax)
            {
                upper = _xMax;
                lower = Math.Max(0.0, _xMax - width);
            }
            SetXView(lower, upper);
        }

        /// <summary>A discrete zoom (a band dragged out, a time window entered): the window is remembered first, so
        /// <see cref="ZoomBack"/> returns to it; the oldest of more than <see cref="MaxZoomDepth"/> is forgotten.</summary>
        public void PushXZoom(double min, double max)
        {
            _zoomStack.Add((_xViewMin, _xViewMax));
            if (_zoomStack.Count > MaxZoomDepth) _zoomStack.RemoveAt(0);
            SetXView(min, max);
        }

        public void ZoomBack()
        {
            if (_zoomStack.Count == 0) return;
            (double min, double max) = _zoomStack[^1];
            _zoomStack.RemoveAt(_zoomStack.Count - 1);
            SetXView(min, max);
        }

        /// <summary>The whole recording again; the zoom history goes with it.</summary>
        public void ResetXRange()
        {
            _zoomStack.Clear();
            SetXView(0.0, _xMax);
        }

        /// <summary>A band dragged out between two times: ignored when narrower than <see cref="MinZoomSpanSeconds"/> (a
        /// click, not a drag), otherwise kept inside the data and remembered as a discrete zoom.</summary>
        public void ApplyBandZoom(double from, double to)
        {
            if (!HasData) return;
            double lower = Math.Min(from, to), upper = Math.Max(from, to);
            if (upper - lower < MinZoomSpanSeconds) return;
            double start = Math.Clamp(lower, 0.0, _xMax), stop = Math.Clamp(upper, 0.0, _xMax);
            PushXZoom(start, Math.Max(start, stop));
        }

        private void SetXView(double min, double max)
        {
            _xViewMin = min;
            _xViewMax = max;
            RangesChanged?.Invoke(this, EventArgs.Empty);
            OnPropertyChanged(nameof(CanZoomBack));
        }

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
        /// that series' nearest sample wherever it is. Null when there is nothing to say. The Qt <c>onPlotMouseMove</c>.
        /// </summary>
        public string? Readout(double x, double cursorPixel, Func<double, double> xToPixel)
        {
            if (!HasData) return null;
            int pinnedIndex = _readoutSeriesId is int id ? _series.FindIndex(s => s.Id == id) : -1;
            bool pinned = pinnedIndex >= 0;
            double bestDist = double.MaxValue, bestX = 0.0, bestY = 0.0;
            int best = -1;
            for (int i = 0; i < _series.Count; i++)
            {
                if (pinned && i != pinnedIndex) continue;
                PlotSeries s = _series[i];
                if (!EffectiveVisible(s) || s.X.Length == 0) continue;
                int at = LowerBound(s.X, x);
                for (int k = at; k < s.X.Length && k - at < 2; k++)
                    Consider(i, k);
                if (at > 0) Consider(i, at - 1);

                void Consider(int series, int index)
                {
                    double d = Math.Abs(_series[series].X[index] - x);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        bestX = _series[series].X[index];
                        bestY = _series[series].Y[index];
                        best = series;
                    }
                }
            }
            if (best < 0) return null;
            if (!pinned && Math.Abs(xToPixel(bestX) - cursorPixel) > 10.0) return null;
            PlotSeries found = _series[best];
            if (found.Name.Length == 0) return null;
            string unit = found.Metric switch
            {
                MetricType.FrameSyncLock => "%",
                MetricType.AccumulatedMissedFrames => " frames",
                _ => " dB",
            };
            return $"{found.Name}{(pinned ? "  (pinned)" : "")}\n{FormatTime(bestX)}\n" +
                   bestY.ToString("F2", CultureInfo.InvariantCulture) + unit;
        }

        private static int LowerBound(double[] xs, double value)
        {
            int lo = 0, hi = xs.Length;
            while (lo < hi)
            {
                int mid = lo + (hi - lo) / 2;
                if (xs[mid] < value) lo = mid + 1;
                else hi = mid;
            }
            return lo;
        }

        public void Dispose() => _timeBase?.Dispose();
    }
}
