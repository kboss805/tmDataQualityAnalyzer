using Avalonia;
using Avalonia.Headless;
using TmDq.App.Tests;

[assembly: AvaloniaTestApplication(typeof(TestAppBuilder))]

namespace TmDq.App.Tests
{
    /// <summary>
    /// The application's own App class (its theme and styles) on the headless platform. Drawing goes through
    /// Skia rather than the headless stub (UseHeadlessDrawing = false), so a test can capture a rendered frame
    /// and measure real text. Under the headless lifetime App creates no main window; a test makes its own.
    /// </summary>
    public static class TestAppBuilder
    {
        public static AppBuilder BuildAvaloniaApp() =>
            AppBuilder.Configure<App>()
                      .UseSkia()
                      .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false });
    }
}
