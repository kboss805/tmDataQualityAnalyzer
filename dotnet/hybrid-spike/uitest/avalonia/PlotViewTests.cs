using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using Avalonia.Threading;
using SkiaSharp;
using Xunit;

namespace Tmdq.UiSpike.Ava
{
    /// <summary>
    /// The project's measured-widget tests (tst_plotwidget's legend sizing, keyboard shortcuts and band
    /// zoom, the PR #60 title-button size) ported to Avalonia's headless platform. wpf/PlotViewTests.cs
    /// holds the same tests against WPF; the two differ only where the frameworks force them to.
    /// </summary>
    public sealed class PlotViewTests
    {
        private static (Window window, PlotView view, PlotVm vm) Show()
        {
            var vm = new PlotVm();
            var view = new PlotView { DataContext = vm };
            var window = new Window { Content = view, SizeToContent = SizeToContent.WidthAndHeight };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return (window, view, vm);
        }

        private static T Find<T>(PlotView view, string name) where T : Control => view.FindControl<T>(name);

        private static void Settle() => Dispatcher.UIThread.RunJobs();

        [AvaloniaFact]
        public void LegendGrowsWithItsRowsAndScrollsOnceItIsFull()
        {
            // tst_plotwidget's legendOverlaySizesCorrectlyAfterRebuild: a rebuild must leave a legend sized
            // to its rows, never a collapsed frame-only box.
            var (window, view, vm) = Show();
            Border legend = Find<Border>(view, "Legend");
            ScrollViewer scroll = Find<ScrollViewer>(view, "LegendScroll");
            const double frameOnly = 2 * 8 + 2;   // padding top + bottom, border

            vm.Series.Add("CH-01 PRN 15 5M");
            Settle();
            double oneRow = legend.Bounds.Height;
            Assert.True(oneRow > frameOnly + 8, $"one row: legend {legend.Bounds.Width}x{oneRow}");
            Assert.True(legend.Bounds.Width > 20 + 20);

            vm.Series.Add("CH-02 PRN 15 20M");
            Settle();
            Assert.True(legend.Bounds.Height > oneRow, $"two rows: {legend.Bounds.Height} vs {oneRow}");

            for (int i = 3; i <= 20; ++i) { vm.Series.Add($"CH{i} L_RCVR{i}"); }
            Settle();
            Assert.True(legend.Bounds.Height <= 120 + frameOnly + 0.5, $"capped: {legend.Bounds.Height}");
            Assert.True(scroll.Extent.Height > scroll.Viewport.Height, "rows beyond the cap scroll");
            window.Close();
        }

        [AvaloniaFact]
        public void TitleButtonKeepsTheSizeSetInCode()
        {
            // The PR #60 trap: a stylesheet minimum overrode the size set in code, and the render looked
            // fine. Measured, not looked at, under the theme the app actually ships.
            var (window, view, _) = Show();
            Button close = Find<Button>(view, "CloseButton");
            Assert.Equal(new Size(30, 26), close.Bounds.Size);
            window.Close();
        }

        [AvaloniaFact]
        public void KeyboardShortcutsDriveTheView()
        {
            var (window, view, vm) = Show();
            Border plot = Find<Border>(view, "Plot");
            Border legend = Find<Border>(view, "Legend");
            plot.Focus();
            Settle();

            window.KeyPressQwerty(PhysicalKey.L, RawInputModifiers.None);
            Settle();
            Assert.False(vm.LegendVisible);
            Assert.False(legend.IsEffectivelyVisible);
            window.KeyPressQwerty(PhysicalKey.L, RawInputModifiers.None);
            Settle();
            Assert.True(legend.IsEffectivelyVisible);

            vm.SetView(0.4, 0.6);
            window.KeyPressQwerty(PhysicalKey.ArrowRight, RawInputModifiers.None);
            Assert.Equal(0.45, vm.ViewMin, 9);
            Assert.Equal(0.2, vm.ViewMax - vm.ViewMin, 9);
            window.KeyPressQwerty(PhysicalKey.ArrowLeft, RawInputModifiers.None);
            Assert.Equal(0.4, vm.ViewMin, 9);

            window.KeyPressQwerty(PhysicalKey.Home, RawInputModifiers.None);
            Assert.Equal(PlotVm.XMin, vm.ViewMin);
            Assert.Equal(PlotVm.XMax, vm.ViewMax);
            window.Close();
        }

