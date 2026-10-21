using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media.Imaging;
using SkiaSharp;

namespace Tmdq.AvaloniaSpike
{
    /// <summary>
    /// Step 0.2 of docs/dotnet-migration/avalonia-plan.md: the chart drawn by Skia inside Avalonia, measured
    /// against MainWindow's Direct2D chart. Both draw the SAME geometry - tmdq_chart.dll builds it (culling
    /// and the per-pixel-column envelope) in both cases - so the one variable is the drawing backend.
    /// Geometry is built on the UI thread, as the native harness does; Skia draws on Avalonia's render
    /// thread. Writes results-skia.txt plus PNG / PDF / SVG exports next to the exe, then exits.
    ///
    ///     AvaloniaChartSpike.exe --skia [--serial]
    /// </summary>
    public partial class SkiaWindow : Window
    {
        private const int PlotW = 1200, PlotH = 700;

        private sealed class Phase
        {
            public string Name;
            public int Series, Samples, Frames;
            public bool WaitForGpu;
        }

        private static readonly Phase[] Phases =
        {
            new Phase { Name = "empty: the Skia path only",           Series = 0,  Samples = 1,       Frames = 400, WaitForGpu = true },
            new Phase { Name = "3 x 360k, waits for the GPU",          Series = 3,  Samples = 360000,  Frames = 400, WaitForGpu = true },
            new Phase { Name = "12 x 360k, waits for the GPU",         Series = 12, Samples = 360000,  Frames = 300, WaitForGpu = true },
            new Phase { Name = "3 x 1M, waits for the GPU",            Series = 3,  Samples = 1000000, Frames = 300, WaitForGpu = true },
            new Phase { Name = "3 x 360k, does NOT wait for the GPU",  Series = 3,  Samples = 360000,  Frames = 400, WaitForGpu = false },
            new Phase { Name = "12 x 360k, does NOT wait for the GPU", Series = 12, Samples = 360000,  Frames = 300, WaitForGpu = false },
        };

        /// <summary>A new frame is handed over on every tick, as a real app invalidates. --serial instead waits
        /// for the previous frame to be drawn first - kept as a diagnostic, because it is a trap: a tick that
        /// invalidates nothing lets Avalonia's render loop go idle, it wakes ~15 ms later, and the chart runs
        /// at ~33 fps even when it draws nothing at all. Measured; handing over every tick drew every frame
        /// built (none superseded) at the refresh rate.</summary>
        private readonly bool _pipelined = !Environment.GetCommandLineArgs().Contains("--serial");

        private readonly StringBuilder _report = new StringBuilder();
        private TopLevel _top;
        private IntPtr _chart;
        private string _backend = "?";
        private string _startupLine = "";

        // per-phase measurement state
        private Phase _phase;
        private int _phaseIndex = -1, _built, _busy, _dropped;
        private int _drawn;                       // frames the render thread drew (written there)
        private volatile bool _inFlight;          // a frame handed over and not yet drawn
        private readonly Stopwatch _inFlightClock = new Stopwatch();
        private readonly List<double> _geometryMs = new List<double>();
        private readonly List<double> _prepMs = new List<double>();
        private readonly ConcurrentQueue<RenderSample> _render = new ConcurrentQueue<RenderSample>();
        private readonly List<double> _intervalMs = new List<double>();
        private readonly ConcurrentQueue<double> _drawIntervalMs = new ConcurrentQueue<double>();   // render thread
        private readonly Stopwatch _drawClock = Stopwatch.StartNew();
        private double _lastDraw = -1;
        private TimeSpan _lastTime = TimeSpan.MinValue;
        private readonly Stopwatch _phaseClock = new Stopwatch();

        public SkiaWindow()
        {
            InitializeComponent();
            Opened += OnOpened;
        }

        private void Log(string line)
        {
            _report.AppendLine(line);
            Console.WriteLine(line);
        }

        private void OnOpened(object sender, EventArgs e)
        {
            _top = TopLevel.GetTopLevel(this);
            Log("avalonia: " + typeof(Window).Assembly.GetName().Version + ", skiasharp " +
                typeof(SKCanvas).Assembly.GetName().Version + ", runtime " + Environment.Version + ", render scaling " + RenderScaling);
            Log("frame hand-over: " + (_pipelined ? "a new frame every tick" : "--serial: the next frame waits until the last is drawn (diagnostic; starves the render loop)"));
            Log("");
            _chart = ChartNative.tmdq_chart_create_ex(PlotW, PlotH, ChartNative.GeometryOnly);
            Chart.Rendered += OnRendered;
            NextPhase();
            _top.RequestAnimationFrame(OnFrame);
        }

        // Render thread.
        private void OnRendered(RenderSample s, string backend)
        {
            _render.Enqueue(s);
            double t = _drawClock.Elapsed.TotalMilliseconds;
            if (_lastDraw >= 0) { _drawIntervalMs.Enqueue(t - _lastDraw); }
            _lastDraw = t;
            _backend = backend;
            if (_startupLine.Length == 0)
            {
                _startupLine = string.Format("process start to the first chart frame drawn: {0:F0} ms",
                    (DateTime.Now - Process.GetCurrentProcess().StartTime).TotalMilliseconds);
            }
            Interlocked.Increment(ref _drawn);
            _inFlight = false;
        }

