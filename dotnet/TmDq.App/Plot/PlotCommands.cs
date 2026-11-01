using System;
using System.Threading.Tasks;

namespace TmDq.App.Plot
{
    /// <summary>
    /// What the plot's context menu items do that needs an answer from the user (plan step 4.4): the Qt
    /// <c>PlotWidget</c>'s <c>onSetPlotTitle</c>, <c>onSetTimeWindow</c>, the four axis limits and
    /// <c>onCustomizePlotClicked</c>. Each does nothing without data, as the Qt slots do (the menu disables them anyway).
    /// </summary>
    public sealed class PlotCommands
    {
        private readonly PlotViewModel _plot;
        private readonly IPlotPrompts _prompts;

        public PlotCommands(PlotViewModel plot, IPlotPrompts prompts)
        {
            _plot = plot;
            _prompts = prompts;
        }

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

        /// <summary>A value as <c>QDoubleSpinBox</c> keeps it: clamped to its range and rounded to its decimals.</summary>
        public static double SpinValue(double value, double min, double max, int decimals) =>
            Math.Round(Math.Clamp(value, min, Math.Max(min, max)), decimals, MidpointRounding.AwayFromZero);
    }
}