        [AvaloniaFact]
        public void DraggingABandZoomsToIt()
        {
            // tst_plotwidget's bandZoomAppliesDraggedRange calls applyBandZoom() directly. Here the drag
            // goes through the real pointer pipeline, at real coordinates.
            var (window, view, vm) = Show();
            Border plot = Find<Border>(view, "Plot");
            Point origin = plot.TranslatePoint(new Point(0, 0), window) ?? default;
            double w = plot.Bounds.Width, y = origin.Y + 200;

            window.MouseDown(new Point(origin.X + 0.25 * w, y), MouseButton.Left);
            window.MouseMove(new Point(origin.X + 0.5 * w, y));
            window.MouseUp(new Point(origin.X + 0.75 * w, y), MouseButton.Left);
            Assert.Equal(0.5, vm.ViewMin, 6);
            Assert.Equal(1.5, vm.ViewMax, 6);

            vm.ResetX();
            window.MouseDown(new Point(origin.X + 0.75 * w, y), MouseButton.Left);   // right to left
            window.MouseUp(new Point(origin.X + 0.25 * w, y), MouseButton.Left);
            Assert.Equal(0.5, vm.ViewMin, 6);
            Assert.Equal(1.5, vm.ViewMax, 6);

            window.MouseDown(new Point(origin.X + 0.5 * w, y), MouseButton.Left);    // a click
            window.MouseUp(new Point(origin.X + 0.5 * w, y), MouseButton.Left);
            Assert.Equal(PlotVm.XMin, vm.ViewMin);
            Assert.Equal(PlotVm.XMax, vm.ViewMax);
            window.Close();
        }

        [AvaloniaFact]
        public void ARenderedFrameShowsTheLegendOverThePlotWithText()
        {
            var (window, view, vm) = Show();
            vm.Series.Add("Ch 13 L");
            vm.Series.Add("Ch 13 R");
            Settle();
            Border legend = Find<Border>(view, "Legend");
            Border plot = Find<Border>(view, "Plot");

            WriteableBitmap frame = window.CaptureRenderedFrame();
            Assert.NotNull(frame);
            string png = Path.Combine(AppContext.BaseDirectory, "avalonia-headless-frame.png");
            frame.Save(png, new PngBitmapEncoderOptions());

            Point inLegend = legend.TranslatePoint(new Point(legend.Bounds.Width - 4, legend.Bounds.Height / 2), window) ?? default;
            Point onPlot = plot.TranslatePoint(new Point(40, plot.Bounds.Height - 40), window) ?? default;
            uint l = Pixel(frame, inLegend), p = Pixel(frame, onPlot);
            Assert.True(Sum(p) > 3 * 245, $"plot pixel {p:X6} should be white");
            Assert.True(Sum(l) < 3 * 90, $"legend pixel {l:X6} should be the dark translucent legend over white");

            // Real glyphs, not empty boxes: the label area must hold the antialiased edges of text.
            Point labelTopLeft = legend.TranslatePoint(new Point(10 + 20, 8), window) ?? default;
            int colours = DistinctColours(frame, labelTopLeft, 40, 14);
            Assert.True(colours > 6, $"only {colours} distinct colours where the label text is: no glyphs drawn");
            window.Close();
        }

        [AvaloniaFact]
        public async Task TheNativeChartSurfaceIsNotAvailableHeadless()
        {
            // Not a port of an existing test: it records what the headless platform can host. The chart is
            // a D3D11 texture imported through ICompositionGpuInterop; with no GPU there is nothing to
            // import into, so chart-host code needs a seam (a stub surface) to be tested here.
            var (window, _, _) = Show();
            Compositor compositor = ElementComposition.GetElementVisual(window)?.Compositor;
            Assert.NotNull(compositor);
            ICompositionGpuInterop interop = await compositor.TryGetCompositionGpuInterop();
            Assert.Null(interop);
            window.Close();
        }

        /// <summary>Draws straight onto Skia's canvas through a custom draw operation - the way the chart draws
        /// when D1 of avalonia-plan.md is Skia - and records whether Skia's canvas was offered at all.</summary>
        private sealed class SkiaDrawn : Control
        {
            public bool CanvasOffered;

            public override void Render(DrawingContext context) => context.Custom(new Op(this));

            private sealed class Op : ICustomDrawOperation
            {
                private readonly SkiaDrawn _owner;
                public Op(SkiaDrawn owner) { _owner = owner; }
                public Rect Bounds => new Rect(0, 0, 200, 120);
                public bool HitTest(Point p) => false;
                public bool Equals(ICustomDrawOperation other) => ReferenceEquals(this, other);
                public void Dispose() { }

