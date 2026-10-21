using System.Windows.Controls;
using System.Windows.Input;

namespace Tmdq.UiSpike.Wpf
{
    /// <summary>The plot's keyboard shortcuts and drag-to-zoom (see the Avalonia twin).</summary>
    public partial class PlotView : UserControl
    {
        private double? _dragFrom;

        public PlotView()
        {
            InitializeComponent();
            Plot.KeyDown += OnPlotKeyDown;
            Plot.MouseLeftButtonDown += (s, e) =>
            {
                Plot.Focus();
                _dragFrom = e.GetPosition(Plot).X;
                Plot.CaptureMouse();
            };
            Plot.MouseLeftButtonUp += (s, e) =>
            {
                if (_dragFrom is not double from) { return; }
                ApplyBand(from, e.GetPosition(Plot).X);
                _dragFrom = null;
                Plot.ReleaseMouseCapture();
            };
        }

        /// <summary>The drag's effect, from two x positions on the plot. Public because WPF cannot inject a
        /// pointer event at a chosen position (a synthesised event reads the real cursor), so a test drives
        /// this rather than the mouse - as tst_plotwidget drives applyBandZoom().</summary>
        public void ApplyBand(double fromX, double toX)
        {
            if (DataContext is not PlotVm vm) { return; }
            double w = Plot.ActualWidth;
            vm.ZoomBand(vm.DataAt(fromX, w), vm.DataAt(toX, w));
        }

        private void OnPlotKeyDown(object sender, KeyEventArgs e)
        {
            if (DataContext is not PlotVm vm) { return; }
            switch (e.Key)
            {
                case Key.L: vm.ToggleLegend(); break;
                case Key.Right: vm.Step(+1); break;
                case Key.Left: vm.Step(-1); break;
                case Key.Home: vm.ResetX(); break;
                default: return;
            }
            e.Handled = true;
        }
    }
}
