using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Tmdq.Spike;

namespace Tmdq.WpfSpike
{
    /// <summary>
    /// Runs a scripted benchmark of the native chart shown through D3DImage, then writes
    /// results.txt next to the exe and exits. Pass --interactive to keep the window open instead
    /// (the legend and crosshair then react to the mouse), --no-sample to measure pacing with no
    /// screen capture running.
    ///
    /// Its twin is ..\avalonia: the same phases, the same pan, the same checks, the same ScreenProbe.
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

        private readonly D3DImage _image = new D3DImage();
        private IntPtr _chart;
        private readonly StringBuilder _report = new StringBuilder();
        private readonly bool _interactive = Environment.GetCommandLineArgs().Contains("--interactive");
        private readonly bool _sample = !Environment.GetCommandLineArgs().Contains("--no-sample");
        private ScreenProbe _probe;
        private bool _firstFrameLogged;
        private string _startupLine = "";

        // per-phase measurement state
        private Phase _phase;
        private int _phaseIndex = -1, _frame;
        private readonly List<double> _renderMs = new List<double>();
        private readonly List<ChartTiming> _split = new List<ChartTiming>();   ///< the parts of _renderMs
        private readonly List<double> _lockMs = new List<double>();
        private readonly List<double> _intervalMs = new List<double>();
        private TimeSpan _lastRenderingTime = TimeSpan.MinValue;
        private int _torn, _inspected;
        private readonly HashSet<uint> _seen = new HashSet<uint>();
        private readonly List<string> _badDetail = new List<string>();
        private double _nominalIntervalMs = 16.7;
        private Thread _worker;
        private volatile bool _workerStop;
        private long _workerFrame, _shownWorkerFrame;
        private readonly List<double> _workerMs = new List<double>();
        private readonly Stopwatch _phaseClock = new Stopwatch();

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
            Topmost = true;   // the screen check reads what is actually composed
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

            Log("wpf: " + typeof(Window).Assembly.GetName().Version + ", runtime " + Environment.Version +
                ", DPI scale " + VisualTreeHelper.GetDpi(this).DpiScaleX);
            Log("render tier: " + (RenderCapability.Tier >> 16) + "  (2 = full hardware acceleration)");
            Log("adapter: " + ChartNative.Adapter(_chart) + (ChartNative.tmdq_chart_is_hardware(_chart) == 1 ? "  (hardware)" : "  (WARP software)"));
            if (_sample)
            {
                Point o = Chart.PointToScreen(new Point(20, 5));
                _probe = ScreenProbe.Open((int)o.X, (int)o.Y, out string probeError);
                Log("screen probe: " + (_probe != null ? "DXGI Desktop Duplication - " + _probe.Describe : "UNAVAILABLE - " + probeError));
            }
            else
            {
                Log("screen probe: off (--no-sample), so frame pacing carries no capture cost");
            }
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
            _renderMs.Clear(); _split.Clear(); _lockMs.Clear(); _intervalMs.Clear(); _workerMs.Clear();
            _torn = 0; _inspected = 0; _seen.Clear(); _badDetail.Clear();
            _lastRenderingTime = TimeSpan.MinValue;
            ChartNative.tmdq_chart_generate(_chart, p.Series, p.Samples);
            _workerFrame = 0; _shownWorkerFrame = 0;
            _phaseClock.Restart();
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

        private void NoteFirstFrame()
        {
            if (_firstFrameLogged) { return; }
            _firstFrameLogged = true;
            _startupLine = string.Format("process start to the first chart frame handed to WPF: {0:F0} ms",
                (DateTime.Now - Process.GetCurrentProcess().StartTime).TotalMilliseconds);
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
            var sw = Stopwatch.StartNew();

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
                ViewWindow(_frame, out double xl, out double xu);
                _image.Lock();
                ChartTiming t;
                ChartNative.tmdq_chart_render(_chart, xl, xu, 5, 35, (uint)(_frame & 0xFFFF), _phase.Fence ? 1 : 0, out t);
                _image.AddDirtyRect(new Int32Rect(0, 0, PlotW, PlotH));
                _image.Unlock();
                _renderMs.Add(t.GeometryMs + t.RasterMs + t.GpuWaitMs);
                _split.Add(t);
                _lockMs.Add(sw.Elapsed.TotalMilliseconds);
            }
            NoteFirstFrame();

