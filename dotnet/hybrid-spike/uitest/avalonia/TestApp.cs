using Avalonia;
using Avalonia.Headless;
using Avalonia.Themes.Fluent;
using Tmdq.UiSpike.Ava;

[assembly: AvaloniaTestApplication(typeof(TestApp))]

namespace Tmdq.UiSpike.Ava
{
    /// <summary>
    /// The headless platform the tests run on: no window system, no GPU, no desktop session. Drawing goes
    /// through Skia rather than the headless stub (UseHeadlessDrawing = false), so a test can capture a
    /// real rendered frame, with real text, and read its pixels. The Fluent theme is the one the app ships,
    /// so a test measures what a user would get, theme sizing included.
    /// </summary>
    public sealed class TestApp : Application
    {
        public override void Initialize() => Styles.Add(new FluentTheme());

        public static AppBuilder BuildAvaloniaApp() =>
            AppBuilder.Configure<TestApp>()
                      .UseSkia()
                      .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false });
    }
}
