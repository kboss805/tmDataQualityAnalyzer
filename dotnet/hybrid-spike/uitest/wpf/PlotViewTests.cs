using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.ExceptionServices;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Xunit;

namespace Tmdq.UiSpike.Wpf
{
    /// <summary>
    /// The same tests as avalonia/PlotViewTests.cs, against WPF. WPF has no headless platform, so each test
    /// runs on its own STA thread with the view in an HwndSource that is never shown: a real window handle
    /// (needed for keyboard events) on whatever desktop the process has, and nothing on screen.
    /// </summary>
    public sealed class PlotViewTests
    {
        private static void Sta(Action body)
        {
            Exception failure = null;
            var thread = new Thread(() =>
            {
                try { body(); }
                catch (Exception ex) { failure = ex; }
                finally { Dispatcher.CurrentDispatcher.InvokeShutdown(); }
            });
            thread.SetApartmentState(ApartmentState.STA);
            thread.Start();
            thread.Join();
            if (failure != null) { ExceptionDispatchInfo.Capture(failure).Throw(); }
        }

        /// <summary>Runs the dispatcher until bindings, item generation and layout have caught up.</summary>
        private static void Settle(UIElement root)
        {
            var frame = new DispatcherFrame();
            Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle, new Action(() => frame.Continue = false));
            Dispatcher.PushFrame(frame);
            root.UpdateLayout();
        }

        private static (HwndSource source, PlotView view, PlotVm vm) Host()
        {
            var vm = new PlotVm();
            var view = new PlotView { DataContext = vm };
            var source = new HwndSource(new HwndSourceParameters("tmdq-ui-test") { WindowStyle = 0, Width = 900, Height = 560 })
            {
                SizeToContent = SizeToContent.WidthAndHeight,
                RootVisual = view,
            };
            Settle(view);
            return (source, view, vm);
        }