            if (_interactive) { Status.Text = "interactive - frame " + _frame; return; }
            if (_frame >= _phase.Frames) { EndPhase(); }
        }

        // ---- checks -------------------------------------------------------------------------

        /// <summary>
        /// Reads the top and bottom marker strips of the chart from ONE desktop frame, if the desktop
        /// has shown a new frame since the last look. They must name the same chart frame; if they do
        /// not, that desktop frame showed the top of one chart frame and the bottom of another - a tear.
        /// </summary>
        private void SampleOnScreen()
        {
            if (_probe == null) { return; }
            Point top = Chart.PointToScreen(new Point(20, 5));
            Point bot = Chart.PointToScreen(new Point(20, PlotH - 5));
            var rgb = new uint[2];
            if (_probe.Pixels(new[] { (int)top.X, (int)top.Y, (int)bot.X, (int)bot.Y }, rgb, 0) != 1) { return; }
            _inspected++;
            _seen.Add(rgb[0] & 0xFFFFFF);
            if (ScreenProbe.SameFrame(rgb[0], rgb[1])) { return; }
            _torn++;
            if (_badDetail.Count < 6)
            {
                _badDetail.Add(string.Format("frame {0}: top {1}, bottom {2}", _frame, ScreenProbe.Hex(rgb[0]), ScreenProbe.Hex(rgb[1])));
            }
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
            double seconds = _phaseClock.Elapsed.TotalSeconds;
            List<double> render = _renderMs.Skip(10).ToList();
            List<double> intervals = _intervalMs.Skip(10).ToList();
            if (_phaseIndex == 0 && intervals.Count > 0) { _nominalIntervalMs = Pct(intervals, 0.5); }
            int late = intervals.Count(x => x > _nominalIntervalMs * 1.5);
            int shown = _phase.Worker ? _lockMs.Count : _frame;

            Log("== " + _phase.Name);
            Log(string.Format("   chart frames presented: {0} in {1:F2} s = {2:F1} per second", shown, seconds, shown / seconds));
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
                List<ChartTiming> parts = _split.Skip(10).ToList();
                Log(string.Format("     of which CPU geometry {0:F1}, Direct2D issue {1:F1}, GPU fence wait {2:F1} ms (medians)",
                                  Pct(parts.Select(x => x.GeometryMs).ToList(), 0.5), Pct(parts.Select(x => x.RasterMs).ToList(), 0.5),
                                  Pct(parts.Select(x => x.GpuWaitMs).ToList(), 0.5)));
                Log(string.Format("   incl. Lock/AddDirtyRect/Unlock:           {0:F1} ms median, {1:F1} p95",
                                  Pct(_lockMs.Skip(10).ToList(), 0.5), Pct(_lockMs.Skip(10).ToList(), 0.95)));
            }
            Log(string.Format("   frame interval: {0:F1} ms median, {1:F1} p95, {2:F1} max;  {3} of {4} later than 1.5x the {5:F1} ms nominal",
                              Pct(intervals, 0.5), Pct(intervals, 0.95), intervals.Count == 0 ? 0 : intervals.Max(),
                              late, intervals.Count, _nominalIntervalMs));
            Log(_probe == null
                ? "   on-screen marker check: not sampled"
                : string.Format("   on-screen marker check: {0} desktop frames inspected, {1} showed a tear (top and bottom from different chart frames); {2} distinct marker colours seen{3}",
                                _inspected, _torn, _seen.Count,
                                _seen.Count * 2 < Math.Min(_inspected, shown) ? " - TOO FEW: the probe is not reading the markers, so this check proves nothing" : ""));
            foreach (string d in _badDetail) { Log("      " + d); }
            Log("");
            NextPhase();
        }

        private static int R(uint c) => (int)((c >> 16) & 0xFF);
        private static int G(uint c) => (int)((c >> 8) & 0xFF);
        private static int B(uint c) => (int)(c & 0xFF);
        private static (int X, int Y) Glass(Visual relativeTo, Point p)
        {
            Point s = relativeTo.PointToScreen(p);
            return ((int)s.X, (int)s.Y);
        }

        /// <summary>Presents the check frame again, so the desktop composes a frame showing the window as it is now.</summary>
        private Task RepaintCheckFrame()
        {
            _image.Lock();
            ChartNative.tmdq_chart_render(_chart, 0, 72000, 5, 35, 0xBEEF, 1, out ChartTiming _);
            _image.AddDirtyRect(new Int32Rect(0, 0, PlotW, PlotH));
            _image.Unlock();
            return Task.Delay(1);
        }

        private async Task<uint[]> ReadGlass(params (int X, int Y)[] points)
        {
            if (_probe != null) { return await _probe.ReadStill(RepaintCheckFrame, points); }
            var none = new uint[points.Length];
            Array.Fill(none, 0xFFFFFFFFu);
            return none;
        }

        private async void Conclude()
        {
            _phase = null;
            StopWorker();
            // The phase probe stays open: the checks below read the still window through it (ReadStill).
            ChartNative.tmdq_chart_generate(_chart, 3, 360000);
            await RepaintCheckFrame();
            await Task.Delay(300);   // let the frame reach the glass

            Log("== composition: a WPF legend over the D3DImage");
            Log("   screen probe, as used: " + (_probe?.Describe ?? "none"));
            Point inLegend = Legend.TranslatePoint(new Point(Legend.ActualWidth - 5, Legend.ActualHeight / 2), Stage);   // right padding: not a corner, not text
            uint overlaid = (await ReadGlass(Glass(Stage, inLegend)))[0];
            Legend.Visibility = Visibility.Hidden;
            await Task.Delay(300);
            uint chartOnly = (await ReadGlass(Glass(Stage, inLegend)))[0];
            Legend.Visibility = Visibility.Visible;
            await Task.Delay(300);
            Log("   desktop pixel under the legend, chart alone: " + ScreenProbe.Hex(chartOnly) + "   with the legend on top: " + ScreenProbe.Hex(overlaid));
            bool legendDrawn = R(overlaid) < 90 && G(overlaid) < 90 && B(overlaid) < 90 && R(chartOnly) > 200;
            Log("   legend is drawn over the live chart on the glass: " + (legendDrawn ? "YES" : "NO"));

            uint[] px = await ReadGlass(Glass(Chart, new Point(PlotW - 20, 5)), Glass(Chart, new Point(PlotW - 20, PlotH - 5)), Glass(Chart, new Point(20, 5)));
            uint topRight = px[0], bottomRight = px[1], marker = px[2];
            bool green(uint c) => c != 0xFFFFFFFF && R(c) < 80 && G(c) > 180 && B(c) < 80;
            Log("   orientation block (green, top right only): top right " + ScreenProbe.Hex(topRight) + ", bottom right " + ScreenProbe.Hex(bottomRight) +
                " - image is the right way up: " + (green(topRight) && !green(bottomRight) ? "YES" : "NO"));
            Log(string.Format("   frame marker read back from the desktop: R={0:X2} G={1:X2} B={2:X2} (written EF BE A5)",
                              R(marker), G(marker), B(marker)));
            await SaveScreenshot();

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

            Log("");
            Log("== startup and memory");
            Log("   " + _startupLine);
            var me = Process.GetCurrentProcess();
            me.Refresh();
            Log(string.Format("   working set {0:F0} MB, private bytes {1:F0} MB, managed heap {2:F0} MB",
                              me.WorkingSet64 / 1048576.0, me.PrivateMemorySize64 / 1048576.0, GC.GetTotalMemory(false) / 1048576.0));
            Finish(0);
        }

        /// <summary>Captures the window as the desktop shows it, through Desktop Duplication.</summary>
        private async Task SaveScreenshot()
        {
            DpiScale dpi = VisualTreeHelper.GetDpi(this);
            var client = (FrameworkElement)Content;   // the client area; the Window's own size includes the frame
            Point o = client.PointToScreen(new Point(0, 0));
            int w = (int)Math.Ceiling(client.ActualWidth * dpi.DpiScaleX), h = (int)Math.Ceiling(client.ActualHeight * dpi.DpiScaleY);
            byte[] pixels = _probe == null ? null : await _probe.ReadStillRegion(RepaintCheckFrame, (int)o.X, (int)o.Y, w, h);
            if (pixels == null) { Log("   screenshot FAILED: " + (_probe?.Error ?? "no screen probe")); return; }
            BitmapSource src = BitmapSource.Create(w, h, 96, 96, PixelFormats.Bgr32, null, pixels, w * 4);
            var enc = new PngBitmapEncoder();
            enc.Frames.Add(BitmapFrame.Create(src));
            string path = Path.Combine(AppContext.BaseDirectory, "screenshot.png");
            using (var f = File.Create(path)) { enc.Save(f); }
            Log("   screenshot of the window as composed: " + path);
        }

        private void Finish(int code)
        {
            StopWorker();
            _probe?.Dispose();
            _probe = null;
            CompositionTarget.Rendering -= OnRendering;
            string path = Path.Combine(AppContext.BaseDirectory, "results.txt");
            File.WriteAllText(path, _report.ToString());
            Console.WriteLine("wrote " + path);
            if (_chart != IntPtr.Zero) { ChartNative.tmdq_chart_destroy(_chart); _chart = IntPtr.Zero; }
            Application.Current.Shutdown(code);
        }
    }
}
