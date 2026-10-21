using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Tmdq.Spike
{
    /// <summary>
    /// Reads the composed desktop through DXGI Desktop Duplication (native\tmdq_chart.dll). Linked into
    /// both the WPF and the Avalonia spike so they are judged by the same code.
    ///
    /// GDI cannot do this job for both: GetPixel / BitBlt on the screen DC see WPF's output but return
    /// black for a window composed through DirectComposition, which is how Avalonia draws on Windows.
    /// Desktop Duplication reads what is on the glass whoever drew it, and every pixel asked for in one
    /// call comes from the same desktop frame.
    ///
    /// Colours are 0x00RRGGBB.
    /// </summary>
    internal sealed class ScreenProbe : IDisposable
    {
        private const string Dll = "tmdq_chart.dll";
        [DllImport(Dll)] private static extern IntPtr tmdq_capture_create(int x, int y);
        [DllImport(Dll)] private static extern IntPtr tmdq_capture_error(IntPtr p);
        [DllImport(Dll)] private static extern void tmdq_capture_destroy(IntPtr p);
        [DllImport(Dll)] private static extern IntPtr tmdq_capture_describe(IntPtr p);
        [DllImport(Dll)] private static extern int tmdq_capture_pixels(IntPtr p, int n, int[] xy, uint[] rgb, int timeoutMs);
        [DllImport(Dll)] private static extern int tmdq_capture_region(IntPtr p, int x, int y, int w, int h, byte[] bgra, int timeoutMs);

        private IntPtr _p;

        private ScreenProbe(IntPtr p) { _p = p; }

        /// <summary>Opens duplication on the monitor containing desktop point (x, y). Reads wait for the
        /// desktop to compose a new frame; see ReadStill for a window that is not animating.</summary>
        public static ScreenProbe Open(int x, int y, out string error)
        {
            IntPtr p = tmdq_capture_create(x, y);
            error = p == IntPtr.Zero ? "out of memory" : Marshal.PtrToStringUTF8(tmdq_capture_error(p)) ?? "";
            if (p != IntPtr.Zero && error.Length > 0) { tmdq_capture_destroy(p); p = IntPtr.Zero; }
            return p == IntPtr.Zero ? null : new ScreenProbe(p);
        }

        public string Error => _p == IntPtr.Zero ? "closed" : Marshal.PtrToStringUTF8(tmdq_capture_error(_p)) ?? "";

        /// <summary>How the desktop is being read: the duplication call, the output's colour space and bit
        /// depth, and the format frames arrive in. On an Advanced Color (HDR / auto colour managed) desktop the
        /// colours read back are the desktop's, after the system's colour handling - close to, not equal to, what
        /// an app wrote. Checks therefore compare pixels with each other, or against generous thresholds.</summary>
        public string Describe => _p == IntPtr.Zero ? "closed" : Marshal.PtrToStringUTF8(tmdq_capture_describe(_p)) ?? "";

        /// <summary>Reads the pixels at xy (x0, y0, x1, y1, ...) from ONE desktop frame. Returns 1 when
        /// read, 0 when the desktop showed no new frame within timeoutMs, -1 on error.</summary>
        public int Pixels(int[] xy, uint[] rgb, int timeoutMs) => tmdq_capture_pixels(_p, xy.Length / 2, xy, rgb, timeoutMs);

        /// <summary>A desktop rectangle as top-down BGRA rows; null if no frame or an error.</summary>
        public byte[] Region(int x, int y, int w, int h, int timeoutMs)
        {
            var bgra = new byte[w * h * 4];
            return tmdq_capture_region(_p, x, y, w, h, bgra, timeoutMs) == 1 ? bgra : null;
        }

        /// <summary>Reads pixels of a window that is not animating. Duplication only delivers a frame when the
        /// desktop changes (a fresh duplication's first frame carries no image at all), so this drains what is
        /// waiting, asks the app to present again - which makes the desktop compose a new frame showing the
        /// window's current state - and reads that frame. 0xFFFFFFFF for each pixel if nothing arrived.</summary>
        public async Task<uint[]> ReadStill(Func<Task> repaint, params (int X, int Y)[] points)
        {
            var xy = new int[points.Length * 2];
            for (int i = 0; i < points.Length; ++i) { xy[2 * i] = points[i].X; xy[2 * i + 1] = points[i].Y; }
            var rgb = new uint[points.Length];
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                while (Pixels(xy, rgb, 0) == 1) { }
                await repaint();
                await Task.Delay(50);
                if (Pixels(xy, rgb, 250) == 1) { return rgb; }
            }
            Array.Fill(rgb, 0xFFFFFFFFu);
            return rgb;
        }

        /// <summary>The same for a rectangle (top-down BGRA rows); null if nothing arrived.</summary>
        public async Task<byte[]> ReadStillRegion(Func<Task> repaint, int x, int y, int w, int h)
        {
            var one = new uint[1];
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                while (Pixels(new[] { x, y }, one, 0) == 1) { }
                await repaint();
                await Task.Delay(50);
                byte[] got = Region(x, y, w, h, 250);
                if (got != null) { return got; }
            }
            return null;
        }

        public static string Hex(uint rgb) => rgb == 0xFFFFFFFF ? "(unreadable)" : string.Format("#{0:X6}", rgb & 0xFFFFFF);

        /// <summary>The chart stamps the same colour at the top and the bottom of every frame, and a different
        /// one each frame. A desktop frame that shows two different colours there showed the top of one chart
        /// frame and the bottom of another. Compared with each other rather than against the colour written,
        /// because the desktop's colour handling may legitimately change the value.</summary>
        public static bool SameFrame(uint top, uint bottom) => (top & 0xFFFFFF) == (bottom & 0xFFFFFF);

        public void Dispose()
        {
            if (_p != IntPtr.Zero) { tmdq_capture_destroy(_p); _p = IntPtr.Zero; }
        }
    }
}
