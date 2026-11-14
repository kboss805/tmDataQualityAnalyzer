using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.LogicalTree;
using Avalonia.Platform;
using Avalonia.Threading;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>The main window as the skeleton has it (plan step 3.1): its title, the version its log opens with, and
    /// its size, laid out by the real theme.</summary>
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
        public void TheWindowIsTitledTmDataQualityAnalyzer()
        {
            MainWindow window = Show();
            Assert.Equal("TM Data Quality Analyzer", window.Title);
            window.Close();
        }

        [AvaloniaFact]
        public void TheVersionTheBuildReadFromTheHeaderIsTheLogsFirstLineAndThereIsNoStatusBar()
        {
            var vm = new MainWindowViewModel();
            var window = new MainWindow { DataContext = vm };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("tmDataQualityAnalyzer v" + VersionTests.HeaderVersion(), vm.Log.Entries[0].Text);
            // No status bar (it said what the log says): nothing under the log and the chart, which reach the window's
            // bottom but for the log panel's margin.
            Assert.Null(window.FindControl<TextBlock>("StatusText"));
            Assert.Null(window.FindControl<TextBlock>("VersionLabel"));
            Assert.Equal(window.ClientSize.Height, Frames.InWindow(window.Body, window).Bottom, 0.5);
            window.Close();
        }

        [AvaloniaFact]
        public void TheLogIsDrawnNotOnlyLaidOut()
        {
            // A rendered frame, through Skia, read pixel by pixel: the log's first line holds the drawn text and an empty
            // patch of the window does not. Layout alone cannot show this, and on the CI runner (a service in
            // Session 0, no desktop) it is the proof that the headless platform really renders there.
            MainWindow window = Show();
            Border log = window.FindControl<Border>("LogPanel")!;
            Control text = window.FindControl<LogView>("Log")!.GetLogicalDescendants().OfType<SelectableTextBlock>().Single();
            var chart = (PlotChart)window.FindControl<PlotView>("PlotView")!.Chart;
            using ILockedFramebuffer fb = Frames.Capture(window).Lock();
            Rect panel = Frames.InWindow(log, window), t = Frames.InWindow(text, window);
            uint logBackground = Frames.Pixel(fb, (int)panel.Center.X, (int)(panel.Bottom - 20));
            int inFirstLine = Frames.CountDifferent(fb, new Rect(t.X, t.Y, t.Width, 16), logBackground);
            // The chart plots nothing until a recording is processed, so a cell of its empty grid is a plain patch.
            Rect area = chart.PlotArea, inChart = Frames.InWindow(chart, window);
            double cellW = area.Width / (PlotChart.XTickCount - 1), cellH = area.Height / (PlotChart.YTickCount - 1);
            var cell = new Rect(inChart.X + area.X + 4 * cellW, inChart.Y + area.Y + 2 * cellH, cellW, cellH).Deflate(3);
            int inEmpty = Frames.CountDifferent(fb, cell, Frames.Pixel(fb, (int)cell.Center.X, (int)cell.Center.Y));

            // "hh:mm:ss  tmDataQualityAnalyzer v3.1.0" in the default font covers well over a hundred pixels.
            Assert.True(inFirstLine > 100, $"only {inFirstLine} pixels of the log's first line differ from its background");
            Assert.Equal(0, inEmpty);
            window.Close();
        }

        [AvaloniaFact]
        public void TheWindowOpensAtItsDefaultSizeAndCannotShrinkPastItsMinimum()
        {
            // v2.x's size, centred, on a screen that holds it (the headless platform's is larger).
            MainWindow window = Show();
            Assert.Equal(new Size(1920, 1080), window.ClientSize);
            Assert.Equal(WindowStartupLocation.CenterScreen, window.WindowStartupLocation);
            Assert.Equal(640, window.MinWidth);
            Assert.Equal(400, window.MinHeight);
            window.Close();
        }

        [Theory]
        [InlineData(2560, 1392, 1.0, 1920, 1080)]   // a 1440p screen, its taskbar below: the default
        [InlineData(3840, 2112, 1.5, 1920, 1080)]   // 4K at 150 %: 2560 x 1408 units, the default
        [InlineData(1920, 1032, 1.0, 1920, 1032)]   // a 1080p screen: as wide, and down to the taskbar
        [InlineData(1920, 1032, 1.25, 1536, 825)]   // 1080p at 125 %: the whole working area, no more
        [InlineData(1366, 728, 1.0, 1366, 728)]     // a small laptop screen
        [InlineData(0, 0, 1.0, 1920, 1080)]         // no working area reported: the default
        public void TheDefaultSizeFitsTheScreensWorkingArea(int width, int height, double scaling, double expectWidth, double expectHeight)
        {
            Assert.Equal(new Size(expectWidth, expectHeight), MainWindow.FitDefaultSize(new PixelRect(0, 0, width, height), scaling));
        }
    }
}