        private void NextPhase()
        {
            _phaseIndex++;
            if (_phaseIndex >= Phases.Length) { Conclude(); return; }
            _phase = Phases[_phaseIndex];
            _built = 0; _busy = 0; _dropped = 0;
            Interlocked.Exchange(ref _drawn, 0);
            _geometryMs.Clear(); _prepMs.Clear(); _intervalMs.Clear();
            while (_render.TryDequeue(out _)) { }
            while (_drawIntervalMs.TryDequeue(out _)) { }
            _lastDraw = -1;
            _lastTime = TimeSpan.MinValue;
            ChartNative.tmdq_chart_generate(_chart, _phase.Series, _phase.Samples);
            Chart.WaitForGpu = _phase.WaitForGpu;
            Status.Text = "running: " + _phase.Name;
            _phaseClock.Restart();
        }

        private void View(int frame, out double xl, out double xu)
        {
            // A 20% window panning across the data, as MainWindow's ViewWindow does.
            int span = Math.Max(1, _phase.Samples / 5);
            int travel = Math.Max(1, _phase.Samples - span);
            xl = (frame * 997L) % travel;
            xu = xl + span;
        }

        private ChartFrame Build(double xl, double xu, uint marker, out double geometryMs, out double prepMs)
        {
            int n = ChartNative.tmdq_chart_build(_chart, xl, xu, 5, 35, out geometryMs);
            var sw = Stopwatch.StartNew();
            ChartFrame f = ChartFrame.FromNative(_chart, n, PlotW, PlotH, marker);
            prepMs = sw.Elapsed.TotalMilliseconds;
            return f;
        }

        private void OnFrame(TimeSpan now)
        {
            if (_phase == null) { return; }
            _top.RequestAnimationFrame(OnFrame);
            if (_lastTime != TimeSpan.MinValue) { _intervalMs.Add((now - _lastTime).TotalMilliseconds); }
            _lastTime = now;

            if (Volatile.Read(ref _drawn) >= _phase.Frames) { EndPhase(); return; }
            if (_inFlight && !_pipelined)
            {
                if (_inFlightClock.ElapsedMilliseconds < 1000) { _busy++; return; }
                _dropped++;   // never drawn: do not stall the phase on it
            }
            View(++_built, out double xl, out double xu);
            ChartFrame f = Build(xl, xu, (uint)(_built & 0xFFFF), out double g, out double p);
            _geometryMs.Add(g);
            _prepMs.Add(p);
            _inFlight = true;
            _inFlightClock.Restart();
            Chart.Show(f);
        }

        private static double Pct(IReadOnlyList<double> v, double p)
        {
            if (v.Count == 0) { return 0; }
            var s = v.OrderBy(x => x).ToList();
            return s[Math.Min(s.Count - 1, (int)(p * s.Count))];
        }

        private void EndPhase()
        {
            double seconds = _phaseClock.Elapsed.TotalSeconds;
            int drawn = Volatile.Read(ref _drawn);
            List<RenderSample> r = _render.ToList().Skip(10).ToList();
            List<double> issue = r.Select(x => x.IssueMs).ToList(), gpu = r.Select(x => x.GpuMs).ToList();
            List<double> geo = _geometryMs.Skip(10).ToList(), prep = _prepMs.Skip(10).ToList(), iv = _intervalMs.Skip(10).ToList();

            Log("== " + _phase.Name);
            Log(string.Format("   chart frames drawn: {0} in {1:F2} s = {2:F1} per second", drawn, seconds, drawn / seconds));
            Log(string.Format("   UI thread  - native geometry: {0:F1} ms median, {1:F1} p95;  copy into Skia types: {2:F1} ms median, {3:F1} p95",
                              Pct(geo, 0.5), Pct(geo, 0.95), Pct(prep, 0.5), Pct(prep, 0.95)));
            Log(string.Format("   render thread - Skia draw calls: {0:F1} ms median, {1:F1} p95, {2:F1} max;  GPU wait: {3:F1} ms median, {4:F1} p95",
                              Pct(issue, 0.5), Pct(issue, 0.95), issue.Count == 0 ? 0 : issue.Max(), Pct(gpu, 0.5), Pct(gpu, 0.95)));
            List<double> di = _drawIntervalMs.ToList().Skip(10).ToList();
            Log(string.Format("   UI tick interval: {0:F1} ms median, {1:F1} p95;  draw interval (render thread): {2:F1} ms median, {3:F1} p95",
                              Pct(iv, 0.5), Pct(iv, 0.95), Pct(di, 0.5), Pct(di, 0.95)));
            Log(string.Format("   frames built: {0};  ticks skipped while a frame was in flight: {1}, dropped: {2}{3}",
                              _built, _busy, _dropped, _pipelined ? "" : "  (--serial)"));
            Log("");
            NextPhase();
        }

