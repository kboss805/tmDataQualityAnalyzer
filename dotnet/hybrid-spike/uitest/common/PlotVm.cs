using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Tmdq.UiSpike
{
    /// <summary>
    /// The view model both UI test spikes bind to: a cut-down PlotViewModel with exactly the state the
    /// ported tests exercise (legend rows, legend visibility, the X view window). Linked into the WPF and
    /// the Avalonia projects, so the two views and their tests run against one implementation.
    /// </summary>
    public sealed class PlotVm : INotifyPropertyChanged
    {
        public const double XMin = 0.0, XMax = 2.0;

        private bool _legendVisible = true;
        private double _viewMin = XMin, _viewMax = XMax;

        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>One legend row per visible series.</summary>
        public ObservableCollection<string> Series { get; } = new ObservableCollection<string>();

        public bool LegendVisible
        {
            get => _legendVisible;
            set { if (_legendVisible != value) { _legendVisible = value; Raise(); } }
        }

        public double ViewMin { get => _viewMin; private set { _viewMin = value; Raise(); } }
        public double ViewMax { get => _viewMax; private set { _viewMax = value; Raise(); } }

        public void ToggleLegend() => LegendVisible = !LegendVisible;

        /// <summary>Arrow keys: slide the window a quarter of its width, keeping the width.</summary>
        public void Step(int direction)
        {
            double span = ViewMax - ViewMin;
            double lo = Math.Clamp(ViewMin + direction * span / 4.0, XMin, XMax - span);
            SetView(lo, lo + span);
        }

        /// <summary>A dragged band zooms to exactly that span, in either drag direction; a zero-width drag
        /// (a click) must not collapse the view to nothing, so it leaves the full span.</summary>
        public void ZoomBand(double a, double b)
        {
            if (Math.Abs(a - b) < 1e-9) { ResetX(); return; }
            SetView(Math.Max(XMin, Math.Min(a, b)), Math.Min(XMax, Math.Max(a, b)));
        }

        public void ResetX() => SetView(XMin, XMax);

        public void SetView(double lo, double hi) { ViewMin = lo; ViewMax = hi; }

        /// <summary>Maps an x position on a plot `width` pixels wide to a data value in the current window.</summary>
        public double DataAt(double x, double width) => ViewMin + x / width * (ViewMax - ViewMin);

        private void Raise([CallerMemberName] string name = null) =>
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
