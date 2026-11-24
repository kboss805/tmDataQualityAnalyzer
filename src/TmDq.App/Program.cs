using System;
using Avalonia;
using Avalonia.Threading;
using TmDq.App.Models.Logging;
using TmDq.App.Platform;

namespace TmDq.App;

internal static class Program
{
    [STAThread]
    public static void Main(string[] args)
    {
        // Before anything else can fail: a crash leaves a report (CrashHandling). The installer's smoke test has no one to
        // dismiss a message box, so it gets the report and no box.
        var crash = CrashHandling.Install(
            new CrashReporter(CrashReporter.FolderFor(AppContext.BaseDirectory, Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData)),
                              AppInfo.Version),
            Startup.SmokeReportPath() is null ? text => NativeMessageBox.Show("tmDataQualityAnalyzer", text) : null);
        BuildAvaloniaApp()
            .AfterSetup(_ => Dispatcher.UIThread.UnhandledException += (_, e) => crash.Raised("an unhandled exception on the UI thread", e.Exception))
            .StartWithClassicDesktopLifetime(args);
    }

    /// <summary>The application as the desktop runs it. The headless tests configure the same App class
    /// on their own platform (TmDq.App.Tests), so they measure what a user gets.</summary>
    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>()
                  .UsePlatformDetect()
                  .LogToTrace();
}
