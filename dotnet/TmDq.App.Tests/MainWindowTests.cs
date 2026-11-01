using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
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
            Control chart = window.FindControl<PlotView>("PlotView")!.Chart;
            using ILockedFramebuffer fb = Frames.Capture(window).Lock();
            Rect chartArea = Frames.InWindow(chart, window);
            uint background = Frames.Pixel(fb, (int)chartArea.Center.X, (int)chartArea.Center.Y);
            int inLabel = Frames.CountDifferent(fb, Frames.InWindow(label, window), background);
            // The chart draws nothing until a recording is processed, so its middle is an empty patch of the window.
            int inEmpty = Frames.CountDifferent(fb, new Rect(chartArea.Center.X - 100, chartArea.Center.Y - 30, 200, 60), background);

            // "Version 2.14.2" in the default font covers well over a hundred pixels.
            Assert.True(inLabel > 100, $"only {inLabel} pixels in the label's box differ from the background");
            Assert.Equal(0, inEmpty);
            window.Close();
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