                public void Render(ImmediateDrawingContext context)
                {
                    var feature = context.TryGetFeature<ISkiaSharpApiLeaseFeature>();
                    if (feature == null) { return; }
                    using ISkiaSharpApiLease lease = feature.Lease();
                    _owner.CanvasOffered = true;
                    lease.SkCanvas.Clear(SKColors.White);
                    using var red = new SKPaint { Color = new SKColor(220, 30, 30), Style = SKPaintStyle.Fill };
                    lease.SkCanvas.DrawRect(new SKRect(50, 30, 150, 90), red);
                }
            }
        }

        [AvaloniaFact]
        public void ASkiaDrawnChartRendersHeadless()
        {
            // The case for drawing the chart with Skia (avalonia-plan.md, D1): unlike a native GPU surface,
            // which the headless platform cannot import, a Skia draw operation reaches the headless frame -
            // so chart rendering can be asserted in tests, in Session 0, with no GPU.
            var control = new SkiaDrawn { Width = 200, Height = 120 };
            var window = new Window { Content = control, SizeToContent = SizeToContent.WidthAndHeight };
            window.Show();
            Settle();

            WriteableBitmap frame = window.CaptureRenderedFrame();
            Assert.True(control.CanvasOffered, "the headless platform did not offer a Skia canvas to the draw operation");
            uint inside = Pixel(frame, new Point(100, 60)), outside = Pixel(frame, new Point(10, 10));
            Assert.Equal(0xDC1E1Eu, inside);
            Assert.Equal(0xFFFFFFu, outside);
            window.Close();
        }

        [AvaloniaFact]
        public void TheHeadlessPlatformCreatesNoWin32Window()
        {
            // Why these tests need no desktop at all: a shown headless Window is not an HWND. (The WPF twin
            // needs one, if hidden, because WPF input and layout hang off an HwndSource.)
            var (window, _, _) = Show();
            Assert.True(window.IsVisible);
            Assert.Equal(0, TopLevelWin32WindowsOfThisProcess());

            // The counter must be able to see a window, or the zero above means nothing. A thread's first
            // window also brings Windows' hidden "Default IME" window with it, so expect at least one, not
            // exactly one - and note that had Avalonia made any HWND, that IME window would already be here.
            IntPtr probe = CreateWindowEx(0, "STATIC", "probe", 0, 0, 0, 1, 1, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
            Assert.NotEqual(IntPtr.Zero, probe);
            Assert.True(TopLevelWin32WindowsOfThisProcess() >= 1);
            DestroyWindow(probe);
            window.Close();
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr CreateWindowEx(int exStyle, string cls, string name, int style, int x, int y, int w, int h,
                                                    IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
        [DllImport("user32.dll")] private static extern bool DestroyWindow(IntPtr hwnd);
        private delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);

        private static int TopLevelWin32WindowsOfThisProcess()
        {
            uint me = (uint)Environment.ProcessId;
            int count = 0;
            EnumWindows((hwnd, _) =>
            {
                GetWindowThreadProcessId(hwnd, out uint pid);
                if (pid == me) { count++; }
                return true;
            }, IntPtr.Zero);
            return count;
        }

        private static uint Pixel(WriteableBitmap bmp, Point p)
        {
            using ILockedFramebuffer fb = bmp.Lock();
            int x = (int)p.X, y = (int)p.Y;
            uint v = (uint)Marshal.ReadInt32(fb.Address, y * fb.RowBytes + x * 4);
            // The headless frame is RGBA here, not BGRA: honour the format, or red and blue swap silently
            // (the colour-sum checks above could not notice; an exact colour does).
            if (fb.Format == PixelFormat.Rgba8888) { v = (v & 0xFF00FF00) | ((v & 0xFF) << 16) | ((v >> 16) & 0xFF); }
            return v & 0xFFFFFF;   // 0x00RRGGBB
        }

        private static int Sum(uint rgb) => (int)((rgb >> 16) & 0xFF) + (int)((rgb >> 8) & 0xFF) + (int)(rgb & 0xFF);

        private static int DistinctColours(WriteableBitmap bmp, Point origin, int w, int h)
        {
            var seen = new HashSet<uint>();
            for (int dy = 0; dy < h; ++dy)
            {
                for (int dx = 0; dx < w; ++dx) { seen.Add(Pixel(bmp, new Point(origin.X + dx, origin.Y + dy))); }
            }
            return seen.Count;
        }
    }
}
