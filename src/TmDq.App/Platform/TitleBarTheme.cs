using System;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Styling;

namespace TmDq.App.Platform;

/// <summary>
/// Makes the title bar of every window follow the application's theme (US9.0), on the Windows versions where Avalonia does
/// not. Avalonia asks Windows for a dark title bar only from Windows 11 (build 22000): on Windows 10 every dialog keeps a
/// white title bar over its dark body. Windows 10 does take the request (the same DWM attribute, from 1809), so this makes it
/// for each window, as the window opens and whenever its theme changes; the main window draws its own title bar and is left to
/// do so. Windows 11 is left to Avalonia, which does it already.
/// </summary>
public static class TitleBarTheme
{
    /// <summary>The first build of Windows 11, from which Avalonia sets the title bar's theme itself.</summary>
    private const int AvaloniaSetsItFromBuild = 22000;

    // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 from Windows 10 2004 (build 19041), 19 on 1809 to 1909 (builds 17763 to 18363).
    private const int UseImmersiveDarkMode = 20;
    private const int UseImmersiveDarkModeBefore2004 = 19;

    private const uint SwpNoSize = 0x0001, SwpNoMove = 0x0002, SwpNoZOrder = 0x0004, SwpNoActivate = 0x0010, SwpFrameChanged = 0x0020;

    [DllImport("dwmapi.dll", ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);

    [DllImport("dwmapi.dll", ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out int value, int size);

    [DllImport("user32.dll", ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);

    /// <summary>Whether Windows of this build needs us to set the title bar's theme: before Windows 11.</summary>
    public static bool NeedsHelp(int build) => build < AvaloniaSetsItFromBuild;

    /// <summary>Starts following the theme for every window opened from now on, where it is needed (Windows 10).</summary>
    /// <returns>What stops it again: nothing is installed on a Windows that needs no help, and nothing then needs stopping.</returns>
    public static IDisposable Install() => Install(Environment.OSVersion.Version.Build, window => window.TryGetPlatformHandle()?.Handle);

    /// <summary>As <see cref="Install()"/>, for Windows of <paramref name="build"/>, taking each window's handle from
    /// <paramref name="handleOf"/> (null when it has none yet).</summary>
    public static IDisposable Install(int build, Func<Window, IntPtr?> handleOf)
    {
        if (!OperatingSystem.IsWindows() || !NeedsHelp(build)) return Nothing.Instance;
        // A window's theme is resolved as it is made, before it is shown, and again when the application switches theme.
        IDisposable themed = Window.ActualThemeVariantProperty.Changed.AddClassHandler<Window>(
            (window, _) => Follow(window, handleOf, redraw: window.IsVisible));
        // And once it is up, with the frame redrawn, for a window whose handle did not exist yet when its theme was resolved.
        IDisposable loaded = Control.LoadedEvent.AddClassHandler<Window>(
            (window, _) => Follow(window, handleOf, redraw: true), RoutingStrategies.Direct);
        return new Both(themed, loaded);
    }

    private static void Follow(Window window, Func<Window, IntPtr?> handleOf, bool redraw)
    {
        // The main window has no system title bar to colour: it draws its own.
        if (window.ExtendClientAreaToDecorationsHint) return;
        if (handleOf(window) is { } handle) Apply(handle, window.ActualThemeVariant == ThemeVariant.Dark, redraw);
    }

    private sealed class Nothing : IDisposable
    {
        public static readonly Nothing Instance = new();
        public void Dispose() { }
    }

    private sealed class Both(IDisposable first, IDisposable second) : IDisposable
    {
        public void Dispose()
        {
            first.Dispose();
            second.Dispose();
        }
    }

    /// <summary>Asks Windows for a dark (or light) title bar on <paramref name="window"/>, which the frame shows once it is
    /// drawn again; <paramref name="redraw"/> draws it at once, for a window that is already on the screen.</summary>
    /// <returns>Whether Windows took the request: false on a Windows that has no dark title bar to give.</returns>
    public static bool Apply(IntPtr window, bool dark, bool redraw = false)
    {
        int on = dark ? 1 : 0;
        bool taken = DwmSetWindowAttribute(window, UseImmersiveDarkMode, ref on, sizeof(int)) == 0
                     || DwmSetWindowAttribute(window, UseImmersiveDarkModeBefore2004, ref on, sizeof(int)) == 0;
        if (taken && redraw)
        {
            _ = SetWindowPos(window, IntPtr.Zero, 0, 0, 0, 0, SwpNoMove | SwpNoSize | SwpNoZOrder | SwpNoActivate | SwpFrameChanged);
        }
        return taken;
    }

    /// <summary>The title bar's theme on <paramref name="window"/> as Windows has it: true for dark, null where it cannot say.</summary>
    public static bool? IsDark(IntPtr window)
    {
        if (DwmGetWindowAttribute(window, UseImmersiveDarkMode, out int on, sizeof(int)) == 0) return on != 0;
        return DwmGetWindowAttribute(window, UseImmersiveDarkModeBefore2004, out on, sizeof(int)) == 0 ? on != 0 : null;
    }
}
