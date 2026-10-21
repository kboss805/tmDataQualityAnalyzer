using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Threading;
using Tmdq.Spike;

namespace Tmdq.AvaloniaSpike
{
    /// <summary>
    /// The Avalonia twin of the WPF spike's MainWindow: the same phases, the same pan, the same
    /// on-screen checks, so the two results.txt files can be read side by side. Writes results.txt
    /// next to the exe and exits.
    ///
    /// The one structural difference: WPF's D3DImage copies the surface when it is unlocked, so the
    /// renderer may draw the next frame straight away. Avalonia's UpdateWithKeyedMutexAsync hands the
    /// texture to the compositor until it has read it (key 1 -> key 0) - about one composition later. With
    /// ONE texture that halves the frame rate, so the host rotates --buffers N (default 2) imported
    /// textures and renders into whichever the compositor has released; a frame that finds none free is
    /// skipped and counted. --buffers 1 reproduces the single-texture result.
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
            new Phase { Name = "empty: GPU-interop path only",            Series = 0,  Samples = 1,       Frames = 400, Fence = true },
            new Phase { Name = "3 x 360k, UI thread, fenced",             Series = 3,  Samples = 360000,  Frames = 400, Fence = true },
            new Phase { Name = "12 x 360k, UI thread, fenced",            Series = 12, Samples = 360000,  Frames = 300, Fence = true },
            new Phase { Name = "3 x 1M, UI thread, fenced",               Series = 3,  Samples = 1000000, Frames = 300, Fence = true },
            new Phase { Name = "3 x 360k, UI thread, NOT fenced",         Series = 3,  Samples = 360000,  Frames = 400, Fence = false },
            new Phase { Name = "3 x 360k, worker thread, keyed mutex",    Series = 3,  Samples = 360000,  Frames = 400, Fence = true, Worker = true },
        };

        private readonly StringBuilder _report = new StringBuilder();
        private readonly bool _topLeftOrigin = !Environment.GetCommandLineArgs().Contains("--bottom-left");
        private readonly bool _sample = !Environment.GetCommandLineArgs().Contains("--no-sample");
        private readonly int _buffers = ArgInt("--buffers", 2);
        private ScreenProbe _probe;
        private TopLevel _top;
        private IntPtr _chart;
        private ICompositionGpuInterop _interop;
        private ICompositionImportedGpuImage[] _images;
        private Task[] _bufPending;   ///< per buffer: the update that still holds it, if any
        private int _next;            ///< the buffer the next frame renders into
        private Task _pending;        ///< the most recent update
        private bool _firstFrameLogged;
        private string _startupLine = "";

        // per-phase measurement state
        private Phase _phase;
        private int _phaseIndex = -1, _frame, _busy;
        private readonly List<double> _renderMs = new List<double>();
        private readonly List<ChartTiming> _split = new List<ChartTiming>();   ///< the parts of _renderMs
        private readonly List<double> _uiMs = new List<double>();
        private readonly List<double> _turnaroundMs = new List<double>();
        private readonly List<double> _intervalMs = new List<double>();
        private TimeSpan _lastTime = TimeSpan.MinValue;
        private int _torn, _inspected;
        private readonly HashSet<uint> _seen = new HashSet<uint>();
        private readonly List<string> _badDetail = new List<string>();
        private double _nominalIntervalMs = 16.7;
        private Thread _worker;
        private volatile bool _workerStop;
        private int _presented;
        private readonly Stopwatch _phaseClock = new Stopwatch();
        private readonly List<double> _workerMs = new List<double>();

        public MainWindow()
        {
            InitializeComponent();
            Opened += OnOpened;
            PointerMoved += OnPointerMoved;
            PointerExited += (s, a) => { Crosshair.IsVisible = false; Readout.IsVisible = false; };
        }

        private static int ArgInt(string name, int fallback)
        {
            string[] a = Environment.GetCommandLineArgs();
            int i = Array.IndexOf(a, name);
            return i >= 0 && i + 1 < a.Length && int.TryParse(a[i + 1], out int v) ? v : fallback;
        }

        private void Log(string line)
        {
            _report.AppendLine(line);
            Console.WriteLine(line);
        }

        private async void OnOpened(object sender, EventArgs e)
        {
            try
            {
                Topmost = true;   // the screen-pixel tear check reads what is actually composed
                _top = TopLevel.GetTopLevel(this);

                _interop = await Chart.Compositor.TryGetCompositionGpuInterop();
                if (_interop == null)
                {
                    Log("NO GPU INTEROP on this rendering backend");
                    Finish(2);
                    return;
                }
                const string handleType = KnownPlatformGraphicsExternalImageHandleTypes.D3D11TextureGlobalSharedHandle;
                Log("avalonia: " + typeof(Window).Assembly.GetName().Version + ", runtime " + Environment.Version +
                    ", render scaling " + RenderScaling);
                Log("gpu interop image handle types: " + string.Join(", ", _interop.SupportedImageHandleTypes));
                Log("synchronisation offered for a D3D11 shared handle: " + _interop.GetSynchronizationCapabilities(handleType));
                if (!_interop.SupportedImageHandleTypes.Contains(handleType))
                {
                    Log("this backend cannot import a D3D11 shared texture");
                    Finish(2);
                    return;
                }

                _chart = ChartNative.tmdq_chart_create_ex(PlotW, PlotH, ChartNative.KeyedMutex);
                IntPtr handle = _chart == IntPtr.Zero ? IntPtr.Zero : ChartNative.tmdq_chart_shared_handle(_chart);
                if (handle != IntPtr.Zero && _buffers > 1 && ChartNative.tmdq_chart_set_buffers(_chart, _buffers) != 0) { handle = IntPtr.Zero; }
                if (handle == IntPtr.Zero)
                {
                    Log("CREATE FAILED: " + (_chart == IntPtr.Zero ? "out of memory" : ChartNative.Error(_chart)));
                    Finish(2);
                    return;
                }
                Log("adapter: " + ChartNative.Adapter(_chart) + (ChartNative.tmdq_chart_is_hardware(_chart) == 1 ? "  (hardware)" : "  (WARP software)"));
                Log("image origin passed to the import: " + (_topLeftOrigin ? "top-left" : "bottom-left"));
                Log("shared textures rotated: " + _buffers);
                Log("");

                await ImportAll(PlotW, PlotH);

                if (_sample)
                {
                    PixelPoint o = Chart.PointToScreen(new Point(20, 5));
                    _probe = ScreenProbe.Open(o.X, o.Y, out string probeError);
                    Log("screen probe: " + (_probe != null ? "DXGI Desktop Duplication - " + _probe.Describe : "UNAVAILABLE - " + probeError));
                }
                else
                {
                    Log("screen probe: off (--no-sample), so frame pacing carries no capture cost");
                }
                Log("");

                NextPhase();
                _top.RequestAnimationFrame(OnFrame);
            }
            catch (Exception ex)
            {
                Log("FAILED: " + ex);
                Finish(3);
            }
        }

        private async Task ImportAll(int w, int h)
        {
            _images = new ICompositionImportedGpuImage[_buffers];
            _bufPending = new Task[_buffers];
            _next = 0;
            for (int i = 0; i < _buffers; ++i)
            {
                _images[i] = Import(ChartNative.tmdq_chart_shared_handle_at(_chart, i), w, h);
                await _images[i].ImportCompleted;
            }
        }

        private ICompositionImportedGpuImage Import(IntPtr handle, int w, int h) =>
            _interop.ImportImage(
                new PlatformHandle(handle, KnownPlatformGraphicsExternalImageHandleTypes.D3D11TextureGlobalSharedHandle),
                new PlatformGraphicsExternalImageProperties
                {
                    Width = w, Height = h,
                    Format = PlatformGraphicsExternalImageFormat.B8G8R8A8UNorm,
                    TopLeftOrigin = _topLeftOrigin,
                });

        private void OnPointerMoved(object sender, PointerEventArgs e)
        {
            Point p = e.GetPosition(Stage);
            Crosshair.StartPoint = new Point(p.X, 0);
            Crosshair.EndPoint = new Point(p.X, PlotH);
            Crosshair.IsVisible = true;
            Readout.Text = string.Format("x {0:F0}  frame {1}", p.X, _frame);
            Readout.Margin = new Thickness(p.X + 8, p.Y + 8, 0, 0);
            Readout.IsVisible = true;
        }

        // ---- phases -------------------------------------------------------------------------

        private void NextPhase()
        {
            _phaseIndex++;
            if (_phaseIndex >= Phases.Length) { _ = Conclude(); return; }
            StartPhase(Phases[_phaseIndex]);
        }

        private void StartPhase(Phase p)
        {
            StopWorker();
            _phase = p;
            _frame = 0; _busy = 0; _presented = 0;
            _renderMs.Clear(); _split.Clear(); _uiMs.Clear(); _intervalMs.Clear(); _workerMs.Clear();
            lock (_turnaroundMs) { _turnaroundMs.Clear(); }
            _torn = 0; _inspected = 0; _seen.Clear(); _badDetail.Clear();
            _lastTime = TimeSpan.MinValue;
            ChartNative.tmdq_chart_generate(_chart, p.Series, p.Samples);
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

        /// <summary>Starts an update and returns a task that completes after recording how long the
        /// compositor held the texture (call to completion).</summary>
        private Task Present(int buffer)
        {
            var sw = Stopwatch.StartNew();
            Task update = Chart.Surface.UpdateWithKeyedMutexAsync(_images[buffer], 1, 0);
            return update.ContinueWith(_ =>
            {
                lock (_turnaroundMs) { _turnaroundMs.Add(sw.Elapsed.TotalMilliseconds); }
                if (!_firstFrameLogged)
                {
                    _firstFrameLogged = true;
                    _startupLine = string.Format("process start to the first chart frame handed over by the compositor: {0:F0} ms",
                        (DateTime.Now - Process.GetCurrentProcess().StartTime).TotalMilliseconds);
                }
            }, TaskScheduler.Default);
        }

        private void StartWorker()
        {
            _workerStop = false;
            IntPtr chart = _chart;
            Phase ph = _phase;
            int buffers = _buffers;
            var ready = new SemaphoreSlim(buffers);   // buffers the compositor has released
            _worker = new Thread(() =>
            {
                long n = 0;
                while (!_workerStop)
                {
                    if (!ready.Wait(100)) { continue; }
                    if (_workerStop) { break; }
                    n++;
                    int span = Math.Max(1, ph.Samples / 5);
                    int travel = Math.Max(1, ph.Samples - span);
                    double xl = (n * 997L) % travel, xu = xl + span;
                    int buf = (int)(n % buffers);   // released in the order presented; render() waits if one is late
                    ChartTiming t;
                    ChartNative.tmdq_chart_select(chart, buf);
                    ChartNative.tmdq_chart_render(chart, xl, xu, 5, 35, (uint)(n & 0xFFFF), 1, out t);
                    lock (_workerMs) { _workerMs.Add(t.GeometryMs + t.RasterMs + t.GpuWaitMs); }
                    // The compositor objects belong to the UI thread: the hand-over is posted there, and
                    // the worker renders again once the compositor has released the texture.
                    Dispatcher.UIThread.Post(() =>
                    {
                        var sw = Stopwatch.StartNew();
                        Task done = Present(buf);
                        _pending = _bufPending[buf] = done;
                        _uiMs.Add(sw.Elapsed.TotalMilliseconds);
                        _presented++;
                        done.ContinueWith(_ => ready.Release(), TaskScheduler.Default);
                    });
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
            // A hand-over the worker posted may still be queued here; run it now, so its update is
            // the one the next frame waits for (otherwise the next render waits for a key the
            // compositor is still owed, and both sides stall).
            Dispatcher.UIThread.RunJobs();
        }

        private void OnFrame(TimeSpan now)
        {
            if (_phase == null) { return; }
            _top.RequestAnimationFrame(OnFrame);   // keep the frame clock running

            if (_lastTime != TimeSpan.MinValue) { _intervalMs.Add((now - _lastTime).TotalMilliseconds); }
            _lastTime = now;

            // What is on the glass NOW is the previous frame's result: check it before replacing it.
            if (_frame > 3) { SampleOnScreen(); }

            if (_phase.Worker)
            {
                // The worker drives the updates; this clock only measures and samples.
                _frame++;
            }
            else
            {
                int buf = _next;
                if (_bufPending[buf] != null && !_bufPending[buf].IsCompleted)
                {
                    _busy++;      // the compositor still holds the texture this frame would draw into
                    return;
                }
                _frame++;
                var sw = Stopwatch.StartNew();
                ViewWindow(_frame, out double xl, out double xu);
                ChartTiming t;
                ChartNative.tmdq_chart_select(_chart, buf);
                ChartNative.tmdq_chart_render(_chart, xl, xu, 5, 35, (uint)(_frame & 0xFFFF), _phase.Fence ? 1 : 0, out t);
                _pending = _bufPending[buf] = Present(buf);
                _next = (buf + 1) % _buffers;
                _renderMs.Add(t.GeometryMs + t.RasterMs + t.GpuWaitMs);
                _split.Add(t);
                _uiMs.Add(sw.Elapsed.TotalMilliseconds);
            }

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
            PixelPoint top = Chart.PointToScreen(new Point(20, 5));
            PixelPoint bot = Chart.PointToScreen(new Point(20, PlotH - 5));
            var rgb = new uint[2];
            if (_probe.Pixels(new[] { top.X, top.Y, bot.X, bot.Y }, rgb, 0) != 1) { return; }
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
            List<double> render = _renderMs.Skip(10).ToList();
            List<double> intervals = _intervalMs.Skip(10).ToList();
            List<double> turnaround;
            lock (_turnaroundMs) { turnaround = _turnaroundMs.Skip(10).ToList(); }
            if (_phaseIndex == 0 && intervals.Count > 0) { _nominalIntervalMs = Pct(intervals, 0.5); }
            int late = intervals.Count(x => x > _nominalIntervalMs * 1.5);

            double seconds = _phaseClock.Elapsed.TotalSeconds;
            int shown = _phase.Worker ? _presented : _frame;
            Log("== " + _phase.Name);
            Log(string.Format("   chart frames presented: {0} in {1:F2} s = {2:F1} per second", shown, seconds, shown / seconds));
            if (_phase.Worker)
            {
                List<double> w;
                lock (_workerMs) { w = _workerMs.Skip(10).ToList(); }
                Log(string.Format("   worker rendered {0} frames, {1:F1} ms median / {2:F1} p95 (geometry + raster + GPU), shown by UI {3} times",
                                  _workerMs.Count, Pct(w, 0.5), Pct(w, 0.95), _presented));
                Log(string.Format("   UI thread UpdateWithKeyedMutexAsync call: {0:F3} ms median, {1:F3} max",
                                  Pct(_uiMs, 0.5), _uiMs.Count == 0 ? 0 : _uiMs.Max()));
            }
            else
            {
                Log(string.Format("   native render (geometry+raster+GPU wait): {0:F1} ms median, {1:F1} p95, {2:F1} max",
                                  Pct(render, 0.5), Pct(render, 0.95), render.Count == 0 ? 0 : render.Max()));
                List<ChartTiming> parts = _split.Skip(10).ToList();
                Log(string.Format("     of which CPU geometry {0:F1}, Direct2D issue {1:F1}, GPU fence wait {2:F1} ms (medians)",
                                  Pct(parts.Select(x => x.GeometryMs).ToList(), 0.5), Pct(parts.Select(x => x.RasterMs).ToList(), 0.5),
                                  Pct(parts.Select(x => x.GpuWaitMs).ToList(), 0.5)));
                Log(string.Format("   incl. UpdateWithKeyedMutexAsync call:     {0:F1} ms median, {1:F1} p95",
                                  Pct(_uiMs.Skip(10).ToList(), 0.5), Pct(_uiMs.Skip(10).ToList(), 0.95)));
                Log(string.Format("   frames skipped because the compositor still held the texture: {0}", _busy));
            }
            Log(string.Format("   compositor turnaround (update call to texture released): {0:F1} ms median, {1:F1} p95",
                              Pct(turnaround, 0.5), Pct(turnaround, 0.95)));
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

        private static (int X, int Y) Glass(PixelPoint p) => (p.X, p.Y);

        /// <summary>Presents the check frame again, so the desktop composes a frame showing the window as it is now.</summary>
        private async Task RepaintCheckFrame()
        {
            int buf = _next;
            if (_bufPending[buf] != null) { await Within(_bufPending[buf], 3000, "the buffer's previous update"); }
            ChartNative.tmdq_chart_select(_chart, buf);
            ChartNative.tmdq_chart_render(_chart, 0, 72000, 5, 35, 0xBEEF, 1, out ChartTiming _);
            _pending = _bufPending[buf] = Present(buf);
            _next = (buf + 1) % _buffers;
            await Within(_pending, 3000, "repainting the check frame");
        }

        private async Task<uint[]> ReadGlass(params (int X, int Y)[] points)
        {
            if (_probe != null) { return await _probe.ReadStill(RepaintCheckFrame, points); }
            var none = new uint[points.Length];
            Array.Fill(none, 0xFFFFFFFFu);
            return none;
        }
        private static int R(uint c) => (int)((c >> 16) & 0xFF);
        private static int G(uint c) => (int)((c >> 8) & 0xFF);
        private static int B(uint c) => (int)(c & 0xFF);

        /// <summary>Awaits a task for at most ms; logs and returns false on a timeout, so one stuck step is
        /// reported instead of hanging the run.</summary>
        private async Task<bool> Within(Task task, int ms, string what)
        {
            if (await Task.WhenAny(task, Task.Delay(ms)) == task) { await task; return true; }
            Log("   TIMED OUT after " + ms + " ms: " + what);
            return false;
        }

        private async Task Conclude()
        {
            _phase = null;
            StopWorker();
            // The phase probe stays open: the checks below read the still window through it (ReadStill).
            foreach (Task t in _bufPending.Append(_pending))
            {
                if (t != null) { await Within(t, 3000, "an outstanding update"); }
            }

            ChartNative.tmdq_chart_generate(_chart, 3, 360000);
            await RepaintCheckFrame();
            await Task.Delay(300);   // let the frame reach the glass

            Log("== composition: an Avalonia legend over the imported GPU image");
            Log("   screen probe, as used: " + (_probe?.Describe ?? "none"));
            Point inLegend = Legend.TranslatePoint(new Point(Legend.Bounds.Width - 5, Legend.Bounds.Height / 2), Stage) ?? default;
            PixelPoint legendOnScreen = Stage.PointToScreen(inLegend);
            uint overlaid = (await ReadGlass(Glass(legendOnScreen)))[0];
            Legend.IsVisible = false;
            await Task.Delay(300);
            uint chartOnly = (await ReadGlass(Glass(legendOnScreen)))[0];
            Legend.IsVisible = true;
            await Task.Delay(300);
            Log("   desktop pixel under the legend, chart alone: " + ScreenProbe.Hex(chartOnly) + "   with the legend on top: " + ScreenProbe.Hex(overlaid));
            bool legendDrawn = R(overlaid) < 90 && G(overlaid) < 90 && B(overlaid) < 90 && R(chartOnly) > 200;
            Log("   legend is drawn over the live chart on the glass: " + (legendDrawn ? "YES" : "NO"));

            uint[] px = await ReadGlass(Glass(Chart.PointToScreen(new Point(PlotW - 20, 5))),
                                        Glass(Chart.PointToScreen(new Point(PlotW - 20, PlotH - 5))),
                                        Glass(Chart.PointToScreen(new Point(20, 5))));
            uint topRight = px[0], bottomRight = px[1], marker = px[2];
            bool green(uint c) => c != 0xFFFFFFFF && R(c) < 80 && G(c) > 180 && B(c) < 80;
            Log("   orientation block (green, top right only): top right " + ScreenProbe.Hex(topRight) + ", bottom right " + ScreenProbe.Hex(bottomRight) +
                " - image is the right way up: " + (green(topRight) && !green(bottomRight) ? "YES" : "NO"));
            Log(string.Format("   frame marker read back from the desktop: R={0:X2} G={1:X2} B={2:X2} (written EF BE A5)",
                              R(marker), G(marker), B(marker)));
            await SaveScreenshot();

            // Resize: the textures are recreated, so the old imports are released and the new shared
            // handles imported again.
            bool resized = true;
            foreach (ICompositionImportedGpuImage old in _images)
            {
                resized &= await Within(old.DisposeAsync().AsTask(), 3000, "disposing an old imported image");
            }
            resized &= ChartNative.tmdq_chart_resize(_chart, 900, 500) == 0;
            if (resized)
            {
                resized &= await Within(ImportAll(900, 500), 3000, "importing the resized textures");
                ChartNative.tmdq_chart_select(_chart, 0);
                ChartNative.tmdq_chart_render(_chart, 0, 72000, 5, 35, 1, 1, out ChartTiming _);
                resized &= await Within(Present(0), 3000, "presenting into a resized texture");
            }
            Log("   resize to 900x500 and re-import the textures: " + (resized ? "ok" : "FAILED " + ChartNative.Error(_chart)) +
                (resized ? ", any image lost: " + _images.Any(i => i.IsLost) : ""));

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
            PixelPoint o = this.PointToScreen(new Point(0, 0));
            int w = (int)Math.Ceiling(ClientSize.Width * RenderScaling), h = (int)Math.Ceiling(ClientSize.Height * RenderScaling);
            byte[] pixels = _probe == null ? null : await _probe.ReadStillRegion(RepaintCheckFrame, o.X, o.Y, w, h);
            if (pixels == null) { Log("   screenshot FAILED: " + (_probe?.Error ?? "no screen probe")); return; }

            string path = Path.Combine(AppContext.BaseDirectory, "screenshot.png");
            GCHandle pin = GCHandle.Alloc(pixels, GCHandleType.Pinned);
            try
            {
                using var wb = new WriteableBitmap(PixelFormat.Bgra8888, AlphaFormat.Opaque, pin.AddrOfPinnedObject(),
                                                   new PixelSize(w, h), new Vector(96, 96), w * 4);
                wb.Save(path, new PngBitmapEncoderOptions());
            }
            finally { pin.Free(); }
            Log("   screenshot of the window as composed: " + path);
        }

        private void Finish(int code)
        {
            StopWorker();
            _phase = null;
            _probe?.Dispose();
            _probe = null;
            string path = Path.Combine(AppContext.BaseDirectory, "results.txt");
            File.WriteAllText(path, _report.ToString());
            Console.WriteLine("wrote " + path);
            if (_chart != IntPtr.Zero) { ChartNative.tmdq_chart_destroy(_chart); _chart = IntPtr.Zero; }
            (Avalonia.Application.Current?.ApplicationLifetime as IClassicDesktopStyleApplicationLifetime)?.Shutdown(code);
        }
    }
}