        private static void Key(PlotView view, Key key)
        {
            var args = new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(view), 0, key)
            {
                RoutedEvent = Keyboard.KeyDownEvent,
            };
            view.Plot.RaiseEvent(args);
        }

        [Fact]
        public void LegendGrowsWithItsRowsAndScrollsOnceItIsFull() => Sta(() =>
        {
            var (source, view, vm) = Host();
            const double frameOnly = 2 * 8 + 2;

            vm.Series.Add("CH-01 PRN 15 5M");
            Settle(view);
            double oneRow = view.Legend.ActualHeight;
            Assert.True(oneRow > frameOnly + 8, $"one row: legend {view.Legend.ActualWidth}x{oneRow}");
            Assert.True(view.Legend.ActualWidth > 20 + 20);

            vm.Series.Add("CH-02 PRN 15 20M");
            Settle(view);
            Assert.True(view.Legend.ActualHeight > oneRow, $"two rows: {view.Legend.ActualHeight} vs {oneRow}");

            for (int i = 3; i <= 20; ++i) { vm.Series.Add($"CH{i} L_RCVR{i}"); }
            Settle(view);
            Assert.True(view.Legend.ActualHeight <= 120 + frameOnly + 0.5, $"capped: {view.Legend.ActualHeight}");
            Assert.True(view.LegendScroll.ExtentHeight > view.LegendScroll.ViewportHeight, "rows beyond the cap scroll");
            source.Dispose();
        });

        [Fact]
        public void TitleButtonKeepsTheSizeSetInCode() => Sta(() =>
        {
            var (source, view, _) = Host();
            Assert.Equal(new Size(30, 26), new Size(view.CloseButton.ActualWidth, view.CloseButton.ActualHeight));
            source.Dispose();
        });

        [Fact]
        public void KeyboardShortcutsDriveTheView() => Sta(() =>
        {
            var (source, view, vm) = Host();

            Key(view, System.Windows.Input.Key.L);
            Settle(view);
            Assert.False(vm.LegendVisible);
            Assert.Equal(Visibility.Collapsed, view.Legend.Visibility);
            Key(view, System.Windows.Input.Key.L);
            Settle(view);
            Assert.Equal(Visibility.Visible, view.Legend.Visibility);

            vm.SetView(0.4, 0.6);
            Key(view, System.Windows.Input.Key.Right);
            Assert.Equal(0.45, vm.ViewMin, 9);
            Assert.Equal(0.2, vm.ViewMax - vm.ViewMin, 9);
            Key(view, System.Windows.Input.Key.Left);
            Assert.Equal(0.4, vm.ViewMin, 9);

            Key(view, System.Windows.Input.Key.Home);
            Assert.Equal(PlotVm.XMin, vm.ViewMin);
            Assert.Equal(PlotVm.XMax, vm.ViewMax);
            source.Dispose();
        });

        [Fact]
        public void DraggingABandZoomsToIt() => Sta(() =>
        {
            // Through the view's handler, not the mouse: see PlotView.ApplyBand.
            var (source, view, vm) = Host();
            double w = view.Plot.ActualWidth;

            view.ApplyBand(0.25 * w, 0.75 * w);
            Assert.Equal(0.5, vm.ViewMin, 6);
            Assert.Equal(1.5, vm.ViewMax, 6);

            vm.ResetX();
            view.ApplyBand(0.75 * w, 0.25 * w);
            Assert.Equal(0.5, vm.ViewMin, 6);
            Assert.Equal(1.5, vm.ViewMax, 6);

            view.ApplyBand(0.5 * w, 0.5 * w);
            Assert.Equal(PlotVm.XMin, vm.ViewMin);
            Assert.Equal(PlotVm.XMax, vm.ViewMax);
            source.Dispose();
        });

        [Fact]
        public void ARenderedFrameShowsTheLegendOverThePlotWithText() => Sta(() =>
        {
            var (source, view, vm) = Host();
            vm.Series.Add("Ch 13 L");
            vm.Series.Add("Ch 13 R");
            Settle(view);

            var rtb = new RenderTargetBitmap((int)view.ActualWidth, (int)view.ActualHeight, 96, 96, PixelFormats.Pbgra32);
            rtb.Render(view);
            var pixels = new byte[rtb.PixelWidth * rtb.PixelHeight * 4];
            rtb.CopyPixels(pixels, rtb.PixelWidth * 4, 0);
            var enc = new PngBitmapEncoder();
            enc.Frames.Add(BitmapFrame.Create(rtb));
            using (var f = File.Create(Path.Combine(AppContext.BaseDirectory, "wpf-headless-frame.png"))) { enc.Save(f); }

            uint Pixel(Point p)
            {
                int i = ((int)p.Y * rtb.PixelWidth + (int)p.X) * 4;
                return (uint)(pixels[i + 2] << 16 | pixels[i + 1] << 8 | pixels[i]);
            }
            static int Sum(uint c) => (int)((c >> 16) & 0xFF) + (int)((c >> 8) & 0xFF) + (int)(c & 0xFF);

            Point inLegend = view.Legend.TranslatePoint(new Point(view.Legend.ActualWidth - 4, view.Legend.ActualHeight / 2), view);
            Point onPlot = view.Plot.TranslatePoint(new Point(40, view.Plot.ActualHeight - 40), view);
            uint l = Pixel(inLegend), p = Pixel(onPlot);
            Assert.True(Sum(p) > 3 * 245, $"plot pixel {p:X6} should be white");
            Assert.True(Sum(l) < 3 * 90, $"legend pixel {l:X6} should be the dark translucent legend over white");

            Point label = view.Legend.TranslatePoint(new Point(10 + 20, 8), view);
            var seen = new HashSet<uint>();
            for (int dy = 0; dy < 14; ++dy)
            {
                for (int dx = 0; dx < 40; ++dx) { seen.Add(Pixel(new Point(label.X + dx, label.Y + dy))); }
            }
            Assert.True(seen.Count > 6, $"only {seen.Count} distinct colours where the label text is: no glyphs drawn");
            source.Dispose();
        });

        // No counterpart to the Avalonia project's native-surface test. The D3DImage spike already measured
        // that RenderTargetBitmap does not reproduce a D3DImage composition (dotnet/hybrid-spike/README.md),
        // so offscreen renders cannot check the chart surface in WPF either; chart-host code needs a seam.
    }
}
