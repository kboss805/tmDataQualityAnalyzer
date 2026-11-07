using System;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>Reading a rendered frame: what a window drew, through Skia, pixel by pixel.</summary>
    internal static class Frames
    {
        public static WriteableBitmap Capture(TopLevel window)
        {
            WriteableBitmap? frame = window.CaptureRenderedFrame();
            Assert.NotNull(frame);
            return frame!;
        }

        /// <summary>One pixel as 0x00RRGGBB, whatever the frame's byte order.</summary>
        public static uint Pixel(ILockedFramebuffer fb, int x, int y)
        {
            uint v = (uint)Marshal.ReadInt32(fb.Address, y * fb.RowBytes + x * 4);
            if (fb.Format == PixelFormat.Rgba8888) v = (v & 0xFF00FF00) | ((v & 0xFF) << 16) | ((v >> 16) & 0xFF);
            return v & 0xFFFFFF;
        }

        /// <summary>How many pixels of <paramref name="area"/> (in window coordinates) differ from <paramref name="background"/>.</summary>
        public static int CountDifferent(ILockedFramebuffer fb, Rect area, uint background)
        {
            int count = 0;
            for (int y = (int)area.Top; y < (int)area.Bottom; y++)
                for (int x = (int)area.Left; x < (int)area.Right; x++)
                    if (Pixel(fb, x, y) != background) count++;
            return count;
        }

        /// <summary>Pixels of <paramref name="area"/> drawn in <paramref name="colour"/> over a white background. An
        /// antialiased line or glyph is the colour blended with white, so a pixel is counted when its departure from white
        /// points the same way as the colour's (within a few degrees), not when it is the colour itself.</summary>
        public static int Near(ILockedFramebuffer fb, Rect area, Color colour)
        {
            double cr = 255 - colour.R, cg = 255 - colour.G, cb = 255 - colour.B;
            double cn = Math.Sqrt(cr * cr + cg * cg + cb * cb);
            int count = 0;
            for (int y = (int)area.Top; y < (int)area.Bottom; y++)
                for (int x = (int)area.Left; x < (int)area.Right; x++)
                {
                    uint p = Pixel(fb, x, y);
                    double r = 255 - (int)((p >> 16) & 0xFF), g = 255 - (int)((p >> 8) & 0xFF), b = 255 - (int)(p & 0xFF);
                    double n = Math.Sqrt(r * r + g * g + b * b);
                    if (n > 40 && (r * cr + g * cg + b * cb) / (n * cn) > 0.995) count++;
                }
            return count;
        }

        /// <summary>A control's bounds in its window's coordinates.</summary>
        public static Rect InWindow(Control control, Visual window)
        {
            Point topLeft = control.TranslatePoint(new Point(0, 0), window) ?? throw new InvalidOperationException(control.Name + " is not in the window");
            return new Rect(topLeft, control.Bounds.Size);
        }
    }
}
