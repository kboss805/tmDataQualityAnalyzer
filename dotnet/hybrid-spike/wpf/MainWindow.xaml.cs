using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Tmdq.WpfSpike
{
    /// <summary>
    /// Runs a scripted benchmark of the native chart shown through D3DImage, then writes
    /// results.txt next to the exe and exits. Pass --interactive to keep the window open instead
    /// (the legend and crosshair then react to the mouse).
    /// </summary>
    public partial class MainWindow : Window
    {
        private const int PlotW = 1200, PlotH = 700;

        private sealed class Phase
        {
            public string Name;
            public int Series, Samples, Frames;
            public bool Fence, Worker;
        }

        private static readonly Phase[] Phases =
        {
            new Phase { Name = "empty: D3DImage path only",              Series = 0,  Samples = 1,       Frames = 400, Fence = true },
            new Phase { Name = "3 x 360k, UI thread, fenced",            Series = 3,  Samples = 360000,  Frames = 400, Fence = true },
            new Phase { Name = "12 x 360k, UI thread, fenced",           Series = 12, Samples = 360000,  Frames = 300, Fence = true },
            new Phase { Name = "3 x 1M, UI thread, fenced",              Series = 3,  Samples = 1000000, Frames = 300, Fence = true },
            new Phase { Name = "3 x 360k, UI thread, NOT fenced",        Series = 3,  Samples = 360000,  Frames = 400, Fence = false },
            new Phase { Name = "3 x 360k, worker thread, single buffer", Series = 3,  Samples = 360000,  Frames = 400, Fence = true, Worker = true },
        };

        [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr hwnd);
        [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);
        [DllImport("gdi32.dll")] private static extern uint GetPixel(IntPtr dc, int x, int y);
        [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleDC(IntPtr dc);
        [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleBitmap(IntPtr dc, int w, int h);
        [DllImport("gdi32.dll")] private static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
        [DllImport("gdi32.dll")] private static extern bool BitBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, uint rop);
        [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr obj);
        [DllImport("gdi32.dll")] private static extern bool DeleteDC(IntPtr dc);

        private readonly D3DImage _image = new D3DImage();
        private IntPtr _chart;
        private readonly StringBuilder _report = new StringBuilder();
        private readonly bool _interactive = Environment.GetCommandLineArgs().Contains("--interactive");

        // per-phase measurement state
        private Phase _phase;
        private int _phaseIndex = -1, _frame;
        private readonly List<double> _renderMs = new List<double>();
        private readonly List<double> _lockMs = new List<double>();
        private readonly List<double> _intervalMs = new List<double>();
        private TimeSpan _lastRenderingTime = TimeSpan.MinValue;
        private int _tornOrBad, _transient, _sampled;
        private readonly List<string> _badDetail = new List<string>();
        private double _nominalIntervalMs = 16.7;
        private Thread _worker;
        private volatile bool _workerStop;
        private long _workerFrame, _shownWorkerFrame;
        private readonly List<double> _workerMs = new List<double>();

        public MainWindow()
        {
            InitializeComponent();
            Loaded += OnLoaded;
        }

        private void Log(string line)
        {
            _report.AppendLine(line);
            Console.WriteLine(line);
        }

        private void OnLoaded(object sender, RoutedEventArgs e)
        {
            Topmost = true;   // the screen-pixel tear check reads what is actually composed
            _chart = ChartNative.tmdq_chart_create(PlotW, PlotH);
            IntPtr surface = _chart == IntPtr.Zero ? IntPtr.Zero : ChartNative.tmdq_chart_surface(_chart);
            if (surface == IntPtr.Zero)
            {
                Log("CREATE FAILED: " + (_chart == IntPtr.Zero ? "out of memory" : ChartNative.Error(_chart)));
                Finish(2);
                return;
            }

            _image.Lock();
            _image.SetBackBuffer(D3DResourceType.IDirect3DSurface9, surface);
            _image.Unlock();
            Chart.Source = _image;

            Log("adapter: " + ChartNative.Adapter(_chart) + (ChartNative.tmdq_chart_is_hardware(_chart) == 1 ? "  (hardware)" : "  (WARP software)"));
            Log("runtime: " + Environment.Version + ", DPI scale " + VisualTreeHelper.GetDpi(this).DpiScaleX);
            Log("render tier: " + (RenderCapability.Tier >> 16) + "  (2 = full hardware acceleration)");
            Log("");

            MouseMove += OnMouseMove;
            MouseLeave += (s, a) => { Crosshair.Visibility = Visibility.Collapsed; Readout.Visibility = Visibility.Collapsed; };

            if (_interactive)
            {
                ChartNative.tmdq_chart_generate(_chart, 3, 360000);
                StartPhase(new Phase { Name = "interactive", Series = 3, Samples = 360000, Frames = int.MaxValue, Fence = true });
                CompositionTarget.Rendering += OnRendering;
                return;
            }

            CompositionTarget.Rendering += OnRendering;
            NextPhase();
        }

        private void OnMouseMove(object sender, MouseEventArgs e)
        {
            Point p = e.GetPosition(Stage);
            Crosshair.X1 = Crosshair.X2 = p.X;
            Crosshair.Visibility = Visibility.Visible;
            Readout.Text = string.Format("x {0:F0}  frame {1}", p.X, _frame);
            Readout.Margin = new Thickness(p.X + 8, p.Y + 8, 0, 0);
            Readout.Visibility = Visibility.Visible;
        }

        // ---- phases -------------------------------------------------------------------------

        private void NextPhase()
        {
            _phaseIndex++;
            if (_phaseIndex >= Phases.Length) { Conclude(); return; }
            StartPhase(Phases[_phaseIndex]);
        }

        private void StartPhase(Phase p)
        {
            StopWorker();
            _phase = p;
            _frame = 0;
            _renderMs.Clear(); _lockMs.Clear(); _intervalMs.Clear(); _workerMs.Clear();
            _tornOrBad = 0; _transient = 0; _sampled = 0; _badDetail.Clear();
            _lastRenderingTime = TimeSpan.MinValue;
            ChartNative.tmdq_chart_generate(_chart, p.Series, p.Samples);
            _workerFrame = 0; _shownWorkerFrame = 0;
            if (p.Worker) { StartWorker(); }
            Status.Text = "running: " + p.Name;
        }

        private void ViewWindow(int frame, out double xl, out double xu)
        {
            // A 20% window panning across the data, so the culling and the envelope both do work.
            int span = Math.Max(1, _phase.Samples / 5);
            int travel = Math.Max(1, _phase.Samples - span);
            xl = (frame * 997L) % travel;
            xu = xl + span;
        }

        private void StartWorker()
        {
            _workerStop = false;
            IntPtr chart = _chart;
            Phase ph = _phase;
            _worker = new Thread(() =>
            {
                long n = 0;
                while (!_workerStop)
                {
                    n++;
                    double xl, xu;
                    int span = Math.Max(1, ph.Samples / 5);
                    int travel = Math.Max(1, ph.Samples - span);
                    xl = (n * 997L) % travel; xu = xl + span;
                    ChartTiming t;
                    ChartNative.tmdq_chart_render(chart, xl, xu, 5, 35, (uint)(n & 0xFFFF), 1, out t);
                    lock (_workerMs) { _workerMs.Add(t.GeometryMs + t.RasterMs + t.GpuWaitMs); }
                    Interlocked.Exchange(ref _workerFrame, n);
                }
            }) { IsBackground = true, Name = "chart render" };
            _worker.Start();
        }

        private void StopWorker()
        {
            if (_worker == null) { return; }
            _workerStop = true;
            _worker.Join();
            _worker = null;
        }

        private void OnRendering(object sender, EventArgs args)
        {
            if (_phase == null) { return; }
            TimeSpan now = ((RenderingEventArgs)args).RenderingTime;
            if (now == _lastRenderingTime) { return; }   // WPF can call twice for one frame
            if (_lastRenderingTime != TimeSpan.MinValue) { _intervalMs.Add((now - _lastRenderingTime).TotalMilliseconds); }
            _lastRenderingTime = now;

            // What is on the glass NOW is the previous frame's result: check it before replacing it.
            if (_frame > 3) { SampleOnScreen(); }

            _frame++;
            var sw = System.Diagnostics.Stopwatch.StartNew();

            if (_phase.Worker)
            {
                long latest = Interlocked.Read(ref _workerFrame);
                if (latest == _shownWorkerFrame) { return; }   // nothing new
                _shownWorkerFrame = latest;
                _image.Lock();
                _image.AddDirtyRect(new Int32Rect(0, 0, PlotW, PlotH));
                _image.Unlock();
                _lockMs.Add(sw.Elapsed.TotalMilliseconds);
            }
            else
            {
                double xl, xu;
                ViewWindow(_frame, out xl, out xu);
                _image.Lock();
                ChartTiming t;
                ChartNative.tmdq_chart_render(_chart, xl, xu, 5, 35, (uint)(_frame & 0xFFFF), _phase.Fence ? 1 : 0, out t);
                _image.AddDirtyRect(new Int32Rect(0, 0, PlotW, PlotH));
                _image.Unlock();
                _renderMs.Add(t.GeometryMs + t.RasterMs + t.GpuWaitMs);
                _lockMs.Add(sw.Elapsed.TotalMilliseconds);
            }

            if (_interactive) { Status.Text = "interactive - frame " + _frame; return; }
            if (_frame >= _phase.Frames) { EndPhase(); }
        }

        // ---- checks -------------------------------------------------------------------------

        /// <summary>
        /// Reads the top and bottom marker strips back from the composed desktop. Two GetPixel calls
        /// are not atomic against the desktop compositor, so a mismatch is re-read about 2 ms later:
        /// one that resolves is a sampling race (the desktop updated between the two reads), one that
        /// is still there is a tear that stayed on the glass for at least a couple of milliseconds.
        /// </summary>
        private void SampleOnScreen()
        {
            Point top = Chart.PointToScreen(new Point(20, 5));
            Point bot = Chart.PointToScreen(new Point(20, PlotH - 5));
            IntPtr dc = GetDC(IntPtr.Zero);
            try
            {
                _sampled++;
                uint a, b;
                if (Consistent(dc, top, bot, out a, out b)) { return; }
                _transient++;
                Thread.Sleep(2);
                uint a2, b2;
                if (Consistent(dc, top, bot, out a2, out b2)) { return; }
                _tornOrBad++;
                if (_badDetail.Count < 6)
                {
                    _badDetail.Add(string.Format("frame {0}: top marker {1:X4}, bottom marker {2:X4} (blue {3:X2}/{4:X2}); 2 ms later {5:X4} / {6:X4}",
                        _frame, a & 0xFFFF, b & 0xFFFF, (a >> 16) & 0xFF, (b >> 16) & 0xFF, a2 & 0xFFFF, b2 & 0xFFFF));
                }
            }
            finally { ReleaseDC(IntPtr.Zero, dc); }
        }

        private static bool Consistent(IntPtr dc, Point top, Point bot, out uint a, out uint b)
        {
            a = GetPixel(dc, (int)top.X, (int)top.Y);
            b = GetPixel(dc, (int)bot.X, (int)bot.Y);
            // COLORREF is 0x00BBGGRR. B = 0xA5 marks a marker pixel at all.
            return ((a >> 16) & 0xFF) == 0xA5 && ((b >> 16) & 0xFF) == 0xA5 && (a & 0xFFFF) == (b & 0xFFFF);
        }

        private static double Pct(List<double> v, double p)
        {
            if (v.Count == 0) { return 0; }
            var s = v.OrderBy(x => x).ToList();
            return s[Math.Min(s.Count - 1, (int)Math.Floor(p * (s.Count - 1) + 0.5))];
        }

        private void EndPhase()
        {
            StopWorker();
            // Discard warm-up.
            List<double> render = _renderMs.Skip(10).ToList();
            List<double> intervals = _intervalMs.Skip(10).ToList();
            if (_phaseIndex == 0 && intervals.Count > 0) { _nominalIntervalMs = Pct(intervals, 0.5); }
            int late = intervals.Count(x => x > _nominalIntervalMs * 1.5);

            Log("== " + _phase.Name);
            if (_phase.Worker)
            {
                List<double> w;
                lock (_workerMs) { w = _workerMs.Skip(10).ToList(); }
                Log(string.Format("   worker rendered {0} frames, {1:F1} ms median / {2:F1} p95 (geometry + raster + GPU), shown by UI {3} times",
                                  _workerMs.Count, Pct(w, 0.5), Pct(w, 0.95), _lockMs.Count));
                Log(string.Format("   UI thread Lock+AddDirtyRect+Unlock: {0:F3} ms median, {1:F3} max",
                                  Pct(_lockMs, 0.5), _lockMs.Count == 0 ? 0 : _lockMs.Max()));
            }
            else
            {
                Log(string.Format("   native render (geometry+raster+GPU wait): {0:F1} ms median, {1:F1} p95, {2:F1} max",
                                  Pct(render, 0.5), Pct(render, 0.95), render.Count == 0 ? 0 : render.Max()));
                Log(string.Format("   incl. Lock/AddDirtyRect/Unlock:           {0:F1} ms median, {1:F1} p95",
                                  Pct(_lockMs.Skip(10).ToList(), 0.5), Pct(_lockMs.Skip(10).ToList(), 0.95)));
            }
            Log(string.Format("   frame interval: {0:F1} ms median, {1:F1} p95, {2:F1} max;  {3} of {4} later than 1.5x the {5:F1} ms nominal",
                              Pct(intervals, 0.5), Pct(intervals, 0.95), intervals.Count == 0 ? 0 : intervals.Max(),
                              late, intervals.Count, _nominalIntervalMs));
            Log(string.Format("   on-screen marker check, {0} sampled frames: {1} mismatched on first read, {2} still mismatched 2 ms later (persistent tear)",
                              _sampled, _transient, _tornOrBad));
            foreach (string d in _badDetail) { Log("      " + d); }
            Log("");
            NextPhase();
        }

        private void Conclude()
        {
            // Composition: does a WPF element draw OVER the D3DImage?
            _phase = null;
            ChartNative.tmdq_chart_generate(_chart, 3, 360000);
            ChartNative.tmdq_chart_render(_chart, 0, 72000, 5, 35, 0xBEEF, 1, out ChartTiming ignored);
            _image.Lock(); _image.AddDirtyRect(new Int32Rect(0, 0, PlotW, PlotH)); _image.Unlock();
            Stage.UpdateLayout();

            DpiScale dpi = VisualTreeHelper.GetDpi(this);
            Point inLegend = Legend.TranslatePoint(new Point(Legend.ActualWidth - 5, Legend.ActualHeight / 2), Stage);   // right padding: not a corner, not text
            Color overlaid = PixelAt(Stage, inLegend, dpi);
            Color chartOnly = PixelAt(Chart, inLegend, dpi);
            Color markerTop = PixelAt(Chart, new Point(20, 5), dpi);
            Log("== composition: a WPF legend over the D3DImage");
            Log(string.Format("   pixel under the legend, chart alone: #{0:X2}{1:X2}{2:X2}   with the legend on top: #{3:X2}{4:X2}{5:X2}",
                              chartOnly.R, chartOnly.G, chartOnly.B, overlaid.R, overlaid.G, overlaid.B));
            Log("   (RenderTargetBitmap of a tree containing a D3DImage did not reproduce the composition, so it is not used as the judge)");
            Point sp = Stage.PointToScreen(inLegend);
            IntPtr dc = GetDC(IntPtr.Zero);
            uint scr = GetPixel(dc, (int)sp.X, (int)sp.Y);
            ReleaseDC(IntPtr.Zero, dc);
            uint sr = scr & 0xFF, sg = (scr >> 8) & 0xFF, sb = (scr >> 16) & 0xFF;
            Log(string.Format("   same point read from the composed desktop: #{0:X2}{1:X2}{2:X2}", sr, sg, sb));
            bool legendDrawn = sr < 90 && sg < 90 && sb < 90 && chartOnly.R > 200;
            Log("   legend is drawn over the live chart on the glass: " + (legendDrawn ? "YES" : "NO"));
            SaveScreenshot();
            Log(string.Format("   frame marker read back through WPF's own composition: R={0:X2} G={1:X2} B={2:X2} (expected EF BE A5)",
                              markerTop.R, markerTop.G, markerTop.B));

            // Resize: the target is recreated and the back buffer re-attached.
            bool resized = ChartNative.tmdq_chart_resize(_chart, 900, 500) == 0;
            if (resized)
            {
                IntPtr s2 = ChartNative.tmdq_chart_surface(_chart);
                _image.Lock(); _image.SetBackBuffer(D3DResourceType.IDirect3DSurface9, s2); _image.Unlock();
                ChartNative.tmdq_chart_render(_chart, 0, 72000, 5, 35, 1, 1, out ChartTiming t2);
                _image.Lock(); _image.AddDirtyRect(new Int32Rect(0, 0, 900, 500)); _image.Unlock();
            }
            Log("   resize to 900x500 and re-attach the back buffer: " + (resized ? "ok" : "FAILED " + ChartNative.Error(_chart)));
            Log("   front buffer available: " + _image.IsFrontBufferAvailable);
            Finish(0);
        }

        /// <summary>Captures the window as the desktop shows it (not as WPF would render it offscreen).</summary>
        private void SaveScreenshot()
        {
            Point o = PointToScreen(new Point(0, 0));
            int w = (int)Math.Ceiling(ActualWidth), h = (int)Math.Ceiling(ActualHeight);
            IntPtr screen = GetDC(IntPtr.Zero);
            IntPtr mem = CreateCompatibleDC(screen);
            IntPtr bmp = CreateCompatibleBitmap(screen, w, h);
            IntPtr old = SelectObject(mem, bmp);
            BitBlt(mem, 0, 0, w, h, screen, (int)o.X, (int)o.Y, 0x00CC0020 /* SRCCOPY */);
            SelectObject(mem, old);
            BitmapSource src = Imaging.CreateBitmapSourceFromHBitmap(bmp, IntPtr.Zero, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
            var enc = new PngBitmapEncoder();
            enc.Frames.Add(BitmapFrame.Create(src));
            string path = Path.Combine(AppContext.BaseDirectory, "screenshot.png");
            using (var f = File.Create(path)) { enc.Save(f); }
            DeleteObject(bmp); DeleteDC(mem); ReleaseDC(IntPtr.Zero, screen);
            Log("   screenshot of the window as composed: " + path);
        }

        private static Color PixelAt(System.Windows.Media.Visual v, Point p, DpiScale dpi)
        {
            var el = (FrameworkElement)v;
            int pw = (int)Math.Ceiling(el.ActualWidth * dpi.DpiScaleX), ph = (int)Math.Ceiling(el.ActualHeight * dpi.DpiScaleY);
            var rtb = new RenderTargetBitmap(pw, ph, 96 * dpi.DpiScaleX, 96 * dpi.DpiScaleY, PixelFormats.Pbgra32);
            rtb.Render(v);
            var px = new byte[4];
            rtb.CopyPixels(new Int32Rect((int)(p.X * dpi.DpiScaleX), (int)(p.Y * dpi.DpiScaleY), 1, 1), px, 4, 0);
            return Color.FromRgb(px[2], px[1], px[0]);
        }

        private void Finish(int code)
        {
            StopWorker();
            CompositionTarget.Rendering -= OnRendering;
            string path = Path.Combine(AppContext.BaseDirectory, "results.txt");
            File.WriteAllText(path, _report.ToString());
            Console.WriteLine("wrote " + path);
            if (_chart != IntPtr.Zero) { ChartNative.tmdq_chart_destroy(_chart); _chart = IntPtr.Zero; }
            Application.Current.Shutdown(code);
        }
    }
}
