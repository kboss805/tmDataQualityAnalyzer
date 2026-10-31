using System;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>The main window as the skeleton has it (plan step 3.1): its title, and the version it shows,
    /// laid out by the real theme.</summary>
    public sealed class MainWindowTests
    {
        private static MainWindow Show()
        {
            var window = new MainWindow { DataContext = new MainWindowViewModel() };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return window;
        }

        [AvaloniaFact]
        public void TheWindowIsTitledAsTheQtApplicationIs()
        {
            MainWindow window = Show();
            Assert.Equal("TM Data Quality Analyzer", window.Title);
            window.Close();
        }

        [AvaloniaFact]
        public void TheWindowShowsTheVersionTheBuildReadFromTheHeader()
        {
            MainWindow window = Show();
            TextBlock label = window.FindControl<TextBlock>("VersionLabel")!;
            Assert.Equal("Version " + VersionTests.HeaderVersion(), label.Text);
            // Laid out, not merely bound: the label has a size and sits inside the window.
            Assert.True(label.Bounds.Width > 20 && label.Bounds.Height > 5, $"label {label.Bounds}");
            Assert.True(label.Bounds.Right <= window.ClientSize.Width && label.Bounds.Bottom <= window.ClientSize.Height);
            window.Close();
        }

        [AvaloniaFact]
        public void TheVersionIsDrawnNotOnlyLaidOut()
        {
            // A rendered frame, through Skia, read pixel by pixel: the label's box holds the drawn text and an empty
            // patch of the window does not. Layout alone cannot show this, and on the CI runner (a service in
            // Session 0, no desktop) it is the proof that the headless platform really renders there.
            MainWindow window = Show();
            TextBlock label = window.FindControl<TextBlock>("VersionLabel")!;
            WriteableBitmap? frame = window.CaptureRenderedFrame();
            Assert.NotNull(frame);
            Point topLeft = label.TranslatePoint(new Point(0, 0), window) ?? throw new InvalidOperationException("label not in window");

            using ILockedFramebuffer fb = frame!.Lock();
            uint background = Pixel(fb, 5, 5);
            int inLabel = 0;
            for (int y = (int)topLeft.Y; y < (int)(topLeft.Y + label.Bounds.Height); y++)
                for (int x = (int)topLeft.X; x < (int)(topLeft.X + label.Bounds.Width); x++)
                    if (Pixel(fb, x, y) != background) inLabel++;
            int inEmpty = 0;
            for (int y = 0; y < 60; y++)
                for (int x = 0; x < 200; x++)
                    if (Pixel(fb, x, y) != background) inEmpty++;

            // "Version 2.14.2" in the default font covers well over a hundred pixels; nothing is drawn top-left.
            Assert.True(inLabel > 100, $"only {inLabel} pixels in the label's box differ from the background");
            Assert.Equal(0, inEmpty);
            window.Close();
        }

        /// <summary>One pixel as 0x00RRGGBB, whatever the frame's byte order.</summary>
        private static uint Pixel(ILockedFramebuffer fb, int x, int y)
        {
            uint v = (uint)Marshal.ReadInt32(fb.Address, y * fb.RowBytes + x * 4);
            if (fb.Format == PixelFormat.Rgba8888) v = (v & 0xFF00FF00) | ((v & 0xFF) << 16) | ((v >> 16) & 0xFF);
            return v & 0xFFFFFF;
        }

        [AvaloniaFact]
        public void TheWindowOpensAtItsDefaultSizeAndCannotShrinkPastItsMinimum()
        {
            MainWindow window = Show();
            Assert.Equal(1280, window.ClientSize.Width);
            Assert.Equal(800, window.ClientSize.Height);
            Assert.Equal(640, window.MinWidth);
            Assert.Equal(400, window.MinHeight);
            window.Close();
        }
    }
}
