using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using SkiaSharp;

namespace Tmdq.AvaloniaSpike
{
    /// <summary>
    /// One frame of the chart, as geometry: per series, the line segments and the min-max band polygon
    /// that tmdq_chart.dll built (the same culling and per-pixel-column envelope the Direct2D chart draws).
    /// Immutable once built, because the render thread draws it while the UI thread builds the next one.
    /// </summary>
    internal sealed class ChartFrame : IDisposable
    {
        public SKPoint[][] Segments;   // two points per segment, for DrawPoints(Lines)
        public SKPath[] Bands;         // null where a series was not decimated
        public uint Marker;
        public int Width, Height;

        /// <summary>Copies the chart's last build out of native memory.</summary>
        public static unsafe ChartFrame FromNative(IntPtr chart, int series, int w, int h, uint marker)
        {
            var f = new ChartFrame { Segments = new SKPoint[series][], Bands = new SKPath[series], Marker = marker, Width = w, Height = h };
            for (int i = 0; i < series; ++i)
            {
                IntPtr seg = ChartNative.tmdq_chart_segments(chart, i, out int segCount);
                var pts = new SKPoint[segCount * 2];
                if (segCount > 0)
                {
                    fixed (SKPoint* dst = pts) { Buffer.MemoryCopy((void*)seg, dst, pts.Length * sizeof(SKPoint), segCount * 4L * sizeof(float)); }
                }
                f.Segments[i] = pts;

                IntPtr band = ChartNative.tmdq_chart_band(chart, i, out int bandCount);
                if (bandCount >= 3)
                {
                    var p = (float*)band;
                    var path = new SKPath();
                    path.MoveTo(p[0], p[1]);
                    for (int k = 1; k < bandCount; ++k) { path.LineTo(p[2 * k], p[2 * k + 1]); }
                    path.Close();
                    f.Bands[i] = path;
                }
            }
            return f;
        }

        public void Dispose()
        {
            foreach (SKPath p in Bands ?? Array.Empty<SKPath>()) { p?.Dispose(); }
            Bands = null;
        }
    }

    /// <summary>
    /// The one drawing routine: the screen, the raster benchmark and the PNG / PDF / SVG exports all call
    /// it, which is the property that fixed the dropped PDF legend in v2.9.0 (one renderer, every format).
    /// Draws what tmdq_chart.cpp draws, with the same colours, widths and markers.
    /// </summary>
    internal static class ChartPainter
    {
        private static readonly SKColor Pen = new SKColor(107, 97, 230);
        private static readonly SKColor Fill = new SKColor(107, 97, 230, 64);

        public static void Draw(SKCanvas c, ChartFrame f)
        {
            c.Save();
            c.ClipRect(new SKRect(0, 0, f.Width, f.Height));
            c.Clear(SKColors.White);
            using var fill = new SKPaint { Color = Fill, Style = SKPaintStyle.Fill, IsAntialias = true };
            using var outline = new SKPaint { Color = Pen, Style = SKPaintStyle.Stroke, StrokeWidth = 1.0f, IsAntialias = true };
            using var line = new SKPaint { Color = Pen, Style = SKPaintStyle.Stroke, StrokeWidth = 1.5f, IsAntialias = true };
            for (int i = 0; i < f.Segments.Length; ++i)
            {
                SKPath band = f.Bands[i];
                if (band != null)
                {
                    c.DrawPath(band, fill);
                    c.DrawPath(band, outline);
                }
                if (f.Segments[i].Length > 0) { c.DrawPoints(SKPointMode.Lines, f.Segments[i], line); }
            }
            // Frame marker (R = low byte, G = next byte, B = 0xA5) at the top and bottom left, and the green
            // orientation block at the top right - the same as the native chart, so the frames compare.
            using var marker = new SKPaint { Color = new SKColor((byte)(f.Marker & 0xFF), (byte)((f.Marker >> 8) & 0xFF), 0xA5), Style = SKPaintStyle.Fill };
            c.DrawRect(new SKRect(0, 0, 40, 10), marker);
            c.DrawRect(new SKRect(0, f.Height - 10, 40, f.Height), marker);
            using var orient = new SKPaint { Color = new SKColor(0, 255, 0), Style = SKPaintStyle.Fill };
            c.DrawRect(new SKRect(f.Width - 40, 0, f.Width, 10), orient);
            c.Restore();
        }
    }

    /// <summary>What the render thread measured for one frame.</summary>
    internal readonly struct RenderSample
    {
        public RenderSample(double issueMs, double gpuMs) { IssueMs = issueMs; GpuMs = gpuMs; }
        public double IssueMs { get; }
        public double GpuMs { get; }
    }

    /// <summary>
    /// The chart as an Avalonia control: its content is drawn by Skia on Avalonia's render thread through
    /// a custom draw operation, so it is an ordinary visual (the legend over it needs nothing special) and
    /// renders wherever Avalonia renders, the headless test platform included.
    /// </summary>
    public sealed class SkiaChart : Control
    {
        private ChartOp _op;

        /// <summary>Raised on the render thread when a frame has been drawn (once per frame).</summary>
        internal event Action<RenderSample, string> Rendered;

        /// <summary>When true, each frame waits for the GPU to finish (the counterpart of the D2D fence).</summary>
        internal bool WaitForGpu { get; set; } = true;

        internal void Show(ChartFrame frame)
        {
            _op = new ChartOp(this, frame, WaitForGpu);
            InvalidateVisual();
        }

        public override void Render(DrawingContext context)
        {
            if (_op != null) { context.Custom(_op); }
        }

        internal void OnRendered(RenderSample s, string backend) => Rendered?.Invoke(s, backend);

        private sealed class ChartOp : ICustomDrawOperation
        {
            private readonly SkiaChart _owner;
            private readonly ChartFrame _frame;
            private readonly bool _waitForGpu;
            private int _done;

            public ChartOp(SkiaChart owner, ChartFrame frame, bool waitForGpu)
            {
                _owner = owner; _frame = frame; _waitForGpu = waitForGpu;
                Bounds = new Rect(0, 0, frame.Width, frame.Height);
            }

            public Rect Bounds { get; }
            public bool HitTest(Point p) => false;
            public bool Equals(ICustomDrawOperation other) => ReferenceEquals(this, other);

            public void Render(ImmediateDrawingContext context)
            {
                var feature = context.TryGetFeature<ISkiaSharpApiLeaseFeature>();
                if (feature == null) { return; }
                using ISkiaSharpApiLease lease = feature.Lease();
                var sw = Stopwatch.StartNew();
                ChartPainter.Draw(lease.SkCanvas, _frame);
                double issue = sw.Elapsed.TotalMilliseconds;
                double gpu = 0;
                if (_waitForGpu && lease.GrContext != null)
                {
                    sw.Restart();
                    lease.GrContext.Flush(true, true);   // submit and wait: the GPU has finished this frame
                    gpu = sw.Elapsed.TotalMilliseconds;
                }
                // Avalonia may re-render a still scene; only the first draw of a frame is a frame.
                if (System.Threading.Interlocked.Exchange(ref _done, 1) == 0)
                {
                    string backend = lease.GrContext != null ? "GPU (" + lease.GrContext.Backend + ")" : "CPU raster";
                    _owner.OnRendered(new RenderSample(issue, gpu), backend);
                }
            }

            public void Dispose() => _frame.Dispose();
        }
    }
}
