using System;
using Avalonia;

namespace Tmdq.AvaloniaSpike
{
    internal static class Program
    {
        [STAThread]
        public static int Main(string[] args) => BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);

        // Default platform detection: on Windows that is Win32 with ANGLE (OpenGL ES over Direct3D 11),
        // which is the backend whose GPU interop imports a D3D11 shared texture.
        public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>().UsePlatformDetect();
    }
}
