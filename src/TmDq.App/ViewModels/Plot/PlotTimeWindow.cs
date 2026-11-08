using System;
using System.Collections.Generic;

namespace TmDq.App.ViewModels.Plot
{
    /// <summary>
    /// The plot's time axis: the span of the data, the window shown on it, and the zoom history behind
    /// <see cref="Back"/> (US4.0, US4.1). The continuous gestures (wheel, drag) move the window without remembering it;
    /// the discrete ones (a band, a time window entered or stepped to) remember the window first. Kept for
    /// <see cref="PlotViewModel"/>, which owns the series and says when there is data.
    /// </summary>
    internal sealed class PlotTimeWindow
    {
        private readonly List<(double Min, double Max)> _history = new();
        private readonly Func<bool> _hasData;
        private readonly Func<double, string> _formatTime;

        /// <param name="hasData">Whether the plot has series: every gesture is ignored without.</param>
        /// <param name="formatTime">An elapsed time as the axis labels it, for a window stepped to by a key.</param>
        public PlotTimeWindow(Func<bool> hasData, Func<double, string> formatTime)
        {
            _hasData = hasData;
            _formatTime = formatTime;
        }

        /// <summary>Raised when a gesture moves the window (not by <see cref="ShowSpan"/>, <see cref="Show"/> or
        /// <see cref="Clear"/>, whose caller tells the chart itself).</summary>
        public event EventHandler? Changed;

        /// <summary>Raised with a warning for the log: an end of an entered window outside the data, or the stop before
        /// the start.</summary>
        public event EventHandler<string>? Warning;

        public double XMax { get; private set; }
        public double ViewMin { get; private set; }
        public double ViewMax { get; private set; }

        public bool CanZoomBack => _history.Count > 0;
        public int Depth => _history.Count;

        /// <summary>Whether the window is narrower than the whole span (by <see cref="FuzzyEqual"/>).</summary>
        public bool IsZoomed => _hasData() && (!FuzzyEqual(ViewMin, 0.0) || !FuzzyEqual(ViewMax, XMax));

        /// <summary>The data now ends at <paramref name="xMax"/>, and the whole of it is shown. The history is kept.</summary>
        public void ShowSpan(double xMax)
        {
            XMax = xMax;
            ViewMin = 0.0;
            ViewMax = xMax;
        }

        /// <summary>Shows <paramref name="min"/> to <paramref name="max"/> as given, with no history and no notice.</summary>
        public void Show(double min, double max)
        {
            ViewMin = min;
            ViewMax = max;
        }

        public void ClearHistory() => _history.Clear();

        /// <summary>No data: nothing to show and no history.</summary>
        public void Clear()
        {
            _history.Clear();
            XMax = ViewMin = ViewMax = 0.0;
        }

        /// <summary>A continuous gesture (wheel zoom, drag pan) moved the window: kept inside the data, its width kept, and
        /// not remembered in the zoom history.</summary>
        public void SetByUser(double lower, double upper)
        {
            if (!_hasData()) return;
            double width = upper - lower;
            if (lower < 0.0)
            {
                lower = 0.0;
                upper = width;
            }
            if (upper > XMax)
            {
                upper = XMax;
                lower = Math.Max(0.0, XMax - width);
            }
            Set(lower, upper);
        }

        /// <summary>A discrete zoom (a band dragged out, a time window entered): the window is remembered first, so
        /// <see cref="Back"/> returns to it; the oldest of more than <see cref="PlotViewModel.MaxZoomDepth"/> is forgotten.</summary>
        public void Push(double min, double max)
        {
            _history.Add((ViewMin, ViewMax));
            if (_history.Count > PlotViewModel.MaxZoomDepth) _history.RemoveAt(0);
            Set(min, max);
        }

        public void Back()
        {
            if (_history.Count == 0) return;
            (double min, double max) = _history[^1];
            _history.RemoveAt(_history.Count - 1);
            Set(min, max);
        }

        /// <summary>The whole span again; the zoom history goes with it.</summary>
        public void Reset()
        {
            _history.Clear();
            Set(0.0, XMax);
        }

        /// <summary>A band dragged out between two times: ignored when narrower than
        /// <see cref="PlotViewModel.MinZoomSpanSeconds"/> (a click, not a drag), otherwise kept inside the data and
        /// remembered as a discrete zoom.</summary>
        public void Band(double from, double to)
        {
            if (!_hasData()) return;
            double lower = Math.Min(from, to), upper = Math.Max(from, to);
            if (upper - lower < PlotViewModel.MinZoomSpanSeconds) return;
            double start = Math.Clamp(lower, 0.0, XMax), stop = Math.Clamp(upper, 0.0, XMax);
            Push(start, Math.Max(start, stop));
        }

        /// <summary>Slides the window by <paramref name="fraction"/> of its width (negative is earlier): the arrow keys.
        /// Nothing when the whole span is shown, since there is nowhere to go.</summary>
        public void Pan(double fraction)
        {
            double lower = ViewMin, upper = ViewMax, span = upper - lower;
            if (span <= 0.0) return;
            if (lower <= 0.0 && upper >= XMax) return;
            double shift = span * fraction;
            Apply(lower + shift, upper + shift, _formatTime(lower + shift), _formatTime(upper + shift));
        }

        /// <summary>Scales the window about its centre (below 1 zooms in): + and -. Never narrower than a band zoom may be.</summary>
        public void Zoom(double factor)
        {
            double lower = ViewMin, upper = ViewMax, span = upper - lower;
            if (span <= 0.0) return;
            double centre = lower + span / 2.0, newSpan = span * factor;
            if (newSpan < PlotViewModel.MinZoomSpanSeconds) return;
            double newLower = centre - newSpan / 2.0, newUpper = centre + newSpan / 2.0;
            Apply(newLower, newUpper, _formatTime(newLower), _formatTime(newUpper));
        }

        /// <summary>
        /// A time window entered or stepped to: each end clamped into the data, with a warning naming the value as
        /// entered; the stop pulled up to the start if it is before it; then remembered as a discrete zoom.
        /// </summary>
        public void Apply(double rawStart, double rawStop, string enteredStart, string enteredStop)
        {
            if (!_hasData()) return;
            double start = Math.Clamp(rawStart, 0.0, XMax), stop = Math.Clamp(rawStop, 0.0, XMax);
            if (!FuzzyEqual(start, rawStart))
                Warning?.Invoke(this, $"Start \"{enteredStart}\" is outside the file time range - clamped to file bounds.");
            if (!FuzzyEqual(stop, rawStop))
                Warning?.Invoke(this, $"Stop \"{enteredStop}\" is outside the file time range - clamped to file bounds.");
            if (start > stop)
            {
                Warning?.Invoke(this, "Stop time is before Start - clamped to start time.");
                stop = start;
            }
            Push(start, stop);
        }

        private void Set(double min, double max)
        {
            ViewMin = min;
            ViewMax = max;
            Changed?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>Equal to about twelve significant digits of the smaller magnitude (so 0 equals only 0).</summary>
        private static bool FuzzyEqual(double a, double b) => Math.Abs(a - b) * 1000000000000.0 <= Math.Min(Math.Abs(a), Math.Abs(b));
    }
}
