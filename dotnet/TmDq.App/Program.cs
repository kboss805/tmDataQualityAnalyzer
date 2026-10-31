using System;
using Avalonia;

namespace TmDq.App
{
    internal static class Program
    {
        [STAThread]
        public static void Main(string[] args) =>
            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);

        /// <summary>The application as the desktop runs it. The headless tests configure the same App class
        /// on their own platform (TmDq.App.Tests), so they measure what a user gets.</summary>
        public static AppBuilder BuildAvaloniaApp() =>
            AppBuilder.Configure<App>()
                      .UsePlatformDetect()
                      .LogToTrace();
    }
}
