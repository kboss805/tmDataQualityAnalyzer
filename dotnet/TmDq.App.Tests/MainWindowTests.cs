using Avalonia.Controls;
using Avalonia.Headless.XUnit;
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
