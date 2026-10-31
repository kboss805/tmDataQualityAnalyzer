using System;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
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

        /// <summary>A control's bounds in its window's coordinates.</summary>
        public static Rect InWindow(Control control, Visual window)
        {
            Point topLeft = control.TranslatePoint(new Point(0, 0), window) ?? throw new InvalidOperationException(control.Name + " is not in the window");
            return new Rect(topLeft, control.Bounds.Size);
        }
    }
}