        private void Conclude()
        {
            _phase = null;
            Log("Skia backend on screen: " + _backend);
            Log("");
            string dir = AppContext.BaseDirectory;

            // The software path: what headless tests and a machine with no usable GPU get.
            Log("== CPU raster (no GPU), the check view 0..72000");
            foreach (int series in new[] { 3, 12 })
            {
                ChartNative.tmdq_chart_generate(_chart, series, 360000);
                using ChartFrame f = Build(0, 72000, 0xBEEF, out double g, out _);
                using var surface = SKSurface.Create(new SKImageInfo(PlotW, PlotH, SKColorType.Bgra8888, SKAlphaType.Premul));
                var times = new List<double>();
                for (int i = 0; i < 25; ++i)
                {
                    var sw = Stopwatch.StartNew();
                    ChartPainter.Draw(surface.Canvas, f);
                    surface.Canvas.Flush();
                    times.Add(sw.Elapsed.TotalMilliseconds);
                }
                Log(string.Format("   {0} x 360k: {1:F1} ms median, {2:F1} p95 to draw (geometry {3:F1} ms)",
                                  series, Pct(times.Skip(3).ToList(), 0.5), Pct(times.Skip(3).ToList(), 0.95), g));
            }
            Log("");

            // Export: PNG, PDF and SVG from the one drawing routine.
            Log("== export of the check view, from the same drawing code");
            foreach (int series in new[] { 3, 12 })
            {
                ChartNative.tmdq_chart_generate(_chart, series, 360000);
                using ChartFrame f = Build(0, 72000, 0xBEEF, out _, out _);
                string stem = Path.Combine(dir, "skia-" + series + "x360k");

                var sw = Stopwatch.StartNew();
                using (var surface = SKSurface.Create(new SKImageInfo(PlotW, PlotH, SKColorType.Bgra8888, SKAlphaType.Premul)))
                {
                    ChartPainter.Draw(surface.Canvas, f);
                    using SKImage img = surface.Snapshot();
                    using SKData data = img.Encode(SKEncodedImageFormat.Png, 100);
                    using FileStream fs = File.Create(stem + ".png");
                    data.SaveTo(fs);
                }
                double pngMs = sw.Elapsed.TotalMilliseconds;

                sw.Restart();
                using (FileStream fs = File.Create(stem + ".pdf"))
                using (SKDocument doc = SKDocument.CreatePdf(fs))
                {
                    SKCanvas page = doc.BeginPage(PlotW, PlotH);
                    ChartPainter.Draw(page, f);
                    doc.EndPage();
                    doc.Close();
                }
                double pdfMs = sw.Elapsed.TotalMilliseconds;

                sw.Restart();
                using (FileStream fs = File.Create(stem + ".svg"))
                using (SKCanvas svg = SKSvgCanvas.Create(new SKRect(0, 0, PlotW, PlotH), fs))
                {
                    ChartPainter.Draw(svg, f);
                }
                double svgMs = sw.Elapsed.TotalMilliseconds;

                Log(string.Format("   {0} x 360k:  PNG {1,6:F1} ms {2,8:N0} bytes   PDF {3,6:F1} ms {4,9:N0} bytes   SVG {5,6:F1} ms {6,10:N0} bytes",
                                  series, pngMs, new FileInfo(stem + ".png").Length, pdfMs, new FileInfo(stem + ".pdf").Length,
                                  svgMs, new FileInfo(stem + ".svg").Length));
            }
            Log("");

            // The window as Avalonia renders it offscreen: the chart's draw operation must work there too.
            var rtb = new RenderTargetBitmap(new PixelSize((int)Bounds.Width, (int)Bounds.Height));
            ChartNative.tmdq_chart_generate(_chart, 3, 360000);
            Chart.Show(Build(0, 72000, 0xBEEF, out _, out _));
            rtb.Render(this);
            rtb.Save(Path.Combine(dir, "skia-window.png"), new PngBitmapEncoderOptions());
            Log("== offscreen render of the whole window (RenderTargetBitmap): skia-window.png");
            Log("");

            Log("== startup and memory");
            Log("   " + _startupLine);
            var me = Process.GetCurrentProcess();
            me.Refresh();
            Log(string.Format("   working set {0:F0} MB, private bytes {1:F0} MB, managed heap {2:F0} MB",
                              me.WorkingSet64 / 1048576.0, me.PrivateMemorySize64 / 1048576.0, GC.GetTotalMemory(false) / 1048576.0));

            File.WriteAllText(Path.Combine(dir, "results-skia.txt"), _report.ToString());
            if (_chart != IntPtr.Zero) { ChartNative.tmdq_chart_destroy(_chart); _chart = IntPtr.Zero; }
            (Avalonia.Application.Current?.ApplicationLifetime as IClassicDesktopStyleApplicationLifetime)?.Shutdown(0);
        }
    }
}
