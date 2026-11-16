using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Styling;
using Avalonia.Threading;
using TmDq.App.Models.Logging;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The status bar under the log and the chart: one short sentence for the last thing that happened, marked and coloured
    /// by how it went, there with the log hidden. (What each action says is held where the action is tested.)
    /// </summary>
    public class StatusBarTests
    {
        public StatusBarTests() => NativeCore.EnsureLoaded();

        private const string Csv =
            "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames\r\n" +
            "001:23:59:59.000,90.000000,0.000000\r\n" +
            "002:00:00:01.000,95.000000,1.000000\r\n";

        private static string TempCsv(string name = "")
        {
            string path = TempFiles.NewPath("statusbar", name + ".csv");
            File.WriteAllText(path, Csv, new UTF8Encoding(false));
            return path;
        }

        private static (MainWindow Window, MainWindowViewModel Vm) Show()
        {
            var vm = new MainWindowViewModel();
            var window = new MainWindow { DataContext = vm };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return (window, vm);
        }

        private static Color Token(string key, Window window)
        {
            Assert.True(Application.Current!.TryFindResource(key, window.ActualThemeVariant, out object? b), key);
            return ((ISolidColorBrush)b!).Color;
        }

        private static Color DrawnAt(ILockedFramebuffer fb, double x, double y) => Color.FromUInt32(0xFF000000 | Frames.Pixel(fb, (int)x, (int)y));

        [AvaloniaTheory]
        [InlineData(false)]
        [InlineData(true)]
        public void TheStatusBarIsTheChromeUnderAHairlineAtTheFootOfTheWindowAndStaysWithTheLogHidden(bool dark)
        {
            var app = (App)Application.Current!;
            ThemeVariant? before = app.RequestedThemeVariant;
            app.ApplyTheme(dark);
            (MainWindow window, MainWindowViewModel vm) = Show();
            try
            {
                Rect bar = Frames.InWindow(window.StatusBar, window), body = Frames.InWindow(window.Body, window);
                // Across the foot of the window, 24 px, the chart and the log ending where it begins.
                Assert.Equal((0.0, window.ClientSize.Width, 24.0, window.ClientSize.Height), (bar.Left, bar.Width, bar.Height, bar.Bottom));
                Assert.Equal(bar.Top, body.Bottom);
                // The chrome's colour under a divider's hairline: drawn.
                Color chrome = Token("SolidBackgroundFillColorBaseBrush", window), divider = Token("DividerStrokeColorDefaultBrush", window);
                using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                {
                    Assert.Equal(chrome, DrawnAt(fb, 2, bar.Top + 8));
                    Assert.Equal(divider, DrawnAt(fb, 2, bar.Top));
                }
                // It says the status, in the caption size, with the whole of it in the tooltip; the first prompt has no mark.
                Assert.Equal(("Open a Chapter 10 recording.", "Open a Chapter 10 recording.", false), (window.StatusText.Text, ToolTip.GetTip(window.StatusText), window.StatusMark.IsVisible));
                Assert.Equal((12.0, TextTrimming.CharacterEllipsis), (window.StatusText.FontSize, window.StatusText.TextTrimming));

                // With the log hidden it is the one place the outcome shows.
                vm.SidebarVisible = false;
                Dispatcher.UIThread.RunJobs();
                Assert.True(window.StatusBar.IsEffectivelyVisible);
                Assert.Equal("Open a Chapter 10 recording.", window.StatusText.Text);
            }
            finally
            {
                window.Close();
                app.RequestedThemeVariant = before;
            }
        }

        [AvaloniaTheory]
        [InlineData(false)]
        [InlineData(true)]
        public async Task ASuccessAndAnErrorAreMarkedInFluentsStatusColours(bool dark)
        {
            var app = (App)Application.Current!;
            ThemeVariant? before = app.RequestedThemeVariant;
            app.ApplyTheme(dark);
            (MainWindow window, MainWindowViewModel vm) = Show();
            string csv = TempCsv();
            try
            {
                await vm.ImportCsvAsync(csv);
                Dispatcher.UIThread.RunJobs();
                Assert.Equal(("Imported " + Path.GetFileName(csv) + ".", "✓", true), (window.StatusText.Text, window.StatusMark.Text, window.StatusMark.IsVisible));
                Assert.Equal(Token("SystemFillColorSuccessBrush", window), ((ISolidColorBrush)window.StatusMark.Foreground!).Color);

                // An error: the sentence, a cross in the critical colour. (The text stays the text colour.)
                Assert.Null(vm.ReadTemplate(Path.Combine(Path.GetTempPath(), "tmdq-no-such-template.json")));
                Dispatcher.UIThread.RunJobs();
                Assert.Equal(("Could not read the template. See the log.", "✗"), (window.StatusText.Text, window.StatusMark.Text));
                Assert.Equal(Token("SystemFillColorCriticalBrush", window), ((ISolidColorBrush)window.StatusMark.Foreground!).Color);
                Assert.Equal(Token("TextFillColorPrimaryBrush", window), ((ISolidColorBrush)window.StatusText.Foreground!).Color);

                // And it is drawn: the mark's area differs from the bar's background.
                Rect mark = Frames.InWindow(window.StatusMark, window);
                using ILockedFramebuffer fb = Frames.Capture(window).Lock();
                Assert.True(Frames.CountDifferent(fb, mark, Token("SolidBackgroundFillColorBaseBrush", window).ToUInt32() & 0xFFFFFF) > 5, "the mark is not drawn");
            }
            finally
            {
                TempFiles.Delete(csv);
                window.Close();
                app.RequestedThemeVariant = before;
            }
        }

        [Fact]
        public void EachLevelHasItsMarkAndItsStatusColourAndInformationHasNeither()
        {
            Assert.Equal(new[] { "", "✓", "⚠", "✗" }, new[] { LogLevel.Info, LogLevel.Success, LogLevel.Warning, LogLevel.Error }.Select(StatusPaint.MarkOf));
            Assert.Equal(new StatusLevel?[] { null, StatusLevel.Success, StatusLevel.Caution, StatusLevel.Critical },
                         new[] { LogLevel.Info, LogLevel.Success, LogLevel.Warning, LogLevel.Error }.Select(LogView.StatusOf));
        }

        [AvaloniaFact]
        public async Task ASentenceLongerThanTheBarIsTrimmedAndTheTooltipHasAllOfIt()
        {
            string csv = TempCsv(new string('x', 150));
            (MainWindow window, MainWindowViewModel vm) = Show();
            try
            {
                window.Width = 700;
                await vm.ImportCsvAsync(csv);
                Dispatcher.UIThread.RunJobs();
                Assert.Equal("Imported " + Path.GetFileName(csv) + ".", ToolTip.GetTip(window.StatusText));
                Rect bar = Frames.InWindow(window.StatusBar, window), text = Frames.InWindow(window.StatusText, window);
                Assert.True(text.Right <= bar.Right, $"the text ({text.Right}) runs past the bar ({bar.Right})");
                Assert.True(window.StatusText.TextLayout.TextLines[0].HasCollapsed, "the long sentence was not collapsed to an ellipsis");
            }
            finally
            {
                TempFiles.Delete(csv);
                window.Close();
            }
        }
    }
}
