using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Markup.Xaml;

namespace Tmdq.UiSpike.Ava
{
    /// <summary>The plot's keyboard shortcuts and drag-to-zoom, as PlotWidget has them: L toggles the
    /// legend, arrows slide the window, Home restores the full span, a left drag zooms to the band.</summary>
    public partial class PlotView : UserControl
    {
        private double? _dragFrom;

        public PlotView()
        {
            AvaloniaXamlLoader.Load(this);
            Border plot = this.FindControl<Border>("Plot");
            plot.KeyDown += OnPlotKeyDown;
            plot.PointerPressed += (s, e) =>
            {
                plot.Focus();
                _dragFrom = e.GetPosition(plot).X;
                e.Pointer.Capture(plot);
            };
            plot.PointerReleased += (s, e) =>
            {
                if (_dragFrom is not double from || DataContext is not PlotVm vm) { return; }
                double w = plot.Bounds.Width, to = e.GetPosition(plot).X;
                vm.ZoomBand(vm.DataAt(from, w), vm.DataAt(to, w));
                _dragFrom = null;
                e.Pointer.Capture(null);
            };
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
